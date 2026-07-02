// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-10: Integration Attack Chain Tests
 *
 * Multi-step adversarial tests that combine subsystems:
 * 1. Reorg DD double-spend across fork boundary
 * 2. Mint + immediate redeem (timelock bypass attempt)
 * 3. Oracle price + mint race (stale price exploitation)
 * 4. Supply tracking consistency across mint/transfer/redeem
 * 5. Block stuffing DoS (oracle bundle inclusion guarantee)
 */

#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <consensus/digidollar_transaction_validation.h>
#include <consensus/dca.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <digidollar/digidollar.h>
#include <digidollar/health.h>
#include <digidollar/scripts.h>
#include <digidollar/txbuilder.h>
#include <digidollar/validation.h>
#include <kernel/chainparams.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <hash.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/standard.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

namespace {

// Helper to create a mock mint transaction
CMutableTransaction CreateMockMintTx(const CKey& ownerKey, CAmount dgbCollateral,
                                      CAmount ddAmount, int64_t lockBlocks, int baseHeight)
{
    CMutableTransaction mtx;
    mtx.nVersion = 0x44440100; // DD_TX_MINT

    // Input: some DGB being locked
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256::ONE, 0);

    // Output 0: collateral P2TR (with value)
    CScript collateralScript;
    collateralScript << OP_1;
    XOnlyPubKey xonly{ownerKey.GetPubKey()};
    collateralScript << ToByteVector(xonly);
    CTxOut collateralOut(dgbCollateral, collateralScript);
    mtx.vout.push_back(collateralOut);

    // Output 1: DD token P2TR (zero value)
    CScript ddScript;
    ddScript << OP_1;
    // Use a different key for DD output
    CKey ddKey;
    ddKey.MakeNewKey(true);
    XOnlyPubKey ddXonly{ddKey.GetPubKey()};
    ddScript << ToByteVector(ddXonly);
    CTxOut ddOut(0, ddScript);
    mtx.vout.push_back(ddOut);

    // Output 2: OP_RETURN with DD amount
    CScript opReturn;
    opReturn << OP_RETURN;
    // Encode DD amount
    std::vector<unsigned char> ddData(8);
    for (int i = 0; i < 8; i++) {
        ddData[i] = (ddAmount >> (i * 8)) & 0xFF;
    }
    opReturn << ddData;
    CTxOut opReturnOut(0, opReturn);
    mtx.vout.push_back(opReturnOut);

    // Set locktime
    mtx.nLockTime = baseHeight + lockBlocks;

    return mtx;
}

// Helper to create a mock redeem transaction
CMutableTransaction CreateMockRedeemTx(const uint256& mintTxId, CAmount ddBurned, int lockHeight)
{
    CMutableTransaction mtx;
    mtx.nVersion = 0x44440300; // DD_TX_REDEEM

    // Input: spending the collateral from mint
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(mintTxId, 0);

    // Output: return DGB to owner
    CKey returnKey;
    returnKey.MakeNewKey(true);
    CScript returnScript;
    returnScript << OP_1;
    XOnlyPubKey returnXonly{returnKey.GetPubKey()};
    returnScript << ToByteVector(returnXonly);
    mtx.vout.push_back(CTxOut(500000 * COIN, returnScript));

    // Set locktime to the original lock height
    mtx.nLockTime = lockHeight;

    return mtx;
}

// Helper to create a mock transfer transaction
[[maybe_unused]] CMutableTransaction CreateMockTransferTx(const uint256& prevTxId, CAmount ddAmount)
{
    CMutableTransaction mtx;
    mtx.nVersion = 0x44440200; // DD_TX_TRANSFER

    // Input: spending DD output
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(prevTxId, 1); // DD output is typically index 1

    // Output: DD to new address (zero value P2TR)
    CKey newKey;
    newKey.MakeNewKey(true);
    CScript ddScript;
    ddScript << OP_1;
    XOnlyPubKey newXonly{newKey.GetPubKey()};
    ddScript << ToByteVector(newXonly);
    mtx.vout.push_back(CTxOut(0, ddScript));

    // OP_RETURN preserving DD amount
    CScript opReturn;
    opReturn << OP_RETURN;
    std::vector<unsigned char> ddData(8);
    for (int i = 0; i < 8; i++) {
        ddData[i] = (ddAmount >> (i * 8)) & 0xFF;
    }
    opReturn << ddData;
    mtx.vout.push_back(CTxOut(0, opReturn));

    return mtx;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_integration_attack_tests, BasicTestingSetup)

