// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * MuSig2 Signing Orchestration Tests
 *
 * Tests the MuSig2Orchestrator which orchestrates session lifecycle:
 * - Session creation per epoch
 * - Nonce generation on epoch start
 * - Session advancement through signing states
 * - Session timeout and pruning on epoch boundary
 * - Oracle/non-oracle behavior
 * - Concurrent epoch isolation
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <key.h>
#include <oracle/musig2_orchestrator.h>
#include <oracle/musig2_session.h>
#include <random.h>
#include <test/util/setup_common.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <cstring>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(musig2_orchestration_tests, BasicTestingSetup)

/** Helper: generate a secp256k1 keypair from random bytes. */
static bool MakeRandomKeypair(secp256k1_context* ctx,
                              unsigned char seckey[32],
                              secp256k1_keypair* keypair,
                              secp256k1_pubkey* pubkey)
{
    GetStrongRandBytes(Span{seckey, 32});
    if (!secp256k1_keypair_create(ctx, keypair, seckey)) return false;
    if (!secp256k1_keypair_pub(ctx, pubkey, keypair)) return false;
    return true;
}

/** Helper: create CKey from raw 32-byte secret key. */
static CKey MakeCKey(const unsigned char seckey[32])
{
    CKey key;
    key.Set(seckey, seckey + 32, true);
    return key;
}

// ============================================================================
// test_session_created_per_epoch
// New epoch = new session, same epoch = same session
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_created_per_epoch)
{
    MuSig2Orchestrator manager;

    // No sessions initially
    BOOST_CHECK(manager.GetSession(100) == nullptr);
    BOOST_CHECK(manager.GetSession(101) == nullptr);

    // Create session for epoch 100
    BOOST_CHECK(manager.CreateSessionForEpoch(100, 9));
    BOOST_CHECK(manager.GetSession(100) != nullptr);
    BOOST_CHECK_EQUAL(manager.GetSession(100)->GetEpoch(), 100);

    // Creating session for same epoch returns false (already exists)
    BOOST_CHECK(!manager.CreateSessionForEpoch(100, 9));

    // Create session for epoch 101 — independent
    BOOST_CHECK(manager.CreateSessionForEpoch(101, 9));
    BOOST_CHECK(manager.GetSession(101) != nullptr);
    BOOST_CHECK_EQUAL(manager.GetSession(101)->GetEpoch(), 101);

    // Both coexist
    BOOST_CHECK(manager.GetSession(100) != nullptr);
    BOOST_CHECK(manager.GetSession(101) != nullptr);
}

// ============================================================================
// test_nonce_generated_on_epoch_start
// GenerateNonce() called with correct key, session transitions
// ============================================================================
BOOST_AUTO_TEST_CASE(test_nonce_generated_on_epoch_start)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr int32_t EPOCH = 42;

    // Generate a keypair for the local oracle
    unsigned char seckey[32];
    secp256k1_keypair kp;
    secp256k1_pubkey pk;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &kp, &pk));

    const secp256k1_pubkey* pk_ptr = &pk;
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, &pk_ptr, 1));

    MuSig2Orchestrator manager;
    BOOST_CHECK(manager.CreateSessionForEpoch(EPOCH, 1));

    // Session should start in CREATED state
    auto* session = manager.GetSession(EPOCH);
    BOOST_REQUIRE(session != nullptr);
    BOOST_CHECK(session->GetState() == MuSig2SessionState::CREATED);

    // Generate nonce
    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(manager.GenerateNonceForEpoch(EPOCH, 0, ckey, pk, cache, pubnonce));

    // Session should now be in NONCES_COLLECTING
    BOOST_CHECK(session->GetState() == MuSig2SessionState::NONCES_COLLECTING);

    // Pubnonce should be valid (66 bytes serializable)
    unsigned char ser[66];
    BOOST_CHECK(secp256k1_musig_pubnonce_serialize(ctx, ser, &pubnonce));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_nonce_broadcast_to_peers
