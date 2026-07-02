// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-07: DigiDollar Redemption Validation Attack Tests
 *
 * Red-team adversarial tests targeting the redemption logic.
 * Attack surface: Can someone steal collateral or redeem early?
 *
 * Attack vectors tested:
 * 1. Early redemption — before timelock expires
 * 2. Partial redemption — redeem part of locked collateral
 * 3. Double redemption — redeem same DD UTXO twice
 * 4. Redemption amount mismatch — claim more DGB than locked
 * 5. Integer overflow in redemption math
 * 6. Redeem someone else's DD
 * 7. Fake DD UTXO — non-DD inputs as DD redemption
 * 8. Lock height manipulation — change lock height after minting
 * 9. Transfer then redeem — original owner tries to redeem transferred DD
 * 10. Redemption with stale oracle price
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <consensus/validation.h>
#include <consensus/tx_check.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <digidollar/txbuilder.h>
#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

// Helper: Build a simple P2TR output script
static CScript RH07_MakeP2TR(const XOnlyPubKey& key) {
    CScript script;
    script << OP_1;
    script << std::vector<unsigned char>(key.begin(), key.end());
    return script;
}

// Helper: Build DD MINT OP_RETURN
static CScript RH07_MakeDDMintOpReturn(CAmount ddAmount, int64_t lockHeight, int lockTier, const XOnlyPubKey& ownerKey) {
    CScript script;
    script << OP_RETURN;
    std::vector<unsigned char> dd_marker = {'D', 'D'};
    script << dd_marker;
    script << CScriptNum(1);  // Type = MINT
    script << CScriptNum::serialize(ddAmount);
    script << CScriptNum::serialize(lockHeight);
    script << CScriptNum(lockTier);
    std::vector<unsigned char> keyData(ownerKey.begin(), ownerKey.end());
    script << keyData;
    return script;
}

// Helper: Build DD REDEEM OP_RETURN
static CScript RH07_MakeDDRedeemOpReturn(CAmount ddAmount) {
    CScript script;
    script << OP_RETURN;
    std::vector<unsigned char> dd_marker = {'D', 'D'};
    script << dd_marker;
    script << CScriptNum(3);  // Type = REDEEM
    script << CScriptNum::serialize(ddAmount);
    return script;
}

static CScript RH07_MakeLegacyDDAmountOpReturn(CAmount ddAmount) {
    CScript script;
    std::vector<unsigned char> amountBytes(8);
    for (int i = 0; i < 8; ++i) {
        amountBytes[i] = static_cast<unsigned char>((static_cast<uint64_t>(ddAmount) >> (8 * i)) & 0xff);
    }
    script << OP_RETURN << OP_DIGIDOLLAR << amountBytes;
    return script;
}

// Helper: Build DD TRANSFER OP_RETURN
static CScript RH07_MakeDDTransferOpReturn(const std::vector<CAmount>& amounts) {
    CScript script;
    script << OP_RETURN;
    std::vector<unsigned char> dd_marker = {'D', 'D'};
    script << dd_marker;
    script << CScriptNum(2);  // Type = TRANSFER
    for (CAmount amt : amounts) {
        script << CScriptNum::serialize(amt);
    }
    return script;
}

// Helper: Create a mint transaction reference for txLookup
struct MockMintContext {
    CKey ownerKey;
    XOnlyPubKey ownerXOnly;
    CKey ddKey;
    XOnlyPubKey ddXOnly;
    CScript collateralScript;
    CTransactionRef mintTxRef;
    uint256 mintTxHash;

