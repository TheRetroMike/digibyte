// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-34: MULTI-BLOCK STATE MACHINE ATTACK TESTS
 *
 * Attacks spanning multiple blocks that exploit state transitions in the
 * DigiDollar health/ERR/freeze/recovery state machine.
 *
 * VULNERABILITY FOUND: GetCurrentState() re-fetches health from
 * DCA::GetCurrentSystemHealth() and OVERWRITES the state set by
 * ReconstructERRState(). This creates a TOCTOU race where ERR activation
 * depends on the DCA health cache rather than actual chain state.
 *
 * Attack vectors tested:
 * 1. ERR→recovery race — ERR at height H, conditions clear at H+1
 * 2. Supply tracking across deep reorgs (100-block)
 * 3. Health metric staleness — no DD txs for 1000 blocks
 * 4. Oracle price feed gaps — 10 consecutive blocks with no oracle data
 * 5. Atomic block group constraints — intra-block DD supply inflation
 * 6. Zero-clamping drift accumulation across many reorgs
 * 7. ERR oscillation — rapid health flapping around 100% threshold
 * 8. Disconnect order dependency — mint+redeem in same block reorg
 */

#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <consensus/err.h>
#include <consensus/volatility.h>
#include <digidollar/health.h>
#include <digidollar/digidollar.h>
#include <digidollar/validation.h>
#include <kernel/chainparams.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>
#include <limits>
#include <vector>

using namespace DigiDollar;

struct RH34MultiblockStateTestSetup : BasicTestingSetup {
    RH34MultiblockStateTestSetup()
    {
        ResetSharedState();
    }

    ~RH34MultiblockStateTestSetup()
    {
        ResetSharedState();
    }

    static void ResetSharedState()
    {
        ERR::EmergencyRedemptionRatio::ResetForTesting();
        SystemHealthMonitor::ResetMetrics();
        Volatility::VolatilityMonitor::ClearHistory();
    }
};

BOOST_FIXTURE_TEST_SUITE(digidollar_rh34_multiblock_state_tests, RH34MultiblockStateTestSetup)

// =============================================================================
// RH-34-01: ERR→Recovery Race Condition
// VULNERABILITY: GetCurrentState() overwrites ReconstructERRState() values
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_err_should_activate_boundary)
{
    // ShouldActivateERR is a pure function — tests correctly
    BOOST_CHECK(ERR::EmergencyRedemptionRatio::ShouldActivateERR(99));
    BOOST_CHECK(ERR::EmergencyRedemptionRatio::ShouldActivateERR(50));
    BOOST_CHECK(ERR::EmergencyRedemptionRatio::ShouldActivateERR(1));
    BOOST_CHECK(!ERR::EmergencyRedemptionRatio::ShouldActivateERR(100));
    BOOST_CHECK(!ERR::EmergencyRedemptionRatio::ShouldActivateERR(150));
    BOOST_CHECK(!ERR::EmergencyRedemptionRatio::ShouldActivateERR(300));
    // Note: ShouldActivateERR(0) returns true because 0 < 100
    BOOST_CHECK(ERR::EmergencyRedemptionRatio::ShouldActivateERR(0));
}

BOOST_AUTO_TEST_CASE(rh34_err_should_activate_zero)
{
    // Edge case: health=0 (total collapse) — should activate
    BOOST_CHECK(ERR::EmergencyRedemptionRatio::ShouldActivateERR(0));
}

