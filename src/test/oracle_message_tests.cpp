// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <logging.h>
#include <util/strencodings.h>
#include <primitives/oracle.h>
#include <key.h>
#include <pubkey.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <test/util/setup_common.h>
#include <test/util/random.h>
#include <uint256.h>
#include <streams.h>

BOOST_FIXTURE_TEST_SUITE(oracle_message_tests, BasicTestingSetup)

//
// CATEGORY 0: SIGNATURE ROUNDTRIP BUG TESTS (TDD — Schnorr mismatch fix)
//

/**
 * TDD Test 1: SignAttestation → VerifyAttestation roundtrip MUST work
 * This is the correct path used by all oracle signing code.
 */
BOOST_AUTO_TEST_CASE(signphase2_verifyphase2_roundtrip)
{
    CKey privkey;
    privkey.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6000;
    msg.timestamp = GetTime();
    msg.block_height = 1000;
    msg.nonce = 12345;
    msg.oracle_pubkey = XOnlyPubKey(privkey.GetPubKey());

    // Sign with Phase 2 (3-field hash: oracle_id + price + timestamp)
    BOOST_REQUIRE(msg.SignAttestation(privkey));

    // Verify with Phase 2 — MUST succeed
    BOOST_CHECK_MESSAGE(msg.VerifyAttestation(), "SignAttestation → VerifyAttestation roundtrip MUST work");
}

/**
 * TDD Test 2: SignAttestation → Verify() MUST FAIL
 * This documents the bug: signing uses 3-field hash, but Verify() uses 5-field hash.
 * If this test passes (Verify returns false), the mismatch is confirmed.
 */
BOOST_AUTO_TEST_CASE(signphase2_verify_mismatch_fails)
{
    CKey privkey;
    privkey.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6000;
    msg.timestamp = GetTime();
    msg.block_height = 1000;
    msg.nonce = 12345;
    msg.oracle_pubkey = XOnlyPubKey(privkey.GetPubKey());

    // Sign with Phase 2 (3-field hash)
    BOOST_REQUIRE(msg.SignAttestation(privkey));

    // Verify with old Verify() (5-field hash) — MUST FAIL (this is the bug!)
    BOOST_CHECK_MESSAGE(!msg.Verify(), "SignAttestation → Verify() SHOULD fail due to hash mismatch");
}

/**
 * TDD Test 3: Sign → Verify roundtrip works (old path, keep working)
 * The old Sign()/Verify() path uses 5-field hash and must remain functional
 * for backward compatibility and benchmarks.
 */
BOOST_AUTO_TEST_CASE(sign_verify_roundtrip)
{
    CKey privkey;
    privkey.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6000;
    msg.timestamp = GetTime();
    msg.block_height = 1000;
    msg.nonce = 12345;
    msg.oracle_pubkey = XOnlyPubKey(privkey.GetPubKey());

    // Sign with old Sign() (5-field hash)
    BOOST_REQUIRE(msg.Sign(privkey));

    // Verify with old Verify() — MUST succeed
    BOOST_CHECK_MESSAGE(msg.Verify(), "Sign → Verify roundtrip MUST work");

    // Cross-check: Sign() → VerifyAttestation() should FAIL (different hash)
    BOOST_CHECK_MESSAGE(!msg.VerifyAttestation(), "Sign → VerifyAttestation SHOULD fail due to hash mismatch");
}

//
// CATEGORY 1: SCHNORR SIGNATURE TESTS (6 tests)
//

/**
 * RED TEST: Test creating Schnorr signature for price message
 *
 * EXPECTED TO FAIL: COraclePriceMessage does not have:
 * - XOnlyPubKey oracle_pubkey field
 * - uint64_t price_micro_usd field
 * - std::vector<unsigned char> schnorr_sig field
 * - GetHash() method
 */
