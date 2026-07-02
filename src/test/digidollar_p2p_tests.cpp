// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <protocol.h>
#include <chainparams.h>
#include <oracle/bundle_manager.h>
#include <oracle/musig2_aggregator.h>
#include <oracle/musig2_messages.h>
#include <primitives/oracle.h>
#include <serialize.h>
#include <uint256.h>
#include <key.h>
#include <pubkey.h>
#include <util/time.h>
#include <net_processing.h>
#include <validation.h>
#include <hash.h>
#include <test/util/setup_common.h>

#include <vector>
#include <string>

BOOST_FIXTURE_TEST_SUITE(digidollar_p2p_tests, BasicTestingSetup)

// Test structures for oracle P2P messages
struct OraclePriceMsg {
    COraclePriceMessage price_message;

    SERIALIZE_METHODS(OraclePriceMsg, obj) {
        READWRITE(obj.price_message);
    }

    uint256 GetHash() const {
        return price_message.GetSignatureHash();
    }
};

struct OracleBundleMsg {
    COracleBundle bundle;
    uint256 block_hash;

    SERIALIZE_METHODS(OracleBundleMsg, obj) {
        READWRITE(obj.bundle);
        READWRITE(obj.block_hash);
    }

    uint256 GetHash() const {
        CHashWriter hasher(0);
        hasher << bundle << block_hash;
        return hasher.GetHash();
    }
};

struct GetOracleDataMsg {
    int32_t epoch;
    uint32_t oracle_id; // 0xFFFFFFFF for all oracles

    SERIALIZE_METHODS(GetOracleDataMsg, obj) {
        READWRITE(obj.epoch);
        READWRITE(obj.oracle_id);
    }
};

BOOST_AUTO_TEST_CASE(test_oracle_message_types_exist)
{
    // Test that oracle message types are defined in protocol
    // This will fail initially since these constants don't exist yet
    BOOST_CHECK_EQUAL(std::string(NetMsgType::ORACLEPRICE), "oracleprice");
    BOOST_CHECK_EQUAL(std::string(NetMsgType::ORACLEBUNDLE), "oraclebundle");
    BOOST_CHECK_EQUAL(std::string(NetMsgType::GETORACLES), "getoracles");
    BOOST_CHECK_EQUAL(std::string(NetMsgType::ORACLEHEARTBEAT), "oraclehb");
}

BOOST_AUTO_TEST_CASE(test_oracle_message_enum_values)
{
    // Test that oracle message enum values are defined and unique
    // These will fail initially since these enums don't exist yet
    BOOST_CHECK_EQUAL(MSG_ORACLE_PRICE, 0x40000000);
    BOOST_CHECK_EQUAL(MSG_ORACLE_BUNDLE, 0x40000001);
    BOOST_CHECK_EQUAL(MSG_GET_ORACLE_DATA, 0x40000002);
    BOOST_CHECK_EQUAL(MSG_ORACLE_HEARTBEAT, 0x40000008);

    // Ensure they are unique
    BOOST_CHECK_NE(MSG_ORACLE_PRICE, MSG_ORACLE_BUNDLE);
    BOOST_CHECK_NE(MSG_ORACLE_PRICE, MSG_GET_ORACLE_DATA);
    BOOST_CHECK_NE(MSG_ORACLE_BUNDLE, MSG_GET_ORACLE_DATA);
    BOOST_CHECK_NE(MSG_ORACLE_PRICE, MSG_ORACLE_HEARTBEAT);
    BOOST_CHECK_NE(MSG_ORACLE_BUNDLE, MSG_ORACLE_HEARTBEAT);
    BOOST_CHECK_NE(MSG_GET_ORACLE_DATA, MSG_ORACLE_HEARTBEAT);
}

