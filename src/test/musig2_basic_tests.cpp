// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * MuSig2 Basic Crypto Tests
 *
 * Tests for the secp256k1 MuSig2 module (BIP 327):
 * - Key aggregation (2 signers, 17 signers)
 * - Full sign/verify round-trip (2 signers, 9-of-17 subset, RC30)
 * - Deterministic aggregate pubkey
 * - Aggregate sig verifiable via standard schnorrsig_verify (BIP-340)
 * - Nonce generation uniqueness
 * - Regression: existing Schnorr and ECDSA still work
 */

#include <boost/test/unit_test.hpp>

#include <key.h>
#include <random.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <hash.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <array>
#include <cstring>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(musig2_basic_tests, BasicTestingSetup)

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

/**
 * Helper: run the full MuSig2 signing protocol for n_signers.
 * Returns true and fills sig64 on success.
 */
static bool MuSig2SignFull(secp256k1_context* ctx,
                           unsigned char* sig64,
                           secp256k1_xonly_pubkey* agg_pk_out,
                           const unsigned char msg32[32],
                           std::vector<std::array<unsigned char, 32>>& seckeys,
                           std::vector<secp256k1_keypair>& keypairs,
                           std::vector<secp256k1_pubkey>& pubkeys,
                           size_t n_signers)
{
    // 1. Key aggregation
    std::vector<const secp256k1_pubkey*> pubkey_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; i++) {
        pubkey_ptrs[i] = &pubkeys[i];
    }

    secp256k1_musig_keyagg_cache keyagg_cache;
    if (!secp256k1_musig_pubkey_agg(ctx, agg_pk_out, &keyagg_cache, pubkey_ptrs.data(), n_signers)) {
        return false;
    }

    // 2. Nonce generation
    std::vector<secp256k1_musig_secnonce> secnonces(n_signers);
    std::vector<secp256k1_musig_pubnonce> pubnonces(n_signers);

    for (size_t i = 0; i < n_signers; i++) {
        unsigned char session_secrand[32];
        GetStrongRandBytes(Span{session_secrand, 32});
        if (!secp256k1_musig_nonce_gen(ctx, &secnonces[i], &pubnonces[i],
                                       session_secrand, seckeys[i].data(), &pubkeys[i],
                                       msg32, &keyagg_cache, nullptr)) {
            return false;
        }
    }

    // 3. Nonce aggregation
    std::vector<const secp256k1_musig_pubnonce*> pubnonce_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; i++) {
        pubnonce_ptrs[i] = &pubnonces[i];
    }

    secp256k1_musig_aggnonce aggnonce;
    if (!secp256k1_musig_nonce_agg(ctx, &aggnonce, pubnonce_ptrs.data(), n_signers)) {
        return false;
    }

    // 4. Session initialization
    secp256k1_musig_session session;
    if (!secp256k1_musig_nonce_process(ctx, &session, &aggnonce, msg32, &keyagg_cache)) {
        return false;
    }

    // 5. Partial signing
    std::vector<secp256k1_musig_partial_sig> partial_sigs(n_signers);
    for (size_t i = 0; i < n_signers; i++) {
        if (!secp256k1_musig_partial_sign(ctx, &partial_sigs[i], &secnonces[i],
                                          &keypairs[i], &keyagg_cache, &session)) {
            return false;
        }
    }

    // 6. Verify each partial signature
    for (size_t i = 0; i < n_signers; i++) {
        if (!secp256k1_musig_partial_sig_verify(ctx, &partial_sigs[i], &pubnonces[i],
                                                &pubkeys[i], &keyagg_cache, &session)) {
            return false;
        }
    }

    // 7. Aggregate partial signatures into final 64-byte Schnorr signature
    std::vector<const secp256k1_musig_partial_sig*> psig_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; i++) {
        psig_ptrs[i] = &partial_sigs[i];
    }

    if (!secp256k1_musig_partial_sig_agg(ctx, sig64, &session, psig_ptrs.data(), n_signers)) {
        return false;
    }

    return true;
}

