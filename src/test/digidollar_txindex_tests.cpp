// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <init.h>

#include <common/args.h>
#include <consensus/digidollar.h>
#include <consensus/params.h>
#include <digidollar/validation.h>
#include <kernel/chainparams.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

BOOST_AUTO_TEST_SUITE(digidollar_txindex_tests)

namespace {

CScript MakeDummyP2TR(unsigned char tag)
{
    return CScript() << OP_1 << std::vector<unsigned char>(32, tag);
}

CScript MakeDDOpReturn(DigiDollarTxType tx_type, const std::vector<CAmount>& amounts)
{
    CScript op_return = CScript() << OP_RETURN
                                  << std::vector<unsigned char>{'D', 'D'}
                                  << CScriptNum(static_cast<int>(tx_type));
    for (const CAmount amount : amounts) {
        op_return << CScriptNum(amount);
    }
    return op_return;
}

} // namespace

BOOST_AUTO_TEST_CASE(dd_chain_requires_txindex)
{
    const auto chainparams = CChainParams::TestNet();
    ArgsManager args;
    SetupServerArgs(args);

    BOOST_CHECK(IsDigiDollarTxIndexRequired(*chainparams, args));

    const bool should_error = IsDigiDollarTxIndexRequired(*chainparams, args) &&
                              !args.GetBoolArg("-txindex", false);
    BOOST_CHECK(should_error);

    const std::string error = GetDigiDollarTxIndexRequirementError(*chainparams);
    BOOST_CHECK(error.find("-txindex=1") != std::string::npos);
    BOOST_CHECK(error.find("[test]") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(dd_chain_with_txindex_ok)
{
    const auto chainparams = CChainParams::Main();
    ArgsManager args;
    SetupServerArgs(args);
    args.ForceSetArg("-txindex", "1");

    BOOST_CHECK(IsDigiDollarTxIndexRequired(*chainparams, args));
    BOOST_CHECK(args.GetBoolArg("-txindex", false));

    const bool should_error = IsDigiDollarTxIndexRequired(*chainparams, args) &&
                              !args.GetBoolArg("-txindex", false);
    BOOST_CHECK(!should_error);
}

BOOST_AUTO_TEST_CASE(non_dd_chain_no_txindex_ok)
{
    CChainParams::RegTestOptions opts;
    CChainParams::VersionBitsParameters dd_vb_params{};
    dd_vb_params.start_time = Consensus::BIP9Deployment::NEVER_ACTIVE;
    dd_vb_params.timeout = Consensus::BIP9Deployment::NO_TIMEOUT;
    dd_vb_params.min_activation_height = 0;
    opts.version_bits_parameters[Consensus::DEPLOYMENT_DIGIDOLLAR] = dd_vb_params;

    const auto chainparams = CChainParams::RegTest(opts);

    ArgsManager args;
    SetupServerArgs(args);
    args.ForceSetArg("-digidollar", "1");

    BOOST_CHECK(!IsDigiDollarTxIndexRequired(*chainparams, args));

    const bool should_error = IsDigiDollarTxIndexRequired(*chainparams, args) &&
                              !args.GetBoolArg("-txindex", false);
    BOOST_CHECK(!should_error);
}

BOOST_AUTO_TEST_CASE(block_db_fallback_correct_amounts)
{
    constexpr uint32_t COIN_HEIGHT = 777;

    auto lookup_for = [](const CTransactionRef& tx, uint32_t expected_height) {
        return [tx, expected_height](const uint256& txid, uint32_t coin_height, CTransactionRef& tx_out) -> bool {
            if (coin_height != expected_height || txid != tx->GetHash()) return false;
            tx_out = tx;
            return true;
        };
    };

    // MINT: single DD amount in OP_RETURN should map to first DD P2TR output.
    CMutableTransaction mint_mtx;
    mint_mtx.nVersion = 0x01000770;
    mint_mtx.vout = {
        CTxOut(0, MakeDDOpReturn(DD_TX_MINT, {10000})),
        CTxOut(5 * COIN, CScript() << OP_TRUE),
        CTxOut(0, MakeDummyP2TR(0x11)),
    };
    const CTransactionRef mint_tx = MakeTransactionRef(mint_mtx);
    CAmount amount = 0;
    BOOST_REQUIRE(DigiDollar::ExtractDDAmountFromBlockDb(COutPoint(mint_tx->GetHash(), 2), COIN_HEIGHT,
                                                          lookup_for(mint_tx, COIN_HEIGHT), amount));
    BOOST_CHECK_EQUAL(amount, 10000);

    // TRANSFER: multiple OP_RETURN DD amounts map to DD P2TR output order.
    CMutableTransaction transfer_mtx;
    transfer_mtx.nVersion = 0x02000770;
    transfer_mtx.vout = {
        CTxOut(0, MakeDDOpReturn(DD_TX_TRANSFER, {2500, 7500})),
        CTxOut(0, MakeDummyP2TR(0x21)),
        CTxOut(0, MakeDummyP2TR(0x22)),
        CTxOut(1 * COIN, CScript() << OP_TRUE),
    };
    const CTransactionRef transfer_tx = MakeTransactionRef(transfer_mtx);
    BOOST_REQUIRE(DigiDollar::ExtractDDAmountFromBlockDb(COutPoint(transfer_tx->GetHash(), 1), COIN_HEIGHT,
                                                          lookup_for(transfer_tx, COIN_HEIGHT), amount));
    BOOST_CHECK_EQUAL(amount, 2500);
    BOOST_REQUIRE(DigiDollar::ExtractDDAmountFromBlockDb(COutPoint(transfer_tx->GetHash(), 2), COIN_HEIGHT,
                                                          lookup_for(transfer_tx, COIN_HEIGHT), amount));
    BOOST_CHECK_EQUAL(amount, 7500);

    // REDEEM: first OP_RETURN amount maps to DD change output.
    CMutableTransaction redeem_mtx;
    redeem_mtx.nVersion = 0x03000770;
    redeem_mtx.vout = {
        CTxOut(0, MakeDDOpReturn(DD_TX_REDEEM, {1200})),
        CTxOut(4 * COIN, CScript() << OP_TRUE),
        CTxOut(0, MakeDummyP2TR(0x31)),
    };
    const CTransactionRef redeem_tx = MakeTransactionRef(redeem_mtx);
    BOOST_REQUIRE(DigiDollar::ExtractDDAmountFromBlockDb(COutPoint(redeem_tx->GetHash(), 2), COIN_HEIGHT,
                                                          lookup_for(redeem_tx, COIN_HEIGHT), amount));
    BOOST_CHECK_EQUAL(amount, 1200);
}

BOOST_AUTO_TEST_CASE(dd_chain_prune_does_not_require_txindex)
{
    // v9.26.4: a pruned DigiDollar node does not require txindex. DD amount/lock
    // resolution reads the creating transaction from the retained DigiDollar-era block
    // window (kept by the "digidollar" prune lock) instead of the transaction index, so
    // pruning and DigiDollar can coexist. A full node still requires txindex.
    auto check = [](const CChainParams& params) {
        ArgsManager args;
        SetupServerArgs(args);
        // Full node (no prune): txindex required.
        BOOST_CHECK(IsDigiDollarTxIndexRequired(params, args));
        // Pruned node: not required.
        args.ForceSetArg("-prune", "550");
        BOOST_CHECK(!IsDigiDollarTxIndexRequired(params, args));
    };
    check(*CChainParams::Main());
    check(*CChainParams::TestNet());
}

BOOST_AUTO_TEST_SUITE_END()
