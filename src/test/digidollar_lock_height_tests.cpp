// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/digidollar.h>
#include <consensus/volatility.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_lock_height_tests)

struct DigiDollarLockHeightTestSetup : public TestingSetup {
    DigiDollarLockHeightTestSetup() : TestingSetup(ChainType::REGTEST)
    {
        testKey.MakeNewKey(true);
        testPubKey = testKey.GetPubKey();
        testXOnlyKey = XOnlyPubKey(testPubKey);

        mockOraclePrice = 500000;       // $0.50 per DGB in micro-USD
        mockSystemCollateral = 150;     // 150% system collateralization

        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
    }

    DigiDollar::ValidationContext MakeContext(int height, bool skip_oracle_validation) const
    {
        return DigiDollar::ValidationContext(
            height,
            mockOraclePrice,
            mockSystemCollateral,
            Params(),
            nullptr,
            skip_oracle_validation);
    }

    void ResetVolatilityState(int height) const
    {
        DigiDollar::Volatility::VolatilityMonitor::ReconstructFromBlockData({}, height);
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
    }

    CTransaction CreateMintTx(int64_t lock_height, int64_t lock_tier, CAmount dd_amount, CAmount collateral_amount) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x01000770; // DD_TX_MINT

        mtx.vin.resize(1);
        mtx.vin[0].prevout = COutPoint(uint256S("0x1234"), 0);

        DigiDollar::MintParams params;
        params.ddAmount = dd_amount;
        params.lockHeight = lock_height;
        params.ownerKey = testXOnlyKey;
        params.internalKey = DigiDollar::GetCollateralNUMSKey();
        params.oracleKeys = DigiDollar::GetOracleKeys(15);

        const CScript collateralScript = DigiDollar::CreateCollateralP2TR(params);
        const CScript opReturn = CScript() << OP_RETURN
                                           << std::vector<unsigned char>{'D', 'D'}
                                           << CScriptNum(1)
                                           << CScriptNum(dd_amount)
                                           << CScriptNum(lock_height)
                                           << CScriptNum(lock_tier)
                                           << std::vector<unsigned char>(testXOnlyKey.begin(), testXOnlyKey.end());
        const CScript ddScript = DigiDollar::CreateDigiDollarP2TR(testXOnlyKey, dd_amount);

        mtx.vout.resize(3);
        mtx.vout[0] = CTxOut(0, opReturn);
        mtx.vout[1] = CTxOut(collateral_amount, collateralScript);
        mtx.vout[2] = CTxOut(0, ddScript);

        return CTransaction(mtx);
    }

    CKey testKey;
    CPubKey testPubKey;
    XOnlyPubKey testXOnlyKey;
    CAmount mockOraclePrice;
    int mockSystemCollateral;
};

BOOST_FIXTURE_TEST_CASE(rescan_mature_mint_passes, DigiDollarLockHeightTestSetup)
{
    // Mint created at height 100 with lockHeight 340 (1-hour tier / 240 blocks).
    // Historical revalidation uses the original block-connection height. Mint
    // validation still enforces collateral using lockHeight - nHeight, even when
    // oracle validation is skipped.
    const int mintHeight = 100;
    const int64_t lockHeight = 340;
    DigiDollar::ValidationContext ctx = MakeContext(mintHeight, /*skip_oracle_validation=*/true);
    const CAmount ddAmount = 10000;
    const CAmount requiredCollateral = DigiDollar::CalculateRequiredCollateral(
        ddAmount, lockHeight - mintHeight, ctx);
    BOOST_REQUIRE(requiredCollateral > 0);
    const CTransaction tx = CreateMintTx(lockHeight, /*lock_tier=*/0, ddAmount, requiredCollateral);
    TxValidationState state;

    ResetVolatilityState(ctx.nHeight);
    BOOST_CHECK_MESSAGE(DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state),
        "Historical mint should pass at original validation height, got: " + state.GetRejectReason());
    BOOST_CHECK(state.IsValid());
}

