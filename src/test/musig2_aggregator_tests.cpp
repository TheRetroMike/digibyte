// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <oracle/musig2_aggregator.h>
#include <primitives/oracle.h>
#include <chainparams.h>
#include <hash.h>
#include <key.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <numeric>
#include <set>
#include <vector>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>

namespace {

//! Serialize an x-only pubkey to a 32-byte array for deterministic comparison.
std::array<unsigned char, 32> SerializeXOnly(const secp256k1_xonly_pubkey& pk)
{
    std::array<unsigned char, 32> buf{};
    secp256k1_xonly_pubkey_serialize(secp256k1_context_static, buf.data(), &pk);
    return buf;
}

} // anonymous namespace

// Use testnet fixture: active oracle key prefix, 7-signature consensus.
struct TestnetSetup : public BasicTestingSetup {
    TestnetSetup() : BasicTestingSetup(ChainType::TESTNET) {}
};

BOOST_FIXTURE_TEST_SUITE(musig2_aggregator_tests, TestnetSetup)

// ============================================================================
// Bitmap Encoding/Decoding Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(test_bitmap_encode_decode_7_of_21)
{
    std::vector<uint8_t> oracle_ids = {0, 1, 2, 3, 4, 5, 6};
    uint16_t total = 21;

    auto bitmap = MuSig2OracleAggregator::EncodeBitmap(oracle_ids, total);
    BOOST_REQUIRE(!bitmap.empty());
    BOOST_CHECK_EQUAL(bitmap.size(), 3u); // ceil(21/8) = 3 bytes

    // Verify bit pattern: bits 0-6 set
    BOOST_CHECK_EQUAL(bitmap[0], 0x7F); // oracles 0-6
    BOOST_CHECK_EQUAL(bitmap[1], 0x00);
    BOOST_CHECK_EQUAL(bitmap[2], 0x00);

    auto decoded = MuSig2OracleAggregator::DecodeBitmap(bitmap, total);
    BOOST_CHECK_EQUAL(decoded.size(), oracle_ids.size());
    BOOST_CHECK(decoded == oracle_ids);
}

BOOST_AUTO_TEST_CASE(test_bitmap_rejects_unused_high_bits)
{
    // For 21 oracle slots, only bits 0..4 of the final byte are valid.
    // Bits 5..7 must be zero so each participant set has one canonical bitmap.
    std::vector<unsigned char> bitmap = {0xFF, 0xFF, 0x20};
    auto decoded = MuSig2OracleAggregator::DecodeBitmap(bitmap, /*total_oracles=*/21);
    BOOST_CHECK(decoded.empty());
}

BOOST_AUTO_TEST_CASE(test_bitmap_variable_length_30_oracles)
{
    // Select 10 oracles spread across 30 slots
    std::vector<uint8_t> oracle_ids = {0, 3, 7, 10, 15, 18, 22, 25, 27, 29};
    uint16_t total = 30;

    auto bitmap = MuSig2OracleAggregator::EncodeBitmap(oracle_ids, total);
    BOOST_REQUIRE(!bitmap.empty());
    BOOST_CHECK_EQUAL(bitmap.size(), 4u); // ceil(30/8) = 4 bytes

    auto decoded = MuSig2OracleAggregator::DecodeBitmap(bitmap, total);
    BOOST_CHECK(decoded == oracle_ids);
}

BOOST_AUTO_TEST_CASE(test_bitmap_variable_length_256_oracles)
{
    // Select 12 oracles spread across all 256 slots
    std::vector<uint8_t> oracle_ids = {0, 15, 31, 63, 100, 127, 128, 150, 200, 240, 250, 255};
    uint16_t total = 256;

    auto bitmap = MuSig2OracleAggregator::EncodeBitmap(oracle_ids, total);
    BOOST_REQUIRE(!bitmap.empty());
    BOOST_CHECK_EQUAL(bitmap.size(), 32u); // ceil(256/8) = 32 bytes

    auto decoded = MuSig2OracleAggregator::DecodeBitmap(bitmap, total);
    BOOST_CHECK(decoded == oracle_ids);
}

BOOST_AUTO_TEST_CASE(test_bitmap_invalid_empty)
{
    std::vector<uint8_t> empty_ids;
    auto bitmap = MuSig2OracleAggregator::EncodeBitmap(empty_ids, 21);
    BOOST_CHECK(bitmap.empty());
}

BOOST_AUTO_TEST_CASE(test_bitmap_invalid_below_threshold)
{
    // 6 oracles < ORACLE_CONSENSUS_REQUIRED (7) must be rejected.
    std::vector<uint8_t> oracle_ids = {0, 1, 2, 3, 4, 5};
    auto bitmap = MuSig2OracleAggregator::EncodeBitmap(oracle_ids, 21);
    BOOST_CHECK(bitmap.empty());
}