BOOST_AUTO_TEST_CASE(test_oracle_price_msg_serialization)
{
    // Create a test oracle price message
    COraclePriceMessage oracle_price{1, 5, GetTime()};

    // Create test key for signing
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    // Sign the message (this will be implemented in oracle.cpp)
    uint256 hash = oracle_price.GetSignatureHash();
    std::vector<unsigned char> signature;
    BOOST_CHECK(key.Sign(hash, signature));
    oracle_price.schnorr_sig = signature;

    // Test serialization
    OraclePriceMsg msg;
    msg.price_message = oracle_price;

    // Serialize
    DataStream stream{};
    stream << msg;

    // Deserialize
    OraclePriceMsg deserialized_msg;
    stream >> deserialized_msg;

    // Verify content
    BOOST_CHECK_EQUAL(deserialized_msg.price_message.oracle_id, 1);
    BOOST_CHECK_EQUAL(deserialized_msg.price_message.price_micro_usd, 5);
    BOOST_CHECK(deserialized_msg.price_message.schnorr_sig == signature);
}

BOOST_AUTO_TEST_CASE(test_oracle_bundle_msg_serialization)
{
    COracleBundle bundle{123}; // epoch 123
    bundle.version = 3;
    bundle.median_price_micro_usd = 6000;
    bundle.timestamp = GetTime();
    bundle.participation_bitmap = {0x0f};
    bundle.aggregate_sig.assign(64, 0x42);

    // Create bundle message
    OracleBundleMsg bundle_msg;
    bundle_msg.bundle = bundle;
    bundle_msg.block_hash = uint256S("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");

    // Test serialization
    DataStream stream{};
    stream << bundle_msg;

    // Deserialize
    OracleBundleMsg deserialized_msg;
    stream >> deserialized_msg;

    // Verify content
    BOOST_CHECK_EQUAL(deserialized_msg.bundle.epoch, 123);
    BOOST_CHECK_EQUAL(deserialized_msg.bundle.version, 3);
    BOOST_CHECK_EQUAL(deserialized_msg.bundle.median_price_micro_usd, 6000U);
    BOOST_CHECK_EQUAL(deserialized_msg.bundle.aggregate_sig.size(), 64U);
    BOOST_CHECK(deserialized_msg.bundle.participation_bitmap == std::vector<unsigned char>{0x0f});
    BOOST_CHECK_EQUAL(deserialized_msg.block_hash, uint256S("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"));
}

BOOST_AUTO_TEST_CASE(test_get_oracle_data_msg_serialization)
{
    // Create test request message
    GetOracleDataMsg request;
    request.epoch = 456;
    request.oracle_id = 7; // specific oracle

    // Test serialization
    DataStream stream{};
    stream << request;

    // Deserialize
    GetOracleDataMsg deserialized_request;
    stream >> deserialized_request;

    // Verify content
    BOOST_CHECK_EQUAL(deserialized_request.epoch, 456);
    BOOST_CHECK_EQUAL(deserialized_request.oracle_id, 7);

    // Test "all oracles" request
    GetOracleDataMsg all_request;
    all_request.epoch = 789;
    all_request.oracle_id = 0xFFFFFFFF;

    DataStream stream2{};
    stream2 << all_request;

    GetOracleDataMsg deserialized_all;
    stream2 >> deserialized_all;

    BOOST_CHECK_EQUAL(deserialized_all.epoch, 789);
    BOOST_CHECK_EQUAL(deserialized_all.oracle_id, 0xFFFFFFFF);
}

BOOST_AUTO_TEST_CASE(test_oracle_message_command_conversion)
{
    // Test CInv command conversion for oracle messages
    // This will fail initially since GetCommand() doesn't handle oracle types yet
    CInv oracle_price_inv{MSG_ORACLE_PRICE, uint256S("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890")};
    BOOST_CHECK_EQUAL(oracle_price_inv.GetCommand(), "oracleprice");

    CInv oracle_bundle_inv{MSG_ORACLE_BUNDLE, uint256S("0xfedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321")};
    BOOST_CHECK_EQUAL(oracle_bundle_inv.GetCommand(), "oraclebundle");

    CInv get_oracle_inv{MSG_GET_ORACLE_DATA, uint256S("0x1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff")};
    BOOST_CHECK_EQUAL(get_oracle_inv.GetCommand(), "getoracles");

    CInv heartbeat_inv{MSG_ORACLE_HEARTBEAT, uint256S("0x222233334444555566667777888899990000aaaabbbbccccddddeeeeffff1111")};
    BOOST_CHECK_EQUAL(heartbeat_inv.GetCommand(), "oraclehb");
    BOOST_CHECK(heartbeat_inv.IsOracleMsg());
}