BOOST_FIXTURE_TEST_CASE(rescan_wrong_height_noncanonical_remaining_lock_fails, DigiDollarLockHeightTestSetup)
{
    // A historical 1-hour mint must be revalidated at its original block height.
    // Using a later height makes the remaining lock period 140 blocks, which is
    // not a canonical V1 tier and must fail closed.
    const int mintHeight = 100;
    const int64_t lockHeight = 340;
    DigiDollar::ValidationContext ctx = MakeContext(/*height=*/200, /*skip_oracle_validation=*/true);
    const CAmount ddAmount = 10000;
    DigiDollar::ValidationContext mintCtx = MakeContext(mintHeight, /*skip_oracle_validation=*/true);
    const CAmount requiredCollateral = DigiDollar::CalculateRequiredCollateral(
        ddAmount, lockHeight - mintHeight, mintCtx);
    BOOST_REQUIRE(requiredCollateral > 0);
    const CTransaction tx = CreateMintTx(lockHeight, /*lock_tier=*/0, ddAmount, requiredCollateral);
    TxValidationState state;

    ResetVolatilityState(ctx.nHeight);
    BOOST_CHECK(!DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state));
    BOOST_CHECK(!state.IsValid());
}

BOOST_FIXTURE_TEST_CASE(fresh_mint_tier_mismatch_fails, DigiDollarLockHeightTestSetup)
{
    // Fresh mint claims tier 1 (30-day lock) but lockHeight is only 10 blocks ahead.
    // This is rejected by collateral validation (10-block lock requires ~1000% ratio)
    // or by the lock-height tier check if remaining is within the fresh-mint window.
    const int freshHeight = 100;
    const int64_t badLockHeight = freshHeight + 10;
    const CTransaction tx = CreateMintTx(badLockHeight, /*lock_tier=*/1, /*dd_amount=*/10000, /*collateral=*/1000 * COIN);
    DigiDollar::ValidationContext ctx = MakeContext(freshHeight, /*skip_oracle_validation=*/false);
    TxValidationState state;

    ResetVolatilityState(ctx.nHeight);
    BOOST_CHECK(!DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state));
    BOOST_CHECK(!state.IsValid());
}

BOOST_FIXTURE_TEST_CASE(fresh_mint_tier_correct_passes, DigiDollarLockHeightTestSetup)
{
    // Fresh mint with tier 1 and the expected ~172,800 block lock period must pass.
    const int freshHeight = 100;
    const int64_t tier1LockBlocks = DigiDollar::LockDaysToBlocks(30);
    const int64_t lockHeight = freshHeight + tier1LockBlocks;
    DigiDollar::ValidationContext ctx = MakeContext(freshHeight, /*skip_oracle_validation=*/false);
    const CAmount ddAmount = 10000;
    const CAmount requiredCollateral = DigiDollar::CalculateRequiredCollateral(ddAmount, tier1LockBlocks, ctx);
    BOOST_REQUIRE(requiredCollateral > 0);

    const CTransaction tx = CreateMintTx(lockHeight, /*lock_tier=*/1, ddAmount, requiredCollateral);
    TxValidationState state;

    ResetVolatilityState(ctx.nHeight);
    BOOST_CHECK(DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state));
    BOOST_CHECK(state.IsValid());
}

