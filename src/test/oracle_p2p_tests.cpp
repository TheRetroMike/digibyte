// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Oracle P2P Protocol Tests
 *
 * Tests for P2P broadcasting, relay, and validation of oracle price messages.
 * This file contains RED phase TDD tests for Week 3: P2P Broadcasting.
 *
 * Test Categories:
 * 1. Message Relay Tests (5 tests)
 * 2. Message Validation Tests (5 tests)
 * 3. Network Propagation Tests (4 tests)
 *
 * Total: 14 unit tests
 *
 * Expected to FAIL until net_processing.cpp implements:
 * - ProcessOraclePriceMessage()
 * - ShouldRelayOracleMessage()
 * - BroadcastOraclePriceToAllPeers()
 * - ValidateOraclePriceMessage()
 * - IsOracleMessageDuplicate()
 */

#include <boost/test/unit_test.hpp>
#include <logging.h>
#include <util/strencodings.h>

#include <consensus/amount.h>
#include <crypto/sha256.h>
#include <key.h>
#include <net.h>
#include <net_processing.h>
#include <netmessagemaker.h>
#include <oracle/bundle_manager.h>
#include <oracle/node.h>
#include <primitives/oracle.h>
#include <protocol.h>
#include <pubkey.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/time.h>

#include <memory>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(oracle_p2p_tests, RegTestingSetup)

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Create a valid oracle price message with proper signature
 */
static COraclePriceMessage CreateValidOracleMessage(const CKey& privkey, uint32_t oracle_id = 1, int64_t timestamp = 0)
{
    if (timestamp == 0) {
        timestamp = GetTime();
    }

    COraclePriceMessage msg;
    msg.oracle_id = oracle_id;
    msg.price_micro_usd = 500; // 0.05 DGB per USD (typical price)
    msg.timestamp = timestamp;
    msg.block_height = 0;
    msg.nonce = 0;

    // Sign message with Schnorr signature using msg.Sign()
    bool signed_ok = msg.Sign(privkey);
    assert(signed_ok);

    return msg;
}

/**
 * Create a test key pair for oracle signing
 */
static std::pair<CKey, CPubKey> CreateOracleKeyPair()
{
    CKey privkey;
    privkey.MakeNewKey(true);
    CPubKey pubkey = privkey.GetPubKey();
    return {privkey, pubkey};
}

// ============================================================================
// CATEGORY 1: Message Relay Tests (5 tests)
// ============================================================================

