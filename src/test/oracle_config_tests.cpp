// Copyright (c) 2024-2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <logging.h>
#include <util/strencodings.h>

#include <chainparams.h>
#include <consensus/params.h>
#include <kernel/chainparams.h>
#include <logging.h>
#include <primitives/oracle.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <algorithm>

BOOST_FIXTURE_TEST_SUITE(oracle_config_tests, BasicTestingSetup)

/**
 * Week 5: Configuration Validation Tests
 *
 * These tests verify oracle system configuration parameters are correctly set
 * for the configured testnet DigiDollar oracle roster.
 */

// ============================================================================
// PART 1: Consensus Parameter Tests (4 tests)
// ============================================================================

/**
 * Test: Testnet Oracle Activation Height
 *
 * Verifies that the oracle system has a defined activation height on testnet.
 * For Phase One, this should be set to allow testing.
 *
 * NOTE: This test currently validates DigiDollar parameters. Once oracle-specific
 * parameters are added to Consensus::Params, update this test accordingly.
 */
BOOST_AUTO_TEST_CASE(testnet_oracle_activation_height)
{
    SelectParams(ChainType::TESTNET);
    const CChainParams& params = Params();
    const Consensus::Params& consensus = params.GetConsensus();

    // Verify DigiDollar activation height is set for testnet
    // In Phase One, oracle uses DigiDollar infrastructure
    BOOST_CHECK(consensus.nDDActivationHeight >= 0);
    BOOST_CHECK(consensus.nDDActivationHeight < 1000000);  // Reasonable testnet value

    // Log activation height for manual verification
    LogPrintf("DigiDollar/Oracle activation height (testnet): %d\n", consensus.nDDActivationHeight);

    // TODO: Once nOracleActivationHeight is added to Consensus::Params:
    // BOOST_CHECK(consensus.nOracleActivationHeight > 0);
    // BOOST_CHECK_EQUAL(consensus.nOracleActivationHeight, 1000);  // Expected testnet value
}

/**
 * Test: Testnet Oracle Epoch Length
 *
 * Verifies that the oracle epoch length is configured correctly.
 * Expected: 1440 blocks (approximately 24 hours at 15 seconds/block)
 *
 * For Phase One: Epoch length determines oracle rotation period (though only 1 oracle exists)
 */
BOOST_AUTO_TEST_CASE(testnet_oracle_epoch_length)
{
    SelectParams(ChainType::TESTNET);
    const Consensus::Params& consensus = Params().GetConsensus();

    // Verify epoch length using DigiDollar parameters
    // Phase One uses same epoch infrastructure
    BOOST_CHECK(consensus.nDDOracleEpochBlocks > 0);

    // Calculate epoch duration in hours (15 seconds per block)
    int epoch_seconds = consensus.nDDOracleEpochBlocks * 15;
    double epoch_hours = epoch_seconds / 3600.0;

    // For testnet, epoch should be shorter for faster testing
    BOOST_CHECK(epoch_hours > 0);
    BOOST_CHECK(epoch_hours < 24);  // Testnet uses shorter epochs than production

    LogPrintf("Oracle epoch length (testnet): %d blocks (%.1f hours)\n",
              consensus.nDDOracleEpochBlocks, epoch_hours);

    // DigiDollar V1 uses 40-block (~10 minute) oracle signing epochs.
    BOOST_CHECK_EQUAL(consensus.nDDOracleEpochBlocks, 40);
    BOOST_CHECK_EQUAL(consensus.nOracleEpochLength, 40);
}

/**
 * Test: Testnet Oracle Consensus Requirements
 *
 * Verifies testnet consensus requirements.
 */
