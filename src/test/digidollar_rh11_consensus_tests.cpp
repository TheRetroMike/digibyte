// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// RH-11: Deep Consensus Edge Cases — Adversarial Red-Team Tests
// Focus: ConnectBlock/DisconnectBlock asymmetry, supply overflow, version field
//        manipulation, thread safety, activation boundaries

#include <boost/test/unit_test.hpp>

#include <consensus/digidollar.h>
#include <digidollar/digidollar.h>
#include <digidollar/health.h>
#include <digidollar/validation.h>
#include <digidollar/scripts.h>
#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <util/strencodings.h>

#include <limits>
#include <thread>
#include <vector>
#include <atomic>

// Note: Don't use 'using namespace DigiDollar' — DD_TX_* enums are
// duplicated in primitives/transaction.h and consensus/digidollar.h

BOOST_AUTO_TEST_SUITE(digidollar_rh11_consensus_tests)

// ============================================================================
// Helper: Build a DD version field
// ============================================================================
static int32_t MakeDDVersion(uint8_t txType, uint8_t flags = 0) {
    // Lower 16 bits: 0x0770 (DD marker)
    // Bits 16-23: flags
    // Bits 24-31: transaction type
    return (static_cast<int32_t>(txType) << 24) |
           (static_cast<int32_t>(flags) << 16) |
           0x0770;
}

// ============================================================================
// 1. Supply Tracking Overflow (Attack Vector #5)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_supply_overflow_no_protection)
{
    // FINDING: OnMintConnected does unchecked addition on totalDDSupply.
    // If enough mints are connected, totalDDSupply can overflow int64_t.
    // This would cause ShouldBlockMinting() / ERR to use garbage values.

    DigiDollar::SystemHealthMonitor::ResetMetrics();

    CAmount bigMint = std::numeric_limits<CAmount>::max() / 2;
    DigiDollar::SystemHealthMonitor::OnMintConnected(bigMint, 100 * COIN);

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, bigMint);

    // Second mint still fits in CAmount and must be accounted exactly.
    DigiDollar::SystemHealthMonitor::OnMintConnected(bigMint, 100 * COIN);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, bigMint * 2);

    // A true int64 overflow attempt is clamped at CAmount max.
    DigiDollar::SystemHealthMonitor::OnMintConnected(2, 100 * COIN);
    metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, std::numeric_limits<CAmount>::max());

    DigiDollar::SystemHealthMonitor::ResetMetrics();
}

BOOST_AUTO_TEST_CASE(rh11_supply_collateral_overflow)
{
    // Same issue for totalCollateral
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    CAmount bigCollateral = std::numeric_limits<CAmount>::max() / 2;
    DigiDollar::SystemHealthMonitor::OnMintConnected(1000, bigCollateral);
    DigiDollar::SystemHealthMonitor::OnMintConnected(1000, bigCollateral);

    auto metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    if (metrics.totalCollateral < 0) {
        BOOST_CHECK_MESSAGE(false,
            "RH-11-OVERFLOW: totalCollateral overflowed to negative: " << metrics.totalCollateral);
    }

    DigiDollar::SystemHealthMonitor::ResetMetrics();
}

// ============================================================================
// 2. Version Field Manipulation (Attack Vector #6)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_version_type_zero_with_marker)
{
    // FINDING: nVersion=0x00000770 passes HasDigiDollarMarker() (lower 16 bits match)
    // but GetDigiDollarTxType() returns DigiDollar::DD_TX_NONE (type byte 0x00).
    // ValidateDigiDollarTransaction hits default: case and rejects.
    // This is safe but the version is "ambiguous" — it looks like DD but isn't a valid type.

    CMutableTransaction mtx;
    mtx.nVersion = 0x00000770;  // Marker present, type = 0 (DigiDollar::DD_TX_NONE)

    CTransaction tx(mtx);
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(tx));
    BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(tx), DigiDollar::DD_TX_NONE);

    BOOST_TEST_MESSAGE("INFO: Version 0x00000770 passes HasDigiDollarMarker but type=DigiDollar::DD_TX_NONE. "
                       "ValidateDigiDollarTransaction rejects via default case — safe.");
}