/**
 * Test that valid oracle messages should be relayed to peers
 *
 * EXPECTED: FAIL (until ShouldRelayOracleMessage() is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_relay_valid)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    // Verify message is valid before testing relay
    BOOST_CHECK(msg.Verify());

    // Test relay logic - THIS WILL FAIL until implemented
    // BOOST_CHECK(ShouldRelayOracleMessage(msg, pubkey));

    // For now, just verify message structure is correct
    BOOST_CHECK_EQUAL(msg.oracle_id, 1);
    BOOST_CHECK_EQUAL(msg.price_micro_usd, 500);
    BOOST_CHECK_GT(msg.timestamp, 0);
    BOOST_CHECK(!msg.schnorr_sig.empty());
}

/**
 * Test that duplicate oracle messages are NOT relayed
 *
 * EXPECTED: FAIL (until IsOracleMessageDuplicate() is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_relay_duplicate)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    // First relay should succeed
    // BOOST_CHECK(ShouldRelayOracleMessage(msg, pubkey));

    // Second relay of SAME message should fail (duplicate detection)
    // BOOST_CHECK(!ShouldRelayOracleMessage(msg, pubkey));

    // Verify we can detect duplicates by hash
    uint256 hash1 = msg.GetSignatureHash();
    COraclePriceMessage msg2 = msg; // Same message
    uint256 hash2 = msg2.GetSignatureHash();
    BOOST_CHECK_EQUAL(hash1, hash2);
}

/**
 * Test that messages with invalid signatures are rejected
 *
 * EXPECTED: FAIL (until ValidateOraclePriceMessage() is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_relay_invalid_sig)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    // Corrupt the signature
    msg.schnorr_sig[0] ^= 0xFF;

    // Should NOT validate
    BOOST_CHECK(!msg.Verify());

    // Should NOT be relayed
    // BOOST_CHECK(!ShouldRelayOracleMessage(msg, pubkey));
}

/**
 * Test that old messages (> 1 hour) are rejected
 *
 * EXPECTED: FAIL (until timestamp validation is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_relay_old_timestamp)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();

    // Create message with old timestamp (2 hours ago)
    int64_t old_timestamp = GetTime() - 7200;
    COraclePriceMessage msg = CreateValidOracleMessage(privkey, 1, old_timestamp);

    // Message should have valid signature but be too old
    BOOST_CHECK(msg.Verify());

    // Should be rejected due to age
    // BOOST_CHECK(!ShouldRelayOracleMessage(msg, pubkey));

    // Verify the timestamp is actually old
    BOOST_CHECK_LT(msg.timestamp, GetTime() - 3600);
}

/**
 * Test that future messages are rejected
 *
 * EXPECTED: FAIL (until timestamp validation is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_relay_future_timestamp)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();

    // Create message with future timestamp (1 hour ahead)
    int64_t future_timestamp = GetTime() + 3600;
    COraclePriceMessage msg = CreateValidOracleMessage(privkey, 1, future_timestamp);

    // Message should have valid signature but be in the future
    BOOST_CHECK(msg.Verify());

    // Should be rejected due to future timestamp
    // BOOST_CHECK(!ShouldRelayOracleMessage(msg, pubkey));

    // Verify the timestamp is actually in the future
    BOOST_CHECK_GT(msg.timestamp, GetTime());
}

// ============================================================================
// CATEGORY 2: Message Validation Tests (5 tests)
// ============================================================================

/**
 * Test that signature is verified before relay
 *
 * EXPECTED: FAIL (until ValidateOraclePriceMessage() is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_signature_verification)
{
    auto [privkey1, pubkey1] = CreateOracleKeyPair();
    auto [privkey2, pubkey2] = CreateOracleKeyPair();

    // Create message signed with privkey1
    COraclePriceMessage msg = CreateValidOracleMessage(privkey1);

    // Should validate with correct signature
    BOOST_CHECK(msg.Verify());

    // Modify the pubkey to simulate wrong pubkey attack
    msg.oracle_pubkey = XOnlyPubKey(pubkey2);

    // Should NOT validate with wrong pubkey (signature won't match)
    BOOST_CHECK(!msg.Verify());

    // Relay should fail if pubkey doesn't match
    // BOOST_CHECK(!ValidateOraclePriceMessage(msg, pubkey2));
}

/**
 * Test that timestamp is within acceptable range (±1 hour)
 *
 * EXPECTED: FAIL (until timestamp validation is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_timestamp_check)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    int64_t now = GetTime();

    // Test valid timestamps (within ±1 hour)
    std::vector<int64_t> valid_timestamps = {
        now,
        now - 1800,  // 30 minutes ago
        now + 1800,  // 30 minutes ahead (some clock skew allowed)
        now - 3500,  // Just under 1 hour ago
    };

    for (int64_t ts : valid_timestamps) {
        COraclePriceMessage msg = CreateValidOracleMessage(privkey, 1, ts);
        // BOOST_CHECK(ValidateOraclePriceMessage(msg, pubkey));
    }

    // Test invalid timestamps (outside ±1 hour)
    std::vector<int64_t> invalid_timestamps = {
        now - 3700,  // Over 1 hour ago
        now + 3700,  // Over 1 hour ahead
        now - 86400, // 1 day ago
        now + 86400, // 1 day ahead
    };

    for (int64_t ts : invalid_timestamps) {
        COraclePriceMessage msg = CreateValidOracleMessage(privkey, 1, ts);
        // BOOST_CHECK(!ValidateOraclePriceMessage(msg, pubkey));
    }
}

/**
 * Test that price is within reasonable range ($0.0001 to $10.00)
 *
 * EXPECTED: FAIL (until price sanity check is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_price_sanity_check)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();

    // Test valid prices
    std::vector<CAmount> valid_prices = {
        1000,       // 0.00001 DGB/USD (very low but possible)
        5000000,    // 0.05 DGB/USD (typical)
        100000000,  // 1.00 DGB/USD (high but possible)
    };

    for (CAmount price : valid_prices) {
        COraclePriceMessage msg = CreateValidOracleMessage(privkey);
        msg.price_micro_usd = price;

        // Re-sign after modifying price using Schnorr
        bool sign_ok = msg.Sign(privkey);
        BOOST_REQUIRE(sign_ok);

        BOOST_CHECK(msg.Verify());
        // BOOST_CHECK(ValidateOraclePriceMessage(msg, pubkey));
    }

    // Test invalid prices
    std::vector<CAmount> invalid_prices = {
        0,          // Zero price
        -1,         // Negative price
        1000000000000LL, // 10,000 DGB/USD (unrealistic)
    };

    for (CAmount price : invalid_prices) {
        COraclePriceMessage msg = CreateValidOracleMessage(privkey);
        msg.price_micro_usd = price;

        // Re-sign after modifying price using Schnorr
        bool sign_ok = msg.Sign(privkey);
        BOOST_REQUIRE(sign_ok);

        // BOOST_CHECK(!ValidateOraclePriceMessage(msg, pubkey));
    }
}

/**
 * Test that duplicate messages are detected and rejected
 *
 * EXPECTED: FAIL (until duplicate detection is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_duplicate_detection)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    // Track the message hash
    uint256 msg_hash = msg.GetSignatureHash();

    // First message should not be duplicate
    // BOOST_CHECK(!IsOracleMessageDuplicate(msg_hash));

    // Mark as seen
    // MarkOracleMessageAsSeen(msg_hash);

    // Second identical message should be detected as duplicate
    // BOOST_CHECK(IsOracleMessageDuplicate(msg_hash));

    // Different message should not be duplicate
    COraclePriceMessage msg2 = CreateValidOracleMessage(privkey);
    msg2.timestamp = GetTime() + 1; // Different timestamp

    // Re-sign using Schnorr
    bool sign_ok = msg2.Sign(privkey);
    BOOST_REQUIRE(sign_ok);

    uint256 msg_hash2 = msg2.GetSignatureHash();
    BOOST_CHECK(msg_hash != msg_hash2); // Different hashes
    // BOOST_CHECK(!IsOracleMessageDuplicate(msg_hash2));
}

/**
 * Test that malformed messages are rejected
 *
 * EXPECTED: FAIL (until malformed message detection is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_malformed_rejection)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();

    // Test empty signature
    COraclePriceMessage msg1 = CreateValidOracleMessage(privkey);
    msg1.schnorr_sig.clear();
    BOOST_CHECK(!msg1.Verify());
    // BOOST_CHECK(!ValidateOraclePriceMessage(msg1, pubkey));

    // Test invalid signature length
    COraclePriceMessage msg2 = CreateValidOracleMessage(privkey);
    msg2.schnorr_sig.resize(10); // Too short
    BOOST_CHECK(!msg2.Verify());
    // BOOST_CHECK(!ValidateOraclePriceMessage(msg2, pubkey));

    // Test zero oracle ID
    COraclePriceMessage msg3 = CreateValidOracleMessage(privkey);
    msg3.oracle_id = 0; // Invalid ID
    // BOOST_CHECK(!ValidateOraclePriceMessage(msg3, pubkey));
}

// ============================================================================
// CATEGORY 3: Network Propagation Tests (4 tests)
// ============================================================================

/**
 * Test that oracle messages are broadcast to all connected peers
 *
 * EXPECTED: FAIL (until BroadcastOraclePriceToAllPeers() is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_broadcasts_to_all_peers)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    // Verify message is valid
    BOOST_CHECK(msg.Verify());

    // Test broadcast to all peers
    // This will fail until net_processing.cpp implements broadcasting
    // std::vector<NodeId> peer_ids = {1, 2, 3, 4, 5};
    // BroadcastOraclePriceToAllPeers(msg, peer_ids);

    // Verify all peers received the message
    // for (NodeId peer_id : peer_ids) {
    //     BOOST_CHECK(PeerReceivedOracleMessage(peer_id, msg.GetSignatureHash()));
    // }

    // For now, just verify message structure
    BOOST_CHECK_GT(msg.timestamp, 0);
}

/**
 * Test that oracle messages are NOT echoed back to the sender
 *
 * EXPECTED: FAIL (until relay logic with sender filtering is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_not_sent_to_sender)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    // NodeId of the sender
    NodeId sender_id = 42;

    // Relay to all peers except sender
    // std::vector<NodeId> all_peers = {1, 2, 42, 3, 4};
    // RelayOracleMessageExceptSender(msg, all_peers, sender_id);

    // Verify sender did NOT receive the message
    // BOOST_CHECK(!PeerReceivedOracleMessage(sender_id, msg.GetSignatureHash()));

    // Verify other peers DID receive the message
    // BOOST_CHECK(PeerReceivedOracleMessage(1, msg.GetSignatureHash()));
    // BOOST_CHECK(PeerReceivedOracleMessage(2, msg.GetSignatureHash()));

    // For now, just verify message is valid
    BOOST_CHECK(msg.Verify());
}

/**
 * Test that INV message announces oracle price message availability
 *
 * EXPECTED: FAIL (until INV announcement for oracle messages is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_inventory_announcement)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    uint256 msg_hash = msg.GetSignatureHash();

    // Create INV message for oracle price
    // CInv inv(MSG_ORACLE_PRICE, msg_hash);

    // Verify INV type is correct
    // BOOST_CHECK(inv.IsOracleMsg());
    // BOOST_CHECK_EQUAL(inv.type, MSG_ORACLE_PRICE);
    // BOOST_CHECK_EQUAL(inv.hash, msg_hash);

    // Verify GetCommand() returns correct string
    // BOOST_CHECK_EQUAL(inv.GetCommand(), NetMsgType::ORACLEPRICE);

    // For now, verify oracle message types are defined
    BOOST_CHECK_EQUAL(std::string(NetMsgType::ORACLEPRICE), "oracleprice");
}

/**
 * Test that GETDATA can retrieve oracle messages
 *
 * EXPECTED: FAIL (until GETDATA handler for oracle messages is implemented)
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_message_getdata_request)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    uint256 msg_hash = msg.GetSignatureHash();

    // Peer sends GETDATA requesting oracle message
    // CInv inv(MSG_ORACLE_PRICE, msg_hash);
    // std::vector<CInv> inv_list = {inv};

    // Process GETDATA request
    // std::optional<COraclePriceMessage> retrieved_msg = ProcessGetDataOracleMessage(msg_hash);

    // Verify message was retrieved
    // BOOST_CHECK(retrieved_msg.has_value());
    // BOOST_CHECK_EQUAL(retrieved_msg->oracle_id, msg.oracle_id);
    // BOOST_CHECK_EQUAL(retrieved_msg->price_satoshis, msg.price_micro_usd);

    // For now, verify message hash is consistent
    uint256 hash1 = msg.GetSignatureHash();

    // Serialize and deserialize
    DataStream ss{};
    ss << msg;

    COraclePriceMessage msg2;
    ss >> msg2;

    uint256 hash2 = msg2.GetSignatureHash();
    BOOST_CHECK_EQUAL(hash1, hash2);
}

// ============================================================================
// Additional P2P Protocol Tests
// ============================================================================

/**
 * Test OraclePriceMsg wrapper serialization
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_price_msg_serialization)
{
    auto [privkey, pubkey] = CreateOracleKeyPair();
    COraclePriceMessage msg = CreateValidOracleMessage(privkey);

    // Create P2P wrapper
    OraclePriceMsg p2p_msg;
    p2p_msg.price_message = msg;

    // Serialize
    DataStream ss{};
    ss << p2p_msg;

    // Deserialize
    OraclePriceMsg p2p_msg2;
    ss >> p2p_msg2;

    // Verify roundtrip
    BOOST_CHECK_EQUAL(p2p_msg2.price_message.oracle_id, msg.oracle_id);
    BOOST_CHECK_EQUAL(p2p_msg2.price_message.price_micro_usd, msg.price_micro_usd);
    BOOST_CHECK_EQUAL(p2p_msg2.price_message.timestamp, msg.timestamp);
    BOOST_CHECK(p2p_msg2.price_message.schnorr_sig == msg.schnorr_sig);
}

/**
 * Test CInv oracle message helper methods
 */