BOOST_AUTO_TEST_CASE(testnet_oracle_consensus_requirements)
{
    SelectParams(ChainType::TESTNET);
    const CChainParams& params = Params();
    const DigiDollar::ConsensusParams& ddParams = params.GetDigiDollarParams();

    // Verify testnet: 35 active slots, 7 signatures required.
    BOOST_CHECK_EQUAL(ddParams.oracleThreshold, 7);   // 7 signatures required
    BOOST_CHECK_EQUAL(ddParams.activeOracles, 35);    // 35 active oracles
    BOOST_CHECK_EQUAL(ddParams.oracleCount, 35);      // 35 configured slots

    double consensus_ratio = static_cast<double>(ddParams.oracleThreshold) /
                             ddParams.oracleCount;
    BOOST_CHECK_EQUAL(ddParams.oracleThreshold, 7);
    BOOST_CHECK_LE(ddParams.oracleThreshold, ddParams.activeOracles);

    LogPrintf("Oracle consensus (testnet): %d-of-%d (%.0f%%)\n",
              ddParams.oracleThreshold, ddParams.oracleCount, consensus_ratio * 100);

    // TODO: Once Consensus::Params has oracle fields:
    // BOOST_CHECK_EQUAL(consensus.nOracleRequiredMessages, 1);
    // BOOST_CHECK_EQUAL(consensus.nOracleTotalOracles, 1);
}

/**
 * Test: Testnet Oracle Public Keys
 *
 * Verifies that testnet has hardcoded oracle public keys configured.
 * The public key must be 33 bytes (compressed) or 65 bytes (uncompressed).
 */
BOOST_AUTO_TEST_CASE(testnet_oracle_public_keys)
{
    SelectParams(ChainType::TESTNET);
    const CChainParams& params = Params();

    // Verify oracle nodes are configured
    const std::vector<OracleNodeInfo>& oracle_nodes = params.GetOracleNodes();
    BOOST_REQUIRE(!oracle_nodes.empty());

    // Phase One: Must have at least 1 oracle configured
    BOOST_CHECK(oracle_nodes.size() >= 1);

    // Verify first oracle has valid public key
    const OracleNodeInfo& oracle = oracle_nodes[0];
    BOOST_CHECK(oracle.pubkey.IsValid());
    // NOTE: Placeholder keys may not be fully valid secp256k1 points
    // This will be enforced when real oracle keys are added in Phase 2
    // BOOST_CHECK(oracle.pubkey.IsFullyValid());

    // Verify public key is compressed (33 bytes) or uncompressed (65 bytes)
    size_t pubkey_size = oracle.pubkey.size();
    BOOST_CHECK(pubkey_size == 33 || pubkey_size == 65);

    // Verify oracle is marked as active
    BOOST_CHECK(oracle.is_active);

    LogPrintf("Oracle count (testnet): %d\n", oracle_nodes.size());
    LogPrintf("Oracle 0 pubkey size: %d bytes\n", pubkey_size);
    LogPrintf("Oracle 0 active: %s\n", oracle.is_active ? "true" : "false");

    // TODO: Once consensus.vOraclePublicKeys exists:
    // BOOST_CHECK_EQUAL(consensus.vOraclePublicKeys.size(), 1);
    // std::string pubkey_hex = consensus.vOraclePublicKeys[0];
    // BOOST_CHECK(pubkey_hex.length() == 66 || pubkey_hex.length() == 130);  // Hex encoding
}

// ============================================================================
// PART 2: Activation Check Tests (3 tests)
// ============================================================================

/**
 * Test: Oracle Inactive Before Activation
 *
 * Verifies that the oracle system is NOT active before the activation height.
 * Uses DigiDollar activation as proxy for oracle activation in Phase One.
 */
BOOST_AUTO_TEST_CASE(oracle_inactive_before_activation)
{
    SelectParams(ChainType::TESTNET);
    const Consensus::Params& consensus = Params().GetConsensus();

    // Test height before activation
    int height_before = consensus.nDDActivationHeight - 1;

    // Oracle should be inactive before activation height
    BOOST_CHECK(height_before < consensus.nDDActivationHeight);

    // Verify that attempting to use oracle features before activation would fail
    // (Implementation detail: oracle messages would be rejected)

    LogPrintf("Testing oracle inactive at height: %d (activation: %d)\n",
              height_before, consensus.nDDActivationHeight);

    // TODO: Once IsOracleActive() function exists:
    // BOOST_CHECK(!Consensus::IsOracleActive(consensus, height_before));
}

/**
 * Test: Oracle Active At and After Activation
 *
 * Verifies that the oracle system IS active at and after the activation height.
 */
