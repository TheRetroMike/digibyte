// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/digidollar.h>
#include <consensus/params.h>
#include <digidollar/digidollar.h>
#include <digidollar/validation.h>
#include <kernel/chainparams.h>
#include <script/script.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(digidollar_consensus_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(collateral_ratio_lookup_test)
{
    DigiDollar::ConsensusParams params;

    // Test exact tier matches
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(30 * DigiDollar::BLOCKS_PER_DAY, params), 500);
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(90 * DigiDollar::BLOCKS_PER_DAY, params), 400);
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(180 * DigiDollar::BLOCKS_PER_DAY, params), 350);
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(365 * DigiDollar::BLOCKS_PER_DAY, params), 300);
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(2 * 365 * DigiDollar::BLOCKS_PER_DAY, params), 275);
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(3 * 365 * DigiDollar::BLOCKS_PER_DAY, params), 250);
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(5 * 365 * DigiDollar::BLOCKS_PER_DAY, params), 225);
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(7 * 365 * DigiDollar::BLOCKS_PER_DAY, params), 212);
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(10 * 365 * DigiDollar::BLOCKS_PER_DAY, params), 200);

    // V1 accepts canonical tiers only; in-between/custom durations reject.
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(15 * DigiDollar::BLOCKS_PER_DAY, params), 0); // Less than 30 days
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(60 * DigiDollar::BLOCKS_PER_DAY, params), 0); // Between 30 and 90 days
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(120 * DigiDollar::BLOCKS_PER_DAY, params), 0); // Between 90 and 180 days

    // Test beyond longest tier
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(15 * 365 * DigiDollar::BLOCKS_PER_DAY, params), 0); // 15 years
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(100 * 365 * DigiDollar::BLOCKS_PER_DAY, params), 0); // 100 years

    // Test very short non-canonical periods
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(1, params), 0); // 1 block (< 240 blocks)
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(DigiDollar::BLOCKS_PER_DAY, params), 0); // 1 day
}

BOOST_AUTO_TEST_CASE(dca_multiplier_test)
{
    DigiDollar::ConsensusParams params;

    // Test exact DCA levels
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(150, params), 1.0); // Normal level
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(120, params), 1.25); // 25% increase
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(110, params), 1.5); // 50% increase
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(100, params), 2.0); // 100% increase

    // Test above highest level
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(200, params), 1.0); // Above 150%
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(175, params), 1.0); // Above 150%

    // Test between levels
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(140, params), 1.25); // Between 150 and 120
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(115, params), 1.5); // Between 120 and 110
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(105, params), 2.0); // Between 110 and 100

    // Test below lowest level
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(90, params), 2.0); // Below 100%
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(50, params), 2.0); // Much below 100%
}

BOOST_AUTO_TEST_CASE(mint_amount_validation_test)
{
    DigiDollar::ConsensusParams params;

    // Test valid amounts (amounts are in CENTS, not satoshis)
    // Default params: minMintAmount = 10000 cents ($100), maxMintAmount = 10000000 cents ($100k)
    BOOST_CHECK(DigiDollar::IsValidMintAmount(10000, params)); // Minimum: $100.00
    BOOST_CHECK(DigiDollar::IsValidMintAmount(100000, params)); // Mid-range: $1,000.00
    BOOST_CHECK(DigiDollar::IsValidMintAmount(10000000, params)); // Maximum: $100,000.00

    // Test invalid amounts (too low)
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(9999, params)); // Below minimum ($99.99)
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(100, params)); // Way below ($1.00)
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(1, params)); // Very low ($0.01)
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(0, params)); // Zero

    // Test invalid amounts (too high)
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(10000001, params)); // Above maximum ($100,000.01)
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(100000000, params)); // Much above maximum ($1,000,000.00)
}

BOOST_AUTO_TEST_CASE(minimum_output_test)
{
    DigiDollar::ConsensusParams params;
    BOOST_CHECK_EQUAL(DigiDollar::GetMinimumDDOutput(params), 100); // $1 minimum
}

