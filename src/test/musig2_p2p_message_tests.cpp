// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// RH-03: Red-team P2P message handling tests for MuSig2 oracle messages.
// Tests attack vectors: flooding, oversized payloads, replay, invalid nonces,
// epoch mismatch, sybil spoofing, malformed serialization, memory exhaustion.

#include <boost/test/unit_test.hpp>
#include <test/util/setup_common.h>

#include <oracle/musig2_messages.h>
#include <primitives/oracle.h>
#include <hash.h>
#include <serialize.h>
#include <streams.h>

#include <vector>
#include <set>

// Helper: create a valid nonce message with dummy signature (RH-24 requires signature for IsValid)
static OracleMusigNonceMsg MakeNonceMsg(int32_t epoch, uint8_t oracle_id, size_t nonce_size = 66) {
    OracleMusigNonceMsg msg;
    msg.epoch = epoch;
    msg.oracle_id = oracle_id;
    msg.pubnonce.assign(nonce_size, 0xAA);
    msg.signature.assign(64, 0xBB); // dummy sig for IsValid()
    return msg;
}

static OracleMusigPartialSigMsg MakePartialSigMsg(int32_t epoch, uint8_t oracle_id, size_t sig_size = 32) {
    OracleMusigPartialSigMsg msg;
    msg.epoch = epoch;
    msg.session_context_id = uint256(1);
    msg.oracle_id = oracle_id;
    msg.partial_sig.assign(sig_size, 0xCC);
    msg.signature.assign(64, 0xDD); // dummy sig for IsValid()
    return msg;
}

static OracleMusigContextMsg MakeContextMsg(int32_t epoch, uint8_t proposer_id) {
    OracleMusigContextMsg msg;
    msg.epoch = epoch;
    msg.context_version = ORACLE_MUSIG2_SESSION_CONTEXT_VERSION;
    msg.epoch_selection_seed = uint256(7);
    msg.proposer_id = proposer_id;
    msg.participant_ids = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    msg.consensus_price = 3777;
    msg.consensus_timestamp = 1710000000;
    msg.session_context_id = uint256(9);
    msg.signature.assign(64, 0xEE);
    return msg;
}

BOOST_FIXTURE_TEST_SUITE(musig2_p2p_message_tests, BasicTestingSetup)

// ──────────────────────────────────────────────────────────────────────
// Attack Vector 1: Invalid nonce injection — zero/wrong-size nonces
// ──────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(rh03_invalid_nonce_zero_pubnonce)
{
    // A nonce message with an all-zero 66-byte pubnonce should pass IsValid()
    // (size check) but fail at secp256k1_musig_pubnonce_parse() in the handler.
    // The P2P layer defense is IsValid() which only checks size — deeper
    // validation happens in ProcessRemoteMusigNonce.
    OracleMusigNonceMsg msg;
    msg.epoch = 1;
    msg.oracle_id = 0;
    msg.pubnonce.assign(66, 0x00);
    msg.signature.assign(64, 0xBB); // RH-24: dummy sig for IsValid()

    // Size check passes — this is by design; the P2P handler calls IsValid()
    // then relies on secp256k1 to reject bad nonces
    BOOST_CHECK(msg.IsValid());

    // Verify hash is deterministic (dedup works even for garbage)
    uint256 h1 = msg.GetHash();
    uint256 h2 = msg.GetHash();
    BOOST_CHECK_EQUAL(h1, h2);
}

BOOST_AUTO_TEST_CASE(rh03_invalid_nonce_wrong_size)
{
    // Pubnonce must be exactly 66 bytes
    OracleMusigNonceMsg msg;
    msg.epoch = 1;
    msg.oracle_id = 0;

    // Too short
    msg.pubnonce.assign(65, 0xAA);
    BOOST_CHECK(!msg.IsValid());

    // Too long
    msg.pubnonce.assign(67, 0xAA);
    BOOST_CHECK(!msg.IsValid());

    // Empty
    msg.pubnonce.clear();
    BOOST_CHECK(!msg.IsValid());
}

BOOST_AUTO_TEST_CASE(rh03_invalid_partialsig_wrong_size)
{
    // Partial sig must be exactly 32 bytes
    OracleMusigPartialSigMsg msg;
    msg.epoch = 1;
    msg.oracle_id = 0;

    msg.partial_sig.assign(31, 0xBB);
    BOOST_CHECK(!msg.IsValid());

    msg.partial_sig.assign(33, 0xBB);
    BOOST_CHECK(!msg.IsValid());

    msg.partial_sig.clear();
    BOOST_CHECK(!msg.IsValid());
}

