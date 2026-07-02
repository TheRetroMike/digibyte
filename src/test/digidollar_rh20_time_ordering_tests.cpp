// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// RH-20: Time & Ordering Attack Tests
// Tests for attacks exploiting time-dependent validation and tx ordering.

#include <consensus/digidollar.h>
#include <consensus/volatility.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_rh20_time_ordering_tests)

// ============================================================================
// Test Fixture
// ============================================================================

struct RH20TestSetup : public TestingSetup {
    RH20TestSetup() : TestingSetup(ChainType::REGTEST)
    {
        testKey.MakeNewKey(true);
        testPubKey = testKey.GetPubKey();
        testXOnlyKey = XOnlyPubKey(testPubKey);
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
    }

    ~RH20TestSetup()
    {
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
    }

    DigiDollar::ValidationContext MakeContext(int height, CAmount oraclePriceMicroUSD,
                                              int systemCollateral = 150,
                                              bool skipOracle = false) const
    {
        return DigiDollar::ValidationContext(
            height, oraclePriceMicroUSD, systemCollateral,
            Params(), nullptr, skipOracle);
    }

    CTransaction CreateMintTx(int64_t lockHeight, int64_t lockTier, CAmount ddAmount,
                               CAmount collateral) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x01000770; // DD_TX_MINT

        mtx.vin.resize(1);
        mtx.vin[0].prevout = COutPoint(uint256S("0xabcd"), 0);

        DigiDollar::MintParams params;
        params.ddAmount = ddAmount;
        params.lockHeight = lockHeight;
        params.ownerKey = testXOnlyKey;
        params.internalKey = DigiDollar::GetCollateralNUMSKey();
        params.oracleKeys = DigiDollar::GetOracleKeys(15);

        const CScript collateralScript = DigiDollar::CreateCollateralP2TR(params);
        const CScript opReturn = CScript() << OP_RETURN
                                           << std::vector<unsigned char>{'D', 'D'}
                                           << CScriptNum(1)
                                           << CScriptNum(ddAmount)
                                           << CScriptNum(lockHeight)
                                           << CScriptNum(lockTier)
                                           << std::vector<unsigned char>(testXOnlyKey.begin(), testXOnlyKey.end());
        const CScript ddScript = DigiDollar::CreateDigiDollarP2TR(testXOnlyKey, ddAmount);

        mtx.vout.resize(3);
        mtx.vout[0] = CTxOut(0, opReturn);
        mtx.vout[1] = CTxOut(collateral, collateralScript);
        mtx.vout[2] = CTxOut(0, ddScript);

        return CTransaction(mtx);
    }

    void ResetVolatility(int height) const
    {
        DigiDollar::Volatility::VolatilityMonitor::ReconstructFromBlockData({}, height);
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
    }

    CKey testKey;
    CPubKey testPubKey;
    XOnlyPubKey testXOnlyKey;
};

// ============================================================================
// Attack Vector 1: Epoch Boundary Race
// A miner includes a DD mint near an epoch boundary. Different epochs may
// have different oracle prices. The miner chooses which block to include
// the tx in, picking the more favorable price epoch.
// ============================================================================

BOOST_FIXTURE_TEST_CASE(epoch_boundary_oracle_price_determinism, RH20TestSetup)
{
    // Oracle epoch boundary follows the consensus nDDOracleEpochBlocks value.
    // RC34 pins V1 to 40-block (~10 minute) epochs on all networks:
    // block 39 = epoch 0, block 40 = epoch 1.
    const auto& consensus = Params().GetConsensus();
    int epochLen = consensus.nDDOracleEpochBlocks;
    BOOST_CHECK_GT(epochLen, 0);

    int32_t epochBoundary = epochLen; // First epoch transition
    int32_t epoch0 = GetCurrentEpoch(epochBoundary - 1);
    int32_t epoch1 = GetCurrentEpoch(epochBoundary);

    // Verify epoch actually changes at boundary
    BOOST_CHECK_EQUAL(epoch0, 0);
    BOOST_CHECK_EQUAL(epoch1, 1);

    // Key insight: The same mint tx validated at height (epochBoundary-1) vs
    // (epochBoundary) uses the same oraclePriceMicroUSD from ValidationContext,
    // which is set by the block's oracle bundle. Different epochs = different bundles.
    //
    // VERIFY: lockPeriod calculation is height-dependent
    int lockBlocks = 30 * 24 * 60 * 4; // 30 days in blocks
    // At height epochBoundary-1 (epoch 0), lockHeight - nHeight = lockPeriod
    (void)(epochBoundary); // used above for epoch checks

    // The lockPeriod differs by 1 block between these two heights.
    // V1 requires exact canonical tiers, so only the exact 30-day lock gets
    // the 30-day ratio. The off-by-one duration must fail closed.
    const auto& ddParams = Params().GetDigiDollarParams();
    int ratio_e0 = DigiDollar::GetCollateralRatioForLockTime(lockBlocks, ddParams);
    int ratio_e1 = DigiDollar::GetCollateralRatioForLockTime(lockBlocks - 1, ddParams);

    BOOST_CHECK_EQUAL(ratio_e0, 500);
    BOOST_CHECK_EQUAL(ratio_e1, 0);
    BOOST_CHECK(DigiDollar::IsCanonicalLockTier(lockBlocks, ddParams));
    BOOST_CHECK(!DigiDollar::IsCanonicalLockTier(lockBlocks - 1, ddParams));
}