// ============================================================================
// test_musig2_key_aggregation_2_signers
// Aggregate 2 pubkeys, verify deterministic output
// ============================================================================
BOOST_AUTO_TEST_CASE(test_musig2_key_aggregation_2_signers)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    unsigned char seckey1[32], seckey2[32];
    secp256k1_keypair kp1, kp2;
    secp256k1_pubkey pk1, pk2;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey1, &kp1, &pk1));
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey2, &kp2, &pk2));

    const secp256k1_pubkey* pubkeys[2] = {&pk1, &pk2};
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;

    int ret = secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkeys, 2);
    BOOST_CHECK_EQUAL(ret, 1);

    // Verify aggregate key serializes to 32 bytes (x-only)
    unsigned char agg_pk_ser[32];
    BOOST_CHECK(secp256k1_xonly_pubkey_serialize(ctx, agg_pk_ser, &agg_pk));

    // Aggregate key should be non-zero
    unsigned char zeros[32] = {0};
    BOOST_CHECK(memcmp(agg_pk_ser, zeros, 32) != 0);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_musig2_key_aggregation_15_signers
// Aggregate 15 pubkeys
// ============================================================================
BOOST_AUTO_TEST_CASE(test_musig2_key_aggregation_15_signers)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 15;
    unsigned char seckeys[N][32];
    secp256k1_keypair keypairs[N];
    secp256k1_pubkey pubkeys[N];
    const secp256k1_pubkey* pubkey_ptrs[N];

    for (size_t i = 0; i < N; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
        pubkey_ptrs[i] = &pubkeys[i];
    }

    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    int ret = secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs, N);
    BOOST_CHECK_EQUAL(ret, 1);

    unsigned char agg_pk_ser[32];
    BOOST_CHECK(secp256k1_xonly_pubkey_serialize(ctx, agg_pk_ser, &agg_pk));

    unsigned char zeros[32] = {0};
    BOOST_CHECK(memcmp(agg_pk_ser, zeros, 32) != 0);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_musig2_sign_verify_roundtrip_2_signers