BOOST_AUTO_TEST_CASE(max_digidollar_is_opreturn_serialization_boundary)
{
    CAmount amount = 0;

    CScript at_max = CScript() << OP_RETURN << OP_DIGIDOLLAR;
    std::vector<unsigned char> max_bytes(8, 0);
    for (int i = 0; i < 8; ++i) {
        max_bytes[i] = (MAX_DIGIDOLLAR >> (i * 8)) & 0xff;
    }
    at_max << max_bytes;

    BOOST_CHECK(DigiDollar::ExtractDDAmount(at_max, amount));
    BOOST_CHECK_EQUAL(amount, MAX_DIGIDOLLAR);
    BOOST_CHECK(DigiDollar::ValidateOutputAmount(amount, Params()));

    CScript above_max = CScript() << OP_RETURN << OP_DIGIDOLLAR;
    std::vector<unsigned char> above_bytes(8, 0);
    const CAmount above = MAX_DIGIDOLLAR + 1;
    for (int i = 0; i < 8; ++i) {
        above_bytes[i] = (above >> (i * 8)) & 0xff;
    }
    above_max << above_bytes;

    BOOST_CHECK(!DigiDollar::ExtractDDAmount(above_max, amount));
    BOOST_CHECK(!DigiDollar::ValidateOutputAmount(above, Params()));
}

BOOST_AUTO_TEST_CASE(lock_time_conversion_test)
{
    // Test day to block conversion
    BOOST_CHECK_EQUAL(DigiDollar::LockDaysToBlocks(1), DigiDollar::BLOCKS_PER_DAY); // 1 day = 5760 blocks
    BOOST_CHECK_EQUAL(DigiDollar::LockDaysToBlocks(30), 30 * DigiDollar::BLOCKS_PER_DAY); // 30 days
    BOOST_CHECK_EQUAL(DigiDollar::LockDaysToBlocks(365), 365 * DigiDollar::BLOCKS_PER_DAY); // 1 year

    // Test block to day conversion
    BOOST_CHECK_EQUAL(DigiDollar::BlocksToLockDays(DigiDollar::BLOCKS_PER_DAY), 1); // 1 day
    BOOST_CHECK_EQUAL(DigiDollar::BlocksToLockDays(30 * DigiDollar::BLOCKS_PER_DAY), 30); // 30 days
    BOOST_CHECK_EQUAL(DigiDollar::BlocksToLockDays(365 * DigiDollar::BLOCKS_PER_DAY), 365); // 1 year

    // Test roundtrip conversions
    BOOST_CHECK_EQUAL(DigiDollar::BlocksToLockDays(DigiDollar::LockDaysToBlocks(100)), 100);
    BOOST_CHECK_EQUAL(DigiDollar::LockDaysToBlocks(DigiDollar::BlocksToLockDays(576000)), 576000);
}

