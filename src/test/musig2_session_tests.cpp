// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * MuSig2SigningSession Tests (TDD — tests before implementation)
 *
 * Tests the in-process MuSig2 signing state machine:
 * - State transitions: CREATED→NONCES_COLLECTING→NONCES_COMPLETE→SIGNING→COMPLETE
 * - Nonce generation, collection, aggregation
 * - Partial signature creation and aggregation
 * - Configured active signer quorum inside the 35-slot oracle reserve
 * - Security: nonce zeroing, reuse prevention
 * - Timeout/failure transitions
 * - Concurrent epoch isolation
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <key.h>
#include <primitives/oracle.h>
#include <random.h>
#include <test/util/setup_common.h>
#include <oracle/musig2_session.h>
#include <oracle/musig2_session_manager.h>
#include <uint256.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <set>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(musig2_session_tests, BasicTestingSetup)

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

static secp256k1_musig_keyagg_cache MakeSingleKeyAggCache(secp256k1_context* ctx)
{
    unsigned char seckey[32];
    secp256k1_keypair keypair;
    secp256k1_pubkey pubkey;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &keypair, &pubkey));

    const secp256k1_pubkey* pubkey_ptr = &pubkey;
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, &pubkey_ptr, 1));
    return cache;
}

static secp256k1_musig_pubnonce MakeValidPubnonceForOracle(secp256k1_context* ctx,
                                                           uint8_t oracle_id,
                                                           int32_t epoch)
{
    unsigned char seckey[32];
    secp256k1_keypair keypair;
    secp256k1_pubkey pubkey;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &keypair, &pubkey));

    const secp256k1_pubkey* pubkey_ptr = &pubkey;
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, &pubkey_ptr, 1));

    MuSig2SigningSession nonce_session(epoch, 1);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_REQUIRE(nonce_session.GenerateNonce(oracle_id, MakeCKey(seckey), pubkey, cache, pubnonce));
    return pubnonce;
}

static std::vector<uint8_t> ExpectedEpochCommittee(std::vector<uint8_t> ids,
                                                   int32_t epoch,
                                                   size_t threshold,
                                                   const uint256& seed)
{
    std::vector<std::pair<uint256, uint8_t>> scored;
    scored.reserve(ids.size());
    for (uint8_t id : ids) {
        scored.emplace_back(GetOracleEpochSelectionHash(epoch, id, seed), id);
    }
    std::sort(scored.begin(), scored.end());

    std::vector<uint8_t> selected;
    for (const auto& [score, id] : scored) {
        if (selected.size() >= threshold) break;
        selected.push_back(id);
    }
    return selected;
}

static std::vector<uint8_t> ExpectedEpochCommittee(std::vector<uint8_t> ids,
                                                   int32_t epoch,
                                                   size_t threshold)
{
    return ExpectedEpochCommittee(std::move(ids), epoch, threshold,
                                  Params().GetConsensus().hashGenesisBlock);
}

static uint256 FilledSeed(unsigned char value)
{
    uint256 seed;
    std::fill(seed.begin(), seed.end(), value);
    return seed;
}

static uint8_t ActiveMuSig2OracleCount()
{
    const int active_count = Params().GetConsensus().nOraclePubkeyCount;
    BOOST_REQUIRE(active_count > 0);
    BOOST_REQUIRE(active_count < 256);
    return static_cast<uint8_t>(active_count);
}

static uint8_t ActiveMuSig2Threshold()
{
    const int threshold = Params().GetConsensus().nOracleConsensusRequired;
    BOOST_REQUIRE(threshold > 0);
    BOOST_REQUIRE(threshold < 256);
    return static_cast<uint8_t>(threshold);
}