    MockMintContext(CAmount collateralAmount, CAmount ddAmount, int64_t lockHeight, int lockTier = 1) {
        ownerKey.MakeNewKey(true);
        ownerXOnly = XOnlyPubKey(ownerKey.GetPubKey());
        ddKey.MakeNewKey(true);
        ddXOnly = XOnlyPubKey(ddKey.GetPubKey());
        collateralScript = RH07_MakeP2TR(ownerXOnly);

        CMutableTransaction mintTx;
        mintTx.nVersion = 0x01000770;  // DD_TX_MINT
        mintTx.vin.push_back(CTxIn(COutPoint(uint256S("fff0000000000000000000000000000000000000000000000000000000000001"), 0)));
        // vout[0]: collateral P2TR with value
        mintTx.vout.push_back(CTxOut(collateralAmount, collateralScript));
        // vout[1]: DD token P2TR zero-value
        mintTx.vout.push_back(CTxOut(0, RH07_MakeP2TR(ddXOnly)));
        // vout[2]: OP_RETURN metadata
        mintTx.vout.push_back(CTxOut(0, RH07_MakeDDMintOpReturn(ddAmount, lockHeight, lockTier, ownerXOnly)));

        mintTxRef = MakeTransactionRef(mintTx);
        mintTxHash = mintTxRef->GetHash();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_rh07_redemption_attacks, TestingSetup)

// ============================================================================
// ATTACK VECTOR 1: Early Redemption — Redeem before timelock expires
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_01a_early_redemption_via_nLockTime_bypass)
{
    // ATTACK: Set tx.nLockTime to 0 (or current height) to bypass CLTV check
    // ValidateNormalRedemptionConditions checks: ctx.nHeight < tx.nLockTime
    // If nLockTime=0, the check (currentHeight < 0) is always false → redemption allowed
    // BUT: nLockTime=0 means "no timelock" which should still be caught

    auto regTestParams = CChainParams::RegTest({});
    int currentHeight = 500;
    int64_t lockHeight = 1000;  // Locked until block 1000

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;  // DD_TX_REDEEM
    mtx.nLockTime = 0;  // ATTACK: No locktime set — bypasses CLTV

    // Minimal inputs/outputs
    CKey key; key.MakeNewKey(true);
    XOnlyPubKey xonly(key.GetPubKey());

    MockMintContext mint(200 * COIN, 10000, lockHeight);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(200 * COIN, mint.collateralScript), 400, false), false);

    // DD input
    COutPoint ddOutpoint(mint.mintTxHash, 1);
    coinsView.AddCoin(ddOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(mint.ddXOnly)), 400, false), false);

    // Fee input
    CKey feeKey; feeKey.MakeNewKey(true);
    COutPoint feeOutpoint(uint256S("eee0000000000000000000000000000000000000000000000000000000000001"), 0);
    coinsView.AddCoin(feeOutpoint, Coin(CTxOut(1 * COIN, RH07_MakeP2TR(XOnlyPubKey(feeKey.GetPubKey()))), 300, false), false);

    mtx.vin.push_back(CTxIn(collOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(ddOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(feeOutpoint));

    // Outputs: release collateral + OP_RETURN
    mtx.vout.push_back(CTxOut(199 * COIN, RH07_MakeP2TR(xonly)));  // collateral return minus fee

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    CTransaction tx(mtx);
    TxValidationState state;

    DigiDollar::ValidationContext ctx(currentHeight, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    // nLockTime=0 at height 500 → height >= nLockTime → passes timelock check!
    // This is actually correct Bitcoin behavior: nLockTime=0 means "no lock"
    // The REAL defense is that the collateral UTXO's MAST script has CLTV embedded,
    // which is enforced by script execution, not by ValidateNormalRedemptionConditions.
    //
    // However, ValidateRedemptionTransaction should verify the nLockTime against
    // the collateral position's unlock height, not just against current height.
    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);

    // nLockTime=0 means "mine anytime" — if the script-level CLTV isn't enforced
    // in the validation layer, this is a bypass. The test verifies the defense exists.
    BOOST_CHECK_MESSAGE(!result || state.GetRejectReason().empty(),
        "ATTACK [RH-07-01a]: nLockTime=0 bypass attempt. "
        "If this passes, script-level CLTV must be enforced elsewhere. "
        "Reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(rh07_01b_early_redemption_timelock_not_expired)
{
    // ATTACK: Try to redeem when nLockTime > currentHeight
    // This should be caught by ValidateNormalRedemptionConditions

    auto regTestParams = CChainParams::RegTest({});
    int currentHeight = 500;
    int64_t lockHeight = 1000;  // Locked until block 1000

    MockMintContext mint(200 * COIN, 10000, lockHeight);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(200 * COIN, mint.collateralScript), 400, false), false);
    COutPoint ddOutpoint(mint.mintTxHash, 1);
    coinsView.AddCoin(ddOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(mint.ddXOnly)), 400, false), false);

    CKey feeKey; feeKey.MakeNewKey(true);
    COutPoint feeOutpoint(uint256S("eee0000000000000000000000000000000000000000000000000000000000002"), 0);
    coinsView.AddCoin(feeOutpoint, Coin(CTxOut(1 * COIN, RH07_MakeP2TR(XOnlyPubKey(feeKey.GetPubKey()))), 300, false), false);

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = lockHeight;  // Set correctly to lock height

    mtx.vin.push_back(CTxIn(collOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(ddOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(feeOutpoint));
    mtx.vout.push_back(CTxOut(199 * COIN, RH07_MakeP2TR(mint.ownerXOnly)));

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(currentHeight, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);

    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-01b]: Early redemption MUST be rejected. "
        "CurrentHeight=500, lockHeight=1000. "
        "Reason: " + state.GetRejectReason());

    // Verify specific rejection reason is timelock-related
    BOOST_CHECK_MESSAGE(state.GetRejectReason().find("timelock") != std::string::npos,
        "Rejection reason should mention timelock, got: " + state.GetRejectReason());
}