// ──────────────────────────────────────────────────────────────────────
// Attack Vector 2: Oracle ID range — sybil/spoofing
// ──────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(rh03_nonce_oracle_id_boundary)
{
    // IsValid() checks oracle_id < 255. But ORACLE_TOTAL_COUNT is 30.
    // IDs 30-254 pass IsValid() but are invalid oracles.
    // The P2P handler MUST reject these (via Misbehaving or similar).
    OracleMusigNonceMsg msg;
    msg.epoch = 1;
    msg.pubnonce.assign(66, 0xAA);
    msg.signature.assign(64, 0xBB); // RH-24: dummy sig for IsValid()

    // Valid range: 0 to ORACLE_TOTAL_COUNT-1
    msg.oracle_id = 0;
    BOOST_CHECK(msg.IsValid());
    msg.oracle_id = ORACLE_TOTAL_COUNT - 1; // 29
    BOOST_CHECK(msg.IsValid());

    // Invalid but passes IsValid() — this is the gap!
    // These should be caught by the P2P handler's oracle_id range check.
    msg.oracle_id = ORACLE_TOTAL_COUNT; // 30
    BOOST_CHECK(msg.IsValid()); // NOTE: IsValid() doesn't know ORACLE_TOTAL_COUNT
    // ^^^^ This documents that IsValid() alone is insufficient.
    // The net_processing.cpp handler MUST add: oracle_id >= ORACLE_TOTAL_COUNT check

    msg.oracle_id = 254;
    BOOST_CHECK(msg.IsValid()); // passes basic check but is invalid oracle

    // 255 is caught by IsValid()
    msg.oracle_id = 255;
    BOOST_CHECK(!msg.IsValid());
}

BOOST_AUTO_TEST_CASE(rh03_partialsig_oracle_id_boundary)
{
    OracleMusigPartialSigMsg msg;
    msg.epoch = 1;
    msg.partial_sig.assign(32, 0xCC);
    msg.signature.assign(64, 0xDD); // RH-24: dummy sig for IsValid()

    msg.oracle_id = ORACLE_TOTAL_COUNT; // 30
    BOOST_CHECK(msg.IsValid()); // Gap: passes but shouldn't be a valid oracle

    msg.oracle_id = 255;
    BOOST_CHECK(!msg.IsValid());
}

BOOST_AUTO_TEST_CASE(rc38_attempt_id_changes_musig_message_domains)
{
    OracleMusigNonceMsg nonce_a = MakeNonceMsg(10, 1);
    OracleMusigNonceMsg nonce_b = nonce_a;
    nonce_b.attempt_id = 1;
    BOOST_CHECK(nonce_a.GetHash() != nonce_b.GetHash());
    BOOST_CHECK(nonce_a.GetSignatureHash() != nonce_b.GetSignatureHash());

    OracleMusigPartialSigMsg psig_a = MakePartialSigMsg(10, 1);
    OracleMusigPartialSigMsg psig_b = psig_a;
    psig_b.attempt_id = 1;
    BOOST_CHECK(psig_a.GetHash() != psig_b.GetHash());
    BOOST_CHECK(psig_a.GetSignatureHash() != psig_b.GetSignatureHash());

    OracleMusigContextMsg ctx_a = MakeContextMsg(10, 1);
    ctx_a.nonce_set_hash = uint256(11);
    ctx_a.quote_set_hash = uint256(12);
    OracleMusigContextMsg ctx_b = ctx_a;
    ctx_b.attempt_id = 1;
    BOOST_CHECK(ctx_a.GetHash() != ctx_b.GetHash());
    BOOST_CHECK(ctx_a.GetSignatureHash() != ctx_b.GetSignatureHash());
}

BOOST_AUTO_TEST_CASE(rc38_context_signature_commits_nonce_and_price_evidence)
{
    OracleMusigContextMsg base = MakeContextMsg(10, 1);
    base.nonce_set_hash = uint256(11);
    base.quote_set_hash = uint256(12);
    base.nonce_evidence.push_back(MakeNonceMsg(10, 2));
    COraclePriceMessage price_msg(2, 12345, 1710000000);
    price_msg.schnorr_sig.assign(64, 0x44);
    base.price_evidence.push_back(price_msg);

    OracleMusigContextMsg changed_nonce = base;
    changed_nonce.nonce_evidence[0].attempt_id = 1;
    BOOST_CHECK(base.GetSignatureHash() != changed_nonce.GetSignatureHash());

    OracleMusigContextMsg changed_price = base;
    changed_price.price_evidence[0].price_micro_usd += 1;
    BOOST_CHECK(base.GetSignatureHash() != changed_price.GetSignatureHash());
}