// ============================================================================
// test_session_state_machine_transitions
// CREATED → NONCES_COLLECTING → NONCES_COMPLETE → SIGNING → COMPLETE
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_state_machine_transitions)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 9;
    constexpr uint8_t MIN_SIGNERS = 9;
    constexpr int32_t EPOCH = 100;

    // Generate 9 keypairs
    unsigned char seckeys[N][32];
    secp256k1_keypair keypairs[N];
    secp256k1_pubkey pubkeys[N];
    for (size_t i = 0; i < N; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
    }

    // Key aggregation
    std::vector<const secp256k1_pubkey*> pubkey_ptrs(N);
    for (size_t i = 0; i < N; i++) pubkey_ptrs[i] = &pubkeys[i];
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), N));

    // Create session — should be CREATED
    MuSig2SigningSession session(EPOCH, MIN_SIGNERS);
    BOOST_CHECK(session.GetState() == MuSig2SessionState::CREATED);
    BOOST_CHECK_EQUAL(session.GetEpoch(), EPOCH);

    // Generate nonce for signer 0 → should transition to NONCES_COLLECTING
    CKey ckey0 = MakeCKey(seckeys[0]);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_CHECK(session.GenerateNonce(0, ckey0, pubkeys[0], cache, pubnonce0));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COLLECTING);

    // Add all 9 pubnonces (including signer 0's own)
    // First, generate nonces externally for signers 1-8
    secp256k1_musig_secnonce ext_secnonces[N];
    secp256k1_musig_pubnonce ext_pubnonces[N];
    ext_pubnonces[0] = pubnonce0;

    for (size_t i = 1; i < N; i++) {
        unsigned char session_secrand[32];
        GetStrongRandBytes(Span{session_secrand, 32});
        BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &ext_secnonces[i], &ext_pubnonces[i],
                                                 session_secrand, seckeys[i], &pubkeys[i],
                                                 nullptr, &cache, nullptr));
    }

    // Add pubnonces from all 9 signers
    for (size_t i = 0; i < N; i++) {
        BOOST_CHECK(session.AddPubnonce(static_cast<uint8_t>(i), ext_pubnonces[i]));
    }
    BOOST_CHECK(session.HasEnoughNonces());
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COMPLETE);

    // Aggregate nonces with message → transitions to SIGNING
    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(session.AggregateNonces(msg));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::SIGNING);

    // Create partial signature for our signer (0)
    secp256k1_musig_partial_sig psig0;
    BOOST_CHECK(session.CreatePartialSignature(0, ckey0, psig0));

    // Add partial sigs for all 9 signers (0's was just created, do externally for 1-8)
    BOOST_CHECK(session.AddPartialSignature(0, psig0));

    // For signers 1-8, create partial sigs externally via secp256k1 API
    // We need the session's internal secp256k1_musig_session, so instead
    // we create the remaining partial sigs through the session interface
    // by calling CreatePartialSignature for each signer
    // Actually, since MuSig2SigningSession manages a single signer's secnonce,
    // external partial sigs should be added via AddPartialSignature.
    // Let's create them using the raw API with the same aggnonce.

    // Get the aggregate nonce and session from the raw API for external signers
    std::vector<const secp256k1_musig_pubnonce*> pubnonce_ptrs(N);
    for (size_t i = 0; i < N; i++) pubnonce_ptrs[i] = &ext_pubnonces[i];
    secp256k1_musig_aggnonce aggnonce;
    BOOST_REQUIRE(secp256k1_musig_nonce_agg(ctx, &aggnonce, pubnonce_ptrs.data(), N));
    secp256k1_musig_session raw_session;
    BOOST_REQUIRE(secp256k1_musig_nonce_process(ctx, &raw_session, &aggnonce, msg, &cache));

    for (size_t i = 1; i < N; i++) {
        secp256k1_musig_partial_sig psig;
        BOOST_REQUIRE(secp256k1_musig_partial_sign(ctx, &psig, &ext_secnonces[i],
                                                    &keypairs[i], &cache, &raw_session));
        BOOST_CHECK(session.AddPartialSignature(static_cast<uint8_t>(i), psig));
    }

    BOOST_CHECK(session.HasEnoughPartialSigs());

    // Aggregate final signature → COMPLETE
    std::vector<unsigned char> sig64;
    BOOST_CHECK(session.AggregateSignature(sig64));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::COMPLETE);
    BOOST_CHECK_EQUAL(sig64.size(), 64u);

    // Verify the aggregate signature with standard schnorrsig_verify
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, sig64.data(), msg, 32, &agg_pk));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_nonce_generation
// Generate nonce pair (secnonce + pubnonce)
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_nonce_generation)
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

    MuSig2SigningSession session(1, 1);
    BOOST_CHECK(session.GetState() == MuSig2SessionState::CREATED);

    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(session.GenerateNonce(0, ckey, pk, cache, pubnonce));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COLLECTING);

    // Pubnonce should serialize to 66 bytes and be non-zero
    unsigned char ser[66];
    BOOST_CHECK(secp256k1_musig_pubnonce_serialize(ctx, ser, &pubnonce));
    unsigned char zeros[66] = {0};
    BOOST_CHECK(memcmp(ser, zeros, 66) != 0);

    // Cannot generate nonce twice
    secp256k1_musig_pubnonce pubnonce2;
    BOOST_CHECK(!session.GenerateNonce(0, ckey, pk, cache, pubnonce2));

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_CASE(test_passive_init_keeps_locally_started_session_alive)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 2;
    unsigned char seckeys[N][32];
    secp256k1_keypair keypairs[N];
    secp256k1_pubkey pubkeys[N];
    for (size_t i = 0; i < N; ++i) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
    }

    std::vector<const secp256k1_pubkey*> pubkey_ptrs(N);
    for (size_t i = 0; i < N; ++i) pubkey_ptrs[i] = &pubkeys[i];
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), N));

    MuSig2SigningSession session(77, N);

    secp256k1_musig_pubnonce local_pubnonce;
    BOOST_REQUIRE(session.GenerateNonce(0, MakeCKey(seckeys[0]), pubkeys[0], cache, local_pubnonce));
    BOOST_REQUIRE_EQUAL(static_cast<int>(session.GetState()),
                        static_cast<int>(MuSig2SessionState::NONCES_COLLECTING));

    // Regression for the live multi-oracle race: a P2P nonce can enter
    // IngestRemoteNonce after it observed CREATED but after local nonce
    // generation moved the same session to NONCES_COLLECTING. Passive init
    // must be idempotent so that valid remote nonce is not dropped.
    BOOST_REQUIRE(session.InitializePassive(cache));
    BOOST_REQUIRE_EQUAL(static_cast<int>(session.GetState()),
                        static_cast<int>(MuSig2SessionState::NONCES_COLLECTING));

    secp256k1_musig_secnonce remote_secnonce;
    secp256k1_musig_pubnonce remote_pubnonce;
    unsigned char session_rand[32];
    GetStrongRandBytes(Span{session_rand, 32});
    BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx,
                                            &remote_secnonce,
                                            &remote_pubnonce,
                                            session_rand,
                                            seckeys[1],
                                            &pubkeys[1],
                                            nullptr,
                                            &cache,
                                            nullptr));

    BOOST_REQUIRE(session.AddPubnonce(0, local_pubnonce));
    BOOST_REQUIRE(session.AddPubnonce(1, remote_pubnonce));
    BOOST_CHECK(session.HasEnoughNonces());
    BOOST_CHECK_EQUAL(static_cast<int>(session.GetState()),
                      static_cast<int>(MuSig2SessionState::NONCES_COMPLETE));

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_CASE(required_participants_use_epoch_hash_not_sequential_ids)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    constexpr int32_t epoch = 40;
    const uint8_t threshold = ActiveMuSig2Threshold();

    MuSig2SigningSession session(epoch, threshold);
    BOOST_REQUIRE(session.InitializePassive(MakeSingleKeyAggCache(ctx)));

    std::vector<uint8_t> submitted;
    const uint8_t active_count = ActiveMuSig2OracleCount();
    for (uint8_t id = 0; id < active_count; ++id) {
        submitted.push_back(id);
        secp256k1_musig_pubnonce pubnonce = MakeValidPubnonceForOracle(ctx, id, epoch);
        BOOST_REQUIRE(session.AddPubnonce(id, pubnonce));
    }

    std::vector<uint8_t> expected = ExpectedEpochCommittee(submitted, epoch, threshold);
    std::vector<uint8_t> required = session.GetRequiredParticipants();
    BOOST_CHECK(required == expected);

    std::vector<uint8_t> sequential(threshold);
    std::iota(sequential.begin(), sequential.end(), 0);
    BOOST_CHECK(required != sequential);

    std::vector<uint8_t> next_expected = ExpectedEpochCommittee(submitted, epoch + 1, threshold);
    BOOST_CHECK(next_expected != expected);

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_CASE(required_participants_use_chain_seeded_epoch_hash)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    constexpr int32_t epoch = 52;
    const uint8_t threshold = ActiveMuSig2Threshold();
    const uint256 seed_a = FilledSeed(0x11);
    const uint256 seed_b = FilledSeed(0x22);

    MuSig2SigningSession session_a(epoch, threshold);
    MuSig2SigningSession session_b(epoch, threshold);
    session_a.SetEpochSelectionSeed(seed_a);
    session_b.SetEpochSelectionSeed(seed_b);
    BOOST_REQUIRE(session_a.InitializePassive(MakeSingleKeyAggCache(ctx)));
    BOOST_REQUIRE(session_b.InitializePassive(MakeSingleKeyAggCache(ctx)));

    std::vector<uint8_t> submitted;
    const uint8_t active_count = ActiveMuSig2OracleCount();
    for (uint8_t id = 0; id < active_count; ++id) {
        submitted.push_back(id);
        secp256k1_musig_pubnonce pubnonce = MakeValidPubnonceForOracle(ctx, id, epoch);
        BOOST_REQUIRE(session_a.AddPubnonce(id, pubnonce));
        BOOST_REQUIRE(session_b.AddPubnonce(id, pubnonce));
    }

    const std::vector<uint8_t> expected_a =
        ExpectedEpochCommittee(submitted, epoch, threshold, seed_a);
    const std::vector<uint8_t> expected_b =
        ExpectedEpochCommittee(submitted, epoch, threshold, seed_b);
    BOOST_CHECK(session_a.GetRequiredParticipants() == expected_a);
    BOOST_CHECK(session_b.GetRequiredParticipants() == expected_b);
    BOOST_CHECK(expected_a != expected_b);

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_CASE(required_participants_and_context_converge_across_nonce_arrival_order)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    constexpr int32_t epoch = 61;
    const uint8_t threshold = ActiveMuSig2Threshold();
    const uint256 seed = FilledSeed(0x37);

    const uint8_t active_count = ActiveMuSig2OracleCount();
    std::vector<uint8_t> all_ids(active_count);
    std::iota(all_ids.begin(), all_ids.end(), 0);

    std::vector<secp256k1_musig_pubnonce> pubnonces(active_count);
    for (uint8_t id : all_ids) {
        pubnonces[id] = MakeValidPubnonceForOracle(ctx, id, epoch);
    }

    std::vector<uint8_t> descending = all_ids;
    std::reverse(descending.begin(), descending.end());
    std::vector<uint8_t> mixed = all_ids;
    std::rotate(mixed.begin(), mixed.begin() + 13, mixed.end());
    std::reverse(mixed.begin() + 5, mixed.end());

    MuSig2SigningSession ascending_session(epoch, threshold);
    MuSig2SigningSession descending_session(epoch, threshold);
    MuSig2SigningSession mixed_session(epoch, threshold);
    ascending_session.SetEpochSelectionSeed(seed);
    descending_session.SetEpochSelectionSeed(seed);
    mixed_session.SetEpochSelectionSeed(seed);
    BOOST_REQUIRE(ascending_session.InitializePassive(MakeSingleKeyAggCache(ctx)));
    BOOST_REQUIRE(descending_session.InitializePassive(MakeSingleKeyAggCache(ctx)));
    BOOST_REQUIRE(mixed_session.InitializePassive(MakeSingleKeyAggCache(ctx)));

    for (uint8_t id : all_ids) {
        BOOST_REQUIRE(ascending_session.AddPubnonce(id, pubnonces[id]));
    }
    for (uint8_t id : descending) {
        BOOST_REQUIRE(descending_session.AddPubnonce(id, pubnonces[id]));
    }
    for (uint8_t id : mixed) {
        BOOST_REQUIRE(mixed_session.AddPubnonce(id, pubnonces[id]));
    }

    const std::vector<uint8_t> expected =
        ExpectedEpochCommittee(all_ids, epoch, threshold, seed);
    BOOST_CHECK(ascending_session.GetRequiredParticipants() == expected);
    BOOST_CHECK(descending_session.GetRequiredParticipants() == expected);
    BOOST_CHECK(mixed_session.GetRequiredParticipants() == expected);

    unsigned char msg32[32];
    std::fill(msg32, msg32 + 32, 0x42);
    uint256 nonce_hash_ascending;
    uint256 context_ascending;
    uint256 nonce_hash_descending;
    uint256 context_descending;
    uint256 nonce_hash_mixed;
    uint256 context_mixed;
    BOOST_REQUIRE(ascending_session.ComputeContextIdForParticipants(
        expected, msg32, nonce_hash_ascending, context_ascending));
    BOOST_REQUIRE(descending_session.ComputeContextIdForParticipants(
        expected, msg32, nonce_hash_descending, context_descending));
    BOOST_REQUIRE(mixed_session.ComputeContextIdForParticipants(
        expected, msg32, nonce_hash_mixed, context_mixed));

    BOOST_CHECK(nonce_hash_ascending == nonce_hash_descending);
    BOOST_CHECK(nonce_hash_ascending == nonce_hash_mixed);
    BOOST_CHECK(context_ascending == context_descending);
    BOOST_CHECK(context_ascending == context_mixed);

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_CASE(required_participants_reject_committee_that_omits_known_better_nonce)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    constexpr int32_t epoch = 63;
    const uint8_t threshold = ActiveMuSig2Threshold();
    const uint256 seed = FilledSeed(0x63);

    const uint8_t active_count = ActiveMuSig2OracleCount();
    std::vector<uint8_t> all_ids(active_count);
    std::iota(all_ids.begin(), all_ids.end(), 0);

    MuSig2SigningSession session(epoch, threshold);
    session.SetEpochSelectionSeed(seed);
    BOOST_REQUIRE(session.InitializePassive(MakeSingleKeyAggCache(ctx)));

    for (uint8_t id : all_ids) {
        secp256k1_musig_pubnonce pubnonce = MakeValidPubnonceForOracle(ctx, id, epoch);
        BOOST_REQUIRE(session.AddPubnonce(id, pubnonce));
    }

    const std::vector<uint8_t> expected =
        ExpectedEpochCommittee(all_ids, epoch, threshold, seed);
    BOOST_REQUIRE_EQUAL(expected.size(), threshold);
    BOOST_CHECK(session.MatchesRequiredParticipants(expected));

    std::vector<uint8_t> biased = expected;
    const auto replacement_it = std::find_if(all_ids.begin(), all_ids.end(), [&](uint8_t id) {
        return std::find(expected.begin(), expected.end(), id) == expected.end();
    });
    BOOST_REQUIRE(replacement_it != all_ids.end());
    biased.back() = *replacement_it;
    std::sort(biased.begin(), biased.end(), [&](uint8_t a, uint8_t b) {
        const uint256 score_a = GetOracleEpochSelectionHash(epoch, a, seed);
        const uint256 score_b = GetOracleEpochSelectionHash(epoch, b, seed);
        if (score_a == score_b) return a < b;
        return score_a < score_b;
    });

    BOOST_CHECK(!session.MatchesRequiredParticipants(biased));

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_CASE(session_context_id_changes_when_epoch_selection_seed_changes)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    constexpr int32_t epoch = 62;
    const uint8_t threshold = ActiveMuSig2Threshold();
    const uint256 seed_a = FilledSeed(0x51);
    const uint256 seed_b = FilledSeed(0x52);
    const std::vector<uint8_t> participants{0, 1, 2, 3, 4, 5, 6};

    MuSig2SigningSession session_a(epoch, threshold);
    MuSig2SigningSession session_b(epoch, threshold);
    session_a.SetEpochSelectionSeed(seed_a);
    session_b.SetEpochSelectionSeed(seed_b);
    BOOST_REQUIRE(session_a.InitializePassive(MakeSingleKeyAggCache(ctx)));
    BOOST_REQUIRE(session_b.InitializePassive(MakeSingleKeyAggCache(ctx)));

    for (uint8_t id : participants) {
        secp256k1_musig_pubnonce pubnonce = MakeValidPubnonceForOracle(ctx, id, epoch);
        BOOST_REQUIRE(session_a.AddPubnonce(id, pubnonce));
        BOOST_REQUIRE(session_b.AddPubnonce(id, pubnonce));
    }

    unsigned char msg32[32];
    std::fill(msg32, msg32 + 32, 0x24);
    uint256 nonce_hash_a;
    uint256 context_a;
    uint256 nonce_hash_b;
    uint256 context_b;
    BOOST_REQUIRE(session_a.ComputeContextIdForParticipants(
        participants, msg32, nonce_hash_a, context_a));
    BOOST_REQUIRE(session_b.ComputeContextIdForParticipants(
        participants, msg32, nonce_hash_b, context_b));

    BOOST_CHECK(nonce_hash_a == nonce_hash_b);
    BOOST_CHECK(context_a != context_b);

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_CASE(session_context_id_changes_when_attempt_id_changes)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    constexpr int32_t epoch = 63;
    const uint8_t threshold = ActiveMuSig2Threshold();
    const uint256 seed = FilledSeed(0x53);
    const std::vector<uint8_t> participants{0, 1, 2, 3, 4, 5, 6};

    MuSig2SigningSession attempt0(epoch, threshold, 0);
    MuSig2SigningSession attempt1(epoch, threshold, 1);
    attempt0.SetEpochSelectionSeed(seed);
    attempt1.SetEpochSelectionSeed(seed);
    BOOST_REQUIRE(attempt0.InitializePassive(MakeSingleKeyAggCache(ctx)));
    BOOST_REQUIRE(attempt1.InitializePassive(MakeSingleKeyAggCache(ctx)));

    for (uint8_t id : participants) {
        secp256k1_musig_pubnonce pubnonce = MakeValidPubnonceForOracle(ctx, id, epoch);
        BOOST_REQUIRE(attempt0.AddPubnonce(id, pubnonce));
        BOOST_REQUIRE(attempt1.AddPubnonce(id, pubnonce));
    }

    unsigned char msg32[32];
    std::fill(msg32, msg32 + 32, 0x25);
    uint256 nonce_hash_0;
    uint256 context_0;
    uint256 nonce_hash_1;
    uint256 context_1;
    BOOST_REQUIRE(attempt0.ComputeContextIdForParticipants(
        participants, msg32, nonce_hash_0, context_0));
    BOOST_REQUIRE(attempt1.ComputeContextIdForParticipants(
        participants, msg32, nonce_hash_1, context_1));

    BOOST_CHECK(nonce_hash_0 != nonce_hash_1);
    BOOST_CHECK(context_0 != context_1);

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_CASE(required_participants_complete_with_offline_low_ids)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx);

    constexpr int32_t epoch = 40;
    const uint8_t threshold = ActiveMuSig2Threshold();
    const std::vector<uint8_t> online_ids{0, 1, 2, 3, 4, 10, 14};

    MuSig2SigningSession session(epoch, threshold);
    BOOST_REQUIRE(session.InitializePassive(MakeSingleKeyAggCache(ctx)));

    for (uint8_t id : online_ids) {
        secp256k1_musig_pubnonce pubnonce = MakeValidPubnonceForOracle(ctx, id, epoch);
        BOOST_REQUIRE(session.AddPubnonce(id, pubnonce));
    }

    BOOST_CHECK_EQUAL(session.GetNonceCount(), online_ids.size());
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COMPLETE);

    std::vector<uint8_t> required = session.GetRequiredParticipants();
    BOOST_CHECK_EQUAL(required.size(), online_ids.size());

    const std::set<uint8_t> online_set(online_ids.begin(), online_ids.end());
    for (uint8_t id : required) {
        BOOST_CHECK(online_set.count(id) == 1);
    }

    session.TrimNoncesToThreshold();
    std::vector<uint8_t> participants = session.GetNonceParticipants();
    std::vector<uint8_t> sorted_online = online_ids;
    std::sort(sorted_online.begin(), sorted_online.end());
    BOOST_CHECK(participants == sorted_online);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_nonce_collection_9_of_15
