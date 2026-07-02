// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <wallet/wallet.h>
#include <wallet/digidollarwallet.h>
#include <wallet/walletdb.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>
#include <base58.h>
#include <hash.h>
#include <util/time.h>
#include <util/strencodings.h>
#include <streams.h>
#include <test/util/setup_common.h>

namespace wallet {

/**
 * Helper: derive the database key for a CDigiDollarAddress the same way
 * DigiDollarWallet::WriteDDBalance does (Hash-based fallback for test addresses
 * whose ToString() returns empty).
 */
static std::string DDBalanceKey(const CDigiDollarAddress& addr)
{
    std::string key = addr.ToString();
    if (key.empty()) {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << addr;
        uint256 hash = Hash(ss);
        key = "test_addr_" + hash.GetHex();
    }
    return key;
}

BOOST_FIXTURE_TEST_SUITE(digidollar_persistence_wallet_tests, WalletTestingSetup)

// =============================================================================
// PHASE 3 TASK 3.1: WriteDDBalance Tests
// =============================================================================

BOOST_AUTO_TEST_CASE(digidollarwallet_write_balance_persists)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    CDigiDollarAddress addr("DD1qtest123456789abcdefghijklmnopqrstuvwxyz");
    CAmount balance = 10000; // $100.00

    BOOST_CHECK(dd_wallet.WriteDDBalance(addr, balance));

    // Verify written to database using the same key derivation as the impl
    WalletBatch batch(m_wallet.GetDatabase());
    WalletDDBalance read_balance;
    std::string key = DDBalanceKey(addr);
    BOOST_CHECK(batch.ReadDDBalance(key, read_balance));
    BOOST_CHECK_EQUAL(read_balance.balance, balance);
    BOOST_CHECK(read_balance.last_updated > 0);

    // Verify in-memory cache updated
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr), balance);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_balance_updates_total)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    CDigiDollarAddress addr1("DD1qtest111111111111111111111111111111111");
    CDigiDollarAddress addr2("DD1qtest222222222222222222222222222222222");

    BOOST_CHECK(dd_wallet.WriteDDBalance(addr1, 5000));
    BOOST_CHECK(dd_wallet.WriteDDBalance(addr2, 3000));

    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr1), 5000);
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr2), 3000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_balance_updates_existing)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    CDigiDollarAddress addr("DD1qtest333333333333333333333333333333333");

    BOOST_CHECK(dd_wallet.WriteDDBalance(addr, 1000));
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr), 1000);

    BOOST_CHECK(dd_wallet.WriteDDBalance(addr, 2000));
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr), 2000);

    // Verify database has updated value
    WalletBatch batch(m_wallet.GetDatabase());
    WalletDDBalance read_balance;
    std::string key = DDBalanceKey(addr);
    BOOST_CHECK(batch.ReadDDBalance(key, read_balance));
    BOOST_CHECK_EQUAL(read_balance.balance, 2000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_balance_persists_total_metadata)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    CDigiDollarAddress addr1("DD1qtest444444444444444444444444444444444");
    CDigiDollarAddress addr2("DD1qtest555555555555555555555555555555555");

    BOOST_CHECK(dd_wallet.WriteDDBalance(addr1, 7500));
    BOOST_CHECK(dd_wallet.WriteDDBalance(addr2, 2500));

    WalletBatch batch(m_wallet.GetDatabase());
    std::string total_str;
    BOOST_CHECK(batch.ReadDDMetadata("total_dd_balance", total_str));

    CAmount total = std::stoll(total_str);
    BOOST_CHECK_EQUAL(total, 10000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_balance_empty_address_uses_test_key)
{
    // The implementation does NOT reject empty addresses — it generates a
    // hash-based test key so mock addresses work.  Verify that behaviour.
    DigiDollarWallet dd_wallet(&m_wallet);

    CDigiDollarAddress empty_addr;
    BOOST_CHECK(dd_wallet.WriteDDBalance(empty_addr, 1000));
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(empty_addr), 1000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_balance_negative_amount)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    CDigiDollarAddress addr("DD1qtest666666666666666666666666666666666");

    BOOST_CHECK(!dd_wallet.WriteDDBalance(addr, -1000));
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_balance_zero_amount)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    CDigiDollarAddress addr("DD1qtest777777777777777777777777777777777");

    BOOST_CHECK(dd_wallet.WriteDDBalance(addr, 0));
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr), 0);
}

// =============================================================================
// PHASE 3 TASK 3.2: WriteDDTimeLock Tests
// =============================================================================