// ──────────────────────────────────────────────────────────────────────
// Attack Vector 3: Epoch mismatch / replay
// ──────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(rh03_nonce_negative_epoch)
{
    // Negative or zero epochs should be detectable
    OracleMusigNonceMsg msg;
    msg.oracle_id = 0;
    msg.pubnonce.assign(66, 0xAA);
    msg.signature.assign(64, 0xBB); // RH-24: dummy sig for IsValid()

    msg.epoch = -1;
    BOOST_CHECK(msg.IsValid()); // IsValid() doesn't check epoch — gap!

    msg.epoch = 0;
    BOOST_CHECK(msg.IsValid()); // epoch 0 is pre-activation

    msg.epoch = INT32_MAX;
    BOOST_CHECK(msg.IsValid()); // far-future epoch — should be bounded
}

BOOST_AUTO_TEST_CASE(rh03_replay_different_epochs_different_hashes)
{
    // Replay protection: same oracle data in different epochs must hash differently
    OracleMusigNonceMsg msg1, msg2;
    msg1.oracle_id = msg2.oracle_id = 5;
    msg1.pubnonce = msg2.pubnonce = std::vector<unsigned char>(66, 0xDD);
    msg1.epoch = 100;
    msg2.epoch = 101;

    BOOST_CHECK(msg1.GetHash() != msg2.GetHash());
}

BOOST_AUTO_TEST_CASE(rh03_replay_same_epoch_same_hash)
{
    // Same message replayed in same epoch should be caught by dedup
    OracleMusigNonceMsg msg1, msg2;
    msg1.epoch = msg2.epoch = 100;
    msg1.oracle_id = msg2.oracle_id = 5;
    msg1.pubnonce = msg2.pubnonce = std::vector<unsigned char>(66, 0xDD);

    BOOST_CHECK_EQUAL(msg1.GetHash(), msg2.GetHash());
}

// ──────────────────────────────────────────────────────────────────────
// Attack Vector 4: Malformed serialization
// ──────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(rh03_nonce_deserialization_truncated)
{
    // Craft a valid nonce message, serialize it, then truncate
    OracleMusigNonceMsg orig;
    orig.epoch = 42;
    orig.oracle_id = 3;
    orig.pubnonce.assign(66, 0xEE);
    orig.signature.assign(64, 0xBB); // RH-24: dummy sig for IsValid()

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << orig;

    // Truncate to half
    CDataStream ss_trunc(SER_NETWORK, PROTOCOL_VERSION);
    ss_trunc.write(MakeByteSpan(ss).first(ss.size() / 2));

    OracleMusigNonceMsg recovered;
    bool threw = false;
    try {
        ss_trunc >> recovered;
    } catch (const std::exception&) {
        threw = true;
    }
    // Either throws or produces invalid message
    if (!threw) {
        BOOST_CHECK(!recovered.IsValid());
    } else {
        BOOST_CHECK(true); // Exception is the defense
    }
}

BOOST_AUTO_TEST_CASE(rh03_partialsig_deserialization_truncated)
{
    OracleMusigPartialSigMsg orig;
    orig.epoch = 42;
    orig.oracle_id = 3;
    orig.partial_sig.assign(32, 0xFF);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << orig;

    CDataStream ss_trunc(SER_NETWORK, PROTOCOL_VERSION);
    ss_trunc.write(MakeByteSpan(ss).first(ss.size() / 2));

    OracleMusigPartialSigMsg recovered;
    bool threw = false;
    try {
        ss_trunc >> recovered;
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        BOOST_CHECK(!recovered.IsValid());
    } else {
        BOOST_CHECK(true);
    }
}