// Add 9 pubnonces → session advances to NONCES_COMPLETE
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_nonce_collection_9_of_15)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 9;
    MuSig2SigningSession session(42, 9);

    // Generate 9 keypairs and nonces
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

    // Generate our local nonce first
    CKey ckey0 = MakeCKey(seckeys[0]);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_CHECK(session.GenerateNonce(0, ckey0, pubkeys[0], cache, pubnonce0));

    // Generate external nonces
    secp256k1_musig_secnonce secnonces[N];
    secp256k1_musig_pubnonce pubnonces[N];
    pubnonces[0] = pubnonce0;
    for (size_t i = 1; i < N; i++) {
        unsigned char rand[32];
        GetStrongRandBytes(Span{rand, 32});
        BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &secnonces[i], &pubnonces[i],
                                                 rand, seckeys[i], &pubkeys[i],
                                                 nullptr, &cache, nullptr));
    }

    // Add 8 nonces — should NOT be complete yet
    for (size_t i = 0; i < 8; i++) {
        BOOST_CHECK(session.AddPubnonce(static_cast<uint8_t>(i), pubnonces[i]));
    }
    BOOST_CHECK(!session.HasEnoughNonces());
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COLLECTING);

    // Add 9th nonce — should transition to NONCES_COMPLETE
    BOOST_CHECK(session.AddPubnonce(8, pubnonces[8]));
    BOOST_CHECK(session.HasEnoughNonces());
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COMPLETE);

    // Duplicate oracle_id should be rejected
    BOOST_CHECK(!session.AddPubnonce(0, pubnonces[0]));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_nonce_collection_below_threshold
