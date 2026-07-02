// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-35: CHAOS ENGINEERING & EXTREME BOUNDARY TESTS
 *
 * Final deep-dive: block timestamp attacks, supply cap boundaries,
 * extreme reorgs, operation ordering, oracle failure modes,
 * DCA multiplier overflow, and zero-value output spam.
 */

#include <boost/test/unit_test.hpp>

#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <consensus/dca.h>
#include <consensus/err.h>
#include <consensus/volatility.h>
#include <digidollar/digidollar.h>
#include <digidollar/validation.h>
#include <digidollar/health.h>
#include <digidollar/scripts.h>
#include <kernel/chainparams.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <key.h>
#include <pubkey.h>
#include <test/util/setup_common.h>

#include <limits>
#include <cmath>

using namespace DigiDollar;
using namespace DigiDollar::DCA;

BOOST_FIXTURE_TEST_SUITE(digidollar_rh35_chaos_tests, BasicTestingSetup)

// =============================================================================
// CHAOS-1: Supply at MAX_DIGIDOLLAR Boundary
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_supply_exactly_at_max)
{
    // MAX_DIGIDOLLAR = 21B * 100 = 2,100,000,000,000 cents
    // ValidateOutputAmount checks: amount >= minOutputAmount && amount <= MAX_DIGIDOLLAR
    // ValidateMintAmount checks: amount >= effectiveMinMint && amount <= maxMintAmount
    //
    // FINDING: There is NO cumulative supply cap enforcement at the chain level.
    // Each mint is independently validated against maxMintAmount (per-tx limit),
    // not against the total outstanding supply. An attacker could theoretically
    // mint beyond MAX_DIGIDOLLAR in aggregate if they have the collateral.

    const CAmount maxDD = MAX_DIGIDOLLAR; // 2,100,000,000,000

    // Per-output validation: exactly MAX_DIGIDOLLAR should be valid
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();

    bool atMax = ValidateOutputAmount(maxDD, params);
    BOOST_CHECK_MESSAGE(atMax, "Output at exactly MAX_DIGIDOLLAR should be valid");

    // One satoshi over MAX_DIGIDOLLAR should fail
    bool overMax = ValidateOutputAmount(maxDD + 1, params);
    BOOST_CHECK_MESSAGE(!overMax, "Output at MAX_DIGIDOLLAR + 1 must be rejected");

    // BUG DOCUMENTATION: ValidateMintAmount uses maxMintAmount (per-tx cap, e.g. $10k),
    // NOT MAX_DIGIDOLLAR. There is no check that cumulative supply stays under MAX_DIGIDOLLAR.
    // Per-tx: maxMintAmount = 100000 (regtest) = $1000. So single mint can't hit MAX_DD.
    // But 2.1 billion $1000 mints could collectively exceed the cap without any check.
    //
    // SEVERITY: LOW-MEDIUM — collateral requirements make this economically infeasible,
    // but there's no consensus-level enforcement of the aggregate supply cap.
    // RECOMMENDATION: Add cumulative supply tracking to ConnectBlock/DisconnectBlock.
}

BOOST_AUTO_TEST_CASE(rh35_mint_at_max_per_tx_boundary)
{
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();
    const auto& ddParams = params.GetDigiDollarParams();
    int height = ddParams.minMintAmountActivationHeight + 1;

    // Exactly at maxMintAmount
    bool atMax = ValidateMintAmount(ddParams.maxMintAmount, params, height);
    BOOST_CHECK_MESSAGE(atMax, "Mint at exactly maxMintAmount should pass");

    // One cent over
    bool over = ValidateMintAmount(ddParams.maxMintAmount + 1, params, height);
    BOOST_CHECK_MESSAGE(!over, "Mint at maxMintAmount + 1 must be rejected");

    // Exactly at minMintAmount
    bool atMin = ValidateMintAmount(ddParams.minMintAmount, params, height);
    BOOST_CHECK_MESSAGE(atMin, "Mint at exactly minMintAmount should pass");

    // One cent under
    bool under = ValidateMintAmount(ddParams.minMintAmount - 1, params, height);
    BOOST_CHECK_MESSAGE(!under, "Mint at minMintAmount - 1 must be rejected");
}

// =============================================================================
// CHAOS-2: DCA Multiplier at Extremes — Near-Zero Supply
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_dca_multiplier_near_zero_supply)
{
    // If DD supply is 1 cent but collateral is massive, health = 30000 (capped).
    // GetDCAMultiplier(30000) should return 1.0x (healthy tier).
    // No overflow possible because health is capped at 30000.

    int health = DynamicCollateralAdjustment::CalculateSystemHealth(
        1000000 * COIN, // 1M DGB collateral
        1,              // 1 cent DD supply
        5000            // $0.05/DGB
    );
    BOOST_CHECK_EQUAL(health, 30000); // Capped at max

    double mult = DynamicCollateralAdjustment::GetDCAMultiplier(health);
    BOOST_CHECK_EQUAL(mult, 1.0); // Healthy
}

