// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/err.h>
#include <digidollar/digidollar.h>
#include <digidollar/health.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/standard.h>
#include <test/util/setup_common.h>
#include <test/util/txmempool.h>
#include <uint256.h>
#include <validation.h>

#include <cstdint>
#include <string>
#include <limits>
#include <vector>

namespace {

static CScript MakeP2TR(const XOnlyPubKey& key)
{
    return CScript() << OP_1 << std::vector<unsigned char>(key.begin(), key.end());
}

static CScript MakeMintOpReturn(CAmount dd_amount, int64_t lock_height, int64_t lock_tier, const XOnlyPubKey& owner)
{
    return CScript() << OP_RETURN
                     << std::vector<unsigned char>{'D', 'D'}
                     << CScriptNum(1)
                     << CScriptNum(dd_amount)
                     << CScriptNum(lock_height)
                     << CScriptNum(lock_tier)
                     << std::vector<unsigned char>(owner.begin(), owner.end());
}

static CScript MakeUnregisteredCollateralP2TR(const DigiDollar::MintParams& params)
{
    TaprootBuilder builder;
    const CScript normal_path = DigiDollar::CreateNormalRedemptionPath(params);
    const CScript err_path = DigiDollar::CreateERRPath(params);

    if (!normal_path.empty()) {
        builder.Add(1, normal_path, 0xC0);
    }
    if (!err_path.empty()) {
        builder.Add(1, err_path, 0xC0);
    }
    builder.Finalize(params.internalKey);

    if (!builder.IsValid() || !builder.IsComplete()) {
        return CScript();
    }

    const WitnessV1Taproot output = builder.GetOutput();
    return CScript() << OP_1 << std::vector<unsigned char>(output.begin(), output.end());
}

static CScript MakeRedeemOpReturn(CAmount dd_amount)
{
    return CScript() << OP_RETURN
                     << std::vector<unsigned char>{'D', 'D'}
                     << CScriptNum(3)
                     << CScriptNum(dd_amount);
}

struct MintData {
    XOnlyPubKey owner;
    CTransactionRef tx;
    uint32_t collateral_index;
    uint32_t dd_index;
    uint32_t dgb_change_index;
};

enum class MintOutputOrder {
    CollateralThenDDThenOpReturn,
    DDThenOpReturnThenCollateral,
};

struct DigiDollarBurnEnforcementTestSetup : public TestingSetup {
    DigiDollarBurnEnforcementTestSetup()
        : TestingSetup(ChainType::REGTEST)
    {
        DigiDollar::SystemHealthMonitor::ResetMetrics();
    }

    ~DigiDollarBurnEnforcementTestSetup()
    {
        DigiDollar::SystemHealthMonitor::ResetMetrics();
    }

    static constexpr int MINT_HEIGHT = 1000;
    static constexpr int REDEEM_HEIGHT = 2000;
    static constexpr CAmount ORACLE_PRICE_MICRO_USD = 1000000;
    static constexpr int SYSTEM_COLLATERAL = 150;
    static constexpr CAmount DD_AMOUNT = 10000;
    static constexpr CAmount LOCKED_COLLATERAL = 500 * COIN;
    static constexpr int64_t LOCK_HEIGHT = 1240;

