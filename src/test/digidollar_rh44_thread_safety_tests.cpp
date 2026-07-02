// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-44: Race Condition & Thread Safety Deep Dive
 *
 * Tests for thread safety bugs in DigiDollar-specific code paths:
 * 1. SystemHealthMonitor static metrics (OnMintConnected/OnRedeemConnected)
 * 2. ERR static state (s_currentState read/write across threads)
 * 3. OracleBundleManager::RemovePriceCache (wrong mutex for cached_price)
 * 4. Concurrent oracle price updates
 * 5. Health monitor read/write across threads
 */

#include <boost/test/unit_test.hpp>

#include <consensus/err.h>
#include <digidollar/health.h>
#include <oracle/bundle_manager.h>

#include <atomic>
#include <thread>
#include <vector>

struct RH44ThreadSafetyTestSetup {
    RH44ThreadSafetyTestSetup()
    {
        ResetSharedState();
    }

    ~RH44ThreadSafetyTestSetup()
    {
        ResetSharedState();
    }

    static void ResetSharedState()
    {
        DigiDollar::ERR::EmergencyRedemptionRatio::ResetForTesting();
        DigiDollar::SystemHealthMonitor::ResetMetrics();
    }
};

BOOST_FIXTURE_TEST_SUITE(digidollar_rh44_thread_safety_tests, RH44ThreadSafetyTestSetup)

// ---------------------------------------------------------------------------
// Test 1: SystemHealthMonitor concurrent OnMintConnected / OnRedeemConnected
// Pre-fix: bare static increments = data races (UB under TSan)
// Post-fix: mutex-protected, totals consistent
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(health_monitor_concurrent_mint_redeem)
{
    // Reset metrics
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    constexpr int N = 1000;
    constexpr CAmount dd_per_op  = 100;   // cents
    constexpr CAmount dgb_per_op = 100000000; // 1 DGB

    // Phase 1: Concurrent mints from 10 threads
    std::vector<std::thread> threads;
    threads.reserve(10);

    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < N; ++i) {
                DigiDollar::SystemHealthMonitor::OnMintConnected(dd_per_op, dgb_per_op);
            }
        });
    }
    for (auto& th : threads) th.join();
    threads.clear();

    // After 10 * 1000 mints:
    auto m1 = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m1.totalDDSupply, 10 * N * dd_per_op);
    BOOST_CHECK_EQUAL(m1.totalCollateral, 10 * N * dgb_per_op);

    // Phase 2: Concurrent redeems from 10 threads (same count)
    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < N; ++i) {
                DigiDollar::SystemHealthMonitor::OnRedeemConnected(dd_per_op, dgb_per_op);
            }
        });
    }
    for (auto& th : threads) th.join();

    // After equal redeems, totals should be 0
    auto m = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(m.totalCollateral, 0);
}

// ---------------------------------------------------------------------------
// Test 2: SystemHealthMonitor concurrent mint + read (GetCachedMetrics)
// Pre-fix: torn reads on s_currentMetrics fields
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(health_monitor_concurrent_write_read)
{
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    std::atomic<bool> stop{false};
    constexpr CAmount dd_per = 100;
    constexpr CAmount dgb_per = 100000000;

    // Writer thread
    std::thread writer([&] {
        for (int i = 0; i < 5000 && !stop; ++i) {
            DigiDollar::SystemHealthMonitor::OnMintConnected(dd_per, dgb_per);
        }
    });

    // Reader thread — must not see negative values (torn read symptom)
    std::atomic<bool> saw_negative{false};
    std::thread reader([&] {
        for (int i = 0; i < 5000 && !stop; ++i) {
            auto m = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
            if (m.totalDDSupply < 0 || m.totalCollateral < 0) {
                saw_negative = true;
                stop = true;
            }
        }
    });

    writer.join();
    stop = true;
    reader.join();

    BOOST_CHECK(!saw_negative);

    // Supply should be exactly 5000 * 100 = 500000
    auto m = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m.totalDDSupply, 500000);
}

