// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// =============================================================================
// Wave 16 Agent B - Wallet Persistence, Restore, Rescan, Encryption, Legacy
// =============================================================================
//
// Strengthens DD wallet test coverage for the Wave 16 brief:
//   1. Restore-from-DB: wallet restart reconstructs DD positions, balances,
//      DD UTXOs, and stored owner / address keys.
//   2. Crash between broadcast and key persistence: StoreOwnerKey writes the
//      owner key to disk synchronously BEFORE CommitTransaction, so a crash
//      between broadcast and post-broadcast bookkeeping cannot orphan a DD
//      mint position.
//   3. Encrypted wallet lock/unlock: GetOwnerKey / GetAddressKey return false
//      for a crypted-only path while wallet is locked, but still succeed for
//      surviving plaintext entries (encrypted-then-restart upgrade contract).
//   4. Watch-only display: WALLET_FLAG_DISABLE_PRIVATE_KEYS blocks every
//      DigiDollarWallet spend entry point (mint / transfer / redeem) but
//      positions, balances, and UTXOs added through validated paths remain
//      visible for monitoring.
//   5. Legacy wallet rejection: a DD spend on a wallet without DD owner /
//      address keys (the "legacy / no-DD-key" failure mode) returns a clean
//      false without a stack trace, so the RPC layer can map it to a clear
//      error string.
//
// All cases are written as ordinary BasicTestingSetup-style wallet tests,
// using the existing WalletTestingSetup fixture from the rh59 / RH-08 suites.
// They run inside test_digibyte (registered via src/Makefile.test.include
// alongside digidollar_persistence_wallet_tests.cpp).

#include <boost/test/unit_test.hpp>

#include <wallet/wallet.h>
#include <wallet/digidollarwallet.h>
#include <wallet/walletdb.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>
#include <wallet/walletutil.h>

#include <base58.h>
#include <oracle/mock_oracle.h>
#include <hash.h>
#include <key.h>
#include <key_io.h>
#include <random.h>
#include <script/script.h>
#include <script/standard.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/time.h>

namespace wallet {

BOOST_FIXTURE_TEST_SUITE(digidollar_wave16_persistence_tests, WalletTestingSetup)

// -----------------------------------------------------------------------------
// Helper: build a regtest CDigiDollarAddress whose ToString() returns a stable
// 'RD' prefix. This avoids the "test_addr_<hash>" fallback path used by
// CDigiDollarAddress with empty data.
// -----------------------------------------------------------------------------
static CDigiDollarAddress MakeRegtestDDAddress(const std::string& payload)
{
    return CDigiDollarAddress("RD1q" + payload);
}

// =============================================================================
// W16-01: Restore-from-DB rebuilds collateral positions and DD UTXOs
// =============================================================================
//
// Pins the contract that a fresh DigiDollarWallet bound to an existing
// CWallet's database recovers BOTH collateral positions AND the DD token
// UTXOs that were tracked alongside them. Without the UTXO restore, an
// otherwise-restored wallet would show 0 spendable DD even though the on-chain
// DD tokens still exist.
//
// IMPORTANT: production RPC code at src/rpc/digidollar.cpp:1359-1370 does
// the persistence in two steps — AddDDUTXO() updates the in-memory map and
// WalletBatch::WriteDDUTXO() persists to disk. AddDDUTXO() itself is a
// memory-only helper. This test mirrors the production order. See
// "DD-FA-TEST-025 (downgraded)" in the report for why an "AddDDUTXO writes
// to disk" expectation was rejected.

BOOST_AUTO_TEST_CASE(w16_01_restart_restores_positions_and_dd_utxos)
{
    uint256 pos_id = uint256S(
        "0xfeed16010000000000000000000000000000000000000000000000000000feed");
    COutPoint dd_token_outpoint(pos_id, 1);
    const CAmount minted = 25000;          // $250.00
    const CAmount collateral = 12345678;   // arbitrary DGB sats

    // Stage 1: write a position + DD UTXO via the same two-step pattern the
    // RPC mint flow uses (in-memory + on-disk).
    {
        DigiDollarWallet dd_wallet(&m_wallet);
        WalletCollateralPosition pos(pos_id, minted, collateral, /*tier=*/3,
                                     /*unlock_height=*/100000);
        BOOST_REQUIRE(dd_wallet.WriteDDTimeLock(pos));

        dd_wallet.AddDDUTXO(dd_token_outpoint, minted);
        {
            WalletBatch batch(m_wallet.GetDatabase());
            BOOST_REQUIRE(batch.WriteDDUTXO(dd_token_outpoint, minted));
        }

        BOOST_CHECK_EQUAL(dd_wallet.GetPositionCount(), 1u);
        BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), collateral);
        BOOST_CHECK(dd_wallet.HasDDUTXO(dd_token_outpoint));
        BOOST_CHECK_EQUAL(dd_wallet.GetDDFromUTXO(dd_token_outpoint), minted);
    }

    // Stage 2: simulate a wallet restart by constructing a fresh
    // DigiDollarWallet against the same CWallet database. The constructor
    // calls LoadFromDatabase(), which must restore both maps.
    {
        DigiDollarWallet restored(&m_wallet);
        BOOST_CHECK_EQUAL(restored.GetPositionCount(), 1u);
        BOOST_CHECK_EQUAL(restored.GetLockedCollateral(), collateral);

        BOOST_CHECK_MESSAGE(
            restored.HasDDUTXO(dd_token_outpoint),
            "DD-FA-TEST-019: restart did not restore DD token UTXO from "
            "database — spendable DD balance would silently drop to 0");
        BOOST_CHECK_EQUAL(restored.GetDDFromUTXO(dd_token_outpoint), minted);

        auto positions = restored.GetDDTimeLocks(/*active_only=*/true);
        BOOST_REQUIRE_EQUAL(positions.size(), 1u);
        BOOST_CHECK_EQUAL(positions[0].dd_minted, minted);
        BOOST_CHECK_EQUAL(positions[0].dgb_collateral, collateral);
    }
}

