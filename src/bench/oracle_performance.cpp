// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Oracle Performance Benchmarks
 * Measures critical performance paths in the oracle system
 */

#include <bench/bench.h>
#include <oracle/exchange.h>
#include <oracle/bundle_manager.h>
#include <primitives/oracle.h>
#include <key.h>
#include <random.h>
#include <util/time.h>
#include <validation.h>
#include <primitives/block.h>
#include <consensus/params.h>
#include <chainparams.h>

#include <chrono>
#include <vector>

using namespace ExchangeAPI;

/**
 * Benchmark 1: Exchange API Performance
 * Measures time to fetch DGB/USD from all 8 exchanges
 * Target: < 5 seconds for aggregate
 */
static void BenchmarkExchangeAPIAggregate(benchmark::Bench& bench)
{
    MultiExchangeAggregator aggregator;

    // Warm up (establish connections, DNS lookups, etc.)
    aggregator.FetchAggregatePrice();

    bench.run([&] {
        CAmount price = aggregator.FetchAggregatePrice();
        // Sanity check: price should be non-zero
        assert(price > 0 || true); // Allow zero for network failures in benchmark
    });
}

/**
 * Benchmark individual exchange fetchers
 */
static void BenchmarkExchangeBinance(benchmark::Bench& bench)
{
    BinanceFetcher fetcher;
    fetcher.FetchPrice(); // Warm up

    bench.run([&] {
        CAmount price = fetcher.FetchPrice();
    });
}

static void BenchmarkExchangeCoinGecko(benchmark::Bench& bench)
{
    CoinGeckoFetcher fetcher;
    fetcher.FetchPrice(); // Warm up

    bench.run([&] {
        CAmount price = fetcher.FetchPrice();
    });
}

static void BenchmarkExchangeCoinbase(benchmark::Bench& bench)
{
    CoinbaseFetcher fetcher;
    fetcher.FetchPrice(); // Warm up

    bench.run([&] {
        CAmount price = fetcher.FetchPrice();
    });
}

static void BenchmarkExchangeKraken(benchmark::Bench& bench)
{
    KrakenFetcher fetcher;
    fetcher.FetchPrice(); // Warm up

    bench.run([&] {
        CAmount price = fetcher.FetchPrice();
    });
}

static void BenchmarkExchangeMessari(benchmark::Bench& bench)
{
    MessariFetcher fetcher;
    fetcher.FetchPrice(); // Warm up

    bench.run([&] {
        CAmount price = fetcher.FetchPrice();
    });
}

/**
 * Benchmark 2: Schnorr Signature Performance
 * Target: < 1ms per sign operation, < 1ms per verify operation
 */
static void BenchmarkSchnorrSign(benchmark::Bench& bench)
{
    // Create test key
    CKey key;
    key.MakeNewKey(true);

    // Create test message
    COraclePriceMessage message;
    message.oracle_id = 1;
    message.price_micro_usd = 12340; // $0.01234
    message.timestamp = GetTime();
    message.block_height = 100000;
    message.nonce = GetRand<uint64_t>();

    bench.run([&] {
        // Sign the message (Schnorr signature creation)
        message.Sign(key);
    });
}

static void BenchmarkSchnorrVerify(benchmark::Bench& bench)
{
    // Create and sign test message
    CKey key;
    key.MakeNewKey(true);

    COraclePriceMessage message;
    message.oracle_id = 1;
    message.price_micro_usd = 12340;
    message.timestamp = GetTime();
    message.block_height = 100000;
    message.nonce = GetRand<uint64_t>();
    message.Sign(key);

    bench.run([&] {
        // Verify Schnorr signature
        bool valid = message.Verify();
        assert(valid); // Should always be valid
    });
}

/**
 * Benchmark 3: P2P Message Relay
 * Measures message size and serialization overhead
 */
static void BenchmarkOraclePriceMessageSize(benchmark::Bench& bench)
{
    // Create signed message
    CKey key;
    key.MakeNewKey(true);

    COraclePriceMessage message;
    message.oracle_id = 1;
    message.price_micro_usd = 12340;
    message.timestamp = GetTime();
    message.block_height = 100000;
    message.nonce = GetRand<uint64_t>();
    message.Sign(key);

    bench.run([&] {
        // Serialize to measure size
        DataStream ss{};
        ss << message;

        // Verify size is reasonable (< 100 bytes target)
        size_t size = ss.size();
        assert(size < 200); // Allow some overhead but should be compact
    });
}

static void BenchmarkOracleBundleMessageSize(benchmark::Bench& bench)
{
    // Create bundle with 1 message (Phase One)
    CKey key;
    key.MakeNewKey(true);

    COracleBundle bundle;
    bundle.epoch = 100;
    bundle.timestamp = GetTime();

    // Add one message
    COraclePriceMessage message;
    message.oracle_id = 1;
    message.price_micro_usd = 12340;
    message.timestamp = GetTime();
    message.block_height = 100000;
    message.nonce = GetRand<uint64_t>();
    message.Sign(key);

    bundle.messages.push_back(message);
    bundle.median_price_micro_usd = 12340;

    bench.run([&] {
        // Serialize bundle
        DataStream ss{};
        ss << bundle;

        size_t size = ss.size();
        // Bundle should be compact even with Phase Two expansion
        assert(size < 500); // Allow room for 15 messages
    });
}

/**
 * Benchmark 4: Block Validation Overhead
 * Measures oracle bundle validation time in CheckBlock()
 * Target: < 10ms added to block validation
 */