    MintData CreateMint(MintOutputOrder order = MintOutputOrder::CollateralThenDDThenOpReturn,
                        bool register_collateral_metadata = true,
                        CAmount dd_amount = DD_AMOUNT,
                        CAmount dgb_change = 0) const
    {
        CKey owner_key;
        owner_key.MakeNewKey(true);
        const XOnlyPubKey owner(owner_key.GetPubKey());

        DigiDollar::MintParams params;
        params.ddAmount = dd_amount;
        params.lockHeight = LOCK_HEIGHT;
        params.ownerKey = owner;
        params.internalKey = DigiDollar::GetCollateralNUMSKey();
        params.oracleKeys = DigiDollar::GetOracleKeys(15);

        const CScript collateral_script = register_collateral_metadata
            ? DigiDollar::CreateCollateralP2TR(params)
            : MakeUnregisteredCollateralP2TR(params);
        const CScript dd_script = DigiDollar::CreateDigiDollarP2TR(owner, dd_amount);
        const CScript op_return = MakeMintOpReturn(dd_amount, LOCK_HEIGHT, /*lock_tier=*/0, owner);

        CMutableTransaction mtx;
        mtx.nVersion = 0x01000770;
        mtx.vin.push_back(CTxIn(COutPoint(uint256S("0101010101010101010101010101010101010101010101010101010101010101"), 0)));

        MintData mint;
        mint.owner = owner;
        mint.dgb_change_index = std::numeric_limits<uint32_t>::max();

        if (order == MintOutputOrder::CollateralThenDDThenOpReturn) {
            mint.collateral_index = 0;
            mint.dd_index = 1;
            mtx.vout.push_back(CTxOut(LOCKED_COLLATERAL, collateral_script));
            mtx.vout.push_back(CTxOut(0, dd_script));
            mtx.vout.push_back(CTxOut(0, op_return));
        } else {
            mint.dd_index = 0;
            mint.collateral_index = 2;
            mtx.vout.push_back(CTxOut(0, dd_script));
            mtx.vout.push_back(CTxOut(0, op_return));
            mtx.vout.push_back(CTxOut(LOCKED_COLLATERAL, collateral_script));
        }

        if (dgb_change > 0) {
            mint.dgb_change_index = mtx.vout.size();
            mtx.vout.push_back(CTxOut(dgb_change, CScript() << OP_TRUE));
        }

        mint.tx = MakeTransactionRef(mtx);
        return mint;
    }

    COutPoint CollateralOutpoint(const MintData& mint) const
    {
        return COutPoint(mint.tx->GetHash(), mint.collateral_index);
    }

    COutPoint DDOutpoint(const MintData& mint) const
    {
        return COutPoint(mint.tx->GetHash(), mint.dd_index);
    }

    COutPoint DGBChangeOutpoint(const MintData& mint) const
    {
        return COutPoint(mint.tx->GetHash(), mint.dgb_change_index);
    }

    void AddMintCoins(CCoinsViewCache& coins, const MintData& mint, bool include_dd = true) const
    {
        coins.AddCoin(CollateralOutpoint(mint), Coin(mint.tx->vout[mint.collateral_index], MINT_HEIGHT, false), false);
        if (include_dd) {
            coins.AddCoin(DDOutpoint(mint), Coin(mint.tx->vout[mint.dd_index], MINT_HEIGHT, false), false);
        }
        if (mint.dgb_change_index != std::numeric_limits<uint32_t>::max()) {
            coins.AddCoin(DGBChangeOutpoint(mint), Coin(mint.tx->vout[mint.dgb_change_index], MINT_HEIGHT, false), false);
        }
    }

    DigiDollar::TxLookupFn TxLookupFor(const MintData& mint) const
    {
        return [mint_ref = mint.tx](const uint256& txid, uint32_t, CTransactionRef& out) -> bool {
            if (txid == mint_ref->GetHash()) {
                out = mint_ref;
                return true;
            }
            return false;
        };
    }

    DigiDollar::TxLookupFn TxLookupFor(const MintData& first, const MintData& second) const
    {
        return [first_ref = first.tx, second_ref = second.tx](const uint256& txid, uint32_t, CTransactionRef& out) -> bool {
            if (txid == first_ref->GetHash()) {
                out = first_ref;
                return true;
            }
            if (txid == second_ref->GetHash()) {
                out = second_ref;
                return true;
            }
            return false;
        };
    }

    DigiDollar::TxLookupFn TxLookupFor(const std::vector<MintData>& mints) const
    {
        std::vector<CTransactionRef> mint_txs;
        mint_txs.reserve(mints.size());
        for (const MintData& mint : mints) {
            mint_txs.push_back(mint.tx);
        }
        return [mint_txs](const uint256& txid, uint32_t, CTransactionRef& out) -> bool {
            for (const CTransactionRef& tx : mint_txs) {
                if (txid == tx->GetHash()) {
                    out = tx;
                    return true;
                }
            }
            return false;
        };
    }

    DigiDollar::ValidationContext MakeContext(const CCoinsViewCache& coins, DigiDollar::TxLookupFn tx_lookup,
                                              int system_collateral = SYSTEM_COLLATERAL,
                                              CAmount oracle_price_micro_usd = ORACLE_PRICE_MICRO_USD) const
    {
        return DigiDollar::ValidationContext(REDEEM_HEIGHT,
                                             oracle_price_micro_usd,
                                             system_collateral,
                                             Params(),
                                             &coins,
                                             /*skip_oracle=*/false,
                                             tx_lookup);
    }