// =============================================================================
// W16-01b: AddDDUTXO is in-memory only — persistence is the caller's job
// =============================================================================
//
// Documents the production contract that AddDDUTXO() is a pure in-memory
// update. Persistence requires the caller to also invoke
// WalletBatch::WriteDDUTXO. Pin this so a future "make AddDDUTXO write to
// disk" change does not silently double-write while the RPC layer still
// performs its own WriteDDUTXO.
//
// If/when the call sites are refactored to put persistence inside
// AddDDUTXO, flip this assertion to BOOST_CHECK(restored.HasDDUTXO(...)).

BOOST_AUTO_TEST_CASE(w16_01b_add_dd_utxo_alone_does_not_persist)
{
    uint256 pos_id;
    GetRandBytes(pos_id);
    COutPoint outpoint(pos_id, 1);
    const CAmount amount = 9999;

    {
        DigiDollarWallet dd_wallet(&m_wallet);
        dd_wallet.AddDDUTXO(outpoint, amount);
        BOOST_REQUIRE(dd_wallet.HasDDUTXO(outpoint));
    }

    DigiDollarWallet restored(&m_wallet);
    BOOST_CHECK_MESSAGE(!restored.HasDDUTXO(outpoint),
        "Contract change: AddDDUTXO now persists by itself. Update the W16 "
        "report so callers can stop double-writing via WalletBatch.");
}

// =============================================================================
// W16-02: Restart restores stored owner keys (mint signing path)
// =============================================================================
//
// Pins the contract that owner keys persisted by StoreOwnerKey survive a
// wallet restart and remain retrievable via GetOwnerKey on a fresh
// DigiDollarWallet instance. Without this, a redemption after restart would
// fall into the "no owner key" branch in RedeemDigiDollar and either fail or
// silently use a freshly-generated key the consensus path cannot match.

