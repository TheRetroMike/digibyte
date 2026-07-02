// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/oracle.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <streams.h>
#include <key.h>

#include <cstdint>
#include <limits>
#include <vector>

FUZZ_TARGET(oracle_bundle_validation)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    // Create bundle with fuzzed epoch and price
    int32_t epoch = fuzzed_data.ConsumeIntegral<int32_t>();
    COracleBundle bundle(epoch);
    bundle.median_price_micro_usd = fuzzed_data.ConsumeIntegral<uint64_t>();
    bundle.timestamp = fuzzed_data.ConsumeIntegral<int64_t>();
    bundle.version = fuzzed_data.ConsumeIntegral<uint8_t>();

    // Add fuzzed messages from the RC44 active ID range.
    uint8_t num_messages = fuzzed_data.ConsumeIntegralInRange<uint8_t>(0, 20);
    for (uint8_t i = 0; i < num_messages && fuzzed_data.remaining_bytes() > 20; ++i) {
        uint32_t oracle_id = fuzzed_data.ConsumeIntegral<uint32_t>();
        uint64_t price = fuzzed_data.ConsumeIntegral<uint64_t>();
        int64_t ts = fuzzed_data.ConsumeIntegral<int64_t>();

        COraclePriceMessage msg(oracle_id, price, ts);
        msg.block_height = fuzzed_data.ConsumeIntegral<int32_t>();
        msg.nonce = fuzzed_data.ConsumeIntegral<uint64_t>();

        // Give it a fake signature so AddMessage doesn't reject on size
        msg.schnorr_sig.resize(64);
        auto sig_bytes = fuzzed_data.ConsumeBytes<unsigned char>(64);
        if (sig_bytes.size() == 64) {
            msg.schnorr_sig = sig_bytes;
        }

        bundle.AddMessage(msg);
    }

    // Exercise all validation paths
    int min_required = fuzzed_data.ConsumeIntegralInRange<int>(0, 20);
    (void)bundle.IsValid(min_required, 0);
    (void)bundle.IsValid(min_required, GetTime());
    (void)bundle.HasConsensus(min_required);
    (void)bundle.GetConsensusPrice(min_required);
    (void)bundle.ValidateEpoch(epoch);
    if (epoch < std::numeric_limits<int32_t>::max()) {
        (void)bundle.ValidateEpoch(epoch + 1);
    }
    (void)bundle.IsMuSig2();
    (void)bundle.GetV03PayloadSize();

    // Serialization round-trip
    try {
        DataStream ds{};
        ds << bundle;

        COracleBundle bundle2;
        ds >> bundle2;
    } catch (const std::exception&) {
    }
}

FUZZ_TARGET(oracle_bundle_v03_roundtrip)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    // Create a v0x03 (MuSig2) bundle with fuzzed data
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = fuzzed_data.ConsumeIntegral<int32_t>();
    bundle.median_price_micro_usd = fuzzed_data.ConsumeIntegral<uint64_t>();
    bundle.timestamp = fuzzed_data.ConsumeIntegral<int64_t>();

    // Fuzz aggregate signature (should be 64 bytes)
    size_t sig_size = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 128);
    bundle.aggregate_sig = fuzzed_data.ConsumeBytes<unsigned char>(sig_size);

    // Fuzz participation bitmap
    size_t bitmap_size = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 32);
    bundle.participation_bitmap = fuzzed_data.ConsumeBytes<unsigned char>(bitmap_size);

    // Serialize v0x03 data
    std::vector<unsigned char> serialized = bundle.SerializeV03Data();

    // Deserialize back
    COracleBundle bundle2;
    bool ok = COracleBundle::DeserializeV03Data(serialized, bundle2);

    if (ok && bundle.aggregate_sig.size() == 64 && !bundle.participation_bitmap.empty()) {
        // Round-trip should preserve price and timestamp
        assert(bundle2.median_price_micro_usd == bundle.median_price_micro_usd);
        assert(bundle2.timestamp == bundle.timestamp);
    }

    // Also try deserializing raw fuzzed data
    auto raw = fuzzed_data.ConsumeRemainingBytes<unsigned char>();
    COracleBundle bundle3;
    (void)COracleBundle::DeserializeV03Data(raw, bundle3);
}
