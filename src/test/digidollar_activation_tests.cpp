// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/digidollar.h>
#include <consensus/params.h>
#include <digidollar/digidollar.h>
#include <kernel/chainparams.h>
#include <chainparams.h>
#include <test/util/setup_common.h>
#include <versionbits.h>
#include <chain.h>
#include <util/time.h>

#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

// Test constants for better readability and maintainability
namespace {
    constexpr int64_t TEST_START_TIME = 1000000000;     // Past time for activation
    constexpr int64_t TEST_TIMEOUT = 3000000000;        // Far future timeout
    constexpr int64_t TEST_BLOCK_TIME = 1500000000;     // Test block timestamp
    constexpr uint32_t TEST_THRESHOLD = 3;              // Simplified threshold for testing
    constexpr uint32_t TEST_WINDOW = 4;                 // Simplified window for testing
}

// Forward declarations for helper functions we'll implement
static CBlockIndex* CreateTestBlock(CBlockIndex* pprev, int32_t nVersion, int64_t nTime);
static void SetMockTimeForTesting(int64_t nMockTime);
static Consensus::Params CreateTestParams(int64_t startTime, int64_t timeout);

BOOST_FIXTURE_TEST_SUITE(digidollar_activation_tests, RegTestingSetup)

/**
 * Test BIP9 state transitions for DigiDollar deployment.
 * REFACTOR Phase: Clean implementation with helper functions.
 */
BOOST_AUTO_TEST_CASE(test_bip9_state_transitions)
{
    // Create standardized test parameters
    Consensus::Params testParams = CreateTestParams(TEST_START_TIME, TEST_TIMEOUT);

    VersionBitsCache cache;
    CBlockIndex* tip = nullptr;

    // BIP9 evaluates state at period boundaries (heights where (h+1) % period == 0)
    // With TEST_WINDOW=4, period boundaries are at heights 3, 7, 11, 15, etc.

    // Mine initial blocks to establish proper MTP before start time
    // We need at least 11 blocks for MTP calculation
    // Mine blocks 0-10 before start time
    for (int i = 0; i < 11; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, TEST_START_TIME - 1000 + i * 10);
    }

    // At height 10, state is evaluated at height 7 (last period boundary)
    // At height 7, MTP is still before start time, so state should be DEFINED
    ThresholdState state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::DEFINED);

    // Mine more blocks past start time
    // Need enough blocks so that at the NEXT period boundary (height 11), MTP >= start time
    for (int i = 0; i < 10; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, TEST_START_TIME + 1000 + i * 10);
    }

    // At height 20, state is evaluated at height 19 (last period boundary)
    // At height 19, MTP should be past start time, so state should be STARTED
    state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::STARTED);

    // Generate more blocks without signaling - should remain STARTED
    for (int i = 0; i < TEST_WINDOW; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, TEST_START_TIME + 2000 + i * 10);
    }
    state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::STARTED);

    // Generate blocks WITH signaling to trigger LOCKED_IN
    uint32_t signal_bit = 1 << testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].bit;
    int32_t signaling_version = VERSIONBITS_TOP_BITS | signal_bit;

    // Signal in a complete period (4 blocks)
    // Need 3 out of 4 (75%) to reach threshold
    // This period will end at height 27 (next period boundary)
    for (int i = 0; i < 3; i++) {
        tip = CreateTestBlock(tip, signaling_version, TEST_START_TIME + 3000 + i * 10);
    }
    tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, TEST_START_TIME + 3030); // One non-signaling

    // Now at height 27 (period boundary), threshold should be met -> LOCKED_IN
    state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::LOCKED_IN);

    // Generate another complete period to move to ACTIVE
    for (int i = 0; i < TEST_WINDOW; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, TEST_START_TIME + 4000 + i * 10);
    }

    // Now at height 31 (period boundary), should transition to ACTIVE
    state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::ACTIVE);
}

/**
 * Test activation threshold mechanics.
 * GREEN Phase: Simplified test with smaller numbers.
 */
BOOST_AUTO_TEST_CASE(test_activation_threshold)
{
    const auto& consensusParams = Params().GetConsensus();

    // Create test params with smaller window for easier testing
    Consensus::Params testParams = consensusParams;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = 1000000000;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = 3000000000;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 0;
    testParams.nRuleChangeActivationThreshold = 3; // 3 out of 4 = 75%
    testParams.nMinerConfirmationWindow = 4;

    VersionBitsCache cache;
    CBlockIndex* tip = nullptr;

    // Mine initial blocks to establish MTP and reach STARTED state
    for (int i = 0; i < 11; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 999999900 + i * 10);
    }
    for (int i = 0; i < 10; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1000000100 + i * 10);
    }

    // Now at height 20, should be in STARTED state
    ThresholdState state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::STARTED);

    uint32_t signal_bit = 1 << testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].bit;
    int32_t signaling_version = VERSIONBITS_TOP_BITS | signal_bit;

    // Test just below threshold (2 out of 4 = 50% < 75%) - heights 21-24
    tip = CreateTestBlock(tip, signaling_version, 1500000001);
    tip = CreateTestBlock(tip, signaling_version, 1500000002);
    tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1500000003);
    tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1500000004);

    // At height 24 (not a period boundary), state evaluated at 23, should still be STARTED
    state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::STARTED);

    // Test at threshold (3 out of 4 = 75%) - heights 25-28 (period boundary at 27)
    tip = CreateTestBlock(tip, signaling_version, 1500000005);
    tip = CreateTestBlock(tip, signaling_version, 1500000006);
    tip = CreateTestBlock(tip, signaling_version, 1500000007);
    tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1500000008);

    // At height 28, state evaluated at 27 (period boundary), should be LOCKED_IN
    state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::LOCKED_IN);
}

