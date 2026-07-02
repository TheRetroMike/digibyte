// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <net.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util/net.h>
#include <test/util/net.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
const TestingSetup* g_setup;
}

void initialize_fuzz_dandelion_epoch()
{
    static const auto testing_setup = MakeNoLogFileContext<const TestingSetup>();
    g_setup = testing_setup.get();
}

FUZZ_TARGET(fuzz_dandelion_epoch, .init = initialize_fuzz_dandelion_epoch)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    ConnmanTestMsg connman{fdp.ConsumeIntegral<uint64_t>(),
                           fdp.ConsumeIntegral<uint64_t>(),
                           *g_setup->m_node.addrman,
                           *g_setup->m_node.netgroupman,
                           Params()};

    std::vector<CNode*> nodes;
    const size_t num_nodes = fdp.ConsumeIntegralInRange<size_t>(0, 24);
    for (size_t i = 0; i < num_nodes && fdp.remaining_bytes() > 0; ++i) {
        CNode* pnode = ConsumeNodeAsUniquePtr(fdp).release();
        connman.AddTestNode(*pnode);
        nodes.push_back(pnode);
        if (fdp.ConsumeBool()) connman.AddDandelionOutboundTest(pnode);
        if (fdp.ConsumeBool()) connman.AddDandelionInboundTest(pnode);
    }

    // Seed embargo map and stem-routed set before epoch rotations.
    LIMITED_WHILE(fdp.remaining_bytes() > 0, 64) {
        uint256 hash;
        const auto h = fdp.ConsumeBytes<uint8_t>(32);
        if (h.size() == 32) {
            std::memcpy(hash.begin(), h.data(), 32);
        }
        auto embargo = std::chrono::microseconds{fdp.ConsumeIntegral<int64_t>()};
        (void)connman.insertDandelionEmbargo(hash, embargo);
        LOCK(connman.m_dandelion_embargo_mutex);
        connman.m_dandelion_stem_routed.insert(hash);
    }

    const size_t shuffles = fdp.ConsumeIntegralInRange<size_t>(1, 32);
    for (size_t i = 0; i < shuffles; ++i) {
        connman.DandelionShuffleTest();
        (void)connman.usingDandelion();
        (void)connman.getAllDandelionDestinations();

        // Invariant: shuffle clears stem-routed bookkeeping for safe re-routing.
        {
            LOCK(connman.m_dandelion_embargo_mutex);
            assert(connman.m_dandelion_stem_routed.empty());
        }

        if (!nodes.empty() && fdp.ConsumeBool()) {
            CNode* pfrom = nodes[fdp.ConsumeIntegralInRange<size_t>(0, nodes.size() - 1)];
            (void)connman.getDandelionDestination(pfrom);
        } else {
            (void)connman.getDandelionDestination(nullptr);
        }
    }

    connman.ClearTestNodes();
}