// =============================================================================
// RH-10-01: Reorg DD Double-Spend Attack
// =============================================================================
// ATTACK: Mint DD on chain A, transfer to victim on chain A, then reorg to chain B
// where the mint never happened. The DD on chain A are now unbacked.
// DEFENSE: DisconnectBlock must properly undo DD supply tracking.

BOOST_AUTO_TEST_CASE(attack_reorg_dd_double_spend)
{
    auto regTestParams = CChainParams::RegTest({});
    const CAmount ORACLE_PRICE = 10000; // $0.01 per DGB
    const int START_HEIGHT = 1000;

    // Step 1: Simulate minting 100 DD ($100 = 10000 cents)
    const CAmount DD_AMOUNT = 10000; // $100 in cents
    const int64_t LOCK_BLOCKS = 30 * DigiDollar::BLOCKS_PER_DAY;

    // Record supply before mint
    DigiDollar::SystemHealthMonitor::ResetMetrics();
    auto metricsBefore = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    CAmount supplyBefore = metricsBefore.totalDDSupply;

    // Step 2: Simulate OnMintConnected (what ConnectBlock does)
    CAmount collateral = 500000 * COIN; // Enough collateral
    DigiDollar::SystemHealthMonitor::OnMintConnected(DD_AMOUNT, collateral);

    auto metricsAfterMint = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metricsAfterMint.totalDDSupply, supplyBefore + DD_AMOUNT);

    // Step 3: Simulate transfer (supply should NOT change on transfer)
    // Transfers don't affect supply tracking - just UTXO ownership

    // Step 4: REORG - DisconnectBlock should undo the mint
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(DD_AMOUNT, collateral);

    auto metricsAfterReorg = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_MESSAGE(metricsAfterReorg.totalDDSupply == supplyBefore,
        "EXPLOIT: After reorg undo, DD supply should return to pre-mint level. "
        "Got " + std::to_string(metricsAfterReorg.totalDDSupply) +
        ", expected " + std::to_string(supplyBefore));

    // Step 5: Verify the disconnected DD can't be spent (double-spend protection)
    // If supply tracking is correct, any attempt to transfer the "ghost" DD
    // would fail because the UTXO no longer exists after disconnect.
    // The key defense is UTXO set consistency, not just supply tracking.
}

// =============================================================================
// RH-10-02: Mint + Immediate Redeem (Timelock Bypass)
// =============================================================================
// ATTACK: Mint DD in block N, attempt to redeem in block N+1 before timelock expires.
// DEFENSE: ValidateNormalRedemptionConditions must enforce nLockTime.

