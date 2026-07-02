// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <net.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <test/fuzz/util/net.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace {
const TestingSetup* g_dandelion_setup;
} // namespace

void initialize_dandelion()
{
    static const auto testing_setup = MakeNoLogFileContext<const TestingSetup>();
    g_dandelion_setup = testing_setup.get();
}

FUZZ_TARGET(fuzz_dandelion_embargo, .init = initialize_dandelion)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    ConnmanTestMsg connman{fdp.ConsumeIntegral<uint64_t>(),
                           fdp.ConsumeIntegral<uint64_t>(),
                           *g_dandelion_setup->m_node.addrman,
                           *g_dandelion_setup->m_node.netgroupman,
                           Params()};

    // Track inserted hashes so we can query/remove them
    std::vector<uint256> inserted_hashes;

    LIMITED_WHILE(fdp.remaining_bytes() > 0, 1000) {
        CallOneOf(
            fdp,
            [&] {
                // Insert a new embargo with fuzzed hash and time
                uint256 hash;
                auto hash_bytes = fdp.ConsumeBytes<uint8_t>(32);
                if (hash_bytes.size() == 32) {
                    std::memcpy(hash.begin(), hash_bytes.data(), 32);
                }
                auto embargo_us = std::chrono::microseconds{fdp.ConsumeIntegral<int64_t>()};
                bool was_new = connman.insertDandelionEmbargo(hash, embargo_us);
                (void)was_new;
                inserted_hashes.push_back(hash);
            },
            [&] {
                // Check embargo status of a known hash
                if (!inserted_hashes.empty()) {
                    const auto& hash = inserted_hashes[fdp.ConsumeIntegralInRange<size_t>(0, inserted_hashes.size() - 1)];
                    (void)connman.isTxDandelionEmbargoed(hash);
                }
            },
            [&] {
                // Check embargo status of a random hash
                uint256 hash;
                auto hash_bytes = fdp.ConsumeBytes<uint8_t>(32);
                if (hash_bytes.size() == 32) {
                    std::memcpy(hash.begin(), hash_bytes.data(), 32);
                }
                (void)connman.isTxDandelionEmbargoed(hash);
            },
            [&] {
                // Remove embargo for a known hash
                if (!inserted_hashes.empty()) {
                    size_t idx = fdp.ConsumeIntegralInRange<size_t>(0, inserted_hashes.size() - 1);
                    (void)connman.removeDandelionEmbargo(inserted_hashes[idx]);
                }
            },
            [&] {
                // Remove embargo for a random hash (should be no-op)
                uint256 hash;
                auto hash_bytes = fdp.ConsumeBytes<uint8_t>(32);
                if (hash_bytes.size() == 32) {
                    std::memcpy(hash.begin(), hash_bytes.data(), 32);
                }
                (void)connman.removeDandelionEmbargo(hash);
            },
            [&] {
                // Insert with insert_or_assign semantics (update existing)
                if (!inserted_hashes.empty()) {
                    const auto& hash = inserted_hashes[fdp.ConsumeIntegralInRange<size_t>(0, inserted_hashes.size() - 1)];
                    auto new_embargo = std::chrono::microseconds{fdp.ConsumeIntegral<int64_t>()};
                    (void)connman.insertDandelionEmbargo(hash, new_embargo);
                }
            },
            [&] {
                // Directly manipulate stem-routed set through the public mutex
                uint256 hash;
                auto hash_bytes = fdp.ConsumeBytes<uint8_t>(32);
                if (hash_bytes.size() == 32) {
                    std::memcpy(hash.begin(), hash_bytes.data(), 32);
                }
                LOCK(connman.m_dandelion_embargo_mutex);
                if (fdp.ConsumeBool()) {
                    connman.m_dandelion_stem_routed.insert(hash);
                } else {
                    connman.m_dandelion_stem_routed.erase(hash);
                }
            });
    }

    // Final sweep: verify all operations on remaining hashes don't crash
    for (const auto& hash : inserted_hashes) {
        (void)connman.isTxDandelionEmbargoed(hash);
        (void)connman.removeDandelionEmbargo(hash);
    }
}

FUZZ_TARGET(fuzz_dandelion_shuffle, .init = initialize_dandelion)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    ConnmanTestMsg connman{fdp.ConsumeIntegral<uint64_t>(),
                           fdp.ConsumeIntegral<uint64_t>(),
                           *g_dandelion_setup->m_node.addrman,
                           *g_dandelion_setup->m_node.netgroupman,
                           Params()};

    // Add fuzzed number of test nodes as outbound/inbound peers
    const size_t num_nodes = fdp.ConsumeIntegralInRange<size_t>(0, 20);
    std::vector<CNode*> added_nodes;

    for (size_t i = 0; i < num_nodes && fdp.remaining_bytes() > 0; i++) {
        CNode* pnode = ConsumeNodeAsUniquePtr(fdp).release();
        connman.AddTestNode(*pnode);
        added_nodes.push_back(pnode);
    }

    // Populate Dandelion inbound/outbound vectors via test helpers
    for (CNode* pnode : added_nodes) {
        if (fdp.ConsumeBool()) {
            connman.AddDandelionOutboundTest(pnode);
        }
        if (fdp.ConsumeBool()) {
            connman.AddDandelionInboundTest(pnode);
        }
    }

    // Insert some embargoes before shuffle
    LIMITED_WHILE(fdp.ConsumeBool() && fdp.remaining_bytes() > 0, 50) {
        uint256 hash;
        auto hash_bytes = fdp.ConsumeBytes<uint8_t>(32);
        if (hash_bytes.size() == 32) {
            std::memcpy(hash.begin(), hash_bytes.data(), 32);
        }
        auto embargo_us = std::chrono::microseconds{fdp.ConsumeIntegral<int64_t>()};
        connman.insertDandelionEmbargo(hash, embargo_us);
        {
            LOCK(connman.m_dandelion_embargo_mutex);
            connman.m_dandelion_stem_routed.insert(hash);
        }
    }

    // Perform shuffle — this exercises the full routing logic
    connman.DandelionShuffleTest();

    // Verify post-shuffle state is consistent
    (void)connman.usingDandelion();

    // Do another shuffle to test double-shuffle
    if (fdp.ConsumeBool()) {
        connman.DandelionShuffleTest();
    }

    // Verify stem-routed was cleared by shuffle
    {
        LOCK(connman.m_dandelion_embargo_mutex);
        assert(connman.m_dandelion_stem_routed.empty());
    }

    connman.ClearTestNodes();
}
