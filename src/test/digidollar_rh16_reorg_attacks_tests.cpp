// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-16: REORG & CHAIN SPLIT ATTACK TESTS
 *
 * Red-team tests targeting DigiDollar state consistency during blockchain
 * reorganizations. These tests verify that connect/disconnect cycles
 * maintain correct supply accounting, oracle price consistency, and
 * UTXO state integrity.
 *
 * Attack vectors tested:
 * 1. Supply drift from connect/disconnect asymmetry
 * 2. Oracle cached_price stale after disconnect (VULNERABILITY FOUND)
 * 3. Failed disk read during redeem disconnect silently corrupts supply
 * 4. Rapid connect/disconnect cycles (stress test)
 * 5. Clamping-to-zero masks accounting errors
 */

#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <digidollar/health.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <oracle/bundle_manager.h>
#include <kernel/chainparams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(digidollar_rh16_reorg_attacks_tests, BasicTestingSetup)

// =============================================================================
// RH-16-01: Supply Accounting Symmetry Under Reorgs
// Verify that OnMintConnected + OnMintDisconnected = net zero
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_supply_symmetry_mint_connect_disconnect)
{
    // ATTACK: After connect+disconnect of a mint, supply must return to zero.
    // A nation-state attacker who can trigger reorgs wants supply to drift.
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const CAmount ddAmount = 100000; // $1000 in cents
    const CAmount collateral = 500 * COIN; // 500 DGB

    // Baseline
    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);

    // Connect mint
    DigiDollar::SystemHealthMonitor::OnMintConnected(ddAmount, collateral);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, ddAmount);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, collateral);

    // Disconnect mint (reorg removes the block)
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(ddAmount, collateral);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);
}

BOOST_AUTO_TEST_CASE(rh16_supply_symmetry_redeem_connect_disconnect)
{
    // ATTACK: Connect mint, connect redeem, disconnect redeem, disconnect mint.
    // Final state must be zero.
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const CAmount ddAmount = 50000; // $500
    const CAmount collateral = 250 * COIN;

    // Connect mint
    DigiDollar::SystemHealthMonitor::OnMintConnected(ddAmount, collateral);
    // Connect redeem
    DigiDollar::SystemHealthMonitor::OnRedeemConnected(ddAmount, collateral);

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);

    // Now reorg: disconnect redeem first, then disconnect mint
    DigiDollar::SystemHealthMonitor::OnRedeemDisconnected(ddAmount, collateral);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, ddAmount);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, collateral);

    DigiDollar::SystemHealthMonitor::OnMintDisconnected(ddAmount, collateral);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);
}

// =============================================================================
// RH-16-02: Rapid Connect/Disconnect Stress Test
// Simulate chain reorgs happening in rapid succession
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_rapid_reorg_supply_consistency)
{
    // ATTACK: Rapid connect-disconnect-connect-disconnect cycles.
    // After N complete cycles, supply must be exactly correct.
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const CAmount ddAmount = 10000; // $100
    const CAmount collateral = 50 * COIN;
    const int CYCLES = 1000;

    for (int i = 0; i < CYCLES; i++) {
        DigiDollar::SystemHealthMonitor::OnMintConnected(ddAmount, collateral);
        DigiDollar::SystemHealthMonitor::OnMintDisconnected(ddAmount, collateral);
    }

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);
}

BOOST_AUTO_TEST_CASE(rh16_rapid_reorg_with_multiple_txs_per_block)
{
    // ATTACK: Block has 10 DD mints. Connect all, disconnect all, repeat.
    // Tests that per-block batch accounting stays consistent.
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const int TXS_PER_BLOCK = 10;
    const CAmount ddPerTx = 5000;
    const CAmount collPerTx = 25 * COIN;
    const int CYCLES = 100;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        // Connect block (all 10 mints)
        for (int t = 0; t < TXS_PER_BLOCK; t++) {
            DigiDollar::SystemHealthMonitor::OnMintConnected(ddPerTx, collPerTx);
        }
        // Disconnect block (all 10 mints, reverse order)
        for (int t = 0; t < TXS_PER_BLOCK; t++) {
            DigiDollar::SystemHealthMonitor::OnMintDisconnected(ddPerTx, collPerTx);
        }
    }

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);
}