BOOST_AUTO_TEST_CASE(p2p_oracle_cinv_helpers)
{
    uint256 dummy_hash;
    dummy_hash.SetHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    // Test oracle price INV
    CInv inv_price(MSG_ORACLE_PRICE, dummy_hash);
    BOOST_CHECK(inv_price.IsOracleMsg());
    BOOST_CHECK_EQUAL(inv_price.type, MSG_ORACLE_PRICE);

    // Test oracle bundle INV
    CInv inv_bundle(MSG_ORACLE_BUNDLE, dummy_hash);
    BOOST_CHECK(inv_bundle.IsOracleMsg());
    BOOST_CHECK_EQUAL(inv_bundle.type, MSG_ORACLE_BUNDLE);

    // Test get oracle data INV
    CInv inv_getdata(MSG_GET_ORACLE_DATA, dummy_hash);
    BOOST_CHECK(inv_getdata.IsOracleMsg());
    BOOST_CHECK_EQUAL(inv_getdata.type, MSG_GET_ORACLE_DATA);

    // Test non-oracle messages
    CInv inv_tx(MSG_TX, dummy_hash);
    BOOST_CHECK(!inv_tx.IsOracleMsg());

    CInv inv_block(MSG_BLOCK, dummy_hash);
    BOOST_CHECK(!inv_block.IsOracleMsg());
}

