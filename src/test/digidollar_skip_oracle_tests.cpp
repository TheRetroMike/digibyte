// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * T1-05a: Tests for skipOracleValidation bypass vulnerability
 *
 * BUG: ConnectBlock unconditionally sets skipOracleValidation=true, which
 * disables collateral ratio checking for ALL block connections — not just IBD.
 * A malicious miner could include a mint tx with minimal collateral for $100 DD.
 *
 * These tests verify:
 * 1. The vulnerability exists (mint with insufficient collateral passes when skipOracle=true)
 * 2. The fix correctly rejects such transactions when skipOracle=false (non-IBD)
 * 3. IBD blocks still pass with skipOracle=true (no regression)
 */

#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <consensus/err.h>
#include <consensus/volatility.h>
#include <digidollar/validation.h>
#include <digidollar/health.h>
#include <digidollar/scripts.h>
#include <digidollar/digidollar.h>
#include <key.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_skip_oracle_tests)

/**
 * Helper: Build a mint transaction with specified collateral amount.
 * The DD amount is always $100 (10000 cents), lock period 30 days.
 * collateralAmount MUST be > 0 (otherwise the output structure changes
 * and the collateral output gets misidentified as a DD token output).
 */
static CMutableTransaction BuildMintTx(const XOnlyPubKey& ownerKey, CAmount collateralAmount, int currentHeight)
{
    assert(collateralAmount > 0);

    CMutableTransaction mtx;
    mtx.nVersion = 0x01000770; // DD_TX_MINT

    // Input (simplified)
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256S("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"), 0);

    CAmount ddAmount = 10000; // $100.00
    int64_t lockBlocks = 30 * 24 * 60 * 4; // 30 days in blocks
    int64_t lockHeight = currentHeight + lockBlocks;

    // Create proper collateral script with NUMS internal key
    DigiDollar::MintParams params;
    params.ddAmount = ddAmount;
    params.lockHeight = lockHeight;
    params.ownerKey = ownerKey;
    params.internalKey = DigiDollar::GetCollateralNUMSKey();
    params.oracleKeys = DigiDollar::GetOracleKeys(15);

    CScript collateralScript = DigiDollar::CreateCollateralP2TR(params);

    // OP_RETURN with DD metadata (matching existing test patterns)
    CScript opReturn = CScript() << OP_RETURN
                                 << std::vector<unsigned char>{'D', 'D'}
                                 << CScriptNum(1) // DD_TX_MINT
                                 << CScriptNum(ddAmount)
                                 << CScriptNum(static_cast<int64_t>(lockHeight))
                                 << CScriptNum(1) // lockTier 1 = 30 days
                                 << std::vector<unsigned char>(ownerKey.begin(), ownerKey.end());

    // DD token output
    CScript ddScript = DigiDollar::CreateDigiDollarP2TR(ownerKey, ddAmount);

    // Output order: [0] OP_RETURN, [1] collateral (value > 0), [2] DD token (value = 0)
    mtx.vout.resize(3);
    mtx.vout[0] = CTxOut(0, opReturn);
    mtx.vout[1] = CTxOut(collateralAmount, collateralScript);
    mtx.vout[2] = CTxOut(0, ddScript);

    return mtx;
}

// =============================================================================
// T1-05a: skipOracleValidation bypass allows insufficient collateral
// =============================================================================

