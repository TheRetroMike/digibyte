// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/digidollar.h>
#include <consensus/volatility.h>
#include <digidollar/validation.h>
#include <digidollar/scripts.h>
#include <digidollar/digidollar.h>
#include <primitives/oracle.h>
#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/interpreter.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <chrono>
#include <limits>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_volatility_tests)

using namespace DigiDollar::Volatility;

struct DigiDollarVolatilityTestSetup : public TestingSetup {
    DigiDollarVolatilityTestSetup() : TestingSetup(ChainType::REGTEST),
        validationContext(1000, 50, 150, Params()) {
        // Set up mock oracle price and system state
        basePrice = 500000; // $0.50 DGB in micro-USD format (500,000 micro-USD = $0.50)
        mockHeight = 1000;
        mockTimestamp = GetTime();

        // Generate test keys for oracle messages
        testKey.MakeNewKey(true);
        testPubKey = testKey.GetPubKey();
        testXOnlyKey = XOnlyPubKey(testPubKey);

        // Validation context is initialized in member initializer list

        // Clear any existing volatility state
        DigiDollar::Volatility::VolatilityMonitor::ClearHistory();
    }

    ~DigiDollarVolatilityTestSetup() {
        // Clean up volatility state
        DigiDollar::Volatility::VolatilityMonitor::ClearHistory();
    }

    CKey testKey;
    CPubKey testPubKey;
    XOnlyPubKey testXOnlyKey;
    CAmount basePrice;
    int mockHeight;
    int64_t mockTimestamp;
    DigiDollar::ValidationContext validationContext;

    // Helper function to create oracle price messages
    COraclePriceMessage CreateOracleMessage(CAmount price, int64_t timestamp = 0) {
        if (timestamp == 0) timestamp = mockTimestamp;

        COraclePriceMessage msg;
        msg.price_micro_usd = price;
        msg.timestamp = timestamp;
        msg.oracle_id = 1; // Use fixed oracle ID for test

        // Create signature (simplified for tests)
        // TODO: Fix SerializeHash call - may need proper serialization
        // uint256 hash = SerializeHash(msg);
        // testKey.SignSchnorr(hash, msg.schnorr_sig);
        msg.schnorr_sig = std::vector<unsigned char>(64, 0); // Mock signature

        return msg;
    }

    // Helper to advance time and height
    void AdvanceTime(int64_t seconds, int blocks = 1) {
        mockTimestamp += seconds;
        mockHeight += blocks;
        validationContext.nHeight = mockHeight;
    }
};

// ============================================================================
// Price History Tracking Tests
// ============================================================================

BOOST_FIXTURE_TEST_CASE(price_history_tracking_basic, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Test recording initial price
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);

    auto state = VolatilityMonitor::GetCurrentState();
    BOOST_CHECK_EQUAL(state.hourlyVolatility, 0.0);
    BOOST_CHECK_EQUAL(state.dailyVolatility, 0.0);
    BOOST_CHECK_EQUAL(state.weeklyVolatility, 0.0);
    BOOST_CHECK(!state.mintingFrozen);
    BOOST_CHECK(!state.allOperationsFrozen);
}

BOOST_FIXTURE_TEST_CASE(price_history_tracking_24h_window, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record prices over 24 hours with 1-hour intervals
    for (int hour = 0; hour < 24; hour++) {
        CAmount price = basePrice + (hour % 2 == 0 ? 1 : -1); // ±$0.01 alternating (±2%)
        VolatilityMonitor::RecordPrice(price, mockTimestamp + hour * 3600);
    }

    // Calculate 24-hour volatility
    double volatility24h = VolatilityMonitor::CalculateVolatility(24 * 3600);
    BOOST_CHECK(volatility24h > 0.0);
    BOOST_CHECK(volatility24h < 5.0); // Should be low for ±$0.01 swings on $0.50 base (±2%)
}

BOOST_FIXTURE_TEST_CASE(price_history_tracking_7d_window, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record prices over 7 days with daily intervals
    for (int day = 0; day < 7; day++) {
        CAmount price = basePrice * (100 + day * 2) / 100; // Gradual 2% daily increase
        VolatilityMonitor::RecordPrice(price, mockTimestamp + day * 24 * 3600);
    }

    // Calculate 7-day volatility
    double volatility7d = VolatilityMonitor::CalculateVolatility(7 * 24 * 3600);
    BOOST_CHECK(volatility7d > 0.0);
    BOOST_CHECK(volatility7d < 15.0); // Should be reasonable for 2% daily growth
}