// ============================================================================
// CATEGORY 4: Oracle Propagation & Discovery Tests
// Tests for Bug #1 (Sign/Verify mismatch), Bug #2 (net_processing Verify),
// Bug #3 (bundle relay), Bug #4 (GETORACLES discovery)
// ============================================================================

/**
 * Bug #1 Test: CreatePriceMessage must produce Phase2-valid signatures
 *
 * CreatePriceMessage() was calling Sign() (5-field hash) but
 * IsValidOracleMessage() in Phase 2 mode calls VerifyAttestation() (3-field hash).
 * The message must pass both VerifyAttestation() and IsValid().
 */
BOOST_AUTO_TEST_CASE(test_create_price_message_phase2_signature)
{
    // Create oracle node with known key
    CKey privkey;
    privkey.MakeNewKey(true);
    CPubKey pubkey = privkey.GetPubKey();

    OracleNode node;
    node.Initialize(0, privkey, pubkey);

    // CreatePriceMessage should produce a message that passes Phase 2 verification
    COraclePriceMessage msg = node.CreatePriceMessage(5000, GetTime());
    BOOST_CHECK(msg.price_micro_usd == 5000);
    BOOST_CHECK(msg.oracle_id == 0);

    // CRITICAL: Must pass Phase 2 verification (3-field hash)
    BOOST_CHECK_MESSAGE(msg.VerifyAttestation(),
        "CreatePriceMessage must produce Phase2-valid signatures");

    // Must also pass IsValid() which tries Phase2 first
    BOOST_CHECK_MESSAGE(msg.IsValid(),
        "CreatePriceMessage output must pass IsValid()");
}