// Nonce message is correctly constructed for P2P broadcast
// ============================================================================
BOOST_AUTO_TEST_CASE(test_nonce_broadcast_to_peers)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr int32_t EPOCH = 50;

    unsigned char seckey[32];
    secp256k1_keypair kp;
    secp256k1_pubkey pk;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &kp, &pk));

    const secp256k1_pubkey* pk_ptr = &pk;
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, &pk_ptr, 1));

    MuSig2Orchestrator manager;
    BOOST_CHECK(manager.CreateSessionForEpoch(EPOCH, 1));

    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(manager.GenerateNonceForEpoch(EPOCH, 0, ckey, pk, cache, pubnonce));

    // Verify pubnonce serializes to 66 bytes (ready for P2P broadcast)
    unsigned char serialized[66];
    BOOST_CHECK(secp256k1_musig_pubnonce_serialize(ctx, serialized, &pubnonce));
    // Serialized pubnonce should not be all zeros
    unsigned char zeros[66] = {};
    BOOST_CHECK(memcmp(serialized, zeros, 66) != 0);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_advances_to_signing
// When enough nonces collected, session advances through states
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_advances_to_signing)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 9;
    constexpr int32_t EPOCH = 77;

    // Generate N keypairs
    unsigned char seckeys[N][32];
    secp256k1_keypair keypairs[N];
    secp256k1_pubkey pubkeys[N];
    for (size_t i = 0; i < N; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
    }

    std::vector<const secp256k1_pubkey*> pubkey_ptrs(N);
    for (size_t i = 0; i < N; i++) pubkey_ptrs[i] = &pubkeys[i];
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), N));

    MuSig2Orchestrator manager;
    BOOST_CHECK(manager.CreateSessionForEpoch(EPOCH, 9));

    // Generate our local nonce (signer 0)
    CKey ckey0 = MakeCKey(seckeys[0]);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_CHECK(manager.GenerateNonceForEpoch(EPOCH, 0, ckey0, pubkeys[0], cache, pubnonce0));

    // Generate and add external nonces
    secp256k1_musig_secnonce ext_secnonces[N];
    secp256k1_musig_pubnonce ext_pubnonces[N];
    ext_pubnonces[0] = pubnonce0;

    for (size_t i = 1; i < N; i++) {
        unsigned char rand[32];
        GetStrongRandBytes(Span{rand, 32});
        BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &ext_secnonces[i], &ext_pubnonces[i],
                                                 rand, seckeys[i], &pubkeys[i],
                                                 nullptr, &cache, nullptr));
    }

    // Feed nonces through manager
    for (size_t i = 0; i < N; i++) {
        BOOST_CHECK(manager.AddNonceForEpoch(EPOCH, static_cast<uint8_t>(i), ext_pubnonces[i]));
    }

    auto* session = manager.GetSession(EPOCH);
    BOOST_REQUIRE(session != nullptr);
    BOOST_CHECK(session->HasEnoughNonces());
    BOOST_CHECK(session->GetState() == MuSig2SessionState::NONCES_COMPLETE);

    // Aggregate nonces with a message hash — advances to SIGNING
    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(manager.AdvanceToSigning(EPOCH, msg));
    BOOST_CHECK(session->GetState() == MuSig2SessionState::SIGNING);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_completes_on_aggregate
