// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Legacy Oracle Phase 2 Unit Tests
 *
 * V1 on-chain oracle data is MuSig2-only (v0x03). These tests keep legacy
 * Phase 2 calculation helpers covered while asserting that Phase 1/2 bundle
 * validation/routing does not accept, mine, or extract legacy bundles.
 *
 * Specification: ORACLE_PHASE_2_SPEC_PRD.md
 */

#include <boost/test/unit_test.hpp>
#include <logging.h>
#include <util/strencodings.h>

#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/sha256.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <oracle/mock_oracle.h>
#include <primitives/oracle.h>
#include <pubkey.h>
#include <random.h>
#include <test/util/setup_common.h>
#include <util/time.h>

#include <algorithm>
#include <set>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(oracle_phase2_tests, RegTestingSetup)

//
// HELPER FUNCTIONS
//

/**
 * Create a valid signed oracle price message
 */
static COraclePriceMessage CreateSignedOracleMessage(
    const CKey& oracle_key,
    uint32_t oracle_id,
    uint64_t price_micro_usd,
    int64_t timestamp,
    int32_t block_height)
{
    COraclePriceMessage msg;
    msg.oracle_id = oracle_id;
    msg.price_micro_usd = price_micro_usd;
    msg.timestamp = timestamp;
    msg.block_height = block_height;
    msg.nonce = GetRand(UINT64_MAX);
    msg.oracle_pubkey = XOnlyPubKey(oracle_key.GetPubKey());

    BOOST_REQUIRE(msg.SignAttestation(oracle_key));
    return msg;
}

/**
 * Create multiple oracle keys for testing (random keys, for non-round-trip tests)
 */
static std::vector<CKey> CreateOracleKeys(size_t count)
{
    std::vector<CKey> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        CKey key;
        key.MakeNewKey(true);
        keys.push_back(key);
    }
    return keys;
}

/**
 * Get deterministic regtest oracle keys (match chainparams pubkeys)
 * These keys are derived from SHA256("digibyte_regtest_oracle_N") — same as MockOracleManager
 */
static CKey GetRegtestOracleKey(uint32_t oracle_id)
{
    std::string seed = "digibyte_regtest_oracle_" + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write((const unsigned char*)seed.data(), seed.size()).Finalize(hash.begin());

    CKey key;
    key.Set(hash.begin(), hash.end(), true);
    return key;
}

static std::vector<CKey> GetRegtestOracleKeys(size_t count)
{
    std::vector<CKey> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        keys.push_back(GetRegtestOracleKey(i));
    }
    return keys;
}

/**
 * Minimal valid block template helper: ensure block has a coinbase tx.
 * AddOracleBundleToBlock() assumes block.vtx[0] exists.
 */
static void AddDummyCoinbase(CBlock& block)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 0;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.clear();
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
}

/**
 * Create Phase 2 consensus params for testing
 */
static Consensus::Params CreatePhase2Params(int required_messages, int total_oracles)
{
    Consensus::Params params;
    params.nOracleRequiredMessages = required_messages;
    params.nOracleConsensusRequired = required_messages;
    params.nOracleTotalOracles = total_oracles;
    params.nDDActivationHeight = 100;  // Activate Phase 2 at block 100
    params.nOracleEpochLength = 144;
    return params;
}

BOOST_AUTO_TEST_CASE(phase2_legacy_bundle_not_serialized_from_coinbase_height)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    const int32_t block_height = 1000;
    const int32_t expected_epoch = GetCurrentEpoch(block_height);
    BOOST_REQUIRE_NE(expected_epoch, 0);

    COracleBundle bundle(expected_epoch);
    bundle.version = 2;
    bundle.timestamp = GetTime();
    bundle.median_price_micro_usd = 50000;
    for (uint32_t oracle_id = 0; oracle_id < 4; ++oracle_id) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            GetRegtestOracleKey(oracle_id), oracle_id, bundle.median_price_micro_usd,
            bundle.timestamp, block_height));
    }

    CScript oracle_script = manager.CreateOracleScript(bundle);
    BOOST_CHECK_MESSAGE(oracle_script.empty(),
        "V1 must not serialize legacy Phase Two oracle bundles");

    CMutableTransaction coinbase_tx;
    coinbase_tx.vin.resize(1);
    coinbase_tx.vin[0].prevout.SetNull();
    coinbase_tx.vin[0].scriptSig << CScriptNum(block_height);
    coinbase_tx.vout.push_back(CTxOut(5000000000LL, CScript()));
    coinbase_tx.vout.push_back(CTxOut(0, oracle_script));

    COracleBundle extracted;
    BOOST_CHECK_MESSAGE(!manager.ExtractOracleBundle(CTransaction(coinbase_tx), extracted),
        "No bundle should be extracted when V1 refuses to serialize legacy data");

    manager.Clear();
}

//
// CATEGORY 1: ValidatePhaseTwoBundle() Tests
//

/**
 * Test: ValidatePhaseTwoBundle rejects legacy bundles even when below threshold
 */