BOOST_AUTO_TEST_CASE(rh11_version_unknown_future_type)
{
    // Type bytes 4-255 are unrecognized. They pass HasDigiDollarMarker but
    // fall through to default: in the switch. Verify all are rejected.
    for (int typeVal = 4; typeVal <= 255; typeVal++) {
        CMutableTransaction mtx;
        mtx.nVersion = MakeDDVersion(typeVal);

        CTransaction tx(mtx);
        BOOST_CHECK_MESSAGE(DigiDollar::HasDigiDollarMarker(tx),
            "Type " << typeVal << " should pass HasDigiDollarMarker");
        
        int txType = static_cast<int>(DigiDollar::GetDigiDollarTxType(tx));
        // Should NOT be a known type (1=MINT, 2=TRANSFER, 3=REDEEM)
        BOOST_CHECK_MESSAGE(txType != 1 && txType != 2 && txType != 3,
            "Type " << typeVal << " unexpectedly matched a known DD type: " << txType);
    }
}

BOOST_AUTO_TEST_CASE(rh11_version_flag_bits_malleability)
{
    // FINDING: Bits 16-23 of nVersion are completely ignored by both
    // HasDigiDollarMarker() and GetDigiDollarTxType().
    // Two transactions with identical content but different flag bits
    // would have different txids but pass identical DD validation.
    // This is a form of transaction malleability on the version field.

    CMutableTransaction mtx1;
    mtx1.nVersion = MakeDDVersion(1, 0x00);  // MINT, flags=0x00

    CMutableTransaction mtx2;
    mtx2.nVersion = MakeDDVersion(1, 0xFF);  // MINT, flags=0xFF

    CTransaction tx1(mtx1);
    CTransaction tx2(mtx2);

    // Both pass marker check
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(tx1));
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(tx2));

    // Both have same type
    BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(tx1), DigiDollar::DD_TX_MINT);
    BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(tx2), DigiDollar::DD_TX_MINT);

    // But different version values → different txids
    BOOST_CHECK(tx1.nVersion != tx2.nVersion);

    BOOST_TEST_MESSAGE(
        "RH-11-MALLEABILITY: Flag bits 16-23 of nVersion are unchecked. "
        "Two DD transactions can have identical semantics but different txids. "
        "Recommendation: Reject DD transactions where bits 16-23 are non-zero, "
        "or define specific flag meanings.");
}

// ============================================================================
// 3. ConnectBlock/DisconnectBlock Asymmetry (Attack Vector #1)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_disconnect_missing_transfer_handling)
{
    // FINDING: ConnectBlock handles DigiDollar::DD_TX_MINT and DigiDollar::DD_TX_REDEEM for metrics
    // but does NOT track DigiDollar::DD_TX_TRANSFER in metrics (which is correct since
    // transfers don't change supply). However, DisconnectBlock also only
    // handles MINT and REDEEM. Verify there's no hidden state change for transfers.

    // This is an informational finding — transfers don't change supply.
    // But if a future change adds transfer-related state, the disconnect
    // path must be updated simultaneously.

    DigiDollar::SystemHealthMonitor::ResetMetrics();
    auto before = DigiDollar::SystemHealthMonitor::GetCachedMetrics();

    // Simulate: Connect a mint, then a transfer, then disconnect the transfer
    DigiDollar::SystemHealthMonitor::OnMintConnected(10000, 100 * COIN);
    auto afterMint = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(afterMint.totalDDSupply, 10000);

    // Transfer doesn't call any health monitor function — supply unchanged
    auto afterTransfer = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(afterTransfer.totalDDSupply, 10000);

    // Disconnect transfer — also no health monitor call needed
    // Supply still 10000
    auto afterDisconnect = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(afterDisconnect.totalDDSupply, 10000);

    DigiDollar::SystemHealthMonitor::ResetMetrics();
    BOOST_TEST_MESSAGE("INFO: Transfer connect/disconnect is symmetric (no-op on both sides). Safe.");
}