BOOST_AUTO_TEST_CASE(w16_02_restart_restores_owner_and_address_keys)
{
    CKey owner_key;
    owner_key.MakeNewKey(true);
    uint256 timelock_id;
    GetRandBytes(timelock_id);

    CKey addr_key;
    addr_key.MakeNewKey(true);
    XOnlyPubKey output_key(addr_key.GetPubKey());

    {
        DigiDollarWallet dd_wallet(&m_wallet);
        dd_wallet.StoreOwnerKey(timelock_id, owner_key);
        dd_wallet.StoreAddressKey(output_key, addr_key);

        CKey check;
        BOOST_REQUIRE(dd_wallet.GetOwnerKey(timelock_id, check));
        BOOST_CHECK(check.GetPubKey() == owner_key.GetPubKey());
        BOOST_REQUIRE(dd_wallet.GetAddressKey(output_key, check));
        BOOST_CHECK(check.GetPubKey() == addr_key.GetPubKey());
    }

    // Fresh DigiDollarWallet on the same CWallet -> must reload both keys.
    DigiDollarWallet restored(&m_wallet);
    CKey reloaded;
    BOOST_CHECK_MESSAGE(restored.GetOwnerKey(timelock_id, reloaded),
        "DD-FA-TEST-020: wallet restart lost the persisted DD owner key — "
        "redeemdigidollar after restart would fall back to a new random key "
        "and fail consensus");
    BOOST_CHECK(reloaded.GetPubKey() == owner_key.GetPubKey());

    BOOST_REQUIRE(restored.GetAddressKey(output_key, reloaded));
    BOOST_CHECK(reloaded.GetPubKey() == addr_key.GetPubKey());
}

// =============================================================================
// W16-03: Owner key is on disk before broadcast bookkeeping runs
// =============================================================================
//
// Mirrors the production order in src/rpc/digidollar.cpp:1305-1361 — the RPC
// flow stores the owner key via DigiDollarWallet::StoreOwnerKey BEFORE
// pwallet->chain().broadcastTransaction(...) is called and BEFORE the
// post-broadcast WalletCollateralPosition / AddDDUTXO bookkeeping runs.
//
// This test simulates a crash WHERE WE BLEW UP between broadcast and the
// post-broadcast bookkeeping (i.e. AddCollateralPosition and AddDDUTXO never
// ran). On the next start we should still be able to recover the owner key
// from disk and use it for any redemption / spend that is later reconciled
// from the on-chain state via rescan.

BOOST_AUTO_TEST_CASE(w16_03_crash_after_broadcast_keeps_owner_key_on_disk)
{
    CKey owner_key;
    owner_key.MakeNewKey(true);
    uint256 timelock_id;
    GetRandBytes(timelock_id);

    // Stage 1: pre-broadcast persistence. Production order:
    //   pwallet->GetDDWallet()->StoreOwnerKey(positionId, ownerKey);
    //   pwallet->chain().broadcastTransaction(tx, ...);
    //   pwallet->CommitTransaction(tx, {}, {});
    //   pwallet->GetDDWallet()->AddCollateralPosition(position);
    //   pwallet->GetDDWallet()->AddDDUTXO(ddOutpoint, ddAmount);
    {
        DigiDollarWallet dd_wallet(&m_wallet);
        dd_wallet.StoreOwnerKey(timelock_id, owner_key);

        // Verify the key was actually written to the wallet database (not
        // just the in-memory map). This is the property a crash would rely
        // on: synchronous batch write before the broadcast call returns.
        WalletBatch batch(m_wallet.GetDatabase());
        CKey on_disk;
        BOOST_REQUIRE_MESSAGE(batch.ReadDDOwnerKey(timelock_id, on_disk),
            "DD-FA-TEST-021: StoreOwnerKey did not synchronously persist "
            "the DD owner key — a crash between broadcast and the post-"
            "broadcast bookkeeping would orphan the position permanently");
        BOOST_CHECK(on_disk.GetPubKey() == owner_key.GetPubKey());
    }

    // Stage 2: the "crash" happened — we never got to AddCollateralPosition
    // or AddDDUTXO. Spin up a fresh DigiDollarWallet (simulating a node
    // restart) and confirm the owner key is still recoverable for the later
    // rescan-driven reconciliation path.
    DigiDollarWallet recovered(&m_wallet);
    CKey reloaded;
    BOOST_CHECK_MESSAGE(recovered.GetOwnerKey(timelock_id, reloaded),
        "DD-FA-TEST-021: the owner key was lost across the simulated crash; "
        "this is the loss-of-funds path the pre-broadcast persistence is "
        "supposed to prevent");
    BOOST_CHECK(reloaded.GetPubKey() == owner_key.GetPubKey());

    // The collateral position itself was NEVER persisted (post-broadcast),
    // so the wallet correctly reports zero positions until a rescan picks
    // up the on-chain mint and re-adds it. Confirm we don't have a phantom
    // half-recorded position.
    BOOST_CHECK_EQUAL(recovered.GetPositionCount(), 0u);
    BOOST_CHECK_EQUAL(recovered.GetLockedCollateral(), 0);
}

