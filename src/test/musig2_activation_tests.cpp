// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * MuSig2 Phase 3 Activation & Oracle Configuration Tests
 *
 * Tests Phase 3 (MuSig2 aggregate signatures) activation heights,
 * oracle pubkey configuration, and ValidateOracleConfiguration():
 * - Per-network activation heights (mainnet, testnet, regtest)
 * - Oracle pubkey count, consensus threshold, uniqueness, validity, sort order
 * - Phase 2 backward compatibility before Phase 3
 * - Phase 3 activation logic via IsMuSig2OracleActive()
 * - ValidateOracleConfiguration() on all networks
 * - Bundle version gating at Phase 3 boundary
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/params.h>
#include <kernel/chainparams.h>
#include <primitives/oracle.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <algorithm>
#include <limits>
#include <set>
#include <string>

namespace {

/**
 * Bundle version acceptance per phase-aware gating rules:
 * - v0x03 only accepted at/after Phase 3 activation
 * - v0x01/v0x02 always accepted (backward compatibility)
 */
static bool IsBundleVersionAccepted(uint8_t bundle_version, int32_t block_height, const Consensus::Params& params)
{
    if (bundle_version == 3) {
        return params.IsMuSig2OracleActive(block_height);
    }
    return true;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(musig2_activation_tests, BasicTestingSetup)

// ============================================================================
// PART 1: Phase 3 Activation Height Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(test_phase3_activation_mainnet)
{
    SelectParams(ChainType::MAIN);
    const auto& params = Params().GetConsensus();
    // Mainnet activates MuSig2 alongside DigiDollar/oracle consensus.
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, params.nDDActivationHeight);
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, 23627520);
}

BOOST_AUTO_TEST_CASE(test_phase3_activation_testnet)
{
    SelectParams(ChainType::TESTNET);
    const auto& params = Params().GetConsensus();
    // Testnet activates MuSig2 alongside DigiDollar/oracle consensus.
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, params.nDDActivationHeight);
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, 600);
}

BOOST_AUTO_TEST_CASE(test_phase3_activation_regtest)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    const auto& dd_deployment = params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR];
    // Default regtest uses BIP9 ALWAYS_ACTIVE for DigiDollar, so MuSig2 must
    // be valid at the same effective boundary. Leaving MuSig2 at the raw
    // nDDActivationHeight=650 makes DD active before v0x03 quotes validate.
    BOOST_CHECK_EQUAL(dd_deployment.nStartTime, Consensus::BIP9Deployment::ALWAYS_ACTIVE);
    BOOST_CHECK_EQUAL(dd_deployment.min_activation_height, 0);
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, dd_deployment.min_activation_height);
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, 0);
}

BOOST_AUTO_TEST_CASE(test_regtest_digidollaractivationheight_moves_musig2_with_digidollar)
{
    CChainParams::RegTestOptions opts;
    opts.digidollar_activation_height = 432;
    opts.version_bits_parameters[Consensus::DEPLOYMENT_DIGIDOLLAR] = {
        0,
        Consensus::BIP9Deployment::NO_TIMEOUT,
        432,
    };

    const auto chainparams = CChainParams::RegTest(opts);
    const auto& params = chainparams->GetConsensus();
    const auto& dd_deployment = params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR];

    BOOST_CHECK_EQUAL(params.nDDActivationHeight, 432);
    BOOST_CHECK_EQUAL(params.nOracleActivationHeight, 432);
    BOOST_CHECK_EQUAL(dd_deployment.min_activation_height, 432);
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, 432);
    BOOST_CHECK(!params.IsMuSig2OracleActive(431));
    BOOST_CHECK(params.IsMuSig2OracleActive(432));
}

// ============================================================================
// PART 2: Oracle Configuration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(test_oracle_pubkey_count_and_total_slots)
{
    SelectParams(ChainType::TESTNET);
    const auto& params = Params().GetConsensus();
    // Testnet uses the full 35-slot RC44 oracle roster.
    BOOST_CHECK_EQUAL(params.nOraclePubkeyCount, 35);
    BOOST_CHECK_EQUAL(params.nOracleTotalOracles, 35);
    BOOST_CHECK_EQUAL(static_cast<int>(params.vOraclePublicKeys.size()), params.nOraclePubkeyCount);
}

BOOST_AUTO_TEST_CASE(test_oracle_consensus_required_is_7)
{
    SelectParams(ChainType::TESTNET);
    const auto& params = Params().GetConsensus();
    // 7 signatures are required from the active consensus keyset.
    BOOST_CHECK_EQUAL(params.nOracleConsensusRequired, 7);
    BOOST_CHECK_LE(params.nOracleConsensusRequired, params.nOraclePubkeyCount);
}