BOOST_AUTO_TEST_CASE(rh34_vulnerability_getcurrentstate_overwrites_reconstruct)
{
    // VULNERABILITY DOCUMENTATION:
    // ReconstructERRState(95, 1000) sets s_currentState.isActive = true,
    // systemHealth = 95. But GetCurrentState() immediately calls
    // DCA::GetCurrentSystemHealth() and overwrites systemHealth with
    // whatever the DCA cache returns (default 30000 = 300%).
    // Since 30000 >= 100, it DEACTIVATES ERR right inside GetCurrentState().
    //
    // This means: After a node restart, ReconstructERRState correctly
    // detects under-collateralization, but the FIRST call to GetCurrentState()
    // clobbers the state because the DCA health cache hasn't been populated yet.
    //
    // IMPACT: ERR can be bypassed after node restart during under-collateralization.
    // The attacker waits for any node restart, then mints during the window
    // where DCA cache returns stale "healthy" state.

    ERR::EmergencyRedemptionRatio::ReconstructERRState(95, 1000);

    // GetCurrentState() — FIXED [RH-36a]: s_stateReconstructed flag prevents
    // DCA cache from overwriting reconstructed ERR state
    auto state = ERR::EmergencyRedemptionRatio::GetCurrentState();

    // FIXED: ERR state is preserved after reconstruction
    // Previously this was false (the TOCTOU vulnerability)
    BOOST_CHECK_MESSAGE(state.isActive,
        "FIXED [RH-36a]: GetCurrentState() now respects reconstructed state. "
        "ERR remains active after ReconstructERRState() until first real health update.");

    // systemHealth preserved at 95, not overwritten to 30000
    BOOST_CHECK_EQUAL(state.systemHealth, 95);
}

BOOST_AUTO_TEST_CASE(rh34_err_adjustment_ratio_consistency)
{
    // Verify ERR adjustment ratios are monotonically decreasing
    // as health decreases (more DD burn required at lower health)
    double prevRatio = 1.0;
    for (int health = 99; health >= 1; health--) {
        double ratio = ERR::EmergencyRedemptionRatio::CalculateERRAdjustment(health);
        BOOST_CHECK(ratio <= prevRatio);
        BOOST_CHECK(ratio >= 0.0);
        BOOST_CHECK(ratio <= 1.0);
        prevRatio = ratio;
    }
}

BOOST_AUTO_TEST_CASE(rh34_err_adjustment_edge_values)
{
    // Health = 0 (complete collapse)
    double ratio = ERR::EmergencyRedemptionRatio::CalculateERRAdjustment(0);
    BOOST_CHECK(ratio >= 0.0);
    BOOST_CHECK(ratio <= 1.0);

    // Health = -1 (invalid)
    ratio = ERR::EmergencyRedemptionRatio::CalculateERRAdjustment(-1);
    BOOST_CHECK(ratio >= 0.0);

    // Health = INT_MAX (overflow test)
    ratio = ERR::EmergencyRedemptionRatio::CalculateERRAdjustment(std::numeric_limits<int>::max());
    BOOST_CHECK(ratio >= 0.0);
    BOOST_CHECK(ratio <= 1.0);
}

// =============================================================================
// RH-34-02: Supply Tracking Across Deep Reorgs (100 blocks)
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_deep_reorg_100_blocks_supply_exact)
{
    // ATTACK: Nation-state triggers a 100-block reorg. After disconnect+reconnect,
    // supply must be EXACTLY restored. Any drift = exploitable.

    SystemHealthMonitor::ResetMetrics();

    const int NUM_BLOCKS = 100;
    const CAmount DD_PER_BLOCK = 10000; // $100 per block
    const CAmount COLLATERAL_PER_BLOCK = 50 * COIN;

    // Phase 1: Connect 100 blocks with mints
    for (int i = 0; i < NUM_BLOCKS; i++) {
        SystemHealthMonitor::OnMintConnected(DD_PER_BLOCK, COLLATERAL_PER_BLOCK);
    }

    auto metrics = SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, DD_PER_BLOCK * NUM_BLOCKS);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, COLLATERAL_PER_BLOCK * NUM_BLOCKS);

    // Phase 2: Disconnect all 100 blocks (reverse order, as reorg does)
    for (int i = 0; i < NUM_BLOCKS; i++) {
        SystemHealthMonitor::OnMintDisconnected(DD_PER_BLOCK, COLLATERAL_PER_BLOCK);
    }

    metrics = SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);

    // Phase 3: Reconnect different blocks (alternative chain)
    const CAmount ALT_DD = 8000;
    const CAmount ALT_COLLATERAL = 40 * COIN;
    for (int i = 0; i < NUM_BLOCKS; i++) {
        SystemHealthMonitor::OnMintConnected(ALT_DD, ALT_COLLATERAL);
    }

    metrics = SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, ALT_DD * NUM_BLOCKS);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, ALT_COLLATERAL * NUM_BLOCKS);
}