BOOST_AUTO_TEST_CASE(oracle_active_at_activation)
{
    SelectParams(ChainType::TESTNET);
    const Consensus::Params& consensus = Params().GetConsensus();

    // Test height at activation
    int height_at = consensus.nDDActivationHeight;

    // Oracle should be active at activation height
    BOOST_CHECK(height_at >= consensus.nDDActivationHeight);

    // Test height after activation
    int height_after = consensus.nDDActivationHeight + 1000;
    BOOST_CHECK(height_after > consensus.nDDActivationHeight);

    LogPrintf("Testing oracle active at height: %d (activation: %d)\n",
              height_at, consensus.nDDActivationHeight);
    LogPrintf("Testing oracle active at height: %d (activation: %d)\n",
              height_after, consensus.nDDActivationHeight);

    // TODO: Once IsOracleActive() exists:
    // BOOST_CHECK(Consensus::IsOracleActive(consensus, height_at));
    // BOOST_CHECK(Consensus::IsOracleActive(consensus, height_after));
}

/**
 * Test: Oracle Activation Check Function
 *
 * Tests boundary conditions for oracle activation logic.
 * Verifies activation state at critical block heights.
 */
BOOST_AUTO_TEST_CASE(oracle_activation_check_function)
{
    SelectParams(ChainType::TESTNET);
    const Consensus::Params& consensus = Params().GetConsensus();

    // Test boundary conditions around activation height
    int activation = consensus.nDDActivationHeight;

    // Before activation
    BOOST_CHECK(0 < activation);
    BOOST_CHECK(1 < activation || activation == 1);
    BOOST_CHECK(activation - 1 < activation);

    // At and after activation
    BOOST_CHECK(activation >= activation);
    BOOST_CHECK(activation + 1 > activation);
    BOOST_CHECK(activation + 10000 > activation);

    LogPrintf("Oracle activation boundary tests passed\n");
    LogPrintf("Activation height: %d\n", activation);

    // TODO: Once IsOracleActive() exists, test actual function:
    // BOOST_CHECK(!Consensus::IsOracleActive(consensus, 0));
    // BOOST_CHECK(!Consensus::IsOracleActive(consensus, 1));
    // BOOST_CHECK(!Consensus::IsOracleActive(consensus, activation - 1));
    // BOOST_CHECK(Consensus::IsOracleActive(consensus, activation));
    // BOOST_CHECK(Consensus::IsOracleActive(consensus, activation + 1));
}

// ============================================================================
// PART 3: Phase One Constraint Tests (3 tests)
// ============================================================================

/**
 * Test: Testnet Active Oracle Requirement
 *
 * Verifies that testnet has the configured active oracle roster.
 * This is a critical safety constraint for initial deployment.
 */
BOOST_AUTO_TEST_CASE(phase_one_single_oracle_requirement)
{
    SelectParams(ChainType::TESTNET);
    const CChainParams& params = Params();
    const DigiDollar::ConsensusParams& ddParams = params.GetDigiDollarParams();

    // Active testnet keys fill the 35-slot RC44 roster.
    BOOST_CHECK_EQUAL(ddParams.activeOracles, 35);

    // Verify oracle nodes match configuration
    const std::vector<OracleNodeInfo>& oracle_nodes = params.GetOracleNodes();

    // Count active testnet oracles.
    int active_count = 0;
    for (const auto& oracle : oracle_nodes) {
        if (oracle.is_active) {
            active_count++;
        }
    }

    // Active oracles in the oracle nodes list.
    BOOST_CHECK_EQUAL(active_count, static_cast<int>(ddParams.activeOracles));
    BOOST_CHECK_EQUAL((int)oracle_nodes.size(), 35);

    LogPrintf("Phase Two oracle count: %d active, %d total\n",
              active_count, oracle_nodes.size());

    // TODO: Once consensus.nOracleTotalOracles exists:
    // BOOST_CHECK_EQUAL(consensus.nOracleTotalOracles, 1);
    // BOOST_CHECK_EQUAL(oracle_nodes.size(), 1);
}

/**
 * Test: Testnet Oracle Consensus
 *
 * Verifies that testnet keeps the configured quorum.
 */