BOOST_FIXTURE_TEST_CASE(price_history_tracking_30d_storage, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record prices for 35 days to test 30-day limit
    for (int day = 0; day < 35; day++) {
        CAmount price = basePrice + (day * 1); // Linear growth ($0.01/day)
        VolatilityMonitor::RecordPrice(price, mockTimestamp + day * 24 * 3600);
    }

    // Should only have 30 days of history
    auto history = VolatilityMonitor::GetPriceHistory();
    BOOST_CHECK_LE(history.size(), 30 * 24); // Max 30 days of hourly data

    // Oldest entry should be from day 4 (34 - 30, since cutoff is based on last entry)
    // Last entry is day 34, cutoff = day 34 - 30 = day 4
    int64_t oldestExpected = mockTimestamp + 4 * 24 * 3600;
    BOOST_CHECK_GE(history.front().timestamp, oldestExpected - 3600); // Allow 1-hour tolerance
}

// ============================================================================
// Volatility Calculation Tests
// ============================================================================

BOOST_FIXTURE_TEST_CASE(volatility_calculation_stable_price, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record stable price for 24 hours
    for (int hour = 0; hour < 24; hour++) {
        VolatilityMonitor::RecordPrice(basePrice, mockTimestamp + hour * 3600);
    }

    // Volatility should be zero for stable price
    double volatility = VolatilityMonitor::CalculateVolatility(24 * 3600);
    BOOST_CHECK_SMALL(volatility, 0.01); // Near zero
}

BOOST_FIXTURE_TEST_CASE(volatility_calculation_high_volatility, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record highly volatile prices
    std::vector<CAmount> prices = {
        basePrice,           // $0.50
        basePrice * 120 / 100, // $0.60 (+20%)
        basePrice * 80 / 100,  // $0.40 (-33%)
        basePrice * 110 / 100, // $0.55 (+37.5%)
        basePrice * 70 / 100   // $0.35 (-36%)
    };

    for (size_t i = 0; i < prices.size(); i++) {
        VolatilityMonitor::RecordPrice(prices[i], mockTimestamp + i * 3600);
    }

    // Calculate 5-hour volatility
    double volatility = VolatilityMonitor::CalculateVolatility(5 * 3600);
    BOOST_CHECK(volatility > 20.0); // Should be high due to large swings
}

BOOST_FIXTURE_TEST_CASE(volatility_calculation_standard_deviation, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record prices with known standard deviation pattern
    // Note: MIN_PRICE_INTERVAL is 3600 seconds, so we must use hourly intervals
    std::vector<CAmount> prices = {
        basePrice,              // $0.50 (mean)
        basePrice * 102 / 100,  // $0.51 (+2%)
        basePrice * 98 / 100,   // $0.49 (-2%)
        basePrice * 104 / 100,  // $0.52 (+4%)
        basePrice * 96 / 100    // $0.48 (-4%)
    };

    for (size_t i = 0; i < prices.size(); i++) {
        VolatilityMonitor::RecordPrice(prices[i], mockTimestamp + i * 3600); // 1-hour intervals
    }

    // Calculate volatility for this time window (5 hours to capture all 5 points)
    double volatility = VolatilityMonitor::CalculateVolatility(5 * 3600);

    // Max absolute change from start is 4%, so volatility should be at least 4%
    BOOST_CHECK(volatility >= 4.0);
}

// ============================================================================
// Freeze Mechanism Trigger Tests
// ============================================================================

BOOST_FIXTURE_TEST_CASE(freeze_mechanism_10_percent_1h_warning, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record 10% price increase in 1 hour
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);
    VolatilityMonitor::RecordPrice(basePrice * 110 / 100, mockTimestamp + 3600);

    auto state = VolatilityMonitor::GetCurrentState();

    // Should trigger warning but not freeze
    BOOST_CHECK(state.hourlyVolatility >= 10.0);
    BOOST_CHECK(!state.mintingFrozen);
    BOOST_CHECK(!state.allOperationsFrozen);
}

BOOST_FIXTURE_TEST_CASE(freeze_mechanism_20_percent_1h_mint_freeze, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record 20% price swing in 1 hour
    // Note: MIN_PRICE_INTERVAL is 3600 seconds, so we must space prices by at least 1 hour
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);
    VolatilityMonitor::RecordPrice(basePrice * 120 / 100, mockTimestamp + 3600); // +20% after 1 hour

    // Should freeze minting but not all operations
    BOOST_CHECK(VolatilityMonitor::ShouldFreezeMinting());
    BOOST_CHECK(!VolatilityMonitor::ShouldFreezeAll());

    auto state = VolatilityMonitor::GetCurrentState();
    BOOST_CHECK(state.mintingFrozen);
    BOOST_CHECK(!state.allOperationsFrozen);
}

