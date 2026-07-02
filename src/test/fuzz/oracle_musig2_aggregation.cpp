// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <primitives/oracle.h>
#include <pubkey.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <cassert>
#include <cstdint>
#include <set>
#include <vector>

namespace {
void initialize_musig2_aggregation()
{
    static const auto testing_setup = MakeNoLogFileContext<const BasicTestingSetup>(ChainType::MAIN);
    (void)testing_setup;
}
} // namespace

FUZZ_TARGET(oracle_musig2_aggregation, .init = initialize_musig2_aggregation)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // Generate a fuzzed set of oracle keys
    size_t num_oracles = fdp.ConsumeIntegralInRange<size_t>(1, ORACLE_TOTAL_COUNT);
    std::vector<OracleNodeInfo> oracles;
    std::vector<CKey> privkeys;

    for (size_t i = 0; i < num_oracles; ++i) {
        CKey key;
        auto key_bytes = fdp.ConsumeBytes<uint8_t>(32);
        if (key_bytes.size() == 32) {
            key.Set(key_bytes.begin(), key_bytes.end(), true);
        }
        if (!key.IsValid()) {
            key.MakeNewKey(true);
        }
        privkeys.push_back(key);

        OracleNodeInfo info;
        info.id = static_cast<uint32_t>(i);
        info.pubkey = key.GetPubKey();
        info.is_active = fdp.ConsumeBool();
        oracles.push_back(info);
    }

    // Select a fuzzed subset of oracles (simulate participation bitmap)
    std::set<size_t> participating;
    size_t subset_size = fdp.ConsumeIntegralInRange<size_t>(0, num_oracles);
    for (size_t i = 0; i < subset_size; ++i) {
        participating.insert(fdp.ConsumeIntegralInRange<size_t>(0, num_oracles - 1));
    }

    // Build a participation bitmap from the subset
    size_t bitmap_bytes = (num_oracles + 7) / 8;
    std::vector<unsigned char> bitmap(bitmap_bytes, 0);
    for (size_t idx : participating) {
        bitmap[idx / 8] |= (1 << (idx % 8));
    }

    // Construct a v0x03 bundle with the participation bitmap
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = fdp.ConsumeIntegral<int32_t>();
    bundle.median_price_micro_usd = fdp.ConsumeIntegralInRange<uint64_t>(
        ORACLE_MIN_PRICE_MICRO_USD, ORACLE_MAX_PRICE_MICRO_USD);
    bundle.timestamp = fdp.ConsumeIntegral<int64_t>();
    bundle.participation_bitmap = bitmap;
    bundle.aggregate_sig.assign(64, 0x00); // Placeholder signature

    // Exercise bundle methods
    (void)bundle.IsMuSig2();
    (void)bundle.GetV03PayloadSize();

    // Serialize and roundtrip
    auto payload = bundle.SerializeV03Data();
    if (!payload.empty()) {
        COracleBundle decoded;
        bool ok = COracleBundle::DeserializeV03Data(payload, decoded);
        if (ok) {
            assert(decoded.participation_bitmap == bundle.participation_bitmap);
            assert(decoded.median_price_micro_usd == bundle.median_price_micro_usd);
        }
    }

    // Create individual price messages from participating oracles
    for (size_t idx : participating) {
        if (idx >= privkeys.size()) continue;
        COraclePriceMessage msg;
        msg.oracle_id = static_cast<uint32_t>(idx);
        msg.price_micro_usd = bundle.median_price_micro_usd;
        msg.timestamp = bundle.timestamp;
        msg.block_height = fdp.ConsumeIntegral<int32_t>();
        msg.nonce = fdp.ConsumeIntegral<uint64_t>();
        msg.oracle_pubkey = XOnlyPubKey(privkeys[idx].GetPubKey());

        // Sign and verify individual messages
        if (msg.SignAttestation(privkeys[idx])) {
            (void)msg.VerifyAttestation();
        }
    }

    // Fuzz SelectOraclesForEpoch with random epoch values
    int32_t epoch = fdp.ConsumeIntegral<int32_t>();
    auto selected = SelectOraclesForEpoch(oracles, epoch);
    assert(selected.size() <= static_cast<size_t>(ORACLE_ACTIVE_COUNT));
}