BOOST_AUTO_TEST_CASE(schnorr_signature_creation_valid)
{
    // Generate test keypair
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    // Create oracle message
    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL: field doesn't exist
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();                      // OK: exists but wrong type (int64_t exists)

    // Get message hash for signing
    uint256 hash = msg.GetSignatureHash();         // Fixed: use GetSignatureHash()

    // Create Schnorr signature (64 bytes)
    msg.schnorr_sig.resize(64);                    // WILL FAIL: field doesn't exist

    // Sign message with Schnorr
    BOOST_REQUIRE(privkey.SignSchnorr(hash, msg.schnorr_sig, nullptr, uint256()));

    // Verify signature size is exactly 64 bytes (BIP-340)
    BOOST_CHECK_EQUAL(msg.schnorr_sig.size(), 64);
}

/**
 * RED TEST: Test verifying valid Schnorr signature
 *
 * EXPECTED TO FAIL: COraclePriceMessage does not have Verify() method
 */
BOOST_AUTO_TEST_CASE(schnorr_signature_verification_valid)
{
    // Generate keypair
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    // Create and sign message
    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL
    msg.price_micro_usd = 12000;               // $0.012 (realistic DGB price)
    msg.timestamp = GetTime();

    uint256 hash = msg.GetSignatureHash();         // Fixed: use GetSignatureHash()
    msg.schnorr_sig.resize(64);                    // WILL FAIL
    BOOST_REQUIRE(privkey.SignSchnorr(hash, msg.schnorr_sig, nullptr, uint256()));

    // Verify signature using XOnlyPubKey
    BOOST_CHECK(pubkey.VerifySchnorr(hash, msg.schnorr_sig));

    // Also test message-level Verify() method
    BOOST_CHECK(msg.Verify());                     // WILL FAIL: method doesn't exist
}

/**
 * RED TEST: Test rejecting invalid Schnorr signature
 *
 * EXPECTED TO FAIL: Verify() method doesn't exist
 */
BOOST_AUTO_TEST_CASE(schnorr_signature_verification_invalid)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();

    // Create invalid signature (all zeros)
    msg.schnorr_sig.resize(64, 0);                 // WILL FAIL

    // Verify should fail with invalid signature
    BOOST_CHECK(!msg.Verify());                    // WILL FAIL
}

/**
 * RED TEST: Test rejecting signature with wrong public key
 *
 * EXPECTED TO FAIL: Fields and methods don't exist
 */
BOOST_AUTO_TEST_CASE(schnorr_signature_wrong_pubkey)
{
    // Create two different keypairs
    CKey privkey1, privkey2;
    privkey1.MakeNewKey(true);
    privkey2.MakeNewKey(true);

    XOnlyPubKey pubkey1(privkey1.GetPubKey());
    XOnlyPubKey pubkey2(privkey2.GetPubKey());

    // Create message signed by privkey1
    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey1;                   // WILL FAIL
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();

    uint256 hash = msg.GetSignatureHash();         // Fixed: use GetSignatureHash()
    msg.schnorr_sig.resize(64);                    // WILL FAIL
    BOOST_REQUIRE(privkey1.SignSchnorr(hash, msg.schnorr_sig, nullptr, uint256()));

    // Replace with wrong public key
    msg.oracle_pubkey = pubkey2;                   // WILL FAIL

    // Verification should fail
    BOOST_CHECK(!msg.Verify());                    // WILL FAIL
}

/**
 * RED TEST: Test rejecting signature when message is modified
 *
 * EXPECTED TO FAIL: Fields don't exist
 */
BOOST_AUTO_TEST_CASE(schnorr_signature_tampered_message)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    // Create and sign original message
    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();

    uint256 hash = msg.GetSignatureHash();         // Fixed: use GetSignatureHash()
    msg.schnorr_sig.resize(64);                    // WILL FAIL
    BOOST_REQUIRE(privkey.SignSchnorr(hash, msg.schnorr_sig, nullptr, uint256()));

    // Verify signature is valid initially
    BOOST_REQUIRE(msg.Verify());                   // WILL FAIL

    // Tamper with message (change price)
    msg.price_micro_usd = 7000;                // Changed from 6000 to 7000

    // Verification should now fail
    BOOST_CHECK(!msg.Verify());                    // WILL FAIL
}