    CTransaction MakeRedeemTx(const MintData& mint, bool include_dd_input, CAmount collateral_release, CAmount dd_change = 0) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x03000770;
        mtx.nLockTime = LOCK_HEIGHT;
        mtx.vin.push_back(CTxIn(CollateralOutpoint(mint), CScript(), 0xfffffffe));
        if (include_dd_input) {
            mtx.vin.push_back(CTxIn(DDOutpoint(mint), CScript(), 0xfffffffe));
        }

        mtx.vout.push_back(CTxOut(collateral_release, MakeP2TR(mint.owner)));

        if (dd_change > 0) {
            mtx.vout.push_back(CTxOut(0, DigiDollar::CreateDigiDollarP2TR(mint.owner, dd_change)));
            mtx.vout.push_back(CTxOut(0, MakeRedeemOpReturn(dd_change)));
        }

        return CTransaction(mtx);
    }

    CTransaction MakeRedeemTxWithFeeInput(const MintData& mint,
                                          const COutPoint& fee_outpoint,
                                          CAmount collateral_release,
                                          CAmount fee_change) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x03000770;
        mtx.nLockTime = LOCK_HEIGHT;
        mtx.vin.push_back(CTxIn(CollateralOutpoint(mint), CScript(), 0xfffffffe));
        mtx.vin.push_back(CTxIn(DDOutpoint(mint), CScript(), 0xfffffffe));
        mtx.vin.push_back(CTxIn(fee_outpoint, CScript(), 0xfffffffe));

        mtx.vout.push_back(CTxOut(collateral_release, MakeP2TR(mint.owner)));
        if (fee_change > 0) {
            mtx.vout.push_back(CTxOut(fee_change, MakeP2TR(mint.owner)));
        }