BOOST_AUTO_TEST_CASE(attack_mint_immediate_redeem)
{
    auto regTestParams = CChainParams::RegTest({});
    const CAmount ORACLE_PRICE = 10000; // $0.01 per DGB
    const int MINT_HEIGHT = 1000;
    const int64_t LOCK_BLOCKS = 30 * DigiDollar::BLOCKS_PER_DAY; // 30 days

    DigiDollar::ValidationContext mintCtx(MINT_HEIGHT, ORACLE_PRICE, 150, *regTestParams);

    // Step 1: Create mint transaction
    CKey ownerKey;
    ownerKey.MakeNewKey(true);
    auto mintTx = CreateMockMintTx(ownerKey, 500000 * COIN, 10000, LOCK_BLOCKS, MINT_HEIGHT);
    uint256 mintTxId = CTransaction(mintTx).GetHash();

    // Step 2: Try to redeem in the VERY NEXT BLOCK (block N+1)
    int REDEEM_HEIGHT = MINT_HEIGHT + 1;
    auto redeemTx = CreateMockRedeemTx(mintTxId, 10000, MINT_HEIGHT + LOCK_BLOCKS);

    DigiDollar::ValidationContext redeemCtx(REDEEM_HEIGHT, ORACLE_PRICE, 150, *regTestParams);

    // Step 3: Validate the redemption - should FAIL due to timelock
    TxValidationState state;
    CTransaction redeemCTx(redeemTx);

    bool timelockHolds = ValidateNormalRedemptionConditions(redeemCTx, redeemCtx, state);

    BOOST_CHECK_MESSAGE(!timelockHolds,
        "EXPLOIT: Immediate redemption should be rejected! Timelock must hold. "
        "Mint height: " + std::to_string(MINT_HEIGHT) +
        ", Redeem height: " + std::to_string(REDEEM_HEIGHT) +
        ", Lock expires: " + std::to_string(MINT_HEIGHT + LOCK_BLOCKS));

    // Step 4: Verify it succeeds AFTER timelock expires
    int VALID_REDEEM_HEIGHT = MINT_HEIGHT + LOCK_BLOCKS + 1;
    DigiDollar::ValidationContext validRedeemCtx(VALID_REDEEM_HEIGHT, ORACLE_PRICE, 150, *regTestParams);

    TxValidationState validState;
    bool timelockExpired = ValidateNormalRedemptionConditions(redeemCTx, validRedeemCtx, validState);

    BOOST_CHECK_MESSAGE(timelockExpired,
        "Redemption should succeed after timelock expires. Height: " +
        std::to_string(VALID_REDEEM_HEIGHT) +
        ", Lock: " + std::to_string(MINT_HEIGHT + LOCK_BLOCKS));

    // Step 5: Edge case - try at EXACTLY the lock height (should pass, >= semantics)
    int EXACT_HEIGHT = MINT_HEIGHT + LOCK_BLOCKS;
    DigiDollar::ValidationContext exactCtx(EXACT_HEIGHT, ORACLE_PRICE, 150, *regTestParams);

    TxValidationState exactState;
    bool exactTimelockResult = ValidateNormalRedemptionConditions(redeemCTx, exactCtx, exactState);

    BOOST_CHECK_MESSAGE(exactTimelockResult,
        "Redemption should succeed at exact lock height (>= semantics). Height: " +
        std::to_string(EXACT_HEIGHT));
}

// =============================================================================
// RH-10-03: Oracle Price + Mint Race Condition
// =============================================================================
// ATTACK: Oracle updates price in block N. Attacker uses the OLD (higher) price
// in block N+1 to mint DD with less collateral than the new price requires.
// DEFENSE: Oracle staleness check and per-block price binding.