BOOST_AUTO_TEST_CASE(test_oracle_pubkey_uniqueness)
{
    SelectParams(ChainType::TESTNET);
    const auto& params = Params().GetConsensus();
    std::set<std::string> unique_keys(params.vOraclePublicKeys.begin(), params.vOraclePublicKeys.end());
    BOOST_CHECK_EQUAL(unique_keys.size(), params.vOraclePublicKeys.size());
}

BOOST_AUTO_TEST_CASE(test_oracle_pubkey_validity)
{
    SelectParams(ChainType::TESTNET);
    const auto& params = Params().GetConsensus();
    for (size_t i = 0; i < params.vOraclePublicKeys.size(); ++i) {
        const std::string& hex = params.vOraclePublicKeys[i];
        // X-only pubkeys are 32 bytes = 64 hex characters
        BOOST_CHECK_MESSAGE(hex.size() == 64,
            "Oracle pubkey " + std::to_string(i) + " wrong length: " + std::to_string(hex.size()));
        // Must parse as valid hex
        std::vector<unsigned char> data = ParseHex(hex);
        BOOST_CHECK_EQUAL(data.size(), 32u);
        // Construct compressed pubkey (prepend 0x02) and validate
        std::vector<unsigned char> compressed(33);
        compressed[0] = 0x02;
        std::copy(data.begin(), data.end(), compressed.begin() + 1);
        CPubKey pubkey(compressed);
        BOOST_CHECK_MESSAGE(pubkey.IsValid(),
            "Oracle pubkey " + std::to_string(i) + " invalid: " + hex);
    }
}

BOOST_AUTO_TEST_CASE(test_oracle_config_unique_by_pubkey)
{
    // RC30: vOraclePublicKeys is ordered by oracle slot (0..N-1) — matches
    // vOracleNodes and the MuSig2 participation bitmap. BIP-327 key aggregation
    // sorts internally, so consensus pubkey ordering is slot-based, not
    // lexicographic. We still require per-slot uniqueness.
    SelectParams(ChainType::TESTNET);
    const auto& params = Params().GetConsensus();
    BOOST_REQUIRE_GE(params.vOraclePublicKeys.size(), 2u);
    std::set<std::string> seen;
    for (size_t i = 0; i < params.vOraclePublicKeys.size(); ++i) {
        auto [it, inserted] = seen.insert(params.vOraclePublicKeys[i]);
        BOOST_CHECK_MESSAGE(inserted,
            "Duplicate oracle pubkey at slot " + std::to_string(i) + ": " +
            params.vOraclePublicKeys[i].substr(0, 8));
    }
}