        return CTransaction(mtx);
    }

    CTransaction MakeRedeemTxWithExtraDD(const MintData& redeemed_mint,
                                         const MintData& extra_dd_mint,
                                         CAmount collateral_release,
                                         CAmount dd_change) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x03000770;
        mtx.nLockTime = LOCK_HEIGHT;
        mtx.vin.push_back(CTxIn(CollateralOutpoint(redeemed_mint), CScript(), 0xfffffffe));
        mtx.vin.push_back(CTxIn(DDOutpoint(redeemed_mint), CScript(), 0xfffffffe));
        mtx.vin.push_back(CTxIn(DDOutpoint(extra_dd_mint), CScript(), 0xfffffffe));

        mtx.vout.push_back(CTxOut(collateral_release, MakeP2TR(redeemed_mint.owner)));

        if (dd_change > 0) {
            mtx.vout.push_back(CTxOut(0, DigiDollar::CreateDigiDollarP2TR(redeemed_mint.owner, dd_change)));
            mtx.vout.push_back(CTxOut(0, MakeRedeemOpReturn(dd_change)));
        }

        return CTransaction(mtx);
    }

    CTransaction MakeRedeemTxWithDDInputs(const MintData& redeemed_mint,
                                          const std::vector<MintData>& dd_input_mints,
                                          CAmount collateral_release,
                                          CAmount dd_change) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x03000770;
        mtx.nLockTime = LOCK_HEIGHT;
        mtx.vin.push_back(CTxIn(CollateralOutpoint(redeemed_mint), CScript(), 0xfffffffe));
        for (const MintData& mint : dd_input_mints) {
            mtx.vin.push_back(CTxIn(DDOutpoint(mint), CScript(), 0xfffffffe));
        }

        mtx.vout.push_back(CTxOut(collateral_release, MakeP2TR(redeemed_mint.owner)));
        if (dd_change > 0) {
            mtx.vout.push_back(CTxOut(0, DigiDollar::CreateDigiDollarP2TR(redeemed_mint.owner, dd_change)));
            mtx.vout.push_back(CTxOut(0, MakeRedeemOpReturn(dd_change)));
        }

        return CTransaction(mtx);
    }

    CTransaction MakeRedeemTxFromPrevout(const MintData& mint,
                                         const COutPoint& collateral_outpoint,
                                         CAmount collateral_release,
                                         CAmount dd_change = 0) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x03000770;
        mtx.nLockTime = LOCK_HEIGHT;
        mtx.vin.push_back(CTxIn(collateral_outpoint, CScript(), 0xfffffffe));
        mtx.vin.push_back(CTxIn(DDOutpoint(mint), CScript(), 0xfffffffe));
        mtx.vout.push_back(CTxOut(collateral_release, MakeP2TR(mint.owner)));

        if (dd_change > 0) {
            mtx.vout.push_back(CTxOut(0, DigiDollar::CreateDigiDollarP2TR(mint.owner, dd_change)));
            mtx.vout.push_back(CTxOut(0, MakeRedeemOpReturn(dd_change)));
        }

        return CTransaction(mtx);
    }

    CTransaction MakeRedeemTxFromPrevoutWithExtraDD(const MintData& redeemed_mint,
                                                    const COutPoint& collateral_outpoint,
                                                    const MintData& extra_dd_mint,
                                                    CAmount collateral_release,
                                                    CAmount dd_change) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x03000770;
        mtx.nLockTime = LOCK_HEIGHT;
        mtx.vin.push_back(CTxIn(collateral_outpoint, CScript(), 0xfffffffe));
        mtx.vin.push_back(CTxIn(DDOutpoint(redeemed_mint), CScript(), 0xfffffffe));
        mtx.vin.push_back(CTxIn(DDOutpoint(extra_dd_mint), CScript(), 0xfffffffe));
        mtx.vout.push_back(CTxOut(collateral_release, MakeP2TR(redeemed_mint.owner)));

        if (dd_change > 0) {
            mtx.vout.push_back(CTxOut(0, DigiDollar::CreateDigiDollarP2TR(redeemed_mint.owner, dd_change)));
            mtx.vout.push_back(CTxOut(0, MakeRedeemOpReturn(dd_change)));
        }

        return CTransaction(mtx);
    }

    CTransaction WithLockTime(const CTransaction& tx, uint32_t lock_time) const
    {
        CMutableTransaction mtx(tx);
        mtx.nLockTime = lock_time;
        return CTransaction(mtx);
    }

    CTransaction MakeNonDDCollateralSpend(const MintData& mint) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 2;
        mtx.nLockTime = LOCK_HEIGHT;
        mtx.vin.push_back(CTxIn(CollateralOutpoint(mint), CScript(), 0xfffffffe));
        mtx.vout.push_back(CTxOut(LOCKED_COLLATERAL, MakeP2TR(mint.owner)));
        return CTransaction(mtx);
    }

    CTransaction MakeTransferWithCollateralInput(const MintData& mint) const
    {
        CMutableTransaction mtx;
        mtx.nVersion = 0x02000770;
        mtx.nLockTime = LOCK_HEIGHT;
        mtx.vin.push_back(CTxIn(DDOutpoint(mint), CScript(), 0xfffffffe));
        mtx.vin.push_back(CTxIn(CollateralOutpoint(mint), CScript(), 0xfffffffe));
        mtx.vout.push_back(CTxOut(0, DigiDollar::CreateDigiDollarP2TR(mint.owner, DD_AMOUNT)));
        mtx.vout.push_back(CTxOut(0, CScript() << OP_RETURN
                                            << std::vector<unsigned char>{'D', 'D'}
                                            << CScriptNum(2)
                                            << CScriptNum(DD_AMOUNT)));
        mtx.vout.push_back(CTxOut(LOCKED_COLLATERAL, MakeP2TR(mint.owner)));
        return CTransaction(mtx);
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_burn_enforcement_tests, DigiDollarBurnEnforcementTestSetup)