// =============================================================================
// W16-04: Encrypted wallet locked path returns false for crypted-only key
// =============================================================================
//
// Pins the encrypted-wallet contract for the path that matters in production:
// the wallet was encrypted (so plaintext maps were wiped and only encrypted
// entries remain) and is currently locked. GetOwnerKey / GetAddressKey must
// refuse to return a key in that state.
//
// Note: a key that was stored as plaintext BEFORE the wallet was encrypted is
// still returned by GetOwnerKey because the in-memory map survives until
// EncryptDDKeys() runs and clears it. RH-08-01 already pins that EncryptDDKeys
// erases the plaintext rows from disk; this test pins the post-encryption /
// locked refusal path instead, simulated by calling EncryptDDKeys ourselves
// and then directly toggling the underlying CWallet to a locked / crypted
// state through CWallet::EncryptWallet (the production entry point).

BOOST_AUTO_TEST_CASE(w16_04_encrypted_locked_wallet_blocks_dd_key_decryption)
{
    // Wire the DD wallet through CWallet so EncryptWallet() sees it and runs
    // the integrated encryption path that mirrors production.
    m_wallet.EnsureDDWallet();
    DigiDollarWallet* dd_wallet = m_wallet.GetDDWallet();
    BOOST_REQUIRE(dd_wallet != nullptr);

    CKey owner_key;
    owner_key.MakeNewKey(true);
    uint256 timelock_id;
    GetRandBytes(timelock_id);
    dd_wallet->StoreOwnerKey(timelock_id, owner_key);

    CKey addr_key;
    addr_key.MakeNewKey(true);
    XOnlyPubKey output_key(addr_key.GetPubKey());
    dd_wallet->StoreAddressKey(output_key, addr_key);

    // CWallet::EncryptWallet will call DigiDollarWallet::EncryptDDKeys()
    // and Lock() on the way out, leaving the wallet crypted + locked.
    SecureString passphrase{"wave16-locked-passphrase"};
    BOOST_REQUIRE(m_wallet.EncryptWallet(passphrase));
    BOOST_REQUIRE(m_wallet.IsCrypted());
    BOOST_REQUIRE(m_wallet.IsLocked());

    {
        CKey reject;
        BOOST_CHECK_MESSAGE(!dd_wallet->GetOwnerKey(timelock_id, reject),
            "DD-FA-TEST-022: locked encrypted wallet returned a DD owner "
            "key — every DD spend path would silently work without a "
            "passphrase prompt");
        BOOST_CHECK_MESSAGE(!dd_wallet->GetAddressKey(output_key, reject),
            "DD-FA-TEST-022: locked encrypted wallet returned a DD address "
            "key — received-DD spends would silently work without a "
            "passphrase prompt");
    }

    // Sanity: after unlocking, the keys must come back so the wallet is
    // actually usable (not silently bricked by the encryption flow).
    BOOST_REQUIRE(m_wallet.Unlock(passphrase));
    {
        CKey reloaded;
        BOOST_CHECK(dd_wallet->GetOwnerKey(timelock_id, reloaded));
        BOOST_CHECK(reloaded.GetPubKey() == owner_key.GetPubKey());
        BOOST_CHECK(dd_wallet->GetAddressKey(output_key, reloaded));
        BOOST_CHECK(reloaded.GetPubKey() == addr_key.GetPubKey());
    }
}