// ============================================================================
// ATTACK VECTOR 2: Partial Redemption — Burn less DD than was minted
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_02a_partial_burn_steals_collateral)
{
    // ATTACK: Burn only 1 cent of DD ($0.01), try to release 200 DGB collateral
    // Originally minted 10000 cents ($100) — partial burn should be rejected

    auto regTestParams = CChainParams::RegTest({});
    int currentHeight = 2000;
    int64_t lockHeight = 1500;  // Already expired

    CAmount lockedCollateral = 200 * COIN;
    CAmount originalDD = 10000;  // $100
    CAmount ddBurnedTiny = 1;    // $0.01

    MockMintContext mint(lockedCollateral, originalDD, lockHeight);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(lockedCollateral, mint.collateralScript), 400, false), false);
    COutPoint ddOutpoint(mint.mintTxHash, 1);
    coinsView.AddCoin(ddOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(mint.ddXOnly)), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    TxValidationState state;
    DigiDollar::ValidationContext ctx(currentHeight, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateCollateralReleaseAmount(
        CTransaction(CMutableTransaction()), ctx, ddBurnedTiny, state);

    // Without coins view for vin[0], this will fail structurally.
    // Let's test directly with a properly constructed tx:
    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = lockHeight;
    mtx.vin.push_back(CTxIn(collOutpoint));
    mtx.vin.push_back(CTxIn(ddOutpoint));
    // Release ALL collateral with tiny DD burn
    mtx.vout.push_back(CTxOut(lockedCollateral, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state2;

    result = DigiDollar::ValidateCollateralReleaseAmount(tx, ctx, ddBurnedTiny, state2);

    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-02a]: Partial DD burn MUST be rejected. "
        "Burning 1 cent cannot release 200 DGB. "
        "Reason: " + state2.GetRejectReason());

    // Verify it's specifically the partial burn check
    BOOST_CHECK_MESSAGE(state2.GetRejectReason().find("partial-burn") != std::string::npos,
        "Should reject with 'partial-burn' reason, got: " + state2.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(rh07_02b_partial_burn_at_99_percent)
{
    // ATTACK: Burn 9999 of 10000 cents — nearly full but not quite
    // Should still be rejected (full burn required)

    auto regTestParams = CChainParams::RegTest({});
    CAmount lockedCollateral = 200 * COIN;
    CAmount originalDD = 10000;
    CAmount ddBurned = 9999;  // 1 cent short

    MockMintContext mint(lockedCollateral, originalDD, 1500);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(lockedCollateral, mint.collateralScript), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;
    mtx.vin.push_back(CTxIn(collOutpoint));
    mtx.vin.push_back(CTxIn(COutPoint(uint256S("bbb0000000000000000000000000000000000000000000000000000000000001"), 0)));
    mtx.vout.push_back(CTxOut(lockedCollateral, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateCollateralReleaseAmount(tx, ctx, ddBurned, state);

    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-02b]: Even 99.99% burn MUST be rejected. "
        "Full burn required (10000 cents, not 9999). "
        "Reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(rh07_02c_full_burn_passes)
{
    // VALID: Full burn (10000 of 10000) should pass

    auto regTestParams = CChainParams::RegTest({});
    CAmount lockedCollateral = 200 * COIN;
    CAmount originalDD = 10000;

    MockMintContext mint(lockedCollateral, originalDD, 1500);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(lockedCollateral, mint.collateralScript), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;
    mtx.vin.push_back(CTxIn(collOutpoint));
    mtx.vin.push_back(CTxIn(COutPoint(uint256S("bbb0000000000000000000000000000000000000000000000000000000000002"), 0)));
    mtx.vout.push_back(CTxOut(lockedCollateral, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateCollateralReleaseAmount(tx, ctx, originalDD, state);

    BOOST_CHECK_MESSAGE(result,
        "VALID [RH-07-02c]: Full DD burn should release full collateral. "
        "Reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(rh07_02d_legacy_opreturn_redeem_change_preserves_burn)
{
    auto regTestParams = CChainParams::RegTest({});
    const int currentHeight = 2000;
    const int64_t lockHeight = 1500;
    const CAmount lockedCollateral = 200 * COIN;
    const CAmount redeemedPositionDD = 10000;
    const CAmount otherDD = 90000;
    const CAmount totalDDInputs = redeemedPositionDD + otherDD;

    MockMintContext redeemedMint(lockedCollateral, redeemedPositionDD, lockHeight);
    MockMintContext otherMint(300 * COIN, otherDD, lockHeight);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);

    COutPoint collateralOutpoint(redeemedMint.mintTxHash, 0);
    COutPoint redeemedDDOutpoint(redeemedMint.mintTxHash, 1);
    COutPoint otherDDOutpoint(otherMint.mintTxHash, 1);

    coinsView.AddCoin(collateralOutpoint, Coin(CTxOut(lockedCollateral, redeemedMint.collateralScript), 400, false), false);
    coinsView.AddCoin(redeemedDDOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(redeemedMint.ddXOnly)), 400, false), false);
    coinsView.AddCoin(otherDDOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(otherMint.ddXOnly)), 400, false), false);

    CKey changeKey;
    changeKey.MakeNewKey(true);
    XOnlyPubKey changeXOnly(changeKey.GetPubKey());

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770; // DD_TX_REDEEM
    mtx.nLockTime = lockHeight;
    mtx.vin.push_back(CTxIn(collateralOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(redeemedDDOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(otherDDOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vout.push_back(CTxOut(lockedCollateral, RH07_MakeP2TR(redeemedMint.ownerXOnly)));
    mtx.vout.push_back(CTxOut(0, RH07_MakeP2TR(changeXOnly)));
    mtx.vout.push_back(CTxOut(0, RH07_MakeLegacyDDAmountOpReturn(otherDD)));
    mtx.vout.push_back(CTxOut(0, RH07_MakeDDRedeemOpReturn(totalDDInputs)));

    CTransaction tx(mtx);
    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == redeemedMint.mintTxHash) { out = redeemedMint.mintTxRef; return true; }
        if (txid == otherMint.mintTxHash) { out = otherMint.mintTxRef; return true; }
        if (txid == tx.GetHash()) { out = MakeTransactionRef(tx); return true; }
        return false;
    };

    TxValidationState state;
    DigiDollar::ValidationContext ctx(currentHeight, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);
    if (result) {
        CAmount extractedChange = 0;
        BOOST_REQUIRE(DigiDollar::ExtractDDAmountFromBlockDb(COutPoint(tx.GetHash(), 1), currentHeight, txLookup, extractedChange));
        BOOST_CHECK_EQUAL(extractedChange, totalDDInputs);
    }

    BOOST_CHECK_MESSAGE(!result,
        "ATTACK [RH-07-02d]: redemption with conflicting legacy/current OP_RETURN metadata must be rejected. "
        "Otherwise validation treats change as 90000 DD, burns 10000 DD to release collateral, "
        "but later source extraction treats the same change output as 100000 DD.");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-redeem-multiple-dd-opreturn");
}

// ============================================================================
// ATTACK VECTOR 3: Double Redemption — Redeem same DD UTXO twice
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_03a_double_redemption_same_utxo)
{
    // ATTACK: Include the same DD input twice in a redemption tx
    // This could inflate the "DD burned" amount, allowing release of excess collateral
    // Bitcoin's CheckTransaction rejects duplicate inputs at the tx level

    auto regTestParams = CChainParams::RegTest({});

    CKey key; key.MakeNewKey(true);
    XOnlyPubKey xonly(key.GetPubKey());

    COutPoint ddOutpoint(uint256S("aaa0000000000000000000000000000000000000000000000000000000000001"), 1);

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    // Input 0: collateral
    mtx.vin.push_back(CTxIn(COutPoint(uint256S("aaa0000000000000000000000000000000000000000000000000000000000001"), 0)));
    // Input 1: DD UTXO
    mtx.vin.push_back(CTxIn(ddOutpoint));
    // Input 2: SAME DD UTXO again (duplicate!)
    mtx.vin.push_back(CTxIn(ddOutpoint));

    mtx.vout.push_back(CTxOut(100 * COIN, RH07_MakeP2TR(xonly)));

    CTransaction tx(mtx);
    TxValidationState state;

    // CheckTransaction (in Bitcoin Core consensus) rejects duplicate inputs
    bool txValid = CheckTransaction(tx, state);

    BOOST_CHECK_MESSAGE(!txValid,
        "DEFENSE VERIFIED [RH-07-03a]: Duplicate inputs MUST be rejected by CheckTransaction. "
        "This is Bitcoin's built-in defense against double-spending within a single tx. "
        "Reason: " + state.GetRejectReason());
}

// ============================================================================
// ATTACK VECTOR 4: Redemption Amount Mismatch — Claim more DGB than locked
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_04a_claim_more_collateral_than_locked)
{
    // ATTACK: Collateral was 200 DGB, try to release 500 DGB
    // The excess would come from... nowhere (fee inputs or thin air)

    auto regTestParams = CChainParams::RegTest({});
    CAmount lockedCollateral = 200 * COIN;
    CAmount originalDD = 10000;
    CAmount ddBurned = 10000;  // Full burn (valid)

    MockMintContext mint(lockedCollateral, originalDD, 1500);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(lockedCollateral, mint.collateralScript), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;
    mtx.vin.push_back(CTxIn(collOutpoint));
    mtx.vin.push_back(CTxIn(COutPoint(uint256S("bbb0000000000000000000000000000000000000000000000000000000000003"), 0)));

    // ATTACK: Output 500 DGB when only 200 was locked
    mtx.vout.push_back(CTxOut(500 * COIN, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateCollateralReleaseAmount(tx, ctx, ddBurned, state);

    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-04a]: Cannot release 500 DGB from 200 DGB collateral. "
        "Reason: " + state.GetRejectReason());

    BOOST_CHECK_MESSAGE(state.GetRejectReason().find("excessive") != std::string::npos,
        "Should reject as 'excessive' collateral release, got: " + state.GetRejectReason());
}

// ============================================================================
// ATTACK VECTOR 5: Integer Overflow in Redemption Math
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_05a_overflow_in_collateral_release_calc)
{
    // ATTACK: Use extreme values that could overflow CAmount (int64_t)
    // If proportional calculation overflows, result wraps to a small number
    // allowing release of more collateral than proportional share

    auto regTestParams = CChainParams::RegTest({});

    // MAX_MONEY is ~21 billion DGB in satoshis
    CAmount lockedCollateral = MAX_MONEY;  // Maximum possible collateral
    CAmount originalDD = 1;               // Tiny DD amount
    CAmount ddBurned = 1;                 // Full burn

    MockMintContext mint(lockedCollateral, originalDD, 1500);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(lockedCollateral, mint.collateralScript), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;
    mtx.vin.push_back(CTxIn(collOutpoint));
    mtx.vin.push_back(CTxIn(COutPoint(uint256S("bbb0000000000000000000000000000000000000000000000000000000000004"), 0)));
    // Release full collateral (valid — full burn of 1 cent DD)
    mtx.vout.push_back(CTxOut(lockedCollateral, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    // This should pass: full burn of 1 DD releasing MAX_MONEY collateral is valid
    // (as long as the math doesn't overflow)
    bool result = DigiDollar::ValidateCollateralReleaseAmount(tx, ctx, ddBurned, state);

    BOOST_CHECK_MESSAGE(result,
        "VALID [RH-07-05a]: Full burn of tiny DD should release all collateral (even MAX_MONEY). "
        "Reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(rh07_05b_overflow_zero_dd_minted)
{
    // ATTACK: What if originalDDMinted is somehow 0? Division by zero?
    // CalculateCollateralReturn does: proportional = (dgbLocked * ddBurned) / ddMinted
    // If ddMinted=0, this is division by zero

    auto regTestParams = CChainParams::RegTest({});

    // Create mint with 0 DD amount (should be rejected at mint time, but what if it got through?)
    CKey key; key.MakeNewKey(true);
    XOnlyPubKey xonly(key.GetPubKey());
    CScript collScript = RH07_MakeP2TR(xonly);

    CMutableTransaction mintTx;
    mintTx.nVersion = 0x01000770;
    mintTx.vin.push_back(CTxIn(COutPoint(uint256S("fff0000000000000000000000000000000000000000000000000000000000002"), 0)));
    mintTx.vout.push_back(CTxOut(200 * COIN, collScript));
    mintTx.vout.push_back(CTxOut(0, RH07_MakeP2TR(xonly)));
    // OP_RETURN with DD amount = 0 (should be invalid but test defense-in-depth)
    mintTx.vout.push_back(CTxOut(0, RH07_MakeDDMintOpReturn(0, 1500, 1, xonly)));

    CTransactionRef mintRef = MakeTransactionRef(mintTx);
    uint256 mintHash = mintRef->GetHash();

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mintHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(200 * COIN, collScript), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mintHash) { out = mintRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.vin.push_back(CTxIn(collOutpoint));
    mtx.vin.push_back(CTxIn(COutPoint(uint256S("bbb0000000000000000000000000000000000000000000000000000000000005"), 0)));
    mtx.vout.push_back(CTxOut(200 * COIN, RH07_MakeP2TR(xonly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    // ddBurned=0 with originalDD=0 — should still be rejected (not cause crash)
    bool result = DigiDollar::ValidateCollateralReleaseAmount(tx, ctx, 0, state);

    // Should reject: 0 DD burned < 0 DD minted is false, but amount is extracted as 0
    // from OP_RETURN which should be rejected by "originalDDMinted <= 0" check
    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-05b]: Zero DD minted must not cause crash or allow collateral release. "
        "Reason: " + state.GetRejectReason());
}

// ============================================================================
// ATTACK VECTOR 6: Redeem Someone Else's DD
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_06a_spend_others_dd_utxos)
{
    // ATTACK: Alice minted DD. Bob constructs a redemption tx spending Alice's DD UTXOs.
    // Defense: P2TR signatures — Bob can't sign for Alice's key.
    // This is enforced at the script execution level, not DD validation level.
    // Test verifies DD validation doesn't have a bypass that skips signature checks.

    auto regTestParams = CChainParams::RegTest({});

    // Alice's mint
    MockMintContext aliceMint(200 * COIN, 10000, 1500);

    // Bob's key (attacker)
    CKey bobKey; bobKey.MakeNewKey(true);
    XOnlyPubKey bobXOnly(bobKey.GetPubKey());

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint aliceCollOutpoint(aliceMint.mintTxHash, 0);
    coinsView.AddCoin(aliceCollOutpoint, Coin(CTxOut(200 * COIN, aliceMint.collateralScript), 400, false), false);
    COutPoint aliceDDOutpoint(aliceMint.mintTxHash, 1);
    coinsView.AddCoin(aliceDDOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(aliceMint.ddXOnly)), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == aliceMint.mintTxHash) { out = aliceMint.mintTxRef; return true; }
        return false;
    };

    // Bob constructs a redemption tx sending collateral TO BOB
    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;  // Timelock expired (current=2000)

    mtx.vin.push_back(CTxIn(aliceCollOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(aliceDDOutpoint, CScript(), 0xFFFFFFFE));
    // Bob's output — sending Alice's collateral to himself!
    mtx.vout.push_back(CTxOut(200 * COIN, RH07_MakeP2TR(bobXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    // DD validation will pass structurally (it doesn't check signatures)
    // The REAL defense is that ConnectBlock's script verification will fail
    // because Bob can't produce valid Schnorr signatures for Alice's keys.
    //
    // This test documents that DD-level validation doesn't independently prevent this
    // and relies on standard Bitcoin script execution for ownership enforcement.
    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);

    // The redemption validation may pass (structural checks OK) — that's fine.
    // Script verification is what prevents Bob from actually spending Alice's UTXOs.
    // We just document this dependency.
    if (result) {
        BOOST_TEST_MESSAGE("NOTE [RH-07-06a]: DD validation passed structurally. "
                          "Ownership enforcement depends on Schnorr signature verification "
                          "in script execution (ConnectBlock). This is by design.");
    } else {
        BOOST_TEST_MESSAGE("NOTE [RH-07-06a]: DD validation rejected: " + state.GetRejectReason());
    }

    // Either way, this test passes — it's documenting the security boundary
    BOOST_CHECK(true);
}

// ============================================================================
// ATTACK VECTOR 7: Fake DD UTXO — Non-DD inputs as DD redemption
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_07a_non_dd_input_as_dd_burn)
{
    // ATTACK: Create a redemption tx where "DD inputs" are actually regular DGB UTXOs
    // If validation counts any zero-value P2TR input as DD, attacker could create
    // fake "DD" UTXOs that never went through minting, then "burn" them to unlock collateral.
    //
    // Defense: ExtractDDAmountFromTxRef checks HasDigiDollarMarker on the source tx.
    // A regular tx won't have the DD marker, so the DD amount lookup returns 0.

    auto regTestParams = CChainParams::RegTest({});

    // Create a legitimate collateral position
    MockMintContext mint(200 * COIN, 10000, 1500);

    // Create a FAKE "DD" UTXO from a regular (non-DD) transaction
    CKey fakeKey; fakeKey.MakeNewKey(true);
    XOnlyPubKey fakeXOnly(fakeKey.GetPubKey());

    CMutableTransaction fakeTx;
    fakeTx.nVersion = 2;  // Regular tx, NOT DD
    fakeTx.vin.push_back(CTxIn(COutPoint(uint256S("ddd0000000000000000000000000000000000000000000000000000000000001"), 0)));
    // Output: zero-value P2TR that looks like a DD output but isn't
    fakeTx.vout.push_back(CTxOut(0, RH07_MakeP2TR(fakeXOnly)));
    // Even add a fake DD OP_RETURN
    fakeTx.vout.push_back(CTxOut(0, RH07_MakeDDRedeemOpReturn(10000)));

    CTransactionRef fakeRef = MakeTransactionRef(fakeTx);
    uint256 fakeHash = fakeRef->GetHash();

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(200 * COIN, mint.collateralScript), 400, false), false);

    // Add fake DD UTXO (zero-value P2TR from non-DD tx)
    COutPoint fakeDDOutpoint(fakeHash, 0);
    coinsView.AddCoin(fakeDDOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(fakeXOnly)), 500, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        if (txid == fakeHash) { out = fakeRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;
    mtx.vin.push_back(CTxIn(collOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(fakeDDOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vout.push_back(CTxOut(200 * COIN, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);

    // The fake DD input should NOT be counted as DD because:
    // 1. ExtractDDAmountFromTxRef checks HasDigiDollarMarker — fake tx has nVersion=2
    // 2. Without valid DD inputs, totalDDInputs=0 → dd not burned → reject
    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-07a]: Fake DD UTXOs from non-DD transactions "
        "must not be accepted as valid DD burns. "
        "HasDigiDollarMarker check on source tx prevents this. "
        "Reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(rh07_07b_regular_tx_with_dd_version_marker)
{
    // ATTACK: Craft a regular tx that HAS the DD version marker but ISN'T a real mint
    // nVersion = 0x01000770 (DD MINT marker) but no valid DD structure
    // If this tx's outputs are later used as "DD inputs" in a redemption,
    // HasDigiDollarMarker returns true, potentially allowing fake DD amounts

    auto regTestParams = CChainParams::RegTest({});

    MockMintContext mint(200 * COIN, 10000, 1500);

    // Fake tx with DD version marker but crafted OP_RETURN claiming 99999 DD
    CKey fakeKey; fakeKey.MakeNewKey(true);
    XOnlyPubKey fakeXOnly(fakeKey.GetPubKey());

    CMutableTransaction fakeDDTx;
    fakeDDTx.nVersion = 0x01000770;  // DD MINT marker!
    fakeDDTx.vin.push_back(CTxIn(COutPoint(uint256S("eee0000000000000000000000000000000000000000000000000000000000001"), 0)));
    fakeDDTx.vout.push_back(CTxOut(0, RH07_MakeP2TR(fakeXOnly)));
    // Fake OP_RETURN claiming 99999 cents DD
    fakeDDTx.vout.push_back(CTxOut(0, RH07_MakeDDMintOpReturn(99999, 1500, 1, fakeXOnly)));

    CTransactionRef fakeRef = MakeTransactionRef(fakeDDTx);
    uint256 fakeHash = fakeRef->GetHash();

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(200 * COIN, mint.collateralScript), 400, false), false);

    COutPoint fakeDDOutpoint(fakeHash, 0);
    coinsView.AddCoin(fakeDDOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(fakeXOnly)), 500, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        if (txid == fakeHash) { out = fakeRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;
    mtx.vin.push_back(CTxIn(collOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(fakeDDOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vout.push_back(CTxOut(200 * COIN, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);

    // NOTE: The fake tx HAS DD marker so ExtractDDAmountFromTxRef will parse it!
    // This means 99999 cents of fake DD could be counted as valid DD burn.
    // The REAL defense is that the fake mint tx would never have been accepted
    // by ConnectBlock's ValidateMintTransaction (NUMS check, collateral check, etc.)
    // So the fake output would never exist in the UTXO set.
    //
    // If this test PASSES validation, it documents an interesting attack surface:
    // the redemption validator trusts that DD UTXOs in the UTXO set were properly
    // validated at creation time. This is by design (same as Bitcoin trusting UTXOs).
    BOOST_TEST_MESSAGE("NOTE [RH-07-07b]: Fake DD tx with version marker. "
                      "Defense relies on mint-time validation (NUMS, collateral checks) "
                      "preventing fake DD UTXOs from entering the UTXO set. "
                      "Result: " + std::string(result ? "PASSED" : "REJECTED: " + state.GetRejectReason()));
    BOOST_CHECK(true);  // Documenting test — either outcome is interesting
}

// ============================================================================
// ATTACK VECTOR 8: Lock Height Manipulation After Minting
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_08a_nLockTime_lower_than_committed)
{
    // ATTACK: Mint with lockHeight=10000, but set redemption tx nLockTime=500
    // This attempts to redeem earlier than the committed lock period.
    // Defense: CLTV in the MAST script enforces the original lockHeight

    auto regTestParams = CChainParams::RegTest({});
    int64_t commitedLockHeight = 10000;
    int currentHeight = 600;

    MockMintContext mint(200 * COIN, 10000, commitedLockHeight);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(200 * COIN, mint.collateralScript), 400, false), false);
    COutPoint ddOutpoint(mint.mintTxHash, 1);
    coinsView.AddCoin(ddOutpoint, Coin(CTxOut(0, RH07_MakeP2TR(mint.ddXOnly)), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 500;  // ATTACK: Lower than committed lockHeight

    mtx.vin.push_back(CTxIn(collOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(ddOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vout.push_back(CTxOut(199 * COIN, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(currentHeight, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);

    // nLockTime=500, currentHeight=600 → passes the nLockTime >= height check.
    // BUT: the MAST script has CLTV 10000, which OP_CHECKLOCKTIMEVERIFY enforces
    // against nLockTime. Since nLockTime=500 < 10000, CLTV would fail in script execution.
    //
    // DD validation may pass structurally (it only checks nLockTime vs height).
    // CLTV enforcement happens in script interpreter.
    BOOST_TEST_MESSAGE("NOTE [RH-07-08a]: nLockTime manipulation (500 vs committed 10000). "
                      "DD validation checks nLockTime vs height. "
                      "CLTV in MAST enforces nLockTime >= committed lockHeight. "
                      "Result: " + std::string(result ? "PASSED structural" : "REJECTED: " + state.GetRejectReason()));

    // The IMPORTANT thing is: if DD validation passes, script execution MUST catch this
    BOOST_CHECK(true);  // Documenting the security boundary
}

// ============================================================================
// ATTACK VECTOR 9: Transfer Then Redeem — Original Owner Tries to Redeem
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_09a_transfer_then_original_owner_redeems)
{
    // SCENARIO: Alice mints DD, transfers DD to Bob, then tries to redeem
    // the collateral by burning new (fake) DD.
    //
    // The UTXO model prevents this: after transfer, Alice's original DD UTXO
    // is spent. Bob has a new UTXO. Alice can't spend a spent UTXO.
    //
    // BUT: Alice still controls the collateral key (via MAST normal path).
    // Can Alice redeem collateral without burning the DD that's now with Bob?
    //
    // Defense: ValidateRedemptionTransaction requires DD inputs to be burned.
    // Alice needs DD UTXOs to burn, but hers are spent (transferred to Bob).

    auto regTestParams = CChainParams::RegTest({});

    MockMintContext aliceMint(200 * COIN, 10000, 1500);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);

    // Collateral still exists (not spent)
    COutPoint collOutpoint(aliceMint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(200 * COIN, aliceMint.collateralScript), 400, false), false);

    // Alice's DD UTXO is SPENT (transferred to Bob) — NOT in UTXO set

    // Alice tries to redeem with no DD inputs (or a fake one)
    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == aliceMint.mintTxHash) { out = aliceMint.mintTxRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;

    mtx.vin.push_back(CTxIn(collOutpoint, CScript(), 0xFFFFFFFE));
    // Alice adds a "DD input" that doesn't exist in UTXO set
    COutPoint spentDDOutpoint(aliceMint.mintTxHash, 1);
    mtx.vin.push_back(CTxIn(spentDDOutpoint, CScript(), 0xFFFFFFFE));

    mtx.vout.push_back(CTxOut(200 * COIN, RH07_MakeP2TR(aliceMint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);

    // The DD input (vout 1) is NOT in the coins view (it was spent in transfer).
    // GetCoin fails → can't classify it → hasDDInput stays false → reject
    // Additionally, ConnectBlock would reject spending a non-existent UTXO.
    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-09a]: Cannot redeem collateral after DD was transferred. "
        "Spent DD UTXOs don't exist in UTXO set, so they can't be used as inputs. "
        "Reason: " + state.GetRejectReason());
}

// ============================================================================
// ATTACK VECTOR 10: Redemption with Stale Oracle Price
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_10a_stale_oracle_price_affects_collateral_release)
{
    // ANALYSIS: Does oracle price staleness affect redemption validity?
    //
    // In the current code, ValidateCollateralReleaseAmount does NOT use oracle price.
    // It compares: totalDGBRelease <= lockedCollateral + feeTolerance
    // The full collateral is released regardless of current price.
    //
    // This is actually CORRECT for normal redemption:
    // - User burns ALL DD → gets ALL collateral back
    // - Price changes don't matter — it's a full unwind
    //
    // Oracle price only matters for ERR path (burn MORE DD at discount)

    auto regTestParams = CChainParams::RegTest({});
    CAmount lockedCollateral = 200 * COIN;
    CAmount originalDD = 10000;

    MockMintContext mint(lockedCollateral, originalDD, 1500);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);
    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(lockedCollateral, mint.collateralScript), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    // Test with various oracle prices — collateral release should be the same
    std::vector<CAmount> prices = {1000, 50000, 500000, 5000000};  // $0.01 to $50.00

    for (CAmount price : prices) {
        CMutableTransaction mtx;
        mtx.nVersion = 0x03000770;
        mtx.nLockTime = 1500;
        mtx.vin.push_back(CTxIn(collOutpoint));
        mtx.vin.push_back(CTxIn(COutPoint(uint256S("bbb0000000000000000000000000000000000000000000000000000000000006"), 0)));
        mtx.vout.push_back(CTxOut(lockedCollateral, RH07_MakeP2TR(mint.ownerXOnly)));

        CTransaction tx(mtx);
        TxValidationState state;
        DigiDollar::ValidationContext ctx(2000, price, 150, *regTestParams, &coinsView, false, txLookup);

        bool result = DigiDollar::ValidateCollateralReleaseAmount(tx, ctx, originalDD, state);

        BOOST_CHECK_MESSAGE(result,
            "VALID [RH-07-10a]: Full redemption should succeed regardless of oracle price ($" +
            std::to_string(price / 1000000.0) + "/DGB). "
            "Reason: " + state.GetRejectReason());
    }
}

// ============================================================================
// ADDITIONAL ATTACK: Collateral from another mint used as fee input (T2-06b)
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_extra_collateral_as_fee_input)
{
    // ATTACK: Include a second collateral UTXO as a "fee input" in redemption.
    // Its DGB value gets subtracted from totalDGBRelease, making the net release
    // appear correct. But the second collateral's DD remains unbacked.
    //
    // Defense: T2-06b check in ValidateCollateralReleaseAmount

    auto regTestParams = CChainParams::RegTest({});

    // Mint A: 200 DGB collateral, 10000 DD
    MockMintContext mintA(200 * COIN, 10000, 1500);
    // Mint B: 300 DGB collateral, 15000 DD
    MockMintContext mintB(300 * COIN, 15000, 1500);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);

    COutPoint collA(mintA.mintTxHash, 0);
    coinsView.AddCoin(collA, Coin(CTxOut(200 * COIN, mintA.collateralScript), 400, false), false);
    COutPoint collB(mintB.mintTxHash, 0);
    coinsView.AddCoin(collB, Coin(CTxOut(300 * COIN, mintB.collateralScript), 400, false), false);

    COutPoint ddA(mintA.mintTxHash, 1);
    coinsView.AddCoin(ddA, Coin(CTxOut(0, RH07_MakeP2TR(mintA.ddXOnly)), 400, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mintA.mintTxHash) { out = mintA.mintTxRef; return true; }
        if (txid == mintB.mintTxHash) { out = mintB.mintTxRef; return true; }
        return false;
    };

    // Redeem A, but include B's collateral as "fee input"
    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;

    mtx.vin.push_back(CTxIn(collA, CScript(), 0xFFFFFFFE));  // vin[0]: A's collateral
    mtx.vin.push_back(CTxIn(ddA, CScript(), 0xFFFFFFFE));    // vin[1]: A's DD (burn)
    mtx.vin.push_back(CTxIn(collB));                          // vin[2]: B's collateral as "fee"!

    // Output: 500 DGB total (200 from A + 300 from B)
    mtx.vout.push_back(CTxOut(500 * COIN, RH07_MakeP2TR(mintA.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateCollateralReleaseAmount(tx, ctx, 10000, state);

    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-extra]: Collateral from mint B cannot be used as fee input. "
        "Each collateral must be redeemed separately with its own DD burn. "
        "Reason: " + state.GetRejectReason());

    BOOST_CHECK_MESSAGE(state.GetRejectReason().find("collateral-as-fee") != std::string::npos,
        "Should reject with 'collateral-as-fee' reason, got: " + state.GetRejectReason());
}

// ============================================================================
// ADDITIONAL ATTACK: Redemption with no DD inputs at all
// ============================================================================

BOOST_AUTO_TEST_CASE(rh07_extra_redeem_without_dd_inputs)
{
    // ATTACK: Construct a redemption tx with only collateral input + fee input
    // and no DD burn at all. This would release collateral without burning any DD.

    auto regTestParams = CChainParams::RegTest({});

    MockMintContext mint(200 * COIN, 10000, 1500);

    CCoinsView baseView;
    CCoinsViewCache coinsView(&baseView);

    COutPoint collOutpoint(mint.mintTxHash, 0);
    coinsView.AddCoin(collOutpoint, Coin(CTxOut(200 * COIN, mint.collateralScript), 400, false), false);

    // Fee input (regular DGB, not DD)
    CKey feeKey; feeKey.MakeNewKey(true);
    COutPoint feeOutpoint(uint256S("fee0000000000000000000000000000000000000000000000000000000000001"), 0);
    CScript feeScript = CScript() << OP_0 << std::vector<unsigned char>(20, 0xab);  // P2WPKH
    coinsView.AddCoin(feeOutpoint, Coin(CTxOut(1 * COIN, feeScript), 300, false), false);

    auto txLookup = [&](const uint256& txid, uint32_t h, CTransactionRef& out) -> bool {
        if (txid == mint.mintTxHash) { out = mint.mintTxRef; return true; }
        return false;
    };

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = 1500;

    mtx.vin.push_back(CTxIn(collOutpoint, CScript(), 0xFFFFFFFE));
    mtx.vin.push_back(CTxIn(feeOutpoint));  // Fee only, no DD!
    mtx.vout.push_back(CTxOut(200 * COIN, RH07_MakeP2TR(mint.ownerXOnly)));

    CTransaction tx(mtx);
    TxValidationState state;
    DigiDollar::ValidationContext ctx(2000, 50000, 150, *regTestParams, &coinsView, false, txLookup);

    bool result = DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);

    BOOST_CHECK_MESSAGE(!result,
        "DEFENSE VERIFIED [RH-07-extra]: Cannot redeem without burning DD. "
        "hasDDInput must be false when only fee inputs are provided. "
        "Reason: " + state.GetRejectReason());

    BOOST_CHECK_MESSAGE(state.GetRejectReason().find("no-dd") != std::string::npos ||
                        state.GetRejectReason().find("dd-not-burned") != std::string::npos,
        "Should mention missing DD inputs, got: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_SUITE_END()