BOOST_AUTO_TEST_CASE(post_timelock_non_dd_collateral_spend_is_rejected)
{
    const MintData mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction spend = MakeNonDDCollateralSpend(mint);

    const bool valid = DigiDollar::ValidateDigiDollarTransaction(spend, ctx, state);
    BOOST_CHECK_MESSAGE(!valid,
        "Post-timelock collateral spends must remain DD redemptions; non-DD spend was accepted. "
        "Reject reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(post_timelock_non_dd_collateral_spend_is_rejected_without_script_metadata)
{
    const MintData mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                     /*register_collateral_metadata=*/false);
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction spend = MakeNonDDCollateralSpend(mint);

    const bool valid = DigiDollar::ValidateDigiDollarTransaction(spend, ctx, state);
    BOOST_CHECK_MESSAGE(!valid,
        "Post-restart/reindex collateral spends must be detected from the creating mint tx, "
        "not only process-local script metadata. Reject reason: " + state.GetRejectReason());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-spend-missing-dd-burn");
}

BOOST_AUTO_TEST_CASE(production_gate_must_not_rely_only_on_dd_marker_for_collateral_spends)
{
    const MintData mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                     /*register_collateral_metadata=*/false);
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    const CTransaction spend = MakeNonDDCollateralSpend(mint);
    BOOST_REQUIRE(!DigiDollar::HasDigiDollarMarker(spend));

    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    BOOST_CHECK_MESSAGE(DigiDollar::RequiresDigiDollarValidation(spend, ctx),
        "Production mempool/miner/block gates cannot use only the DD version marker; "
        "a non-DD transaction spending DD collateral must still enter DD validation.");
}

BOOST_AUTO_TEST_CASE(unconfirmed_mint_collateral_child_requires_dd_validation)
{
    const MintData mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                     /*register_collateral_metadata=*/false);
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    coins.AddCoin(CollateralOutpoint(mint),
                  Coin(mint.tx->vout[mint.collateral_index], MEMPOOL_HEIGHT, false),
                  false);

    {
        LOCK2(cs_main, m_node.mempool->cs);
        TestMemPoolEntryHelper entry;
        m_node.mempool->addUnchecked(entry.Fee(1000).FromTx(mint.tx));
    }

    const auto no_block_lookup = [](const uint256&, uint32_t, CTransactionRef&) {
        return false;
    };
    const DigiDollar::ValidationContext ctx(REDEEM_HEIGHT,
                                            ORACLE_PRICE_MICRO_USD,
                                            SYSTEM_COLLATERAL,
                                            Params(),
                                            &coins,
                                            /*skip_oracle=*/false,
                                            no_block_lookup,
                                            m_node.mempool.get());

    const CTransaction spend = MakeNonDDCollateralSpend(mint);
    BOOST_REQUIRE(!DigiDollar::HasDigiDollarMarker(spend));
    BOOST_CHECK_MESSAGE(DigiDollar::RequiresDigiDollarValidation(spend, ctx),
        "A child spending an unconfirmed DD mint's collateral output must still "
        "enter DigiDollar validation via the mempool parent; otherwise package "
        "acceptance can miss the mandatory burn gate.");

    TxValidationState state;
    const bool valid = DigiDollar::ValidateDigiDollarTransaction(spend, ctx, state);
    BOOST_CHECK_MESSAGE(!valid,
        "Unconfirmed mint collateral child spend was accepted without DD burn. "
        "Reject reason: " + state.GetRejectReason());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-spend-missing-dd-burn");
}

BOOST_AUTO_TEST_CASE(dd_marked_transfer_cannot_spend_collateral_vault)
{
    const MintData mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                     /*register_collateral_metadata=*/false);
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction transfer = MakeTransferWithCollateralInput(mint);

    BOOST_REQUIRE(DigiDollar::HasDigiDollarMarker(transfer));
    BOOST_REQUIRE(DigiDollar::RequiresDigiDollarValidation(transfer, ctx));

    const bool valid = DigiDollar::ValidateDigiDollarTransaction(transfer, ctx, state);
    BOOST_CHECK_MESSAGE(!valid,
        "DD-marked transfer transactions must not be able to spend a collateral vault "
        "as an ordinary positive-value input without a redemption burn. Reject reason: " +
        state.GetRejectReason());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-spend-missing-dd-burn");
}