BOOST_AUTO_TEST_CASE(testnet_active_roster_tail_slots_are_active)
{
    SelectParams(ChainType::TESTNET);
    const auto& params = Params().GetConsensus();
    const auto& nodes = Params().GetOracleNodes();

    constexpr const char* DIGIBYTE_MAXI_COMPRESSED =
        "03649d750bcad5b42b3dd0f11c8d98d62ed5afd515cd986663f81c35f086e58d47";
    constexpr const char* DIGIBYTE_MAXI_XONLY =
        "649d750bcad5b42b3dd0f11c8d98d62ed5afd515cd986663f81c35f086e58d47";
    constexpr const char* ANTHONY_COMPRESSED =
        "0345f8cb22dfde6aff8f18552c338256e0df551ca2df007f6449d6da1dbb7f4d89";
    constexpr const char* MBAH_JAMBON_COMPRESSED =
        "031758a6d7f1f87c95d1a4a38415608d41463a504ea28da7c6129e2a9d654add42";
    constexpr const char* CAMDEN_COMPRESSED =
        "03018c81746d6ddc326c993d9f2e7f2015554e97a261f3b5fe637ac5098f421a4c";
    constexpr const char* TWOFACE123_COMPRESSED =
        "03d8165aa05b045de2a9b979b23a63cca1fec865784d12ab6f3f1bca8a90f3dd86";
    constexpr const char* LIVINGTHELIFE_COMPRESSED =
        "039241688b464c3f03f957cd85a3d1d6a760963be3707a16805b8064a8740e07ef";
    constexpr const char* CHOZENONE43_COMPRESSED =
        "03b6302e3cc8ee6d474c3c0078c25b87ce708757e2a81e8f4f01975dc4b25e0f6d";
    constexpr const char* CKUNCHAINED_COMPRESSED =
        "03926ed40635d294a554ec046a96d3fa58587521385c7df58ff21ede12a31add0e";
    constexpr const char* JMAG_COMPRESSED =
        "034103ed4168d11dcaafa96494d5b3dd37247fa6deefa08d47f7004568792b1672";
    constexpr const char* HASHEDMAX_COMPRESSED =
        "038adf7df5fcd114178643f16aa0e3be8fa1e221ca421479e48c0bd04f2561d3a8";
    constexpr const char* DENNISPITALLANO_COMPRESSED =
        "02557029e2419af54984f3f2fb600004c0a6f8573dac5730cfaab2048c80ba6894";
    constexpr const char* DIGIHASH_COMPRESSED =
        "03532efd6277226f38903401ec8317ba7cc8f13eb48dc8cb1e102fdc23488d7cef";
    constexpr const char* MEDGBORACLE3452_COMPRESSED =
        "03d566a244719aa577d828da31ad9863f94686f710ac1f0638914eff5692ec7d58";
    constexpr const char* DIGIBYTEDAILY_COMPRESSED =
        "03603a0175197a1fe28859c71c69fd0081710c5569fceceaa96ce7de386d3ebf61";
    constexpr const char* PEER2PEER_COMPRESSED =
        "02e96759ba5c67df6fc5cfc86977389d08fa31b4b297586f13d43bdd1569b459ba";
    constexpr const char* THREEDOGSKANAB_COMPRESSED =
        "035878eb72be3710d8c3f9585e27374011a476378f5ea7f3ab2f2830ab052ec5f8";
    constexpr const char* LIBERATEDLARK_COMPRESSED =
        "035b3729a255bfbcff1bfd5b72fdb1a971b098545db7e874751179bc22c7f7f0b0";
    constexpr const char* MANU_DGB_ORACLE_COMPRESSED =
        "03d8fe0cd773604fa78fb1cef7d1e88a05c2473961178e40a4ac3c9aeeb4cbca87";
    constexpr const char* ANTHONY_XONLY =
        "45f8cb22dfde6aff8f18552c338256e0df551ca2df007f6449d6da1dbb7f4d89";
    constexpr const char* MBAH_JAMBON_XONLY =
        "1758a6d7f1f87c95d1a4a38415608d41463a504ea28da7c6129e2a9d654add42";
    constexpr const char* CAMDEN_XONLY =
        "018c81746d6ddc326c993d9f2e7f2015554e97a261f3b5fe637ac5098f421a4c";
    constexpr const char* TWOFACE123_XONLY =
        "d8165aa05b045de2a9b979b23a63cca1fec865784d12ab6f3f1bca8a90f3dd86";
    constexpr const char* LIVINGTHELIFE_XONLY =
        "9241688b464c3f03f957cd85a3d1d6a760963be3707a16805b8064a8740e07ef";
    constexpr const char* CHOZENONE43_XONLY =
        "b6302e3cc8ee6d474c3c0078c25b87ce708757e2a81e8f4f01975dc4b25e0f6d";
    constexpr const char* CKUNCHAINED_XONLY =
        "926ed40635d294a554ec046a96d3fa58587521385c7df58ff21ede12a31add0e";
    constexpr const char* JMAG_XONLY =
        "4103ed4168d11dcaafa96494d5b3dd37247fa6deefa08d47f7004568792b1672";
    constexpr const char* HASHEDMAX_XONLY =
        "8adf7df5fcd114178643f16aa0e3be8fa1e221ca421479e48c0bd04f2561d3a8";
    constexpr const char* DENNISPITALLANO_XONLY =
        "557029e2419af54984f3f2fb600004c0a6f8573dac5730cfaab2048c80ba6894";
    constexpr const char* DIGIHASH_XONLY =
        "532efd6277226f38903401ec8317ba7cc8f13eb48dc8cb1e102fdc23488d7cef";
    constexpr const char* MEDGBORACLE3452_XONLY =
        "d566a244719aa577d828da31ad9863f94686f710ac1f0638914eff5692ec7d58";
    constexpr const char* DIGIBYTEDAILY_XONLY =
        "603a0175197a1fe28859c71c69fd0081710c5569fceceaa96ce7de386d3ebf61";
    constexpr const char* PEER2PEER_XONLY =
        "e96759ba5c67df6fc5cfc86977389d08fa31b4b297586f13d43bdd1569b459ba";
    constexpr const char* THREEDOGSKANAB_XONLY =
        "5878eb72be3710d8c3f9585e27374011a476378f5ea7f3ab2f2830ab052ec5f8";
    constexpr const char* LIBERATEDLARK_XONLY =
        "5b3729a255bfbcff1bfd5b72fdb1a971b098545db7e874751179bc22c7f7f0b0";
    constexpr const char* MANU_DGB_ORACLE_XONLY =
        "d8fe0cd773604fa78fb1cef7d1e88a05c2473961178e40a4ac3c9aeeb4cbca87";

    BOOST_REQUIRE_EQUAL(params.nOraclePubkeyCount, 35);
    BOOST_REQUIRE_EQUAL(params.nOracleConsensusRequired, 7);
    BOOST_REQUIRE_EQUAL(params.vOraclePublicKeys.size(), static_cast<size_t>(params.nOraclePubkeyCount));
    BOOST_REQUIRE_GE(nodes.size(), static_cast<size_t>(params.nOraclePubkeyCount));

    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[17], DIGIBYTE_MAXI_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[18], ANTHONY_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[19], MBAH_JAMBON_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[20], CAMDEN_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[21], TWOFACE123_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[22], LIVINGTHELIFE_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[23], CHOZENONE43_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[24], CKUNCHAINED_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[25], JMAG_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[26], HASHEDMAX_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[27], DENNISPITALLANO_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[28], DIGIHASH_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[29], MEDGBORACLE3452_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[30], DIGIBYTEDAILY_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[31], PEER2PEER_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[32], THREEDOGSKANAB_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[33], LIBERATEDLARK_XONLY);
    BOOST_CHECK_EQUAL(params.vOraclePublicKeys[34], MANU_DGB_ORACLE_XONLY);
    for (uint32_t slot = 17; slot <= 34; ++slot) {
        BOOST_CHECK_EQUAL(nodes[slot].id, slot);
        BOOST_CHECK(nodes[slot].is_active);
    }
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                          nodes[17].pubkey.data(), nodes[17].pubkey.size())),
                      DIGIBYTE_MAXI_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                          nodes[18].pubkey.data(), nodes[18].pubkey.size())),
                      ANTHONY_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                          nodes[19].pubkey.data(), nodes[19].pubkey.size())),
                      MBAH_JAMBON_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                          nodes[20].pubkey.data(), nodes[20].pubkey.size())),
                      CAMDEN_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                          nodes[21].pubkey.data(), nodes[21].pubkey.size())),
                      TWOFACE123_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                          nodes[22].pubkey.data(), nodes[22].pubkey.size())),
                      LIVINGTHELIFE_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[23].pubkey.data(), nodes[23].pubkey.size())),
                      CHOZENONE43_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[24].pubkey.data(), nodes[24].pubkey.size())),
                      CKUNCHAINED_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[25].pubkey.data(), nodes[25].pubkey.size())),
                      JMAG_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[26].pubkey.data(), nodes[26].pubkey.size())),
                      HASHEDMAX_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[27].pubkey.data(), nodes[27].pubkey.size())),
                      DENNISPITALLANO_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[28].pubkey.data(), nodes[28].pubkey.size())),
                      DIGIHASH_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[29].pubkey.data(), nodes[29].pubkey.size())),
                      MEDGBORACLE3452_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[30].pubkey.data(), nodes[30].pubkey.size())),
                      DIGIBYTEDAILY_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[31].pubkey.data(), nodes[31].pubkey.size())),
                      PEER2PEER_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[32].pubkey.data(), nodes[32].pubkey.size())),
                      THREEDOGSKANAB_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[33].pubkey.data(), nodes[33].pubkey.size())),
                      LIBERATEDLARK_COMPRESSED);
    BOOST_CHECK_EQUAL(HexStr(Span<const unsigned char>(
                              nodes[34].pubkey.data(), nodes[34].pubkey.size())),
                      MANU_DGB_ORACLE_COMPRESSED);
}