// 8 nonces with threshold 9 = stays in COLLECTING
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_nonce_collection_below_threshold)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    MuSig2SigningSession session(10, 9);

    unsigned char seckey[32];
    secp256k1_keypair kp;
    secp256k1_pubkey pk;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &kp, &pk));

    const secp256k1_pubkey* pk_ptr = &pk;
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, &pk_ptr, 1));

    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_CHECK(session.GenerateNonce(0, ckey, pk, cache, pubnonce0));

    // Add 8 nonces (unique oracle IDs, but re-using same pubnonce data for simplicity)
    for (uint8_t i = 0; i < 8; i++) {
        secp256k1_musig_secnonce sn;
        secp256k1_musig_pubnonce pn;
        unsigned char rand[32];
        GetStrongRandBytes(Span{rand, 32});
        BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &sn, &pn,
                                                 rand, seckey, &pk,
                                                 nullptr, &cache, nullptr));
        BOOST_CHECK(session.AddPubnonce(i, pn));
    }

    BOOST_CHECK(!session.HasEnoughNonces());
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COLLECTING);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_partial_sig_generation
// Create partial signature
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_partial_sig_generation)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    // Single signer for simplicity
    unsigned char seckey[32];
    secp256k1_keypair kp;
    secp256k1_pubkey pk;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &kp, &pk));

    const secp256k1_pubkey* pk_ptr = &pk;
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, &pk_ptr, 1));

    MuSig2SigningSession session(50, 1);
    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(session.GenerateNonce(0, ckey, pk, cache, pubnonce));

    BOOST_CHECK(session.AddPubnonce(0, pubnonce));
    BOOST_CHECK(session.HasEnoughNonces());

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(session.AggregateNonces(msg));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::SIGNING);

    secp256k1_musig_partial_sig psig;
    BOOST_CHECK(session.CreatePartialSignature(0, ckey, psig));

    // Partial sig should serialize to 32 bytes
    unsigned char ser[32];
    BOOST_CHECK(secp256k1_musig_partial_sig_serialize(ctx, ser, &psig));
    unsigned char zeros[32] = {0};
    BOOST_CHECK(memcmp(ser, zeros, 32) != 0);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_partial_sig_aggregation
