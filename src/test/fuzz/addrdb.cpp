// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <addrdb.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

FUZZ_TARGET(addrdb)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());

    // The point of this code is to exercise all CBanEntry constructors.
    const CBanEntry ban_entry = [&] {
        switch (fuzzed_data_provider.ConsumeIntegralInRange<int>(0, 2)) {
        case 0:
            return CBanEntry{fuzzed_data_provider.ConsumeIntegral<int64_t>()};
        case 1: {
            CBanEntry entry{fuzzed_data_provider.ConsumeIntegral<int64_t>()};
            entry.nVersion = fuzzed_data_provider.ConsumeIntegral<int>();
            entry.nBanUntil = fuzzed_data_provider.ConsumeIntegral<int64_t>();
            const CBanEntry roundtrip{entry.ToJson()};
            assert(roundtrip.nVersion == entry.nVersion);
            assert(roundtrip.nCreateTime == entry.nCreateTime);
            assert(roundtrip.nBanUntil == entry.nBanUntil);
            return roundtrip;
        }
        case 2: {
            UniValue json(UniValue::VOBJ);
            if (fuzzed_data_provider.ConsumeBool()) {
                json.pushKV("version", fuzzed_data_provider.ConsumeIntegral<int>());
            }
            if (fuzzed_data_provider.ConsumeBool()) {
                json.pushKV("ban_created", fuzzed_data_provider.ConsumeIntegral<int64_t>());
            }
            if (fuzzed_data_provider.ConsumeBool()) {
                json.pushKV("banned_until", fuzzed_data_provider.ConsumeIntegral<int64_t>());
            }
            try {
                return CBanEntry{json};
            } catch (const std::exception&) {
                break;
            }
        }
        }
        return CBanEntry{};
    }();
    (void)ban_entry; // currently unused
}