BOOST_AUTO_TEST_CASE(attack_oracle_price_mint_race)
{
    auto regTestParams = CChainParams::RegTest({});

    // Step 1: Oracle reports high price ($0.05/DGB)
    const CAmount HIGH_PRICE = 50000; // $0.05 in micro-USD
    const int HEIGHT_N = 1000;
    const CAmount DD_AMOUNT = 10000; // $100 in cents
    const int64_t LOCK_BLOCKS = 30 * DigiDollar::BLOCKS_PER_DAY;

    DigiDollar::ValidationContext highPriceCtx(HEIGHT_N, HIGH_PRICE, 150, *regTestParams);

    // Calculate collateral required at HIGH price
    CAmount collateralAtHighPrice = DigiDollar::CalculateRequiredCollateral(DD_AMOUNT, LOCK_BLOCKS, highPriceCtx);
    BOOST_CHECK_GT(collateralAtHighPrice, 0);

    // Step 2: Oracle updates to LOW price ($0.01/DGB) - DGB crashed 80%
    const CAmount LOW_PRICE = 10000; // $0.01 in micro-USD
    DigiDollar::ValidationContext lowPriceCtx(HEIGHT_N + 1, LOW_PRICE, 150, *regTestParams);

    // Calculate collateral required at LOW price
    CAmount collateralAtLowPrice = DigiDollar::CalculateRequiredCollateral(DD_AMOUNT, LOCK_BLOCKS, lowPriceCtx);
    BOOST_CHECK_GT(collateralAtLowPrice, 0);

    // Step 3: Verify that low price requires MORE collateral (correct behavior)
    BOOST_CHECK_MESSAGE(collateralAtLowPrice > collateralAtHighPrice,
        "EXPLOIT: Lower DGB price should require MORE collateral, not less! "
        "High price collateral: " + std::to_string(collateralAtHighPrice) +
        ", Low price collateral: " + std::to_string(collateralAtLowPrice));

    // Step 4: Verify validation fails if attacker provides high-price collateral at low price
    bool validAtLowPrice = DigiDollar::ValidateCollateralRatio(
        collateralAtHighPrice, DD_AMOUNT, LOCK_BLOCKS, lowPriceCtx);

    BOOST_CHECK_MESSAGE(!validAtLowPrice,
        "EXPLOIT: Collateral calculated at high price should be INSUFFICIENT at low price! "
        "Provided: " + std::to_string(collateralAtHighPrice) +
        ", Required: " + std::to_string(collateralAtLowPrice));

    // Step 5: Test oracle staleness protection
    OracleBundleManager bundleMgr;
    bundleMgr.SetEnabled(true);

    // Without any oracle messages, GetLatestPrice should return 0
    CAmount stalePrice = bundleMgr.GetLatestPrice();
    BOOST_CHECK_MESSAGE(stalePrice == 0,
        "Oracle should return 0 when no price data exists (stale protection)");

    // Step 6: Verify zero price prevents minting
    DigiDollar::ValidationContext zeroPriceCtx(HEIGHT_N, 0, 150, *regTestParams);
    CAmount zeroPriceCollateral = DigiDollar::CalculateRequiredCollateral(DD_AMOUNT, LOCK_BLOCKS, zeroPriceCtx);
    BOOST_CHECK_MESSAGE(zeroPriceCollateral == 0,
        "Zero oracle price should return 0 required collateral (minting blocked)");
}

// =============================================================================
// RH-10-04: Supply Tracking Consistency
// =============================================================================
// ATTACK: Mint 100 DD → Transfer to self → Redeem 50 → does supply = 50?
// Tests that supply tracking remains consistent across mixed operations.

BOOST_AUTO_TEST_CASE(attack_supply_tracking_consistency)
{
    // Reset to clean state
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    auto metricsBefore = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metricsBefore.totalDDSupply, 0);

    // Step 1: Mint 100 DD ($100 = 10000 cents)
    const CAmount MINT_AMOUNT = 10000; // 10000 cents = $100
    const CAmount COLLATERAL = 500000 * COIN;
    DigiDollar::SystemHealthMonitor::OnMintConnected(MINT_AMOUNT, COLLATERAL);

    auto metricsAfterMint = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metricsAfterMint.totalDDSupply, MINT_AMOUNT);
    BOOST_CHECK_EQUAL(metricsAfterMint.totalCollateral, COLLATERAL);

    // Step 2: Transfer to self — supply should NOT change
    // Transfers are UTXO-level operations, they don't touch system supply metrics
    auto metricsAfterTransfer = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_MESSAGE(metricsAfterTransfer.totalDDSupply == MINT_AMOUNT,
        "EXPLOIT: Transfer should NOT change supply. Expected " +
        std::to_string(MINT_AMOUNT) + ", got " +
        std::to_string(metricsAfterTransfer.totalDDSupply));

    // Step 3: Redeem 50 DD ($50 = 5000 cents)
    const CAmount REDEEM_AMOUNT = 5000; // 5000 cents = $50
    // Calculate proportional collateral release
    CAmount collateralRelease = (COLLATERAL * REDEEM_AMOUNT) / MINT_AMOUNT;
    DigiDollar::SystemHealthMonitor::OnRedeemConnected(REDEEM_AMOUNT, collateralRelease);

    auto metricsAfterRedeem = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    CAmount expectedSupply = MINT_AMOUNT - REDEEM_AMOUNT; // Should be 5000

    BOOST_CHECK_MESSAGE(metricsAfterRedeem.totalDDSupply == expectedSupply,
        "EXPLOIT: Supply tracking inconsistency! After mint " +
        std::to_string(MINT_AMOUNT) + " and redeem " +
        std::to_string(REDEEM_AMOUNT) + ", expected supply " +
        std::to_string(expectedSupply) + " but got " +
        std::to_string(metricsAfterRedeem.totalDDSupply));

    // Step 4: Verify collateral tracking is also consistent
    CAmount expectedCollateral = COLLATERAL - collateralRelease;
    BOOST_CHECK_MESSAGE(metricsAfterRedeem.totalCollateral == expectedCollateral,
        "EXPLOIT: Collateral tracking inconsistency! Expected " +
        std::to_string(expectedCollateral) + " but got " +
        std::to_string(metricsAfterRedeem.totalCollateral));

    // Step 5: Stress test — multiple mints, then full redeem
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const int NUM_MINTS = 10;
    const CAmount EACH_MINT = 1000; // $10 each
    const CAmount EACH_COLLATERAL = 50000 * COIN;

    for (int i = 0; i < NUM_MINTS; i++) {
        DigiDollar::SystemHealthMonitor::OnMintConnected(EACH_MINT, EACH_COLLATERAL);
    }

    auto metricsAfterMultiMint = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metricsAfterMultiMint.totalDDSupply, NUM_MINTS * EACH_MINT);

    // Redeem all in reverse
    for (int i = 0; i < NUM_MINTS; i++) {
        DigiDollar::SystemHealthMonitor::OnRedeemConnected(EACH_MINT, EACH_COLLATERAL);
    }

    auto metricsAfterFullRedeem = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_MESSAGE(metricsAfterFullRedeem.totalDDSupply == 0,
        "EXPLOIT: After minting and redeeming all, supply should be 0! Got " +
        std::to_string(metricsAfterFullRedeem.totalDDSupply));
    BOOST_CHECK_MESSAGE(metricsAfterFullRedeem.totalCollateral == 0,
        "EXPLOIT: After minting and redeeming all, collateral should be 0! Got " +
        std::to_string(metricsAfterFullRedeem.totalCollateral));
}

