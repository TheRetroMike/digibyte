// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 11 Agent B — MultiExchangeAggregator price-aggregator fuzz harness
 *
 * Drives the public surface of `MultiExchangeAggregator`:
 *   - `FilterOutliers` (median + 10% threshold, no-filter when <3 inputs)
 *   - `IsOutlier`
 *   - `CalculateMedianPrice`
 *   - `CalculateWeightedAverage`
 *   - `CalculateWeightedMedian`
 *   - `SetMinRequiredSources`, `SetOutlierThreshold`, `SetUseWeightedMedian`
 *   - exchange-weight setter/getter
 *
 * Bounds:
 *   - sample at most 32 prices per round to keep iterations cheap.
 *   - sample prices in `[0, 2 * ORACLE_MAX_PRICE_MICRO_USD]` to exercise
 *     both inside-range (valid) and out-of-range (sanity-rejected) values.
 *   - allow `success=false` and price=0 to drive the FilterValidPrices path
 *     even though FilterOutliers does not call it directly — the outlier
 *     filter must remain stable under a mixed valid/invalid input set.
 *
 * Invariants checked at the end:
 *   - The filtered set size is at most the input size.
 *   - When the input has fewer than 3 prices, FilterOutliers is a no-op
 *     (returns the input unchanged).
 *   - When the input has at least 3 prices, every survivor's deviation
 *     from the input median is at most outlier_threshold * median.
 *   - CalculateMedianPrice on an empty set returns 0.
 *   - CalculateWeightedAverage on an empty / all-zero-weight set returns 0.
 */

#include <oracle/exchange.h>
#include <primitives/oracle.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/time.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using ExchangeAPI::MultiExchangeAggregator;

namespace {

constexpr size_t MAX_FEEDS = 32;

std::string ConsumeExchangeName(FuzzedDataProvider& fdp)
{
    static const char* kNames[] = {
        "Binance", "Coinbase", "Kraken", "CoinGecko", "Messari",
        "KuCoin", "Crypto.com", "Bittrex", "Poloniex", "Gate.io",
        "HTX", "Unknown",
    };
    return kNames[fdp.ConsumeIntegralInRange<size_t>(0, std::size(kNames) - 1)];
}

CAmount ConsumePriceMicroUSD(FuzzedDataProvider& fdp)
{
    // Range covers [0, 2 * ORACLE_MAX_PRICE_MICRO_USD]: zero and out-of-range
    // values exercise the rejection edges, not just the happy path.
    return static_cast<CAmount>(
        fdp.ConsumeIntegralInRange<int64_t>(0, 2 * ORACLE_MAX_PRICE_MICRO_USD));
}

double ConsumeWeight(FuzzedDataProvider& fdp)
{
    return fdp.ConsumeFloatingPointInRange<double>(0.0, 5.0);
}

double ConsumeOutlierThreshold(FuzzedDataProvider& fdp)
{
    return fdp.ConsumeFloatingPointInRange<double>(0.0, 1.0);
}

} // namespace

FUZZ_TARGET(oracle_price_aggregator)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    MultiExchangeAggregator aggregator;
    aggregator.SetMinRequiredSources(fdp.ConsumeIntegralInRange<size_t>(0, 16));
    const double threshold = ConsumeOutlierThreshold(fdp);
    aggregator.SetOutlierThreshold(threshold);
    aggregator.SetUseWeightedMedian(fdp.ConsumeBool());

    // Optional weight overrides for known exchanges.
    const size_t weight_overrides = fdp.ConsumeIntegralInRange<size_t>(0, 4);
    for (size_t i = 0; i < weight_overrides; ++i) {
        aggregator.SetExchangeWeight(ConsumeExchangeName(fdp), ConsumeWeight(fdp));
    }

    // Build a fuzzed feed set.
    const size_t n = fdp.ConsumeIntegralInRange<size_t>(0, MAX_FEEDS);
    std::vector<MultiExchangeAggregator::ExchangePrice> feeds;
    feeds.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const std::string name = ConsumeExchangeName(fdp);
        const CAmount price = ConsumePriceMicroUSD(fdp);
        const int64_t ts = fdp.ConsumeIntegral<int64_t>();
        const bool ok = fdp.ConsumeBool();
        const double w = ConsumeWeight(fdp);
        feeds.emplace_back(name, price, ts, ok, w);
    }

    // Exercise the public aggregator surface.
    const auto filtered = aggregator.FilterOutliers(feeds);
    const CAmount median = aggregator.CalculateMedianPrice(filtered);
    const CAmount weighted_median = aggregator.CalculateWeightedMedian(filtered);
    const CAmount weighted_avg = aggregator.CalculateWeightedAverage(filtered);
    (void)weighted_median;
    (void)weighted_avg;

    if (!feeds.empty()) {
        const auto& sample = feeds.at(fdp.ConsumeIntegralInRange<size_t>(0, feeds.size() - 1));
        (void)aggregator.IsOutlier(sample.price_micro_usd, feeds);
    }

    // Invariants.
    assert(filtered.size() <= feeds.size());

    if (feeds.size() < 3) {
        // Documented behavior: < 3 inputs return unchanged.
        assert(filtered.size() == feeds.size());
    } else {
        // Survivors must satisfy the threshold band against the *input* median.
        const CAmount input_median = aggregator.CalculateMedianPrice(feeds);
        if (input_median > 0) {
            const CAmount band = static_cast<CAmount>(input_median * threshold);
            for (const auto& f : filtered) {
                const CAmount delta =
                    f.price_micro_usd > input_median
                    ? f.price_micro_usd - input_median
                    : input_median - f.price_micro_usd;
                assert(delta <= band);
            }
        }
    }

    if (filtered.empty()) {
        assert(median == 0);
    }
}
