// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * MuSig2 P2P Ingestion Tests
 *
 * Tests the critical path: net_processing receives ORACLEMUSIGNONCE and
 * ORACLEMUSIGPARTIALSIG messages from peers, and feeds them into the
 * global g_oracle_signing_sessions map via OracleBundleManager::
 * ProcessRemoteMusigNonce() / ProcessRemoteMusigPartialSig().
 *
 * This was the "Wave 3 gap" — P2P relay worked but ingestion into
 * signing sessions was missing, meaning multi-node MuSig2 could
 * never complete.
 */

#include <boost/test/unit_test.hpp>

#include <key.h>
#include <oracle/bundle_manager.h>
#include <oracle/musig2_messages.h>
#include <oracle/musig2_orchestrator.h>
#include <oracle/musig2_session.h>
#include <test/util/setup_common.h>
#include <chainparams.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>

#include <cstring>

struct MuSig2P2PTestSetup : public BasicTestingSetup {
    MuSig2P2PTestSetup() : BasicTestingSetup(ChainType::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(musig2_p2p_ingestion_tests, MuSig2P2PTestSetup)

/**
 * Helper: generate a deterministic oracle key from an index.
 * Matches the regtest oracle key derivation: SHA256("digibyte_regtest_oracle_N")
 */
static CKey MakeOracleKey(int index)
{
    CKey key;
    std::string seed = "digibyte_regtest_oracle_" + std::to_string(index);
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size()).Finalize(hash.begin());
    key.Set(hash.begin(), hash.end(), true);
    assert(key.IsValid());
    return key;
}

/**
 * Test that ProcessRemoteMusigNonce rejects messages when no session exists.
 */
BOOST_AUTO_TEST_CASE(reject_nonce_no_session)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // Clear any existing sessions
    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
    }

    OracleMusigNonceMsg msg;
    msg.epoch = 999;
    msg.oracle_id = 0;
    msg.pubnonce.resize(66, 0xAA);  // Dummy data — won't parse but that's fine

    BOOST_CHECK(!manager.ProcessRemoteMusigNonce(msg));
}

/**
 * Test that ProcessRemoteMusigPartialSig rejects messages when no session exists.
 */
BOOST_AUTO_TEST_CASE(reject_partial_sig_no_session)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
    }

    OracleMusigPartialSigMsg msg;
    msg.epoch = 999;
    msg.oracle_id = 0;
    msg.partial_sig.resize(32, 0xBB);

    BOOST_CHECK(!manager.ProcessRemoteMusigPartialSig(msg));
}

/**
 * Test that ProcessRemoteMusigNonce rejects invalid messages.
 */
BOOST_AUTO_TEST_CASE(reject_invalid_nonce_message)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // Create a session
    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
        g_oracle_signing_sessions.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(42),
            std::forward_as_tuple(42, 4));  // epoch 42, min_signers 4
    }

    // Invalid: wrong pubnonce size
    OracleMusigNonceMsg bad_msg;
    bad_msg.epoch = 42;
    bad_msg.oracle_id = 0;
    bad_msg.pubnonce.resize(10);  // Too short

    BOOST_CHECK(!manager.ProcessRemoteMusigNonce(bad_msg));

    // Cleanup
    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
    }
}

/**
 * Full round-trip test: create a session, generate nonces from multiple
 * oracle keys, serialize/deserialize through P2P message format, and
 * verify they are ingested into the session via ProcessRemoteMusigNonce.
 */
BOOST_AUTO_TEST_CASE(nonce_ingestion_round_trip)
{
    const int32_t epoch = 100;
    const uint8_t min_signers = 4;
    const int num_oracles = min_signers;

    // Generate oracle keys
    std::vector<CKey> keys;
    std::vector<CPubKey> pubkeys;
    std::vector<secp256k1_pubkey> secp_pubkeys;

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    for (int i = 0; i < num_oracles; i++) {
        CKey key = MakeOracleKey(i);
        CPubKey pub = key.GetPubKey();
        keys.push_back(key);
        pubkeys.push_back(pub);

        secp256k1_pubkey spk;
        BOOST_REQUIRE(secp256k1_ec_pubkey_parse(ctx, &spk, pub.data(), pub.size()));
        secp_pubkeys.push_back(spk);
    }

    // Compute aggregate pubkey + keyagg cache
    std::vector<const secp256k1_pubkey*> pk_ptrs;
    for (auto& pk : secp_pubkeys) pk_ptrs.push_back(&pk);

    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache keyagg_cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &keyagg_cache,
                                              pk_ptrs.data(), pk_ptrs.size()));

    // Create a session in g_oracle_signing_sessions
    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
        g_oracle_signing_sessions.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(epoch),
            std::forward_as_tuple(epoch, min_signers));
    }

    // Oracle 0 generates nonce directly (simulating the local oracle)
    secp256k1_musig_pubnonce local_nonce;
    {
        LOCK(g_oracle_signing_sessions_mutex);
        auto& session = g_oracle_signing_sessions.at(epoch);
        BOOST_REQUIRE(session.GenerateNonce(0, keys[0], secp_pubkeys[0], keyagg_cache, local_nonce));
        BOOST_REQUIRE(session.AddPubnonce(0, local_nonce));
    }

    // Remote oracles generate exactly the quorum needed for regtest V1.
    // The old fallback-era test pushed all seven slots through a four-signer
    // session; V1 only needs the active quorum to prove P2P ingestion.
    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // We need separate sessions per oracle to generate their nonces,
    // since GenerateNonce uses internal secnonce state. We'll create
    // temporary sessions just for nonce generation.
    for (int i = 1; i < num_oracles; i++) {
        // Generate nonce in a temporary session
        MuSig2SigningSession temp_session(epoch, min_signers);
        secp256k1_musig_pubnonce remote_nonce;
        BOOST_REQUIRE(temp_session.GenerateNonce(static_cast<uint8_t>(i), keys[i], secp_pubkeys[i], keyagg_cache, remote_nonce));

        // Serialize to P2P message format
        OracleMusigNonceMsg nonce_msg;
        nonce_msg.epoch = epoch;
        nonce_msg.oracle_id = static_cast<uint8_t>(i);
        nonce_msg.pubnonce.resize(66);
        BOOST_REQUIRE(secp256k1_musig_pubnonce_serialize(ctx, nonce_msg.pubnonce.data(), &remote_nonce));

        // RH-24: Sign the nonce message with the oracle's private key
        BOOST_REQUIRE(nonce_msg.Sign(keys[i]));

        // Ingest via ProcessRemoteMusigNonce
        BOOST_CHECK(manager.ProcessRemoteMusigNonce(nonce_msg));
    }

    // Verify the session has all nonces
    {
        LOCK(g_oracle_signing_sessions_mutex);
        auto& session = g_oracle_signing_sessions.at(epoch);
        BOOST_CHECK_EQUAL(session.GetNonceCount(), (size_t)num_oracles);
        BOOST_CHECK(session.HasEnoughNonces());
    }

    // Cleanup
    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
    }

    secp256k1_context_destroy(ctx);
}