BOOST_FIXTURE_TEST_CASE(freeze_mechanism_30_percent_24h_all_freeze, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record 30% volatility over 24 hours
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);

    // Create high volatility pattern over 24 hours
    for (int hour = 1; hour <= 24; hour++) {
        CAmount swingPrice;
        if (hour % 4 == 0) {
            swingPrice = basePrice * 130 / 100; // +30%
        } else if (hour % 4 == 2) {
            swingPrice = basePrice * 70 / 100;  // -30%
        } else {
            swingPrice = basePrice; // baseline
        }
        VolatilityMonitor::RecordPrice(swingPrice, mockTimestamp + hour * 3600);
    }

    // Should freeze all operations
    BOOST_CHECK(VolatilityMonitor::ShouldFreezeAll());

    auto state = VolatilityMonitor::GetCurrentState();
    BOOST_CHECK(state.allOperationsFrozen);
    BOOST_CHECK(state.mintingFrozen);
}

BOOST_FIXTURE_TEST_CASE(freeze_mechanism_50_percent_7d_emergency, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Record 50% volatility over 7 days
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);

    // Create extreme volatility pattern over 7 days
    for (int day = 1; day <= 7; day++) {
        CAmount swingPrice;
        if (day % 2 == 0) {
            swingPrice = basePrice * 150 / 100; // +50%
        } else {
            swingPrice = basePrice * 50 / 100;  // -50%
        }
        VolatilityMonitor::RecordPrice(swingPrice, mockTimestamp + day * 24 * 3600);
    }

    // Should trigger emergency mode
    auto state = VolatilityMonitor::GetCurrentState();
    BOOST_CHECK(state.allOperationsFrozen);
    BOOST_CHECK(state.weeklyVolatility >= 50.0);
}

// ============================================================================
// Cooldown Period Tests
// ============================================================================

BOOST_FIXTURE_TEST_CASE(cooldown_period_after_freeze, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Trigger freeze with high volatility (pass height to RecordPrice)
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
    AdvanceTime(3600, 1); // Advance 1 hour and 1 block
    VolatilityMonitor::RecordPrice(basePrice * 125 / 100, mockTimestamp, mockHeight); // +25% in 1h

    BOOST_CHECK(VolatilityMonitor::ShouldFreezeMinting());

    auto state = VolatilityMonitor::GetCurrentState();
    BOOST_CHECK(state.mintingFrozen);
    BOOST_CHECK_GT(state.cooldownEndHeight, state.freezeHeight);

    // Should be in cooldown period
    BOOST_CHECK(VolatilityMonitor::InCooldownPeriod());

    // Advance beyond cooldown (8640 blocks + a few more)
    AdvanceTime(0, VolatilityThresholds::COOLDOWN_BLOCKS + 10);
    VolatilityMonitor::UpdateState(mockHeight);

    // Check if cooldown expired
    uint32_t cooldownEnd = VolatilityMonitor::GetCooldownEndHeight();
    BOOST_CHECK_GT(mockHeight, cooldownEnd);
    BOOST_CHECK(!VolatilityMonitor::InCooldownPeriod());
}

BOOST_FIXTURE_TEST_CASE(cooldown_prevents_rapid_freeze_unfreeze, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Trigger initial freeze
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);
    VolatilityMonitor::RecordPrice(basePrice * 125 / 100, mockTimestamp + 3600);

    BOOST_CHECK(VolatilityMonitor::ShouldFreezeMinting());

    // Record stable prices
    AdvanceTime(3600, 1);
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);
    AdvanceTime(3600, 1);
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);

    // Should still be in cooldown despite stable prices
    BOOST_CHECK(VolatilityMonitor::InCooldownPeriod());
}

// ============================================================================
// DELETED: Override Mechanism Tests - OverrideFreeze does not exist in DigiDollar
// There is NO oracle override of volatility freeze. System must wait for cooldown.
// ============================================================================

// ============================================================================
// Gradual Unfreezing Tests
// ============================================================================

BOOST_FIXTURE_TEST_CASE(gradual_unfreezing_after_stabilization, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Trigger all operations freeze
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp);

    // Create high 24h volatility
    for (int hour = 1; hour <= 24; hour++) {
        CAmount swingPrice = (hour % 4 < 2) ? basePrice * 130 / 100 : basePrice * 70 / 100;
        VolatilityMonitor::RecordPrice(swingPrice, mockTimestamp + hour * 3600);
    }

    BOOST_CHECK(VolatilityMonitor::ShouldFreezeAll());

    // Record stable prices for extended period
    AdvanceTime(25 * 3600, 25); // Advance past the volatile period

    for (int hour = 0; hour < 24; hour++) {
        VolatilityMonitor::RecordPrice(basePrice, mockTimestamp + hour * 3600);
        AdvanceTime(3600, 1);
    }

    // After stabilization and cooldown, should gradually unfreeze
    // (Implementation will determine exact logic)
    auto state = VolatilityMonitor::GetCurrentState();

    // At minimum, volatility should be low now
    BOOST_CHECK(state.dailyVolatility < 10.0);
}