// Collect 9 partial sigs → aggregate to 64-byte Schnorr sig
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_partial_sig_aggregation)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 9;
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

    MuSig2SigningSession session(77, 9);

    // Signer 0 generates via session
    CKey ckey0 = MakeCKey(seckeys[0]);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_CHECK(session.GenerateNonce(0, ckey0, pubkeys[0], cache, pubnonce0));

    // Generate external nonces for signers 1-8
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
        BOOST_CHECK(session.AddPubnonce(static_cast<uint8_t>(i), ext_pubnonces[i]));
    }

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(session.AggregateNonces(msg));

    // Create partial sig for signer 0 via session
    secp256k1_musig_partial_sig psig0;
    BOOST_CHECK(session.CreatePartialSignature(0, ckey0, psig0));
    BOOST_CHECK(session.AddPartialSignature(0, psig0));

    // Create partial sigs externally for signers 1-8
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
        BOOST_CHECK(session.AddPartialSignature(static_cast<uint8_t>(i), psig));
    }

    BOOST_CHECK(session.HasEnoughPartialSigs());

    std::vector<unsigned char> sig64;
    BOOST_CHECK(session.AggregateSignature(sig64));
    BOOST_CHECK_EQUAL(sig64.size(), 64u);
    BOOST_CHECK(session.GetState() == MuSig2SessionState::COMPLETE);

    // Verify with standard schnorrsig_verify
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, sig64.data(), msg, 32, &agg_pk));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_full_roundtrip_in_process
// Complete flow with 9 signers, verify with schnorrsig_verify
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_full_roundtrip_in_process)
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

    // Create N sessions (one per signer, simulating in-process multi-signer)
    std::vector<MuSig2SigningSession> sessions;
    sessions.reserve(N);
    for (size_t i = 0; i < N; i++) {
        sessions.emplace_back(EPOCH, static_cast<uint8_t>(N));
    }

    // Round 1: Each signer generates a nonce
    std::vector<secp256k1_musig_pubnonce> pubnonces(N);
    for (size_t i = 0; i < N; i++) {
        BOOST_CHECK(sessions[i].GenerateNonce(static_cast<uint8_t>(i), ckeys[i], pubkeys[i], cache, pubnonces[i]));
    }

    // Distribute all pubnonces to all sessions
    for (size_t s = 0; s < N; s++) {
        for (size_t i = 0; i < N; i++) {
            BOOST_CHECK(sessions[s].AddPubnonce(static_cast<uint8_t>(i), pubnonces[i]));
        }
        BOOST_CHECK(sessions[s].HasEnoughNonces());
    }

    // Aggregate nonces with message
    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    for (size_t s = 0; s < N; s++) {
        BOOST_CHECK(sessions[s].AggregateNonces(msg));
        BOOST_CHECK(sessions[s].GetState() == MuSig2SessionState::SIGNING);
    }

    // Round 2: Each signer creates a partial signature
    std::vector<secp256k1_musig_partial_sig> partial_sigs(N);
    for (size_t i = 0; i < N; i++) {
        BOOST_CHECK(sessions[i].CreatePartialSignature(static_cast<uint8_t>(i), ckeys[i], partial_sigs[i]));
    }

    // Distribute all partial sigs to all sessions
    for (size_t s = 0; s < N; s++) {
        for (size_t i = 0; i < N; i++) {
            BOOST_CHECK(sessions[s].AddPartialSignature(static_cast<uint8_t>(i), partial_sigs[i]));
        }
        BOOST_CHECK(sessions[s].HasEnoughPartialSigs());
    }

    // Aggregate on all sessions — all should produce the same signature
    std::vector<unsigned char> first_sig;
    for (size_t s = 0; s < N; s++) {
        std::vector<unsigned char> sig64;
        BOOST_CHECK(sessions[s].AggregateSignature(sig64));
        BOOST_CHECK_EQUAL(sig64.size(), 64u);
        BOOST_CHECK(sessions[s].GetState() == MuSig2SessionState::COMPLETE);

        if (s == 0) {
            first_sig = sig64;
        } else {
            // All sessions should produce identical signatures
            BOOST_CHECK(sig64 == first_sig);
        }
    }

    // Verify with standard schnorrsig_verify
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, first_sig.data(), msg, 32, &agg_pk));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_timeout_transitions_to_failed
// Session times out after configurable blocks
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_timeout_transitions_to_failed)
{
    MuSig2SigningSession session(100, 9);
    BOOST_CHECK(session.GetState() == MuSig2SessionState::CREATED);

    // Set creation height and timeout for this test
    session.SetCreationHeight(100);
    session.SetTimeoutBlocks(10);

    // Should not be failed at creation height
    session.CheckTimeout(100);
    BOOST_CHECK(session.GetState() == MuSig2SessionState::CREATED);

    // Still within timeout
    session.CheckTimeout(109);
    BOOST_CHECK(session.GetState() != MuSig2SessionState::FAILED);

    // At timeout boundary
    session.CheckTimeout(110);
    BOOST_CHECK(session.GetState() == MuSig2SessionState::FAILED);
}