BOOST_AUTO_TEST_CASE(rc38_oracle_heartbeat_sign_verify_and_serializes)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey pubkey(key.GetPubKey());

    OracleVersionHeartbeatMsg msg;
    msg.heartbeat_version = 1;
    msg.oracle_id = 1;
    msg.timestamp = GetTime();
    msg.nonce = 42;
    msg.client_version = 9260038;
    msg.p2p_protocol_version = 70019;
    msg.oracle_protocol_version = 1;
    msg.musig2_context_version = ORACLE_MUSIG2_SESSION_CONTEXT_VERSION;
    msg.software_version = "v9.26.0-rc38";
    msg.subversion = "/DigiByte:9.26.0(rc38)/";

    BOOST_REQUIRE(msg.Sign(key));
    BOOST_CHECK(msg.IsValid());
    BOOST_CHECK(msg.VerifySignature(pubkey));

    DataStream stream{};
    stream << msg;
    OracleVersionHeartbeatMsg decoded;
    stream >> decoded;
    BOOST_CHECK_EQUAL(decoded.oracle_id, msg.oracle_id);
    BOOST_CHECK_EQUAL(decoded.client_version, msg.client_version);
    BOOST_CHECK_EQUAL(decoded.musig2_context_version, msg.musig2_context_version);
    BOOST_CHECK(decoded.VerifySignature(pubkey));

    decoded.musig2_context_version++;
    BOOST_CHECK(!decoded.VerifySignature(pubkey));
}

BOOST_AUTO_TEST_CASE(test_oracle_message_validation_timestamp)
{
    // Test timestamp validation for oracle messages
    int64_t now = GetTime();
    CKey key;
    key.MakeNewKey(true);

    // Valid message (recent timestamp)
    COraclePriceMessage valid_msg{1, 6000, now - 30}; // 30 seconds ago, $0.006
    BOOST_REQUIRE(valid_msg.SignAttestation(key));
    BOOST_CHECK(valid_msg.IsValid());

    // Message too far in future (should be invalid)
    COraclePriceMessage future_msg{1, 6000, now + 120}; // 2 minutes in future
    BOOST_REQUIRE(future_msg.SignAttestation(key));
    BOOST_CHECK(!future_msg.IsValid());

    // Message too old (should be invalid)
    COraclePriceMessage old_msg{1, 6000, now - 7200}; // 2 hours ago
    BOOST_REQUIRE(old_msg.SignAttestation(key));
    BOOST_CHECK(!old_msg.IsValid());
}

BOOST_AUTO_TEST_CASE(test_oracle_message_validation_signature)
{
    // Test signature validation
    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    CPubKey oracle_pubkey = oracle_key.GetPubKey();
    XOnlyPubKey oracle_xonly = XOnlyPubKey(oracle_pubkey);

    COraclePriceMessage msg{1, 6000, GetTime()};

    // Sign with correct key using Schnorr signature
    BOOST_CHECK(msg.Sign(oracle_key));

    // Should validate with correct pubkey
    BOOST_CHECK(msg.Verify());

    // Should fail with wrong pubkey
    CKey wrong_key;
    wrong_key.MakeNewKey(true);
    CPubKey wrong_pubkey = wrong_key.GetPubKey();
    XOnlyPubKey wrong_xonly = XOnlyPubKey(wrong_pubkey);
    msg.oracle_pubkey = wrong_xonly;

    BOOST_CHECK(!msg.Verify());

    // Should fail with corrupted signature (restore correct pubkey first)
    msg.oracle_pubkey = oracle_xonly;
    msg.schnorr_sig[0] ^= 0x01; // flip a bit
    BOOST_CHECK(!msg.Verify());
}

