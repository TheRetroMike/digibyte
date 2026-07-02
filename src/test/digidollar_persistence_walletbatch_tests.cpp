// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <wallet/walletdb.h>
#include <wallet/digidollarwallet.h>
#include <wallet/test/util.h>
#include <test/util/setup_common.h>
#include <key_io.h>
#include <pubkey.h>
#include <script/standard.h>

BOOST_FIXTURE_TEST_SUITE(digidollar_persistence_walletbatch_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(walletbatch_write_position)
{
    // Create test wallet database
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Create test position
    WalletCollateralPosition pos;
    pos.dd_timelock_id = uint256S("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    pos.dd_minted = 10000;
    pos.dgb_collateral = 500000;
    pos.lock_tier = 3;
    pos.unlock_height = 100000;
    pos.is_active = true;

    // Write (WILL FAIL - method doesn't exist)
    BOOST_CHECK(batch.WriteDDTimeLock(pos));
}

BOOST_AUTO_TEST_CASE(walletbatch_write_ddtransaction)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    DDTransaction tx;
    tx.txid = "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    tx.amount = 5000;
    tx.timestamp = 1234567890;
    tx.confirmations = 6;
    tx.incoming = true;
    tx.address = "DD1qtest123";
    tx.category = "mint";

    BOOST_CHECK(batch.WriteDDTransaction(tx));  // WILL FAIL
}

BOOST_AUTO_TEST_CASE(walletbatch_write_ddbalance)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Create a valid DD address from P2TR destination
    uint256 hash;
    hash.SetHex("1234567890123456789012345678901234567890123456789012345678901234");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);

    WalletDDBalance balance;
    balance.address.SetDigiDollar(taproot_dest, CChainParams::DIGIDOLLAR_ADDRESS_REGTEST);
    balance.balance = 10000;
    balance.last_updated = 1234567890;

    std::string address = balance.address.ToString();
    BOOST_CHECK(batch.WriteDDBalance(address, balance));
}

BOOST_AUTO_TEST_CASE(walletbatch_write_ddoutput)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    CDigiDollarOutput output;
    output.nDDAmount = 2500;
    output.collateralId = uint256S("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    output.nLockTime = 100;

    uint256 output_id = uint256S("0xfedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");

    BOOST_CHECK(batch.WriteDDOutput(output_id, output));
}

BOOST_AUTO_TEST_CASE(walletbatch_write_ddmetadata)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    std::string key = "total_dd_balance";
    std::string value = "123456";

    BOOST_CHECK(batch.WriteDDMetadata(key, value));  // WILL FAIL
}

BOOST_AUTO_TEST_CASE(walletbatch_write_read_roundtrip)
{
    // Test write then read to ensure serialization works correctly
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();

    // Write position
    {
        wallet::WalletBatch batch(*database);
        WalletCollateralPosition pos;
        pos.dd_timelock_id = uint256S("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        pos.dd_minted = 50000;
        pos.dgb_collateral = 2500000;
        pos.lock_tier = 5;
        pos.unlock_height = 200000;
        pos.is_active = true;

        BOOST_CHECK(batch.WriteDDTimeLock(pos));
    }

    // Read position back (will be implemented in Phase 2, Task 2.2)
    // For now, just verify write succeeded
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(walletbatch_read_position)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Write position
    WalletCollateralPosition original;
    original.dd_timelock_id = uint256S("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    original.dd_minted = 10000;
    original.dgb_collateral = 500000;
    original.lock_tier = 3;
    original.unlock_height = 100000;
    original.is_active = true;

    BOOST_REQUIRE(batch.WriteDDTimeLock(original));

    // Read position (WILL FAIL - method doesn't exist)
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadDDTimeLock(original.dd_timelock_id, read_pos));

    // Verify all fields match
    BOOST_CHECK_EQUAL(read_pos.dd_timelock_id, original.dd_timelock_id);
    BOOST_CHECK_EQUAL(read_pos.dd_minted, original.dd_minted);
    BOOST_CHECK_EQUAL(read_pos.dgb_collateral, original.dgb_collateral);
    BOOST_CHECK_EQUAL(read_pos.lock_tier, original.lock_tier);
    BOOST_CHECK_EQUAL(read_pos.unlock_height, original.unlock_height);
    BOOST_CHECK_EQUAL(read_pos.is_active, original.is_active);
}

BOOST_AUTO_TEST_CASE(walletbatch_read_ddtransaction)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Write transaction
    DDTransaction original;
    original.txid = "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    original.amount = 5000;
    original.timestamp = 1234567890;
    original.confirmations = 6;
    original.incoming = true;
    original.address = "DD1qtest123...";
    original.category = "mint";

    BOOST_REQUIRE(batch.WriteDDTransaction(original));

    // Read transaction (WILL FAIL)
    uint256 txid;
    txid.SetHex(original.txid);
    DDTransaction read_tx;
    BOOST_CHECK(batch.ReadDDTransaction(txid, read_tx));

    // Verify
    BOOST_CHECK_EQUAL(read_tx.txid, original.txid);
    BOOST_CHECK_EQUAL(read_tx.amount, original.amount);
    BOOST_CHECK_EQUAL(read_tx.timestamp, original.timestamp);
    BOOST_CHECK_EQUAL(read_tx.confirmations, original.confirmations);
    BOOST_CHECK_EQUAL(read_tx.incoming, original.incoming);
}