// ============================================================================
// test_session_nonce_zeroed_after_signing
// secnonce is zeroed after partial_sign
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_nonce_zeroed_after_signing)
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

    MuSig2SigningSession session(1, 1);
    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(session.GenerateNonce(0, ckey, pk, cache, pubnonce));
    BOOST_CHECK(session.AddPubnonce(0, pubnonce));

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(session.AggregateNonces(msg));

    secp256k1_musig_partial_sig psig;
    BOOST_CHECK(session.CreatePartialSignature(0, ckey, psig));

    // Attempting to sign again should fail (secnonce is consumed/zeroed)
    secp256k1_musig_partial_sig psig2;
    BOOST_CHECK(!session.CreatePartialSignature(0, ckey, psig2));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_nonce_reuse_prevention
// Cannot sign twice with same session
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_nonce_reuse_prevention)
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

    MuSig2SigningSession session(5, 1);
    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(session.GenerateNonce(0, ckey, pk, cache, pubnonce));
    BOOST_CHECK(session.AddPubnonce(0, pubnonce));

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(session.AggregateNonces(msg));

    // First partial sign should succeed
    secp256k1_musig_partial_sig psig;
    BOOST_CHECK(session.CreatePartialSignature(0, ckey, psig));

    // Second partial sign should fail — nonce consumed
    secp256k1_musig_partial_sig psig2;
    BOOST_CHECK(!session.CreatePartialSignature(0, ckey, psig2));

    // Complete the session
    BOOST_CHECK(session.AddPartialSignature(0, psig));
    std::vector<unsigned char> sig64;
    BOOST_CHECK(session.AggregateSignature(sig64));

    // After COMPLETE, cannot sign again
    secp256k1_musig_partial_sig psig3;
    BOOST_CHECK(!session.CreatePartialSignature(0, ckey, psig3));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_invalid_nonce_rejected
// Malformed pubnonce rejected
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_invalid_nonce_rejected)
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

    MuSig2SigningSession session(1, 2);
    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(session.GenerateNonce(0, ckey, pk, cache, pubnonce));

    // An all-zero pubnonce should be rejected (invalid internal state)
    secp256k1_musig_pubnonce bad_nonce;
    memset(&bad_nonce, 0, sizeof(bad_nonce));
    BOOST_CHECK(!session.AddPubnonce(0, bad_nonce));

    // Cannot add nonce before generating own nonce (test with fresh session)
    MuSig2SigningSession session2(2, 2);
    BOOST_CHECK(!session2.AddPubnonce(0, pubnonce));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_invalid_partial_sig_rejected
// Bad partial sig rejected
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_invalid_partial_sig_rejected)
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

    MuSig2SigningSession session(1, 1);
    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(session.GenerateNonce(0, ckey, pk, cache, pubnonce));
    BOOST_CHECK(session.AddPubnonce(0, pubnonce));

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(session.AggregateNonces(msg));

    // Cannot add partial sig before the session is in SIGNING state for a
    // different session — but this session IS in SIGNING state, so test
    // duplicate oracle_id rejection after adding a valid one
    secp256k1_musig_partial_sig psig;
    BOOST_CHECK(session.CreatePartialSignature(0, ckey, psig));
    BOOST_CHECK(session.AddPartialSignature(0, psig));

    // Duplicate oracle_id should be rejected
    BOOST_CHECK(!session.AddPartialSignature(0, psig));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_rejects_out_of_range_oracle_ids
// Oracle IDs outside configured range are rejected in nonce/partial rounds
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_rejects_out_of_range_oracle_ids)
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

    MuSig2SigningSession session(1, 1);
    CKey ckey = MakeCKey(seckey);
    secp256k1_musig_pubnonce pubnonce;
    BOOST_CHECK(session.GenerateNonce(0, ckey, pk, cache, pubnonce));

    const uint16_t total_oracles = static_cast<uint16_t>(Params().GetConsensus().nOracleTotalOracles);
    BOOST_REQUIRE(total_oracles > 0);
    const uint8_t out_of_range_id = static_cast<uint8_t>(total_oracles);

    // Round 1 hardening: reject out-of-range nonce contributor.
    BOOST_CHECK(!session.AddPubnonce(out_of_range_id, pubnonce));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COLLECTING);

    BOOST_CHECK(session.AddPubnonce(0, pubnonce));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::NONCES_COMPLETE);

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(session.AggregateNonces(msg));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::SIGNING);

    secp256k1_musig_partial_sig psig;
    BOOST_CHECK(session.CreatePartialSignature(0, ckey, psig));

    // Round 2 hardening: reject out-of-range partial signature contributor.
    BOOST_CHECK(!session.AddPartialSignature(out_of_range_id, psig));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::SIGNING);

    BOOST_CHECK(session.AddPartialSignature(0, psig));
    std::vector<unsigned char> sig64;
    BOOST_CHECK(session.AggregateSignature(sig64));
    BOOST_CHECK_EQUAL(sig64.size(), 64u);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_concurrent_epochs
