// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// =============================================================================
// RH-59: Coin-control / fundrawtransaction interaction with IsLockedByDD
// =============================================================================
//
// POST-WALK-BACK NOTE (RC31):
//   The original framing of RH-59-01 ("VULN CONFIRMED") was wrong. Bitcoin
//   Core intentionally ignores normal lockunspent on manually selected outputs
//   (wallet_basic.py:188: "The lock on a manually selected output is
//   ignored"). The earlier audit commit that added an IsLockedCoin() check
//   in FetchSelectedInputs broke a dozen upstream functional tests and was
//   reverted. This test file is retained as a pin on the coin-selection
//   behavior — it asserts the actual (documented) Bitcoin invariant that
//   preset-input path accepts SelectExternal outpoints unconditionally —
//   but only for ordinary locks.
//
// DD token/collateral locks are different. They are protocol-accounting
// locks, not user convenience locks, and a normal DGB transaction must not
// be allowed to manually select them. DD spends must go through the DD-aware
// transfer/redeem builders so the wallet state and consensus metadata stay
// in sync.
//
// Red Hornet Wave-7 sub-agent 7B (angle C) --
// IsLockedByDD bypass via pre-selected coin-control inputs.
//
// Attack: `IsLockedByDD()` (src/wallet/digidollarwallet.cpp:1033) and
// `IsLockedCoin()` (src/wallet/wallet.cpp:2787) are the two wallet-level
// gates that keep DD collateral / DD-token UTXOs out of ordinary DGB
// transactions. Prior tests (rh08-ISMINE in
// digidollar_wallet_security_tests.cpp:669 and rh28_11 in
// digidollar_rh28_wallet_chains_tests.cpp:754) verify the helpers return
// `true` for locked outpoints but never verify that coin selection
// actually respects it.
//
// Finding: `FetchSelectedInputs` at src/wallet/spend.cpp:258-303 accepts
// any outpoint the caller passes via `CCoinControl::Select()` /
// `SelectExternal()`, without consulting *either* `CWallet::IsLockedCoin()`
// *or* `DigiDollarWallet::IsLockedByDD()`. Compare that with
// `AvailableCoins` at spend.cpp:394, which does honour `IsLockedCoin` when
// `params.skip_locked=true` (default), and has the DD-MINT vout-0/1/2
// heuristic at spend.cpp:407-416.
//
// Any RPC that exposes user-controlled prevouts --
// `fundrawtransaction` (src/wallet/rpc/spend.cpp:743),
// `walletcreatefundedpsbt`, `send`, `sendall` --
// threads the outpoint through `FetchSelectedInputs` and then into
// `CreateTransaction`, silently pulling a DD-locked UTXO into a regular
// transaction.
//
// Concrete harm (HIGH, wallet-state integrity + consensus-reject stepping
// stone -- not direct peg break, since consensus still enforces CLTV on the
// collateral and OP_DIGIDOLLAR on the token):
//
//   * The wallet constructs and optionally signs a tx spending DD
//     collateral pre-timelock. The RPC returns success and, if
//     `lockUnspents=true`, the preset prevout is added to
//     `setLockedCoins` again (src/wallet/spend.cpp:1414). DD bookkeeping
//     is never touched.
//   * Broadcast-time consensus rejection bounces back, but the user has
//     already lost: the wallet believes the coin is spent-pending until
//     `abandontransaction`. In the DD-token case (0-value output, simple
//     key-path P2TR), a malicious script can spend the token without the
//     DD validator noticing because `DigiDollarWallet::RemoveDDUTXO` is
//     only called by the DD-aware transfer/redeem builders, not by
//     `CreateTransaction`. Result: `getdigidollarbalance` over-reports the
//     user's DD holdings.
//   * Combined with prior finding C4 (unhandled `CScriptNum` exception in
//     DD transfer validator, src/digidollar/validation.cpp:1199,1206) the
//     attacker can craft an input whose scriptSig throws during ATMP DD
//     validation, turning the wallet-level bypass into a block-validation
//     abort once a miner includes the tx.

#include <boost/test/unit_test.hpp>

#include <consensus/amount.h>
#include <script/solver.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/digidollarwallet.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>
#include <random.h>
#include <script/script.h>
#include <script/standard.h>