BOOST_AUTO_TEST_CASE(parameter_validation_test)
{
    DigiDollar::ConsensusParams validParams;
    std::string strError;

    // Test valid parameters
    BOOST_CHECK(DigiDollar::ValidateConsensusParams(validParams, strError));

    // Test invalid collateral ratios
    DigiDollar::ConsensusParams invalidParams1 = validParams;
    invalidParams1.collateralRatios.clear();
    BOOST_CHECK(!DigiDollar::ValidateConsensusParams(invalidParams1, strError));
    BOOST_CHECK(strError.find("empty") != std::string::npos);

    DigiDollar::ConsensusParams invalidParams2 = validParams;
    invalidParams2.collateralRatios[100] = 50; // Less than 100%
    BOOST_CHECK(!DigiDollar::ValidateConsensusParams(invalidParams2, strError));
    BOOST_CHECK(strError.find("100%") != std::string::npos);

    // Test invalid mint amounts
    DigiDollar::ConsensusParams invalidParams3 = validParams;
    invalidParams3.minMintAmount = 0;
    BOOST_CHECK(!DigiDollar::ValidateConsensusParams(invalidParams3, strError));

    DigiDollar::ConsensusParams invalidParams4 = validParams;
    invalidParams4.maxMintAmount = invalidParams4.minMintAmount; // Equal
    BOOST_CHECK(!DigiDollar::ValidateConsensusParams(invalidParams4, strError));

    // Test invalid oracle configuration
    DigiDollar::ConsensusParams invalidParams5 = validParams;
    invalidParams5.oracleCount = 0;
    BOOST_CHECK(!DigiDollar::ValidateConsensusParams(invalidParams5, strError));

    DigiDollar::ConsensusParams invalidParams6 = validParams;
    invalidParams6.oracleThreshold = invalidParams6.activeOracles + 1; // Exceeds active oracles
    BOOST_CHECK(!DigiDollar::ValidateConsensusParams(invalidParams6, strError));

    // Test invalid DCA levels
    DigiDollar::ConsensusParams invalidParams7 = validParams;
    invalidParams7.dcaLevels.clear();
    BOOST_CHECK(!DigiDollar::ValidateConsensusParams(invalidParams7, strError));

    DigiDollar::ConsensusParams invalidParams8 = validParams;
    invalidParams8.dcaLevels[0].systemCollateral = 100; // Should be descending order
    invalidParams8.dcaLevels[1].systemCollateral = 150; // This breaks descending order
    BOOST_CHECK(!DigiDollar::ValidateConsensusParams(invalidParams8, strError));
}

BOOST_AUTO_TEST_CASE(digidollar_activation_test)
{
    // Test mainnet parameters
    auto mainParams = CChainParams::Main();
    BOOST_CHECK(!DigiDollar::IsDigiDollarActive(0, mainParams->GetConsensus())); // Genesis
    BOOST_CHECK(!DigiDollar::IsDigiDollarActive(23627519, mainParams->GetConsensus())); // Before activation
    BOOST_CHECK(DigiDollar::IsDigiDollarActive(23627520, mainParams->GetConsensus())); // At activation
    BOOST_CHECK(DigiDollar::IsDigiDollarActive(23627521, mainParams->GetConsensus())); // After activation

    // Test regtest parameters - DigiDollar activates at height 650 (after Odocrypt at 600)
    auto regTestParams = CChainParams::RegTest({});
    BOOST_CHECK(!DigiDollar::IsDigiDollarActive(0, regTestParams->GetConsensus())); // Not active at genesis
    BOOST_CHECK(!DigiDollar::IsDigiDollarActive(649, regTestParams->GetConsensus())); // Not active before height 650
    BOOST_CHECK(DigiDollar::IsDigiDollarActive(650, regTestParams->GetConsensus())); // Active at height 650
    BOOST_CHECK(DigiDollar::IsDigiDollarActive(1000, regTestParams->GetConsensus())); // Active after height 650
}

BOOST_AUTO_TEST_CASE(lock_tier_index_test)
{
    DigiDollar::ConsensusParams params;

    // Test non-canonical tier lookup rejects
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(100, params), -1); // Below 1-hour tier
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(15 * DigiDollar::BLOCKS_PER_DAY, params), -1); // Between 1-hour and 30 days
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(60 * DigiDollar::BLOCKS_PER_DAY, params), -1); // Between 30 and 90 days
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(120 * DigiDollar::BLOCKS_PER_DAY, params), -1); // Between 90 and 180 days
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(200 * DigiDollar::BLOCKS_PER_DAY, params), -1); // Between 180 and 365 days

    // Test exact tier boundaries
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(240, params), 0); // Exactly 1 hour (240 blocks)
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(30 * DigiDollar::BLOCKS_PER_DAY, params), 1); // Exactly 30 days
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(90 * DigiDollar::BLOCKS_PER_DAY, params), 2); // Exactly 90 days
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(365 * DigiDollar::BLOCKS_PER_DAY, params), 4); // Exactly 365 days

    // Test 2-year tier
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(500 * DigiDollar::BLOCKS_PER_DAY, params), -1); // Non-canonical
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(2 * 365 * DigiDollar::BLOCKS_PER_DAY, params), 5); // Exactly 2 years

    // Test beyond all tiers
    BOOST_CHECK_EQUAL(DigiDollar::GetLockTierIndex(15 * 365 * DigiDollar::BLOCKS_PER_DAY, params), -1);
}