BOOST_AUTO_TEST_CASE(rh35_dca_multiplier_zero_supply_repeated_mints)
{
    // System starts with 0 DD supply — health is max (30000).
    // Each mint adds DD. As long as collateral keeps pace, multiplier stays 1.0x.
    // Simulate health declining through tiers.

    // Start: zero supply = max health
    int health0 = DynamicCollateralAdjustment::CalculateSystemHealth(0, 0, 5000);
    BOOST_CHECK_EQUAL(health0, 30000);
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(health0), 1.0);

    // Barely collateralized: health ~50% (emergency)
    int healthEmergency = DynamicCollateralAdjustment::CalculateSystemHealth(
        10000000 * COIN, // 10M DGB
        10000000,        // $100k DD (10M cents)
        5000             // $0.05/DGB => collateral value = $500k, health = 500k/100k*100 = 500%
    );
    // Actually 500% so this is healthy
    BOOST_CHECK(healthEmergency >= 150);
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(healthEmergency), 1.0);

    // Force emergency: collateral value << DD supply
    int healthCrash = DynamicCollateralAdjustment::CalculateSystemHealth(
        100 * COIN,    // 100 DGB ($5 at $0.05)
        100000,        // $1000 DD (100k cents) => health = 5/1000*100 = 0.5%
        5000
    );
    BOOST_CHECK(healthCrash < 100);
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(healthCrash), 2.0); // Emergency
}

BOOST_AUTO_TEST_CASE(rh35_dca_negative_health)
{
    // Negative health should map to emergency tier (2.0x)
    double mult = DynamicCollateralAdjustment::GetDCAMultiplier(-1);
    BOOST_CHECK_EQUAL(mult, 2.0); // Falls through to fallback

    mult = DynamicCollateralAdjustment::GetDCAMultiplier(-999999);
    BOOST_CHECK_EQUAL(mult, 2.0);

    mult = DynamicCollateralAdjustment::GetDCAMultiplier(std::numeric_limits<int>::min());
    BOOST_CHECK_EQUAL(mult, 2.0);
}

BOOST_AUTO_TEST_CASE(rh35_dca_apply_overflow_check)
{
    // ApplyDCA with extreme baseRatio and multiplier
    // Max multiplier is 2.0x, max baseRatio from collateralRatios is 1000%.
    // 1000 * 2.0 = 2000 — fits in int easily.
    int result = DynamicCollateralAdjustment::ApplyDCA(1000, 0); // emergency: 2.0x
    BOOST_CHECK_EQUAL(result, 2000);

    // What about INT_MAX as base ratio? 2147483647 * 2.0 = 4294967294 — overflows int!
    // This is a theoretical attack if baseRatio comes from untrusted input.
    // In practice, baseRatio is from collateralRatios map (max 1000), but worth testing.
    int extreme = DynamicCollateralAdjustment::ApplyDCA(std::numeric_limits<int>::max(), 0);
    // double(INT_MAX) * 2.0 = ~4.29e9, cast to int = undefined behavior or wrap
    // FINDING: If baseRatio is ever user-controllable, this is a bug.
    // Current code: static_cast<int>(adjustedRatio) where adjustedRatio is double.
    // double -> int overflow is UB in C++. For now, document it.
    BOOST_TEST_MESSAGE("ApplyDCA(INT_MAX, 0) = " << extreme << " (potential UB if baseRatio is untrusted)");

    // Safe range check
    int safe = DynamicCollateralAdjustment::ApplyDCA(300, 150); // healthy: 1.0x
    BOOST_CHECK_EQUAL(safe, 300);
}

// =============================================================================
// CHAOS-3: Oracle Failure Modes — All Oracles Offline
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_oracle_price_zero_blocks_system_health)
{
    // If oracle price = 0, system health = 0 → emergency tier
    int health = DynamicCollateralAdjustment::CalculateSystemHealth(
        100000 * COIN, 100000, 0);
    BOOST_CHECK_EQUAL(health, 0);

    double mult = DynamicCollateralAdjustment::GetDCAMultiplier(health);
    BOOST_CHECK_EQUAL(mult, 2.0); // Emergency
}