BOOST_AUTO_TEST_CASE(digidollarwallet_write_position_persists)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 pos_id = uint256S("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    WalletCollateralPosition pos(pos_id, 10000, 500000, 3, 100000);

    BOOST_CHECK(dd_wallet.WriteDDTimeLock(pos));

    WalletBatch batch(m_wallet.GetDatabase());
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadDDTimeLock(pos_id, read_pos));
    BOOST_CHECK_EQUAL(read_pos.dd_minted, 10000);
    BOOST_CHECK_EQUAL(read_pos.dgb_collateral, 500000);

    auto positions = dd_wallet.GetDDTimeLocks(false);
    BOOST_CHECK_EQUAL(positions.size(), 1);
    BOOST_CHECK_EQUAL(positions[0].dd_minted, 10000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_position_updates_locked_collateral)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 id1 = uint256S("0x1111111111111111111111111111111111111111111111111111111111111111");
    uint256 id2 = uint256S("0x2222222222222222222222222222222222222222222222222222222222222222");

    WalletCollateralPosition pos1(id1, 5000, 250000, 2, 50000);
    WalletCollateralPosition pos2(id2, 3000, 150000, 1, 30000);

    BOOST_CHECK(dd_wallet.WriteDDTimeLock(pos1));
    BOOST_CHECK(dd_wallet.WriteDDTimeLock(pos2));

    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 400000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_position_updates_existing)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 pos_id = uint256S("0x3333333333333333333333333333333333333333333333333333333333333333");

    WalletCollateralPosition pos1(pos_id, 1000, 50000, 1, 10000);
    BOOST_CHECK(dd_wallet.WriteDDTimeLock(pos1));

    auto positions = dd_wallet.GetDDTimeLocks(false);
    BOOST_CHECK_EQUAL(positions.size(), 1);
    BOOST_CHECK_EQUAL(positions[0].dd_minted, 1000);

    WalletCollateralPosition pos2(pos_id, 2000, 100000, 2, 20000);
    BOOST_CHECK(dd_wallet.WriteDDTimeLock(pos2));

    positions = dd_wallet.GetDDTimeLocks(false);
    BOOST_CHECK_EQUAL(positions.size(), 1);
    BOOST_CHECK_EQUAL(positions[0].dd_minted, 2000);
    BOOST_CHECK_EQUAL(positions[0].dgb_collateral, 100000);

    WalletBatch batch(m_wallet.GetDatabase());
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadDDTimeLock(pos_id, read_pos));
    BOOST_CHECK_EQUAL(read_pos.dd_minted, 2000);
    BOOST_CHECK_EQUAL(read_pos.dgb_collateral, 100000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_position_persists_metadata)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 pos_id = uint256S("0x4444444444444444444444444444444444444444444444444444444444444444");
    WalletCollateralPosition pos(pos_id, 7500, 375000, 3, 75000);

    BOOST_CHECK(dd_wallet.WriteDDTimeLock(pos));

    WalletBatch batch(m_wallet.GetDatabase());
    std::string locked_str;
    BOOST_CHECK(batch.ReadDDMetadata("locked_collateral", locked_str));

    CAmount locked = std::stoll(locked_str);
    BOOST_CHECK_EQUAL(locked, 375000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_position_invalid_id)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 null_id;
    WalletCollateralPosition pos(null_id, 1000, 50000, 1, 10000);

    BOOST_CHECK(!dd_wallet.WriteDDTimeLock(pos));
}

BOOST_AUTO_TEST_CASE(digidollarwallet_write_position_inactive_no_locked)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 pos_id = uint256S("0x5555555555555555555555555555555555555555555555555555555555555555");
    WalletCollateralPosition pos(pos_id, 1000, 50000, 1, 10000);
    pos.is_active = false;

    BOOST_CHECK(dd_wallet.WriteDDTimeLock(pos));

    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 0);
}

// =============================================================================
// PHASE 3 TASK 3.3: UpdatePositionStatus Tests
// =============================================================================