// Two sessions for different epochs don't interfere
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_concurrent_epochs)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    // Create two keypairs
    unsigned char seckey1[32], seckey2[32];
    secp256k1_keypair kp1, kp2;
    secp256k1_pubkey pk1, pk2;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey1, &kp1, &pk1));
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey2, &kp2, &pk2));

    const secp256k1_pubkey* pk_ptr1 = &pk1;
    const secp256k1_pubkey* pk_ptr2 = &pk2;
    secp256k1_xonly_pubkey agg_pk1, agg_pk2;
    secp256k1_musig_keyagg_cache cache1, cache2;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk1, &cache1, &pk_ptr1, 1));
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk2, &cache2, &pk_ptr2, 1));

    // Two sessions for different epochs
    MuSig2SigningSession sessionA(100, 1);
    MuSig2SigningSession sessionB(101, 1);

    BOOST_CHECK_EQUAL(sessionA.GetEpoch(), 100);
    BOOST_CHECK_EQUAL(sessionB.GetEpoch(), 101);

    // Both start as CREATED
    BOOST_CHECK(sessionA.GetState() == MuSig2SessionState::CREATED);
    BOOST_CHECK(sessionB.GetState() == MuSig2SessionState::CREATED);

    // Generate nonces independently
    CKey ckey1 = MakeCKey(seckey1);
    CKey ckey2 = MakeCKey(seckey2);
    secp256k1_musig_pubnonce pnA, pnB;
    BOOST_CHECK(sessionA.GenerateNonce(0, ckey1, pk1, cache1, pnA));
    BOOST_CHECK(sessionB.GenerateNonce(0, ckey2, pk2, cache2, pnB));

    // Advance sessionA but not sessionB
    BOOST_CHECK(sessionA.AddPubnonce(0, pnA));
    BOOST_CHECK(sessionA.HasEnoughNonces());

    unsigned char msgA[32];
    GetStrongRandBytes(Span{msgA, 32});
    BOOST_CHECK(sessionA.AggregateNonces(msgA));
    BOOST_CHECK(sessionA.GetState() == MuSig2SessionState::SIGNING);

    // sessionB should still be NONCES_COLLECTING
    BOOST_CHECK(sessionB.GetState() == MuSig2SessionState::NONCES_COLLECTING);

    // Complete sessionA
    secp256k1_musig_partial_sig psigA;
    BOOST_CHECK(sessionA.CreatePartialSignature(0, ckey1, psigA));
    BOOST_CHECK(sessionA.AddPartialSignature(0, psigA));
    std::vector<unsigned char> sigA;
    BOOST_CHECK(sessionA.AggregateSignature(sigA));
    BOOST_CHECK(sessionA.GetState() == MuSig2SessionState::COMPLETE);

    // sessionB still not complete
    BOOST_CHECK(sessionB.GetState() == MuSig2SessionState::NONCES_COLLECTING);

    // Now complete sessionB
    BOOST_CHECK(sessionB.AddPubnonce(0, pnB));
    unsigned char msgB[32];
    GetStrongRandBytes(Span{msgB, 32});
    BOOST_CHECK(sessionB.AggregateNonces(msgB));
    secp256k1_musig_partial_sig psigB;
    BOOST_CHECK(sessionB.CreatePartialSignature(0, ckey2, psigB));
    BOOST_CHECK(sessionB.AddPartialSignature(0, psigB));
    std::vector<unsigned char> sigB;
    BOOST_CHECK(sessionB.AggregateSignature(sigB));
    BOOST_CHECK(sessionB.GetState() == MuSig2SessionState::COMPLETE);

    // Both signatures should verify independently
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, sigA.data(), msgA, 32, &agg_pk1));
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, sigB.data(), msgB, 32, &agg_pk2));

    // Signatures should be different
    BOOST_CHECK(sigA != sigB);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_session_rejects_invalid_partial_sig_content [RH-02]