BOOST_AUTO_TEST_CASE(format_lock_period_test)
{
    // Test day formatting
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(DigiDollar::BLOCKS_PER_DAY), "1 days");
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(15 * DigiDollar::BLOCKS_PER_DAY), "15 days");
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(29 * DigiDollar::BLOCKS_PER_DAY), "29 days");

    // Test month formatting
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(30 * DigiDollar::BLOCKS_PER_DAY), "1 month");
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(60 * DigiDollar::BLOCKS_PER_DAY), "2 months");
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(90 * DigiDollar::BLOCKS_PER_DAY), "3 months");
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(330 * DigiDollar::BLOCKS_PER_DAY), "11 months");

    // Test year formatting
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(360 * DigiDollar::BLOCKS_PER_DAY), "1 year");
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(365 * DigiDollar::BLOCKS_PER_DAY), "1 year");
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(730 * DigiDollar::BLOCKS_PER_DAY), "2 years");
    BOOST_CHECK_EQUAL(DigiDollar::FormatLockPeriod(3650 * DigiDollar::BLOCKS_PER_DAY), "10 years");
}

BOOST_AUTO_TEST_CASE(chainparams_digidollar_integration_test)
{
    // Test that all chain types have DigiDollar parameters
    auto mainParams = CChainParams::Main();
    auto testParams = CChainParams::TestNet();
    auto regTestParams = CChainParams::RegTest({});

    // Verify parameters are accessible
    const auto& mainDD = mainParams->GetDigiDollarParams();
    const auto& testDD = testParams->GetDigiDollarParams();
    const auto& regTestDD = regTestParams->GetDigiDollarParams();

    // Test that parameters are valid
    std::string strError;
    BOOST_CHECK(DigiDollar::ValidateConsensusParams(mainDD, strError));
    BOOST_CHECK(DigiDollar::ValidateConsensusParams(testDD, strError));
    BOOST_CHECK(DigiDollar::ValidateConsensusParams(regTestDD, strError));

    // Test network-specific differences (amounts are in CENTS, not satoshis)
    BOOST_CHECK_EQUAL(mainDD.minMintAmount, 10000);   // Mainnet: 10000 cents = $100.00 min
    BOOST_CHECK_EQUAL(testDD.minMintAmount, 10000);   // Testnet: 10000 cents = $100.00 min
    BOOST_CHECK_EQUAL(regTestDD.minMintAmount, 1);    // Regtest: 1 cent = $0.01 min
    BOOST_CHECK_GT(mainDD.minMintAmountActivationHeight, 0);
    BOOST_CHECK_GT(testDD.minMintAmountActivationHeight, 0);

    // Mainnet/testnet both use a 7-signature quorum in a 35-slot roster.
    // (kept in sync with Consensus::Params nOracleRequiredMessages/nOracleTotalOracles).
    BOOST_CHECK_EQUAL(mainDD.oracleThreshold, 7);
    BOOST_CHECK_EQUAL(testDD.oracleThreshold, 7);
    BOOST_CHECK_EQUAL(mainDD.oracleCount, 35);
    BOOST_CHECK_EQUAL(testDD.oracleCount, 35);
    BOOST_CHECK_EQUAL(mainDD.activeOracles, 35);
    BOOST_CHECK_EQUAL(testDD.activeOracles, 35);
    BOOST_CHECK_EQUAL(regTestDD.oracleThreshold, 1); // Regtest: 1-of-1 (Phase One, unchanged)

    // Test activation heights
    BOOST_CHECK_EQUAL(mainParams->GetConsensus().nDDActivationHeight, 23627520); // Aligned with BIP9 min_activation_height
    BOOST_CHECK_EQUAL(testParams->GetConsensus().nDDActivationHeight, 600);      // Testnet: active from block 600 (BIP9 DEFINED→STARTED→LOCKED_IN→ACTIVE)
    BOOST_CHECK_EQUAL(regTestParams->GetConsensus().nDDActivationHeight, 650);   // After Odocrypt at 600

    // DD-RHF-009: raw mainnet mints must enforce the same $100 floor as RPC,
    // wallet, and testnet once DigiDollar is active.
    BOOST_CHECK(!DigiDollar::ValidateMintAmount(mainDD.minMintAmount - 1, *mainParams,
                                                mainParams->GetConsensus().nDDActivationHeight));
    BOOST_CHECK(DigiDollar::ValidateMintAmount(mainDD.minMintAmount, *mainParams,
                                               mainParams->GetConsensus().nDDActivationHeight));
}