BOOST_AUTO_TEST_CASE(digidollarwallet_update_position_status)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 pos_id = uint256S("0x3333333333333333333333333333333333333333333333333333333333333333");
    WalletCollateralPosition pos(pos_id, 10000, 500000, 3, 100000);
    BOOST_REQUIRE(dd_wallet.WriteDDTimeLock(pos));
    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 500000);

    BOOST_CHECK(dd_wallet.UpdatePositionStatus(pos_id, false));

    WalletBatch batch(m_wallet.GetDatabase());
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadDDTimeLock(pos_id, read_pos));
    BOOST_CHECK(!read_pos.is_active);

    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 0);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_update_position_status_multiple)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 id1 = uint256S("0x4444444444444444444444444444444444444444444444444444444444444444");
    uint256 id2 = uint256S("0x5555555555555555555555555555555555555555555555555555555555555555");
    uint256 id3 = uint256S("0x6666666666666666666666666666666666666666666666666666666666666666");

    dd_wallet.WriteDDTimeLock(WalletCollateralPosition(id1, 5000, 250000, 2, 50000));
    dd_wallet.WriteDDTimeLock(WalletCollateralPosition(id2, 3000, 150000, 1, 30000));
    dd_wallet.WriteDDTimeLock(WalletCollateralPosition(id3, 2000, 100000, 1, 20000));

    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 500000);

    dd_wallet.UpdatePositionStatus(id2, false);

    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 350000);

    WalletBatch batch(m_wallet.GetDatabase());
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadDDTimeLock(id2, read_pos));
    BOOST_CHECK(!read_pos.is_active);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_update_position_status_not_found)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 fake_id = uint256S("0x7777777777777777777777777777777777777777777777777777777777777777");
    BOOST_CHECK(!dd_wallet.UpdatePositionStatus(fake_id, false));
}

BOOST_AUTO_TEST_CASE(digidollarwallet_update_position_status_null_id)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 null_id;
    BOOST_CHECK(!dd_wallet.UpdatePositionStatus(null_id, false));
}

BOOST_AUTO_TEST_CASE(digidollarwallet_update_position_status_reactivate)
{
    DigiDollarWallet dd_wallet(&m_wallet);

    uint256 pos_id = uint256S("0x8888888888888888888888888888888888888888888888888888888888888888");
    WalletCollateralPosition pos(pos_id, 10000, 500000, 3, 100000);
    BOOST_REQUIRE(dd_wallet.WriteDDTimeLock(pos));

    BOOST_CHECK(dd_wallet.UpdatePositionStatus(pos_id, false));
    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 0);

    BOOST_CHECK(dd_wallet.UpdatePositionStatus(pos_id, true));
    BOOST_CHECK_EQUAL(dd_wallet.GetLockedCollateral(), 500000);

    WalletBatch batch(m_wallet.GetDatabase());
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadDDTimeLock(pos_id, read_pos));
    BOOST_CHECK(read_pos.is_active);
}

// =============================================================================
// PHASE 4 TASK 4.1: LoadFromDatabase Tests - THE CRITICAL METHOD
// =============================================================================