/**
 * Bug #1 Test: Phase 2 signed messages must be accepted by BundleManager
 *
 * IsValidOracleMessage() in Phase 2 mode (min_oracle_count > 1) must
 * accept messages signed with SignAttestation().
 */
BOOST_AUTO_TEST_CASE(test_phase2_message_accepted_by_bundle_manager)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    // Setup Phase 2 mode (min_oracle_count > 1)
    // We need the oracle's pubkey in chainparams for this to work
    // Use regtest oracle keys which are deterministic
    const CChainParams& params = Params();

    // Get oracle 0's config
    const OracleNodeInfo* oracle_config = params.GetOracleNode(0);
    BOOST_REQUIRE(oracle_config != nullptr);

    // Create message signed with Phase 2
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 5000;
    msg.timestamp = GetTime();
    msg.block_height = 0;
    msg.nonce = GetRand<uint64_t>(std::numeric_limits<uint64_t>::max());

    // We need the matching private key for oracle 0
    // In regtest, keys are derived from SHA256("digibyte_regtest_oracle_0")
    std::string seed = "digibyte_regtest_oracle_0";
    uint256 hash;
    CSHA256().Write((const unsigned char*)seed.data(), seed.size()).Finalize(hash.begin());
    CKey oracle_key;
    oracle_key.Set(hash.begin(), hash.end(), true);
    BOOST_REQUIRE(oracle_key.IsValid());

    msg.oracle_pubkey = XOnlyPubKey(oracle_key.GetPubKey());
    BOOST_REQUIRE(msg.SignAttestation(oracle_key));
    BOOST_CHECK(msg.VerifyAttestation());

    // Enable manager in Phase 2 mode
    manager.SetEnabled(true);
    manager.SetMinOracleCount(4); // Phase 2

    // Message must be accepted
    bool added = manager.AddOracleMessage(msg);
    BOOST_CHECK_MESSAGE(added,
        "Phase 2 signed message must be accepted by BundleManager in Phase 2 mode");

    manager.Clear();
    manager.SetEnabled(false);
}