// ============================================================================
// Key Aggregation Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(test_aggregate_pubkey_deterministic)
{
    MuSig2OracleAggregator agg;

    std::vector<uint8_t> oracle_ids = {0, 1, 2, 3, 4, 5, 6};
    secp256k1_xonly_pubkey pk1{}, pk2{};
    secp256k1_musig_keyagg_cache cache1{}, cache2{};

    BOOST_REQUIRE(agg.ComputeAggregatePubkey(oracle_ids, pk1, cache1));

    // Clear cache to force full recomputation
    agg.ClearCache();

    BOOST_REQUIRE(agg.ComputeAggregatePubkey(oracle_ids, pk2, cache2));

    // Same inputs must always produce the same aggregate pubkey
    BOOST_CHECK(SerializeXOnly(pk1) == SerializeXOnly(pk2));
}

BOOST_AUTO_TEST_CASE(test_aggregate_pubkey_different_subsets)
{
    MuSig2OracleAggregator agg;

    std::vector<uint8_t> set_a = {0, 1, 2, 3, 4, 5, 6};
    std::vector<uint8_t> set_b = {0, 1, 2, 3, 4, 5, 7}; // oracle 7 instead of 6

    secp256k1_xonly_pubkey pk_a{}, pk_b{};
    secp256k1_musig_keyagg_cache cache_a{}, cache_b{};

    BOOST_REQUIRE(agg.ComputeAggregatePubkey(set_a, pk_a, cache_a));
    BOOST_REQUIRE(agg.ComputeAggregatePubkey(set_b, pk_b, cache_b));

    // Different oracle subsets must produce different aggregate pubkeys
    BOOST_CHECK(SerializeXOnly(pk_a) != SerializeXOnly(pk_b));
}

BOOST_AUTO_TEST_CASE(test_aggregate_pubkey_order_independent)
{
    MuSig2OracleAggregator agg;

    std::vector<uint8_t> ascending  = {0, 1, 2, 3, 4, 5, 6};
    std::vector<uint8_t> descending = {6, 5, 4, 3, 2, 1, 0};

    secp256k1_xonly_pubkey pk1{}, pk2{};
    secp256k1_musig_keyagg_cache cache1{}, cache2{};

    BOOST_REQUIRE(agg.ComputeAggregatePubkey(ascending, pk1, cache1));

    agg.ClearCache(); // force recomputation

    BOOST_REQUIRE(agg.ComputeAggregatePubkey(descending, pk2, cache2));

    // Internal sorting guarantees order independence
    BOOST_CHECK(SerializeXOnly(pk1) == SerializeXOnly(pk2));
}

BOOST_AUTO_TEST_CASE(test_aggregate_pubkey_cache)
{
    MuSig2OracleAggregator agg;

    std::vector<uint8_t> oracle_ids = {0, 1, 2, 3, 4, 5, 6};
    secp256k1_xonly_pubkey pk_computed{}, pk_cached{};
    secp256k1_musig_keyagg_cache cache{};

    // First call computes and caches
    BOOST_REQUIRE(agg.ComputeAggregatePubkey(oracle_ids, pk_computed, cache));

    // Build the bitmap that ComputeAggregatePubkey used internally
    const auto& nodes = Params().GetOracleNodes();
    auto bitmap = MuSig2OracleAggregator::EncodeBitmap(oracle_ids, static_cast<uint16_t>(nodes.size()));
    BOOST_REQUIRE(!bitmap.empty());

    // Cache lookup should succeed
    BOOST_REQUIRE(agg.GetCachedAggregatePubkey(bitmap, pk_cached));

    // Cached key must match computed key
    BOOST_CHECK(SerializeXOnly(pk_computed) == SerializeXOnly(pk_cached));

    // After clearing cache, lookup must fail
    agg.ClearCache();
    BOOST_CHECK(!agg.GetCachedAggregatePubkey(bitmap, pk_cached));
}