BOOST_AUTO_TEST_CASE(walletbatch_read_ddbalance)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Create a valid DD address from P2TR destination
    uint256 hash;
    hash.SetHex("abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);

    // Write balance
    WalletDDBalance original;
    original.address.SetDigiDollar(taproot_dest, CChainParams::DIGIDOLLAR_ADDRESS_REGTEST);
    original.balance = 10000;
    original.last_updated = 1234567890;

    std::string address = original.address.ToString();
    BOOST_REQUIRE(batch.WriteDDBalance(address, original));

    // Read balance
    WalletDDBalance read_balance;
    BOOST_CHECK(batch.ReadDDBalance(address, read_balance));

    // Verify
    BOOST_CHECK_EQUAL(read_balance.address.ToString(), original.address.ToString());
    BOOST_CHECK_EQUAL(read_balance.balance, original.balance);
    BOOST_CHECK_EQUAL(read_balance.last_updated, original.last_updated);
}

BOOST_AUTO_TEST_CASE(walletbatch_read_ddoutput)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Write output
    CDigiDollarOutput original;
    original.nDDAmount = 2500;
    original.collateralId = uint256S("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    original.nLockTime = 100;

    uint256 output_id = uint256S("0xfedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");

    BOOST_REQUIRE(batch.WriteDDOutput(output_id, original));

    // Read output (WILL FAIL)
    CDigiDollarOutput read_output;
    BOOST_CHECK(batch.ReadDDOutput(output_id, read_output));

    // Verify
    BOOST_CHECK_EQUAL(read_output.nDDAmount, original.nDDAmount);
    BOOST_CHECK_EQUAL(read_output.collateralId, original.collateralId);
    BOOST_CHECK_EQUAL(read_output.nLockTime, original.nLockTime);
}

BOOST_AUTO_TEST_CASE(walletbatch_read_ddmetadata)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Write metadata
    std::string key = "total_dd_balance";
    std::string original_value = "123456";

    BOOST_REQUIRE(batch.WriteDDMetadata(key, original_value));

    // Read metadata (WILL FAIL)
    std::string read_value;
    BOOST_CHECK(batch.ReadDDMetadata(key, read_value));
    BOOST_CHECK_EQUAL(read_value, original_value);
}

BOOST_AUTO_TEST_CASE(walletbatch_read_nonexistent)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Try to read non-existent position
    uint256 fake_id = uint256S("0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    WalletCollateralPosition pos;

    // Should return false for non-existent key
    BOOST_CHECK(!batch.ReadDDTimeLock(fake_id, pos));

    // Try to read non-existent transaction
    uint256 fake_txid = uint256S("0x1111111111111111111111111111111111111111111111111111111111111111");
    DDTransaction tx;
    BOOST_CHECK(!batch.ReadDDTransaction(fake_txid, tx));

    // Try to read non-existent balance
    WalletDDBalance balance;
    BOOST_CHECK(!batch.ReadDDBalance("nonexistent_address", balance));

    // Try to read non-existent output
    CDigiDollarOutput output;
    BOOST_CHECK(!batch.ReadDDOutput(fake_id, output));

    // Try to read non-existent metadata
    std::string value;
    BOOST_CHECK(!batch.ReadDDMetadata("nonexistent_key", value));
}

// ============================================================================
// PHASE 2, TASK 2.3 - ERASE METHODS TESTS
// ============================================================================