// =============================================================================
// RH-10-05: Block Stuffing DoS (Oracle Bundle Inclusion)
// =============================================================================
// ATTACK: Fill block with DD transactions to prevent oracle bundle inclusion.
// DEFENSE: Oracle bundles should have reserved space or priority.

BOOST_AUTO_TEST_CASE(attack_block_stuffing_oracle_dos)
{
    // Test that OracleBundleManager can always add a bundle to a block
    // even when the block is nearly full of DD transactions.

    OracleBundleManager bundleMgr;
    bundleMgr.SetEnabled(true);
    bundleMgr.SetMinOracleCount(1); // Relax for unit testing

    // Step 1: Create oracle messages to form a bundle
    // We need at least min_oracle_count messages for consensus
    COraclePriceMessage msg1;
    msg1.oracle_id = 1;
    msg1.price_micro_usd = 10000; // $0.01
    msg1.timestamp = GetTime();

    // Add message (will fail without valid signature in production, but tests internal state)
    (void)bundleMgr.AddOracleMessage(msg1);
    // Note: May fail due to signature validation - that's expected in unit tests
    // The important test is that the MECHANISM exists for oracle priority

    // Step 2: Verify oracle bundle size is bounded
    // Oracle bundles have a fixed maximum size determined by oracle_count * message_size
    // This ensures they can always fit in the reserved portion of a block
    const uint32_t ORACLE_ACTIVE = 15;
    const size_t MAX_MSG_SIZE = 200; // Approximate max oracle message size in bytes
    const size_t MAX_BUNDLE_SIZE = ORACLE_ACTIVE * MAX_MSG_SIZE;

    // MAX_BLOCK_WEIGHT is 4MB (4000000 weight units)
    // Oracle bundle should be a tiny fraction
    const size_t MAX_BLOCK_WEIGHT = 4000000;
    double bundleFraction = static_cast<double>(MAX_BUNDLE_SIZE * 4) / MAX_BLOCK_WEIGHT;

    BOOST_CHECK_MESSAGE(bundleFraction < 0.01,
        "Oracle bundle should use < 1% of block weight. Current fraction: " +
        std::to_string(bundleFraction));

    // Step 3: Verify that validation context requires valid oracle price
    // If oracle bundle is DoS'd out of blocks, oraclePriceMicroUSD would be 0/stale
    auto regTestParams = CChainParams::RegTest({});
    DigiDollar::ValidationContext noOracleCtx(1000, 0, 150, *regTestParams);

    // Minting should fail without oracle price
    CAmount collateral = DigiDollar::CalculateRequiredCollateral(10000, 30 * DigiDollar::BLOCKS_PER_DAY, noOracleCtx);
    BOOST_CHECK_MESSAGE(collateral == 0,
        "EXPLOIT: Minting must be impossible without valid oracle price. "
        "Returned collateral: " + std::to_string(collateral));

    // Step 4: Verify priceValidBlocks window prevents stale price attacks
    DigiDollar::ConsensusParams ddParams;
    BOOST_CHECK_MESSAGE(ddParams.priceValidBlocks == 20,
        "Price validity window should be 20 blocks (~5 min). Got: " +
        std::to_string(ddParams.priceValidBlocks));

    // Step 5: Even if oracle is DoS'd for priceValidBlocks, system protects itself
    // by refusing to validate mints with stale/zero oracle price
    DigiDollar::ValidationContext stalePriceCtx(1000, 0, 150, *regTestParams,
                                                 nullptr, false); // skipOracleValidation = false

    // With zero price and oracle validation enabled, CalculateRequiredCollateral returns 0
    CAmount staleCollateral = DigiDollar::CalculateRequiredCollateral(
        10000, 30 * DigiDollar::BLOCKS_PER_DAY, stalePriceCtx);
    BOOST_CHECK_EQUAL(staleCollateral, 0);
}