BOOST_AUTO_TEST_CASE(test_all_subsets_9_of_17_legacy_vector)
{
    // Legacy 9-of-17 vector coverage: C(17, 9) = 24310 combinations.
    MuSig2OracleAggregator agg;
    std::set<std::array<unsigned char, 32>> unique_keys;

    // Generate deterministic keypairs independent of chainparams.
    constexpr size_t N = 17;
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    std::vector<secp256k1_pubkey> all_pubkeys(N);

    for (size_t i = 0; i < N; ++i) {
        // Deterministic secret key from hash of index
        uint256 seed = Hash(std::to_string(i));
        secp256k1_keypair kp;
        BOOST_REQUIRE(secp256k1_keypair_create(ctx, &kp, seed.data()));
        BOOST_REQUIRE(secp256k1_keypair_pub(ctx, &all_pubkeys[i], &kp));
    }

    // Generate all C(17,9) = 24310 combinations.
    std::vector<int> selector(N, 0);
    std::fill(selector.end() - 9, selector.end(), 1);

    int count = 0;
    do {
        // Build pointer array for this subset
        std::vector<const secp256k1_pubkey*> subset_ptrs;
        for (size_t i = 0; i < N; ++i) {
            if (selector[i]) subset_ptrs.push_back(&all_pubkeys[i]);
        }
        BOOST_REQUIRE_EQUAL(subset_ptrs.size(), 9u);

        secp256k1_xonly_pubkey pk{};
        secp256k1_musig_keyagg_cache cache{};
        BOOST_REQUIRE_MESSAGE(
            agg.AggregatePubkeys(subset_ptrs.data(), subset_ptrs.size(), pk, cache),
            "Failed to aggregate subset #" + std::to_string(count));

        unique_keys.insert(SerializeXOnly(pk));
        ++count;
    } while (std::next_permutation(selector.begin(), selector.end()));

    secp256k1_context_destroy(ctx);

    // Must have visited exactly C(17,9) = 24310 subsets.
    BOOST_CHECK_EQUAL(count, 24310);

    // Every subset must produce a unique aggregate pubkey
    BOOST_CHECK_EQUAL(unique_keys.size(), 24310u);
}

// ============================================================================
// RED HORNET RH-01: Adversarial Key Aggregation Tests
// ============================================================================

// Attack Vector 1: Rogue key attack is prevented by BIP-327 KeyAgg coefficients
BOOST_AUTO_TEST_CASE(rh01_rogue_key_prevented_by_keyagg_coefficients)
{
    MuSig2OracleAggregator agg;
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    // Generate 3 honest keys
    std::vector<secp256k1_pubkey> honest_keys(3);
    for (size_t i = 0; i < 3; ++i) {
        uint256 seed = Hash(std::string("honest") + std::to_string(i));
        secp256k1_keypair kp;
        BOOST_REQUIRE(secp256k1_keypair_create(ctx, &kp, seed.data()));
        BOOST_REQUIRE(secp256k1_keypair_pub(ctx, &honest_keys[i], &kp));
    }

    // Aggregate honest keys
    std::vector<const secp256k1_pubkey*> honest_ptrs = {&honest_keys[0], &honest_keys[1], &honest_keys[2]};
    secp256k1_xonly_pubkey honest_agg{};
    secp256k1_musig_keyagg_cache honest_cache{};
    BOOST_REQUIRE(agg.AggregatePubkeys(honest_ptrs.data(), honest_ptrs.size(), honest_agg, honest_cache));

    // Generate a "rogue" 4th key — aggregate MUST differ from honest-only
    uint256 rogue_seed = Hash(std::string("rogue_attacker"));
    secp256k1_keypair rogue_kp;
    BOOST_REQUIRE(secp256k1_keypair_create(ctx, &rogue_kp, rogue_seed.data()));
    secp256k1_pubkey rogue_pk;
    BOOST_REQUIRE(secp256k1_keypair_pub(ctx, &rogue_pk, &rogue_kp));

    std::vector<const secp256k1_pubkey*> mixed_ptrs = {&honest_keys[0], &honest_keys[1], &honest_keys[2], &rogue_pk};
    secp256k1_xonly_pubkey mixed_agg{};
    secp256k1_musig_keyagg_cache mixed_cache{};
    BOOST_REQUIRE(agg.AggregatePubkeys(mixed_ptrs.data(), mixed_ptrs.size(), mixed_agg, mixed_cache));

    // Rogue key changes aggregate — attacker cannot cancel honest keys
    BOOST_CHECK(SerializeXOnly(honest_agg) != SerializeXOnly(mixed_agg));

    secp256k1_context_destroy(ctx);
}

// Attack Vector 2: Order manipulation — ComputeAggregatePubkey sorts internally
BOOST_AUTO_TEST_CASE(rh01_order_manipulation_prevented)
{
    MuSig2OracleAggregator agg;
    std::vector<uint8_t> shuffled = {6, 2, 0, 5, 1, 4, 3};
    std::vector<uint8_t> sorted = {0, 1, 2, 3, 4, 5, 6};

    secp256k1_xonly_pubkey pk1{}, pk2{};
    secp256k1_musig_keyagg_cache c1{}, c2{};

    BOOST_REQUIRE(agg.ComputeAggregatePubkey(shuffled, pk1, c1));
    agg.ClearCache();
    BOOST_REQUIRE(agg.ComputeAggregatePubkey(sorted, pk2, c2));

    BOOST_CHECK(SerializeXOnly(pk1) == SerializeXOnly(pk2));
}