// =============================================================================
// RH-16-03: Clamping-to-Zero Masks Accounting Errors
// VULNERABILITY: std::max<CAmount>(0, ...) can silently absorb errors
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_clamp_to_zero_hides_double_disconnect)
{
    // ATTACK: Disconnect a mint TWICE. The second disconnect should be an error,
    // but clamping to 0 silently absorbs it.
    // This means an attacker who can trigger duplicate disconnects
    // can "destroy" supply tracking integrity.
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const CAmount ddAmount = 50000;
    const CAmount collateral = 250 * COIN;

    // Connect one mint
    DigiDollar::SystemHealthMonitor::OnMintConnected(ddAmount, collateral);

    // Disconnect it
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(ddAmount, collateral);

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);

    // EXPLOIT: Disconnect AGAIN — this should be an error but is silently clamped to 0
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(ddAmount, collateral);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    // Supply is 0 (clamped), but now if we re-connect, supply will be WRONG
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);

    // Now connect a new mint — supply should be ddAmount but the "ghost disconnect"
    // has been absorbed. This test passes but documents the vulnerability:
    // If a bug causes double-disconnect, the error is invisible.
    DigiDollar::SystemHealthMonitor::OnMintConnected(ddAmount, collateral);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    // This LOOKS correct but hides a prior error
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, ddAmount);

    // The real test: connect ANOTHER mint, disconnect both.
    // If double-disconnect had been caught, we'd know. Instead it's silent.
    DigiDollar::SystemHealthMonitor::OnMintConnected(ddAmount, collateral);
    // Total should be 2 * ddAmount
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 2 * ddAmount);

    // FINDING: The system SHOULD assert or log an error when disconnect
    // would make supply negative. Currently it silently clamps.
    // Recommendation: Add a LogPrintf warning when clamping occurs.
}

// =============================================================================
// RH-16-04: Overflow Protection During Reorg Reconnect
// After disconnecting, reconnecting must not bypass overflow caps
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_overflow_after_reorg_reconnect)
{
    // ATTACK: Get supply near MAX_DIGIDOLLAR, connect a normal valid-size mint,
    // then reorg it out. MAX_DIGIDOLLAR is a per-output serialization bound,
    // not a global supply cap, so aggregate supply may cross it and must still
    // return to the exact pre-connect value when the mint is disconnected.
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const CAmount nearMax = MAX_DIGIDOLLAR - 100;
    const CAmount smallMint = 50;
    const CAmount validMint = 10000000; // Default maxMintAmount ($100k in cents)

    // Set supply near max
    DigiDollar::SystemHealthMonitor::OnMintConnected(nearMax, 1000 * COIN);
    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, nearMax);

    // Another mint that fits
    DigiDollar::SystemHealthMonitor::OnMintConnected(smallMint, COIN);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, nearMax + smallMint);

    const CAmount beforeReorg = metrics.totalDDSupply;

    // A valid-size mint crossing MAX_DIGIDOLLAR must still be accepted by the
    // accounting cache and reversible on disconnect.
    DigiDollar::SystemHealthMonitor::OnMintConnected(validMint, 1000 * COIN);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_GT(metrics.totalDDSupply, MAX_DIGIDOLLAR);

    DigiDollar::SystemHealthMonitor::OnMintDisconnected(validMint, 1000 * COIN);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();

    BOOST_CHECK_MESSAGE(metrics.totalDDSupply == beforeReorg,
        "Reorg supply drift after valid mint crossing cap: got " +
        std::to_string(metrics.totalDDSupply) + " expected " +
        std::to_string(beforeReorg));
}