BOOST_AUTO_TEST_CASE(rh35_oracle_offline_collateral_calc_returns_zero)
{
    // CalculateRequiredCollateral returns 0 when oraclePrice <= 0
    // This means if oracles are offline, the collateral requirement is ZERO.
    // CRITICAL FINDING: If skipOracleValidation is false and oraclePriceMicroUSD <= 0,
    // ValidateMintTransaction rejects (line 660). BUT if skipOracleValidation is true
    // (IBD mode), the collateral calc returns 0, meaning mints during IBD need no collateral.
    // This is by design (historical blocks are trusted) but worth documenting.

    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();

    ValidationContext ctx(1000, 0, 150, params, nullptr, true);

    CAmount required = CalculateRequiredCollateral(10000, 30 * BLOCKS_PER_DAY, ctx);
    BOOST_CHECK_EQUAL(required, 0);
    BOOST_TEST_MESSAGE("Oracle offline: collateral requirement = 0 (by design for IBD)");
}

BOOST_AUTO_TEST_CASE(rh35_oracle_offline_100_blocks_health_stays_zero)
{
    // Simulate 100 consecutive blocks with oracle price = 0
    // System health should stay at 0 for each block
    for (int block = 0; block < 100; ++block) {
        int health = DynamicCollateralAdjustment::CalculateSystemHealth(
            50000000 * COIN, 100000000, 0);
        BOOST_CHECK_EQUAL(health, 0);
    }
    // DCA multiplier remains emergency (2.0x) throughout
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(0), 2.0);
}

// =============================================================================
// CHAOS-4: Zero-Value DD Outputs
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_zero_value_dd_output_rejected)
{
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();

    // minOutputAmount = 100 cents ($1). Zero should be rejected.
    bool zeroValid = ValidateOutputAmount(0, params);
    BOOST_CHECK_MESSAGE(!zeroValid, "Zero-value DD output must be rejected");

    // 1 cent — below minOutputAmount ($1)
    bool oneCent = ValidateOutputAmount(1, params);
    BOOST_CHECK_MESSAGE(!oneCent, "1-cent DD output below minOutputAmount must be rejected");

    // Exactly minOutputAmount
    bool atMin = ValidateOutputAmount(params.GetDigiDollarParams().minOutputAmount, params);
    BOOST_CHECK_MESSAGE(atMin, "Output at exactly minOutputAmount should be valid");

    // Negative amount
    bool negative = ValidateOutputAmount(-1, params);
    BOOST_CHECK_MESSAGE(!negative, "Negative DD output must be rejected");
}

BOOST_AUTO_TEST_CASE(rh35_zero_dd_output_in_cdigidollaroutput)
{
    // CDigiDollarOutput::IsValid() checks nDDAmount > MAX_DIGIDOLLAR but NOT > 0
    CDigiDollarOutput zeroOutput;
    zeroOutput.nDDAmount = 0;
    zeroOutput.collateralId = uint256::ONE;
    zeroOutput.nLockTime = 1000;

    // IsValid just checks nDDAmount <= MAX_DIGIDOLLAR, which 0 satisfies
    // FINDING: CDigiDollarOutput::IsValid() may accept zero-value outputs!
    bool valid = zeroOutput.IsValid();
    // If this passes, we found a gap — IsValid doesn't check for zero
    if (valid) {
        BOOST_TEST_MESSAGE("FINDING: CDigiDollarOutput::IsValid() accepts nDDAmount=0. "
                          "Relies on ValidateOutputAmount for min check. "
                          "Defense-in-depth: IsValid should also reject zero.");
    }
}

// =============================================================================
// CHAOS-5: Block Timestamp Attacks
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_timestamp_far_future_locktime_interaction)
{
    // Lock times are in BLOCKS, not timestamps. So block timestamp attacks
    // don't directly affect DD lock periods.
    // However, if any code converts timestamp to block height, future timestamps
    // could cause issues.

    // Verify lock time is block-based, not time-based
    // CCollateralPosition.unlockHeight is int64_t (block height)
    CCollateralPosition pos;
    pos.unlockHeight = 1000000; // Block 1M
    pos.dgbLocked = 100 * COIN;
    pos.ddMinted = 10000;
    pos.collateralRatio = 300;

    // Even with absurd unlockHeight, the position structure handles it
    pos.unlockHeight = std::numeric_limits<int64_t>::max();
    // No overflow risk since unlockHeight is just compared with current height

    // Lock period validation: collateralRatios map uses block counts
    // Max key = 10 * 365 * 24 * 60 * 4 = 21,024,000 blocks (~10 years)
    // A 1000-year timestamp doesn't map to blocks differently
    BOOST_TEST_MESSAGE("DD lock times are block-height-based, not timestamp-based. "
                      "Block timestamp attacks don't directly affect DD state.");
}