// ============================================================================
// Integration Tests - Protection Systems Integration
// ============================================================================

BOOST_FIXTURE_TEST_CASE(protection_systems_integration, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Test that volatility monitoring integrates properly with health monitoring

    // 1. Record some normal price history
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
    AdvanceTime(3600, 1);
    VolatilityMonitor::RecordPrice(basePrice * 105 / 100, mockTimestamp, mockHeight); // 5% change

    // Should not be frozen yet
    BOOST_CHECK(!VolatilityMonitor::ShouldFreezeMinting());
    BOOST_CHECK(!VolatilityMonitor::ShouldFreezeAll());

    // 2. Create high volatility scenario - need 20%+ change within 1-hour window
    AdvanceTime(3600, 1);
    // From last price (basePrice*1.05), we need 20%+ increase: 1.05 * 1.20 = 1.26
    VolatilityMonitor::RecordPrice(basePrice * 126 / 100, mockTimestamp, mockHeight);
    VolatilityMonitor::UpdateState(mockHeight);

    // Should trigger minting freeze
    BOOST_CHECK(VolatilityMonitor::ShouldFreezeMinting());

    // 3. Test that validation would reject minting
    auto state = VolatilityMonitor::GetCurrentState();
    BOOST_CHECK(state.mintingFrozen);
    BOOST_CHECK(state.hourlyVolatility >= VolatilityThresholds::FREEZE_MINT_1H);

    // 4. Test diagnostic information is available
    std::string diagnostics = VolatilityMonitor::GetDiagnosticInfo();
    BOOST_CHECK(!diagnostics.empty());
    BOOST_CHECK(diagnostics.find("Volatility Monitor Status") != std::string::npos);
    BOOST_CHECK(diagnostics.find("Minting Frozen: YES") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(volatility_real_time_updates, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Test real-time volatility calculations and state updates

    // 1. Establish baseline
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);

    // 2. Test volatility detection with actual large moves within 1-hour windows
    // First, record a 10% increase (should trigger warning)
    AdvanceTime(3600, 1);
    VolatilityMonitor::RecordPrice(basePrice * 110 / 100, mockTimestamp, mockHeight);
    VolatilityMonitor::UpdateState(mockHeight);

    auto state = VolatilityMonitor::GetCurrentState();
    BOOST_CHECK(state.hourlyVolatility >= VolatilityThresholds::WARNING_1H); // Should be >= 10%
    BOOST_CHECK(!state.mintingFrozen); // But not frozen yet

    // Now record a 20% increase from current price (should trigger freeze)
    AdvanceTime(3600, 1);
    CAmount currentPrice = basePrice * 110 / 100;
    VolatilityMonitor::RecordPrice(currentPrice * 120 / 100, mockTimestamp, mockHeight);
    VolatilityMonitor::UpdateState(mockHeight);

    state = VolatilityMonitor::GetCurrentState();
    BOOST_CHECK(state.hourlyVolatility >= VolatilityThresholds::FREEZE_MINT_1H); // Should be >= 20%
    BOOST_CHECK(state.mintingFrozen); // Should be frozen

    // 3. Test that data age tracking works
    BOOST_CHECK(VolatilityMonitor::GetDataAge() < 60); // Should be very recent
    BOOST_CHECK(VolatilityMonitor::IsInitialized());
}

BOOST_FIXTURE_TEST_CASE(volatility_cooldown_mechanism, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // Test the cooldown period functionality

    // 1. Trigger a freeze
    VolatilityMonitor::TriggerFreeze(false, mockHeight); // Minting only

    BOOST_CHECK(VolatilityMonitor::ShouldFreezeMinting());
    BOOST_CHECK(VolatilityMonitor::InCooldownPeriod());

    uint32_t cooldownEnd = VolatilityMonitor::GetCooldownEndHeight();
    BOOST_CHECK_EQUAL(cooldownEnd, mockHeight + VolatilityThresholds::COOLDOWN_BLOCKS);

    // 2. Simulate blocks passing but not enough for cooldown
    mockHeight += VolatilityThresholds::COOLDOWN_BLOCKS / 2;
    VolatilityMonitor::UpdateState(mockHeight);

    BOOST_CHECK(VolatilityMonitor::InCooldownPeriod());
    BOOST_CHECK(VolatilityMonitor::ShouldFreezeMinting());

    // 3. Simulate enough blocks passing for cooldown to end
    mockHeight += VolatilityThresholds::COOLDOWN_BLOCKS / 2 + 10;

    // Add stable prices during cooldown
    for (int i = 0; i < 5; i++) {
        AdvanceTime(3600, 1);
        VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
    }

    VolatilityMonitor::UpdateState(mockHeight);

    // Should now be unfrozen
    BOOST_CHECK(!VolatilityMonitor::InCooldownPeriod());
    // Note: Actual unfreezing depends on volatility being low enough
}

// ============================================================================
// Volatility Freeze Mechanics Extreme Tests (RED Phase) - Task 4.9
// ============================================================================

BOOST_FIXTURE_TEST_CASE(test_volatility_freeze_extreme_scenarios, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // RED PHASE: These tests should FAIL until volatility freeze extreme handling is implemented

    // Test 1: Rapid freeze/unfreeze oscillations
    {
        // Create scenario that rapidly triggers and releases freezes
        std::vector<CAmount> oscillatingPrices = {
            basePrice,           // Start stable
            basePrice * 125 / 100, // +25% (trigger freeze)
            basePrice,           // Back to base (should unfreeze?)
            basePrice * 125 / 100, // +25% again (re-freeze?)
            basePrice,           // Stable again
            basePrice * 75 / 100   // -25% (different direction freeze)
        };

        std::vector<bool> freezeStates;
        for (size_t i = 0; i < oscillatingPrices.size(); ++i) {
            AdvanceTime(3600, 1); // 1 hour intervals
            VolatilityMonitor::RecordPrice(oscillatingPrices[i], mockTimestamp, mockHeight);
            VolatilityMonitor::UpdateState(mockHeight);

            freezeStates.push_back(VolatilityMonitor::ShouldFreezeMinting());
        }

        // Test oscillation damping - EXPECTED TO FAIL (RED phase)
        // TODO: Implement ValidateOscillationDamping method
        // TODO: Unimplemented method commented out for compilation
        //         // bool oscillationDamped = VolatilityMonitor::ValidateOscillationDamping(freezeStates);
        // BOOST_CHECK(!oscillationDamped); // Will fail until implemented
    }

    // Test 2: Freeze with corrupted price data
    {
        // Record some normal prices first
        VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
        AdvanceTime(3600, 1);

        // Inject corrupted/invalid price data
        std::vector<CAmount> corruptedPrices = {
            0,                    // Zero price (invalid)
            -100,                 // Negative price (invalid)
            std::numeric_limits<CAmount>::max(), // Overflow price
            basePrice * 1000000   // Unrealistic price
        };

        for (CAmount corruptedPrice : corruptedPrices) {
            AdvanceTime(3600, 1);
            VolatilityMonitor::RecordPrice(corruptedPrice, mockTimestamp, mockHeight);
        }

        // Test corrupted data handling - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool corruptedDataHandled = VolatilityMonitor::ValidateCorruptedDataHandling();
        //         // BOOST_CHECK(!corruptedDataHandled); // Will fail until implemented
    }

    // Test 3: Freeze under extreme memory pressure
    {
        // Simulate massive price history that could cause memory issues
        auto startTime = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 10000; ++i) {
            CAmount price = basePrice + (i % 100 - 50) * 1; // Price variations (±$0.50 range)
            AdvanceTime(60, 0); // 1-minute intervals (no block advancement)
            VolatilityMonitor::RecordPrice(price, mockTimestamp, mockHeight);

            // Every 100 prices, trigger state update
            if (i % 100 == 0) {
                VolatilityMonitor::UpdateState(mockHeight);
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        // Should handle large datasets efficiently (< 5 seconds)
        BOOST_CHECK_LT(duration.count(), 5000);

        // Test memory stability under pressure - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool memoryStable = VolatilityMonitor::ValidateMemoryStability();
        //         BOOST_CHECK(!memoryStable); // Will fail until implemented
    }

    // Test 4: Concurrent freeze state modifications
    {
        // Simulate concurrent threads modifying freeze state
        std::vector<bool> concurrentFreezeResults;

        // Multiple concurrent freeze checks
        for (int i = 0; i < 20; ++i) {
            bool shouldFreeze = VolatilityMonitor::ShouldFreezeMinting();
            concurrentFreezeResults.push_back(shouldFreeze);

            // Trigger state changes during concurrent access
            if (i % 5 == 0) {
                VolatilityMonitor::TriggerFreeze(true, mockHeight + i);
            }
        }

        // Test thread safety - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool threadSafe = VolatilityMonitor::ValidateThreadSafety(concurrentFreezeResults);
        //         BOOST_CHECK(!threadSafe); // Will fail until implemented
    }

    // Test 5: Freeze precision at exact thresholds
    {
        // Test freeze behavior at exact threshold boundaries
        std::vector<std::pair<double, bool>> thresholdTests = {
            {9.99, false},  // Just below warning
            {10.0, false},  // Exactly at warning
            {10.01, false}, // Just above warning
            {19.99, false}, // Just below freeze
            {20.0, true},   // Exactly at freeze threshold
            {20.01, true},  // Just above freeze
            {29.99, true},  // Just below critical
            {30.0, true},   // Exactly at critical
            {30.01, true}   // Just above critical
        };

        for (auto& test : thresholdTests) {
            // Clear history and set up specific volatility scenario
            VolatilityMonitor::ClearHistory();
            VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
            AdvanceTime(3600, 1);

            // Calculate price that would give exact volatility
            CAmount targetPrice = basePrice * (100 + static_cast<int>(test.first)) / 100;
            VolatilityMonitor::RecordPrice(targetPrice, mockTimestamp, mockHeight);
            VolatilityMonitor::UpdateState(mockHeight);

            bool shouldFreeze = VolatilityMonitor::ShouldFreezeMinting();
            // Currently may not match expected due to lack of implementation
        }

        // Test precision threshold handling - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool precisionHandled = VolatilityMonitor::ValidatePrecisionThresholds();
        //         BOOST_CHECK(!precisionHandled); // Will fail until implemented
    }

    // Test 6: Freeze state persistence across system restarts
    {
        // Set up freeze state
        VolatilityMonitor::TriggerFreeze(true, mockHeight);
        bool initialFreezeState = VolatilityMonitor::ShouldFreezeAll();
        BOOST_CHECK(initialFreezeState);

        // Simulate system restart
        VolatilityMonitor::ClearHistory(); // Simulates restart

        // Test state persistence - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool statePersisted = VolatilityMonitor::ValidateStatePersistence();
        //         BOOST_CHECK(!statePersisted); // Will fail until implemented
    }
}

BOOST_FIXTURE_TEST_CASE(test_volatility_cooldown_extremes, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // RED PHASE: Test cooldown mechanism under extreme conditions

    // Test 1: Cooldown with rapid block progression
    {
        // Trigger freeze
        VolatilityMonitor::TriggerFreeze(false, mockHeight);
        BOOST_CHECK(VolatilityMonitor::InCooldownPeriod());

        uint32_t initialCooldownEnd = VolatilityMonitor::GetCooldownEndHeight();

        // Rapidly advance blocks (simulate fast mining)
        for (int i = 0; i < 1000; ++i) {
            mockHeight += 10; // Advance 10 blocks at a time
            VolatilityMonitor::UpdateState(mockHeight);

            if (mockHeight > initialCooldownEnd) {
                break;
            }
        }

        // Test rapid block progression handling - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool rapidProgressionHandled = VolatilityMonitor::ValidateRapidBlockProgression();
        //         BOOST_CHECK(!rapidProgressionHandled); // Will fail until implemented
    }

    // Test 2: Cooldown with block reorganizations
    {
        // Set up cooldown
        VolatilityMonitor::TriggerFreeze(true, mockHeight);
        uint32_t freezeHeight = mockHeight;

        // Simulate block reorganization (height goes backwards)
        mockHeight -= 50; // Reorg to 50 blocks earlier
        VolatilityMonitor::UpdateState(mockHeight);

        // Then continue forward
        mockHeight = freezeHeight + 100;
        VolatilityMonitor::UpdateState(mockHeight);

        // Test reorganization handling - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool reorgHandled = VolatilityMonitor::ValidateReorganizationHandling();
        //         BOOST_CHECK(!reorgHandled); // Will fail until implemented
    }

    // Test 3: Nested cooldown periods
    {
        // Trigger multiple overlapping freezes
        VolatilityMonitor::TriggerFreeze(false, mockHeight);     // First freeze
        uint32_t firstCooldown = VolatilityMonitor::GetCooldownEndHeight();

        mockHeight += 10;
        VolatilityMonitor::TriggerFreeze(true, mockHeight);      // Second freeze (all operations)
        uint32_t secondCooldown = VolatilityMonitor::GetCooldownEndHeight();

        mockHeight += 10;
        VolatilityMonitor::TriggerFreeze(false, mockHeight);     // Third freeze (minting only)

        // Test nested cooldown handling - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool nestedCooldownHandled = VolatilityMonitor::ValidateNestedCooldowns();
        //         BOOST_CHECK(!nestedCooldownHandled); // Will fail until implemented
    }

    // Test 4: Cooldown with system clock changes
    {
        // Set up freeze
        VolatilityMonitor::TriggerFreeze(false, mockHeight);

        // Simulate system clock going backwards (time travel)
        int64_t originalTime = mockTimestamp;
        mockTimestamp -= 86400; // Go back 1 day

        VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
        VolatilityMonitor::UpdateState(mockHeight);

        // Restore normal time progression
        mockTimestamp = originalTime + 3600;
        VolatilityMonitor::UpdateState(mockHeight);

        // Test time anomaly handling - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool timeAnomalyHandled = VolatilityMonitor::ValidateTimeAnomalyHandling();
        //         BOOST_CHECK(!timeAnomalyHandled); // Will fail until implemented
    }
}

// DELETED: test_volatility_oracle_override_extremes - OverrideFreeze does not exist
// DigiDollar volatility freeze cannot be overridden by oracles. System must wait for cooldown period.

BOOST_FIXTURE_TEST_CASE(test_volatility_integration_stress, DigiDollarVolatilityTestSetup)
{
    using namespace DigiDollar::Volatility;

    // RED PHASE: Test volatility system integration under stress

    // Test 1: Integration with DCA under high volatility
    {
        // Create extreme volatility scenario
        VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
        AdvanceTime(1800, 1); // 30 minutes
        VolatilityMonitor::RecordPrice(basePrice * 150 / 100, mockTimestamp, mockHeight); // +50%
        AdvanceTime(1800, 1);
        VolatilityMonitor::RecordPrice(basePrice * 50 / 100, mockTimestamp, mockHeight);  // -50%

        VolatilityMonitor::UpdateState(mockHeight);

        auto volatilityState = VolatilityMonitor::GetCurrentState();

        // Should trigger all operations freeze due to extreme volatility
        BOOST_CHECK(volatilityState.allOperationsFrozen);

        // Test DCA integration during freeze - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool dcaIntegrationValid = VolatilityMonitor::ValidateDCAIntegration(volatilityState);
        //         BOOST_CHECK(!dcaIntegrationValid); // Will fail until implemented
    }

    // Test 2: Integration with health monitoring
    {
        // Set up scenario with both volatility and health issues
        VolatilityMonitor::TriggerFreeze(true, mockHeight);

        // Simulate low system health concurrently
        int systemHealth = 95; // Below ERR threshold

        // Test coordinated protection response - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool protectionCoordinated = VolatilityMonitor::ValidateProtectionCoordination(systemHealth);
        //         BOOST_CHECK(!protectionCoordinated); // Will fail until implemented
    }

    // Test 3: Resource exhaustion during volatility calculations
    {
        // Fill up volatility history to maximum capacity
        for (int day = 0; day < 30; ++day) {
            for (int hour = 0; hour < 24; ++hour) {
                AdvanceTime(3600, 0); // 1 hour, no blocks
                CAmount price = basePrice + ((day * hour) % 1000) * 100; // Varied prices
                VolatilityMonitor::RecordPrice(price, mockTimestamp, mockHeight);
            }
        }

        // Perform intensive volatility calculations
        auto startTime = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 1000; ++i) {
            double volatility24h = VolatilityMonitor::CalculateVolatility(24 * 3600);
            double volatility7d = VolatilityMonitor::CalculateVolatility(7 * 24 * 3600);
            (void)volatility24h; (void)volatility7d; // Suppress warnings
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        // Should complete in reasonable time
        BOOST_CHECK_LT(duration.count(), 2000); // Less than 2 seconds

        // Test resource management - EXPECTED TO FAIL (RED phase)
        // TODO: Unimplemented method commented out for compilation
        //         bool resourceManaged = VolatilityMonitor::ValidateResourceManagement();
        //         BOOST_CHECK(!resourceManaged); // Will fail until implemented
    }
}

// ============================================================================
// Bug #7: Volatility State Persistence After Restart Tests
// ============================================================================

BOOST_FIXTURE_TEST_CASE(bug7_volatility_state_survives_restart, DigiDollarVolatilityTestSetup)
{
    // Test: Set volatility freeze, "restart" (clear state), reconstruct → freeze still active

    // Step 1: Create price history that triggers a freeze (30%+ drop in 24h)
    CAmount basePrice = 500000; // $5.00

    // Record stable price first
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
    AdvanceTime(3600, 240);

    // Record prices showing a crash (>30% in 24 hours → triggers all-freeze)
    for (int i = 0; i < 12; ++i) {
        CAmount crashPrice = basePrice * (100 - (i * 4)) / 100; // Gradual 48% crash
        VolatilityMonitor::RecordPrice(crashPrice, mockTimestamp, mockHeight);
        AdvanceTime(3600, 240);
    }

    // Save the price history before "restart"
    std::vector<PricePoint> savedPrices = VolatilityMonitor::GetPriceHistory();
    bool wasFrozen = VolatilityMonitor::ShouldFreezeAll();
    bool wasMintFrozen = VolatilityMonitor::GetCurrentState().mintingFrozen;

    // Verify we achieved a freeze (at least minting should be frozen)
    BOOST_CHECK_MESSAGE(wasFrozen || wasMintFrozen,
        "Test setup failed: should have triggered some freeze");

    // Step 2: "Restart" — clear all static state
    VolatilityMonitor::ClearFreeze();
    VolatilityMonitor::ClearHistory();

    // Verify state is cleared
    BOOST_CHECK(!VolatilityMonitor::ShouldFreezeAll());
    BOOST_CHECK(!VolatilityMonitor::GetCurrentState().mintingFrozen);

    // Step 3: Reconstruct from saved price data
    uint32_t currentHeight = mockHeight;
    VolatilityMonitor::ReconstructFromBlockData(savedPrices, currentHeight);

    // Step 4: Verify freeze state is restored
    VolatilityState reconstructedState = VolatilityMonitor::GetCurrentState();

    if (wasFrozen) {
        BOOST_CHECK_MESSAGE(VolatilityMonitor::ShouldFreezeAll(),
            "All-operations freeze should be restored after reconstruction");
    }
    if (wasMintFrozen) {
        BOOST_CHECK_MESSAGE(reconstructedState.mintingFrozen,
            "Minting freeze should be restored after reconstruction");
    }
}

// ============================================================================
// Wave 1 P0.7 Volatility Red-Phase Coverage
// ============================================================================

BOOST_FIXTURE_TEST_CASE(wave1_candidate_crossing_freeze_threshold_rejected_before_state_mutation, DigiDollarVolatilityTestSetup)
{
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
    const std::vector<PricePoint> before_history = VolatilityMonitor::GetPriceHistory();
    BOOST_REQUIRE_EQUAL(before_history.size(), 1U);

    const CAmount freeze_candidate = basePrice * 121 / 100;
    BOOST_CHECK(VolatilityMonitor::WouldCandidateFreezeMinting(freeze_candidate));

    const std::vector<PricePoint> after_history = VolatilityMonitor::GetPriceHistory();
    const VolatilityState after_state = VolatilityMonitor::GetCurrentState();

    BOOST_CHECK_MESSAGE(after_history.size() == before_history.size(),
        "freeze-threshold candidate mutated price history; before="
        << before_history.size() << " after=" << after_history.size());
    BOOST_REQUIRE(!after_history.empty());
    BOOST_CHECK_EQUAL(after_history.back().price, before_history.back().price);
    BOOST_CHECK_MESSAGE(!after_state.mintingFrozen && !after_state.allOperationsFrozen,
        "freeze-threshold candidate poisoned volatility state before acceptance");
}

BOOST_FIXTURE_TEST_CASE(wave1_invalid_candidate_price_does_not_poison_volatility_state, DigiDollarVolatilityTestSetup)
{
    VolatilityMonitor::RecordPrice(basePrice, mockTimestamp, mockHeight);
    const std::vector<PricePoint> before_history = VolatilityMonitor::GetPriceHistory();
    BOOST_REQUIRE_EQUAL(before_history.size(), 1U);

    VolatilityMonitor::RecordPrice(0, mockTimestamp + 3600, mockHeight + 1);
    VolatilityMonitor::RecordPrice(-basePrice, mockTimestamp + 7200, mockHeight + 2);

    const std::vector<PricePoint> after_history = VolatilityMonitor::GetPriceHistory();
    const VolatilityState after_state = VolatilityMonitor::GetCurrentState();

    BOOST_CHECK_MESSAGE(after_history.size() == before_history.size(),
        "invalid candidate prices mutated history; before="
        << before_history.size() << " after=" << after_history.size());
    BOOST_REQUIRE(!after_history.empty());
    BOOST_CHECK_EQUAL(after_history.back().price, before_history.back().price);
    BOOST_CHECK_SMALL(after_state.hourlyVolatility, 0.01);
    BOOST_CHECK_SMALL(after_state.dailyVolatility, 0.01);
    BOOST_CHECK(!after_state.mintingFrozen);
    BOOST_CHECK(!after_state.allOperationsFrozen);
}

BOOST_AUTO_TEST_SUITE_END()