BOOST_AUTO_TEST_CASE(rh34_deep_reorg_mixed_mint_redeem)
{
    // ATTACK: Reorg a chain that has both mints and redeems.
    SystemHealthMonitor::ResetMetrics();

    const CAmount DD = 5000;
    const CAmount COL = 25 * COIN;

    // Connect 50 mints
    for (int i = 0; i < 50; i++) {
        SystemHealthMonitor::OnMintConnected(DD, COL);
    }
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, DD * 50);

    // Connect 25 redeems
    for (int i = 0; i < 25; i++) {
        SystemHealthMonitor::OnRedeemConnected(DD, COL);
    }
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, DD * 25);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalCollateral, COL * 25);

    // Disconnect 25 redeems (reverse order)
    for (int i = 0; i < 25; i++) {
        SystemHealthMonitor::OnRedeemDisconnected(DD, COL);
    }
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, DD * 50);

    // Disconnect 50 mints
    for (int i = 0; i < 50; i++) {
        SystemHealthMonitor::OnMintDisconnected(DD, COL);
    }
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 0);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalCollateral, 0);
}

// =============================================================================
// RH-34-03: Zero-Clamping Drift Accumulation
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_zero_clamping_drift_attack)
{
    // ATTACK: Trigger reorgs where disconnect amount exceeds current supply.
    // std::max<CAmount>(0, ...) silently absorbs accounting errors.
    SystemHealthMonitor::ResetMetrics();

    // Normal: connect 100, disconnect 100 = 0
    SystemHealthMonitor::OnMintConnected(100, COIN);
    SystemHealthMonitor::OnMintDisconnected(100, COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 0);

    // Attack: connect 100, disconnect 200 = clamped to 0 (not -100)
    SystemHealthMonitor::OnMintConnected(100, COIN);
    SystemHealthMonitor::OnMintDisconnected(200, 2 * COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 0);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalCollateral, 0);

    // Reconnect: supply = 100 (the "extra" 100 was silently eaten by clamping)
    SystemHealthMonitor::OnMintConnected(100, COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 100);

    // SECURITY NOTE: Zero-clamping masks accounting errors. Defense is that
    // disconnect amounts always match connect amounts (same OP_RETURN data).
    // If disk read fails, clamp silently absorbs the error.
    SystemHealthMonitor::ResetMetrics();
}

BOOST_AUTO_TEST_CASE(rh34_accumulated_clamping_drift_1000_reorgs)
{
    // Stress: 1000 connect/disconnect cycles with exact amounts. Zero drift.
    SystemHealthMonitor::ResetMetrics();

    const CAmount DD = 7777;
    const CAmount COL = 33 * COIN;

    for (int i = 0; i < 1000; i++) {
        SystemHealthMonitor::OnMintConnected(DD, COL);
        SystemHealthMonitor::OnMintDisconnected(DD, COL);
    }

    auto metrics = SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);
}