BOOST_AUTO_TEST_CASE(normal_redemption_succeeds_when_full_original_dd_is_burned)
{
    const MintData mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction redeem = MakeRedeemTx(mint, /*include_dd_input=*/true, LOCKED_COLLATERAL);

    BOOST_CHECK_MESSAGE(DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "Full-burn redemption should release the full collateral. Reject reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(redemption_rejects_collateral_under_return_inside_tolerance)
{
    const MintData mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CAmount under_return = LOCKED_COLLATERAL / 1000;
    const CTransaction redeem = MakeRedeemTx(mint,
                                             /*include_dd_input=*/true,
                                             LOCKED_COLLATERAL - under_return);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "A redemption that burns the full DD amount must still return the full "
        "locked collateral; the shortfall must not be accepted as miner fee.");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-release-incomplete");
}

BOOST_AUTO_TEST_CASE(redemption_fee_input_change_does_not_reduce_collateral_release)
{
    const MintData mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    const COutPoint fee_outpoint(uint256S("0202020202020202020202020202020202020202020202020202020202020202"), 0);
    coins.AddCoin(fee_outpoint, Coin(CTxOut(2 * COIN, MakeP2TR(mint.owner)), MINT_HEIGHT, false), false);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction redeem = MakeRedeemTxWithFeeInput(mint, fee_outpoint,
                                                        LOCKED_COLLATERAL,
                                                        /*fee_change=*/COIN);

    BOOST_CHECK_MESSAGE(DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "A redemption that returns full collateral and pays a large fee from a separate DGB input "
        "must not be rejected as an incomplete collateral release. Reject reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(partial_burn_cannot_release_collateral)
{
    const MintData mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CAmount dd_change = DD_AMOUNT / 2;
    const CTransaction redeem = MakeRedeemTx(mint, /*include_dd_input=*/true, LOCKED_COLLATERAL, dd_change);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "Partial DD burn must not release collateral");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-release-partial-burn");
}

BOOST_AUTO_TEST_CASE(redemption_rejects_multiple_dd_change_outputs)
{
    const MintData redeemed_mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                              /*register_collateral_metadata=*/false);
    const MintData extra_dd_mint_1 = CreateMint();
    const MintData extra_dd_mint_2 = CreateMint();
    const std::vector<MintData> all_mints{redeemed_mint, extra_dd_mint_1, extra_dd_mint_2};

    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, redeemed_mint);
    AddMintCoins(coins, extra_dd_mint_1);
    AddMintCoins(coins, extra_dd_mint_2);

    CMutableTransaction mtx;
    mtx.nVersion = 0x03000770;
    mtx.nLockTime = LOCK_HEIGHT;
    mtx.vin.push_back(CTxIn(CollateralOutpoint(redeemed_mint), CScript(), 0xfffffffe));
    mtx.vin.push_back(CTxIn(DDOutpoint(redeemed_mint), CScript(), 0xfffffffe));
    mtx.vin.push_back(CTxIn(DDOutpoint(extra_dd_mint_1), CScript(), 0xfffffffe));
    mtx.vin.push_back(CTxIn(DDOutpoint(extra_dd_mint_2), CScript(), 0xfffffffe));
    mtx.vout.push_back(CTxOut(LOCKED_COLLATERAL, MakeP2TR(redeemed_mint.owner)));
    mtx.vout.push_back(CTxOut(0, DigiDollar::CreateDigiDollarP2TR(redeemed_mint.owner, DD_AMOUNT)));
    mtx.vout.push_back(CTxOut(0, DigiDollar::CreateDigiDollarP2TR(redeemed_mint.owner, DD_AMOUNT)));
    mtx.vout.push_back(CTxOut(0, MakeRedeemOpReturn(DD_AMOUNT)));
    const CTransaction redeem(mtx);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(all_mints));

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "A redemption OP_RETURN carries one DD change amount, so allowing multiple "
        "DD change outputs makes future amount extraction ambiguous.");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-redeem-multiple-dd-change-outputs");
}

BOOST_AUTO_TEST_CASE(redemption_before_mint_lock_height_rejected)
{
    const MintData mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                     /*register_collateral_metadata=*/false);
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction redeem = WithLockTime(MakeRedeemTx(mint, /*include_dd_input=*/true, LOCKED_COLLATERAL),
                                             LOCK_HEIGHT - 1);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "DigiDollar redemption validation must compare tx.nLockTime to the lock height "
        "committed by the original mint, not only to the current chain height.");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "redemption-timelock-active");
}

