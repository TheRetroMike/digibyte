// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 8 - Oracle Bundle Version Reject fuzz harness
 *
 * Mutates the oracle-bundle version byte and the rest of the OP_RETURN
 * payload, builds a coinbase-shaped block (with and without DD-touching
 * extra txs), and feeds it to ExtractOracleBundle and
 * OracleDataValidator::ValidateBlockOracleData.
 *
 * Invariants enforced:
 *   - Only version byte 0x03 may be reported as MuSig2 by ExtractOracleBundle.
 *   - DD-touching blocks with a non-v0x03 version always fail validation.
 *   - When extraction returns false, the validator never reports
 *     bad-oracle-musig2 (the bundle never reaches that branch).
 *   - Multiple oracle outputs always rejected with bad-oracle-multiple-outputs.
 */

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void initialize_oracle_bundle_version_reject()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}

CTransactionRef MakeDDMintTx()
{
    CMutableTransaction tx;
    tx.nVersion = MakeDigiDollarVersion(DD_TX_MINT);
    tx.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx.vout.emplace_back(0, CScript() << OP_TRUE);
    return MakeTransactionRef(std::move(tx));
}

CScript BuildOracleScript(uint8_t version_byte, const std::vector<unsigned char>& payload)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{version_byte};
    if (!payload.empty()) {
        script << payload;
    }
    return script;
}

CBlock BuildBlock(int32_t height,
                  uint32_t block_time,
                  const std::vector<CScript>& oracle_scripts,
                  bool dd_touching)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << static_cast<int64_t>(height);
    coinbase.vout.emplace_back(50 * COIN, CScript() << OP_TRUE);
    for (const auto& s : oracle_scripts) {
        coinbase.vout.emplace_back(0, s);
    }

    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime = block_time;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    if (dd_touching) {
        block.vtx.push_back(MakeDDMintTx());
    }
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

} // namespace

FUZZ_TARGET(oracle_bundle_version_reject, .init = initialize_oracle_bundle_version_reject)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // Mutated version byte across the full uint8 range.
    const uint8_t version_byte = fdp.ConsumeIntegral<uint8_t>();

    // Random payload sized 0..256 bytes.
    const size_t payload_size = fdp.ConsumeIntegralInRange<size_t>(0, 256);
    std::vector<unsigned char> payload = fdp.ConsumeBytes<unsigned char>(payload_size);

    // Should the block be DD-touching?
    const bool dd_touching = fdp.ConsumeBool();

    // Should we duplicate the oracle output to exercise multiple-outputs?
    const bool duplicate_output = fdp.ConsumeBool();

    // Random block height/time.
    const int32_t height = fdp.ConsumeIntegralInRange<int32_t>(700, 1'000'000);
    const uint32_t block_time = fdp.ConsumeIntegralInRange<uint32_t>(1735000000, 1900000000);

    const CScript oracle_script = BuildOracleScript(version_byte, payload);

    std::vector<CScript> scripts;
    scripts.push_back(oracle_script);
    if (duplicate_output) scripts.push_back(oracle_script);

    CBlock block = BuildBlock(height, block_time, scripts, dd_touching);

    // 1) ExtractOracleBundle invariants.
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle extracted;
    const bool ok = manager.ExtractOracleBundle(*block.vtx[0], extracted);

    if (ok) {
        // The only way ExtractOracleBundle returns true today is for a
        // valid v0x03 payload, so the deserialised version field must
        // be 3 and IsMuSig2() must hold. Nothing else may slip past.
        assert(extracted.version == 3);
        assert(extracted.IsMuSig2());
    } else {
        // Failed extraction must never land on the reasons used for
        // structural success; nothing to assert on the bundle contents.
    }

    // 2) ValidateBlockOracleData reject-reason invariants.
    BlockValidationState state;
    const bool valid = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, Params().GetConsensus(), state);

    const std::string& reason = state.GetRejectReason();

    if (duplicate_output) {
        // Two oracle outputs always rejected with multiple-outputs.
        assert(!valid);
        assert(reason == "bad-oracle-multiple-outputs");
        return;
    }

    if (!valid) {
        // Allowed reject reasons for a single oracle output:
        // bad-oracle-malformed, bad-oracle-legacy, bad-oracle-musig2,
        // bad-oracle-timestamp, bad-oracle-missing.
        assert(reason == "bad-oracle-malformed" ||
               reason == "bad-oracle-legacy" ||
               reason == "bad-oracle-musig2" ||
               reason == "bad-oracle-timestamp" ||
               reason == "bad-oracle-missing");

        if (!ok) {
            // Extraction failure cannot land on the post-extraction
            // reasons (musig2/legacy/timestamp); only malformed (or
            // missing on the no-output branch, which we don't hit
            // here because an oracle script is always present).
            assert(reason == "bad-oracle-malformed");
        }
    } else {
        // Success path: must be v0x03 valid bundle.
        assert(ok);
        assert(extracted.IsMuSig2());
    }
}
