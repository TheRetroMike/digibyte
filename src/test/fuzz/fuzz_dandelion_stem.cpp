// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <net.h>
#include <protocol.h>
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

void initialize_fuzz_dandelion_stem()
{
    static const auto testing_setup = MakeNoLogFileContext<const TestingSetup>();
    g_setup = testing_setup.get();
}

FUZZ_TARGET(fuzz_dandelion_stem, .init = initialize_fuzz_dandelion_stem)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    ConnmanTestMsg connman{fdp.ConsumeIntegral<uint64_t>(),
                           fdp.ConsumeIntegral<uint64_t>(),
                           *g_setup->m_node.addrman,
                           *g_setup->m_node.netgroupman,
                           Params()};

    std::vector<CNode*> nodes;
    const size_t num_nodes = fdp.ConsumeIntegralInRange<size_t>(0, 16);
    for (size_t i = 0; i < num_nodes && fdp.remaining_bytes() > 0; ++i) {
        CNode* pnode = ConsumeNodeAsUniquePtr(fdp).release();
        connman.AddTestNode(*pnode);
        nodes.push_back(pnode);
        if (fdp.ConsumeBool()) connman.AddDandelionOutboundTest(pnode);
        if (fdp.ConsumeBool()) connman.AddDandelionInboundTest(pnode);
    }

    std::vector<uint256> hashes;

    LIMITED_WHILE(fdp.remaining_bytes() > 0, 256) {
        uint256 hash;
        const auto hash_bytes = fdp.ConsumeBytes<uint8_t>(32);
        if (hash_bytes.size() == 32) {
            std::memcpy(hash.begin(), hash_bytes.data(), 32);
        }
        hashes.push_back(hash);

        auto embargo = std::chrono::microseconds{fdp.ConsumeIntegral<int64_t>()};
        (void)connman.insertDandelionEmbargo(hash, embargo);
        (void)connman.isTxDandelionEmbargoed(hash);

        // Restrict to tx inventory types used by stem relay.
        const auto inv_type = static_cast<uint32_t>(fdp.PickValueInArray<int>({MSG_TX, MSG_WTX}));
        CInv inv{inv_type, hash};
        (void)connman.localDandelionDestinationPushInventory(inv);

        if (fdp.ConsumeBool()) {
            LOCK(connman.m_dandelion_embargo_mutex);
            if (fdp.ConsumeBool()) {
                connman.m_dandelion_stem_routed.insert(hash);
            } else {
                connman.m_dandelion_stem_routed.erase(hash);
            }
        }

        if (fdp.ConsumeBool()) {
            (void)connman.removeDandelionEmbargo(hash);
        }
    }

    for (const auto& hash : hashes) {
        (void)connman.isTxDandelionEmbargoed(hash);
        (void)connman.removeDandelionEmbargo(hash);
    }

    connman.ClearTestNodes();
}