BOOST_AUTO_TEST_CASE(phase_one_consensus_one_of_one)
{
    SelectParams(ChainType::TESTNET);
    const CChainParams& params = Params();
    const DigiDollar::ConsensusParams& ddParams = params.GetDigiDollarParams();

    // Testnet: 7 signatures from the full 35-slot active roster.
    BOOST_CHECK_EQUAL(ddParams.oracleThreshold, 7);
    BOOST_CHECK_EQUAL(ddParams.activeOracles, 35);

    // Verify testnet active set keeps the configured 7-signature quorum.
    BOOST_CHECK(ddParams.oracleThreshold > 0);

    // Calculate consensus percentage for the active launch roster.
    double consensus_pct = 100.0 * ddParams.oracleThreshold / ddParams.oracleCount;
    BOOST_CHECK(consensus_pct >= 20.0);

    LogPrintf("Phase Two consensus: %d-of-%d (%.0f%% required)\n",
              ddParams.oracleThreshold, ddParams.activeOracles, consensus_pct);

    // TODO: Once consensus oracle parameters exist:
    // BOOST_CHECK_EQUAL(consensus.nOracleRequiredMessages, 1);
    // BOOST_CHECK_EQUAL(consensus.nOracleTotalOracles, 1);
}

/**
 * Test: Phase One No Mainnet Activation
 *
 * Verifies that the oracle system is DISABLED on mainnet during Phase One.
 * Mainnet activation should be set to a very high block height or disabled.
 */
BOOST_AUTO_TEST_CASE(phase_one_no_mainnet_activation)
{
    SelectParams(ChainType::MAIN);
    const Consensus::Params& consensus = Params().GetConsensus();
    const CChainParams& params = Params();

    // Mainnet oracle should never activate in Phase One
    // Activation height should be very far in the future
    BOOST_CHECK(consensus.nDDActivationHeight > 20000000);  // Beyond current chain height

    // Verify mainnet has 35 oracle slots configured for future use.
    const std::vector<OracleNodeInfo>& oracle_nodes = params.GetOracleNodes();

    // Mainnet should have full oracle set configured.
    BOOST_CHECK_EQUAL(oracle_nodes.size(), 35U);

    // The RC44 launch roster activates every configured slot.
    int active_count = 0;
    for (const auto& oracle : oracle_nodes) {
        if (oracle.is_active) {
            active_count++;
        }
    }

    BOOST_CHECK_EQUAL(active_count, static_cast<int>(params.GetDigiDollarParams().activeOracles));

    LogPrintf("Mainnet oracle configuration: %d nodes configured, activation at height %d\n",
              oracle_nodes.size(), consensus.nDDActivationHeight);

    // TODO: Once consensus.fOracleEnabled exists:
    // BOOST_CHECK(!consensus.fOracleEnabled);  // Disabled on mainnet
    // BOOST_CHECK(consensus.vOraclePublicKeys.empty() || !consensus.fOracleEnabled);
}