/**
 * Test duplicate nonce rejection — same oracle_id should be rejected.
 */
BOOST_AUTO_TEST_CASE(duplicate_nonce_rejected)
{
    const int32_t epoch = 200;
    const uint8_t min_signers = 2;

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    CKey key0 = MakeOracleKey(0);
    CKey key1 = MakeOracleKey(1);
    CPubKey pub0 = key0.GetPubKey();
    CPubKey pub1 = key1.GetPubKey();

    secp256k1_pubkey spk0, spk1;
    BOOST_REQUIRE(secp256k1_ec_pubkey_parse(ctx, &spk0, pub0.data(), pub0.size()));
    BOOST_REQUIRE(secp256k1_ec_pubkey_parse(ctx, &spk1, pub1.data(), pub1.size()));

    std::vector<secp256k1_pubkey> all_pks = {spk0, spk1};
    std::vector<const secp256k1_pubkey*> pk_ptrs = {&all_pks[0], &all_pks[1]};

    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache keyagg_cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &keyagg_cache,
                                              pk_ptrs.data(), pk_ptrs.size()));

    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
        g_oracle_signing_sessions.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(epoch),
            std::forward_as_tuple(epoch, min_signers));

        // Transition session to NONCES_COLLECTING by generating local nonce
        // (simulating the local oracle starting its round)
        auto& session = g_oracle_signing_sessions.at(epoch);
        secp256k1_musig_pubnonce local_nonce;
        BOOST_REQUIRE(session.GenerateNonce(0, key0, spk0, keyagg_cache, local_nonce));
        BOOST_REQUIRE(session.AddPubnonce(0, local_nonce));
    }

    // Generate nonce for oracle 1 in a temporary session
    MuSig2SigningSession temp_session(epoch, min_signers);
    secp256k1_musig_pubnonce nonce1;
    BOOST_REQUIRE(temp_session.GenerateNonce(1, key1, spk1, keyagg_cache, nonce1));

    OracleMusigNonceMsg msg;
    msg.epoch = epoch;
    msg.oracle_id = 1;
    msg.pubnonce.resize(66);
    BOOST_REQUIRE(secp256k1_musig_pubnonce_serialize(ctx, msg.pubnonce.data(), &nonce1));

    // RH-24: Sign the nonce message with oracle 1's private key
    BOOST_REQUIRE(msg.Sign(key1));

    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // First ingestion should succeed
    BOOST_CHECK(manager.ProcessRemoteMusigNonce(msg));

    // Second ingestion of same oracle_id should fail (duplicate)
    BOOST_CHECK(!manager.ProcessRemoteMusigNonce(msg));

    {
        LOCK(g_oracle_signing_sessions_mutex);
        auto& session = g_oracle_signing_sessions.at(epoch);
        // Local oracle 0 + remote oracle 1 = 2 (duplicate was rejected)
        BOOST_CHECK_EQUAL(session.GetNonceCount(), (size_t)2);
    }

    // Cleanup
    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
    }

    secp256k1_context_destroy(ctx);
}

/**
 * Test wrong epoch rejection — nonce for non-existent epoch.
 */
BOOST_AUTO_TEST_CASE(wrong_epoch_nonce_rejected)
{
    const int32_t active_epoch = 300;

    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
        g_oracle_signing_sessions.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(active_epoch),
            std::forward_as_tuple(active_epoch, 4));
    }

    // Valid format but wrong epoch
    OracleMusigNonceMsg msg;
    msg.epoch = 999;  // No session for this epoch
    msg.oracle_id = 0;
    msg.pubnonce.resize(66, 0xCC);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    BOOST_CHECK(!manager.ProcessRemoteMusigNonce(msg));

    // Cleanup
    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
    }
}

/**
 * Verify that GetNonceCount and GetPartialSigCount accessors work.
 */
BOOST_AUTO_TEST_CASE(session_count_accessors)
{
    MuSig2SigningSession session(500, 4);
    BOOST_CHECK_EQUAL(session.GetNonceCount(), (size_t)0);
    BOOST_CHECK_EQUAL(session.GetPartialSigCount(), (size_t)0);
    BOOST_CHECK(!session.HasEnoughNonces());
    BOOST_CHECK(!session.HasEnoughPartialSigs());
}

BOOST_AUTO_TEST_SUITE_END()