// Full flow: nonces → signing → partial sigs → COMPLETE
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_completes_on_aggregate)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 9;
    constexpr int32_t EPOCH = 200;

    unsigned char seckeys[N][32];
    secp256k1_keypair keypairs[N];
    secp256k1_pubkey pubkeys[N];
    CKey ckeys[N];
    for (size_t i = 0; i < N; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
        ckeys[i] = MakeCKey(seckeys[i]);
    }

    std::vector<const secp256k1_pubkey*> pubkey_ptrs(N);
    for (size_t i = 0; i < N; i++) pubkey_ptrs[i] = &pubkeys[i];
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), N));

    MuSig2Orchestrator manager;
    BOOST_CHECK(manager.CreateSessionForEpoch(EPOCH, 9));

    // Generate our local nonce (signer 0)
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_CHECK(manager.GenerateNonceForEpoch(EPOCH, 0, ckeys[0], pubkeys[0], cache, pubnonce0));

    // Generate and add all nonces
    secp256k1_musig_secnonce ext_secnonces[N];
    secp256k1_musig_pubnonce ext_pubnonces[N];
    ext_pubnonces[0] = pubnonce0;
    for (size_t i = 1; i < N; i++) {
        unsigned char rand[32];
        GetStrongRandBytes(Span{rand, 32});
        BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &ext_secnonces[i], &ext_pubnonces[i],
                                                 rand, seckeys[i], &pubkeys[i],
                                                 nullptr, &cache, nullptr));
    }
    for (size_t i = 0; i < N; i++) {
        BOOST_CHECK(manager.AddNonceForEpoch(EPOCH, static_cast<uint8_t>(i), ext_pubnonces[i]));
    }

    // Advance to signing
    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(manager.AdvanceToSigning(EPOCH, msg));

    // Create and add partial signatures
    // Signer 0 creates via session
    secp256k1_musig_partial_sig psig0;
    auto* session = manager.GetSession(EPOCH);
    BOOST_REQUIRE(session != nullptr);
    BOOST_CHECK(session->CreatePartialSignature(0, ckeys[0], psig0));
    BOOST_CHECK(manager.AddPartialSigForEpoch(EPOCH, 0, psig0));

    // External signers create partial sigs via raw API
    std::vector<const secp256k1_musig_pubnonce*> pn_ptrs(N);
    for (size_t i = 0; i < N; i++) pn_ptrs[i] = &ext_pubnonces[i];
    secp256k1_musig_aggnonce aggnonce;
    BOOST_REQUIRE(secp256k1_musig_nonce_agg(ctx, &aggnonce, pn_ptrs.data(), N));
    secp256k1_musig_session raw_session;
    BOOST_REQUIRE(secp256k1_musig_nonce_process(ctx, &raw_session, &aggnonce, msg, &cache));

    for (size_t i = 1; i < N; i++) {
        secp256k1_musig_partial_sig psig;
        BOOST_REQUIRE(secp256k1_musig_partial_sign(ctx, &psig, &ext_secnonces[i],
                                                    &keypairs[i], &cache, &raw_session));
        BOOST_CHECK(manager.AddPartialSigForEpoch(EPOCH, static_cast<uint8_t>(i), psig));
    }

    BOOST_CHECK(session->HasEnoughPartialSigs());

    // Aggregate → COMPLETE
    std::vector<unsigned char> sig64;
    BOOST_CHECK(manager.AggregateForEpoch(EPOCH, sig64));
    BOOST_CHECK(session->GetState() == MuSig2SessionState::COMPLETE);
    BOOST_CHECK_EQUAL(sig64.size(), 64u);

    // Verify aggregate signature
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, sig64.data(), msg, 32, &agg_pk));

    // GetCompletedSessionData should return sig + bitmap
    std::vector<unsigned char> out_sig, out_bitmap;
    BOOST_CHECK(manager.GetCompletedSessionData(EPOCH, out_sig, out_bitmap));
    BOOST_CHECK_EQUAL(out_sig.size(), 64u);
    BOOST_CHECK(!out_bitmap.empty());

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_timeout_on_epoch_boundary
// Session expires after epoch ends (pruning old sessions)
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_timeout_on_epoch_boundary)
{
    MuSig2Orchestrator manager;

    // Create sessions for epochs 100 and 101
    BOOST_CHECK(manager.CreateSessionForEpoch(100, 9));
    BOOST_CHECK(manager.CreateSessionForEpoch(101, 9));

    // Both exist
    BOOST_CHECK(manager.GetSession(100) != nullptr);
    BOOST_CHECK(manager.GetSession(101) != nullptr);

    // Prune old sessions — keep only current and next epoch
    // If current epoch is 102, epoch 100 and 101 should be pruned
    manager.PruneOldSessions(102);
    BOOST_CHECK(manager.GetSession(100) == nullptr);
    BOOST_CHECK(manager.GetSession(101) == nullptr);

    // Create fresh sessions
    BOOST_CHECK(manager.CreateSessionForEpoch(102, 9));
    BOOST_CHECK(manager.CreateSessionForEpoch(103, 9));

    // Prune with current=103: epoch 102 is current-1, keep current and next only
    manager.PruneOldSessions(103);
    BOOST_CHECK(manager.GetSession(102) == nullptr);
    BOOST_CHECK(manager.GetSession(103) != nullptr);
}

