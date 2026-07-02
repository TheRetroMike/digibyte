// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/oracle.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>

#include <cassert>
#include <cstdint>
#include <vector>

FUZZ_TARGET(oracle_musig2_bundle)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // 1. Feed random bytes as a v0x03 payload to DeserializeV03Data
    {
        size_t payload_len = fdp.ConsumeIntegralInRange<size_t>(0, 512);
        auto raw = fdp.ConsumeBytes<uint8_t>(payload_len);

        COracleBundle bundle;
        bool ok = COracleBundle::DeserializeV03Data(raw, bundle);

        if (ok) {
            // If deserialization succeeded, exercise accessors
            (void)bundle.IsMuSig2();
            (void)bundle.GetV03PayloadSize();

            // Re-serialize and verify roundtrip
            auto reserialized = bundle.SerializeV03Data();
            COracleBundle bundle2;
            bool ok2 = COracleBundle::DeserializeV03Data(reserialized, bundle2);
            assert(ok2);
            assert(bundle.median_price_micro_usd == bundle2.median_price_micro_usd);
            assert(bundle.timestamp == bundle2.timestamp);
            assert(bundle.aggregate_sig == bundle2.aggregate_sig);
            assert(bundle.participation_bitmap == bundle2.participation_bitmap);
        }
    }

    // 2. Construct a v0x03 bundle with fuzzed fields, serialize, and re-parse
    {
        COracleBundle bundle;
        bundle.version = 3;
        bundle.epoch = fdp.ConsumeIntegral<int32_t>();
        bundle.median_price_micro_usd = fdp.ConsumeIntegral<uint64_t>();
        bundle.timestamp = fdp.ConsumeIntegral<int64_t>();

        // Fuzz the aggregate signature (should be 64 bytes for valid)
        size_t sig_len = fdp.ConsumeIntegralInRange<size_t>(0, 128);
        bundle.aggregate_sig = fdp.ConsumeBytes<unsigned char>(sig_len);

        // Fuzz the participation bitmap
        size_t bmp_len = fdp.ConsumeIntegralInRange<size_t>(0, 8);
        bundle.participation_bitmap = fdp.ConsumeBytes<unsigned char>(bmp_len);

        (void)bundle.IsMuSig2();
        (void)bundle.GetV03PayloadSize();

        auto serialized = bundle.SerializeV03Data();
        if (!serialized.empty()) {
            COracleBundle decoded;
            (void)COracleBundle::DeserializeV03Data(serialized, decoded);
        }
    }
}
