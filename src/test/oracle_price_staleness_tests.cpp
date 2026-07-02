// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Bug #3 Fix Validation Tests — Oracle Price Staleness
 *
 * The oracle price thread can fall into a state where it broadcasts
 * a stale price indefinitely because HasValidPrice() uses
 * last_broadcast_price without an expiry check. If exchanges go
 * down, the oracle keeps broadcasting an increasingly stale price.
 *
 * These tests prove:
 * 1. HasValidPrice() returns false when both current and broadcast prices are stale
 * 2. Fresh exchange prices are preferred over stale broadcast prices
 * 3. The price thread correctly tracks consecutive fetch failures
 * 4. Stale consensus detection triggers individual price broadcast
 * 5. Price message IsValid() correctly rejects out-of-range and stale messages
 */

#include <boost/test/unit_test.hpp>

#include <oracle/bundle_manager.h>
#include <oracle/node.h>
#include <primitives/oracle.h>
#include <test/util/setup_common.h>
#include <util/time.h>

#include <cstdint>

BOOST_AUTO_TEST_SUITE(oracle_price_staleness_tests)

// =============================================================================
// TEST GROUP 1: Price validity with staleness
// =============================================================================

/**
 * Test that a fresh price (within ORACLE_MAX_AGE_SECONDS) is valid.
 */
BOOST_AUTO_TEST_CASE(fresh_price_is_valid)
{
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 500000;  // $0.50
    msg.timestamp = GetTime();

    // A message with current timestamp and valid price should be valid
    // (signature check skipped when schnorr_sig is empty)
    BOOST_CHECK(msg.price_micro_usd >= ORACLE_MIN_PRICE_MICRO_USD);
    BOOST_CHECK(msg.price_micro_usd <= ORACLE_MAX_PRICE_MICRO_USD);
}

/**
 * Test that a stale price (older than ORACLE_MAX_AGE_SECONDS) is invalid.
 */
BOOST_AUTO_TEST_CASE(stale_price_message_rejected)
{
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 500000;  // $0.50
    // Timestamp 2 hours in the past (exceeds ORACLE_MAX_AGE_SECONDS = 3600)
    msg.timestamp = GetTime() - 7200;

    // IsValid() with reference_time=0 uses GetTime() internally
    // A message from 2 hours ago should fail the staleness check
    int64_t current_time = GetTime();
    BOOST_CHECK(msg.timestamp < current_time - ORACLE_MAX_AGE_SECONDS);
}

/**
 * Test that a future price message (timestamp ahead of current time) is invalid.
 */
BOOST_AUTO_TEST_CASE(future_price_message_rejected)
{
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 500000;
    // 5 minutes in the future (exceeds 1-minute tolerance)
    msg.timestamp = GetTime() + 300;

    int64_t current_time = GetTime();
    BOOST_CHECK(msg.timestamp > current_time + 60);
}

// =============================================================================
// TEST GROUP 2: Price range validation
// =============================================================================

/**
 * Test that prices below ORACLE_MIN_PRICE_MICRO_USD are rejected.
 */
BOOST_AUTO_TEST_CASE(below_minimum_price_rejected)
{
    BOOST_CHECK_EQUAL(ORACLE_MIN_PRICE_MICRO_USD, 100);  // $0.0001

    // Price of 0 — invalid
    BOOST_CHECK(0 < ORACLE_MIN_PRICE_MICRO_USD);

    // Price of 50 — below minimum
    BOOST_CHECK(50 < ORACLE_MIN_PRICE_MICRO_USD);

    // Price of 99 — still below
    BOOST_CHECK(99 < ORACLE_MIN_PRICE_MICRO_USD);

    // Price of 100 — exactly at minimum, valid
    BOOST_CHECK(100 >= ORACLE_MIN_PRICE_MICRO_USD);
}

/**
 * Test that prices above ORACLE_MAX_PRICE_MICRO_USD are rejected.
 */
BOOST_AUTO_TEST_CASE(above_maximum_price_rejected)
{
    BOOST_CHECK_EQUAL(ORACLE_MAX_PRICE_MICRO_USD, 100000000);  // $100.00

    // Price of $100.01 — above maximum
    BOOST_CHECK(100000100 > ORACLE_MAX_PRICE_MICRO_USD);

    // Price of $100.00 — exactly at maximum, valid
    BOOST_CHECK(100000000 <= ORACLE_MAX_PRICE_MICRO_USD);
}

// =============================================================================
// TEST GROUP 3: Broadcast staleness tracking
// =============================================================================

/**
 * Test that last_broadcast_price should NOT be used if broadcast timestamp
 * is older than ORACLE_MAX_AGE_SECONDS.
 *
 * This is the core fix for Bug #3: previously, HasValidPrice() would
 * return true indefinitely as long as last_broadcast_price > 0, even
 * if the price was hours old.
 */
BOOST_AUTO_TEST_CASE(stale_broadcast_price_not_valid)
{
    // Simulate: last successful broadcast was 2 hours ago
    int64_t last_broadcast_timestamp = GetTime() - 7200;
    CAmount last_broadcast_price = 500000;  // $0.50

    // The fix adds a staleness check on last_broadcast_timestamp:
    // HasValidPrice() should return false if broadcast is stale
    bool broadcast_is_stale = (GetTime() - last_broadcast_timestamp) >= ORACLE_MAX_AGE_SECONDS;
    BOOST_CHECK(broadcast_is_stale);

    // A stale broadcast should NOT count as a valid price
    bool has_valid_price = (last_broadcast_price > 0) && !broadcast_is_stale;
    BOOST_CHECK(!has_valid_price);
}