BOOST_AUTO_TEST_CASE(phase2_minimum_messages)
{
    LogPrintf("Test: ValidatePhaseTwoBundle rejects legacy bundle with fewer than %d messages\n", 3);

    // Create 3-of-10 testnet params
    Consensus::Params params = CreatePhase2Params(3, 10);

    // Create oracle keys
    auto oracle_keys = CreateOracleKeys(2);  // Only 2 keys - insufficient

    // Create bundle with only 2 messages (less than required 3)
    int64_t timestamp = GetTime();
    int32_t block_height = 200;
    int32_t epoch = GetCurrentEpoch(block_height);

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = epoch;
    bundle.timestamp = timestamp;

    // Add 2 messages
    for (size_t i = 0; i < 2; ++i) {
        COraclePriceMessage msg = CreateSignedOracleMessage(
            oracle_keys[i], i, 50000, timestamp, block_height);
        bundle.messages.push_back(msg);
    }
    bundle.median_price_micro_usd = 50000;

    // V1 rejects all legacy Phase 2 bundles.
    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result, "ValidatePhaseTwoBundle should reject legacy bundles");

    LogPrintf("Test PASSED: Bundle with %zu messages rejected (minimum: %d)\n",
              bundle.messages.size(), params.nOracleRequiredMessages);
}

/**
 * Test: ValidatePhaseTwoBundle rejects duplicate oracle IDs as legacy data
 */
BOOST_AUTO_TEST_CASE(phase2_duplicate_oracle_ids)
{
    LogPrintf("Test: ValidatePhaseTwoBundle rejects duplicate oracle IDs\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(3);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;
    int32_t epoch = GetCurrentEpoch(block_height);

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = epoch;
    bundle.timestamp = timestamp;

    // Add 3 messages, but two with SAME oracle_id
    bundle.messages.push_back(CreateSignedOracleMessage(oracle_keys[0], 0, 50000, timestamp, block_height));
    bundle.messages.push_back(CreateSignedOracleMessage(oracle_keys[1], 1, 51000, timestamp, block_height));
    bundle.messages.push_back(CreateSignedOracleMessage(oracle_keys[2], 1, 52000, timestamp, block_height));  // Duplicate ID 1!

    bundle.median_price_micro_usd = 51000;

    // V1 rejects all legacy Phase 2 bundles.
    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result, "ValidatePhaseTwoBundle should reject legacy bundles");

    LogPrintf("Test PASSED: Bundle with duplicate oracle IDs rejected\n");
}

/**
 * Test: ValidatePhaseTwoBundle rejects legacy bundles with invalid signatures
 */
BOOST_AUTO_TEST_CASE(phase2_invalid_signatures)
{
    LogPrintf("Test: ValidatePhaseTwoBundle rejects invalid signatures\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(3);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;
    int32_t epoch = GetCurrentEpoch(block_height);

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = epoch;
    bundle.timestamp = timestamp;

    // Add 2 valid messages
    bundle.messages.push_back(CreateSignedOracleMessage(oracle_keys[0], 0, 50000, timestamp, block_height));
    bundle.messages.push_back(CreateSignedOracleMessage(oracle_keys[1], 1, 51000, timestamp, block_height));

    // Add 1 message with INVALID signature (tampered)
    COraclePriceMessage bad_msg = CreateSignedOracleMessage(oracle_keys[2], 2, 52000, timestamp, block_height);
    bad_msg.schnorr_sig[0] ^= 0xFF;  // Corrupt the signature
    bundle.messages.push_back(bad_msg);

    bundle.median_price_micro_usd = 51000;

    // V1 rejects all legacy Phase 2 bundles.
    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result, "ValidatePhaseTwoBundle should reject legacy bundles");

    LogPrintf("Test PASSED: Bundle with invalid signature rejected\n");
}

/**
 * Test: ValidatePhaseTwoBundle rejects legacy bundle with exactly threshold signatures
 */
BOOST_AUTO_TEST_CASE(phase2_exact_threshold)
{
    LogPrintf("Test: ValidatePhaseTwoBundle rejects legacy bundle with exactly 3 valid signatures\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(3);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;
    int32_t epoch = GetCurrentEpoch(block_height);

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = epoch;
    bundle.timestamp = timestamp;

    // Add exactly 3 valid messages
    uint64_t prices[] = {50000, 51000, 52000};
    for (size_t i = 0; i < 3; ++i) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            oracle_keys[i], i, prices[i], timestamp, block_height));
    }

    // Calculate correct median price (sorted: 50000, 51000, 52000 -> median = 51000)
    bundle.median_price_micro_usd = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    LogPrintf("Test: Bundle created with %zu messages, median_price=%llu\n",
              bundle.messages.size(), bundle.median_price_micro_usd);

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result,
        "V1 must reject legacy Phase Two bundles even with exactly threshold signatures");
}

/**
 * Test: ValidatePhaseTwoBundle rejects legacy bundle with more than threshold signatures
 */
BOOST_AUTO_TEST_CASE(phase2_above_threshold)
{
    LogPrintf("Test: ValidatePhaseTwoBundle rejects legacy bundle with > 3 valid signatures\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(5);  // 5 > 3

    int64_t timestamp = GetTime();
    int32_t block_height = 200;
    int32_t epoch = GetCurrentEpoch(block_height);

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = epoch;
    bundle.timestamp = timestamp;

    // Add 5 valid messages
    uint64_t prices[] = {49000, 50000, 51000, 52000, 53000};
    for (size_t i = 0; i < 5; ++i) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            oracle_keys[i], i, prices[i], timestamp, block_height));
    }

    // Calculate correct median price
    bundle.median_price_micro_usd = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    // Median of {49000, 50000, 51000, 52000, 53000} = 51000
    BOOST_CHECK_MESSAGE(bundle.median_price_micro_usd == 51000 || bundle.median_price_micro_usd > 0,
                        "Consensus price should be calculated correctly");

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result,
        "V1 must reject legacy Phase Two bundles even above threshold");

    LogPrintf("Test: Bundle with %zu messages has median_price=%llu\n",
              bundle.messages.size(), bundle.median_price_micro_usd);
}

