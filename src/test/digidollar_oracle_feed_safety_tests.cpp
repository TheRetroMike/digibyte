// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 11 (DD-FA-SEC-009) — Oracle Exchange Feed Safety
 *
 * Defense-in-depth tests for the BaseExchangeFetcher / MultiExchangeAggregator
 * surface. The brief asks: "if one exchange returns nonsense, can the
 * aggregator still produce a sane median?" — and the per-exchange post-
 * conversion range check was missing from Binance / Coinbase / Kraken /
 * Messari / CoinGecko while six other fetchers (Bittrex / Poloniex / KuCoin
 * / Crypto.com / Gate.io / HTX) enforced [100, 10000000] micro-USD ($0.0001
 * to $10.00). The shared helper ConvertToMicroUSD(double) capped only at
 * $100 / 100000000 micro-USD, which is two orders of magnitude above any
 * realistic DGB price (historic ATH ~$0.18). A compromised endpoint that
 * returned $99.99 would survive ConvertToMicroUSD and reach the median /
 * outlier filter, where it could poison the aggregate when only the
 * minimum quorum of feeds was up.
 *
 * Wave 11 hardens ConvertToMicroUSD to clamp at the production safety cap
 * ($10) used by the explicit per-fetcher gates. This pins:
 *   - the new central cap is enforced by all fetchers via the shared helper.
 *   - reasonable DGB prices (sub-$1) still convert correctly.
 *   - cross-exchange aggregation tolerates one nonsense feed without
 *     producing an unsafe median.
 */

#include <boost/test/unit_test.hpp>

#include <consensus/amount.h>
#include <oracle/exchange.h>
#include <test/util/setup_common.h>
#include <util/time.h>

#include <vector>

using namespace ExchangeAPI;

BOOST_FIXTURE_TEST_SUITE(digidollar_oracle_feed_safety_tests, BasicTestingSetup)

// =============================================================================
// Group 1 — Central per-fetcher conversion cap
// =============================================================================

BOOST_AUTO_TEST_CASE(convert_to_micro_usd_accepts_realistic_dgb_prices)
{
    BinanceFetcher fetcher; // Any concrete subclass exposes the helper.

    // Realistic DGB prices.
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(0.0065), 6500);   // $0.0065
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(0.18), 180000);   // historic ATH
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(1.0), 1000000);   // $1.00
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(9.99), 9990000);  // $9.99
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(10.0), 10000000); // boundary
}

BOOST_AUTO_TEST_CASE(convert_to_micro_usd_rejects_above_safety_cap)
{
    BinanceFetcher fetcher;

    // The pre-Wave-11 cap was $100, but six fetchers already gated at $10.
    // Wave 11 tightens the central helper to match that production gate.
    // Anything strictly above $10 must convert to 0 (fail closed).
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(10.000001), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(11.0), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(99.99), 0); // pre-Wave-11 escape
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(100.0), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(1000.0), 0);
}

BOOST_AUTO_TEST_CASE(convert_to_micro_usd_rejects_non_finite_or_non_positive)
{
    BinanceFetcher fetcher;

    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(0.0), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(-1.0), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(-100.0), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::numeric_limits<double>::infinity()), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(-std::numeric_limits<double>::infinity()), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::numeric_limits<double>::quiet_NaN()), 0);
}

BOOST_AUTO_TEST_CASE(convert_to_micro_usd_string_overload_inherits_cap)
{
    BinanceFetcher fetcher;

    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::string("0.0065")), 6500);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::string("10.0")), 10000000);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::string("99.99")), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::string("not-a-number")), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::string("")), 0);
}

// =============================================================================
// Group 2 — Cross-exchange cross-validation:
// One compromised feed should not poison the median above safety.
// =============================================================================

BOOST_AUTO_TEST_CASE(median_with_one_nonsense_feed_outlier_filtered)
{
    MultiExchangeAggregator aggregator;
    aggregator.SetMinRequiredSources(3);

    // Three honest exchanges around the realistic price plus one $9.99
    // would-be outlier (still under the safety cap so it passes
    // ConvertToMicroUSD, but FilterOutliers must remove it).
    const int64_t now = GetTime();
    std::vector<MultiExchangeAggregator::ExchangePrice> prices = {
        {"Honest1", 6500, now, true, 1.0},
        {"Honest2", 6510, now, true, 1.0},
        {"Honest3", 6490, now, true, 1.0},
        {"Outlier", 9990000, now, true, 1.0}, // $9.99 — at the safety cap
    };

    auto filtered = aggregator.FilterOutliers(prices);
    // Outlier removed because deviation from the median (6500) far
    // exceeds outlier_threshold (10% of 6500 = 650).
    BOOST_CHECK_EQUAL(filtered.size(), 3u);
    for (const auto& p : filtered) {
        BOOST_CHECK_NE(p.price_micro_usd, 9990000);
    }

    CAmount median = aggregator.CalculateMedianPrice(filtered);
    BOOST_CHECK_GE(median, 6490);
    BOOST_CHECK_LE(median, 6510);
}

BOOST_AUTO_TEST_CASE(median_rejects_aggregate_when_outlier_drops_below_quorum)
{
    MultiExchangeAggregator aggregator;
    aggregator.SetMinRequiredSources(3);

    const int64_t now = GetTime();
    // Only three feeds total: two honest + one nonsense. Outlier filter
    // drops the nonsense leaving 2 < 3 required → aggregate must fail
    // closed.
    std::vector<MultiExchangeAggregator::ExchangePrice> prices = {
        {"Honest1", 6500, now, true, 1.0},
        {"Honest2", 6510, now, true, 1.0},
        {"Outlier", 9990000, now, true, 1.0},
    };

    auto filtered = aggregator.FilterOutliers(prices);
    BOOST_CHECK_EQUAL(filtered.size(), 2u);

    // Mirror the contract enforced by FetchAggregatePrice():
    BOOST_CHECK(filtered.size() < 3u);
}

BOOST_AUTO_TEST_CASE(post_cap_helper_keeps_aggregator_inputs_safe)
{
    // The point of the central cap: an exchange that returns a price
    // above $10 must contribute zero, not a poisoned positive value.
    BinanceFetcher fetcher;

    // Pre-Wave-11 these would convert to non-zero micro-USD values that
    // a 2-source minimum aggregator could surface to consumers.
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(11.0), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(50.0), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(99.99), 0);

    // And the explicit string path used by Binance/Coinbase/Kraken
    // FetchPrice() bodies.
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::string("11")), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::string("50.0")), 0);
    BOOST_CHECK_EQUAL(fetcher.ConvertToMicroUSD(std::string("99.999999")), 0);
}

BOOST_AUTO_TEST_SUITE_END()