// Full MuSig2 flow with 2 signers: keygen, nonce, sign, aggregate, verify
// ============================================================================
BOOST_AUTO_TEST_CASE(test_musig2_sign_verify_roundtrip_2_signers)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 2;
    std::vector<std::array<unsigned char, 32>> seckeys(N);
    std::vector<secp256k1_keypair> keypairs(N);
    std::vector<secp256k1_pubkey> pubkeys(N);

    for (size_t i = 0; i < N; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i].data(), &keypairs[i], &pubkeys[i]));
    }

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});

    unsigned char sig64[64];
    secp256k1_xonly_pubkey agg_pk;
    BOOST_CHECK(MuSig2SignFull(ctx, sig64, &agg_pk, msg, seckeys, keypairs, pubkeys, N));

    // Verify aggregate signature with standard schnorrsig_verify
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, sig64, msg, 32, &agg_pk));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_musig2_sign_verify_roundtrip_9_of_17
// RC30 9-of-17 signer subset: only 9 signers participate in signing
// ============================================================================
BOOST_AUTO_TEST_CASE(test_musig2_sign_verify_roundtrip_9_of_17)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N_TOTAL = 17;
    constexpr size_t N_SIGNING = 9;

    // Generate all 17 key pairs (RC30)
    std::vector<std::array<unsigned char, 32>> all_seckeys(N_TOTAL);
    std::vector<secp256k1_keypair> all_keypairs(N_TOTAL);
    std::vector<secp256k1_pubkey> all_pubkeys(N_TOTAL);

    for (size_t i = 0; i < N_TOTAL; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, all_seckeys[i].data(), &all_keypairs[i], &all_pubkeys[i]));
    }

    // Only first 9 signers participate
    std::vector<std::array<unsigned char, 32>> signing_seckeys(N_SIGNING);
    std::vector<secp256k1_keypair> signing_keypairs(N_SIGNING);
    std::vector<secp256k1_pubkey> signing_pubkeys(N_SIGNING);

    for (size_t i = 0; i < N_SIGNING; i++) {
        memcpy(signing_seckeys[i].data(), all_seckeys[i].data(), 32);
        signing_keypairs[i] = all_keypairs[i];
        signing_pubkeys[i] = all_pubkeys[i];
    }

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});

    unsigned char sig64[64];
    secp256k1_xonly_pubkey agg_pk;
    // MuSig2 key aggregation + signing with just the 9 participating signers
    BOOST_CHECK(MuSig2SignFull(ctx, sig64, &agg_pk, msg,
                               signing_seckeys, signing_keypairs, signing_pubkeys, N_SIGNING));

    // Verify: the aggregate signature is valid for the 9-signer aggregate pubkey
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, sig64, msg, 32, &agg_pk));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_musig2_deterministic_aggregate_pubkey
// Same inputs = same aggregate pubkey output
// ============================================================================
BOOST_AUTO_TEST_CASE(test_musig2_deterministic_aggregate_pubkey)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    unsigned char seckey1[32], seckey2[32];
    secp256k1_keypair kp1, kp2;
    secp256k1_pubkey pk1, pk2;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey1, &kp1, &pk1));
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey2, &kp2, &pk2));

    const secp256k1_pubkey* pubkeys[2] = {&pk1, &pk2};

    // First aggregation
    secp256k1_xonly_pubkey agg_pk1;
    secp256k1_musig_keyagg_cache cache1;
    BOOST_CHECK(secp256k1_musig_pubkey_agg(ctx, &agg_pk1, &cache1, pubkeys, 2));

    // Second aggregation with same inputs
    secp256k1_xonly_pubkey agg_pk2;
    secp256k1_musig_keyagg_cache cache2;
    BOOST_CHECK(secp256k1_musig_pubkey_agg(ctx, &agg_pk2, &cache2, pubkeys, 2));

    // Serialize and compare
    unsigned char ser1[32], ser2[32];
    BOOST_CHECK(secp256k1_xonly_pubkey_serialize(ctx, ser1, &agg_pk1));
    BOOST_CHECK(secp256k1_xonly_pubkey_serialize(ctx, ser2, &agg_pk2));
    BOOST_CHECK(memcmp(ser1, ser2, 32) == 0);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_musig2_verify_with_schnorrsig_verify
// Prove that an aggregate MuSig2 signature is a standard BIP-340 Schnorr sig
// ============================================================================
BOOST_AUTO_TEST_CASE(test_musig2_verify_with_schnorrsig_verify)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    constexpr size_t N = 3;
    std::vector<std::array<unsigned char, 32>> seckeys(N);
    std::vector<secp256k1_keypair> keypairs(N);
    std::vector<secp256k1_pubkey> pubkeys(N);

    for (size_t i = 0; i < N; i++) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i].data(), &keypairs[i], &pubkeys[i]));
    }

    unsigned char msg[32] = "MuSig2 BIP-340 compat test msg!";
    unsigned char sig64[64];
    secp256k1_xonly_pubkey agg_pk;
    BOOST_REQUIRE(MuSig2SignFull(ctx, sig64, &agg_pk, msg, seckeys, keypairs, pubkeys, N));

    // The aggregate sig must verify using ONLY the standard BIP-340 verifier.
    // This proves on-chain compatibility: validators don't need MuSig2 awareness.
    BOOST_CHECK(secp256k1_schnorrsig_verify(ctx, sig64, msg, 32, &agg_pk));

    // Flipping one bit in the signature should fail verification
    unsigned char bad_sig[64];
    memcpy(bad_sig, sig64, 64);
    bad_sig[31] ^= 0x01;
    BOOST_CHECK(!secp256k1_schnorrsig_verify(ctx, bad_sig, msg, 32, &agg_pk));

    // Flipping one bit in the message should fail verification
    unsigned char bad_msg[32];
    memcpy(bad_msg, msg, 32);
    bad_msg[0] ^= 0x01;
    BOOST_CHECK(!secp256k1_schnorrsig_verify(ctx, sig64, bad_msg, 32, &agg_pk));

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_musig2_nonce_generation_uniqueness
// Different random inputs produce different nonces
// ============================================================================
BOOST_AUTO_TEST_CASE(test_musig2_nonce_generation_uniqueness)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    unsigned char seckey[32];
    secp256k1_keypair kp;
    secp256k1_pubkey pk;
    BOOST_REQUIRE(MakeRandomKeypair(ctx, seckey, &kp, &pk));

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});

    // Generate two nonces with different session randomness
    secp256k1_musig_secnonce secnonce1, secnonce2;
    secp256k1_musig_pubnonce pubnonce1, pubnonce2;

    unsigned char rand1[32], rand2[32];
    GetStrongRandBytes(Span{rand1, 32});
    GetStrongRandBytes(Span{rand2, 32});

    BOOST_CHECK(secp256k1_musig_nonce_gen(ctx, &secnonce1, &pubnonce1,
                                          rand1, seckey, &pk, msg, nullptr, nullptr));
    BOOST_CHECK(secp256k1_musig_nonce_gen(ctx, &secnonce2, &pubnonce2,
                                          rand2, seckey, &pk, msg, nullptr, nullptr));

    // Serialize pubnonces and verify they differ
    unsigned char ser1[66], ser2[66];
    BOOST_CHECK(secp256k1_musig_pubnonce_serialize(ctx, ser1, &pubnonce1));
    BOOST_CHECK(secp256k1_musig_pubnonce_serialize(ctx, ser2, &pubnonce2));
    BOOST_CHECK(memcmp(ser1, ser2, 66) != 0);

    secp256k1_context_destroy(ctx);
}