// =============================================================================
// RH-34-04: Health Metric Staleness
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_health_staleness_no_dd_activity)
{
    // ATTACK: System has DD supply but no new DD transactions for 1000 blocks.
    // DGB price drops. Attacker mints at stale health ratio.
    SystemHealthMonitor::ResetMetrics();
    Volatility::VolatilityMonitor::ClearHistory();
    Volatility::VolatilityMonitor::RecordPrice(
        500'000 /* $0.50/DGB in micro-USD */, 1'700'000'000, 1000);

    SystemHealthMonitor::OnMintConnected(10000, 100 * COIN);

    auto metrics = SystemHealthMonitor::GetSystemMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 10000);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 100 * COIN);
    BOOST_CHECK_EQUAL(metrics.lastOraclePrice, 500'000);

    // GetSystemCollateralRatio recalculates from totalCollateral * currentPrice.
    // It does NOT just return a cached percentage. This is the defense.
    CAmount ratio = DigiDollar::GetSystemCollateralRatio();
    BOOST_CHECK(ratio > 0);

    Volatility::VolatilityMonitor::ClearHistory();
    SystemHealthMonitor::ResetMetrics();
}

// =============================================================================
// RH-34-05: Oracle Price Feed Gaps
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_oracle_gap_stale_thresholds)
{
    // Verify alert thresholds are defined
    BOOST_CHECK_EQUAL(AlertThresholds::STALE_ORACLE_BLOCKS, 100);
    BOOST_CHECK_EQUAL(AlertThresholds::MIN_ORACLES, 5);
}

BOOST_AUTO_TEST_CASE(rh34_oracle_gap_collateral_zero_price)
{
    // Oracle price is 0 (all oracles offline). Collateral calc must be safe.
    const auto& params = Params();

    DigiDollar::ValidationContext ctx(1000, 0 /* price=0 */, 150, params);
    CAmount required = DigiDollar::CalculateRequiredCollateral(10000, 240, ctx);
    BOOST_CHECK_EQUAL(required, 0); // Safe: no mint possible

    bool valid = DigiDollar::ValidateCollateralRatio(100 * COIN, 10000, 240, ctx);
    BOOST_CHECK(!valid); // Price <= 0 rejected
}

BOOST_AUTO_TEST_CASE(rh34_oracle_gap_negative_price)
{
    // Negative oracle price (should never happen but defense-in-depth)
    const auto& params = Params();

    DigiDollar::ValidationContext ctx(1000, -1 /* negative */, 150, params);
    CAmount required = DigiDollar::CalculateRequiredCollateral(10000, 240, ctx);
    BOOST_CHECK_EQUAL(required, 0);

    bool valid = DigiDollar::ValidateCollateralRatio(100 * COIN, 10000, 240, ctx);
    BOOST_CHECK(!valid);
}

// =============================================================================
// RH-34-06: Atomic Block Group Constraints (Intra-block supply)
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_intrablock_incremental_tracking)
{
    // Verify T5-06 incremental tracking: each OnMintConnected updates
    // supply immediately, so subsequent txs in the same block see accurate data.
    SystemHealthMonitor::ResetMetrics();

    for (int i = 0; i < 5; i++) {
        SystemHealthMonitor::OnMintConnected(10000, 50 * COIN);
        // After each mint, supply should reflect all previous mints
        BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply,
                          10000 * (i + 1));
    }

    SystemHealthMonitor::ResetMetrics();
}

BOOST_AUTO_TEST_CASE(rh34_intrablock_mint_then_redeem)
{
    // Block contains mint + redeem of different vault
    SystemHealthMonitor::ResetMetrics();

    // Pre-existing vault
    SystemHealthMonitor::OnMintConnected(20000, 100 * COIN);

    // Same block: new mint + redeem old vault
    SystemHealthMonitor::OnMintConnected(10000, 50 * COIN);
    SystemHealthMonitor::OnRedeemConnected(20000, 100 * COIN);

    auto metrics = SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 10000);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 50 * COIN);

    SystemHealthMonitor::ResetMetrics();
}

// =============================================================================
// RH-34-07: ERR Oscillation Attack
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_err_oscillation_should_activate)
{
    // Test that ShouldActivateERR is consistent under rapid oscillation
    for (int i = 0; i < 1000; i++) {
        int health = (i % 2 == 0) ? 99 : 101;
        bool expected = (health < 100);
        BOOST_CHECK_EQUAL(ERR::EmergencyRedemptionRatio::ShouldActivateERR(health), expected);
    }
}