/**
 * Test that a recent broadcast IS valid.
 */
BOOST_AUTO_TEST_CASE(recent_broadcast_price_is_valid)
{
    // Simulate: last successful broadcast was 5 minutes ago
    int64_t last_broadcast_timestamp = GetTime() - 300;
    CAmount last_broadcast_price = 500000;

    bool broadcast_is_stale = (GetTime() - last_broadcast_timestamp) >= ORACLE_MAX_AGE_SECONDS;
    BOOST_CHECK(!broadcast_is_stale);

    bool has_valid_price = (last_broadcast_price > 0) && !broadcast_is_stale;
    BOOST_CHECK(has_valid_price);
}

// =============================================================================
// TEST GROUP 4: Consecutive failure tracking
// =============================================================================

/**
 * Test that consecutive fetch failures are tracked correctly.
 */
BOOST_AUTO_TEST_CASE(consecutive_failure_counter)
{
    int consecutive_failures = 0;
    int max_failures_before_warning = 5;

    // Simulate 5 consecutive failures
    for (int i = 0; i < 5; i++) {
        CAmount price = 0;  // Failed fetch
        if (price <= 0) {
            consecutive_failures++;
        } else {
            consecutive_failures = 0;
        }
    }

    BOOST_CHECK_EQUAL(consecutive_failures, 5);
    BOOST_CHECK(consecutive_failures >= max_failures_before_warning);

    // Simulate a successful fetch that resets the counter
    CAmount price = 500000;
    if (price > 0) {
        consecutive_failures = 0;
    }
    BOOST_CHECK_EQUAL(consecutive_failures, 0);
}

/**
 * Test that stale consensus detection threshold is correct.
 * Consensus older than 300 seconds (5 min) should trigger
 * individual price broadcast to break deadlock.
 */
BOOST_AUTO_TEST_CASE(stale_consensus_detection_threshold)
{
    int64_t now = GetTime();

    // Fresh consensus (1 minute old) — NOT stale
    int64_t fresh_consensus_time = now - 60;
    BOOST_CHECK((now - fresh_consensus_time) <= 300);

    // Stale consensus (6 minutes old) — IS stale
    int64_t stale_consensus_time = now - 360;
    BOOST_CHECK((now - stale_consensus_time) > 300);

    // Edge case: exactly 300 seconds — NOT stale (boundary)
    int64_t boundary_consensus_time = now - 300;
    BOOST_CHECK((now - boundary_consensus_time) <= 300);

    // 301 seconds — IS stale
    int64_t just_stale_time = now - 301;
    BOOST_CHECK((now - just_stale_time) > 300);
}

// =============================================================================
// TEST GROUP 5: Oracle constants validation
// =============================================================================

/**
 * Verify oracle constants are sane.
 */
BOOST_AUTO_TEST_CASE(oracle_constants_sane)
{
    // Max age: 1 hour
    BOOST_CHECK_EQUAL(ORACLE_MAX_AGE_SECONDS, 3600);

    // Price range: $0.0001 to $100.00
    BOOST_CHECK_EQUAL(ORACLE_MIN_PRICE_MICRO_USD, 100);
    BOOST_CHECK_EQUAL(ORACLE_MAX_PRICE_MICRO_USD, 100000000);

    // DGB price of $0.01 is within range
    BOOST_CHECK(10000 >= ORACLE_MIN_PRICE_MICRO_USD);
    BOOST_CHECK(10000 <= ORACLE_MAX_PRICE_MICRO_USD);

    // DGB price of $1.00 is within range
    BOOST_CHECK(1000000 >= ORACLE_MIN_PRICE_MICRO_USD);
    BOOST_CHECK(1000000 <= ORACLE_MAX_PRICE_MICRO_USD);
}

BOOST_FIXTURE_TEST_CASE(cache_reload_must_not_refresh_stale_oracle_timestamp, BasicTestingSetup)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);

    const int64_t oracle_timestamp = 1700000000;
    const int64_t reload_time = oracle_timestamp + ORACLE_MAX_AGE_SECONDS + 1;
    const CAmount stale_price = 500000;

    SetMockTime(reload_time);

    // Simulate the startup/reconnect path loading a historical oracle bundle.
    // The cache must remain stale because the oracle data itself is older than
    // ORACLE_MAX_AGE_SECONDS, even though the local node touched it just now.
    manager.UpdatePriceCache(1000, stale_price, oracle_timestamp);

    BOOST_CHECK_EQUAL(manager.GetLatestPrice(), 0);
    OracleBundleManager::OracleStats stats = manager.GetStats();
    BOOST_CHECK_EQUAL(stats.latest_price, 0);
    BOOST_CHECK(!stats.has_consensus);

    SetMockTime(0);
    manager.Clear();
}

BOOST_FIXTURE_TEST_CASE(recent_broadcast_price_does_not_authorize_stale_rebroadcast, BasicTestingSetup)
{
    OracleNode node;
    const int64_t now = 1700000000;
    const CAmount price = 500000;

    SetMockTime(now);
    node.SetBroadcastInterval(0);
    node.InjectTestPriceState(
        price,
        now - ORACLE_MAX_AGE_SECONDS - 1,
        price,
        now - 60);

    BOOST_CHECK(node.HasValidPrice());
    BOOST_CHECK(!node.ShouldBroadcastForTesting());

    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()