/**
 * Test pre/post activation behavior for DigiDollar functionality.
 * GREEN Phase: Simplified test for basic enable/disable behavior.
 */
BOOST_AUTO_TEST_CASE(test_pre_post_activation_behavior)
{
    const auto& consensusParams = Params().GetConsensus();

    // Create simple test params
    Consensus::Params testParams = consensusParams;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = 1000000000;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = 3000000000;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 0;
    testParams.nRuleChangeActivationThreshold = 3;
    testParams.nMinerConfirmationWindow = 4;

    CBlockIndex* tip = nullptr;

    // Mine initial blocks to establish MTP and reach STARTED state
    for (int i = 0; i < 11; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 999999900 + i * 10);
    }
    for (int i = 0; i < 10; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1000000100 + i * 10);
    }

    // Should not be enabled in STARTED state
    BOOST_CHECK(!DigiDollar::IsDigiDollarEnabled(tip, testParams));

    // Simulate activation by creating blocks that reach ACTIVE state
    uint32_t signal_bit = 1 << testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].bit;
    int32_t signaling_version = VERSIONBITS_TOP_BITS | signal_bit;

    // Create signaling blocks (heights 21-24, period ends at 23)
    for (int i = 0; i < 3; i++) {
        tip = CreateTestBlock(tip, signaling_version, 1500000001 + i * 10);
    }
    tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1500000031);

    // Still in LOCKED_IN at height 24, should not be enabled yet
    BOOST_CHECK(!DigiDollar::IsDigiDollarEnabled(tip, testParams));

    // Complete activation by mining another period (heights 25-28, period ends at 27)
    for (int i = 0; i < 4; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1500000100 + i * 10);
    }

    // Now at height 28, should be ACTIVE and enabled
    BOOST_CHECK(DigiDollar::IsDigiDollarEnabled(tip, testParams));
}

/**
 * Test activation rollback scenarios during chain reorganization.
 * GREEN Phase: Basic test with simplified chain handling.
 */
BOOST_AUTO_TEST_CASE(test_activation_rollback)
{
    const auto& consensusParams = Params().GetConsensus();

    // Create test params
    Consensus::Params testParams = consensusParams;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = 1000000000;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = 3000000000;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 0;
    testParams.nRuleChangeActivationThreshold = 3;
    testParams.nMinerConfirmationWindow = 4;

    VersionBitsCache cache;

    // Create main chain with activation
    CBlockIndex* main_tip = nullptr;

    // Mine initial blocks to establish MTP
    for (int i = 0; i < 11; i++) {
        main_tip = CreateTestBlock(main_tip, VERSIONBITS_TOP_BITS, 999999900 + i * 10);
    }
    for (int i = 0; i < 10; i++) {
        main_tip = CreateTestBlock(main_tip, VERSIONBITS_TOP_BITS, 1000000100 + i * 10);
    }

    uint32_t signal_bit = 1 << testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].bit;
    int32_t signaling_version = VERSIONBITS_TOP_BITS | signal_bit;

    // Build chain to LOCKED_IN by signaling in a complete period (heights 21-24)
    for (int i = 0; i < 3; i++) {
        main_tip = CreateTestBlock(main_tip, signaling_version, 1500000001 + i * 10);
    }
    main_tip = CreateTestBlock(main_tip, VERSIONBITS_TOP_BITS, 1500000031);

    // At height 24, evaluated at period boundary 23, should be LOCKED_IN
    ThresholdState state = cache.State(main_tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::LOCKED_IN);

    // Create alternative chain without activation (separate chain from genesis)
    CBlockIndex* alt_tip = nullptr;

    // Mine initial blocks to establish MTP on alternative chain
    for (int i = 0; i < 11; i++) {
        alt_tip = CreateTestBlock(alt_tip, VERSIONBITS_TOP_BITS, 999999900 + i * 10);
    }
    for (int i = 0; i < 10; i++) {
        alt_tip = CreateTestBlock(alt_tip, VERSIONBITS_TOP_BITS, 1000000100 + i * 10);
    }

    // Continue without signaling for a full period (heights 21-24)
    for (int i = 0; i < 4; i++) {
        alt_tip = CreateTestBlock(alt_tip, VERSIONBITS_TOP_BITS, 1500000001 + i * 10);
    }

    // Alternative chain should still be in STARTED state
    state = cache.State(alt_tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::STARTED);

    // Test that cache properly handles different chain tips
    state = cache.State(main_tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::LOCKED_IN);
}