BOOST_AUTO_TEST_CASE(w16_04b_encrypted_locked_redeem_does_not_use_fallback_owner_key)
{
    m_wallet.EnsureDDWallet();
    DigiDollarWallet* dd_wallet = m_wallet.GetDDWallet();
    BOOST_REQUIRE(dd_wallet != nullptr);

    CKey owner_key;
    owner_key.MakeNewKey(true);
    const uint256 pos_id = uint256S(
        "0x1604b0000000000000000000000000000000000000000000000000000000000aa");
    dd_wallet->StoreOwnerKey(pos_id, owner_key);
    dd_wallet->AddCollateralPosition(WalletCollateralPosition(
        pos_id, /*dd_minted=*/10000, /*dgb_collateral=*/300 * COIN,
        /*lock_tier=*/1, /*unlock_height=*/1));
    dd_wallet->AddDDUTXO(COutPoint(pos_id, 1), 10000);
    WITH_LOCK(m_wallet.cs_wallet, m_wallet.SetLastBlockProcessed(1, uint256::ZERO));

    auto& mock = MockOracleManager::GetInstance();
    const bool old_enabled = mock.IsEnabled();
    const CAmount old_price = mock.GetCurrentPrice();
    mock.SetEnabled(true);
    mock.SetMockPrice(1000000, /*update_height=*/1);

    SecureString passphrase{"wave16-redeem-locked-passphrase"};
    BOOST_REQUIRE(m_wallet.EncryptWallet(passphrase));
    BOOST_REQUIRE(m_wallet.IsCrypted());
    BOOST_REQUIRE(m_wallet.IsLocked());

    CTransactionRef out_tx;
    BOOST_CHECK_MESSAGE(
        !dd_wallet->RedeemDigiDollar(pos_id, /*amount=*/10000, out_tx),
        "locked encrypted redeem must stop at unlock-needed owner-key failure, "
        "not continue with a freshly generated fallback key");
    BOOST_CHECK(out_tx == nullptr);

    mock.SetMockPrice(old_price, /*update_height=*/1);
    mock.SetEnabled(old_enabled);
}

// =============================================================================
// W16-05: Watch-only wallet blocks DD spend entry points
// =============================================================================
//
// Pins that WALLET_FLAG_DISABLE_PRIVATE_KEYS (the flag set by
// `createwallet ... disable_private_keys=true`) blocks every DigiDollarWallet
// spend entry point. The corresponding production checks live at:
//   src/wallet/digidollarwallet.cpp:4237  (MintDigiDollar)
//   src/wallet/digidollarwallet.cpp:4312  (TransferDigiDollar)
//   src/wallet/digidollarwallet.cpp:4693  (RedeemDigiDollar)

BOOST_AUTO_TEST_CASE(w16_05_watch_only_blocks_dd_mint_transfer_redeem)
{
    {
        LOCK(m_wallet.cs_wallet);
        m_wallet.SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        BOOST_REQUIRE(m_wallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    }

    DigiDollarWallet dd_wallet(&m_wallet);

    CTransactionRef out_tx;

    BOOST_CHECK_MESSAGE(
        !dd_wallet.MintDigiDollar(/*dd_amount=*/10000, /*lock_tier=*/1, out_tx),
        "DD-FA-TEST-023: MintDigiDollar succeeded on a wallet with "
        "WALLET_FLAG_DISABLE_PRIVATE_KEYS — watch-only wallets must not "
        "be able to mint");

    CDigiDollarAddress to = MakeRegtestDDAddress(
        "wave16watchonlytransferaddrabcdefghij");
    BOOST_CHECK_MESSAGE(
        !dd_wallet.TransferDigiDollar(to, /*amount=*/1000, out_tx),
        "DD-FA-TEST-023: TransferDigiDollar succeeded on a wallet with "
        "WALLET_FLAG_DISABLE_PRIVATE_KEYS — watch-only wallets must not "
        "be able to send DD");

    uint256 fake_position_id;
    GetRandBytes(fake_position_id);
    BOOST_CHECK_MESSAGE(
        !dd_wallet.RedeemDigiDollar(fake_position_id, /*amount=*/1000, out_tx),
        "DD-FA-TEST-023: RedeemDigiDollar succeeded on a wallet with "
        "WALLET_FLAG_DISABLE_PRIVATE_KEYS — watch-only wallets must not "
        "be able to redeem DD");
}

// =============================================================================
// W16-06: Watch-only wallet still displays positions and balances
// =============================================================================
//
// Read-only DD surface remains visible on a watch-only wallet:
//   - WriteDDTimeLock + AddDDUTXO + WriteDDBalance succeed (they only touch
//     wallet bookkeeping, not the spend path).
//   - GetDDTimeLocks, HasDDUTXO, and GetDDBalance return the stored data.
// This is the contract that lets monitoring wallets watch a hot wallet's
// DD positions without holding spend-capable keys.

BOOST_AUTO_TEST_CASE(w16_06_watch_only_can_see_positions_and_balances)
{
    {
        LOCK(m_wallet.cs_wallet);
        m_wallet.SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    }

    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 pos_id = uint256S(
        "0xa11ce16060000000000000000000000000000000000000000000000000000a11");
    WalletCollateralPosition pos(pos_id, /*dd_minted=*/40000,
                                 /*dgb_collateral=*/55555555,
                                 /*lock_tier=*/2, /*unlock_height=*/200000);
    BOOST_REQUIRE(dd_wallet.WriteDDTimeLock(pos));

    COutPoint dd_token(pos_id, 1);
    dd_wallet.AddDDUTXO(dd_token, 40000);

    CDigiDollarAddress addr = MakeRegtestDDAddress(
        "wave16watchonlyobservedbalanceabcdef");
    BOOST_REQUIRE(dd_wallet.WriteDDBalance(addr, 12345));

    BOOST_CHECK_EQUAL(dd_wallet.GetPositionCount(), 1u);
    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 55555555);
    BOOST_CHECK(dd_wallet.HasDDUTXO(dd_token));
    BOOST_CHECK_EQUAL(dd_wallet.GetDDFromUTXO(dd_token), 40000);
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr), 12345);

    auto positions = dd_wallet.GetDDTimeLocks(/*active_only=*/true);
    BOOST_REQUIRE_EQUAL(positions.size(), 1u);
    BOOST_CHECK_EQUAL(positions[0].dd_minted, 40000);
    BOOST_CHECK_EQUAL(positions[0].dgb_collateral, 55555555);
}