BOOST_AUTO_TEST_CASE(rh03_nonce_oversized_pubnonce_serialized)
{
    // An attacker could craft a serialized message with huge pubnonce vector.
    // The deserialization should cap vector size or IsValid() rejects it.
    OracleMusigNonceMsg msg;
    msg.epoch = 1;
    msg.oracle_id = 0;
    msg.pubnonce.assign(1000000, 0xAA); // 1MB nonce — absurd

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << msg;

    OracleMusigNonceMsg recovered;
    ss >> recovered;

    // Even if deserialization succeeds, IsValid() must reject
    BOOST_CHECK(!recovered.IsValid());
}

// ──────────────────────────────────────────────────────────────────────
// Attack Vector 5: Deduplication / memory exhaustion
// ──────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(rh03_unique_hashes_per_field_change)
{
    // Verify that changing any field produces a different hash (proper coverage)
    OracleMusigNonceMsg base;
    base.epoch = 100;
    base.oracle_id = 5;
    base.pubnonce.assign(66, 0xAA);
    base.signature.assign(64, 0xBB); // RH-24: dummy sig for IsValid()

    std::set<uint256> hashes;
    hashes.insert(base.GetHash());

    // Change epoch
    OracleMusigNonceMsg m1 = base;
    m1.epoch = 101;
    hashes.insert(m1.GetHash());

    // Change oracle_id
    OracleMusigNonceMsg m2 = base;
    m2.oracle_id = 6;
    hashes.insert(m2.GetHash());

    // Change pubnonce
    OracleMusigNonceMsg m3 = base;
    m3.pubnonce[0] = 0xBB;
    hashes.insert(m3.GetHash());

    // All 4 should be unique
    BOOST_CHECK_EQUAL(hashes.size(), 4U);
}

BOOST_AUTO_TEST_CASE(rh03_partialsig_unique_hashes)
{
    OracleMusigPartialSigMsg base;
    base.epoch = 100;
    base.session_context_id = uint256(1);
    base.oracle_id = 5;
    base.partial_sig.assign(32, 0xAA);

    std::set<uint256> hashes;
    hashes.insert(base.GetHash());

    OracleMusigPartialSigMsg m1 = base;
    m1.epoch = 101;
    hashes.insert(m1.GetHash());

    OracleMusigPartialSigMsg m2 = base;
    m2.oracle_id = 6;
    hashes.insert(m2.GetHash());

    OracleMusigPartialSigMsg m3 = base;
    m3.partial_sig[0] = 0xBB;
    hashes.insert(m3.GetHash());

    OracleMusigPartialSigMsg m4 = base;
    m4.session_context_id = uint256(2);
    hashes.insert(m4.GetHash());

    BOOST_CHECK_EQUAL(hashes.size(), 5U);
}

BOOST_AUTO_TEST_CASE(rc36_partialsig_auth_binds_session_context)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey pubkey(key.GetPubKey());

    OracleMusigPartialSigMsg msg;
    msg.epoch = 100;
    msg.context_version = ORACLE_MUSIG2_SESSION_CONTEXT_VERSION;
    msg.session_context_id = uint256(1);
    msg.oracle_id = 5;
    msg.partial_sig.assign(32, 0xAA);
    BOOST_REQUIRE(msg.Sign(key));
    BOOST_CHECK(msg.VerifySignature(pubkey));

    OracleMusigPartialSigMsg changed_context = msg;
    changed_context.session_context_id = uint256(2);
    BOOST_CHECK(msg.GetHash() != changed_context.GetHash());
    BOOST_CHECK(msg.GetSignatureHash() != changed_context.GetSignatureHash());
    BOOST_CHECK(!changed_context.VerifySignature(pubkey));

    OracleMusigPartialSigMsg changed_version = msg;
    ++changed_version.context_version;
    BOOST_CHECK(msg.GetHash() != changed_version.GetHash());
    BOOST_CHECK(msg.GetSignatureHash() != changed_version.GetSignatureHash());
    BOOST_CHECK(!changed_version.VerifySignature(pubkey));
}

BOOST_AUTO_TEST_CASE(rc37_context_message_serialization_roundtrip)
{
    OracleMusigContextMsg orig = MakeContextMsg(12345, 7);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << orig;

    OracleMusigContextMsg recovered;
    ss >> recovered;

    BOOST_CHECK_EQUAL(recovered.epoch, orig.epoch);
    BOOST_CHECK_EQUAL(recovered.context_version, orig.context_version);
    BOOST_CHECK(recovered.epoch_selection_seed == orig.epoch_selection_seed);
    BOOST_CHECK_EQUAL(recovered.proposer_id, orig.proposer_id);
    BOOST_CHECK(recovered.participant_ids == orig.participant_ids);
    BOOST_CHECK_EQUAL(recovered.consensus_price, orig.consensus_price);
    BOOST_CHECK_EQUAL(recovered.consensus_timestamp, orig.consensus_timestamp);
    BOOST_CHECK(recovered.session_context_id == orig.session_context_id);
    BOOST_CHECK(recovered.signature == orig.signature);
    BOOST_CHECK(recovered.IsValid());
    BOOST_CHECK_EQUAL(recovered.GetHash(), orig.GetHash());
}