// ============================================================================
// test_oracle_skips_signing_when_inactive
// Non-oracle node doesn't create sessions
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_skips_signing_when_inactive)
{
    MuSig2Orchestrator manager;

    // ShouldParticipate returns false when not configured as oracle
    BOOST_CHECK(!manager.IsOracleActive());

    // Setting oracle identity enables participation
    CKey key;
    key.MakeNewKey(true);
    manager.SetLocalOracle(5, key);
    BOOST_CHECK(manager.IsOracleActive());
    BOOST_CHECK_EQUAL(manager.GetLocalOracleId(), 5);

    // Clear oracle identity
    manager.ClearLocalOracle();
    BOOST_CHECK(!manager.IsOracleActive());
}

// ============================================================================
// test_concurrent_epochs_no_crosstalk
// Epoch N+1 session doesn't interfere with epoch N
// ============================================================================
BOOST_AUTO_TEST_CASE(test_concurrent_epochs_no_crosstalk)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    // Create two independent keypairs
    unsigned char seckey_a[32], seckey_b[32];
    secp256k1_keypair kp_a, kp_b;
    secp256k1_pubkey pk_a, pk_b;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey_a, &kp_a, &pk_a));
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey_b, &kp_b, &pk_b));

    const secp256k1_pubkey* pk_ptr_a = &pk_a;
    const secp256k1_pubkey* pk_ptr_b = &pk_b;
    secp256k1_xonly_pubkey agg_pk_a, agg_pk_b;
    secp256k1_musig_keyagg_cache cache_a, cache_b;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk_a, &cache_a, &pk_ptr_a, 1));
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk_b, &cache_b, &pk_ptr_b, 1));

    MuSig2Orchestrator manager;

    // Create sessions for two consecutive epochs
    BOOST_CHECK(manager.CreateSessionForEpoch(100, 1));
    BOOST_CHECK(manager.CreateSessionForEpoch(101, 1));

    // Generate nonces independently
    CKey ckey_a = MakeCKey(seckey_a);
    CKey ckey_b = MakeCKey(seckey_b);
    secp256k1_musig_pubnonce pn_a, pn_b;
    BOOST_CHECK(manager.GenerateNonceForEpoch(100, 0, ckey_a, pk_a, cache_a, pn_a));
    BOOST_CHECK(manager.GenerateNonceForEpoch(101, 0, ckey_b, pk_b, cache_b, pn_b));

    // Add nonces to their respective sessions
    BOOST_CHECK(manager.AddNonceForEpoch(100, 0, pn_a));
    BOOST_CHECK(manager.AddNonceForEpoch(101, 0, pn_b));

    // Advance epoch 100 to signing, leave 101 at NONCES_COMPLETE
    unsigned char msg_a[32];
    GetStrongRandBytes(Span{msg_a, 32});
    BOOST_CHECK(manager.AdvanceToSigning(100, msg_a));

    // Verify states are independent
    auto* session_a = manager.GetSession(100);
    auto* session_b = manager.GetSession(101);
    BOOST_REQUIRE(session_a != nullptr);
    BOOST_REQUIRE(session_b != nullptr);

    BOOST_CHECK(session_a->GetState() == MuSig2SessionState::SIGNING);
    BOOST_CHECK(session_b->GetState() == MuSig2SessionState::NONCES_COMPLETE);

    // Complete epoch 100 but not 101
    secp256k1_musig_partial_sig psig_a;
    BOOST_CHECK(session_a->CreatePartialSignature(0, ckey_a, psig_a));
    BOOST_CHECK(manager.AddPartialSigForEpoch(100, 0, psig_a));
    std::vector<unsigned char> sig_a;
    BOOST_CHECK(manager.AggregateForEpoch(100, sig_a));

    BOOST_CHECK(session_a->GetState() == MuSig2SessionState::COMPLETE);
    BOOST_CHECK(session_b->GetState() == MuSig2SessionState::NONCES_COMPLETE);

    // Pruning with current_epoch=101 should remove epoch 100 (completed, old)
    manager.PruneOldSessions(101);
    BOOST_CHECK(manager.GetSession(100) == nullptr);
    BOOST_CHECK(manager.GetSession(101) != nullptr);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_count_limit