namespace wallet {

BOOST_FIXTURE_TEST_SUITE(rh59_coincontrol_dd_lock_bypass_tests, WalletTestingSetup)

// =============================================================================
// RH-59-01: FetchSelectedInputs ignores regular `setLockedCoins`
// =============================================================================
//
// Primary exploit. A UTXO placed in `setLockedCoins` via LockCoin() (which
// is what the DD wallet does at init for every collateral / DD-token output
// it knows about -- see src/wallet/digidollarwallet.cpp:135,142 and
// :2136,2141) is still accepted by `FetchSelectedInputs`. Any RPC that
// exposes preset inputs therefore bypasses the lock entirely.
//
// The test drives `FetchSelectedInputs` directly with a locked external
// outpoint and asserts it is returned in the result set.
// =============================================================================
BOOST_AUTO_TEST_CASE(rh59_01_fetch_selected_inputs_ignores_locked_coin)
{
    // Fabricate an outpoint and mark it locked, then hand it to the preset
    // input path via SelectExternal. SelectExternal is exactly what
    // fundrawtransaction calls at src/wallet/spend.cpp:1384 when the input
    // isn't in the wallet's mapWallet.
    uint256 fake_txid;
    GetRandBytes(fake_txid);
    COutPoint locked_outpoint(fake_txid, 0);

    CKey sigkey;
    sigkey.MakeNewKey(true);
    const CPubKey pub = sigkey.GetPubKey();
    CScript script_pubkey = GetScriptForDestination(WitnessV0KeyHash(pub.GetID()));

    CTxOut txout;
    txout.nValue = 500 * COIN;
    txout.scriptPubKey = script_pubkey;

    {
        LOCK(m_wallet.cs_wallet);
        BOOST_REQUIRE(m_wallet.LockCoin(locked_outpoint));
        BOOST_REQUIRE(m_wallet.IsLockedCoin(locked_outpoint));
    }

    CCoinControl coin_control;
    coin_control.SelectExternal(locked_outpoint, txout);
    // Attacker-supplied `input_weights` field of fundrawtransaction maps
    // straight to SetInputWeight -- spend.cpp:698. This is how a real
    // fundrawtransaction call skips the "Not solvable" rejection at
    // spend.cpp:293 when the wallet doesn't know the input's descriptor.
    // We use the same trick so the test isolates the LOCK check, not the
    // solvability check.
    coin_control.SetInputWeight(locked_outpoint, 272); // ~P2WPKH input weight

    FastRandomContext rng_fast;
    CoinSelectionParams csp{rng_fast};
    csp.m_effective_feerate = CFeeRate(1000);
    csp.m_long_term_feerate = CFeeRate(1000);
    csp.m_discard_feerate   = CFeeRate(1000);

    util::Result<PreSelectedInputs> res = [&]() EXCLUSIVE_LOCKS_REQUIRED(m_wallet.cs_wallet) {
        LOCK(m_wallet.cs_wallet);
        return FetchSelectedInputs(m_wallet, coin_control, csp);
    }();

    BOOST_REQUIRE_MESSAGE(res.has_value(),
        "Expected FetchSelectedInputs to accept the outpoint. Error: "
        << (res.has_value() ? std::string{} : util::ErrorString(res).original));

    const PreSelectedInputs& preset = *res;
    std::set<COutPoint> accepted;
    for (const std::shared_ptr<COutput>& out : preset.coins) {
        accepted.insert(out->outpoint);
    }

    BOOST_CHECK_MESSAGE(accepted.count(locked_outpoint) == 1,
        "RH-59-01: FetchSelectedInputs accepted a UTXO that is in "
        "setLockedCoins, preserving Bitcoin Core's documented manual "
        "coin-control override behavior.");

    BOOST_TEST_MESSAGE("RH-59-01: preset-input path accepted locked "
                       "outpoint. Ordinary lockunspent remains a wallet "
                       "selection hint; DD-RH-053 covers only DD protocol "
                       "locks.");
}

// =============================================================================
// RH-59-02: DD-specific bypass via IsLockedByDD
// =============================================================================
//
// Repeats the preset-input probe with the real wallet-owned DD sidecar so
// `FetchSelectedInputs()` can see the DD lock through `wallet.GetDDWallet()`.
// The hardened behavior is to reject these selected inputs even though
// ordinary manual `lockunspent` selections remain allowed.
// =============================================================================
BOOST_AUTO_TEST_CASE(rh59_02_dd_wallet_locked_outpoint_also_bypassable)
{
    // Populate a DigiDollarWallet with a collateral position.
    m_wallet.EnsureDDWallet();
    DigiDollarWallet* dd_wallet = m_wallet.GetDDWallet();
    BOOST_REQUIRE(dd_wallet != nullptr);

    uint256 mint_txid;
    GetRandBytes(mint_txid);

    WalletCollateralPosition pos;
    pos.dd_timelock_id = mint_txid;
    pos.dd_minted = 50000;           // $500 in cents
    pos.dgb_collateral = 500 * COIN;
    pos.lock_tier = 4;
    pos.unlock_height = 1000000;     // still locked on any reasonable regtest tip
    pos.is_active = true;
    dd_wallet->AddCollateralPosition(pos);

    COutPoint collateral(mint_txid, 0);
    COutPoint dd_token(mint_txid, 1);

    BOOST_REQUIRE(dd_wallet->IsLockedByDD(collateral));
    BOOST_REQUIRE(dd_wallet->IsLockedByDD(dd_token));

    // Also install the regular setLockedCoins entry that init-time DD
    // scanning (src/wallet/digidollarwallet.cpp:135) would create.
    {
        LOCK(m_wallet.cs_wallet);
        BOOST_REQUIRE(m_wallet.LockCoin(collateral));
        BOOST_REQUIRE(m_wallet.LockCoin(dd_token));
    }

    // Build fake P2WPKH txouts so CalculateMaximumSignedInputSize succeeds.
    CKey sigkey;
    sigkey.MakeNewKey(true);
    const CPubKey pub = sigkey.GetPubKey();
    CScript script_pubkey = GetScriptForDestination(WitnessV0KeyHash(pub.GetID()));

    CTxOut coll_txout;
    coll_txout.nValue = 500 * COIN;
    coll_txout.scriptPubKey = script_pubkey;

    CTxOut token_txout;
    token_txout.nValue = 0;          // DD-token outputs are 0 DGB
    token_txout.scriptPubKey = script_pubkey;

    CCoinControl coin_control;
    coin_control.SelectExternal(collateral, coll_txout);
    coin_control.SelectExternal(dd_token, token_txout);
    // Supply input weights to skip the solvability gate -- mirrors the
    // fundrawtransaction `input_weights` argument at
    // src/wallet/rpc/spend.cpp:698. Without this the test would hit the
    // "Not solvable" error at spend.cpp:293 before the lock-check gap has
    // a chance to manifest. Real attackers control this parameter.
    coin_control.SetInputWeight(collateral, 272);
    coin_control.SetInputWeight(dd_token, 272);

    FastRandomContext rng_fast;
    CoinSelectionParams csp{rng_fast};
    csp.m_effective_feerate = CFeeRate(1000);
    csp.m_long_term_feerate = CFeeRate(1000);
    csp.m_discard_feerate   = CFeeRate(1000);

    util::Result<PreSelectedInputs> res = [&]() EXCLUSIVE_LOCKS_REQUIRED(m_wallet.cs_wallet) {
        LOCK(m_wallet.cs_wallet);
        return FetchSelectedInputs(m_wallet, coin_control, csp);
    }();

    BOOST_CHECK_MESSAGE(!res.has_value(),
        "DD-RH-053: preset-input path accepted a DD-locked outpoint. "
        "Ordinary lockunspent can be overridden manually, but DD token and "
        "collateral outpoints must only be spent by DigiDollar-aware flows.");

    // Tightens the finding: even after we forcibly deactivate the
    // position (so IsLockedByDD now returns false for collateral), the
    // token stays in dd_utxos and the regular lock remains. The bypass
    // is stable.
    dd_wallet->UpdatePositionStatus(mint_txid, false);
    BOOST_CHECK(!dd_wallet->IsLockedByDD(collateral));
    BOOST_CHECK(dd_wallet->IsLockedByDD(dd_token)); // token still in dd_utxos

    BOOST_TEST_MESSAGE("DD-RH-053: preset-input path rejects DD-locked "
                       "collateral/token outpoints while RH-59-01 preserves "
                       "ordinary manual lockunspent override behavior.");
}

// =============================================================================
// RH-59-02b: DD transaction shape blocks preset inputs even if sidecar is stale
// =============================================================================
//
// IsLockedByDD is the primary stateful lock, but a wallet-owned DD mint tx is
// also self-identifying through its version and output layout. If the DD sidecar
// is stale or absent, manually preselecting vout 0 must still fail for ordinary
// DGB transaction creation; otherwise coin-control can spend collateral through
// the non-DD path without wallet DD bookkeeping.
// =============================================================================
BOOST_AUTO_TEST_CASE(rh59_02b_preset_rejects_wallet_dd_mint_non_change_without_sidecar_lock)
{
    m_wallet.EnsureDDWallet();
    DigiDollarWallet* dd_wallet = m_wallet.GetDDWallet();
    BOOST_REQUIRE(dd_wallet != nullptr);

    CKey sigkey;
    sigkey.MakeNewKey(true);
    const CPubKey pub = sigkey.GetPubKey();
    CScript spend_script = GetScriptForDestination(WitnessV0KeyHash(pub.GetID()));

    CMutableTransaction dd_mint;
    dd_mint.SetDigiDollarType(DD_TX_MINT);
    dd_mint.vin.resize(1);
    dd_mint.vin[0].prevout = COutPoint(uint256::ONE, 0);
    dd_mint.vout.resize(4);
    dd_mint.vout[0] = CTxOut(500 * COIN, spend_script); // collateral
    dd_mint.vout[1] = CTxOut(0, spend_script);          // DD token placeholder
    dd_mint.vout[2] = CTxOut(0, CScript() << OP_RETURN << std::vector<unsigned char>{'D', 'D'});
    dd_mint.vout[3] = CTxOut(1 * COIN, spend_script);   // ordinary DGB change

    CTransactionRef tx = MakeTransactionRef(std::move(dd_mint));
    const COutPoint collateral(tx->GetHash(), 0);
    const COutPoint change(tx->GetHash(), 3);

    {
        LOCK(m_wallet.cs_wallet);
        m_wallet.AddToWallet(tx, TxStateInMempool{});
    }

    BOOST_REQUIRE(!dd_wallet->IsLockedByDD(collateral));
    BOOST_REQUIRE(!dd_wallet->IsLockedByDD(change));

    CCoinControl coin_control;
    coin_control.Select(collateral);
    coin_control.SetInputWeight(collateral, 272);

    FastRandomContext rng_fast;
    CoinSelectionParams csp{rng_fast};
    csp.m_effective_feerate = CFeeRate(1000);
    csp.m_long_term_feerate = CFeeRate(1000);
    csp.m_discard_feerate   = CFeeRate(1000);

    util::Result<PreSelectedInputs> res = [&]() EXCLUSIVE_LOCKS_REQUIRED(m_wallet.cs_wallet) {
        LOCK(m_wallet.cs_wallet);
        return FetchSelectedInputs(m_wallet, coin_control, csp);
    }();

    BOOST_CHECK_MESSAGE(!res.has_value(),
        "DD-FA-SEC-026: preset-input path accepted wallet-owned DD mint "
        "collateral when the DD sidecar had no lock state");

    // Sanity: the ordinary DGB change output from the same DD mint remains
    // manually selectable. The guard must reject only the protocol-owned
    // collateral/token/metadata outputs.
    CCoinControl change_control;
    change_control.Select(change);
    change_control.SetInputWeight(change, 272);
    util::Result<PreSelectedInputs> change_res = [&]() EXCLUSIVE_LOCKS_REQUIRED(m_wallet.cs_wallet) {
        LOCK(m_wallet.cs_wallet);
        return FetchSelectedInputs(m_wallet, change_control, csp);
    }();
    BOOST_CHECK_MESSAGE(change_res.has_value(),
        "DD mint DGB change output should remain spendable through ordinary "
        "wallet coin-control paths");
}

// =============================================================================
// RH-59-03: Regression anchor for the AvailableCoins defence
// =============================================================================
//
// The non-preset ("auto-select") path does consult IsLockedCoin when
// `skip_locked` is true. Pin the default so a future refactor that weakens
// this gate is caught. Does not hit the exploit; purely defensive.
// =============================================================================
BOOST_AUTO_TEST_CASE(rh59_03_auto_select_skip_locked_default)
{
    CoinFilterParams params;
    BOOST_CHECK_MESSAGE(params.skip_locked == true,
        "CoinFilterParams::skip_locked default must remain true -- it is "
        "the only thing keeping DD-locked UTXOs out of auto coin "
        "selection. If this flips to false, the preset-input bypass "
        "described in RH-59-01/02 becomes the default behaviour for "
        "ordinary `sendtoaddress` calls too.");
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace wallet