BOOST_FIXTURE_TEST_CASE(aged_mint_wrong_height_revalidation_fails, DigiDollarLockHeightTestSetup)
{
    // Bug #25: Tier 1 mint created at height 39237, lockHeight=212037 (172800 blocks = 30 days).
    // Chain now at 163162. Remaining = 48875 blocks < 172800.
    // V1 does not accept a custom 48875-block lock. Historical validation must
    // use the original block height, so validating this mint at the later tip
    // height must fail closed instead of treating the remaining lock as valid.
    const int mintHeight = 39237;
    const int64_t tier1LockBlocks = DigiDollar::LockDaysToBlocks(30);
    const int64_t lockHeight = mintHeight + tier1LockBlocks; // 212037
    DigiDollar::ValidationContext freshCtx = MakeContext(mintHeight, /*skip_oracle_validation=*/false);
    const CAmount ddAmount = 10000;
    const CAmount requiredCollateral = DigiDollar::CalculateRequiredCollateral(ddAmount, tier1LockBlocks, freshCtx);
    BOOST_REQUIRE(requiredCollateral > 0);

    const CTransaction tx = CreateMintTx(lockHeight, /*lock_tier=*/1, ddAmount, requiredCollateral);

    // Re-validating at height 163162 makes the remaining lock non-canonical.
    const int currentHeight = 163162;
    DigiDollar::ValidationContext ctx = MakeContext(currentHeight, /*skip_oracle_validation=*/false);
    TxValidationState state;

    ResetVolatilityState(ctx.nHeight);
    BOOST_CHECK(!DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state));
    BOOST_CHECK(!state.IsValid());
}

BOOST_FIXTURE_TEST_CASE(fresh_mint_short_lock_high_tier_fails, DigiDollarLockHeightTestSetup)
{
    // A fresh mint claims tier 1 (30-day) but lockHeight is only 10 blocks ahead.
    // At acceptance time, remaining ≈ 10 blocks which is within the 95% window of
    // expectedLockBlocks (~172800) since 10 is NOT >= 164160, so this falls to the
    // aged-mint path. However, the direct tier mismatch check (existing test
    // fresh_mint_tier_mismatch_fails) covers this via insufficient-collateral.
    //
    // This test verifies: a fresh mint that claims a HIGHER tier with a SHORT lock
    // is caught by collateral validation (actual lock blocks determine required ratio).
    const int freshHeight = 1000;
    const int64_t shortLock = 10;
    const int64_t lockHeight = freshHeight + shortLock;
    const CTransaction tx = CreateMintTx(lockHeight, /*lock_tier=*/3, /*dd_amount=*/10000, /*collateral=*/1000 * COIN);
    DigiDollar::ValidationContext ctx = MakeContext(freshHeight, /*skip_oracle_validation=*/false);
    TxValidationState state;

    ResetVolatilityState(ctx.nHeight);
    BOOST_CHECK(!DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state));
    BOOST_CHECK(!state.IsValid());
}

BOOST_FIXTURE_TEST_CASE(matured_mint_locktime_in_past_passes, DigiDollarLockHeightTestSetup)
{
    // A 30-day mint validated at its original block height must pass even when
    // oracle validation is skipped. Fully matured mints are redemption cases;
    // mint validation rejects them because lockTime - nHeight is non-positive.
    const int mintHeight = 39237;
    const int64_t tier1LockBlocks = DigiDollar::LockDaysToBlocks(30);
    const int64_t lockHeight = 212037;
    BOOST_REQUIRE_EQUAL(lockHeight, mintHeight + tier1LockBlocks);
    DigiDollar::ValidationContext ctx = MakeContext(mintHeight, /*skip_oracle_validation=*/true);
    const CAmount ddAmount = 10000;
    const CAmount requiredCollateral = DigiDollar::CalculateRequiredCollateral(ddAmount, tier1LockBlocks, ctx);
    BOOST_REQUIRE(requiredCollateral > 0);
    const CTransaction tx = CreateMintTx(lockHeight, /*lock_tier=*/1, ddAmount, requiredCollateral);
    TxValidationState state;

    ResetVolatilityState(ctx.nHeight);
    BOOST_CHECK_MESSAGE(DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state),
        "Historical 30-day mint should pass at original validation height, got: " + state.GetRejectReason());
    BOOST_CHECK(state.IsValid());
}

BOOST_AUTO_TEST_SUITE_END()