// =============================================================================
// RH-16-05: Oracle Price Cache Stale After Disconnect
// VULNERABILITY: RemovePriceCache removes height entry but doesn't revert
// cached_price — GetLatestPrice() returns stale data
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_oracle_cache_stale_after_disconnect)
{
    // ATTACK: Block at height 1000 sets oracle price to $0.05.
    // Block at height 1001 sets oracle price to $0.10.
    // Reorg removes block 1001.
    // cached_price STILL returns $0.10 (the removed block's price).
    //
    // Impact: DD validation after reorg uses wrong price, allowing
    // mints with insufficient collateral.

    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // Simulate block 1000 with price 50000 micro-USD ($0.05)
    manager.UpdatePriceCache(1000, 50000);

    // Simulate block 1001 with price 100000 micro-USD ($0.10)
    manager.UpdatePriceCache(1001, 100000);

    // Verify current price is from block 1001
    uint64_t price = manager.GetOraclePriceForHeight(1001);
    BOOST_CHECK_EQUAL(price, 100000);

    // Simulate reorg: remove block 1001's price
    manager.RemovePriceCache(1001);

    // Height 1001 should be gone
    price = manager.GetOraclePriceForHeight(1001);
    BOOST_CHECK_EQUAL(price, 0);

    // Height 1000 should still be there
    price = manager.GetOraclePriceForHeight(1000);
    BOOST_CHECK_EQUAL(price, 50000);

    // FIX [RH-25b]: RemovePriceCache now reverts cached_price to highest remaining
    // Verify GetLatestPrice() returns the previous block's price (50000), not stale 100000
    uint64_t latest = manager.GetLatestPrice();
    BOOST_CHECK_EQUAL(latest, 50000);
    BOOST_TEST_MESSAGE("BUG FIXED [RH-25b]: RemovePriceCache now reverts cached_price to highest remaining height's price");
}

// =============================================================================
// RH-16-06: Mixed Mint+Redeem Reorg Ordering
// Verify correct behavior when reorg disconnects interleaved operations
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_interleaved_mint_redeem_reorg)
{
    // Scenario: Block N has [Mint A, Mint B, Redeem A]
    // Reorg disconnects this block.
    // DisconnectBlock processes in REVERSE: Redeem A, Mint B, Mint A
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const CAmount ddA = 10000;
    const CAmount collA = 50 * COIN;
    const CAmount ddB = 20000;
    const CAmount collB = 100 * COIN;

    // Forward: connect Mint A, Mint B, Redeem A
    DigiDollar::SystemHealthMonitor::OnMintConnected(ddA, collA);
    DigiDollar::SystemHealthMonitor::OnMintConnected(ddB, collB);
    DigiDollar::SystemHealthMonitor::OnRedeemConnected(ddA, collA);

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    // After: supply = ddB, collateral = collB
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, ddB);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, collB);

    // Reverse: disconnect Redeem A, Mint B, Mint A
    DigiDollar::SystemHealthMonitor::OnRedeemDisconnected(ddA, collA);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, ddA + ddB);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, collA + collB);

    DigiDollar::SystemHealthMonitor::OnMintDisconnected(ddB, collB);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, ddA);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, collA);

    DigiDollar::SystemHealthMonitor::OnMintDisconnected(ddA, collA);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, 0);
}

// =============================================================================
// RH-16-07: Redeem Disconnect With Failed Block Read
// VULNERABILITY: If ReadBlockFromDisk fails in DisconnectBlock for redeem,
// ddAmount stays 0 and OnRedeemDisconnected is never called → supply leak
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_redeem_disconnect_failed_block_read)
{
    // This test documents a vulnerability in DisconnectBlock (validation.cpp ~line 2404-2425).
    //
    // When disconnecting a REDEEM, the code needs to look up the original MINT tx
    // from disk to get the DD amount. If ReadBlockFromDisk fails:
    //   - ddAmount stays 0
    //   - The condition `if (ddAmount > 0 && vaultCollateral > 0)` is false
    //   - OnRedeemDisconnected is NEVER called
    //   - Supply tracking permanently underestimates DD supply
    //
    // Attack scenario:
    // 1. Attacker mints DD, redeems DD (supply goes to 0)
    // 2. Reorg occurs, removing the redeem block
    // 3. If block read fails, OnRedeemDisconnected not called
    // 4. Supply stays at 0 instead of restoring to minted amount
    // 5. System thinks less DD exists than actually does
    //
    // This is hard to exploit in practice (requires corrupted block storage)
    // but a defense-in-depth fix would be:
    // - Store DD amount in the undo data (CTxUndo) so disk read isn't needed
    // - Or fail DisconnectBlock entirely if block read fails for DD tx

    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const CAmount ddAmount = 100000;
    const CAmount collateral = 500 * COIN;

    // Simulate: mint connected, redeem connected
    DigiDollar::SystemHealthMonitor::OnMintConnected(ddAmount, collateral);
    DigiDollar::SystemHealthMonitor::OnRedeemConnected(ddAmount, collateral);

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 0);

    // Simulate: disconnect redeem BUT block read fails (ddAmount=0)
    // This simulates the code path where ReadBlockFromDisk returns false
    DigiDollar::SystemHealthMonitor::OnRedeemDisconnected(0, 0); // Failed read path

    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    // VULNERABILITY: Supply should be ddAmount but stays at 0
    BOOST_CHECK_MESSAGE(metrics.totalDDSupply == 0,
        "Confirmed: failed block read during redeem disconnect loses supply tracking. "
        "Supply is " + std::to_string(metrics.totalDDSupply) + " but should be " +
        std::to_string(ddAmount));

    // Fix recommendation: Store DD amount in CTxUndo so we don't need disk reads
    // during DisconnectBlock, or make DisconnectBlock return DISCONNECT_FAILED.
}

