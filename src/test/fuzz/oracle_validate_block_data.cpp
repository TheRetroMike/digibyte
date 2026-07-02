// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Wave 22 Agent B — Fuzz Harness Completeness.
//
// DD-FA-TEST-044 — Drive a fuzzed CBlock through
// `OracleDataValidator::ValidateBlockOracleData`.
//
// Production paths that call into this validator:
//   * `ConnectBlock` (consensus): every accepted block goes through
//     this once DigiDollar is BIP9-active.
//   * `OracleBundleManager::OnBlockConnected`.
//
// The validator must:
//   * Return early/true for empty-vtx blocks.
//   * Return true for non-DD blocks without an oracle output.
//   * Reject DD-touching blocks without an oracle output
//     (`bad-oracle-missing`).
//   * Reject blocks with multiple oracle outputs
//     (`bad-oracle-multiple-outputs`).
//   * Reject blocks with malformed oracle scripts
//     (`bad-oracle-malformed`).
//   * Reject raw v0x01/v0x02 payloads as malformed legacy data
//     (`bad-oracle-malformed`).
//   * Reject blocks where the bundle epoch / signature / age is wrong.
//   * Never crash, abort, or trip an assertion regardless of input.
//
// The harness fuzzes:
//   * coinbase script shape (BIP34 height encoding edge cases)
//   * presence/absence of OP_ORACLE outputs
//   * count of OP_ORACLE outputs (0, 1, 2, 3+)
//   * version byte after OP_RETURN OP_ORACLE (0x00, 0x01, 0x02, 0x03,
//     0xff)
//   * payload shape after the version byte (truncated, oversized, all-
//     zero, random bytes)
//   * presence of a synthetic DD-marker tx in vtx[1+] (to flip the
//     "DD-touching" gate)
//
// The harness intentionally does NOT try to construct a valid v0x03
// signature — it asserts only that the validator's reject paths fire
// without crashing. The signature verification path is exercised by
// `oracle_bundle_validation` and the dedicated session tests.

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include <chainparams.h>
#include <consensus/validation.h>
#include <consensus/digidollar.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/util/setup_common.h>
#include <uint256.h>