// ============================================================================
// PART 3: Phase Transition Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(test_regtest_musig2_tracks_effective_digidollar_activation)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    const auto& dd_deployment = params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR];

    BOOST_CHECK_EQUAL(params.nOracleActivationHeight, params.nDDActivationHeight);
    BOOST_CHECK_EQUAL(dd_deployment.nStartTime, Consensus::BIP9Deployment::ALWAYS_ACTIVE);
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, dd_deployment.min_activation_height);
    BOOST_CHECK(!Consensus::IsOracleActive(params, params.nDDActivationHeight - 1));
    BOOST_CHECK(!params.IsMuSig2OracleActive(params.nDigiDollarMuSig2Height - 1));
    BOOST_CHECK(params.IsMuSig2OracleActive(params.nDigiDollarMuSig2Height));
    BOOST_CHECK(Consensus::IsOracleActive(params, params.nDDActivationHeight));
    BOOST_CHECK(Consensus::IsMuSig2Active(params, params.nDDActivationHeight));
}

BOOST_AUTO_TEST_CASE(test_phase3_active_after_height)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    int phase3 = params.nDigiDollarMuSig2Height;
    BOOST_CHECK(Consensus::IsMuSig2Active(params, phase3));
    BOOST_CHECK(Consensus::IsMuSig2Active(params, phase3 + 1));
    BOOST_CHECK(Consensus::IsMuSig2Active(params, phase3 + 10000));
    BOOST_CHECK(!Consensus::IsMuSig2Active(params, phase3 - 1));
    BOOST_CHECK(!Consensus::IsMuSig2Active(params, phase3 - 1000));
}