BOOST_AUTO_TEST_CASE(test_oracle_bundle_consensus_validation)
{
    const Consensus::Params& params = Params().GetConsensus();
    const int required = params.nOracleConsensusRequired;

    COracleBundle insufficient{100};
    insufficient.version = 3;
    insufficient.median_price_micro_usd = 6000;
    insufficient.timestamp = GetTime();
    insufficient.aggregate_sig.assign(64, 0x42);
    std::vector<uint8_t> insufficient_ids;
    for (int i = 0; i < required - 1; ++i) {
        insufficient_ids.push_back(static_cast<uint8_t>(i));
    }
    insufficient.participation_bitmap = MuSig2OracleAggregator::EncodeBitmap(
        insufficient_ids, static_cast<uint16_t>(params.nOracleTotalOracles));
    BOOST_CHECK(!OracleBundleManager::ValidateBundle(insufficient, 0, params));

    COracleBundle valid = insufficient;
    std::vector<uint8_t> valid_ids;
    for (int i = 0; i < required; ++i) {
        valid_ids.push_back(static_cast<uint8_t>(i));
    }
    valid.participation_bitmap = MuSig2OracleAggregator::EncodeBitmap(
        valid_ids, static_cast<uint16_t>(params.nOracleTotalOracles));
    BOOST_CHECK(OracleBundleManager::ValidateBundle(valid, 0, params));
}

BOOST_AUTO_TEST_CASE(test_oracle_message_types_in_all_net_message_types)
{
    // Test that oracle message types are included in getAllNetMessageTypes()
    // This will fail initially since these aren't added to the vector yet
    const auto& all_types = getAllNetMessageTypes();

    bool found_oracleprice = false;
    bool found_oraclebundle = false;
    bool found_getoracles = false;

    for (const auto& type : all_types) {
        if (type == "oracleprice") found_oracleprice = true;
        if (type == "oraclebundle") found_oraclebundle = true;
        if (type == "getoracles") found_getoracles = true;
    }

    BOOST_CHECK(found_oracleprice);
    BOOST_CHECK(found_oraclebundle);
    BOOST_CHECK(found_getoracles);
}

BOOST_AUTO_TEST_CASE(test_oracle_message_hash_functions)
{
    // Test hash functions for oracle messages
    COraclePriceMessage msg1{1, 5, 1234567890};
    COraclePriceMessage msg2{1, 5, 1234567890};
    COraclePriceMessage msg3{2, 5, 1234567890}; // different oracle_id

    // Same messages should have same hash
    BOOST_CHECK_EQUAL(msg1.GetSignatureHash(), msg2.GetSignatureHash());

    // Different messages should have different hash
    BOOST_CHECK_NE(msg1.GetSignatureHash(), msg3.GetSignatureHash());

    // Test oracle bundle hashes
    COracleBundle bundle1{100};
    bundle1.AddMessage(msg1);

    COracleBundle bundle2{100};
    bundle2.AddMessage(msg1);

    COracleBundle bundle3{100};
    bundle3.AddMessage(msg3);

    // Bundles with same content should be equal
    BOOST_CHECK(bundle1 == bundle2);

    // Bundles with different content should not be equal
    BOOST_CHECK(bundle1 != bundle3);
}

BOOST_AUTO_TEST_CASE(test_oracle_message_inv_helpers)
{
    // Test inventory message helpers for oracle messages
    // These will fail initially since the helper methods don't exist yet
    CInv oracle_price_inv{MSG_ORACLE_PRICE, uint256{}};
    CInv oracle_bundle_inv{MSG_ORACLE_BUNDLE, uint256{}};
    CInv get_oracle_inv{MSG_GET_ORACLE_DATA, uint256{}};
    CInv regular_tx_inv{MSG_TX, uint256{}};

    // Test that IsOracleMsg() helper works (to be implemented)
    // BOOST_CHECK(oracle_price_inv.IsOracleMsg());
    // BOOST_CHECK(oracle_bundle_inv.IsOracleMsg());
    // BOOST_CHECK(get_oracle_inv.IsOracleMsg());
    // BOOST_CHECK(!regular_tx_inv.IsOracleMsg());
}

BOOST_AUTO_TEST_SUITE_END()