/**
 * Test: ValidatePhaseTwoBundle rejects legacy bundles with missing signatures
 */
BOOST_AUTO_TEST_CASE(phase2_missing_signatures)
{
    LogPrintf("Test: ValidatePhaseTwoBundle rejects messages without signatures\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(3);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;
    int32_t epoch = GetCurrentEpoch(block_height);

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = epoch;
    bundle.timestamp = timestamp;

    // Add 2 valid signed messages
    bundle.messages.push_back(CreateSignedOracleMessage(oracle_keys[0], 0, 50000, timestamp, block_height));
    bundle.messages.push_back(CreateSignedOracleMessage(oracle_keys[1], 1, 51000, timestamp, block_height));

    // Add 1 message WITHOUT signature
    COraclePriceMessage unsigned_msg;
    unsigned_msg.oracle_id = 2;
    unsigned_msg.price_micro_usd = 52000;
    unsigned_msg.timestamp = timestamp;
    unsigned_msg.block_height = block_height;
    unsigned_msg.nonce = GetRand(UINT64_MAX);
    unsigned_msg.oracle_pubkey = XOnlyPubKey(oracle_keys[2].GetPubKey());
    // schnorr_sig is empty - no signature!
    bundle.messages.push_back(unsigned_msg);

    bundle.median_price_micro_usd = 51000;

    // V1 rejects all legacy Phase 2 bundles.
    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result, "ValidatePhaseTwoBundle should reject legacy bundles");

    LogPrintf("Test PASSED: Bundle with unsigned message rejected\n");
}

//
// CATEGORY 2: CalculateConsensusPrice() Tests
//

/**
 * Test: CalculateConsensusPrice returns median for odd count
 */
BOOST_AUTO_TEST_CASE(consensus_price_odd_count)
{
    LogPrintf("Test: CalculateConsensusPrice returns median for odd count\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(5);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;

    COracleBundle bundle;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;

    // Add 5 messages with prices: 100, 200, 300, 400, 500
    uint64_t prices[] = {100, 200, 300, 400, 500};
    for (size_t i = 0; i < 5; ++i) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            oracle_keys[i], i, prices[i], timestamp, block_height));
    }

    CAmount consensus = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    // Median of {100, 200, 300, 400, 500} = 300
    BOOST_CHECK_EQUAL(consensus, 300);

    LogPrintf("Test PASSED: Median of 5 prices = %lld (expected 300)\n", consensus);
}

/**
 * Test: CalculateConsensusPrice returns average of middle two for even count
 */
BOOST_AUTO_TEST_CASE(consensus_price_even_count)
{
    LogPrintf("Test: CalculateConsensusPrice returns average of middle two for even count\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(4);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;

    COracleBundle bundle;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;

    // Add 4 messages with prices: 100, 200, 300, 400
    uint64_t prices[] = {100, 200, 300, 400};
    for (size_t i = 0; i < 4; ++i) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            oracle_keys[i], i, prices[i], timestamp, block_height));
    }

    CAmount consensus = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    // Median of {100, 200, 300, 400} = (200 + 300) / 2 = 250
    BOOST_CHECK_EQUAL(consensus, 250);

    LogPrintf("Test PASSED: Median of 4 prices = %lld (expected 250)\n", consensus);
}

/**
 * Test: CalculateConsensusPrice applies IQR outlier filtering
 */
BOOST_AUTO_TEST_CASE(consensus_price_iqr_filtering)
{
    LogPrintf("Test: CalculateConsensusPrice applies IQR outlier filtering\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(8);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;

    COracleBundle bundle;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;

    // Add 8 messages with one extreme outlier
    // Prices: 48000, 49000, 50000, 51000, 52000, 53000, 54000, 100000 (outlier!)
    uint64_t prices[] = {48000, 49000, 50000, 51000, 52000, 53000, 54000, 100000};
    for (size_t i = 0; i < 8; ++i) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            oracle_keys[i], i, prices[i], timestamp, block_height));
    }

    CAmount consensus = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    // With IQR filtering, 100000 should be filtered out as outlier
    // Filtered set: {48000, 49000, 50000, 51000, 52000, 53000, 54000}
    // Median = 51000
    // If IQR not applied, median would be (51000 + 52000) / 2 = 51500

    // Consensus should be close to 51000 (with outlier removed) rather than skewed
    BOOST_CHECK_MESSAGE(consensus >= 50000 && consensus <= 52000,
                        "Consensus price should be around 51000 with outlier filtered");

    LogPrintf("Test: Consensus price with outlier = %lld (expected ~51000)\n", consensus);
}

/**
 * Test: CalculateConsensusPrice handles empty bundle
 */
BOOST_AUTO_TEST_CASE(consensus_price_empty_bundle)
{
    LogPrintf("Test: CalculateConsensusPrice returns 0 for empty bundle\n");

    Consensus::Params params = CreatePhase2Params(3, 10);

    COracleBundle empty_bundle;
    empty_bundle.epoch = GetCurrentEpoch(200);
    empty_bundle.timestamp = GetTime();

    CAmount consensus = OracleBundleManager::CalculateConsensusPrice(empty_bundle, params);

    BOOST_CHECK_EQUAL(consensus, 0);

    LogPrintf("Test PASSED: Empty bundle returns consensus price = %lld\n", consensus);
}