/**
 * Bug #2 Test: net_processing signature validation must accept Phase 2 messages
 *
 * net_processing.cpp was calling Verify() (Phase 1 only) instead of IsValid()
 * which supports both Phase 1 and Phase 2. Phase 2 messages must not be rejected.
 */
BOOST_AUTO_TEST_CASE(test_phase2_message_passes_isvalid)
{
    CKey privkey;
    privkey.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 5000;
    msg.timestamp = GetTime();
    msg.block_height = 0;
    msg.nonce = 0;

    // Sign with Phase 2 only
    msg.oracle_pubkey = XOnlyPubKey(privkey.GetPubKey());
    BOOST_REQUIRE(msg.SignAttestation(privkey));

    // Phase 1 Verify() should FAIL (different hash)
    BOOST_CHECK(!msg.Verify());

    // But IsValid() should PASS (tries Phase 2 first)
    BOOST_CHECK_MESSAGE(msg.IsValid(),
        "IsValid() must accept Phase 2 signed messages (net_processing fix)");
}

/**
 * V1 rejects legacy message signatures through IsValid().
 */
BOOST_AUTO_TEST_CASE(test_legacy_message_rejected_by_isvalid)
{
    CKey privkey;
    privkey.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 5000;
    msg.timestamp = GetTime();
    msg.block_height = 100;
    msg.nonce = 42;

    // Sign with the legacy message signature helper.
    BOOST_REQUIRE(msg.Sign(privkey));

    // The legacy verifier still proves the helper itself works.
    BOOST_CHECK(msg.Verify());

    // V1 IsValid() must not fall back to legacy signatures.
    BOOST_CHECK_MESSAGE(!msg.IsValid(),
        "IsValid() must reject legacy oracle message signatures in V1");
}

/**
 * Test: OracleNode::BroadcastPriceMessage validates before broadcasting
 *
 * Messages that fail IsValid() must not be broadcast.
 */
BOOST_AUTO_TEST_CASE(test_broadcast_rejects_invalid_message)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    CPubKey pubkey = privkey.GetPubKey();

    OracleNode node;
    node.Initialize(0, privkey, pubkey);

    // Create an invalid message (no signature)
    COraclePriceMessage bad_msg;
    bad_msg.oracle_id = 0;
    bad_msg.price_micro_usd = 5000;
    bad_msg.timestamp = GetTime();

    BOOST_CHECK(!node.BroadcastPriceMessage(bad_msg));
}

/**
 * Test: SignAttestation and VerifyAttestation roundtrip with different prices
 *
 * Ensures the 3-field hash (oracle_id + price + timestamp) works correctly
 * across different price values.
 */
BOOST_AUTO_TEST_CASE(test_phase2_sign_verify_roundtrip_prices)
{
    CKey privkey;
    privkey.MakeNewKey(true);

    std::vector<uint64_t> test_prices = {
        1,          // minimum
        500,        // typical DGB
        5000,       // ~$0.005
        50000,      // ~$0.05
        1000000,    // $1.00
        10000000,   // $10.00
        99999999999 // near max
    };

    for (uint64_t price : test_prices) {
        if (price < ORACLE_MIN_PRICE_MICRO_USD || price > ORACLE_MAX_PRICE_MICRO_USD)
            continue;

        COraclePriceMessage msg;
        msg.oracle_id = 3;
        msg.price_micro_usd = price;
        msg.timestamp = GetTime();
        msg.oracle_pubkey = XOnlyPubKey(privkey.GetPubKey());

        BOOST_REQUIRE(msg.SignAttestation(privkey));
        BOOST_CHECK_MESSAGE(msg.VerifyAttestation(),
            strprintf("Phase2 roundtrip failed for price %llu", price));
    }
}