/**
 * RED TEST: Test signature is exactly 64 bytes
 *
 * EXPECTED TO FAIL: schnorr_sig field doesn't exist
 */
BOOST_AUTO_TEST_CASE(schnorr_signature_64_bytes)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();

    uint256 hash = msg.GetSignatureHash();         // Fixed: use GetSignatureHash()
    msg.schnorr_sig.resize(64);                    // WILL FAIL
    BOOST_REQUIRE(privkey.SignSchnorr(hash, msg.schnorr_sig, nullptr, uint256()));

    // BIP-340 Schnorr signatures are EXACTLY 64 bytes
    BOOST_CHECK_EQUAL(msg.schnorr_sig.size(), 64); // WILL FAIL

    // Not 71-73 bytes like ECDSA DER signatures
    BOOST_CHECK_NE(msg.schnorr_sig.size(), 71);
    BOOST_CHECK_NE(msg.schnorr_sig.size(), 72);
    BOOST_CHECK_NE(msg.schnorr_sig.size(), 73);
}

//
// CATEGORY 2: CORACLEPRICEMESSAGE TESTS (9 tests)
//

/**
 * RED TEST: Test creating COraclePriceMessage with required fields
 *
 * EXPECTED TO FAIL: New fields don't exist
 */
BOOST_AUTO_TEST_CASE(oracle_message_creation)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    // Create message with all required fields
    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL: 32-byte x-only pubkey
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();                      // OK: exists
    msg.schnorr_sig.resize(64);                    // WILL FAIL: 64-byte signature

    // Verify field types
    BOOST_CHECK(msg.oracle_pubkey.IsFullyValid()); // WILL FAIL
    BOOST_CHECK_GT(msg.price_micro_usd, 0);        // WILL FAIL
    BOOST_CHECK_GT(msg.timestamp, 0);               // Should work
    BOOST_CHECK_EQUAL(msg.schnorr_sig.size(), 64); // WILL FAIL
}

/**
 * RED TEST: Test message serialization
 *
 * EXPECTED TO FAIL: SERIALIZE_METHODS not updated for new fields
 */
BOOST_AUTO_TEST_CASE(oracle_message_serialization)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    // Create and sign message
    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL
    msg.price_micro_usd = 12000;               // $0.012 (realistic DGB price)
    msg.timestamp = 1700000000;

    uint256 hash = msg.GetSignatureHash();         // Fixed: use GetSignatureHash()
    msg.schnorr_sig.resize(64);                    // WILL FAIL
    privkey.SignSchnorr(hash, msg.schnorr_sig, nullptr, uint256());

    // Serialize to stream
    DataStream ss{};
    ss << msg;                                      // WILL FAIL: SERIALIZE_METHODS missing new fields

    // Verify serialized size
    // Expected: 32 (pubkey) + 8 (price) + 8 (timestamp) + 64 (sig) = 112 bytes minimum
    BOOST_CHECK_GE(ss.size(), 112);
}

/**
 * RED TEST: Test message deserialization
 *
 * EXPECTED TO FAIL: SERIALIZE_METHODS not updated
 */
BOOST_AUTO_TEST_CASE(oracle_message_deserialization)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    // Create original message
    COraclePriceMessage msg1;
    msg1.oracle_pubkey = pubkey;                   // WILL FAIL
    msg1.price_micro_usd = 6000;               // $0.006 (realistic DGB price)
    msg1.timestamp = 1700000000;

    uint256 hash = msg1.GetSignatureHash();        // Fixed: use GetSignatureHash()
    msg1.schnorr_sig.resize(64);                   // WILL FAIL
    privkey.SignSchnorr(hash, msg1.schnorr_sig, nullptr, uint256());

    // Serialize
    DataStream ss{};
    ss << msg1;                                     // WILL FAIL

    // Deserialize to new message
    COraclePriceMessage msg2;
    ss >> msg2;                                     // WILL FAIL

    // Verify fields match
    BOOST_CHECK_EQUAL(msg1.price_micro_usd, msg2.price_micro_usd);   // WILL FAIL
    BOOST_CHECK_EQUAL(msg1.timestamp, msg2.timestamp);
    BOOST_CHECK(msg1.schnorr_sig == msg2.schnorr_sig);               // WILL FAIL
}