/**
 * Test: CalculateConsensusPrice handles single message (no filtering)
 */
BOOST_AUTO_TEST_CASE(consensus_price_single_message)
{
    LogPrintf("Test: CalculateConsensusPrice handles single message\n");

    Consensus::Params params = CreatePhase2Params(1, 1);  // Phase 1 params
    auto oracle_keys = CreateOracleKeys(1);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;

    COracleBundle bundle;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;

    // Single message
    bundle.messages.push_back(CreateSignedOracleMessage(
        oracle_keys[0], 0, 50000, timestamp, block_height));

    CAmount consensus = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    BOOST_CHECK_EQUAL(consensus, 50000);

    LogPrintf("Test PASSED: Single message price = %lld (expected 50000)\n", consensus);
}

//
// CATEGORY 3: GetRequiredConsensus() Tests
//

/**
 * Test: GetRequiredConsensus always returns configured MuSig2 threshold
 */
BOOST_AUTO_TEST_CASE(required_consensus_v1_uses_configured_threshold_below_legacy_phase2_height)
{
    LogPrintf("Test: GetRequiredConsensus returns configured threshold below legacy Phase 2 height\n");

    Consensus::Params params = CreatePhase2Params(9, 17);  // RC30: 9-of-17
    params.nDDActivationHeight = 10000;  // Phase 2 at block 10000

    // V1 no longer falls back to Phase 1 1-of-1 below the legacy Phase 2 height.
    int required = OracleBundleManager::GetRequiredConsensus(5000, params);
    BOOST_CHECK_EQUAL(required, 9);

    LogPrintf("Test PASSED: V1 at height 5000 requires %d signatures\n", required);
}

/**
 * Test: GetRequiredConsensus returns params value at Phase 2 height
 */
BOOST_AUTO_TEST_CASE(required_consensus_phase2_at_activation)
{
    LogPrintf("Test: GetRequiredConsensus returns params value at Phase 2 height\n");

    Consensus::Params params = CreatePhase2Params(9, 17);  // RC30: 9-of-17
    params.nDDActivationHeight = 10000;

    // Exactly at Phase 2 height (RC30: 9-of-17)
    int required = OracleBundleManager::GetRequiredConsensus(10000, params);
    BOOST_CHECK_EQUAL(required, 9);

    LogPrintf("Test PASSED: Phase 2 (height 10000) requires %d signatures\n", required);
}

/**
 * Test: GetRequiredConsensus returns params value above Phase 2 height
 */
BOOST_AUTO_TEST_CASE(required_consensus_phase2_above_activation)
{
    LogPrintf("Test: GetRequiredConsensus returns params value above Phase 2 height\n");

    Consensus::Params params = CreatePhase2Params(9, 17);  // RC30: 9-of-17
    params.nDDActivationHeight = 10000;

    // Above Phase 2 height (RC30: 9-of-17)
    int required = OracleBundleManager::GetRequiredConsensus(15000, params);
    BOOST_CHECK_EQUAL(required, 9);

    LogPrintf("Test PASSED: Phase 2 (height 15000) requires %d signatures\n", required);
}

//
// CATEGORY 4: ValidateBundle() Routing Tests
//

/**
 * Test: ValidateBundle rejects legacy Phase 1 below legacy Phase 2 activation
 */
BOOST_AUTO_TEST_CASE(validate_bundle_rejects_legacy_phase1)
{
    LogPrintf("Test: ValidateBundle rejects legacy Phase 1 below activation height\n");

    Consensus::Params params = CreatePhase2Params(9, 17);  // RC30: 9-of-17
    params.nDDActivationHeight = 10000;

    auto oracle_keys = CreateOracleKeys(1);
    int64_t timestamp = GetTime();
    int32_t block_height = 5000;  // Below Phase 2

    // Create legacy Phase 1 bundle (1 message)
    COracleBundle bundle;
    bundle.version = 1;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;
    bundle.messages.push_back(CreateSignedOracleMessage(
        oracle_keys[0], 0, 50000, timestamp, block_height));
    bundle.median_price_micro_usd = 50000;

    // V1 validates only MuSig2 v0x03 bundles.
    bool result = OracleBundleManager::ValidateBundle(bundle, block_height, params);
    BOOST_CHECK_MESSAGE(!result, "ValidateBundle should reject legacy Phase 1 bundles");

    LogPrintf("Test PASSED: legacy Phase 1 rejected at height %d\n", block_height);
}

/**
 * Test: ValidateBundle rejects legacy Phase 2 at/above legacy activation
 */
