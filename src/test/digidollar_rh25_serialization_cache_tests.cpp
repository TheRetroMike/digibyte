// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-25: COracleBundle Serialization & RemovePriceCache Fix Tests
 *
 * RH-25a: Verify COracleBundle SERIALIZE_METHODS round-trips version,
 *         aggregate_sig, and participation_bitmap for v0x03 bundles.
 *         Also verify v0x02 bundles remain backward-compatible.
 *
 * RH-25b: Verify RemovePriceCache correctly reverts cached_price to the
 *         highest remaining height's price (or 0 if empty).
 */

#include <boost/test/unit_test.hpp>

#include <oracle/bundle_manager.h>
#include <primitives/oracle.h>
#include <streams.h>
#include <test/util/setup_common.h>

BOOST_FIXTURE_TEST_SUITE(digidollar_rh25_serialization_cache_tests, BasicTestingSetup)

// =============================================================================
// RH-25a: v0x03 bundle round-trip preserves MuSig2 fields
// =============================================================================

BOOST_AUTO_TEST_CASE(rh25a_v03_bundle_roundtrip)
{
    COracleBundle original;
    original.version = 3;
    original.epoch = 100;
    original.median_price_micro_usd = 123456;
    original.timestamp = 1700000000;
    original.aggregate_sig.assign(64, 0xDE);
    original.participation_bitmap = {0xFF, 0x0F, 0x01};

    // Add a message
    COraclePriceMessage msg;
    msg.oracle_id = 1;
    msg.price_micro_usd = 123456;
    msg.timestamp = 1700000000;
    msg.schnorr_sig.assign(64, 0xAA);
    original.messages.push_back(msg);

    DataStream ss{};
    ss << original;

    COracleBundle deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.version, 3u);
    BOOST_CHECK_EQUAL(deserialized.epoch, 100);
    BOOST_CHECK_EQUAL(deserialized.median_price_micro_usd, 123456u);
    BOOST_CHECK_EQUAL(deserialized.timestamp, 1700000000);
    BOOST_CHECK_EQUAL(deserialized.aggregate_sig.size(), 64u);
    BOOST_CHECK_EQUAL(deserialized.aggregate_sig[0], 0xDE);
    BOOST_CHECK_EQUAL(deserialized.participation_bitmap.size(), 3u);
    BOOST_CHECK_EQUAL(deserialized.participation_bitmap[0], 0xFF);
    BOOST_CHECK_EQUAL(deserialized.participation_bitmap[2], 0x01);
    BOOST_CHECK_EQUAL(deserialized.messages.size(), 1u);
}

// =============================================================================
// RH-25a: v0x02 bundle round-trip backward compatibility
// =============================================================================

BOOST_AUTO_TEST_CASE(rh25a_v02_bundle_roundtrip_backward_compat)
{
    COracleBundle original;
    original.version = 2;
    original.epoch = 50;
    original.median_price_micro_usd = 99999;
    original.timestamp = 1699999999;

    DataStream ss{};
    ss << original;

    COracleBundle deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.version, 2u);
    BOOST_CHECK_EQUAL(deserialized.epoch, 50);
    BOOST_CHECK_EQUAL(deserialized.median_price_micro_usd, 99999u);
    BOOST_CHECK_EQUAL(deserialized.timestamp, 1699999999);
    // v02 should NOT have MuSig2 fields
    BOOST_CHECK(deserialized.aggregate_sig.empty());
    BOOST_CHECK(deserialized.participation_bitmap.empty());
}

// =============================================================================
// RH-25a: v0x03 empty MuSig2 fields round-trip
// =============================================================================

BOOST_AUTO_TEST_CASE(rh25a_v03_empty_musig2_fields)
{
    COracleBundle original;
    original.version = 3;
    original.epoch = 1;
    original.median_price_micro_usd = 1;
    original.timestamp = 1;
    // Leave aggregate_sig and participation_bitmap empty

    DataStream ss{};
    ss << original;

    COracleBundle deserialized;
    ss >> deserialized;

    BOOST_CHECK_EQUAL(deserialized.version, 3u);
    BOOST_CHECK(deserialized.aggregate_sig.empty());
    BOOST_CHECK(deserialized.participation_bitmap.empty());
}

// =============================================================================
// RH-25b: RemovePriceCache reverts to highest remaining
// =============================================================================

BOOST_AUTO_TEST_CASE(rh25b_remove_price_cache_reverts_to_highest)
{
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    mgr.UpdatePriceCache(500, 10000);
    mgr.UpdatePriceCache(501, 20000);
    mgr.UpdatePriceCache(502, 30000);

    // Remove highest
    mgr.RemovePriceCache(502);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 20000);

    // Remove highest again
    mgr.RemovePriceCache(501);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 10000);

    // Remove last
    mgr.RemovePriceCache(500);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 0);
}

// =============================================================================
// RH-25b: RemovePriceCache for non-highest doesn't change cached_price
// =============================================================================

BOOST_AUTO_TEST_CASE(rh25b_remove_non_highest_keeps_cached_price)
{
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    mgr.UpdatePriceCache(600, 10000);
    mgr.UpdatePriceCache(601, 20000);
    mgr.UpdatePriceCache(602, 30000);

    // Remove a middle height — cached_price should stay at highest remaining
    mgr.RemovePriceCache(601);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 30000);

    // Remove lowest — still 30000
    mgr.RemovePriceCache(600);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 30000);

    // Cleanup
    mgr.RemovePriceCache(602);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 0);
}

// =============================================================================
// RH-25b: RemovePriceCache for non-existent height is no-op
// =============================================================================

BOOST_AUTO_TEST_CASE(rh25b_remove_nonexistent_height_noop)
{
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    mgr.UpdatePriceCache(700, 50000);

    // Remove non-existent height
    mgr.RemovePriceCache(999);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 50000);

    // Cleanup
    mgr.RemovePriceCache(700);
}

BOOST_AUTO_TEST_SUITE_END()