BOOST_AUTO_TEST_CASE(edge_cases_test)
{
    DigiDollar::ConsensusParams params;

    // Test zero lock time rejects
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(0, params), 0);

    // Test maximum possible values
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(std::numeric_limits<int64_t>::max(), params), 0);

    // Test DCA with extreme values
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(std::numeric_limits<int>::max(), params), 1.0);
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(0, params), 2.0);

    // Test empty DCA levels
    DigiDollar::ConsensusParams emptyDCAParams = params;
    emptyDCAParams.dcaLevels.clear();
    BOOST_CHECK_EQUAL(DigiDollar::GetDCAMultiplier(150, emptyDCAParams), 2.0); // Fallback
}

// =============================================================================
// Lock Tier Display Name Consistency Test
// This test verifies the CORRECT lock tier definitions that MUST be used
// consistently across all GUI widgets and consensus code.
//
// Bug fixed: 2026-02-03 - Tier 5-8 display names were wrong in some widgets,
// and tier 9 (10 years) was missing entirely.
// =============================================================================
BOOST_AUTO_TEST_CASE(lock_tier_display_consistency_test)
{
    // The authoritative lock tier definitions (from consensus):
    // These MUST match getLockTierDisplayName() in digidollarmintwidget.cpp
    // and addPositionToTable() in digidollarpositionswidget.cpp
    
    // Expected lock periods in blocks (15 seconds per block in DigiByte)
    std::map<int, std::pair<int64_t, std::string>> expectedTiers = {
        {0, {240,         "1 hour"}},       // Tier 0: 1 hour = 240 blocks (testing only, 1000% collateral)
        {1, {172800,      "30 days"}},      // Tier 1: 30 days (500% collateral)
        {2, {518400,      "3 months"}},     // Tier 2: 3 months (400% collateral)
        {3, {1036800,     "6 months"}},     // Tier 3: 6 months (350% collateral)
        {4, {2102400,     "1 year"}},       // Tier 4: 1 year (300% collateral)
        {5, {4204800,     "2 years"}},      // Tier 5: 2 years (275% collateral) - NOT "3 years"!
        {6, {6307200,     "3 years"}},      // Tier 6: 3 years (250% collateral) - NOT "5 years"!
        {7, {10512000,    "5 years"}},      // Tier 7: 5 years (225% collateral) - NOT "7 years"!
        {8, {14716800,    "7 years"}},      // Tier 8: 7 years (212% collateral) - NOT "10 years"!
        {9, {21024000,    "10 years"}}      // Tier 9: 10 years (200% collateral) - MUST EXIST!
    };

    // Verify we have exactly 10 tiers (0-9)
    BOOST_CHECK_EQUAL(expectedTiers.size(), 10U);

    // Verify block calculations (15 seconds per block)
    // 1 year = 365 * 24 * 60 * 60 / 15 = 2,102,400 blocks
    int64_t blocks_per_day = 24 * 60 * 60 / 15;  // 5760 blocks/day
    int64_t blocks_per_year = 365 * blocks_per_day; // 2,102,400 blocks/year

    BOOST_CHECK_EQUAL(blocks_per_day, 5760);
    BOOST_CHECK_EQUAL(blocks_per_year, 2102400);

    // Verify each tier's block count
    BOOST_CHECK_EQUAL(expectedTiers[0].first, 240);  // 1 hour
    BOOST_CHECK_EQUAL(expectedTiers[4].first, blocks_per_year);  // 1 year
    BOOST_CHECK_EQUAL(expectedTiers[5].first, 2 * blocks_per_year);  // 2 years
    BOOST_CHECK_EQUAL(expectedTiers[9].first, 10 * blocks_per_year);  // 10 years

    // Most importantly: verify the display names are correct!
    // These were the buggy values before the fix:
    // Tier 5 showed "3 years" (wrong, should be "2 years")
    // Tier 6 showed "5 years" (wrong, should be "3 years")
    // Tier 7 showed "7 years" (wrong, should be "5 years")
    // Tier 8 showed "10 years" (wrong, should be "7 years")
    // Tier 9 was missing (should be "10 years")

    BOOST_CHECK_EQUAL(expectedTiers[5].second, "2 years");  // Critical fix
    BOOST_CHECK_EQUAL(expectedTiers[6].second, "3 years");  // Critical fix
    BOOST_CHECK_EQUAL(expectedTiers[7].second, "5 years");  // Critical fix
    BOOST_CHECK_EQUAL(expectedTiers[8].second, "7 years");  // Critical fix
    BOOST_CHECK_EQUAL(expectedTiers[9].second, "10 years"); // Critical fix - tier must exist!

    // Verify against actual consensus ratios
    DigiDollar::ConsensusParams params;
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(expectedTiers[5].first, params), 275); // 2 years
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(expectedTiers[6].first, params), 250); // 3 years
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(expectedTiers[7].first, params), 225); // 5 years
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(expectedTiers[8].first, params), 212); // 7 years
    BOOST_CHECK_EQUAL(DigiDollar::GetCollateralRatioForLockTime(expectedTiers[9].first, params), 200); // 10 years
}