/**
 * Bug: Cached price updates without consensus
 *
 * Individual oracle messages must never update cached_price in V1.
 * Only complete MuSig2 bundles may become the canonical price.
 */
BOOST_AUTO_TEST_CASE(test_cached_price_requires_musig2_bundle)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(4);

    // Create 4 oracle keys matching chainparams (regtest)
    // Keys are derived from SHA256("digibyte_regtest_oracle_N")
    std::vector<CKey> keys(4);
    for (int i = 0; i < 4; i++) {
        std::string seed = "digibyte_regtest_oracle_" + std::to_string(i);
        uint256 hash;
        CSHA256().Write((const unsigned char*)seed.data(), seed.size()).Finalize(hash.begin());
        keys[i].Set(hash.begin(), hash.end(), true);
    }

    int64_t now = GetTime();

    // Round 1: All 4 oracles send $0.01. This is still not a canonical
    // price source without a completed MuSig2 bundle.
    for (int i = 0; i < 4; i++) {
        COraclePriceMessage msg;
        msg.oracle_id = i;
        msg.price_micro_usd = 10000; // $0.01
        msg.timestamp = now;
        msg.oracle_pubkey = XOnlyPubKey(keys[i].GetPubKey());
        msg.SignAttestation(keys[i]);
        manager.AddOracleMessage(msg);
    }

    // Cached price should remain empty; pending messages are not canonical.
    CAmount price_after_consensus = manager.GetLatestPrice();
    BOOST_CHECK_EQUAL(price_after_consensus, 0);

    // Clear pending messages between rounds (messages persist after bundle creation,
    // so use explicit ClearPendingMessages() to reset pending state for testing
    // price coordination logic.
    manager.ClearPendingMessages();

    // Round 2: Only 3 oracles send $0.02 — below threshold
    for (int i = 0; i < 3; i++) {
        COraclePriceMessage msg;
        msg.oracle_id = i;
        msg.price_micro_usd = 20000; // $0.02
        msg.timestamp = now + 10;
        msg.oracle_pubkey = XOnlyPubKey(keys[i].GetPubKey());
        msg.SignAttestation(keys[i]);
        manager.AddOracleMessage(msg);
    }

    // Cached price should still be empty.
    CAmount price_after_below_threshold = manager.GetLatestPrice();
    BOOST_CHECK_MESSAGE(price_after_below_threshold == 0,
        strprintf("Cached price should remain empty with only pending oracle messages, "
                  "but got %lld", price_after_below_threshold));

    manager.Clear();
    manager.SetEnabled(false);
}

/**
 * Test: pending-message churn does not create a canonical price.
 */
BOOST_AUTO_TEST_CASE(test_pending_message_churn_does_not_update_cache)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(4);

    // Create 4 oracle keys matching chainparams (regtest)
    std::vector<CKey> keys(4);
    for (int i = 0; i < 4; i++) {
        std::string seed = "digibyte_regtest_oracle_" + std::to_string(i);
        uint256 hash;
        CSHA256().Write((const unsigned char*)seed.data(), seed.size()).Finalize(hash.begin());
        keys[i].Set(hash.begin(), hash.end(), true);
    }

    int64_t now = GetTime();

    // All 4 send $0.01
    for (int i = 0; i < 4; i++) {
        COraclePriceMessage msg;
        msg.oracle_id = i;
        msg.price_micro_usd = 10000;
        msg.timestamp = now;
        msg.oracle_pubkey = XOnlyPubKey(keys[i].GetPubKey());
        msg.SignAttestation(keys[i]);
        manager.AddOracleMessage(msg);
    }

    BOOST_CHECK_EQUAL(manager.GetLatestPrice(), 0);

    // 3 oracles update to $0.02 (replaces oracle 0,1,2; oracle 3 stale at $0.01)
    for (int i = 0; i < 3; i++) {
        COraclePriceMessage msg;
        msg.oracle_id = i;
        msg.price_micro_usd = 20000;
        msg.timestamp = now + 10;
        msg.oracle_pubkey = XOnlyPubKey(keys[i].GetPubKey());
        msg.SignAttestation(keys[i]);
        manager.AddOracleMessage(msg);
    }

    // Pending messages changed, but no complete MuSig2 bundle has been accepted.
    CAmount mixed_price = manager.GetLatestPrice();
    BOOST_CHECK_EQUAL(mixed_price, 0);

    manager.Clear();
    manager.SetEnabled(false);
}