// =============================================================================
// RH-16-08: Supply Drift From Partial Disconnect
// What if DisconnectBlock processes coinbase but crashes before DD txs?
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_partial_disconnect_supply_drift)
{
    // DisconnectBlock processes transactions in REVERSE order (last tx first).
    // DD txs are typically not coinbase (enforced by T5-02).
    // If the process crashes after disconnecting some DD txs but before others,
    // the CCoinsViewCache is not flushed (it's atomic via Flush()).
    // So this is actually safe — the view flush is all-or-nothing.
    //
    // However, SystemHealthMonitor updates are NOT covered by the same atomicity.
    // OnMintDisconnected/OnRedeemDisconnected modify static state directly.
    // If crash occurs between two DD disconnects:
    // - CCoinsView correctly reverts (no flush = no persist)
    // - But SystemHealthMonitor may have partial updates in memory
    //
    // On restart, ScanUTXOSet rebuilds from scratch, so this is recovered.
    // FINDING: In-memory supply tracking during reorg is NOT atomic,
    // but it's recovered by ScanUTXOSet on restart. ACCEPTABLE RISK.

    DigiDollar::SystemHealthMonitor::ResetMetrics();

    // Simulate partial disconnect: 2 mints in block, only 1 disconnected
    DigiDollar::SystemHealthMonitor::OnMintConnected(10000, 50 * COIN);
    DigiDollar::SystemHealthMonitor::OnMintConnected(20000, 100 * COIN);

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 30000);

    // Only first disconnect runs (simulating crash after first DD tx disconnect)
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(20000, 100 * COIN);

    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    // Partial state: only 10000 remains
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, 10000);

    // After restart, ScanUTXOSet would rebuild correctly.
    // This test documents that in-memory state can be inconsistent during crash.
    BOOST_CHECK_MESSAGE(true,
        "FINDING: SystemHealthMonitor updates are not atomic with CCoinsView. "
        "Safe due to ScanUTXOSet recovery on restart, but in-memory metrics "
        "can be transiently wrong during partial disconnect.");
}

// =============================================================================
// RH-16-09: Collateral Integer Boundary During Reorg
// =============================================================================

BOOST_AUTO_TEST_CASE(rh16_collateral_overflow_during_reorg)
{
    // ATTACK: Disconnect a redeem that restores near-max collateral,
    // then disconnect another redeem. Does collateral overflow?
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const CAmount maxSafe = std::numeric_limits<CAmount>::max() - COIN;
    const CAmount smallAmount = 2 * COIN;

    // OnRedeemDisconnected ADDS to collateral (vault restored)
    DigiDollar::SystemHealthMonitor::OnRedeemDisconnected(100, maxSafe);
    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalCollateral, maxSafe);

    // Another redeem disconnect that would overflow
    DigiDollar::SystemHealthMonitor::OnRedeemDisconnected(100, smallAmount);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    // Should be capped, not wrapped
    BOOST_CHECK_GE(metrics.totalCollateral, maxSafe);
    BOOST_CHECK_MESSAGE(metrics.totalCollateral > 0,
        "Collateral must not overflow to negative");
}

BOOST_AUTO_TEST_SUITE_END()