BOOST_AUTO_TEST_CASE(digidollarwallet_load_from_database_positions)
{
    wallet::WalletBatch batch(m_wallet.GetDatabase());
    uint256 id1 = uint256S("0x8888888888888888888888888888888888888888888888888888888888888888");
    uint256 id2 = uint256S("0x9999999999999999999999999999999999999999999999999999999999999999");

    WalletCollateralPosition pos1(id1, 10000, 500000, 3, 100000);
    WalletCollateralPosition pos2(id2, 5000, 250000, 2, 50000);
    batch.WriteDDTimeLock(pos1);
    batch.WriteDDTimeLock(pos2);

    DigiDollarWallet fresh_wallet(&m_wallet);

    size_t loaded = fresh_wallet.LoadFromDatabase();
    BOOST_CHECK_EQUAL(loaded, 2);

    auto positions = fresh_wallet.GetDDTimeLocks();
    BOOST_CHECK_EQUAL(positions.size(), 2);
    BOOST_CHECK_EQUAL(fresh_wallet.GetLockedCollateral(), 750000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_load_from_database_balances)
{
    // Write balances using the same key derivation as the implementation
    wallet::WalletBatch batch(m_wallet.GetDatabase());

    CDigiDollarAddress addr1("DD1qtest111111111111111111111111111111111");
    CDigiDollarAddress addr2("DD1qtest222222222222222222222222222222222");

    WalletDDBalance bal1(addr1, 10000);
    bal1.last_updated = GetTime();
    WalletDDBalance bal2(addr2, 5000);
    bal2.last_updated = GetTime();

    batch.WriteDDBalance(DDBalanceKey(addr1), bal1);
    batch.WriteDDBalance(DDBalanceKey(addr2), bal2);

    DigiDollarWallet fresh_wallet(&m_wallet);

    size_t loaded = fresh_wallet.LoadFromDatabase();
    BOOST_CHECK_EQUAL(loaded, 2);

    BOOST_CHECK_EQUAL(fresh_wallet.GetBalanceCount(), 2);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_load_from_database_mixed)
{
    wallet::WalletBatch batch(m_wallet.GetDatabase());

    uint256 pos_id = uint256S("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    WalletCollateralPosition pos(pos_id, 10000, 500000, 3, 100000);
    batch.WriteDDTimeLock(pos);

    CDigiDollarAddress addr("DD1qtest333333333333333333333333333333333");
    WalletDDBalance bal(addr, 7500);
    bal.last_updated = GetTime();
    batch.WriteDDBalance(DDBalanceKey(addr), bal);

    DigiDollarWallet fresh_wallet(&m_wallet);

    size_t loaded = fresh_wallet.LoadFromDatabase();
    BOOST_CHECK_EQUAL(loaded, 2);

    BOOST_CHECK_EQUAL(fresh_wallet.GetPositionCount(), 1);
    BOOST_CHECK_EQUAL(fresh_wallet.GetBalanceCount(), 1);
    BOOST_CHECK_EQUAL(fresh_wallet.GetLockedCollateral(), 500000);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_load_from_database_empty)
{
    DigiDollarWallet empty_wallet(&m_wallet);

    size_t loaded = empty_wallet.LoadFromDatabase();
    BOOST_CHECK_EQUAL(loaded, 0);

    BOOST_CHECK_EQUAL(empty_wallet.GetPositionCount(), 0);
    BOOST_CHECK_EQUAL(empty_wallet.GetBalanceCount(), 0);
    BOOST_CHECK_EQUAL(empty_wallet.GetLockedCollateral(), 0);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_load_from_database_recalculates_totals)
{
    wallet::WalletBatch batch(m_wallet.GetDatabase());

    uint256 id1 = uint256S("0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    uint256 id2 = uint256S("0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    uint256 id3 = uint256S("0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");

    WalletCollateralPosition pos1(id1, 10000, 500000, 3, 100000);
    pos1.is_active = true;

    WalletCollateralPosition pos2(id2, 5000, 250000, 2, 50000);
    pos2.is_active = false;

    WalletCollateralPosition pos3(id3, 3000, 150000, 1, 30000);
    pos3.is_active = true;

    batch.WriteDDTimeLock(pos1);
    batch.WriteDDTimeLock(pos2);
    batch.WriteDDTimeLock(pos3);

    DigiDollarWallet fresh_wallet(&m_wallet);
    fresh_wallet.LoadFromDatabase();

    BOOST_CHECK_EQUAL(fresh_wallet.GetLockedCollateral(), 650000);

    auto all_positions = fresh_wallet.GetDDTimeLocks(false);
    BOOST_CHECK_EQUAL(all_positions.size(), 3);

    auto active_positions = fresh_wallet.GetDDTimeLocks(true);
    BOOST_CHECK_EQUAL(active_positions.size(), 2);
}

BOOST_AUTO_TEST_CASE(digidollarwallet_persistence_integration_test)
{
    // THE KEY TEST: Write → Clear → Load → Data restored

    DigiDollarWallet wallet1(&m_wallet);

    uint256 pos_id = uint256S("0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    WalletCollateralPosition pos(pos_id, 10000, 500000, 3, 100000);
    wallet1.WriteDDTimeLock(pos);

    CDigiDollarAddress addr("DD1qtest999999999999999999999999999999999");
    wallet1.WriteDDBalance(addr, 10000);

    BOOST_CHECK_EQUAL(wallet1.GetPositionCount(), 1);
    BOOST_CHECK_EQUAL(wallet1.GetBalanceCount(), 1);
    BOOST_CHECK_EQUAL(wallet1.GetLockedCollateral(), 500000);

    wallet1.ClearWalletData();
    BOOST_CHECK_EQUAL(wallet1.GetPositionCount(), 0);
    BOOST_CHECK_EQUAL(wallet1.GetBalanceCount(), 0);
    BOOST_CHECK_EQUAL(wallet1.GetLockedCollateral(), 0);

    DigiDollarWallet wallet2(&m_wallet);
    size_t loaded = wallet2.LoadFromDatabase();
    BOOST_CHECK_EQUAL(loaded, 2);

    BOOST_CHECK_EQUAL(wallet2.GetPositionCount(), 1);
    BOOST_CHECK_EQUAL(wallet2.GetBalanceCount(), 1);
    BOOST_CHECK_EQUAL(wallet2.GetLockedCollateral(), 500000);

    auto positions = wallet2.GetDDTimeLocks();
    BOOST_CHECK_EQUAL(positions.size(), 1);
    BOOST_CHECK_EQUAL(positions[0].dd_timelock_id, pos_id);
    BOOST_CHECK_EQUAL(positions[0].dd_minted, 10000);
    BOOST_CHECK_EQUAL(positions[0].dgb_collateral, 500000);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace wallet