BOOST_AUTO_TEST_CASE(walletbatch_erase_position)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Write position
    WalletCollateralPosition pos;
    pos.dd_timelock_id = uint256S("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    pos.dd_minted = 10000;
    pos.dgb_collateral = 500000;
    pos.lock_tier = 3;
    pos.unlock_height = 100000;
    pos.is_active = true;

    BOOST_REQUIRE(batch.WriteDDTimeLock(pos));

    // Verify it exists
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadDDTimeLock(pos.dd_timelock_id, read_pos));

    // Erase it (WILL FAIL - method doesn't exist)
    BOOST_CHECK(batch.EraseDDTimeLock(pos.dd_timelock_id));

    // Verify it's gone
    WalletCollateralPosition deleted_pos;
    BOOST_CHECK(!batch.ReadDDTimeLock(pos.dd_timelock_id, deleted_pos));
}

BOOST_AUTO_TEST_CASE(walletbatch_erase_ddtransaction)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Write transaction
    DDTransaction tx;
    tx.txid = "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    tx.amount = 5000;
    tx.category = "mint";

    BOOST_REQUIRE(batch.WriteDDTransaction(tx));

    // Verify it exists
    uint256 txid;
    txid.SetHex(tx.txid);
    DDTransaction read_tx;
    BOOST_CHECK(batch.ReadDDTransaction(txid, read_tx));

    // Erase it (WILL FAIL)
    BOOST_CHECK(batch.EraseDDTransaction(txid));

    // Verify it's gone
    DDTransaction deleted_tx;
    BOOST_CHECK(!batch.ReadDDTransaction(txid, deleted_tx));
}

BOOST_AUTO_TEST_CASE(walletbatch_erase_ddbalance)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Create a valid DD address from P2TR destination
    uint256 hash;
    hash.SetHex("fedcbafedcbafedcbafedcbafedcbafedcbafedcbafedcbafedcbafedcbafed");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);

    // Write balance
    WalletDDBalance balance;
    balance.address.SetDigiDollar(taproot_dest, CChainParams::DIGIDOLLAR_ADDRESS_REGTEST);
    balance.balance = 10000;

    std::string address = balance.address.ToString();
    BOOST_REQUIRE(batch.WriteDDBalance(address, balance));

    // Verify it exists
    WalletDDBalance read_balance;
    BOOST_CHECK(batch.ReadDDBalance(address, read_balance));

    // Erase it
    BOOST_CHECK(batch.EraseDDBalance(address));

    // Verify it's gone
    WalletDDBalance deleted_balance;
    BOOST_CHECK(!batch.ReadDDBalance(address, deleted_balance));
}

BOOST_AUTO_TEST_CASE(walletbatch_erase_ddoutput)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Create and write DD output
    CDigiDollarOutput output;
    output.nDDAmount = 5000;
    uint256 output_id = uint256S("0xabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd");

    BOOST_REQUIRE(batch.WriteDDOutput(output_id, output));

    // Verify it exists
    CDigiDollarOutput read_output;
    BOOST_CHECK(batch.ReadDDOutput(output_id, read_output));

    // Erase it (WILL FAIL)
    BOOST_CHECK(batch.EraseDDOutput(output_id));

    // Verify it's gone
    CDigiDollarOutput deleted_output;
    BOOST_CHECK(!batch.ReadDDOutput(output_id, deleted_output));
}

BOOST_AUTO_TEST_CASE(walletbatch_erase_nonexistent)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Erase non-existent position should return true (like Bitcoin Core behavior)
    uint256 fake_id = uint256S("0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    BOOST_CHECK(batch.EraseDDTimeLock(fake_id));
}

BOOST_AUTO_TEST_CASE(walletbatch_complete_lifecycle)
{
    std::unique_ptr<wallet::WalletDatabase> database = wallet::CreateMockableWalletDatabase();
    wallet::WalletBatch batch(*database);

    // Test complete lifecycle: Write → Read → Erase → Verify Gone
    WalletCollateralPosition pos;
    pos.dd_timelock_id = uint256S("0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    pos.dd_minted = 20000;
    pos.dgb_collateral = 1000000;
    pos.lock_tier = 5;

    // Write
    BOOST_CHECK(batch.WriteDDTimeLock(pos));

    // Read and verify
    WalletCollateralPosition read1;
    BOOST_REQUIRE(batch.ReadDDTimeLock(pos.dd_timelock_id, read1));
    BOOST_CHECK_EQUAL(read1.dd_minted, 20000);

    // Erase
    BOOST_CHECK(batch.EraseDDTimeLock(pos.dd_timelock_id));

    // Verify gone
    WalletCollateralPosition read2;
    BOOST_CHECK(!batch.ReadDDTimeLock(pos.dd_timelock_id, read2));
}

BOOST_AUTO_TEST_SUITE_END()
