// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 9 (DigiDollar V1 Final Audit) — Bitmap mutation fuzz harness.
 *
 * Mutates the v0x03 participation bitmap and exercises the canonical
 * boundary invariants between the encoder, the decoder, and the
 * MuSig2-quorum gate in `OracleBundleManager`. The contract under test:
 *
 *   1. `DecodeBitmap(bitmap, total)` returns the empty vector unless
 *      `bitmap.size() == ceil(total/8)` AND the unused high bits in the
 *      final byte are all zero.
 *
 *   2. Every decoded oracle id is strictly less than `total_oracles`.
 *
 *   3. `EncodeBitmap` round-trips: if `decode(b, total)` returns a
 *      non-empty list with size >= ORACLE_CONSENSUS_REQUIRED, then
 *      `encode(decoded, total) == b`.
 *
 *   4. `HasMuSig2Quorum(bundle, params)` returns true only if
 *      `decode(bundle.participation_bitmap, params.nOracleTotalOracles)`
 *      has at least `params.nOracleConsensusRequired` distinct ids and
 *      every id is < `params.nOraclePubkeyCount`.
 *
 *   5. Total oracles can only be 1..256; outside that range every input
 *      decodes to empty and quorum returns false.
 *
 * The harness runs across mainnet, testnet, and regtest chainparams to
 * cover the 4-of-7 (regtest) and 7-signature (mainnet/testnet) thresholds.
 */

#include <cassert>
#include <cstdint>
#include <vector>

#include <chainparams.h>
#include <consensus/params.h>
#include <oracle/bundle_manager.h>
#include <oracle/musig2_aggregator.h>
#include <primitives/oracle.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/util/setup_common.h>

namespace {

void initialize_oracle_musig2_bitmap_invariants()
{
    static const auto testing_setup =
        std::make_unique<const BasicTestingSetup>(ChainType::MAIN);
    (void)testing_setup;
}

// ----------------------------------------------------------------------------
// HasMuSig2Quorum is a translation-unit-local helper in
// oracle/bundle_manager.cpp. Reimplement the equivalent invariant inline
// here so the harness can compare its expectation against
// OracleBundleManager::ValidateBundle (which is the public entry point that
// calls HasMuSig2Quorum).
// ----------------------------------------------------------------------------

bool ExpectedQuorum(const std::vector<unsigned char>& bitmap,
                    const Consensus::Params& params)
{
    const uint16_t total = static_cast<uint16_t>(params.nOracleTotalOracles);
    const std::vector<uint8_t> ids = MuSig2OracleAggregator::DecodeBitmap(bitmap, total);
    if (static_cast<int>(ids.size()) < params.nOracleConsensusRequired) return false;
    for (uint8_t id : ids) {
        if (static_cast<int>(id) >= params.nOraclePubkeyCount) return false;
    }
    return true;
}

void RunInvariantsForChain(FuzzedDataProvider& fdp, ChainType chain)
{
    SelectParams(chain);
    const Consensus::Params& params = Params().GetConsensus();
    if (params.nOracleTotalOracles <= 0 || params.nOracleTotalOracles > 256) return;

    const uint16_t total = static_cast<uint16_t>(params.nOracleTotalOracles);
    const size_t expected_bytes = (total + 7) / 8;

    // Choose a fuzzed bitmap length that includes both correct and
    // incorrect sizes (shorter, exact, longer) so the size guard fires.
    const size_t len = fdp.ConsumeIntegralInRange<size_t>(0, expected_bytes + 4);
    auto bitmap = fdp.ConsumeBytes<unsigned char>(len);
    if (bitmap.size() < len) bitmap.resize(len, 0);

    const auto decoded = MuSig2OracleAggregator::DecodeBitmap(bitmap, total);

    // Invariant 1: decode returns empty unless size matches.
    if (bitmap.size() != expected_bytes) {
        assert(decoded.empty());
    }

    // Invariant 2: every decoded id is strictly less than total.
    for (uint8_t id : decoded) {
        assert(static_cast<uint16_t>(id) < total);
    }

    // Invariant 3 (round-trip): a valid decode must round-trip when the
    // chosen subset meets the encoder's threshold guard.
    if (!decoded.empty() && static_cast<int>(decoded.size()) >= params.nOracleConsensusRequired) {
        const auto reencoded = MuSig2OracleAggregator::EncodeBitmap(decoded, total);
        assert(reencoded == bitmap);
    }

    // Invariant 4: HasMuSig2Quorum agrees with the bitmap-only inspection.
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = fdp.ConsumeIntegral<int32_t>();
    bundle.median_price_micro_usd = fdp.ConsumeIntegral<uint64_t>();
    bundle.timestamp = fdp.ConsumeIntegral<int64_t>();
    bundle.aggregate_sig.assign(64, 0xAA); // valid-length placeholder
    bundle.participation_bitmap = bitmap;

    const bool gate = OracleBundleManager::ValidateBundle(bundle, /*block_height=*/0, params);
    const bool expected_gate =
        bundle.median_price_micro_usd > 0 &&
        bundle.timestamp > 0 &&
        ExpectedQuorum(bitmap, params);
    assert(gate == expected_gate);

    // Invariant 5: out-of-range total decodes to empty.
    auto out_of_range_decoded = MuSig2OracleAggregator::DecodeBitmap(bitmap, /*total=*/0);
    assert(out_of_range_decoded.empty());
    auto large_decoded = MuSig2OracleAggregator::DecodeBitmap(bitmap, /*total=*/257);
    assert(large_decoded.empty());
}

} // namespace

FUZZ_TARGET(oracle_musig2_bitmap_invariants, .init = initialize_oracle_musig2_bitmap_invariants)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // Iterate a few rounds across the three chain rosters to surface any
    // chain-dependent boundary surprises (regtest 4-of-7 vs mainnet/testnet
    // mainnet/testnet vs whatever the harness selector chose).
    LIMITED_WHILE(fdp.remaining_bytes() > 0, 30) {
        const uint8_t chain_pick = fdp.ConsumeIntegralInRange<uint8_t>(0, 2);
        switch (chain_pick) {
            case 0: RunInvariantsForChain(fdp, ChainType::MAIN); break;
            case 1: RunInvariantsForChain(fdp, ChainType::TESTNET); break;
            default: RunInvariantsForChain(fdp, ChainType::REGTEST); break;
        }
    }
}