BOOST_AUTO_TEST_CASE(rc37_context_auth_binds_seed_participants_price_and_context)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey pubkey(key.GetPubKey());

    OracleMusigContextMsg msg = MakeContextMsg(100, 5);
    msg.signature.clear();
    BOOST_REQUIRE(msg.Sign(key));
    BOOST_CHECK(msg.VerifySignature(pubkey));

    OracleMusigContextMsg changed_seed = msg;
    changed_seed.epoch_selection_seed = uint256(8);
    BOOST_CHECK(msg.GetHash() != changed_seed.GetHash());
    BOOST_CHECK(msg.GetSignatureHash() != changed_seed.GetSignatureHash());
    BOOST_CHECK(!changed_seed.VerifySignature(pubkey));

    OracleMusigContextMsg changed_participants = msg;
    changed_participants.participant_ids[0] = 9;
    BOOST_CHECK(msg.GetHash() != changed_participants.GetHash());
    BOOST_CHECK(msg.GetSignatureHash() != changed_participants.GetSignatureHash());
    BOOST_CHECK(!changed_participants.VerifySignature(pubkey));

    OracleMusigContextMsg changed_price = msg;
    ++changed_price.consensus_price;
    BOOST_CHECK(msg.GetHash() != changed_price.GetHash());
    BOOST_CHECK(msg.GetSignatureHash() != changed_price.GetSignatureHash());
    BOOST_CHECK(!changed_price.VerifySignature(pubkey));

    OracleMusigContextMsg changed_context = msg;
    changed_context.session_context_id = uint256(10);
    BOOST_CHECK(msg.GetHash() != changed_context.GetHash());
    BOOST_CHECK(msg.GetSignatureHash() != changed_context.GetSignatureHash());
    BOOST_CHECK(!changed_context.VerifySignature(pubkey));
}

// ──────────────────────────────────────────────────────────────────────
// Attack Vector 6: Serialization roundtrip integrity
// ──────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(rh03_nonce_serialization_roundtrip)
{
    OracleMusigNonceMsg orig;
    orig.epoch = 12345;
    orig.oracle_id = 7;
    orig.pubnonce.assign(66, 0x42);
    orig.signature.assign(64, 0xAA); // RH-24: signature now required for IsValid()

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << orig;

    OracleMusigNonceMsg recovered;
    ss >> recovered;

    BOOST_CHECK_EQUAL(recovered.epoch, orig.epoch);
    BOOST_CHECK_EQUAL(recovered.oracle_id, orig.oracle_id);
    BOOST_CHECK(recovered.pubnonce == orig.pubnonce);
    BOOST_CHECK(recovered.IsValid());
    BOOST_CHECK_EQUAL(recovered.GetHash(), orig.GetHash());
}

BOOST_AUTO_TEST_CASE(rh03_partialsig_serialization_roundtrip)
{
    OracleMusigPartialSigMsg orig;
    orig.epoch = 12345;
    orig.session_context_id = uint256(1);
    orig.oracle_id = 7;
    orig.partial_sig.assign(32, 0x42);
    orig.signature.assign(64, 0xBB); // RH-24: signature now required for IsValid()

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << orig;

    OracleMusigPartialSigMsg recovered;
    ss >> recovered;

    BOOST_CHECK_EQUAL(recovered.epoch, orig.epoch);
    BOOST_CHECK_EQUAL(recovered.context_version, orig.context_version);
    BOOST_CHECK(recovered.session_context_id == orig.session_context_id);
    BOOST_CHECK_EQUAL(recovered.oracle_id, orig.oracle_id);
    BOOST_CHECK(recovered.partial_sig == orig.partial_sig);
    BOOST_CHECK(recovered.IsValid());
    BOOST_CHECK_EQUAL(recovered.GetHash(), orig.GetHash());
}

BOOST_AUTO_TEST_SUITE_END()