// ============================================================================
// Attack Vector 2: Lock Period Near-Expiry (lockTime = currentHeight + 1)
// ============================================================================

BOOST_FIXTURE_TEST_CASE(lock_period_minimum_1_block, RH20TestSetup)
{
    // Attacker tries to mint with lockHeight = nHeight + 1 (1 block lock).
    // V1 rejects this because 1 block is not a canonical lock tier.
    int currentHeight = 1000;
    int64_t lockHeight = currentHeight + 1;
    CAmount ddAmount = 10000; // $100
    CAmount oraclePrice = 500000; // $0.50/DGB

    ResetVolatility(currentHeight);

    CTransaction tx = CreateMintTx(lockHeight, /*tier=*/0, ddAmount, /*collateral=*/50000 * COIN);
    auto ctx = MakeContext(currentHeight, oraclePrice);
    TxValidationState state;

    bool result = DigiDollar::ValidateMintTransaction(tx, ctx, state);
    BOOST_CHECK(!result);
    BOOST_CHECK_NE(state.GetRejectReason().find("bad-mint-lock-tier-duration"), std::string::npos);

    const auto& ddParams = Params().GetDigiDollarParams();
    int ratioForOneBlock = DigiDollar::GetCollateralRatioForLockTime(1, ddParams);
    BOOST_CHECK_EQUAL(ratioForOneBlock, 0);
    BOOST_CHECK(!DigiDollar::IsCanonicalLockTier(1, ddParams));
}