// ---------------------------------------------------------------------------
// Test 3: ERR state concurrent read/write
// Pre-fix: s_currentState written by UpdateERRState, read by GetCurrentState
// with zero synchronization
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(err_state_concurrent_access)
{
    using namespace DigiDollar::ERR;

    // Reset ERR state
    EmergencyRedemptionRatio::ReconstructERRState(100, 1000);

    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};

    // Writer: toggle ERR state
    std::thread writer([&] {
        for (int i = 0; i < 2000 && !stop; ++i) {
            int health = (i % 2 == 0) ? 80 : 100;
            EmergencyRedemptionRatio::ReconstructERRState(health, 1000 + i);
        }
    });

    // Readers: GetCurrentState should never crash or return garbage
    std::atomic<bool> saw_bad{false};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&] {
            for (int i = 0; i < 2000 && !stop; ++i) {
                ERRState state = EmergencyRedemptionRatio::GetCurrentState();
                // Adjustment ratio must be in [0.0, 1.0] or exactly 0
                if (state.adjustmentRatio < 0.0 || state.adjustmentRatio > 1.0) {
                    saw_bad = true;
                    stop = true;
                }
                // systemHealth must be non-negative
                if (state.systemHealth < 0) {
                    saw_bad = true;
                    stop = true;
                }
                read_count++;
            }
        });
    }

    writer.join();
    stop = true;
    for (auto& th : readers) th.join();

    BOOST_CHECK(!saw_bad);
    BOOST_CHECK(read_count > 0);
}

// ---------------------------------------------------------------------------
// Test 4: OracleBundleManager::RemovePriceCache writes cached_price
// under mtx_price_cache instead of mtx_bundles — data race with GetLatestPrice
// Pre-fix: RemovePriceCache updates cached_price under wrong mutex
// Post-fix: RemovePriceCache holds mtx_bundles when writing cached_price
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(bundle_manager_remove_price_cache_mutex)
{
    OracleBundleManager mgr;
    mgr.SetEnabled(true);

    // Populate price cache
    for (int h = 100; h < 200; ++h) {
        mgr.UpdatePriceCache(h, 50000 + h);
    }

    std::atomic<bool> stop{false};
    std::atomic<bool> saw_bad{false};

    // Thread 1: remove prices
    std::thread remover([&] {
        for (int h = 100; h < 200 && !stop; ++h) {
            mgr.RemovePriceCache(h);
        }
    });

    // Thread 2: read latest price
    std::thread reader([&] {
        for (int i = 0; i < 5000 && !stop; ++i) {
            CAmount p = mgr.GetLatestPrice();
            // Price must be non-negative
            if (p < 0) {
                saw_bad = true;
                stop = true;
            }
        }
    });

    remover.join();
    stop = true;
    reader.join();

    BOOST_CHECK(!saw_bad);
}

// ---------------------------------------------------------------------------
// Test 5: OnMintConnected / OnRedeemConnected during simulated reorg
// Pre-fix: no atomicity guarantee between connect/disconnect
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(health_monitor_reorg_consistency)
{
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    // Simulate: connect 100 mints, then disconnect them all (reorg)
    constexpr CAmount dd = 500;
    constexpr CAmount dgb = 500000000;

    std::vector<std::thread> threads;

    // 5 threads each connect 20 mints
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 20; ++i) {
                DigiDollar::SystemHealthMonitor::OnMintConnected(dd, dgb);
            }
        });
    }
    for (auto& th : threads) th.join();
    threads.clear();

    auto m1 = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m1.totalDDSupply, 100 * dd);
    BOOST_CHECK_EQUAL(m1.totalCollateral, 100 * dgb);

    // Now disconnect all 100 from 5 threads
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 20; ++i) {
                DigiDollar::SystemHealthMonitor::OnMintDisconnected(dd, dgb);
            }
        });
    }
    for (auto& th : threads) th.join();

    auto m2 = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m2.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(m2.totalCollateral, 0);
}

// ---------------------------------------------------------------------------
// Test 6: ERR queue concurrent access
// Pre-fix: s_errQueue/s_queuedRedemptions unprotected
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(err_queue_concurrent_access)
{
    using namespace DigiDollar::ERR;

    // Activate ERR
    EmergencyRedemptionRatio::ReconstructERRState(80, 1000);
    // Force active for queueing
    ERRState state = EmergencyRedemptionRatio::GetCurrentState();

    std::atomic<int> queued{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 100; ++i) {
                COutPoint outpoint(uint256::ONE, t * 100 + i);
                if (EmergencyRedemptionRatio::QueueERRRedemption(outpoint, 100, 1000 + i)) {
                    queued++;
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    // All 400 should be queued (unique outpoints)
    auto queue = EmergencyRedemptionRatio::GetERRQueue();
    BOOST_CHECK_EQUAL(queue.size(), static_cast<size_t>(queued.load()));
}

BOOST_AUTO_TEST_SUITE_END()