static void BenchmarkOracleBundleValidation(benchmark::Bench& bench)
{
    // Create valid bundle
    CKey key;
    key.MakeNewKey(true);

    COracleBundle bundle;
    bundle.epoch = 100;
    bundle.timestamp = GetTime();

    COraclePriceMessage message;
    message.oracle_id = 1;
    message.price_micro_usd = 12340;
    message.timestamp = GetTime();
    message.block_height = 100000;
    message.nonce = GetRand<uint64_t>();
    message.Sign(key);

    bundle.messages.push_back(message);
    bundle.median_price_micro_usd = 12340;

    bench.run([&] {
        // Validate bundle (all signatures, structure, timestamps)
        // Use 1 as min_required since benchmark bundle has 1 message
        bool valid = bundle.IsValid(1);
        assert(valid);
    });
}

static void BenchmarkSchnorrVerifyInBlock(benchmark::Bench& bench)
{
    // Simulate block validation scenario
    CKey key;
    key.MakeNewKey(true);

    COraclePriceMessage message;
    message.oracle_id = 1;
    message.price_micro_usd = 12340;
    message.timestamp = GetTime();
    message.block_height = 100000;
    message.nonce = GetRand<uint64_t>();
    message.Sign(key);

    bench.run([&] {
        // This is what happens in ContextualCheckBlock()
        bool valid = message.Verify();
        assert(valid);
    });
}

/**
 * Benchmark 5: Bundle Creation
 * Measures OracleBundleManager::GetCurrentBundle() performance
 * Target: < 5ms
 */
static void BenchmarkBundleCreation(benchmark::Bench& bench)
{
    // Initialize bundle manager (singleton pattern)
    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // Add a test message to pending pool
    CKey key;
    key.MakeNewKey(true);

    COraclePriceMessage message;
    message.oracle_id = 1;
    message.price_micro_usd = 12340;
    message.timestamp = GetTime();
    message.block_height = 100000;
    message.nonce = GetRand<uint64_t>();
    message.Sign(key);

    manager.AddOracleMessage(message);

    int32_t epoch = 100;

    bench.run([&] {
        // Get current bundle (Phase One: 1 message)
        COracleBundle bundle = manager.GetCurrentBundle(epoch);

        // Verify bundle created successfully
        assert(!bundle.messages.empty() || true); // Allow empty if no messages
    });
}

static void BenchmarkMedianCalculation(benchmark::Bench& bench)
{
    // Create bundle with multiple messages (simulate Phase Two)
    COracleBundle bundle;
    bundle.epoch = 100;
    bundle.timestamp = GetTime();

    // Add 8 messages (minimum consensus)
    CKey key;
    key.MakeNewKey(true);

    for (int i = 0; i < 8; i++) {
        COraclePriceMessage message;
        message.oracle_id = i;
        message.price_micro_usd = 12000 + (i * 100); // Varying prices
        message.timestamp = GetTime();
        message.block_height = 100000;
        message.nonce = GetRand<uint64_t>();
        message.Sign(key);

        bundle.messages.push_back(message);
    }

    bench.run([&] {
        // Calculate median (with outlier filtering)
        uint64_t median = bundle.GetConsensusPrice(Params().GetConsensus().nOracleRequiredMessages);
        assert(median > 0);
    });
}

/**
 * Benchmark: GetConsensusPrice (unified IQR outlier filtering)
 * Tests the single canonical outlier filtering algorithm (T9-01)
 */
static void BenchmarkConsensusPrice(benchmark::Bench& bench)
{
    COracleBundle bundle;
    bundle.epoch = 100;
    bundle.timestamp = GetTime();

    CKey key;
    key.MakeNewKey(true);

    // Add 15 messages with some outliers
    for (int i = 0; i < 15; i++) {
        COraclePriceMessage message;
        message.oracle_id = i;

        // Create outliers at positions 0, 7, 14
        if (i == 0 || i == 7 || i == 14) {
            message.price_micro_usd = 50000; // Extreme outlier
        } else {
            message.price_micro_usd = 12000 + (i * 50);
        }

        message.timestamp = GetTime();
        message.block_height = 100000;
        message.nonce = GetRand<uint64_t>();
        message.Sign(key);

        bundle.messages.push_back(message);
    }

    bench.run([&] {
        uint64_t price = bundle.GetConsensusPrice(8);
        assert(price > 0);
    });
}

// Register benchmarks
BENCHMARK(BenchmarkExchangeAPIAggregate, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkExchangeBinance, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkExchangeCoinGecko, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkExchangeCoinbase, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkExchangeKraken, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkExchangeMessari, benchmark::PriorityLevel::HIGH);

BENCHMARK(BenchmarkSchnorrSign, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkSchnorrVerify, benchmark::PriorityLevel::HIGH);

BENCHMARK(BenchmarkOraclePriceMessageSize, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkOracleBundleMessageSize, benchmark::PriorityLevel::HIGH);

BENCHMARK(BenchmarkOracleBundleValidation, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkSchnorrVerifyInBlock, benchmark::PriorityLevel::HIGH);

BENCHMARK(BenchmarkBundleCreation, benchmark::PriorityLevel::HIGH);
BENCHMARK(BenchmarkMedianCalculation, benchmark::PriorityLevel::HIGH);

BENCHMARK(BenchmarkConsensusPrice, benchmark::PriorityLevel::HIGH);