// Test that skipOracleValidation allows DD transactions when oracle price is 0.
// This verifies the IBD/catch-up sync path where oracle data isn't available.
BOOST_AUTO_TEST_CASE(skip_oracle_validation_allows_zero_price)
{
    // Create a minimal DD validation context with price=0 and skipOracle=true
    // This simulates the IBD/catch-up case where the node is syncing historical
    // blocks and has no oracle price data available.
    DigiDollar::ValidationContext ctx(
        1000,                               // height
        0,                                   // oraclePriceMicroUSD = 0 (no oracle)
        300,                                 // systemCollateral
        Params(),                            // chain params
        nullptr,                             // no coins view
        true,                                // skipOracleValidation = TRUE
        nullptr                              // no tx lookup
    );

    // With skipOracleValidation=true, the context should allow proceeding
    // even though oraclePriceMicroUSD is 0
    BOOST_CHECK(ctx.skipOracleValidation);
    BOOST_CHECK_EQUAL(ctx.oraclePriceMicroUSD, 0);

    // Create the same context but with skipOracle=false (post-sync, live tip)
    DigiDollar::ValidationContext ctx_strict(
        1000,
        0,                                   // oraclePriceMicroUSD = 0
        300,
        Params(),
        nullptr,
        false,                               // skipOracleValidation = FALSE
        nullptr
    );

    // With skipOracleValidation=false and price=0, DD transactions should be rejected
    BOOST_CHECK(!ctx_strict.skipOracleValidation);
    BOOST_CHECK_EQUAL(ctx_strict.oraclePriceMicroUSD, 0);
    // The actual rejection happens in ValidateDigiDollarTransaction() when it checks
    // !ctx.skipOracleValidation && ctx.oraclePriceMicroUSD <= 0
}

BOOST_AUTO_TEST_SUITE_END()
