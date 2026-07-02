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

#include <cassert>
#include <cstdint>
#include <vector>

namespace {
const TestingSetup* g_setup;
}

void initialize_fuzz_dandelion_routing()
{
    static const auto testing_setup = MakeNoLogFileContext<const TestingSetup>();
    g_setup = testing_setup.get();
}

FUZZ_TARGET(fuzz_dandelion_routing, .init = initialize_fuzz_dandelion_routing)
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

    LIMITED_WHILE(fdp.remaining_bytes() > 0, 128) {
        if (fdp.ConsumeBool()) {
            connman.DandelionShuffleTest();
            {
                LOCK(connman.m_dandelion_embargo_mutex);
                assert(connman.m_dandelion_stem_routed.empty());
            }
        }

        CNode* pfrom = nullptr;
        if (!nodes.empty() && fdp.ConsumeBool()) {
            pfrom = nodes[fdp.ConsumeIntegralInRange<size_t>(0, nodes.size() - 1)];
        }

        CNode* destination = connman.getDandelionDestination(pfrom);
        if (!connman.usingDandelion()) {
            assert(destination == nullptr);
        }

        (void)connman.setLocalDandelionDestination();
        (void)connman.getLocalDandelionDestination();
        (void)connman.getAllDandelionDestinations();
    }

    connman.ClearTestNodes();
}