/**
 * Test timeout scenarios where deployment fails to activate.
 * GREEN Phase: Simple timeout test.
 */
BOOST_AUTO_TEST_CASE(test_activation_timeout)
{
    const auto& consensusParams = Params().GetConsensus();

    // Create test params with short timeout
    Consensus::Params testParams = consensusParams;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = 1000000000;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = 1500000000; // Short timeout
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 0;
    testParams.nRuleChangeActivationThreshold = 3;
    testParams.nMinerConfirmationWindow = 4;

    VersionBitsCache cache;
    CBlockIndex* tip = nullptr;

    // Mine initial blocks before start time
    for (int i = 0; i < 11; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 999999900 + i * 10);
    }

    // Mine blocks that cross start time to enter STARTED state (heights 11-20)
    for (int i = 0; i < 10; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1000000100 + i * 10);
    }

    // Verify we're in STARTED state
    ThresholdState state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::STARTED);

    // Generate blocks past timeout without sufficient signaling
    // Need to ensure MTP crosses the timeout threshold (1500000000)
    // Mine blocks with timestamps past timeout (heights 21-31)
    for (int i = 0; i < 11; i++) {
        tip = CreateTestBlock(tip, VERSIONBITS_TOP_BITS, 1600000000 + i * 100);
    }

    // At height 31 (period boundary), MTP should be past timeout, state should be FAILED
    state = cache.State(tip, testParams, Consensus::DEPLOYMENT_DIGIDOLLAR);
    BOOST_CHECK_EQUAL(state, ThresholdState::FAILED);

    // Verify DigiDollar is not enabled in FAILED state
    BOOST_CHECK(!DigiDollar::IsDigiDollarEnabled(tip, testParams));
}

BOOST_AUTO_TEST_SUITE_END()

// REFACTOR Phase - Improved helper functions with better design

namespace {
    // Static storage for test blocks - ensures memory management
    std::vector<std::unique_ptr<CBlockIndex>> g_test_blocks;

    // Constants for better readability and maintainability
    constexpr uint32_t DEFAULT_REGTEST_BITS = 0x207fffff;
}

/**
 * Create a test block for BIP9 activation testing.
 * REFACTOR Phase - Improved with better documentation and validation.
 *
 * @param pprev Previous block index (nullptr for genesis)
 * @param nVersion Block version (should include version bits)
 * @param nTime Block timestamp
 * @return Pointer to created block index (managed by static storage)
 */
static CBlockIndex* CreateTestBlock(CBlockIndex* pprev, int32_t nVersion, int64_t nTime)
{
    auto pindex = std::make_unique<CBlockIndex>();

    // Initialize block index fields
    pindex->pprev = pprev;
    pindex->nVersion = nVersion;
    pindex->nTime = nTime;
    pindex->nHeight = pprev ? pprev->nHeight + 1 : 0;
    pindex->nBits = DEFAULT_REGTEST_BITS;

    // Build skip pointers for efficient chain traversal
    pindex->BuildSkip();

    // Store the raw pointer before moving the unique_ptr
    CBlockIndex* result = pindex.get();
    g_test_blocks.push_back(std::move(pindex));

    return result;
}

/**
 * Set mock time for testing (placeholder).
 * REFACTOR Phase - Better documentation of future implementation.
 *
 * In a full implementation, this would integrate with the node's
 * mock time infrastructure for deterministic testing.
 *
 * @param nMockTime Unix timestamp to set as mock time
 */
static void SetMockTimeForTesting(int64_t nMockTime)
{
    // Future implementation would call SetMockTime(nMockTime)
    // For now, this is a placeholder for the interface
    (void)nMockTime;
}

/**
 * Create standardized test parameters for DigiDollar activation testing.
 * REFACTOR Phase - Centralized parameter creation for consistency.
 *
 * @param startTime Deployment start time (default: TEST_START_TIME)
 * @param timeout Deployment timeout (default: TEST_TIMEOUT)
 * @return Configured consensus parameters for testing
 */
static Consensus::Params CreateTestParams(int64_t startTime = TEST_START_TIME,
                                         int64_t timeout = TEST_TIMEOUT)
{
    const auto& consensusParams = Params().GetConsensus();
    Consensus::Params testParams = consensusParams;

    // Configure DigiDollar deployment
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = startTime;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = timeout;
    testParams.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 0;

    // Use simplified thresholds for testing
    testParams.nRuleChangeActivationThreshold = TEST_THRESHOLD;
    testParams.nMinerConfirmationWindow = TEST_WINDOW;

    return testParams;
}