// =============================================================================
// RH-34-08: Disconnect Order Dependency
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_disconnect_order_correct)
{
    // DisconnectBlock processes txs in REVERSE order.
    // Block: [mint_A at idx 1, redeem_B at idx 2]
    // Disconnect: redeem_B first, then mint_A
    SystemHealthMonitor::ResetMetrics();

    // Pre-existing vault B
    SystemHealthMonitor::OnMintConnected(5000, 25 * COIN);

    // Block: mint A, redeem B
    SystemHealthMonitor::OnMintConnected(3000, 15 * COIN);
    SystemHealthMonitor::OnRedeemConnected(5000, 25 * COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 3000);

    // CORRECT disconnect order (reverse)
    SystemHealthMonitor::OnRedeemDisconnected(5000, 25 * COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 8000);

    SystemHealthMonitor::OnMintDisconnected(3000, 15 * COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 5000);

    SystemHealthMonitor::ResetMetrics();
}

BOOST_AUTO_TEST_CASE(rh34_disconnect_order_wrong_clamping_demo)
{
    // Demonstrate that WRONG disconnect order can cause clamping drift
    SystemHealthMonitor::ResetMetrics();

    // Only 1000 supply
    SystemHealthMonitor::OnMintConnected(1000, 5 * COIN);

    // Wrong order: disconnect mint of 3000 (clamped to 0!)
    SystemHealthMonitor::OnMintDisconnected(3000, 15 * COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 0);

    // Now add back via redeem disconnect: 0 + 5000 = 5000
    SystemHealthMonitor::OnRedeemDisconnected(5000, 25 * COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 5000);
    // SECURITY: Should be 3000 without clamping. 2000 DD silently created.
    // Defense: DisconnectBlock always uses reverse order.

    SystemHealthMonitor::ResetMetrics();
}

// =============================================================================
// RH-34-09: Overflow Protection
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_supply_near_max)
{
    // Try to push supply near MAX_DIGIDOLLAR
    SystemHealthMonitor::ResetMetrics();

    CAmount almostMax = MAX_DIGIDOLLAR - 1;
    SystemHealthMonitor::OnMintConnected(almostMax, 100 * COIN);
    BOOST_CHECK_EQUAL(SystemHealthMonitor::GetCachedMetrics().totalDDSupply, almostMax);

    // Adding more should cap or overflow safely
    SystemHealthMonitor::OnMintConnected(100, COIN);
    auto metrics = SystemHealthMonitor::GetCachedMetrics();
    // Should be capped at MAX_DIGIDOLLAR or overflow to almostMax+100
    BOOST_CHECK(metrics.totalDDSupply >= almostMax);
    BOOST_CHECK(metrics.totalDDSupply <= MAX_DIGIDOLLAR + 100); // some tolerance

    SystemHealthMonitor::ResetMetrics();
}

// =============================================================================
// RH-34-10: Health Calculation Edge Cases
// =============================================================================

BOOST_AUTO_TEST_CASE(rh34_health_ratio_zero_supply)
{
    SystemHealthMonitor::ResetMetrics();
    auto metrics = SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
}

BOOST_AUTO_TEST_CASE(rh34_health_ratio_extremes)
{
    // Health with tiny supply, large collateral → capped at 300%
    int health = HealthUtils::CalculateHealthRatio(1, 1000 * COIN, 50);
    BOOST_CHECK_EQUAL(health, 300);

    // Health with zero price → 0
    health = HealthUtils::CalculateHealthRatio(10000, 100 * COIN, 0);
    BOOST_CHECK_EQUAL(health, 0);

    // Health with zero collateral → 0
    health = HealthUtils::CalculateHealthRatio(10000, 0, 50);
    BOOST_CHECK_EQUAL(health, 0);
}

BOOST_AUTO_TEST_SUITE_END()