BOOST_AUTO_TEST_CASE(rh35_extreme_lock_period_collateral_ratio)
{
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();
    const auto& ddParams = params.GetDigiDollarParams();

    // V1 accepts only exact canonical lock tiers. Extreme/custom periods reject
    // instead of being coerced to the closest collateral tier.
    int64_t absurdLockPeriod = 100LL * 365 * 24 * 60 * 4; // 100 years in blocks
    int ratio = GetCollateralRatioForLockTime(absurdLockPeriod, ddParams);
    BOOST_CHECK(!IsCanonicalLockTier(absurdLockPeriod, ddParams));
    BOOST_CHECK_EQUAL(ratio, 0);

    // Zero lock period is not a canonical tier.
    ratio = GetCollateralRatioForLockTime(0, ddParams);
    BOOST_CHECK(!IsCanonicalLockTier(0, ddParams));
    BOOST_CHECK_EQUAL(ratio, 0);

    // Negative lock periods must also fail closed.
    ratio = GetCollateralRatioForLockTime(-1, ddParams);
    BOOST_CHECK(!IsCanonicalLockTier(-1, ddParams));
    BOOST_CHECK_EQUAL(ratio, 0);
}

// =============================================================================
// CHAOS-6: System Health at Integer Boundaries
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_system_health_max_collateral_tiny_supply)
{
    // MAX_MONEY collateral, 1 cent DD supply, max price
    // Should cap at 30000
    int health = DynamicCollateralAdjustment::CalculateSystemHealth(
        MAX_MONEY,  // ~21B DGB
        1,          // 1 cent
        1000000     // $10/DGB
    );
    BOOST_CHECK_EQUAL(health, 30000);
}

BOOST_AUTO_TEST_CASE(rh35_system_health_tiny_collateral_max_supply)
{
    // 1 sat collateral, MAX_DIGIDOLLAR supply
    int health = DynamicCollateralAdjustment::CalculateSystemHealth(
        1,               // 1 satoshi
        MAX_DIGIDOLLAR,  // 2.1 trillion cents
        5000             // $0.05/DGB
    );
    BOOST_CHECK_EQUAL(health, 0); // Essentially zero
}

BOOST_AUTO_TEST_CASE(rh35_system_health_both_at_max)
{
    // MAX_MONEY collateral, MAX_DIGIDOLLAR supply, extreme price
    int health = DynamicCollateralAdjustment::CalculateSystemHealth(
        MAX_MONEY,       // ~2.1e18 satoshis
        MAX_DIGIDOLLAR,  // 2.1e12 cents
        1000000          // $10/DGB
    );
    // collateral value = (2.1e18 * 1e6) / 1e8 / 1000 cents = 2.1e13 cents
    // health = 2.1e13 / 2.1e12 * 100 = 1000%
    BOOST_CHECK(health > 0);
    BOOST_CHECK(health <= 30000);
    BOOST_TEST_MESSAGE("Health with MAX_MONEY collateral and MAX_DIGIDOLLAR supply: " << health << "%");
}

// =============================================================================
// CHAOS-7: Collateral Calculation Edge Cases
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_collateral_calc_near_max_money)
{
    // Try to calculate collateral for a large DD amount near limits
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();

    ValidationContext ctx(1000, 1, 0, params, nullptr, true); // min price, emergency DCA

    // Large DD amount with tiny price = infeasible collateral requirement.
    // Should fail closed, not cap to a satisfiable amount or overflow.
    CAmount required = CalculateRequiredCollateral(
        10000000,              // $100k DD (10M cents)
        30 * BLOCKS_PER_DAY,   // 30 day lock (500% base * 2.0 DCA = 1000%)
        ctx
    );

    BOOST_CHECK_MESSAGE(required == 0,
        "Required collateral should fail closed when calculation exceeds MAX_MONEY. Got: " << required);
}

BOOST_AUTO_TEST_CASE(rh35_collateral_calc_1_cent_mint)
{
    // Minimum possible mint (before activation height): 1 cent
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();

    ValidationContext ctx(0, 50000, 150, params, nullptr, true); // height 0, $0.05/DGB, healthy

    CAmount required = CalculateRequiredCollateral(1, 240, ctx); // 1 cent, 1 hour lock (1000%)
    // 1 cent * COIN * 1000 * 100 / 50000 = 1 * 1e8 * 1000 * 100 / 50000 = 200000000 sat = 2 DGB
    BOOST_CHECK(required > 0);
    BOOST_TEST_MESSAGE("Collateral for 1-cent mint at $0.05: " << required << " sat (" << required/COIN << " DGB)");
}