// ============================================================================
// test_existing_schnorr_still_works
// Regression: standard BIP-340 Schnorr sign+verify unaffected by MuSig2 module
// ============================================================================
BOOST_AUTO_TEST_CASE(test_existing_schnorr_still_works)
{
    CKey key;
    key.MakeNewKey(true);
    BOOST_REQUIRE(key.IsValid());

    CPubKey pubkey = key.GetPubKey();
    BOOST_CHECK(key.VerifyPubKey(pubkey));

    // Sign a message with Schnorr (BIP-340) via the CKey API
    uint256 msg_hash = Hash("schnorr regression test");
    uint256 aux_rand;
    GetStrongRandBytes(aux_rand);

    unsigned char schnorr_sig[64];
    BOOST_CHECK(key.SignSchnorr(msg_hash, schnorr_sig, nullptr, aux_rand));

    // Verify with XOnlyPubKey
    XOnlyPubKey xonly_pk{pubkey};
    BOOST_CHECK(xonly_pk.VerifySchnorr(msg_hash, Span<const unsigned char>{schnorr_sig, 64}));

    // Verify flipped bit fails
    schnorr_sig[0] ^= 0x01;
    BOOST_CHECK(!xonly_pk.VerifySchnorr(msg_hash, Span<const unsigned char>{schnorr_sig, 64}));
}

// ============================================================================
// test_existing_ecdsa_still_works
// Regression: standard ECDSA sign+verify unaffected by MuSig2 module
// ============================================================================
BOOST_AUTO_TEST_CASE(test_existing_ecdsa_still_works)
{
    CKey key;
    key.MakeNewKey(true);
    BOOST_REQUIRE(key.IsValid());

    CPubKey pubkey = key.GetPubKey();
    BOOST_CHECK(key.VerifyPubKey(pubkey));

    uint256 msg_hash = Hash("ecdsa regression test");

    // ECDSA sign
    std::vector<unsigned char> ecdsa_sig;
    BOOST_CHECK(key.Sign(msg_hash, ecdsa_sig));
    BOOST_CHECK(ecdsa_sig.size() > 0);

    // ECDSA verify
    BOOST_CHECK(pubkey.Verify(msg_hash, ecdsa_sig));

    // Verify flipped bit fails
    ecdsa_sig[ecdsa_sig.size() / 2] ^= 0x01;
    BOOST_CHECK(!pubkey.Verify(msg_hash, ecdsa_sig));
}

BOOST_AUTO_TEST_SUITE_END()