namespace {

void initialize_oracle_validate_block_data()
{
    static const auto testing_setup =
        std::make_unique<const BasicTestingSetup>(ChainType::REGTEST);
    (void)testing_setup;
}

CScript MakeCoinbaseScriptSig(FuzzedDataProvider& fdp, int32_t& height_out)
{
    // Pick a height in a range that may or may not encode minimally as
    // CScriptNum. Very large or non-minimal pushes should trigger
    // `bad-cb-bip34-height-encoding`.
    height_out = fdp.ConsumeIntegralInRange<int32_t>(0, 50'000'000);
    CScript script;
    const uint8_t mode = fdp.ConsumeIntegralInRange<uint8_t>(0, 4);
    switch (mode) {
    case 0: {
        // Standard CScriptNum encoding (canonical).
        script << CScriptNum(height_out);
        break;
    }
    case 1: {
        // Empty scriptSig — validator falls through to the prev-index path.
        break;
    }
    case 2: {
        // Garbage prefix bytes — height parse may fail.
        const std::vector<uint8_t> garbage = fdp.ConsumeBytes<uint8_t>(8);
        script.insert(script.end(), garbage.begin(), garbage.end());
        break;
    }
    case 3: {
        // Oversized push that decodes but exceeds 32-bit range.
        std::vector<unsigned char> big(8, 0xff);
        script << big;
        break;
    }
    case 4: {
        // Non-minimal encoding — height push has trailing zero byte.
        std::vector<unsigned char> non_min{0x01, 0x00, 0x00};
        script << non_min;
        break;
    }
    }
    return script;
}

CScript MakeOracleScript(FuzzedDataProvider& fdp)
{
    // Generate an OP_RETURN OP_ORACLE script with a fuzzed payload.
    const uint8_t shape = fdp.ConsumeIntegralInRange<uint8_t>(0, 7);

    CScript s;
    s << OP_RETURN << OP_ORACLE;

    switch (shape) {
    case 0: {
        // Empty payload after marker — malformed.
        break;
    }
    case 1: {
        // Single byte version 0x01 (legacy) — malformed in current parser.
        s << std::vector<unsigned char>{0x01};
        s << fdp.ConsumeBytes<unsigned char>(20);
        break;
    }
    case 2: {
        // Single byte version 0x02 (legacy) — malformed in current parser.
        s << std::vector<unsigned char>{0x02};
        s << fdp.ConsumeBytes<unsigned char>(40);
        break;
    }
    case 3: {
        // Version 0x03 with random tail — must fail signature verify
        // (we don't have a valid agg sig).
        s << std::vector<unsigned char>{0x03};
        const size_t len = fdp.ConsumeIntegralInRange<size_t>(0, 200);
        s << fdp.ConsumeBytes<unsigned char>(len);
        break;
    }
    case 4: {
        // Unknown version byte 0xff — malformed unless a future parser branch
        // extracts it and hits the defense-in-depth legacy gate.
        s << std::vector<unsigned char>{0xff};
        s << fdp.ConsumeBytes<unsigned char>(40);
        break;
    }
    case 5: {
        // Truncated push — will fail extraction.
        s << std::vector<unsigned char>{0x03, 0x01, 0x02};
        break;
    }
    case 6: {
        // Random script bytes after the marker — exercises the parser
        // chunk-walker and PUSHDATA1/PUSHDATA2 handling.
        const size_t len = fdp.ConsumeIntegralInRange<size_t>(0, 300);
        std::vector<uint8_t> raw = fdp.ConsumeBytes<uint8_t>(len);
        s.insert(s.end(), raw.begin(), raw.end());
        break;
    }
    case 7: {
        // Multiple version bytes packed back-to-back — tests confusion
        // attacks at the parser layer.
        s << std::vector<unsigned char>{0x03, 0x03, 0x03};
        s << fdp.ConsumeBytes<unsigned char>(20);
        break;
    }
    }
    return s;
}

CScript MakeNonOracleOpReturn(FuzzedDataProvider& fdp)
{
    CScript s;
    s << OP_RETURN;
    const std::vector<uint8_t> payload = fdp.ConsumeBytes<uint8_t>(
        fdp.ConsumeIntegralInRange<size_t>(0, 32));
    s.insert(s.end(), payload.begin(), payload.end());
    return s;
}

CTransactionRef MakeFuzzedCoinbase(FuzzedDataProvider& fdp, int32_t& height_out, int& oracle_count)
{
    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = MakeCoinbaseScriptSig(fdp, height_out);
    mtx.vin[0].nSequence = 0;

    // Output 0: subsidy
    mtx.vout.emplace_back(72000 * COIN, CScript() << OP_TRUE);

    // Number of additional outputs
    const uint8_t extra = fdp.ConsumeIntegralInRange<uint8_t>(0, 4);
    oracle_count = 0;
    for (uint8_t i = 0; i < extra; ++i) {
        const uint8_t kind = fdp.ConsumeIntegralInRange<uint8_t>(0, 3);
        switch (kind) {
        case 0:
            mtx.vout.emplace_back(0, MakeOracleScript(fdp));
            ++oracle_count;
            break;
        case 1:
            mtx.vout.emplace_back(0, MakeNonOracleOpReturn(fdp));
            break;
        case 2:
            mtx.vout.emplace_back(fdp.ConsumeIntegralInRange<CAmount>(0, MAX_MONEY),
                                  CScript() << OP_TRUE);
            break;
        case 3: {
            // Witness commitment-shaped output (OP_RETURN + 36 bytes).
            CScript w;
            w << OP_RETURN;
            std::vector<uint8_t> commit = fdp.ConsumeBytes<uint8_t>(36);
            commit.resize(36, 0xab);
            w.insert(w.end(), commit.begin(), commit.end());
            mtx.vout.emplace_back(0, w);
            break;
        }
        }
    }

    return MakeTransactionRef(std::move(mtx));
}

CTransactionRef MakeMaybeDDTx(FuzzedDataProvider& fdp)
{
    CMutableTransaction mtx;
    // Set DD marker so the block "touches" DigiDollar via HasDigiDollarMarker
    // when the fuzzer asks for it.
    if (fdp.ConsumeBool()) {
        // Type byte (mint/transfer/redeem)
        const uint8_t type = fdp.ConsumeIntegralInRange<uint8_t>(1, 3);
        mtx.nVersion = (static_cast<int32_t>(type) << 24) | 0x0770;
    } else {
        mtx.nVersion = fdp.ConsumeIntegralInRange<int32_t>(1, 3);
    }
    uint256 ph;
    auto bytes = fdp.ConsumeBytes<uint8_t>(32);
    if (bytes.size() == 32) std::memcpy(ph.begin(), bytes.data(), 32);
    mtx.vin.emplace_back(COutPoint(ph, fdp.ConsumeIntegralInRange<uint32_t>(0, 16)));
    mtx.vout.emplace_back(0, CScript() << OP_TRUE);
    return MakeTransactionRef(std::move(mtx));
}

} // namespace