/**
 * RED TEST: Test GetHash() returns correct hash
 *
 * EXPECTED TO FAIL: GetHash() method doesn't exist
 */
BOOST_AUTO_TEST_CASE(oracle_message_hash_calculation)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    msg.timestamp = 1700000000;

    // GetHash() should return deterministic hash
    uint256 hash1 = msg.GetSignatureHash();        // Fixed: use GetSignatureHash()
    uint256 hash2 = msg.GetSignatureHash();        // Fixed: use GetSignatureHash()

    // Same message should produce same hash
    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(!hash1.IsNull());

    // Different message should produce different hash
    msg.price_micro_usd = 7000;                // Changed price
    uint256 hash3 = msg.GetSignatureHash();        // Fixed: use GetSignatureHash()
    BOOST_CHECK(hash1 != hash3);
}

/**
 * RED TEST: Test signing and verifying message
 *
 * EXPECTED TO FAIL: Multiple missing fields and methods
 */
BOOST_AUTO_TEST_CASE(oracle_message_sign_and_verify)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    // Create message
    COraclePriceMessage msg;
    msg.oracle_pubkey = pubkey;                    // WILL FAIL
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();

    // Sign message
    uint256 hash = msg.GetSignatureHash();         // Fixed: use GetSignatureHash()
    msg.schnorr_sig.resize(64);                    // WILL FAIL
    BOOST_REQUIRE(privkey.SignSchnorr(hash, msg.schnorr_sig, nullptr, uint256()));

    // Verify signature
    BOOST_CHECK(msg.Verify());                     // WILL FAIL: method doesn't exist

    // Verify using explicit pubkey
    BOOST_CHECK(pubkey.VerifySchnorr(hash, msg.schnorr_sig));
}

/**
 * RED TEST: Test price is in micro-USD format
 *
 * EXPECTED TO FAIL: price_micro_usd field doesn't exist
 */
BOOST_AUTO_TEST_CASE(oracle_message_micro_usd_format)
{
    COraclePriceMessage msg;

    // Test micro-USD conversion
    // 1 USD = 1,000,000 micro-USD
    // $0.006 per DGB = 6,000 micro-USD (realistic DGB price)
    msg.price_micro_usd = 6000;                // $0.006 (realistic DGB price)
    BOOST_CHECK_EQUAL(msg.price_micro_usd, 6000);

    // Test various price points
    msg.price_micro_usd = 1000000;             // $1.00
    BOOST_CHECK_EQUAL(msg.price_micro_usd, 1000000);

    msg.price_micro_usd = 12340;               // $0.01234
    BOOST_CHECK_EQUAL(msg.price_micro_usd, 12340);

    // Test range (DGB price should be $0.0001 to $10.00)
    msg.price_micro_usd = 100;                 // $0.0001 (minimum)
    BOOST_CHECK_GE(msg.price_micro_usd, 100);

    msg.price_micro_usd = 10000000;            // $10.00 (maximum)
    BOOST_CHECK_LE(msg.price_micro_usd, 10000000);

    // Verify NOT in satoshis (current implementation)
    // Old format: price_satoshis (DGB sats per USD)
    // New format: price_micro_usd (micro-USD per DGB)
    // These are COMPLETELY different!
}

/**
 * Test timestamp validation for signed compact oracle attestations.
 */