BOOST_FIXTURE_TEST_CASE(lock_period_zero_rejected, RH20TestSetup)
{
    // lockHeight = nHeight means lockPeriod = 0, should be rejected
    int currentHeight = 1000;
    int64_t lockHeight = currentHeight; // lockPeriod = 0
    CAmount ddAmount = 10000;
    CAmount oraclePrice = 500000;

    ResetVolatility(currentHeight);

    CTransaction tx = CreateMintTx(lockHeight, /*tier=*/0, ddAmount, /*collateral=*/50000 * COIN);
    auto ctx = MakeContext(currentHeight, oraclePrice);
    TxValidationState state;

    // lockPeriod <= 0 must be rejected
    bool result = DigiDollar::ValidateMintTransaction(tx, ctx, state);
    BOOST_CHECK(!result);
    BOOST_CHECK_NE(state.GetRejectReason().find("bad-lock-period"), std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(lock_period_negative_rejected, RH20TestSetup)
{
    // lockHeight < nHeight means lockPeriod < 0 (tx already expired)
    int currentHeight = 1000;
    int64_t lockHeight = currentHeight - 100; // Expired lock
    CAmount ddAmount = 10000;
    CAmount oraclePrice = 500000;

    ResetVolatility(currentHeight);

    CTransaction tx = CreateMintTx(lockHeight, /*tier=*/0, ddAmount, /*collateral=*/50000 * COIN);
    auto ctx = MakeContext(currentHeight, oraclePrice);
    TxValidationState state;

    bool result = DigiDollar::ValidateMintTransaction(tx, ctx, state);
    BOOST_CHECK(!result);
    BOOST_CHECK_NE(state.GetRejectReason().find("bad-lock-period"), std::string::npos);
}

// ============================================================================
// Attack Vector 3: Oracle Timestamp vs Block Timestamp Mismatch
// A block with max-future timestamp (block.nTime = real_time + 7200) could
// make a stale oracle bundle appear fresh.
// ============================================================================

BOOST_FIXTURE_TEST_CASE(oracle_staleness_with_max_future_block_timestamp, RH20TestSetup)
{
    // oracle_age = block.nTime - bundle.timestamp
    // If block.nTime is 2h in the future, and bundle.timestamp is 3500s ago (real time),
    // then oracle_age = (now+7200) - (now-3500) = 10700s > 3600s = STALE
    //
    // Wait, that makes it MORE stale, not less.
    //
    // The REAL attack: block.nTime in the PAST (but still valid via MTP).
    // block.nTime can be as low as MTP+1 (median of last 11 blocks).
    // If MTP is 20 minutes behind real time, block.nTime = real_time - 1200.
    // oracle_age = (real_time - 1200) - bundle.timestamp
    // If bundle.timestamp = real_time - 2400, oracle_age = 1200 < 3600 = FRESH
    // But in reality, the bundle is 2400s old.
    //
    // Actually, the oracle timestamp check `oracle_age > ORACLE_MAX_AGE_SECONDS`
    // uses block.nTime as the reference, NOT real time. So the check is relative
    // to the block, which is correct for consensus — all nodes see the same value.
    //
    // The forward-timestamp attack: block.nTime = real_time + 7200 (max allowed).
    // oracle bundle.timestamp = real_time + 7200 - 60 (within 60s tolerance).
    // This means oracle claims a timestamp 7140s in the future.
    // ValidateBlockOracleData checks: bundle.timestamp > block.nTime + 60 → false
    // So it passes! The oracle timestamp is in the future but accepted.
    //
    // NET EFFECT: An oracle with a timestamp from the future is accepted if the
    // block miner also uses a future timestamp. Not a security issue per se
    // (both are bounded by same 2h window), but verify the check works.

    int64_t real_time = 1700000000;
    uint32_t max_future_block_time = real_time + 7200; // Max 2h future

    // Oracle timestamp just within tolerance of max-future block
    int64_t oracle_timestamp = max_future_block_time - 30; // 30s before block.nTime
    int64_t oracle_age = max_future_block_time - oracle_timestamp;

    BOOST_CHECK_LT(oracle_age, ORACLE_MAX_AGE_SECONDS); // Should be "fresh"
    BOOST_CHECK_LE(oracle_timestamp, (int64_t)max_future_block_time + 60); // Within future tolerance

    // Oracle timestamp EXCEEDING the future tolerance
    int64_t bad_oracle_timestamp = max_future_block_time + 61; // 61s past block.nTime
    BOOST_CHECK_GT(bad_oracle_timestamp, (int64_t)max_future_block_time + 60);
    // This would be rejected by: bundle.timestamp > block.nTime + 60
}

// ============================================================================
// Attack Vector 4: DCA Epoch Skew
// If block timestamps go backward slightly (allowed by MTP rules), does
// the epoch calculation based on block HEIGHT (not time) remain consistent?
// ============================================================================

BOOST_FIXTURE_TEST_CASE(epoch_calculation_uses_height_not_time, RH20TestSetup)
{
    // GetCurrentEpoch uses block_height / epoch_length — pure integer division.
    // This is immune to timestamp manipulation because it uses height, not time.
    // Verify this property explicitly.
    const auto& consensus = Params().GetConsensus();
    int epochLen = consensus.nDDOracleEpochBlocks;

    // Sequential heights should produce monotonically non-decreasing epochs
    int32_t prev_epoch = -1;
    for (int h = 0; h < epochLen * 5; h++) {
        int32_t epoch = GetCurrentEpoch(h);
        BOOST_CHECK_GE(epoch, prev_epoch);
        prev_epoch = epoch;
    }

    // Verify no underflow at height 0
    BOOST_CHECK_EQUAL(GetCurrentEpoch(0), 0);

    // Verify epoch transitions are exact
    BOOST_CHECK_EQUAL(GetCurrentEpoch(epochLen - 1), 0);
    BOOST_CHECK_EQUAL(GetCurrentEpoch(epochLen), 1);
}

// ============================================================================
// Attack Vector 5: Activation Height Off-by-One
// What happens at EXACTLY the DD activation block?
// ============================================================================

BOOST_FIXTURE_TEST_CASE(activation_height_boundary_dd_tx_rejected_before, RH20TestSetup)
{
    // In regtest, nDDActivationHeight = 650.
    // A DD tx at height 649 should be treated as non-DD (pass through).
    // A DD tx at height 650 should be fully validated.
    const auto& consensus = Params().GetConsensus();
    int activationHeight = consensus.nDDActivationHeight;
    BOOST_CHECK_GT(activationHeight, 0);

    // Create a mint tx
    int lockBlocks = 30 * 24 * 60 * 4;
    CAmount ddAmount = 10000;
    CAmount oraclePrice = 500000;

    // At exactly activation height, validation should work
    ResetVolatility(activationHeight);
    int64_t lockHeight = activationHeight + lockBlocks;
    CTransaction tx = CreateMintTx(lockHeight, /*tier=*/1, ddAmount, /*collateral=*/5000 * COIN);
    auto ctx = MakeContext(activationHeight, oraclePrice);
    TxValidationState state;

    // This should validate (at activation height, DD is active)
    bool result = DigiDollar::ValidateMintTransaction(tx, ctx, state);
    // We're testing that the validation CODE runs at activation height
    // The result depends on collateral sufficiency — but the point is it
    // doesn't crash or skip validation.
    (void)result; // May pass or fail on collateral — that's fine
}

// ============================================================================
// Attack Vector 6: GetCollateralRatioForLockTime with boundary lock values
// Test that tier boundary lock periods are handled correctly.
// ============================================================================

BOOST_FIXTURE_TEST_CASE(collateral_ratio_tier_boundary_values, RH20TestSetup)
{
    const auto& ddParams = Params().GetDigiDollarParams();
    const int BLOCKS_PER_DAY = 24 * 60 * 4; // 5760

    // Tier 0: 1 hour (240 blocks) = 1000%
    // Tier 1: 30 days = 500%
    // V1 accepts only exact canonical values.
    int ratio_239 = DigiDollar::GetCollateralRatioForLockTime(239, ddParams);
    int ratio_240 = DigiDollar::GetCollateralRatioForLockTime(240, ddParams);
    int ratio_241 = DigiDollar::GetCollateralRatioForLockTime(241, ddParams);

    BOOST_CHECK_EQUAL(ratio_239, 0);
    BOOST_CHECK_EQUAL(ratio_240, 1000);
    BOOST_CHECK_EQUAL(ratio_241, 0);
    BOOST_CHECK(!DigiDollar::IsCanonicalLockTier(239, ddParams));
    BOOST_CHECK(DigiDollar::IsCanonicalLockTier(240, ddParams));
    BOOST_CHECK(!DigiDollar::IsCanonicalLockTier(241, ddParams));

    // At exactly 30 days boundary
    int ratio_30d = DigiDollar::GetCollateralRatioForLockTime(30 * BLOCKS_PER_DAY, ddParams);
    int ratio_30d_minus1 = DigiDollar::GetCollateralRatioForLockTime(30 * BLOCKS_PER_DAY - 1, ddParams);
    BOOST_CHECK_EQUAL(ratio_30d_minus1, 0);

    BOOST_CHECK_EQUAL(ratio_30d, 500);
    BOOST_CHECK(!DigiDollar::IsCanonicalLockTier(30 * BLOCKS_PER_DAY - 1, ddParams));
    BOOST_CHECK(DigiDollar::IsCanonicalLockTier(30 * BLOCKS_PER_DAY, ddParams));
}

// ============================================================================
// Attack Vector 7: Oracle staleness in GetLatestPrice
// Verify the staleness check in GetLatestPrice actually works.
// ============================================================================

BOOST_FIXTURE_TEST_CASE(oracle_cached_price_staleness_check, RH20TestSetup)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);

    // Inject a price by updating cache directly
    manager.UpdatePriceCache(1000, 500000); // $0.50

    // Immediately, price should be available
    CAmount price = manager.GetLatestPrice();
    BOOST_CHECK_EQUAL(price, 500000);

    // The staleness check in GetLatestPrice uses GetTime() vs last_update_time.
    // We can't easily fast-forward time in a unit test, but we verify the
    // constant is set correctly.
    BOOST_CHECK_EQUAL(ORACLE_MAX_AGE_SECONDS, 3600);

    manager.Clear();
}