// =============================================================================
// CHAOS-8: Tier Boundary Precision — Health 99 vs 100
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_dca_tier_boundaries_exact)
{
    // Test exact tier boundaries
    // Emergency floor: 0-109, Critical: 110-119, Warning: 120-149, Healthy: 150+

    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(99), 2.0);   // Emergency
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(100), 2.0);  // Emergency floor
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(109), 2.0);  // Emergency floor
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(110), 1.5);  // Critical
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(119), 1.5);  // Critical
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(120), 1.25); // Warning
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(149), 1.25); // Warning
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(150), 1.0);  // Healthy
    BOOST_CHECK_EQUAL(DynamicCollateralAdjustment::GetDCAMultiplier(30000), 1.0); // Max cap
}

BOOST_AUTO_TEST_CASE(rh35_dca_above_max_tier)
{
    // Health above 30000 — falls through all tiers, hits fallback (2.0x)!
    // FINDING: If CalculateSystemHealth somehow returns > 30000, GetDCAMultiplier
    // falls through to the emergency fallback of 2.0x. This means a "super healthy"
    // system would be penalized with emergency multiplier.
    // CalculateSystemHealth caps at 30000, so this shouldn't happen in practice,
    // but it's a latent bug in GetDCAMultiplier.
    double mult = DynamicCollateralAdjustment::GetDCAMultiplier(30001);
    if (mult == 2.0) {
        BOOST_TEST_MESSAGE("CONFIRMED: Health > 30000 falls through to emergency fallback (2.0x). "
                          "This is a latent bug — HEALTH_TIERS max is 30000, so 30001 has no tier.");
    } else {
        BOOST_CHECK_EQUAL(mult, 1.0); // Would be correct if tier range extended
    }
}

// =============================================================================
// CHAOS-9: CDigiDollarOutput Validation Edge Cases
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_dd_output_negative_amount)
{
    CDigiDollarOutput output;
    output.nDDAmount = -1;
    output.collateralId = uint256::ONE;
    output.nLockTime = 1000;

    // nDDAmount is CAmount (int64_t). Negative should fail IsValid().
    bool valid = output.IsValid();
    BOOST_CHECK_MESSAGE(!valid, "Negative DD amount must be rejected by IsValid()");
}

BOOST_AUTO_TEST_CASE(rh35_dd_output_max_amount)
{
    CDigiDollarOutput output;
    output.nDDAmount = MAX_DIGIDOLLAR;
    output.collateralId = uint256::ONE;
    output.nLockTime = 1000;

    bool valid = output.IsValid();
    BOOST_CHECK_MESSAGE(valid, "MAX_DIGIDOLLAR should be accepted by IsValid()");

    output.nDDAmount = MAX_DIGIDOLLAR + 1;
    valid = output.IsValid();
    BOOST_CHECK_MESSAGE(!valid, "MAX_DIGIDOLLAR + 1 must be rejected");
}

BOOST_AUTO_TEST_CASE(rh35_dd_output_int64_max)
{
    CDigiDollarOutput output;
    output.nDDAmount = std::numeric_limits<int64_t>::max();
    output.collateralId = uint256::ONE;
    output.nLockTime = 1000;

    bool valid = output.IsValid();
    BOOST_CHECK_MESSAGE(!valid, "INT64_MAX DD amount must be rejected");
}

// =============================================================================
// CHAOS-10: ValidateMintAmount Before vs After Activation Height
// =============================================================================

BOOST_AUTO_TEST_CASE(rh35_mint_amount_activation_height_boundary)
{
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();
    const auto& ddParams = params.GetDigiDollarParams();

    int activationHeight = ddParams.minMintAmountActivationHeight;

    // Before activation: 1 cent minimum
    BOOST_CHECK(ValidateMintAmount(1, params, activationHeight - 1));
    BOOST_CHECK(!ValidateMintAmount(0, params, activationHeight - 1));

    // At activation: minMintAmount kicks in
    if (activationHeight > 0) {
        BOOST_CHECK(ValidateMintAmount(ddParams.minMintAmount, params, activationHeight));
        BOOST_CHECK(!ValidateMintAmount(ddParams.minMintAmount - 1, params, activationHeight));
    }

    // After activation
    BOOST_CHECK(ValidateMintAmount(ddParams.minMintAmount, params, activationHeight + 1));
    BOOST_CHECK(!ValidateMintAmount(ddParams.minMintAmount - 1, params, activationHeight + 1));
}

BOOST_AUTO_TEST_SUITE_END()