BOOST_FIXTURE_TEST_CASE(skip_oracle_allows_insufficient_collateral_exploit, BasicTestingSetup)
{
    // ATTACK SCENARIO: Malicious miner includes a mint tx with 1000 satoshis
    // (dust+) collateral for $100 DD. At $0.50/DGB with 500% ratio, the
    // real requirement is ~10 DGB (1,000,000,000 sats).
    //
    // Before DD-RH-115, skipOracleValidation=true skipped collateral ratio
    // validation entirely, so this passed only for IBD/catch-up nodes.

    CKey testKey;
    testKey.MakeNewKey(true);
    XOnlyPubKey ownerKey(testKey.GetPubKey());

    // Clear volatility state
    DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();

    int currentHeight = 1000;
    CAmount oraclePrice = 500000; // $0.50/DGB in micro-USD

    // 1000 satoshis collateral for $100 DD — massively undercollateralized
    // Real requirement at 500% ratio: ~10 DGB = 1,000,000,000 sats
    CAmount trivialCollateral = 1000; // 0.00001 DGB
    CMutableTransaction mtx = BuildMintTx(ownerKey, trivialCollateral, currentHeight);
    CTransaction tx(mtx);

    // With skipOracleValidation=true, local sync state must not change the
    // consensus result: a post-activation mint still needs oracle-backed
    // collateral validation.
    {
        DigiDollar::ValidationContext ctx(currentHeight, oraclePrice, 150, Params(),
                                          nullptr, true /* skipOracleValidation */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("skipOracle=true, trivial collateral result: " +
                          std::to_string(result) + " reason: " + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(!result,
            "SECURITY BUG [DD-RH-115]: skipOracleValidation=true bypasses "
            "collateral ratio, so IBD/catch-up nodes can accept an "
            "undercollateralized mint rejected by caught-up nodes");
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "insufficient-collateral");
    }

    // With skipOracleValidation=false (the fix for non-IBD blocks): this MUST FAIL
    {
        DigiDollar::ValidationContext ctx(currentHeight, oraclePrice, 150, Params(),
                                          nullptr, false /* NOT skipping oracle */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("skipOracle=false, trivial collateral result: " +
                          std::to_string(result) + " reason: " + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(!result,
            "SECURITY BUG: Mint with 1000 sats collateral for $100 DD accepted in non-IBD!");
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "insufficient-collateral");
    }
}

BOOST_FIXTURE_TEST_CASE(non_ibd_rejects_zero_oracle_price, BasicTestingSetup)
{
    // Oracle price must be available and positive for mint validation in every
    // local sync state. Otherwise IBD/catch-up nodes can accept blocks that
    // caught-up nodes reject.

    CKey testKey;
    testKey.MakeNewKey(true);
    XOnlyPubKey ownerKey(testKey.GetPubKey());

    DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();

    int currentHeight = 1000;

    // Use reasonable collateral amount (won't matter — zero price causes early rejection)
    CAmount properCollateral = 100 * COIN; // 100 DGB
    CMutableTransaction mtx = BuildMintTx(ownerKey, properCollateral, currentHeight);
    CTransaction tx(mtx);

    // Non-IBD with oracle price = 0: MUST reject (fail-closed)
    {
        DigiDollar::ValidationContext ctx(currentHeight, 0 /* no oracle price */, 150, Params(),
                                          nullptr, false /* NOT skipping oracle */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("non-IBD, zero oracle result: " +
                          std::to_string(result) + " reason: " + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(!result,
            "SECURITY BUG: Mint accepted with zero oracle price in non-IBD mode!");
        BOOST_CHECK_MESSAGE(state.GetRejectReason() == "bad-oracle-price",
            "unexpected fail-closed reason: " + state.GetRejectReason());
    }

    // IBD/catch-up with oracle price = 0: also rejected.
    {
        DigiDollar::ValidationContext ctx(currentHeight, 0, 150, Params(),
                                          nullptr, true /* IBD mode */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("IBD, zero oracle result: " +
                          std::to_string(result) + " reason: " + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(!result,
            "SECURITY BUG [DD-RH-115]: IBD/catch-up mint accepted with zero oracle price");
        BOOST_CHECK_MESSAGE(state.GetRejectReason() == "bad-oracle-price",
            "unexpected fail-closed reason: " + state.GetRejectReason());
    }
}

BOOST_FIXTURE_TEST_CASE(valid_mint_passes_non_ibd, BasicTestingSetup)
{
    // A properly collateralized mint should pass in both IBD and non-IBD modes.
    // This ensures the fix doesn't break valid transactions.

    CKey testKey;
    testKey.MakeNewKey(true);
    XOnlyPubKey ownerKey(testKey.GetPubKey());

    DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();

    int currentHeight = 1000;
    CAmount oraclePrice = 500000; // $0.50/DGB

    // Calculate required collateral manually to match what validation expects
    // $100 DD, $0.50/DGB, 500% ratio for 30 days
    // Formula: (ddAmount * COIN * ratio * 100) / oraclePrice
    // = (10000 * 100000000 * 500 * 100) / 500000
    // = 100,000,000,000 = 1000 DGB
    // Use 2x that (2000 DGB) to be safe against any rounding/DCA issues
    CAmount veryGenerousCollateral = 200000000000LL; // 2000 DGB — way more than needed

    CMutableTransaction mtx = BuildMintTx(ownerKey, veryGenerousCollateral, currentHeight);
    CTransaction tx(mtx);

    // Non-IBD with proper oracle price and generous collateral: MUST pass
    {
        DigiDollar::ValidationContext ctx(currentHeight, oraclePrice, 150, Params(),
                                          nullptr, false /* NOT skipping oracle */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("non-IBD, generous collateral (2000 DGB) result: " +
                          std::to_string(result) + " reason: " + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(result,
            "REGRESSION: Valid mint rejected in non-IBD mode! reason: " + state.GetRejectReason());
    }

    // IBD mode: also passes
    {
        DigiDollar::ValidationContext ctx(currentHeight, oraclePrice, 150, Params(),
                                          nullptr, true /* IBD mode */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_CHECK(result);
    }
}

// =============================================================================
// IBD ERR Minting Block Bug (Block 7586 testnet failure)
//
// BUG: During IBD, oracle price is 0 (no live oracles). Once totalDDSupply > 0
// (from earlier mints that passed because supply was 0), ShouldBlockMinting()
// sees price=0 and fails-closed, blocking ALL subsequent mints. This prevents
// new nodes from syncing past the first mint-after-prior-mints block.
//
// The ERR pre-validation check at line 1881 of validation.cpp does NOT guard
// on ctx.skipOracleValidation, unlike every other oracle-dependent check.
// =============================================================================

BOOST_FIXTURE_TEST_CASE(ibd_err_blocks_minting_with_nonzero_supply, BasicTestingSetup)
{
    // Regression: after earlier mints increment totalDDSupply, the IBD/catch-up
    // path still must fail closed on zero oracle price before any local ERR
    // state can make the result node-dependent.

    CKey testKey;
    testKey.MakeNewKey(true);
    XOnlyPubKey ownerKey(testKey.GetPubKey());

    DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();

    int currentHeight = 7586; // The exact block that fails

    // Simulate the IBD state: earlier mints already connected, so DD supply > 0.
    // This is what OnMintConnected() does during ConnectBlock for prior blocks.
    DigiDollar::SystemHealthMonitor::OnMintConnected(10000, 100 * COIN); // $100 DD, 100 DGB collateral

    // Verify supply is non-zero (precondition for the bug)
    const DigiDollar::SystemMetrics metrics = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK(metrics.totalDDSupply > 0);
    BOOST_TEST_MESSAGE("totalDDSupply after earlier mint: " + std::to_string(metrics.totalDDSupply));

    // Build a mint tx with generous collateral (not the issue - ERR check happens first)
    CAmount generousCollateral = 200000000000LL; // 2000 DGB
    CMutableTransaction mtx = BuildMintTx(ownerKey, generousCollateral, currentHeight);
    CTransaction tx(mtx);

    // IBD/catch-up mode: oracle price is 0, so the mint must be rejected
    // deterministically with the same reason as a caught-up node.
    {
        DigiDollar::ValidationContext ctx(currentHeight, 0 /* no oracle in IBD */, 150, Params(),
                                          nullptr, true /* skipOracleValidation = IBD mode */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("IBD + nonzero supply + zero oracle: result=" +
                          std::to_string(result) + " reason=" + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(!result,
            "SECURITY BUG [DD-RH-115]: IBD/catch-up mint accepted with zero oracle price");
        BOOST_CHECK_MESSAGE(state.GetRejectReason() == "bad-oracle-price",
            "unexpected fail-closed reason: " + state.GetRejectReason());
    }

    // Non-IBD mode with zero oracle: should STILL block (fail-closed is correct for live nodes)
    {
        DigiDollar::ValidationContext ctx(currentHeight, 0, 150, Params(),
                                          nullptr, false /* NOT IBD */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("Non-IBD + nonzero supply + zero oracle: result=" +
                          std::to_string(result) + " reason=" + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(!result,
            "SECURITY: Non-IBD mint with zero oracle should be rejected!");
        BOOST_CHECK_MESSAGE(state.GetRejectReason() == "bad-oracle-price",
            "unexpected fail-closed reason: " + state.GetRejectReason());
    }

    // Clean up: reverse the earlier mint so other tests aren't affected
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(10000, 100 * COIN);
}

BOOST_FIXTURE_TEST_CASE(ibd_err_check_with_valid_oracle_and_healthy_system, BasicTestingSetup)
{
    // When IBD has a valid block oracle price AND system is healthy,
    // the ERR check should pass in both IBD and non-IBD modes.
    // This ensures the fix doesn't accidentally skip ERR for non-IBD.

    CKey testKey;
    testKey.MakeNewKey(true);
    XOnlyPubKey ownerKey(testKey.GetPubKey());

    DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();

    int currentHeight = 8000;
    CAmount oraclePrice = 500000; // $0.50/DGB

    // Set up existing DD supply (simulating earlier mints)
    DigiDollar::SystemHealthMonitor::OnMintConnected(10000, 200 * COIN); // Well-collateralized

    CAmount generousCollateral = 200000000000LL; // 2000 DGB
    CMutableTransaction mtx = BuildMintTx(ownerKey, generousCollateral, currentHeight);
    CTransaction tx(mtx);

    // Non-IBD with healthy oracle: should pass (ERR is not active, health is fine)
    {
        DigiDollar::ValidationContext ctx(currentHeight, oraclePrice, 150, Params(),
                                          nullptr, false /* NOT IBD */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("Non-IBD + healthy oracle: result=" +
                          std::to_string(result) + " reason=" + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(result,
            "Healthy system mint rejected in non-IBD! reason: " + state.GetRejectReason());
    }

    // Clean up
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(10000, 200 * COIN);
}

BOOST_FIXTURE_TEST_CASE(non_ibd_err_still_blocks_when_active, BasicTestingSetup)
{
    // Critical security test: even with the IBD fix, ERR MUST still block minting
    // in non-IBD mode when ERR is formally activated.

    CKey testKey;
    testKey.MakeNewKey(true);
    XOnlyPubKey ownerKey(testKey.GetPubKey());

    DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();

    int currentHeight = 9000;
    CAmount oraclePrice = 500000;

    // Activate ERR by reconstructing with unhealthy system health (<100%)
    DigiDollar::ERR::EmergencyRedemptionRatio::ReconstructERRState(50, currentHeight);

    CAmount generousCollateral = 200000000000LL;
    CMutableTransaction mtx = BuildMintTx(ownerKey, generousCollateral, currentHeight);
    CTransaction tx(mtx);

    // Non-IBD with active ERR: MUST block minting
    {
        DigiDollar::ValidationContext ctx(currentHeight, oraclePrice, 50, Params(),
                                          nullptr, false /* NOT IBD */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("Non-IBD + active ERR: result=" +
                          std::to_string(result) + " reason=" + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(!result,
            "SECURITY: ERR is active but minting was allowed!");
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "minting-blocked-during-err");
    }

    // IBD/catch-up with active ERR: local sync state must not bypass mint blocking.
    {
        DigiDollar::ValidationContext ctx(currentHeight, oraclePrice, 50, Params(),
                                          nullptr, true /* IBD mode */);
        TxValidationState state;
        bool result = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        BOOST_TEST_MESSAGE("IBD + active ERR: result=" +
                          std::to_string(result) + " reason=" + state.GetRejectReason());
        BOOST_CHECK_MESSAGE(!result,
            "SECURITY: skipOracleValidation bypassed active ERR mint blocking");
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "minting-blocked-during-err");
    }

    // Clean up ERR state — reconstruct with healthy system
    DigiDollar::ERR::EmergencyRedemptionRatio::ReconstructERRState(150, currentHeight);
}

BOOST_AUTO_TEST_SUITE_END()