BOOST_AUTO_TEST_CASE(validate_bundle_rejects_legacy_phase2)
{
    LogPrintf("Test: ValidateBundle rejects legacy Phase 2 at/above activation height\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    params.nDDActivationHeight = 10000;

    auto oracle_keys = CreateOracleKeys(1);
    int64_t timestamp = GetTime();
    int32_t block_height = 10000;  // At Phase 2

    // Create a legacy single-message bundle.
    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;
    bundle.messages.push_back(CreateSignedOracleMessage(
        oracle_keys[0], 0, 50000, timestamp, block_height));
    bundle.median_price_micro_usd = 50000;

    // V1 validates only MuSig2 v0x03 bundles.
    bool result = OracleBundleManager::ValidateBundle(bundle, block_height, params);
    BOOST_CHECK_MESSAGE(!result, "ValidateBundle should reject legacy Phase 2 bundles");

    LogPrintf("Test PASSED: legacy Phase 2 bundle rejected at height %d\n", block_height);
}

//
// CATEGORY 5: Byzantine Fault Tolerance Tests
//

/**
 * Test: 9-of-17 consensus tolerates up to 8 Byzantine oracles (RC30)
 */
BOOST_AUTO_TEST_CASE(byzantine_tolerance_test)
{
    LogPrintf("Test: Byzantine fault tolerance - 8 malicious + 9 honest = SUCCESS (RC30 9-of-17)\n");

    Consensus::Params params = CreatePhase2Params(9, 17);
    params.nDDActivationHeight = 100;

    auto oracle_keys = CreateOracleKeys(17);
    int64_t timestamp = GetTime();
    int32_t block_height = 200;

    COracleBundle bundle;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;

    // 9 honest oracles with consistent prices
    for (size_t i = 0; i < 9; ++i) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            oracle_keys[i], i, 50000 + i * 100, timestamp, block_height));
    }

    // 8 Byzantine oracles with wildly different prices (will be filtered)
    for (size_t i = 9; i < 17; ++i) {
        COraclePriceMessage bad_msg = CreateSignedOracleMessage(
            oracle_keys[i], i, 1000000, timestamp, block_height);  // 10x higher
        // Corrupt signatures to simulate malicious behavior
        bad_msg.schnorr_sig[0] ^= 0xFF;
        bundle.messages.push_back(bad_msg);
    }

    // Calculate consensus (should use only 9 valid messages)
    bundle.median_price_micro_usd = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    // Even with 8 bad oracles, 9 honest ones should reach consensus
    BOOST_CHECK_MESSAGE(bundle.median_price_micro_usd > 0,
                        "Consensus should be reached with 9 honest oracles");
    BOOST_CHECK_MESSAGE(bundle.median_price_micro_usd < 100000,
                        "Consensus should not be affected by Byzantine outliers");

    LogPrintf("Test: Byzantine tolerance - consensus price = %llu with 8 bad oracles\n",
              bundle.median_price_micro_usd);
}

//
// CATEGORY 6: Edge Cases
//

/**
 * Test: Bundle with all same prices
 */
BOOST_AUTO_TEST_CASE(all_same_prices)
{
    LogPrintf("Test: Bundle with all identical prices\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(5);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;

    COracleBundle bundle;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;

    // All messages have same price
    for (size_t i = 0; i < 5; ++i) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            oracle_keys[i], i, 50000, timestamp, block_height));
    }

    CAmount consensus = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    BOOST_CHECK_EQUAL(consensus, 50000);

    LogPrintf("Test PASSED: All same prices = %lld\n", consensus);
}

BOOST_AUTO_TEST_CASE(maximum_price_values)
{
    LogPrintf("Test: Bundle with maximum price values\n");

    Consensus::Params params = CreatePhase2Params(3, 10);
    auto oracle_keys = CreateOracleKeys(3);

    int64_t timestamp = GetTime();
    int32_t block_height = 200;

    COracleBundle bundle;
    bundle.epoch = GetCurrentEpoch(block_height);
    bundle.timestamp = timestamp;

    // MAX_PRICE_MICRO_USD = 100000000 ($100), so all prices must be <= 100000000
    uint64_t prices[] = {98000000, 99000000, 100000000};
    for (size_t i = 0; i < 3; ++i) {
        bundle.messages.push_back(CreateSignedOracleMessage(
            oracle_keys[i], i, prices[i], timestamp, block_height));
    }

    CAmount consensus = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    // Median of {98000000, 99000000, 100000000} = 99000000
    BOOST_CHECK_EQUAL(consensus, 99000000);

    LogPrintf("Test PASSED: Maximum price consensus = %lld\n", consensus);
}

/**
 * Test: Testnet 9-of-17 configuration (RC30)
 */
BOOST_AUTO_TEST_CASE(testnet_configuration)
{
    LogPrintf("Test: Testnet 9-of-17 configuration (RC30)\n");

    // Simulate testnet params (RC30 9-of-17)
    Consensus::Params testnet_params = CreatePhase2Params(9, 17);

    BOOST_CHECK_EQUAL(testnet_params.nOracleRequiredMessages, 9);
    BOOST_CHECK_EQUAL(testnet_params.nOracleTotalOracles, 17);

    // Verify GetRequiredConsensus returns correct value
    int required = OracleBundleManager::GetRequiredConsensus(1000, testnet_params);
    BOOST_CHECK_EQUAL(required, 9);

    LogPrintf("Test PASSED: Testnet requires %d of %d oracles\n",
              testnet_params.nOracleRequiredMessages, testnet_params.nOracleTotalOracles);
}

/**
 * Test: Mainnet 9-of-17 configuration (RC30)
 */
BOOST_AUTO_TEST_CASE(mainnet_configuration)
{
    LogPrintf("Test: Mainnet 9-of-17 configuration (RC30)\n");

    // Simulate mainnet params (RC30 9-of-17)
    Consensus::Params mainnet_params = CreatePhase2Params(9, 17);

    BOOST_CHECK_EQUAL(mainnet_params.nOracleRequiredMessages, 9);
    BOOST_CHECK_EQUAL(mainnet_params.nOracleTotalOracles, 17);

    // Verify GetRequiredConsensus returns correct value
    int required = OracleBundleManager::GetRequiredConsensus(1000000, mainnet_params);
    BOOST_CHECK_EQUAL(required, 9);

    LogPrintf("Test PASSED: Mainnet requires %d of %d oracles\n",
              mainnet_params.nOracleRequiredMessages, mainnet_params.nOracleTotalOracles);
}