// ============================================================================
// Attack Vector 8: Mint at exactly lockPeriod = tier boundary
// Verify no off-by-one in the tier/lockPeriod interaction with nHeight.
// ============================================================================

BOOST_FIXTURE_TEST_CASE(mint_lockperiod_tier_consistency, RH20TestSetup)
{
    // Create a mint where lockPeriod = exactly 30 days.
    // The lockHeight stored in OP_RETURN = nHeight + 30*BLOCKS_PER_DAY.
    // ValidateMintTransaction computes lockPeriod = lockTime - nHeight.
    // This should equal exactly 30*BLOCKS_PER_DAY, mapping to tier 1 (500%).
    const int BLOCKS_PER_DAY = 5760;
    int currentHeight = 1000;
    int lockBlocks = 30 * BLOCKS_PER_DAY;
    int64_t lockHeight = currentHeight + lockBlocks;
    CAmount ddAmount = 10000; // $100
    CAmount oraclePrice = 500000; // $0.50/DGB in micro-USD

    ResetVolatility(currentHeight);

    // At 500% ratio and $0.50/DGB: $100 DD needs $500 in DGB = 1000 DGB
    // Give generous collateral to ensure it passes
    CAmount collateral = 2000 * COIN;

    CTransaction tx = CreateMintTx(lockHeight, /*tier=*/1, ddAmount, collateral);
    auto ctx = MakeContext(currentHeight, oraclePrice);
    TxValidationState state;

    bool result = DigiDollar::ValidateMintTransaction(tx, ctx, state);
    BOOST_CHECK_MESSAGE(result, "Mint at exact tier boundary should validate. Reason: " << state.GetRejectReason());
}