// =============================================================================
// W16-07: Legacy / no-key wallet rejects redemption cleanly
// =============================================================================
//
// Pins the "no DD owner key for this position" failure path that a legacy
// wallet (one that has never minted DD itself, e.g. a freshly-restored wallet
// that did not also restore the dd_owner_keys row) would hit if it attempted
// to redeem. We also confirm that GetOwnerKey returns false for an unknown
// position so the RPC layer can surface a clean error rather than relying on
// the silent fallback at digidollarwallet.cpp:4729 generating a garbage key.

BOOST_AUTO_TEST_CASE(w16_07_legacy_wallet_no_owner_key_redeem_returns_false)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    // No keys, no positions — emulates a legacy / restored wallet that did
    // not import DD owner keys.
    uint256 unknown_position;
    GetRandBytes(unknown_position);

    CKey not_present;
    BOOST_CHECK(!dd_wallet.GetOwnerKey(unknown_position, not_present));

    CTransactionRef out_tx;
    BOOST_CHECK_MESSAGE(
        !dd_wallet.RedeemDigiDollar(unknown_position, /*amount=*/1000, out_tx),
        "DD-FA-TEST-024: RedeemDigiDollar returned true for a position the "
        "wallet has never seen — silent fallback to a generated key would "
        "produce an unsignable redemption that consensus rejects");
}

// =============================================================================
// W16-08: Address-key restore succeeds across wallet restart
// =============================================================================
//
// Belt-and-suspenders sibling of W16-02 specifically focused on received-DD
// keys: a freshly-restarted wallet must reload dd_address_keys so any
// received DD remains spendable. Pins the contract so an accidental change to
// LoadDDAddressKeys (e.g. moving it to a lazy / on-demand load) does not
// silently break received-DD spends after a restart.

BOOST_AUTO_TEST_CASE(w16_08_address_key_restore_count_matches_stored_count)
{
    constexpr int kCount = 5;

    std::vector<std::pair<XOnlyPubKey, CKey>> stored;
    {
        DigiDollarWallet dd_wallet(&m_wallet);
        for (int i = 0; i < kCount; ++i) {
            CKey k;
            k.MakeNewKey(true);
            XOnlyPubKey ok(k.GetPubKey());
            dd_wallet.StoreAddressKey(ok, k);
            stored.emplace_back(ok, k);
        }
    }

    DigiDollarWallet restored(&m_wallet);
    BOOST_CHECK_GE(restored.LoadDDAddressKeys(), static_cast<size_t>(kCount));

    for (const auto& [ok, expected] : stored) {
        CKey reloaded;
        BOOST_REQUIRE(restored.GetAddressKey(ok, reloaded));
        BOOST_CHECK(reloaded.GetPubKey() == expected.GetPubKey());
    }
}

// =============================================================================
// W16-09: ClearWalletData drops all DD key caches
// =============================================================================
//
// ClearWalletData is used by DD wallet test helpers to simulate wallet state
// loss. It must not leave private DD address-key material in memory after a
// clear, otherwise restore/rescan tests can accidentally pass with stale keys
// that were not recovered from wallet state.