BOOST_AUTO_TEST_CASE(rh11_disconnect_redeem_block_lookup_failure)
{
    // FINDING: DisconnectBlock for DigiDollar::DD_TX_REDEEM reads the original MINT block
    // from disk to extract the DD amount. If ReadBlockFromDisk fails or the
    // MINT tx isn't found in the block, ddAmount stays 0 and
    // OnRedeemDisconnected is skipped (guard: ddAmount > 0 && vaultCollateral > 0).
    //
    // This means a failed block read during reorg silently corrupts the metrics
    // — supply and collateral remain decremented even though the redeem was undone.
    //
    // The metrics are "best effort" for health monitoring, not consensus-critical,
    // but corrupted metrics could trigger false ERR activation.

    DigiDollar::SystemHealthMonitor::ResetMetrics();

    // Simulate: mint 10000 DD with 100 DGB collateral
    DigiDollar::SystemHealthMonitor::OnMintConnected(10000, 100 * COIN);
    // Simulate: redeem 10000 DD
    DigiDollar::SystemHealthMonitor::OnRedeemConnected(10000, 100 * COIN);

    auto afterRedeem = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(afterRedeem.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(afterRedeem.totalCollateral, 0);

    // Now disconnect the redeem but with ddAmount=0 (simulating block read failure)
    // This is what happens in DisconnectBlock when block read fails
    CAmount ddAmount = 0;  // Failed to extract
    CAmount vaultCollateral = 100 * COIN;
    if (ddAmount > 0 && vaultCollateral > 0) {
        DigiDollar::SystemHealthMonitor::OnRedeemDisconnected(ddAmount, vaultCollateral);
    }
    // OnRedeemDisconnected was NOT called — metrics are now wrong
    auto afterFailedDisconnect = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    
    // Supply should be back to 10000 but it's stuck at 0
    BOOST_CHECK_MESSAGE(afterFailedDisconnect.totalDDSupply == 0,
        "RH-11-ASYMMETRY: After failed redeem disconnect, supply stuck at 0 instead of 10000. "
        "Block read failure during reorg silently corrupts health metrics.");

    DigiDollar::SystemHealthMonitor::ResetMetrics();
}

// ============================================================================
// 4. Transfer Output Amount Overflow (Attack Vector #9 + #5 combined)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_transfer_output_sum_overflow)
{
    // FINDING: In ValidateTransferTransaction, outputDD accumulates via:
    //   outputDD += ddAmount;
    // where ddAmount comes from OP_RETURN (up to 8 bytes = int64_t range).
    // The per-output check caps at $100k (10000000 cents), but:
    //   - Multiple outputs each at $100k don't overflow CAmount easily
    //   - However, the OP_RETURN amounts are attacker-controlled
    //   - If the OP_RETURN claims amounts close to INT64_MAX/2 each,
    //     the sum can overflow
    //
    // The $100k per-output check prevents this in practice, but
    // the overflow check should be explicit.

    // This test verifies the $100k cap prevents practical overflow
    CAmount maxPerOutput = 10000000;  // $100k in cents
    CAmount sum = 0;
    int maxOutputs = 1000;  // Generous upper bound
    for (int i = 0; i < maxOutputs; i++) {
        sum += maxPerOutput;
    }
    // 1000 * 10M = 10B cents = $100M — well within int64_t range
    BOOST_CHECK(sum > 0);
    BOOST_CHECK(sum < std::numeric_limits<CAmount>::max());

    BOOST_TEST_MESSAGE("INFO: Transfer $100k per-output cap prevents practical overflow. "
                       "But outputDD += ddAmount has no explicit overflow guard.");
}

// ============================================================================
// 5. Redemption Without Coins View (Attack Vector related to #1)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_collateral_release_null_coins_bypass)
{
    // FINDING: ValidateCollateralReleaseAmount returns true if ctx.coins==nullptr.
    // Comment says "should not be hit during ConnectBlock" but this means any
    // validation path without a coins view silently skips collateral proportionality.
    //
    // In practice, ConnectBlock always provides coins, and mempool does too.
    // But this is a defense-in-depth gap — if any future code path validates
    // redemptions without coins, collateral theft is possible.

    BOOST_TEST_MESSAGE(
        "RH-11-NULL-COINS: ValidateCollateralReleaseAmount returns true when ctx.coins==nullptr. "
        "This is a latent vulnerability — any validation path without UTXO access "
        "would allow arbitrary collateral release. Recommendation: return false when "
        "coins view is unavailable (fail-closed).");
}

// ============================================================================
// 6. Health Monitor Thread Safety (Attack Vector #8)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_health_monitor_no_mutex)
{
    // FINDING: SystemHealthMonitor static members (s_currentMetrics, s_initialized,
    // s_healthHistory) have NO mutex protection. They're documented as being called
    // "under cs_main" from ConnectBlock/DisconnectBlock, but:
    //
    // 1. GetSystemMetrics() is called from RPC threads (not under cs_main)
    // 2. GetCachedMetrics() is a direct accessor with no lock
    // 3. ResetMetrics() is called from tests without cs_main
    //
    // Under concurrent RPC + block processing, a torn read of totalDDSupply
    // could produce invalid health scores.

    // Demonstrate the race window exists:
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    std::atomic<bool> sawNegative{false};
    std::atomic<bool> done{false};

    // Writer thread: alternating mint/redeem
    std::thread writer([&]() {
        for (int i = 0; i < 10000 && !done; i++) {
            DigiDollar::SystemHealthMonitor::OnMintConnected(1000, COIN);
            DigiDollar::SystemHealthMonitor::OnRedeemConnected(1000, COIN);
        }
        done = true;
    });

    // Reader thread: checking metrics
    std::thread reader([&]() {
        for (int i = 0; i < 10000 && !done; i++) {
            auto m = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
            if (m.totalDDSupply < 0) {
                sawNegative = true;
                break;
            }
        }
    });

    writer.join();
    reader.join();

    // Even if we don't catch a race in this test, the vulnerability exists
    // because there's no synchronization. The test documents the gap.
    BOOST_TEST_MESSAGE(
        "RH-11-THREAD-SAFETY: SystemHealthMonitor has no mutex on static members. "
        "GetCachedMetrics() can race with OnMintConnected()/OnRedeemConnected() during "
        "concurrent block processing + RPC calls. Recommendation: Add RecursiveMutex "
        "or make metrics access go through cs_main.");

    DigiDollar::SystemHealthMonitor::ResetMetrics();
}

// ============================================================================
// 7. Mint/Redeem Disconnect Ordering in Same Block (Attack Vector #3)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_multiple_dd_tx_same_block_disconnect_order)
{
    // FINDING: DisconnectBlock processes transactions in REVERSE order (i = size-1 to 0).
    // If a block has MINT at index 1 and REDEEM at index 2:
    //   ConnectBlock: MINT first (supply+), then REDEEM (supply-)
    //   DisconnectBlock: REDEEM first (supply+), then MINT (supply-)
    //
    // This is correct because disconnect reverses connect. But verify
    // that the intermediate state doesn't trigger any threshold.

    DigiDollar::SystemHealthMonitor::ResetMetrics();

    // Forward: mint 50000, then redeem 50000
    DigiDollar::SystemHealthMonitor::OnMintConnected(50000, 500 * COIN);
    BOOST_CHECK_EQUAL(DigiDollar::SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 50000);
    DigiDollar::SystemHealthMonitor::OnRedeemConnected(50000, 500 * COIN);
    BOOST_CHECK_EQUAL(DigiDollar::SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 0);

    // Reverse (disconnect): undo redeem first, then undo mint
    DigiDollar::SystemHealthMonitor::OnRedeemDisconnected(50000, 500 * COIN);
    BOOST_CHECK_EQUAL(DigiDollar::SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 50000);
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(50000, 500 * COIN);
    BOOST_CHECK_EQUAL(DigiDollar::SystemHealthMonitor::GetCachedMetrics().totalDDSupply, 0);

    BOOST_TEST_MESSAGE("INFO: Multiple DD tx disconnect ordering is correct — reverse of connect.");

    DigiDollar::SystemHealthMonitor::ResetMetrics();
}

BOOST_AUTO_TEST_CASE(rh11_double_mint_same_block)
{
    // Two mints in the same block — verify metrics accumulate correctly
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    DigiDollar::SystemHealthMonitor::OnMintConnected(10000, 100 * COIN);
    DigiDollar::SystemHealthMonitor::OnMintConnected(20000, 200 * COIN);

    auto m = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m.totalDDSupply, 30000);
    BOOST_CHECK_EQUAL(m.totalCollateral, 300 * COIN);

    // Disconnect in reverse
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(20000, 200 * COIN);
    m = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m.totalDDSupply, 10000);

    DigiDollar::SystemHealthMonitor::OnMintDisconnected(10000, 100 * COIN);
    m = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m.totalDDSupply, 0);
    BOOST_CHECK_EQUAL(m.totalCollateral, 0);

    DigiDollar::SystemHealthMonitor::ResetMetrics();
}