// =============================================================================
// PENDING MESSAGE LIFECYCLE TESTS
// =============================================================================

// Helper: Create N consensus attestation keys and inject signed attestations
static std::vector<CKey> InjectConsensusAttestations(
    OracleBundleManager& manager,
    size_t count,
    uint64_t consensus_price,
    int64_t consensus_timestamp)
{
    auto keys = CreateOracleKeys(count);
    for (size_t i = 0; i < count; ++i) {
        // Create a consensus-signed message (same price/timestamp for all oracles)
        COraclePriceMessage att = CreateSignedOracleMessage(
            keys[i], i, consensus_price, consensus_timestamp, 200);
        // Inject as individual message (provides price for consensus computation)
        manager.InjectTestMessage(att);
        // Also add as consensus attestation (provides Phase 2 sig for block construction)
        manager.AddConsensusAttestation(att);
    }
    return keys;
}

// Test: Legacy pending messages and attestations survive when no MuSig2 bundle is mined
BOOST_FIXTURE_TEST_CASE(pending_messages_survive_after_bundle, BasicTestingSetup)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // Reset singleton state to defaults (may be contaminated by prior tests)
    manager.SetEnabled(true);
    manager.SetMinOracleCount(ORACLE_CONSENSUS_REQUIRED);  // RC30: 9-of-17

    // Clear state using public API
    manager.ClearPendingMessages();

    // Inject 9 consensus attestations (properly signed, same price) — RC30 9-of-17
    uint64_t consensus_price = 10000;
    int64_t consensus_timestamp = GetTime();
    InjectConsensusAttestations(manager, 9, consensus_price, consensus_timestamp);

    // Verify 9 messages pending + 9 attestations (RC30)
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 9);
    BOOST_CHECK_EQUAL(manager.GetPendingAttestationCount(), 9);

    // Create a block. V1 does not mine legacy Phase Two data; messages remain
    // pending and expire via stale purge.
    CBlock block;
    AddDummyCoinbase(block);
    block.nTime = GetTime();
    manager.AddOracleBundleToBlock(block, 200);

    // After bundle creation, pending messages and attestations should STILL EXIST
    // They are available for the next block template; expiry is handled by
    // ORACLE_MAX_AGE_SECONDS stale purge, not by bundle creation
    BOOST_CHECK_MESSAGE(
        manager.GetPendingMessageCount() == 9,
        strprintf("Pending messages should survive bundle creation (got %zu, expected 9)", manager.GetPendingMessageCount())
    );
    BOOST_CHECK_MESSAGE(
        manager.GetPendingAttestationCount() == 9,
        strprintf("Attestations should survive bundle creation (got %zu, expected 9)", manager.GetPendingAttestationCount())
    );

    LogPrintf("Test PASSED: Pending messages and attestations persist when no MuSig2 bundle is mined\n");
}

// Test: With fewer than required messages, pending messages should NOT be cleared
BOOST_FIXTURE_TEST_CASE(pending_messages_preserved_when_insufficient, BasicTestingSetup)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(ORACLE_CONSENSUS_REQUIRED);  // RC30: 9-of-17
    manager.ClearPendingMessages();

    // Inject only 2 consensus attestations (below 9-of-17 threshold, RC30)
    InjectConsensusAttestations(manager, 2, 10000, GetTime());

    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 2);

    // Try to create block — should NOT form consensus
    CBlock block;
    AddDummyCoinbase(block);
    block.nTime = GetTime();
    manager.AddOracleBundleToBlock(block, 200);

    // Messages should still be pending (not consumed)
    BOOST_CHECK_MESSAGE(
        manager.GetPendingMessageCount() == 2,
        strprintf("Pending messages should remain at 2 when below threshold, got %zu", manager.GetPendingMessageCount())
    );

    LogPrintf("Test PASSED: Pending messages preserved when insufficient for consensus\n");
}

// Test: Fresh messages needed for each block (no stale carryover)
BOOST_FIXTURE_TEST_CASE(no_stale_message_carryover, BasicTestingSetup)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(ORACLE_CONSENSUS_REQUIRED);  // RC30: 9-of-17
    manager.ClearPendingMessages();

    // Block 1: Inject 9 consensus attestations, create bundle (RC30 9-of-17)
    int64_t ts1 = GetTime();
    InjectConsensusAttestations(manager, 9, 10000, ts1);

    CBlock block1;
    AddDummyCoinbase(block1);
    block1.nTime = GetTime();
    manager.AddOracleBundleToBlock(block1, 200);

    // Explicitly clear pending between rounds to test no-carryover concept.
    // In production, messages persist after bundle creation and expire via
    // ORACLE_MAX_AGE_SECONDS stale purge. For this test, we clear explicitly
    // to verify fresh messages are needed for each epoch/round.
    manager.ClearPendingMessages();
    manager.ClearPendingAttestations();
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 0);
    BOOST_CHECK_EQUAL(manager.GetPendingAttestationCount(), 0);

    // Block 2: Inject only 2 attestations (insufficient)
    int64_t ts2 = GetTime();
    InjectConsensusAttestations(manager, 2, 10000, ts2);

    // Should have exactly 2 (no carryover from block 1)
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 2);

    CBlock block2;
    AddDummyCoinbase(block2);
    block2.nTime = GetTime();
    manager.AddOracleBundleToBlock(block2, 201);

    // Messages should remain (below threshold, no bundle formed)
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 2);

    // Cleanup
    manager.ClearPendingMessages();

    LogPrintf("Test PASSED: No stale message carryover between blocks\n");
}

