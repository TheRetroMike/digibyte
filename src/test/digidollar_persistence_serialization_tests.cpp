// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <test/util/setup_common.h>
#include <wallet/digidollarwallet.h>
#include <streams.h>
#include <uint256.h>

BOOST_FIXTURE_TEST_SUITE(digidollar_persistence_serialization_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(walletcollateralposition_serialize_roundtrip)
{
    // Create original position
    WalletCollateralPosition original;
    original.dd_timelock_id = uint256S("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    original.dd_minted = 10000;  // $100.00
    original.dgb_collateral = 500000;
    original.lock_tier = 3;
    original.unlock_height = 100000;
    original.is_active = true;

    // Serialize
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << original;

    // Deserialize
    WalletCollateralPosition deserialized;
    stream >> deserialized;

    // Verify all fields match
    BOOST_CHECK(deserialized.dd_timelock_id == original.dd_timelock_id);
    BOOST_CHECK_EQUAL(deserialized.dd_minted, original.dd_minted);
    BOOST_CHECK_EQUAL(deserialized.dgb_collateral, original.dgb_collateral);
    BOOST_CHECK_EQUAL(deserialized.lock_tier, original.lock_tier);
    BOOST_CHECK_EQUAL(deserialized.unlock_height, original.unlock_height);
    BOOST_CHECK_EQUAL(deserialized.is_active, original.is_active);
}

BOOST_AUTO_TEST_CASE(ddtransaction_serialize_roundtrip)
{
    // Create original transaction
    DDTransaction original;
    original.txid = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    original.amount = 5000;  // $50.00
    original.timestamp = 1234567890;
    original.confirmations = 6;
    original.incoming = true;
    original.address = "DD1qtest123...";
    original.category = "receive";

    // Serialize
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << original;

    // Deserialize
    DDTransaction deserialized;
    stream >> deserialized;

    // Verify all fields match
    BOOST_CHECK_EQUAL(deserialized.txid, original.txid);
    BOOST_CHECK_EQUAL(deserialized.amount, original.amount);
    BOOST_CHECK_EQUAL(deserialized.timestamp, original.timestamp);
    BOOST_CHECK_EQUAL(deserialized.confirmations, original.confirmations);
    BOOST_CHECK_EQUAL(deserialized.incoming, original.incoming);
    BOOST_CHECK_EQUAL(deserialized.address, original.address);
    BOOST_CHECK_EQUAL(deserialized.category, original.category);
}

BOOST_AUTO_TEST_CASE(walletddbalance_serialize_roundtrip)
{
    // Create original balance with a valid DD address string
    WalletDDBalance original;
    original.address = CDigiDollarAddress("RD1qtest123address456test789test012test345test678");
    original.balance = 75000;  // $750.00
    original.last_updated = 1234567890;

    // Serialize
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << original;

    // Deserialize
    WalletDDBalance deserialized;
    stream >> deserialized;

    // Verify all fields match
    BOOST_CHECK_EQUAL(deserialized.address.ToString(), original.address.ToString());
    BOOST_CHECK_EQUAL(deserialized.balance, original.balance);
    BOOST_CHECK_EQUAL(deserialized.last_updated, original.last_updated);
}

BOOST_AUTO_TEST_SUITE_END()