// ============================================================================
// PART 4: ValidateOracleConfiguration Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(test_validate_oracle_config_testnet)
{
    SelectParams(ChainType::TESTNET);
    BOOST_CHECK(Consensus::ValidateOracleConfiguration(Params().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(test_validate_oracle_config_regtest)
{
    SelectParams(ChainType::REGTEST);
    BOOST_CHECK(Consensus::ValidateOracleConfiguration(Params().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(test_validate_oracle_config_mainnet)
{
    SelectParams(ChainType::MAIN);
    BOOST_CHECK(Consensus::ValidateOracleConfiguration(Params().GetConsensus()));
}

// ============================================================================
// PART 5: Bundle Version Gating
// ============================================================================

BOOST_AUTO_TEST_CASE(test_v02_bundle_before_phase3)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    BOOST_CHECK(IsBundleVersionAccepted(2, params.nDigiDollarMuSig2Height - 1, params));
}

BOOST_AUTO_TEST_CASE(test_v03_bundle_before_phase3)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    BOOST_CHECK(!IsBundleVersionAccepted(3, params.nDigiDollarMuSig2Height - 1, params));
}

BOOST_AUTO_TEST_CASE(test_v03_bundle_at_activation)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    BOOST_CHECK(IsBundleVersionAccepted(3, params.nDigiDollarMuSig2Height, params));
}

BOOST_AUTO_TEST_CASE(test_v02_bundle_after_phase3)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    BOOST_CHECK(IsBundleVersionAccepted(2, params.nDigiDollarMuSig2Height + 1, params));
}

BOOST_AUTO_TEST_CASE(test_v03_bundle_after_phase3)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    BOOST_CHECK(IsBundleVersionAccepted(3, params.nDigiDollarMuSig2Height + 1, params));
}

BOOST_AUTO_TEST_CASE(test_off_by_one_activation)
{
    SelectParams(ChainType::REGTEST);
    const auto& params = Params().GetConsensus();
    int32_t N = params.nDigiDollarMuSig2Height;
    BOOST_CHECK(!params.IsMuSig2OracleActive(N - 1));
    BOOST_CHECK(params.IsMuSig2OracleActive(N));
    BOOST_CHECK(!IsBundleVersionAccepted(3, N - 1, params));
    BOOST_CHECK(IsBundleVersionAccepted(3, N, params));
}

// ============================================================================
// PART 6: Cross-Network Consistency
// ============================================================================

BOOST_AUTO_TEST_CASE(test_musig2_matches_digidollar_activation_on_all_networks)
{
    SelectParams(ChainType::REGTEST);
    const auto& regtest_params = Params().GetConsensus();
    const auto& regtest_deployment = regtest_params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR];
    BOOST_CHECK_EQUAL(regtest_params.nDigiDollarMuSig2Height, regtest_deployment.min_activation_height);

    SelectParams(ChainType::TESTNET);
    const auto& testnet_params = Params().GetConsensus();
    BOOST_CHECK_EQUAL(testnet_params.nDigiDollarMuSig2Height, testnet_params.nDDActivationHeight);

    SelectParams(ChainType::MAIN);
    const auto& mainnet_params = Params().GetConsensus();
    BOOST_CHECK_EQUAL(mainnet_params.nDigiDollarMuSig2Height, mainnet_params.nDDActivationHeight);
}

BOOST_AUTO_TEST_SUITE_END()