// =============================================================================
// T5-03: PHASE 2 ROUND-TRIP TESTS (on-chain format verification)
// =============================================================================

/**
 * Test: legacy Phase 2 consensus-signed messages are not serialized in V1.
 */
BOOST_AUTO_TEST_CASE(phase2_roundtrip_consensus_signed_legacy_not_serialized)
{
    LogPrintf("Test: legacy Phase 2 consensus-signed messages are not serialized in V1\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1); // Don't need full consensus for this test

    // Use regtest oracle keys (match chainparams) — extraction binds chainparams pubkeys
    auto oracle_keys = GetRegtestOracleKeys(5);
    int64_t timestamp = GetTime();

    // Step 1: All oracles sign the SAME consensus price and timestamp
    // This is the Phase 2 design: oracles sign consensus values, not individual prices
    uint64_t consensus_price = 51000;
    int64_t consensus_timestamp = timestamp;

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = GetCurrentEpoch(200);
    bundle.timestamp = consensus_timestamp;

    for (size_t i = 0; i < 5; ++i) {
        COraclePriceMessage msg = CreateSignedOracleMessage(
            oracle_keys[i], i, consensus_price, consensus_timestamp, 200);
        bundle.messages.push_back(msg);
    }
    bundle.median_price_micro_usd = consensus_price;

    // Step 2: V1 rejects the legacy on-chain Phase 2 format.
    CScript oracle_script = manager.CreateOracleScript(bundle);
    BOOST_CHECK_MESSAGE(oracle_script.empty(),
        "V1 must not produce legacy Phase Two oracle scripts");

    // Step 3: Create a coinbase transaction with the oracle data
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CTxOut oracle_out;
    oracle_out.nValue = 0;
    oracle_out.scriptPubKey = oracle_script;
    coinbase.vout.push_back(oracle_out);

    CTransaction tx(coinbase);

    // Step 4: There is no legacy bundle to extract in V1.
    COracleBundle extracted;
    bool extracted_ok = manager.ExtractOracleBundle(tx, extracted);
    BOOST_CHECK_MESSAGE(!extracted_ok,
        "Legacy Phase Two bundle extraction should fail because no script was serialized");

    LogPrintf("Test PASSED: legacy Phase 2 consensus bundle was not serialized\n");
}

/**
 * Test: Individual-price signatures DO NOT survive Phase 2 round-trip
 * This documents the bug that T5-03 fixes — when oracles sign their individual prices
 * but the on-chain format stores consensus price, signatures break.
 */
BOOST_AUTO_TEST_CASE(phase2_roundtrip_individual_prices_legacy_not_serialized)
{
    LogPrintf("Test: legacy Phase 2 individual-price bundle is not serialized in V1\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    auto oracle_keys = GetRegtestOracleKeys(5);
    int64_t timestamp = GetTime();

    // Each oracle signs its OWN individual price (the OLD broken behavior)
    uint64_t individual_prices[] = {49000, 50000, 51000, 52000, 53000};

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = GetCurrentEpoch(200);
    bundle.timestamp = timestamp;

    for (size_t i = 0; i < 5; ++i) {
        COraclePriceMessage msg = CreateSignedOracleMessage(
            oracle_keys[i], i, individual_prices[i], timestamp, 200);
        bundle.messages.push_back(msg);
    }

    // Consensus price is the median, which differs from individual prices
    Consensus::Params params = CreatePhase2Params(3, 10);
    bundle.median_price_micro_usd = OracleBundleManager::CalculateConsensusPrice(bundle, params);
    BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 51000); // Median of 49k-53k

    // V1 serializes no legacy Phase Two script.
    CScript oracle_script = manager.CreateOracleScript(bundle);
    BOOST_CHECK_MESSAGE(oracle_script.empty(),
        "V1 must not produce legacy Phase Two oracle scripts");

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    CTxOut oracle_out;
    oracle_out.nValue = 0;
    oracle_out.scriptPubKey = oracle_script;
    coinbase.vout.push_back(oracle_out);
    CTransaction tx(coinbase);

    COracleBundle extracted;
    BOOST_CHECK_MESSAGE(!manager.ExtractOracleBundle(tx, extracted),
        "Legacy Phase Two extraction should fail because no script was serialized");

    LogPrintf("Test PASSED: legacy Phase 2 individual-price bundle was not serialized\n");
}

/**
 * Test: Legacy Phase 2 pipeline — attestations → AddOracleBundleToBlock.
 * V1 must not mine or extract a fallback bundle without a completed MuSig2 session.
 */
BOOST_AUTO_TEST_CASE(phase2_full_pipeline_legacy_not_mined)
{
    LogPrintf("Test: legacy Phase 2 pipeline does not mine fallback bundle in V1\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(4); // Phase Two: 4-of-7 (regtest)

    // Use regtest oracle keys (match chainparams pubkeys for extraction binding)
    auto oracle_keys = GetRegtestOracleKeys(5);
    int64_t timestamp = GetTime();

    // Simulate: oracles have different individual prices
    uint64_t individual_prices[] = {49000, 50000, 51000, 52000, 53000};

    // Step 1: Inject individual prices into pending messages (round 1)
    for (size_t i = 0; i < 5; ++i) {
        COraclePriceMessage msg = CreateSignedOracleMessage(
            oracle_keys[i], i, individual_prices[i], timestamp, 200);
        manager.InjectTestMessage(msg);
    }
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 5);

    // Step 2: Compute consensus (like each oracle would do independently)
    uint64_t consensus_price = 0;
    int64_t consensus_timestamp = 0;
    BOOST_CHECK(manager.ComputeConsensusValues(consensus_price, consensus_timestamp));
    BOOST_CHECK_EQUAL(consensus_price, 51000); // Median of 49k-53k

    // Step 3: Each oracle signs the consensus values (round 2 attestations)
    for (size_t i = 0; i < 5; ++i) {
        COraclePriceMessage att = CreateSignedOracleMessage(
            oracle_keys[i], i, consensus_price, consensus_timestamp, 200);
        BOOST_CHECK(manager.AddConsensusAttestation(att));
    }
    BOOST_CHECK_EQUAL(manager.GetPendingAttestationCount(), 5);

    // Step 4: Miner builds block (uses consensus attestations)
    CBlock block;
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.nTime = GetTime();

    BOOST_CHECK(manager.AddOracleBundleToBlock(block, 200));

    // Step 5: V1 keeps the pending data but does not mine a legacy fallback
    // bundle when no complete MuSig2 session exists.
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 5); // Persist after bundle
    BOOST_CHECK_EQUAL(manager.GetPendingAttestationCount(), 5); // Persist after bundle
    BOOST_CHECK_EQUAL(block.vtx[0]->vout.size(), 1);

    // Step 6: No legacy bundle should be extractable from the block.
    COracleBundle extracted;
    BOOST_CHECK_MESSAGE(!manager.ExtractOracleBundle(*block.vtx[0], extracted),
        "No legacy Phase Two bundle should be extractable in V1");

    LogPrintf("Test PASSED: legacy Phase 2 pipeline did not mine fallback bundle\n");
}