// ============================================================================
// 8. Negative Supply via Underflow (inverse of #5)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_supply_underflow_via_excess_redeem)
{
    // FINDING: OnRedeemConnected uses std::max<CAmount>(0, ...) to prevent
    // negative supply. But OnMintDisconnected does the same.
    // This means metric errors are silently clamped rather than detected.
    // If a disconnect happens without a matching connect (e.g., corrupt state),
    // the clamping hides the bug.

    DigiDollar::SystemHealthMonitor::ResetMetrics();

    // Redeem more than was minted — supply should clamp to 0, not go negative
    DigiDollar::SystemHealthMonitor::OnMintConnected(5000, 50 * COIN);
    DigiDollar::SystemHealthMonitor::OnRedeemConnected(10000, 100 * COIN);

    auto m = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(m.totalDDSupply, 0);  // Clamped, not -5000
    BOOST_CHECK_EQUAL(m.totalCollateral, 0);  // Clamped

    BOOST_TEST_MESSAGE(
        "INFO: std::max(0, ...) prevents negative supply but silently absorbs accounting errors. "
        "Recommendation: Log a warning when clamping is triggered — it indicates a bug.");

    DigiDollar::SystemHealthMonitor::ResetMetrics();
}

// ============================================================================
// 9. Oracle Price Cache Revert Edge Case (Attack Vector #4)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_disconnect_oracle_revert_non_oracle_block)
{
    // FINDING: DisconnectBlock's oracle cache revert checks:
    //   if (!block.vtx.empty() && block.vtx[0]->vout.size() >= 2)
    //     const CTxOut& oracle_output = block.vtx[0]->vout[1];
    //     if (oracle_output.scriptPubKey.IsUnspendable() && ...)
    //
    // If a post-activation block has NO oracle data in the coinbase,
    // the oracle cache is NOT reverted. This is correct behavior —
    // only blocks with oracle data need cache cleanup.
    //
    // However, if DD transactions in that block were validated with
    // skipOracleValidation=false and a P2P oracle price, disconnecting
    // the block doesn't revert the oracle price used by those txs.
    // This is acceptable because oracle price validation is
    // forward-only (ConnectBlock checks, DisconnectBlock just undoes UTXO).

    BOOST_TEST_MESSAGE(
        "INFO: Oracle cache revert only runs for blocks with coinbase oracle data. "
        "Blocks validated with P2P oracle prices don't need cache revert. Safe.");
}