// Only current and next epoch sessions are kept (max 2 active)
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_count_limit)
{
    MuSig2Orchestrator manager;

    // Create sessions for epochs 1-5
    for (int32_t e = 1; e <= 5; e++) {
        BOOST_CHECK(manager.CreateSessionForEpoch(e, 9));
    }

    // All 5 exist before pruning
    for (int32_t e = 1; e <= 5; e++) {
        BOOST_CHECK(manager.GetSession(e) != nullptr);
    }

    // Prune with current epoch = 4 (keep 4 and 5)
    manager.PruneOldSessions(4);

    BOOST_CHECK(manager.GetSession(1) == nullptr);
    BOOST_CHECK(manager.GetSession(2) == nullptr);
    BOOST_CHECK(manager.GetSession(3) == nullptr);
    BOOST_CHECK(manager.GetSession(4) != nullptr);
    BOOST_CHECK(manager.GetSession(5) != nullptr);
}

// ============================================================================
// test_check_and_advance_signing
// Manager auto-checks if session is ready to advance
// ============================================================================
BOOST_AUTO_TEST_CASE(test_check_and_advance_signing)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 9;
    constexpr int32_t EPOCH = 300;

    unsigned char seckeys[N][32];
    secp256k1_keypair keypairs[N];
    secp256k1_pubkey pubkeys[N];
    for (size_t i = 0; i < N; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
    }

    std::vector<const secp256k1_pubkey*> pubkey_ptrs(N);
    for (size_t i = 0; i < N; i++) pubkey_ptrs[i] = &pubkeys[i];
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), N));

    MuSig2Orchestrator manager;
    BOOST_CHECK(manager.CreateSessionForEpoch(EPOCH, 9));

    // Generate nonce for signer 0
    CKey ckey0 = MakeCKey(seckeys[0]);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_CHECK(manager.GenerateNonceForEpoch(EPOCH, 0, ckey0, pubkeys[0], cache, pubnonce0));

    // Not ready yet — only 0 nonces added
    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(!manager.TryAdvanceToSigning(EPOCH, msg));

    // Add all 9 nonces
    secp256k1_musig_secnonce ext_secnonces[N];
    secp256k1_musig_pubnonce ext_pubnonces[N];
    ext_pubnonces[0] = pubnonce0;
    for (size_t i = 1; i < N; i++) {
        unsigned char rand[32];
        GetStrongRandBytes(Span{rand, 32});
        BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &ext_secnonces[i], &ext_pubnonces[i],
                                                 rand, seckeys[i], &pubkeys[i],
                                                 nullptr, &cache, nullptr));
    }
    for (size_t i = 0; i < N; i++) {
        BOOST_CHECK(manager.AddNonceForEpoch(EPOCH, static_cast<uint8_t>(i), ext_pubnonces[i]));
    }

    // Now TryAdvanceToSigning should succeed
    BOOST_CHECK(manager.TryAdvanceToSigning(EPOCH, msg));
    BOOST_CHECK(manager.GetSession(EPOCH)->GetState() == MuSig2SessionState::SIGNING);

    // Calling again should be no-op (already in SIGNING)
    BOOST_CHECK(!manager.TryAdvanceToSigning(EPOCH, msg));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_completed_bitmap_is_padded_to_network_oracle_count