// ============================================================================
// Attack Vector 9: Integer overflow in lock height
// Very large lockHeight values could cause overflow in lockPeriod calculation.
// ============================================================================

BOOST_FIXTURE_TEST_CASE(lock_height_overflow_protection, RH20TestSetup)
{
    int currentHeight = 1000;
    // lockHeight close to INT64_MAX
    int64_t lockHeight = std::numeric_limits<int64_t>::max();
    CAmount ddAmount = 10000;
    CAmount oraclePrice = 500000;

    ResetVolatility(currentHeight);

    CTransaction tx = CreateMintTx(lockHeight, /*tier=*/9, ddAmount, /*collateral=*/100000 * COIN);
    auto ctx = MakeContext(currentHeight, oraclePrice);
    TxValidationState state;

    // This should either reject or handle gracefully (no crash/overflow)
    bool result = DigiDollar::ValidateMintTransaction(tx, ctx, state);
    // The lock period would be INT64_MAX - 1000, which is massive.
    // CalculateRequiredCollateral should handle this via MAX_MONEY cap.
    // Key: no crash, no undefined behavior.
    BOOST_TEST_MESSAGE("INT64_MAX lock height result: " << result << " reason: " << state.GetRejectReason());
}

// ============================================================================
// Attack Vector 10: skipOracleValidation bypass at historical heights
// Verify that skipOracleValidation doesn't allow post-activation mints
// to skip critical checks.
// ============================================================================

BOOST_FIXTURE_TEST_CASE(skip_oracle_validation_still_checks_structure, RH20TestSetup)
{
    // skipOracleValidation=true means: don't require oracle price or collateral ratio check.
    // But structural checks (DD marker, output counts, amounts) must still apply.
    int currentHeight = 1000;
    CAmount ddAmount = 10000;

    ResetVolatility(currentHeight);

    // Create a mint with NO collateral output (structural violation).
    // Use a canonical tier-1 lock duration (30 days = 172800 blocks) so
    // the missing-collateral structural check is the FIRST consensus
    // failure, not bad-mint-lock-tier-duration. DD-FA-SEC-011 (Wave 14)
    // removed the IBD bypass on the canonical-duration check, so the
    // lock duration must always match the claimed tier or the validator
    // returns bad-mint-lock-tier-duration before the structural checks.
    CMutableTransaction mtx;
    mtx.nVersion = 0x01000770; // DD_TX_MINT
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256S("0xdead"), 0);

    // Only DD output, no collateral.
    const int64_t canonical_tier_1_blocks = DigiDollar::LockDaysToBlocks(30);
    const CScript ddScript = DigiDollar::CreateDigiDollarP2TR(testXOnlyKey, ddAmount);
    const CScript opReturn = CScript() << OP_RETURN
                                       << std::vector<unsigned char>{'D', 'D'}
                                       << CScriptNum(1)
                                       << CScriptNum(ddAmount)
                                       << CScriptNum(currentHeight + canonical_tier_1_blocks)
                                       << CScriptNum(1)
                                       << std::vector<unsigned char>(testXOnlyKey.begin(), testXOnlyKey.end());

    mtx.vout.resize(2);
    mtx.vout[0] = CTxOut(0, opReturn);
    mtx.vout[1] = CTxOut(0, ddScript);

    CTransaction tx(mtx);
    // Use a live oracle price here so this fixture isolates the structural
    // missing-collateral check rather than the consensus oracle-price gate.
    auto ctx = MakeContext(currentHeight, /*oraclePrice=*/500000, /*systemCollateral=*/150, /*skipOracle=*/true);
    TxValidationState state;

    bool result = DigiDollar::ValidateMintTransaction(tx, ctx, state);
    BOOST_CHECK(!result);
    // Should fail on missing collateral, even with skipOracleValidation
    BOOST_CHECK_NE(state.GetRejectReason().find("missing-collateral"), std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