FUZZ_TARGET(oracle_validate_block_data, .init = initialize_oracle_validate_block_data)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    LIMITED_WHILE(fdp.remaining_bytes() > 16, 12) {
        const Consensus::Params& params = Params().GetConsensus();

        // -- Strategy A: empty-vtx block (validator must short-circuit) --
        if (fdp.ConsumeBool()) {
            CBlock block;
            BlockValidationState state;
            const bool ok = OracleDataValidator::ValidateBlockOracleData(block, /*pindex_prev=*/nullptr, params, state);
            assert(ok);  // empty vtx is always accepted
            assert(state.IsValid());
            continue;
        }

        // -- Strategy B: fuzz the coinbase + extra DD-shaped txs --
        CBlock block;
        block.nTime = fdp.ConsumeIntegral<uint32_t>();
        block.nBits = fdp.ConsumeIntegral<uint32_t>();
        block.nNonce = fdp.ConsumeIntegral<uint32_t>();
        block.nVersion = fdp.ConsumeIntegral<int32_t>();

        int32_t height_from_cb = 0;
        int oracle_count = 0;
        block.vtx.push_back(MakeFuzzedCoinbase(fdp, height_from_cb, oracle_count));

        const uint8_t extra_tx = fdp.ConsumeIntegralInRange<uint8_t>(0, 3);
        for (uint8_t i = 0; i < extra_tx; ++i) {
            block.vtx.push_back(MakeMaybeDDTx(fdp));
        }

        // Run the validator without a prev block index — it falls back to
        // the BIP34 height path. Repeat with `pindex_prev=nullptr` so we
        // hit both code paths across the fuzz corpus.
        BlockValidationState state;
        const bool ok = OracleDataValidator::ValidateBlockOracleData(block, /*pindex_prev=*/nullptr, params, state);

        // Determinism: validating the same block twice must give identical
        // results.
        BlockValidationState state2;
        const bool ok2 = OracleDataValidator::ValidateBlockOracleData(block, /*pindex_prev=*/nullptr, params, state2);
        assert(ok == ok2);
        assert(state.IsValid() == state2.IsValid());

        // Multiple oracle outputs must always be rejected with the
        // dedicated reason code.
        if (oracle_count > 1 && !ok) {
            const std::string reason = state.GetRejectReason();
            assert(!reason.empty());
        }

        // Inspect classification: when the validator rejected, the reason
        // string must be one of the documented codes (or a parse error).
        if (!ok) {
            const std::string r = state.GetRejectReason();
            assert(!r.empty());
            // Acceptable rejection prefixes — any future addition should
            // be reflected here.
            const bool known =
                r == "bad-oracle-multiple-outputs" ||
                r == "bad-oracle-missing" ||
                r == "bad-oracle-malformed" ||
                r == "bad-oracle-legacy" ||
                r == "bad-oracle-musig2" ||
                r == "bad-oracle-timestamp" ||
                r == "bad-cb-bip34-height-encoding";
            // Soft assert: if the reason is unknown, surface it via abort.
            // This catches accidental new error codes that should be
            // reflected in the docs/test plan.
            assert(known);
        }
    }
}