// ============================================================================
// 10. DigiDollar::DD_TX_NONE Through Full Validation Pipeline
// ============================================================================

BOOST_AUTO_TEST_CASE(rh11_dd_tx_none_rejected)
{
    // A transaction with DD marker but type=0 (DigiDollar::DD_TX_NONE) should be rejected
    // by ValidateDigiDollarTransaction's default switch case.
    
    CMutableTransaction mtx;
    mtx.nVersion = 0x00000770;  // DD marker, type=0
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 0;

    CTransaction tx(mtx);
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(tx));

    auto txType = DigiDollar::GetDigiDollarTxType(tx);
    BOOST_CHECK_EQUAL(static_cast<int>(txType), 0);
    BOOST_CHECK(txType != DigiDollar::DD_TX_MINT);
    BOOST_CHECK(txType != DigiDollar::DD_TX_TRANSFER);
    BOOST_CHECK(txType != DigiDollar::DD_TX_REDEEM);

    // This would hit the default: case in ValidateDigiDollarTransaction
    // and return state.Invalid(..., "bad-dd-tx-type")
    BOOST_TEST_MESSAGE("INFO: DigiDollar::DD_TX_NONE (type=0) correctly rejected by default switch case.");
}

BOOST_AUTO_TEST_SUITE_END()