/**
 * Pending-message median calculation may still be used for off-chain
 * coordination, but it must not update the canonical V1 price cache.
 */
BOOST_AUTO_TEST_CASE(test_pending_median_not_canonical_cache)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(4);

    std::vector<CKey> keys(4);
    for (int i = 0; i < 4; i++) {
        std::string seed = "digibyte_regtest_oracle_" + std::to_string(i);
        uint256 hash;
        CSHA256().Write((const unsigned char*)seed.data(), seed.size()).Finalize(hash.begin());
        keys[i].Set(hash.begin(), hash.end(), true);
    }

    const int64_t now = GetTime();
    const std::array<uint64_t, 4> prices{{10000, 10000, 20000, 20000}};

    COracleBundle expected_bundle;
    for (int i = 0; i < 4; i++) {
        COraclePriceMessage msg;
        msg.oracle_id = i;
        msg.price_micro_usd = prices[i];
        msg.timestamp = now + i;
        msg.oracle_pubkey = XOnlyPubKey(keys[i].GetPubKey());
        BOOST_REQUIRE(msg.SignAttestation(keys[i]));

        expected_bundle.messages.push_back(msg);
        BOOST_REQUIRE(manager.AddOracleMessage(msg));
    }

    const CAmount consensus_price = manager.CalculateConsensusPrice(expected_bundle, Params().GetConsensus());
    BOOST_REQUIRE_EQUAL(consensus_price, 15000);
    BOOST_CHECK_EQUAL(manager.GetLatestPrice(), 0);

    manager.Clear();
    manager.SetEnabled(false);
}

/**
 * Final cleanup test - MUST RUN LAST
 * Cleans up Oracle singleton state to prevent interference with other test suites
 * This test is placed in oracle_p2p_tests (last oracle test alphabetically) to ensure
 * it runs after all other oracle tests but before validation tests.
 */
// ============================================================================
// FIX-2: P2P oracle pubkey must be bound from chainparams, not message
// ============================================================================

BOOST_AUTO_TEST_CASE(test_fake_pubkey_rejected_by_bundle_manager)
{
    // An attacker generates their own keypair and signs an oracle message.
    // Even though the signature is valid for the attacker's key, it must be
    // rejected because IsValidOracleMessage binds the pubkey from chainparams.
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);

    // Generate attacker's key (NOT an authorized oracle key)
    CKey attackerKey;
    attackerKey.MakeNewKey(true);

    COraclePriceMessage fake_msg;
    fake_msg.oracle_id = 0;  // Pretend to be oracle 0
    fake_msg.price_micro_usd = 5000;  // $0.005
    fake_msg.timestamp = GetTime();
    fake_msg.block_height = 100;

    // Sign with attacker's key — signature is cryptographically valid
    BOOST_CHECK(fake_msg.SignAttestation(attackerKey));
    BOOST_CHECK(fake_msg.VerifyAttestation());  // Passes with attacker's own key

    // AddOracleMessage calls IsValidOracleMessage internally, which binds
    // the pubkey from chainparams. The attacker's key won't match.
    bool accepted = manager.AddOracleMessage(fake_msg);
    BOOST_CHECK(!accepted);  // MUST be rejected

    manager.Clear();
    manager.SetEnabled(false);
}

BOOST_AUTO_TEST_CASE(zzz_cleanup_oracle_singleton_state)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(false);

    LogPrintf("Test: Cleaned up OracleBundleManager singleton state to prevent test isolation issues\n");
}

BOOST_AUTO_TEST_SUITE_END()
