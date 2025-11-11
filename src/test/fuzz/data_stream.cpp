// Copyright (c) 2014-2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <addrdb.h>
#include <addrman.h>
#include <net.h>
#include <netgroup.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <test/util/setup_common.h>

#include <cstdint>
#include <vector>

void initialize_data_stream_addr_man()
{
    static const auto testing_setup = MakeNoLogFileContext<>();
}

FUZZ_TARGET_INIT(data_stream_addr_man, initialize_data_stream_addr_man)
{
    FuzzedDataProvider fuzzed_data_provider{buffer.data(), buffer.size()};
    DataStream data_stream = ConsumeDataStream(fuzzed_data_provider);
    NetGroupManager netgroup_manager{{}};
    AddrMan addr_man{netgroup_manager, /*deterministic=*/true, /*consistency_check_ratio=*/0};
    try {
        ReadFromStream(addr_man, data_stream);
    } catch (const std::exception&) {
        // Expected behavior for malformed data
    }
}