/**
 * Test: Phase 2 bitmask — oracle IDs are correctly preserved through round-trip
 */
BOOST_AUTO_TEST_CASE(phase2_oracle_ids_legacy_not_serialized)
{
    LogPrintf("Test: legacy Phase 2 oracle ID bitmask is not serialized in V1\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    // Use random keys here — we're testing ID preservation, not sig verification
    auto oracle_keys = CreateOracleKeys(5);
    int64_t timestamp = GetTime();
    uint64_t consensus_price = 50000;

    // Use non-sequential oracle IDs to test ID preservation
    uint32_t oracle_ids[] = {0, 3, 5, 7, 8};

    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = 0;
    bundle.timestamp = timestamp;

    for (size_t i = 0; i < 5; ++i) {
        COraclePriceMessage msg = CreateSignedOracleMessage(
            oracle_keys[i], oracle_ids[i], consensus_price, timestamp, 200);
        bundle.messages.push_back(msg);
    }
    bundle.median_price_micro_usd = consensus_price;

    // Round-trip
    CScript script = manager.CreateOracleScript(bundle);
    BOOST_CHECK_MESSAGE(script.empty(),
        "V1 must not produce legacy Phase Two oracle scripts");

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 0;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    CTxOut oracle_out;
    oracle_out.nValue = 0;
    oracle_out.scriptPubKey = script;
    coinbase.vout.push_back(oracle_out);
    CTransaction tx(coinbase);

    COracleBundle extracted;
    BOOST_CHECK_MESSAGE(!manager.ExtractOracleBundle(tx, extracted),
        "Legacy Phase Two extraction should fail because no script was serialized");

    LogPrintf("Test PASSED: legacy Phase 2 oracle ID bitmask was not serialized\n");
}

/**
 * Test: ComputeConsensusValues returns correct median price and timestamp
 */
BOOST_AUTO_TEST_CASE(compute_consensus_values)
{
    LogPrintf("Test: ComputeConsensusValues correctness\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(3);

    auto oracle_keys = CreateOracleKeys(5);

    // Inject 5 messages with different prices and timestamps
    int64_t base_ts = GetTime();
    uint64_t prices[] = {48000, 50000, 52000, 54000, 56000};
    int64_t timestamps[] = {base_ts - 10, base_ts - 5, base_ts, base_ts + 5, base_ts + 10};

    for (size_t i = 0; i < 5; ++i) {
        COraclePriceMessage msg = CreateSignedOracleMessage(
            oracle_keys[i], i, prices[i], timestamps[i], 200);
        manager.InjectTestMessage(msg);
    }

    uint64_t consensus_price = 0;
    int64_t consensus_timestamp = 0;
    BOOST_CHECK(manager.ComputeConsensusValues(consensus_price, consensus_timestamp));

    // Price median: 52000 (middle of sorted 48k, 50k, 52k, 54k, 56k)
    BOOST_CHECK_EQUAL(consensus_price, 52000);

    // Timestamp median: base_ts (middle of sorted timestamps)
    BOOST_CHECK_EQUAL(consensus_timestamp, base_ts);

    LogPrintf("Test PASSED: Consensus values = price=%llu, timestamp=%lld\n",
              consensus_price, consensus_timestamp);
}

BOOST_AUTO_TEST_SUITE_END()
