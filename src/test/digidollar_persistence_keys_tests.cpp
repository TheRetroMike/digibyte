// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <wallet/walletdb.h>

BOOST_AUTO_TEST_SUITE(digidollar_persistence_keys_tests)

BOOST_AUTO_TEST_CASE(digidollar_persistence_keys_exist)
{
    // Test that DD keys are defined
    BOOST_CHECK(!wallet::DBKeys::DD_POSITION.empty());
    BOOST_CHECK(!wallet::DBKeys::DD_TRANSACTION.empty());
    BOOST_CHECK(!wallet::DBKeys::DD_BALANCE.empty());
    BOOST_CHECK(!wallet::DBKeys::DD_OUTPUT.empty());
    BOOST_CHECK(!wallet::DBKeys::DD_METADATA.empty());
}

BOOST_AUTO_TEST_CASE(digidollar_persistence_keys_unique)
{
    // Verify DD keys don't conflict with existing keys
    BOOST_CHECK(wallet::DBKeys::DD_POSITION != wallet::DBKeys::TX);
    BOOST_CHECK(wallet::DBKeys::DD_POSITION != wallet::DBKeys::KEY);
    BOOST_CHECK(wallet::DBKeys::DD_POSITION != wallet::DBKeys::CSCRIPT);
    BOOST_CHECK(wallet::DBKeys::DD_TRANSACTION != wallet::DBKeys::TX);
    BOOST_CHECK(wallet::DBKeys::DD_BALANCE != wallet::DBKeys::NAME);
}

BOOST_AUTO_TEST_CASE(digidollar_persistence_keys_format)
{
    // Verify key naming convention (lowercase, descriptive)
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_POSITION, "ddposition");
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_TRANSACTION, "ddtx");
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_BALANCE, "ddbalance");
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_OUTPUT, "ddutxo");
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_METADATA, "ddmeta");
}

BOOST_AUTO_TEST_SUITE_END()