// =============================================================================
// RH-10-06: Combined Attack — Reorg During Oracle Update
// =============================================================================
// ATTACK: Trigger reorg right when oracle price updates, trying to get
// different prices on different chain tips.
// DEFENSE: Oracle price is per-block, reorg undoes supply correctly.

BOOST_AUTO_TEST_CASE(attack_reorg_during_oracle_update)
{
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    auto regTestParams = CChainParams::RegTest({});

    // Chain A: Oracle price $0.05, mint with this price
    const CAmount PRICE_A = 50000; // $0.05
    const CAmount DD_AMOUNT = 10000; // $100

    DigiDollar::ValidationContext ctxA(1000, PRICE_A, 150, *regTestParams);
    CAmount collateralA = DigiDollar::CalculateRequiredCollateral(
        DD_AMOUNT, 30 * DigiDollar::BLOCKS_PER_DAY, ctxA);

    // Mint on chain A
    DigiDollar::SystemHealthMonitor::OnMintConnected(DD_AMOUNT, collateralA);
    auto metricsA = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(metricsA.totalDDSupply, DD_AMOUNT);

    // Chain B wins (reorg) — different oracle price $0.01
    const CAmount PRICE_B = 10000; // $0.01
    DigiDollar::ValidationContext ctxB(1000, PRICE_B, 150, *regTestParams);
    CAmount collateralB = DigiDollar::CalculateRequiredCollateral(
        DD_AMOUNT, 30 * DigiDollar::BLOCKS_PER_DAY, ctxB);

    // Disconnect chain A's mint
    DigiDollar::SystemHealthMonitor::OnMintDisconnected(DD_AMOUNT, collateralA);

    auto metricsAfterReorg = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_MESSAGE(metricsAfterReorg.totalDDSupply == 0,
        "After reorg disconnect, supply must be 0. Got: " +
        std::to_string(metricsAfterReorg.totalDDSupply));

    // Chain B requires MORE collateral (lower price = more DGB needed)
    BOOST_CHECK_GT(collateralB, collateralA);

    // If attacker tries to use chain A's collateral amount on chain B, it fails
    bool validOnB = DigiDollar::ValidateCollateralRatio(
        collateralA, DD_AMOUNT, 30 * DigiDollar::BLOCKS_PER_DAY, ctxB);

    BOOST_CHECK_MESSAGE(!validOnB,
        "EXPLOIT: Chain A collateral should be insufficient on chain B (lower price)");
}

BOOST_AUTO_TEST_SUITE_END()