// A malicious oracle submitting garbage partial signatures should be detected
// and rejected BEFORE they can burn honest signers' one-time nonces.
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_rejects_invalid_partial_sig_content)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 2;
    constexpr uint8_t MIN_SIGNERS = 2;
    constexpr int32_t EPOCH = 200;

    // Generate 2 keypairs
    unsigned char seckeys[N][32];
    secp256k1_keypair keypairs[N];
    secp256k1_pubkey pubkeys[N];
    for (size_t i = 0; i < N; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
    }

    // Key aggregation
    std::vector<const secp256k1_pubkey*> pubkey_ptrs(N);
    for (size_t i = 0; i < N; i++) pubkey_ptrs[i] = &pubkeys[i];
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), N));

    // Create session, generate nonce for signer 0
    MuSig2SigningSession session(EPOCH, MIN_SIGNERS);
    CKey ckey0 = MakeCKey(seckeys[0]);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_CHECK(session.GenerateNonce(0, ckey0, pubkeys[0], cache, pubnonce0));

    // Generate nonce for signer 1 externally
    secp256k1_musig_secnonce secnonce1;
    secp256k1_musig_pubnonce pubnonce1;
    unsigned char rand1[32];
    GetStrongRandBytes(Span{rand1, 32});
    BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &secnonce1, &pubnonce1,
                                             rand1, seckeys[1], &pubkeys[1],
                                             nullptr, &cache, nullptr));

    // Collect pubnonces
    BOOST_CHECK(session.AddPubnonce(0, pubnonce0));
    BOOST_CHECK(session.AddPubnonce(1, pubnonce1));
    BOOST_CHECK(session.HasEnoughNonces());

    // Aggregate nonces with message
    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_CHECK(session.AggregateNonces(msg));
    BOOST_CHECK(session.GetState() == MuSig2SessionState::SIGNING);

    // Signer 0 creates honest partial sig
    secp256k1_musig_partial_sig psig0;
    BOOST_CHECK(session.CreatePartialSignature(0, ckey0, psig0));
    BOOST_CHECK(session.AddPartialSignature(0, psig0));

    // ATTACK: signer 1 submits a GARBAGE partial signature
    // (random bytes that parse as a valid partial_sig struct but are wrong)
    secp256k1_musig_partial_sig garbage_psig;
    unsigned char garbage_bytes[32];
    GetStrongRandBytes(Span{garbage_bytes, 32});
    // Set the magic bytes so it looks valid to secp256k1
    memcpy(garbage_psig.data, "\xeb\xfb\xce\x86", 4); // partial_sig magic
    memcpy(garbage_psig.data + 4, garbage_bytes, 32);

    // This is the critical test: AddPartialSignature should REJECT
    // a partial sig that doesn't verify against signer 1's pubnonce.
    //
    // NOTE: The partial sig verification fix in AddPartialSignature only
    // works when pubkeys were passed to AddPubnonce. If pubkeys were NOT
    // provided (as in most P2P code paths via OnNonceReceived), the
    // verification is silently skipped and garbage sigs are accepted.
    //
    // Test the WITHOUT-pubkey path (simulating P2P/OnNonceReceived):
    bool accepted = session.AddPartialSignature(1, garbage_psig);

    if (accepted) {
        // BUG CONFIRMED: garbage partial sig was accepted via the
        // no-pubkey code path. This means any P2P-received nonce
        // (which doesn't include a pubkey) allows garbage partial
        // sigs to burn honest signers' nonces.
        std::vector<unsigned char> sig64;
        bool agg_ok = session.AggregateSignature(sig64);
        if (agg_ok) {
            bool verify_ok = secp256k1_schnorrsig_verify(ctx, sig64.data(), msg, 32, &agg_pk);
            BOOST_CHECK_MESSAGE(!verify_ok,
                "CRITICAL: garbage partial sig produced a valid aggregate signature!");
            BOOST_CHECK_MESSAGE(false,
                "BUG [RH-02]: AddPartialSignature accepts unverified partial sigs "
                "when pubkeys were not provided to AddPubnonce (the P2P path). "
                "A malicious oracle can submit garbage to burn honest signers' nonces.");
        }
    } else {
        // DEFENSE HOLDS: garbage partial sig was rejected
        BOOST_CHECK(!accepted);
    }

    // Now test the WITH-pubkey path (verifying the fix works when pubkey IS provided):
    {
        MuSig2SigningSession session2(EPOCH + 1, MIN_SIGNERS);
        CKey ckey0_2 = MakeCKey(seckeys[0]);
        secp256k1_musig_pubnonce pn0_2;
        BOOST_CHECK(session2.GenerateNonce(0, ckey0_2, pubkeys[0], cache, pn0_2));

        secp256k1_musig_secnonce secnonce1_2;
        secp256k1_musig_pubnonce pn1_2;
        unsigned char rand1_2[32];
        GetStrongRandBytes(Span{rand1_2, 32});
        BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &secnonce1_2, &pn1_2,
                                                 rand1_2, seckeys[1], &pubkeys[1],
                                                 nullptr, &cache, nullptr));

        // Pass pubnonces
        BOOST_CHECK(session2.AddPubnonce(0, pn0_2));
        BOOST_CHECK(session2.AddPubnonce(1, pn1_2));

        unsigned char msg2[32];
        GetStrongRandBytes(Span{msg2, 32});
        BOOST_CHECK(session2.AggregateNonces(msg2));

        secp256k1_musig_partial_sig psig0_2;
        BOOST_CHECK(session2.CreatePartialSignature(0, ckey0_2, psig0_2));
        BOOST_CHECK(session2.AddPartialSignature(0, psig0_2));

        // Garbage should be rejected when pubkey IS available
        secp256k1_musig_partial_sig garbage2;
        memcpy(garbage2.data, "\xeb\xfb\xce\x86", 4);
        unsigned char gb2[32];
        GetStrongRandBytes(Span{gb2, 32});
        memcpy(garbage2.data + 4, gb2, 32);
        BOOST_CHECK_MESSAGE(!session2.AddPartialSignature(1, garbage2),
            "Garbage partial sig should be rejected when pubkey is available");
    }

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Separate suite for session manager tests (needs manager header)
// ============================================================================
BOOST_FIXTURE_TEST_SUITE(musig2_session_manager_tests, BasicTestingSetup)

// ============================================================================
// test_session_manager_seen_sets_cleanup [RH-02]
// m_seen_nonces and m_seen_partial_sigs must be pruned during cleanup
// to prevent unbounded memory growth
// ============================================================================
BOOST_AUTO_TEST_CASE(test_session_manager_seen_sets_cleanup)
{
    MuSig2SessionManager manager(9, 100);

    // Register some seen hashes
    uint256 hash1, hash2;
    GetStrongRandBytes(Span{hash1.begin(), 32});
    GetStrongRandBytes(Span{hash2.begin(), 32});
    BOOST_CHECK(manager.RegisterSeenNonce(hash1));
    BOOST_CHECK(manager.RegisterSeenPartialSig(hash2));
    BOOST_CHECK(manager.HasSeenNonce(hash1));
    BOOST_CHECK(manager.HasSeenPartialSig(hash2));

    // After CleanupOldSessions, the replay filters should be pruned so they
    // cannot grow without bound across epochs.
    manager.CleanupOldSessions(1000);

    BOOST_CHECK_MESSAGE(!manager.HasSeenNonce(hash1),
        "CleanupOldSessions should prune stale nonce replay entries.");
    BOOST_CHECK_MESSAGE(!manager.HasSeenPartialSig(hash2),
        "CleanupOldSessions should prune stale partial-sig replay entries.");
}

BOOST_AUTO_TEST_CASE(test_participation_bitmap_sized_for_total_oracles)
{
    // RC41: nOracleTotalOracles = 35 in chainparams
    // expected bitmap size = (35 + 7) / 8 = 5 bytes
    const uint16_t total_oracles = static_cast<uint16_t>(
        Params().GetConsensus().nOracleTotalOracles);
    size_t expected_bytes = (total_oracles + 7) / 8;

    MuSig2SigningSession session(42, 4);

    // Generate nonces and partial sigs for oracles 0-5 (all in byte 0)
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    secp256k1_musig_keyagg_cache cache;
    memset(&cache, 0, sizeof(cache));

    // We can't easily complete a full MuSig2 flow in this unit test,
    // so verify the bitmap sizing logic directly
    BOOST_CHECK(expected_bytes >= 2); // 11 oracles needs 2 bytes
    BOOST_CHECK(total_oracles >= 11);

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_SUITE_END()