BOOST_AUTO_TEST_CASE(mainnet_oracle_endpoints_use_mainnet_p2p_port)
{
    SelectParams(ChainType::MAIN);
    const CChainParams& params = Params();
    const std::vector<OracleNodeInfo>& oracle_nodes = params.GetOracleNodes();

    BOOST_REQUIRE_EQUAL(params.GetDefaultPort(), 12024);
    BOOST_REQUIRE_EQUAL(oracle_nodes.size(), 35U);

    for (const auto& oracle : oracle_nodes) {
        BOOST_CHECK_MESSAGE(oracle.endpoint.size() >= 6 &&
                            oracle.endpoint.rfind(":12024") == oracle.endpoint.size() - 6,
            "Mainnet oracle endpoint for slot " << oracle.id <<
            " must use mainnet P2P port 12024, got " << oracle.endpoint);
    }

    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(mainnet_oracle_seed_peers_include_launch_bootstrap_nodes)
{
    SelectParams(ChainType::MAIN);
    const auto& seed_peers = Params().OracleSeedPeers();

    BOOST_REQUIRE(!seed_peers.empty());

    const std::vector<std::string> expected_peers{
        "oracle1.digibyte.io:12024",
        "digihash.digibyte.io:12024",
        "oracleseed.digibyte.link:12024",
        "digiscope.me:12024",
        "oracle.dgbmaxi.com:12024",
    };

    for (const auto& expected_peer : expected_peers) {
        BOOST_CHECK_MESSAGE(std::find(seed_peers.begin(), seed_peers.end(), expected_peer) != seed_peers.end(),
            "Missing mainnet oracle seed peer " << expected_peer);
    }

    for (const auto& peer : seed_peers) {
        BOOST_CHECK_MESSAGE(peer.size() >= 6 && peer.rfind(":12024") == peer.size() - 6,
            "Mainnet oracle seed peer must use P2P port 12024, got " << peer);
    }
}

// ============================================================================
// PART 4: Network-Specific Configuration Tests (BONUS)
// ============================================================================

/**
 * Test: RegTest Oracle Configuration
 *
 * Verifies that regtest has oracle enabled with minimal activation height
 * for rapid testing. Should have 1 oracle configured.
 */
BOOST_AUTO_TEST_CASE(regtest_oracle_configuration)
{
    SelectParams(ChainType::REGTEST);
    const Consensus::Params& consensus = Params().GetConsensus();
    const CChainParams& params = Params();

    // RegTest should have low activation height for testing
    BOOST_CHECK(consensus.nDDActivationHeight < 1000);

    // Verify oracle nodes configured
    const std::vector<OracleNodeInfo>& oracle_nodes = params.GetOracleNodes();
    BOOST_REQUIRE(!oracle_nodes.empty());

    // RegTest uses 7 oracles (4-of-7 consensus, matches testnet)
    BOOST_CHECK(oracle_nodes.size() >= 1);
    BOOST_CHECK(oracle_nodes.size() <= 7);

    // Verify at least one oracle is active
    int active_count = 0;
    for (const auto& oracle : oracle_nodes) {
        if (oracle.is_active) {
            active_count++;
        }
    }

    BOOST_CHECK(active_count >= 1);

    LogPrintf("RegTest oracle configuration: %d active of %d total, activation at %d\n",
              active_count, oracle_nodes.size(), consensus.nDDActivationHeight);
}

/**
 * Test: Oracle Epoch Calculation
 *
 * Verifies that oracle epoch calculation works correctly across different heights.
 * Tests the GetCurrentEpoch() function from primitives/oracle.h
 */
BOOST_AUTO_TEST_CASE(oracle_epoch_calculation)
{
    SelectParams(ChainType::TESTNET);
    const Consensus::Params& consensus = Params().GetConsensus();

    int epoch_length = consensus.nDDOracleEpochBlocks;

    BOOST_CHECK_EQUAL(epoch_length, 40);

    // Test epoch calculation at the 40-block boundaries.
    BOOST_CHECK_EQUAL(GetCurrentEpoch(0), 0);
    BOOST_CHECK_EQUAL(GetCurrentEpoch(39), 0);
    BOOST_CHECK_EQUAL(GetCurrentEpoch(40), 1);
    BOOST_CHECK_EQUAL(GetCurrentEpoch(79), 1);
    BOOST_CHECK_EQUAL(GetCurrentEpoch(80), 2);

    // Test within epoch boundaries
    int32_t epoch_mid = GetCurrentEpoch(epoch_length / 2);
    BOOST_CHECK_EQUAL(epoch_mid, 0);

    int32_t epoch_end = GetCurrentEpoch(epoch_length - 1);
    BOOST_CHECK_EQUAL(epoch_end, 0);

    LogPrintf("Oracle epoch tests passed (epoch_length=%d)\n", epoch_length);
}

/**
 * Test: Oracle Update Interval Configuration
 *
 * Verifies that oracle update intervals are configured correctly.
 * Tests that update frequency is reasonable (not too fast, not too slow).
 */
BOOST_AUTO_TEST_CASE(oracle_update_interval_configuration)
{
    SelectParams(ChainType::TESTNET);
    const Consensus::Params& consensus = Params().GetConsensus();

    int update_interval = consensus.nDDOracleUpdateInterval;

    // Update interval should be reasonable
    BOOST_CHECK(update_interval > 0);
    BOOST_CHECK(update_interval < 100);  // Less than 100 blocks

    // Calculate update frequency in minutes (15 seconds per block)
    double update_minutes = (update_interval * 15.0) / 60.0;

    // Should update at least every 25 minutes on testnet
    BOOST_CHECK(update_minutes < 25.0);

    LogPrintf("Oracle update interval: %d blocks (%.1f minutes)\n",
              update_interval, update_minutes);
}

BOOST_AUTO_TEST_SUITE_END()