BOOST_AUTO_TEST_CASE(oracle_message_timestamp_validation)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.oracle_pubkey = pubkey;
    msg.price_micro_usd = 6000;            // $0.006 (realistic DGB price)
    msg.block_height = 1000;
    msg.nonce = 12345;

    // Test current timestamp (should be valid)
    msg.timestamp = GetTime();
    BOOST_REQUIRE(msg.SignAttestation(privkey));
    BOOST_CHECK(msg.IsValid());

    // Test timestamp 1 minute ago (should be valid)
    msg.timestamp = GetTime() - 60;
    BOOST_REQUIRE(msg.SignAttestation(privkey));
    BOOST_CHECK(msg.IsValid());

    // Test timestamp 30 minutes ago (should be valid)
    msg.timestamp = GetTime() - 1800;
    BOOST_REQUIRE(msg.SignAttestation(privkey));
    BOOST_CHECK(msg.IsValid());

    // Test timestamp 59 minutes ago (should be valid, within 1 hour)
    msg.timestamp = GetTime() - 3540;
    BOOST_REQUIRE(msg.SignAttestation(privkey));
    BOOST_CHECK(msg.IsValid());
}

/**
 * Test rejecting attestations from the future.
 */
BOOST_AUTO_TEST_CASE(oracle_message_reject_future_timestamp)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.oracle_pubkey = pubkey;
    msg.price_micro_usd = 6000;            // $0.006 (realistic DGB price)
    msg.block_height = 1000;
    msg.nonce = 12345;

    // Test future timestamp (5 minutes ahead, should be invalid)
    msg.timestamp = GetTime() + 300;
    BOOST_REQUIRE(msg.SignAttestation(privkey));
    BOOST_CHECK(!msg.IsValid());

    // Test far future (1 hour ahead, should be invalid)
    msg.timestamp = GetTime() + 3600;
    BOOST_REQUIRE(msg.SignAttestation(privkey));
    BOOST_CHECK(!msg.IsValid());

    // Test timestamp 2 minutes ahead (within clock skew tolerance, might be ok)
    // Spec allows 1 minute tolerance
    msg.timestamp = GetTime() + 120;
    BOOST_REQUIRE(msg.SignAttestation(privkey));
    BOOST_CHECK(!msg.IsValid());
}

/**
 * Test rejecting attestations older than 1 hour.
 */
BOOST_AUTO_TEST_CASE(oracle_message_reject_old_timestamp)
{
    CKey privkey;
    privkey.MakeNewKey(true);
    XOnlyPubKey pubkey(privkey.GetPubKey());

    // Test timestamp 61 minutes ago (should be invalid)
    COraclePriceMessage msg1;
    msg1.oracle_id = 1;
    msg1.oracle_pubkey = pubkey;
    msg1.price_micro_usd = 6000;       // $0.006 (realistic DGB price)
    msg1.timestamp = GetTime() - 3660;  // 61 minutes
    msg1.block_height = 0;
    msg1.nonce = 0;
    BOOST_REQUIRE(msg1.SignAttestation(privkey));
    BOOST_CHECK(!msg1.IsValid());  // Should fail due to old timestamp

    // Test timestamp 2 hours ago (should be invalid)
    COraclePriceMessage msg2;
    msg2.oracle_id = 1;
    msg2.oracle_pubkey = pubkey;
    msg2.price_micro_usd = 6000;       // $0.006 (realistic DGB price)
    msg2.timestamp = GetTime() - 7200;
    msg2.block_height = 0;
    msg2.nonce = 0;
    BOOST_REQUIRE(msg2.SignAttestation(privkey));
    BOOST_CHECK(!msg2.IsValid());

    // Test timestamp exactly 1 hour ago (boundary, should still be valid)
    COraclePriceMessage msg3;
    msg3.oracle_id = 1;
    msg3.oracle_pubkey = pubkey;
    msg3.price_micro_usd = 6000;       // $0.006 (realistic DGB price)
    msg3.timestamp = GetTime() - 3600;
    msg3.block_height = 0;
    msg3.nonce = 0;
    BOOST_REQUIRE(msg3.SignAttestation(privkey));
    BOOST_CHECK(msg3.IsValid());
}

BOOST_AUTO_TEST_SUITE_END()