// Attack Vector 3: Duplicate key injection — deduplicated
BOOST_AUTO_TEST_CASE(rh01_duplicate_key_injection_deduplicated)
{
    MuSig2OracleAggregator agg;
    std::vector<uint8_t> with_dupes = {0, 1, 2, 3, 4, 5, 5, 5, 6};
    std::vector<uint8_t> no_dupes = {0, 1, 2, 3, 4, 5, 6};

    secp256k1_xonly_pubkey pk1{}, pk2{};
    secp256k1_musig_keyagg_cache c1{}, c2{};

    BOOST_REQUIRE(agg.ComputeAggregatePubkey(with_dupes, pk1, c1));
    agg.ClearCache();
    BOOST_REQUIRE(agg.ComputeAggregatePubkey(no_dupes, pk2, c2));

    BOOST_CHECK(SerializeXOnly(pk1) == SerializeXOnly(pk2));
}

// Attack Vector 4: Below-threshold sets rejected
BOOST_AUTO_TEST_CASE(rh01_below_threshold_rejected)
{
    // Below the 7-signature threshold.
    std::vector<uint8_t> too_few = {0, 1, 2, 3, 4, 5};
    auto bitmap = MuSig2OracleAggregator::EncodeBitmap(too_few, 21);
    BOOST_CHECK(bitmap.empty());

    MuSig2OracleAggregator agg;
    secp256k1_xonly_pubkey pk{};
    secp256k1_musig_keyagg_cache cache{};
    BOOST_CHECK(!agg.ComputeAggregatePubkey(too_few, pk, cache));
}

// Attack Vector 5: Out-of-bounds oracle ID
BOOST_AUTO_TEST_CASE(rh01_out_of_bounds_oracle_id)
{
    MuSig2OracleAggregator agg;
    std::vector<uint8_t> bad_ids = {0, 1, 2, 3, 4, 5, 6, 7, 250};
    secp256k1_xonly_pubkey pk{};
    secp256k1_musig_keyagg_cache cache{};

    auto bitmap = MuSig2OracleAggregator::EncodeBitmap(bad_ids, 21);
    BOOST_CHECK(bitmap.empty());
    BOOST_CHECK(!agg.ComputeAggregatePubkey(bad_ids, pk, cache));
}

// Attack Vector 6: Bitmap size mismatch
BOOST_AUTO_TEST_CASE(rh01_bitmap_size_mismatch_rejected)
{
    // 21 oracles means bitmap size should be ceil(21/8) = 3 bytes.
    // An incorrectly-sized 4-byte bitmap should be rejected
    std::vector<unsigned char> bad_bitmap = {0xFF, 0xFF, 0x1F, 0x00};
    auto decoded = MuSig2OracleAggregator::DecodeBitmap(bad_bitmap, 21);
    BOOST_CHECK(decoded.empty());
}

// Attack Vector 7: AggregatePubkeys rejects null/zero inputs
BOOST_AUTO_TEST_CASE(rh01_aggregate_null_inputs)
{
    MuSig2OracleAggregator agg;
    secp256k1_xonly_pubkey pk{};
    secp256k1_musig_keyagg_cache cache{};

    BOOST_CHECK(!agg.AggregatePubkeys(nullptr, 0, pk, cache));
    BOOST_CHECK(!agg.AggregatePubkeys(nullptr, 5, pk, cache));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(musig2_aggregator_regtest_tests, RegTestingSetup)

BOOST_AUTO_TEST_CASE(regtest_aggregate_pubkey_uses_configured_threshold)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    BOOST_REQUIRE_EQUAL(consensus.nOracleConsensusRequired, 4);
    BOOST_REQUIRE_EQUAL(consensus.nOracleTotalOracles, 7);

    MuSig2OracleAggregator agg;
    secp256k1_xonly_pubkey pk{};
    secp256k1_musig_keyagg_cache cache{};

    std::vector<uint8_t> threshold_ids = {0, 1, 2, 3};
    BOOST_CHECK_MESSAGE(
        agg.ComputeAggregatePubkey(threshold_ids, pk, cache),
        "regtest 4-of-7 MuSig2 aggregation must accept exactly the configured threshold");

    agg.ClearCache();
    std::vector<uint8_t> below_threshold_ids = {0, 1, 2};
    BOOST_CHECK_MESSAGE(
        !agg.ComputeAggregatePubkey(below_threshold_ids, pk, cache),
        "regtest MuSig2 aggregation must still reject below-threshold participant sets");
}

BOOST_AUTO_TEST_SUITE_END()