BOOST_AUTO_TEST_CASE(err_redemption_before_mint_lock_height_rejected)
{
    const MintData redeemed_mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                              /*register_collateral_metadata=*/false);
    const MintData extra_dd_mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, redeemed_mint);
    AddMintCoins(coins, extra_dd_mint);

    constexpr int err_health = 80;
    const CAmount required_burn = DigiDollar::ERR::EmergencyRedemptionRatio::GetRequiredDDBurn(DD_AMOUNT, err_health);
    const CAmount total_dd_inputs = 2 * DD_AMOUNT;
    BOOST_REQUIRE_GT(total_dd_inputs, required_burn);
    const CAmount dd_change = total_dd_inputs - required_burn;

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(redeemed_mint, extra_dd_mint), err_health);
    const CTransaction redeem = WithLockTime(MakeRedeemTxWithExtraDD(redeemed_mint, extra_dd_mint,
                                                                     LOCKED_COLLATERAL, dd_change),
                                             LOCK_HEIGHT - 1);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "ERR redemptions must still enforce the original mint timelock before applying "
        "the extra DD burn requirement.");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "redemption-timelock-active");
}

BOOST_AUTO_TEST_CASE(redemption_rejects_mint_change_output_as_collateral_input)
{
    const CAmount dgb_change = 3 * COIN;
    const MintData mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                     /*register_collateral_metadata=*/false,
                                     DD_AMOUNT,
                                     dgb_change);
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction redeem = MakeRedeemTxFromPrevout(mint, DGBChangeOutpoint(mint), dgb_change);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "A redemption must spend the mint's canonical collateral output as input 0; "
        "ordinary DGB change from the mint must not be redeemable as a collateral vault.");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-release-not-vault");
}

BOOST_AUTO_TEST_CASE(err_redemption_rejects_mint_change_output_as_collateral_input)
{
    const CAmount dgb_change = 3 * COIN;
    const MintData redeemed_mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                              /*register_collateral_metadata=*/false,
                                              DD_AMOUNT,
                                              dgb_change);
    const MintData extra_dd_mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, redeemed_mint);
    AddMintCoins(coins, extra_dd_mint);

    constexpr int err_health = 80;
    const CAmount required_burn = DigiDollar::ERR::EmergencyRedemptionRatio::GetRequiredDDBurn(DD_AMOUNT, err_health);
    const CAmount total_dd_inputs = 2 * DD_AMOUNT;
    BOOST_REQUIRE_GT(total_dd_inputs, required_burn);
    const CAmount dd_change = total_dd_inputs - required_burn;

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(redeemed_mint, extra_dd_mint), err_health);
    const CTransaction redeem = MakeRedeemTxFromPrevoutWithExtraDD(redeemed_mint,
                                                                   DGBChangeOutpoint(redeemed_mint),
                                                                   extra_dd_mint,
                                                                   dgb_change,
                                                                   dd_change);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "ERR burn rules must not let a mint's ordinary DGB change output masquerade "
        "as the collateral vault.");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-release-not-vault");
}

BOOST_AUTO_TEST_CASE(collateral_spend_with_no_dd_inputs_cannot_release_collateral)
{
    const MintData mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint, /*include_dd=*/false);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction redeem = MakeRedeemTx(mint, /*include_dd_input=*/false, LOCKED_COLLATERAL);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateCollateralReleaseAmount(redeem, ctx, /*ddBurned=*/0, state),
        "Collateral spend with no DD burn must not release locked DGB");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-release-partial-burn");
}