BOOST_AUTO_TEST_CASE(w16_09_clear_wallet_data_drops_dd_key_caches)
{
    CKey owner_key;
    owner_key.MakeNewKey(true);
    uint256 timelock_id;
    GetRandBytes(timelock_id);

    CKey addr_key;
    addr_key.MakeNewKey(true);
    XOnlyPubKey output_key(addr_key.GetPubKey());

    DigiDollarWallet dd_wallet(&m_wallet);
    dd_wallet.StoreOwnerKey(timelock_id, owner_key);
    dd_wallet.StoreAddressKey(output_key, addr_key);

    CKey check;
    BOOST_REQUIRE(dd_wallet.GetOwnerKey(timelock_id, check));
    BOOST_REQUIRE(dd_wallet.GetAddressKey(output_key, check));

    dd_wallet.ClearWalletData();

    BOOST_CHECK(!dd_wallet.GetOwnerKey(timelock_id, check));
    BOOST_CHECK_MESSAGE(!dd_wallet.GetAddressKey(output_key, check),
        "ClearWalletData left a DD address key cache populated after clear");
}

// =============================================================================
// W16-10: Wallet encryption protects oracle signing keys
// =============================================================================
//
// Oracle keys are DigiDollar signing keys too. After wallet encryption, the
// raw ORACLE_KEY database row must be gone and GetOracleKey() must require an
// unlocked wallet before returning key material.

BOOST_AUTO_TEST_CASE(w16_10_wallet_encryption_protects_oracle_keys)
{
    constexpr uint32_t oracle_id = 0;

    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    BOOST_REQUIRE(m_wallet.StoreOracleKey(oracle_id, oracle_key));

    {
        WalletBatch batch(m_wallet.GetDatabase());
        CKey plaintext;
        BOOST_REQUIRE(batch.ReadOracleKey(oracle_id, plaintext));
        BOOST_CHECK(plaintext.GetPubKey() == oracle_key.GetPubKey());
    }

    SecureString passphrase{"wave16-oracle-passphrase"};
    BOOST_REQUIRE(m_wallet.EncryptWallet(passphrase));
    BOOST_REQUIRE(m_wallet.IsCrypted());
    BOOST_REQUIRE(m_wallet.IsLocked());

    {
        WalletBatch batch(m_wallet.GetDatabase());
        CKey leaked;
        BOOST_CHECK_MESSAGE(!batch.ReadOracleKey(oracle_id, leaked),
            "wallet encryption left the plaintext ORACLE_KEY row readable");
    }

    CKey locked_key;
    BOOST_CHECK_MESSAGE(!m_wallet.GetOracleKey(oracle_id, locked_key),
        "locked encrypted wallet returned an oracle signing key");

    BOOST_REQUIRE(m_wallet.Unlock(passphrase));
    CKey restored;
    BOOST_REQUIRE(m_wallet.GetOracleKey(oracle_id, restored));
    BOOST_CHECK(restored.GetPubKey() == oracle_key.GetPubKey());

    constexpr uint32_t post_encryption_oracle_id = 1;
    CKey post_encryption_key;
    post_encryption_key.MakeNewKey(true);
    BOOST_REQUIRE(m_wallet.StoreOracleKey(post_encryption_oracle_id, post_encryption_key));
    {
        WalletBatch batch(m_wallet.GetDatabase());
        CKey leaked;
        BOOST_CHECK_MESSAGE(!batch.ReadOracleKey(post_encryption_oracle_id, leaked),
            "oracle key created after wallet encryption was stored as plaintext");
    }

    m_wallet.Lock();
    CKey locked_again;
    BOOST_CHECK(!m_wallet.GetOracleKey(oracle_id, locked_again));
    BOOST_CHECK(!m_wallet.GetOracleKey(post_encryption_oracle_id, locked_again));

    BOOST_REQUIRE(m_wallet.Unlock(passphrase));
    CKey restored_post_encryption;
    BOOST_REQUIRE(m_wallet.GetOracleKey(post_encryption_oracle_id, restored_post_encryption));
    BOOST_CHECK(restored_post_encryption.GetPubKey() == post_encryption_key.GetPubKey());
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace wallet