// Completed-session bitmap width must match network oracle count for decoding
// ============================================================================
BOOST_AUTO_TEST_CASE(test_completed_bitmap_is_padded_to_network_oracle_count)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr int32_t EPOCH = 401;

    unsigned char seckey[32];
    secp256k1_keypair kp;
    secp256k1_pubkey pk;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &kp, &pk));

    const secp256k1_pubkey* pk_ptr = &pk;
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, &pk_ptr, 1));

    MuSig2Orchestrator manager;
    BOOST_REQUIRE(manager.CreateSessionForEpoch(EPOCH, 1));

    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_REQUIRE(manager.GenerateNonceForEpoch(EPOCH, 0, ckey, pk, cache, pubnonce));
    BOOST_REQUIRE(manager.AddNonceForEpoch(EPOCH, 0, pubnonce));

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_REQUIRE(manager.AdvanceToSigning(EPOCH, msg));

    auto* session = manager.GetSession(EPOCH);
    BOOST_REQUIRE(session != nullptr);

    secp256k1_musig_partial_sig psig;
    BOOST_REQUIRE(session->CreatePartialSignature(0, ckey, psig));
    BOOST_REQUIRE(manager.AddPartialSigForEpoch(EPOCH, 0, psig));

    std::vector<unsigned char> sig64;
    BOOST_REQUIRE(manager.AggregateForEpoch(EPOCH, sig64));

    std::vector<unsigned char> out_sig, out_bitmap;
    BOOST_REQUIRE(manager.GetCompletedSessionData(EPOCH, out_sig, out_bitmap));

    const uint16_t total_oracles = static_cast<uint16_t>(Params().GetConsensus().nOracleTotalOracles);
    const size_t expected_bitmap_bytes = (total_oracles + 7) / 8;
    BOOST_CHECK_EQUAL(out_bitmap.size(), expected_bitmap_bytes);
    BOOST_CHECK_EQUAL(out_bitmap[0], 0x01);
}

// ============================================================================
// test_nonexistent_epoch_operations_fail
// Operations on non-existent epoch return false gracefully
// ============================================================================
BOOST_AUTO_TEST_CASE(test_nonexistent_epoch_operations_fail)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    unsigned char seckey[32];
    secp256k1_keypair kp;
    secp256k1_pubkey pk;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &kp, &pk));

    const secp256k1_pubkey* pk_ptr = &pk;
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, &pk_ptr, 1));

    MuSig2Orchestrator manager;

    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    secp256k1_musig_partial_sig psig;
    unsigned char msg[32] = {};
    std::vector<unsigned char> sig64;

    // All operations should fail gracefully for non-existent epoch
    BOOST_CHECK(!manager.GenerateNonceForEpoch(999, 0, ckey, pk, cache, pubnonce));
    BOOST_CHECK(!manager.AddNonceForEpoch(999, 0, pubnonce));
    BOOST_CHECK(!manager.AdvanceToSigning(999, msg));
    BOOST_CHECK(!manager.TryAdvanceToSigning(999, msg));
    BOOST_CHECK(!manager.AddPartialSigForEpoch(999, 0, psig));
    BOOST_CHECK(!manager.AggregateForEpoch(999, sig64));

    std::vector<unsigned char> out_sig, out_bitmap;
    BOOST_CHECK(!manager.GetCompletedSessionData(999, out_sig, out_bitmap));

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_SUITE_END()