BOOST_AUTO_TEST_CASE(reordered_mint_outputs_identify_correct_collateral_output)
{
    const MintData mint = CreateMint(MintOutputOrder::DDThenOpReturnThenCollateral);
    CAmount dd_amount = 0;
    CAmount collateral_amount = 0;

    BOOST_REQUIRE(DigiDollar::ExtractMintAccountingAmounts(*mint.tx, dd_amount, collateral_amount));
    BOOST_CHECK_EQUAL(dd_amount, DD_AMOUNT);
    BOOST_CHECK_EQUAL(collateral_amount, LOCKED_COLLATERAL);

    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, TxLookupFor(mint));
    const CTransaction redeem = MakeRedeemTx(mint, /*include_dd_input=*/true, LOCKED_COLLATERAL);

    BOOST_CHECK_MESSAGE(DigiDollar::ValidateCollateralReleaseAmount(redeem, ctx, DD_AMOUNT, state),
        "Collateral vout index should come from the spent outpoint, not fixed output order. "
        "Reject reason: " + state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(err_redemption_accounting_uses_actual_dd_burn)
{
    const MintData redeemed_mint = CreateMint();
    const MintData extra_dd_mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, redeemed_mint);
    AddMintCoins(coins, extra_dd_mint);

    constexpr int err_health = 80;
    const CAmount required_burn = DigiDollar::ERR::EmergencyRedemptionRatio::GetRequiredDDBurn(DD_AMOUNT, err_health);
    BOOST_REQUIRE_GT(required_burn, DD_AMOUNT);
    const CAmount total_dd_inputs = 2 * DD_AMOUNT;
    BOOST_REQUIRE_GT(total_dd_inputs, required_burn);
    const CAmount dd_change = total_dd_inputs - required_burn;

    const DigiDollar::TxLookupFn tx_lookup = TxLookupFor(redeemed_mint, extra_dd_mint);
    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins, tx_lookup, err_health);
    const CTransaction redeem = MakeRedeemTxWithExtraDD(redeemed_mint, extra_dd_mint, LOCKED_COLLATERAL, dd_change);

    BOOST_REQUIRE_MESSAGE(DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "ERR redemption with the exact extra DD burn should be valid. Reject reason: " + state.GetRejectReason());

    std::vector<Coin> spent_coins;
    spent_coins.reserve(redeem.vin.size());
    for (const CTxIn& txin : redeem.vin) {
        Coin coin;
        BOOST_REQUIRE(coins.GetCoin(txin.prevout, coin));
        spent_coins.push_back(coin);
    }

    CAmount actual_burn = 0;
    CAmount released_collateral = 0;
    BOOST_REQUIRE(DigiDollar::ExtractRedemptionAccountingAmounts(redeem, spent_coins, tx_lookup,
                                                                 actual_burn, released_collateral));
    BOOST_CHECK_EQUAL(actual_burn, required_burn);
    BOOST_CHECK_EQUAL(released_collateral, LOCKED_COLLATERAL);
}

BOOST_AUTO_TEST_CASE(redemption_uses_context_oracle_price_for_err_health)
{
    const MintData mint = CreateMint();
    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    AddMintCoins(coins, mint);

    DigiDollar::SystemHealthMonitor::ResetMetrics();
    DigiDollar::SystemHealthMonitor::OnMintConnected(DD_AMOUNT, LOCKED_COLLATERAL);

    TxValidationState state;
    const DigiDollar::ValidationContext ctx = MakeContext(coins,
                                                          TxLookupFor(mint),
                                                          /*system_collateral=*/150,
                                                          /*oracle_price_micro_usd=*/100000);
    const CTransaction redeem = MakeRedeemTx(mint, /*include_dd_input=*/true, LOCKED_COLLATERAL);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "With active DD supply and a deterministic block oracle price that puts health below 100%, "
        "burning only the original DD amount must be rejected and ERR overburn enforced.");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-collateral-release-partial-burn");
}

BOOST_AUTO_TEST_CASE(redemption_change_above_max_digidollar_rejected)
{
    const MintData redeemed_mint = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                              /*register_collateral_metadata=*/true,
                                              MAX_DIGIDOLLAR);
    const MintData extra_mint_a = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                             /*register_collateral_metadata=*/true,
                                             MAX_DIGIDOLLAR);
    const MintData extra_mint_b = CreateMint(MintOutputOrder::CollateralThenDDThenOpReturn,
                                             /*register_collateral_metadata=*/true,
                                             MAX_DIGIDOLLAR);
    const std::vector<MintData> mints{redeemed_mint, extra_mint_a, extra_mint_b};

    CCoinsView base_view;
    CCoinsViewCache coins(&base_view);
    for (const MintData& mint : mints) {
        AddMintCoins(coins, mint);
    }

    const DigiDollar::TxLookupFn tx_lookup = TxLookupFor(mints);
    const DigiDollar::ValidationContext ctx = MakeContext(coins, tx_lookup);
    TxValidationState state;
    const CTransaction redeem = MakeRedeemTxWithDDInputs(redeemed_mint, mints, LOCKED_COLLATERAL, MAX_DIGIDOLLAR + 1);

    BOOST_CHECK_MESSAGE(!DigiDollar::ValidateRedemptionTransaction(redeem, ctx, state),
        "Redemption change above MAX_DIGIDOLLAR must be rejected as a per-output serialization violation");
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-redeem-opreturn-amount");
}

BOOST_AUTO_TEST_SUITE_END()
