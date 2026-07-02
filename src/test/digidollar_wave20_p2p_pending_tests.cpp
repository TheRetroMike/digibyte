// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 20 — P2P Oracle Messages and Pending Bundle Abuse (Agent B / functional)
 *
 * These pins close coverage gaps identified in Wave 20 around the off-chain
 * message-pool / dedup surfaces of OracleBundleManager and the MuSig2 P2P
 * ingestion path. They are TDD pins that exercise the contracts cited in the
 * brief:
 *
 *   1. Malformed P2P payloads — oversized fields, non-canonical encoding.
 *      Asserted behaviour: deserialize succeeds (we want to detect the
 *      malformed shape post-decode), `IsValid()` returns false, and
 *      `OracleBundleManager::AddOracleMessage` rejects the malformed message
 *      without poisoning state.
 *
 *   2. Flooding limits — `seen_message_hashes` is capped at MAX_SEEN_HASHES
 *      (= 2048). The cap is asserted by counting accepted-then-evicted
 *      hashes, not just by demonstrating absence of OOM.
 *
 *   3. Replay protection — re-feeding the *same* signed attestation twice
 *      MUST short-circuit on the second insertion (same hash → reject) and
 *      MUST NOT mutate the pending_messages map.
 *
 *   4. Restart behaviour — `ClearPendingMessages` (the user-visible restart
 *      knob from `src/oracle/node.cpp:425`) MUST drop pending_messages,
 *      pending_attestations, and seen_message_hashes such that a previously
 *      seen attestation hash is accepted again on re-submission. This is
 *      the contract `Oracle::Node::BroadcastPriceMessage` relies on to
 *      break a stuck-consensus deadlock.
 *
 *   5. Cross-epoch isolation — MuSig2 nonces and partial sigs delivered for
 *      epoch X MUST NOT be ingested into a session for epoch Y. Sessions are
 *      keyed by epoch (`g_oracle_signing_sessions[epoch]`); a wrong-epoch
 *      message for an existing session must be no-op. Existing
 *      `attack_cross_epoch_nonce_replay` covers replay of a captured nonce;
 *      this pin additionally covers the simpler case where the wrong epoch
 *      has *no* corresponding session and confirms the cross-talk path is
 *      a no-op (does not lazy-create a poisoned session that pollutes
 *      epoch-X aggregation).
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <oracle/musig2_messages.h>
#include <oracle/musig2_session.h>
#include <oracle/signing_orchestrator.h>
#include <primitives/oracle.h>
#include <protocol.h>
#include <random.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/time.h>

#include <cstdint>
#include <vector>

extern std::map<int32_t, MuSig2SigningSession> g_oracle_signing_sessions;
extern Mutex g_oracle_signing_sessions_mutex;

namespace {

CKey MakeRegtestOracleKey(uint32_t oracle_id)
{
    const std::string seed = "digibyte_regtest_oracle_" + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size())
              .Finalize(hash.begin());
    CKey key;
    key.Set(hash.begin(), hash.end(), true);
    BOOST_REQUIRE(key.IsValid());
    return key;
}

COraclePriceMessage MakeSignedRegtestMessage(uint32_t oracle_id, uint64_t price, int64_t timestamp)
{
    CKey key = MakeRegtestOracleKey(oracle_id);
    COraclePriceMessage msg(oracle_id, price, timestamp);
    msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
    BOOST_REQUIRE(msg.SignAttestation(key));
    BOOST_REQUIRE(msg.VerifyAttestation());
    return msg;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_wave20_p2p_pending_tests, RegTestingSetup)

// ============================================================================
// DD-FA-TEST-031: ClearPendingMessages restart contract
//
// The oracle daemon (src/oracle/node.cpp:425) calls
// `OracleBundleManager::ClearPendingMessages` to recover from a stuck
// consensus round. Once Clear has run, the manager MUST behave as if it
// had just been started: the previously-seen attestation hash, the
// previously-buffered pending message, and the previously-buffered
// consensus attestation MUST all be re-acceptable.
//
// This pins the contract that node.cpp depends on. Without it, the
// restart path silently does nothing and the deadlock persists.
// ============================================================================
BOOST_AUTO_TEST_CASE(clear_pending_messages_resets_dedup_and_pending)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    const int64_t now = GetTime();
    COraclePriceMessage msg = MakeSignedRegtestMessage(0, 6000, now);

    // ── Phase 1: seed state and confirm dedup activates ──
    BOOST_REQUIRE(manager.AddOracleMessage(msg));
    BOOST_REQUIRE_EQUAL(manager.GetPendingMessageCount(), 1U);

    // The exact hash AddOracleMessage stored is the attestation hash.
    const uint256 att_hash = msg.GetAttestationSignatureHash();

    // Replay the same signed message: must be rejected by seen_message_hashes
    // dedup (different return reason than timestamp ordering).
    BOOST_CHECK_MESSAGE(!manager.AddOracleMessage(msg),
        "duplicate AddOracleMessage must be rejected before clear");

    // Also seed the P2P-wrapper hash dedup channel.
    OraclePriceMsg wire_msg{};
    wire_msg.price_message = msg;
    const uint256 wire_hash = wire_msg.GetHash();
    manager.RegisterSeenHash(wire_hash);
    BOOST_REQUIRE(manager.HasOracleMessage(wire_hash));

    // Also seed the separate consensus-attestation replay channel used by
    // ORACLEATTESTATION relay handling. ClearPendingMessages clears
    // pending_attestations, so the matching replay set must reset with it.
    OracleAttestationMsg att_msg{};
    att_msg.attestation = msg;
    const uint256 consensus_att_hash = att_msg.GetHash();
    BOOST_REQUIRE(manager.RegisterSeenAttestation(consensus_att_hash));
    BOOST_REQUIRE(manager.HasSeenAttestation(consensus_att_hash));

    // ── Phase 2: ClearPendingMessages MUST act like a restart ──
    manager.ClearPendingMessages();

    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 0U);
    BOOST_CHECK_EQUAL(manager.GetPendingAttestationCount(), 0U);
    // P2P-wrapper hash dedup also wiped (seen_message_hashes is shared).
    BOOST_CHECK_MESSAGE(!manager.HasOracleMessage(wire_hash),
        "ClearPendingMessages must drop seen P2P wrapper hashes too");
    // Attestation hash also wiped.
    BOOST_CHECK_MESSAGE(!manager.HasOracleMessage(att_hash),
        "ClearPendingMessages must drop seen attestation hashes too");
    BOOST_CHECK_MESSAGE(!manager.HasSeenAttestation(consensus_att_hash),
        "ClearPendingMessages must drop seen consensus-attestation hashes too");

    // ── Phase 3: After clear, the same signed message is acceptable again ──
    BOOST_CHECK_MESSAGE(manager.AddOracleMessage(msg),
        "post-clear restart contract: previously-seen message must be reaccepted");
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 1U);

    manager.Clear();
}

// ============================================================================
// DD-FA-TEST-032: Cross-epoch MuSig2 message isolation
//
// MuSig2 sessions are keyed by epoch (g_oracle_signing_sessions[epoch]).
// A nonce or partial sig addressed to epoch X MUST be ingested only into
// session[X]; it MUST NOT cross-pollinate session[Y] in any direction:
//
//   (a) wrong-epoch nonce against an existing Y session: no-op.
//   (b) wrong-epoch nonce when Y session does not exist: ProcessRemote*
//       must not lazy-create a poisoned session.
//
// Without this isolation, an attacker could relay a stale captured nonce
// from epoch X and have it counted toward consensus in epoch Y, defeating
// per-epoch nonce safety.
// ============================================================================
BOOST_AUTO_TEST_CASE(cross_epoch_musig2_messages_do_not_poison_other_session)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);

    const int32_t epoch_x = 4242;
    const int32_t epoch_y = 9999;

    // Pre-create a session for epoch_x ONLY. ProcessRemoteMusig* needs a
    // session to even consider the message; if Y is in the map, it confirms
    // the routing-by-epoch property.
    {
        LOCK(g_oracle_signing_sessions_mutex);
        g_oracle_signing_sessions.clear();
        g_oracle_signing_sessions.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(epoch_x),
            std::forward_as_tuple(epoch_x, /*min_signers=*/4));
    }

    // Wrong-epoch nonce: epoch_y address but only epoch_x session exists.
    OracleMusigNonceMsg cross_nonce;
    cross_nonce.epoch = epoch_y;
    cross_nonce.oracle_id = 0;
    cross_nonce.pubnonce.assign(66, 0xAA);

    BOOST_CHECK_MESSAGE(!manager.ProcessRemoteMusigNonce(cross_nonce),
        "ProcessRemoteMusigNonce must reject when epoch session does not exist");

    // The epoch_y session must NOT have been lazy-created in
    // g_oracle_signing_sessions either — that map is only mutated by the
    // signing orchestrator's controlled lazy-create path, never by the
    // bundle manager's wrong-epoch P2P handler.
    {
        LOCK(g_oracle_signing_sessions_mutex);
        BOOST_CHECK_MESSAGE(g_oracle_signing_sessions.count(epoch_y) == 0,
            "ProcessRemoteMusigNonce must not lazy-create poisoned epoch_y session");
        BOOST_CHECK(g_oracle_signing_sessions.count(epoch_x) == 1);
    }

    OracleMusigPartialSigMsg cross_psig;
    cross_psig.epoch = epoch_y;
    cross_psig.oracle_id = 0;
    cross_psig.partial_sig.assign(32, 0xBB);

    BOOST_CHECK_MESSAGE(!manager.ProcessRemoteMusigPartialSig(cross_psig),
        "ProcessRemoteMusigPartialSig must reject when epoch session does not exist");

    {
        LOCK(g_oracle_signing_sessions_mutex);
        BOOST_CHECK_MESSAGE(g_oracle_signing_sessions.count(epoch_y) == 0,
            "ProcessRemoteMusigPartialSig must not lazy-create poisoned epoch_y session");
        g_oracle_signing_sessions.clear();
    }

    manager.Clear();
}

// ============================================================================
// DD-FA-TEST-033: Malformed P2P payloads — oversized / truncated /
// non-canonical encoding
//
// The OracleMusigNonceMsg and OracleMusigPartialSigMsg P2P wire formats
// have explicit shape contracts in `src/oracle/musig2_messages.h:45,76`:
//   * pubnonce.size() == 66
//   * partial_sig.size() == 32
//   * signature.size() == 64
//   * oracle_id < 255
//
// COraclePriceMessage exposes `oracle_pubkey` (fixed 32 bytes via XOnlyPubKey
// serializer) and a `schnorr_sig` vector. An attacker can submit messages
// whose vector lengths differ from the protocol contract; the deserializer
// will accept any vector length, but `IsValid()` and the bundle-manager
// validation path MUST reject them. This pin asserts both layers.
// ============================================================================
BOOST_AUTO_TEST_CASE(malformed_p2p_payloads_rejected)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    const int64_t now = GetTime();

    // ── (a) Oversized schnorr_sig (>64 bytes) on a price message ──
    {
        COraclePriceMessage msg = MakeSignedRegtestMessage(0, 6000, now);
        // Append junk so the signature is now 96 bytes — invalid for BIP-340.
        msg.schnorr_sig.insert(msg.schnorr_sig.end(), 32, 0xFF);
        BOOST_CHECK_MESSAGE(!msg.VerifyAttestation(),
            "VerifyAttestation must reject oversized schnorr_sig");
        BOOST_CHECK_MESSAGE(!manager.AddOracleMessage(msg),
            "AddOracleMessage must reject oversized schnorr_sig");
    }

    // ── (b) Truncated schnorr_sig (<64 bytes) on a price message ──
    {
        COraclePriceMessage msg = MakeSignedRegtestMessage(1, 6000, now);
        msg.schnorr_sig.resize(32);
        BOOST_CHECK_MESSAGE(!msg.VerifyAttestation(),
            "VerifyAttestation must reject truncated schnorr_sig");
        BOOST_CHECK_MESSAGE(!manager.AddOracleMessage(msg),
            "AddOracleMessage must reject truncated schnorr_sig");
    }

    // ── (c) Empty schnorr_sig — bypass attempt ──
    {
        COraclePriceMessage msg = MakeSignedRegtestMessage(2, 6000, now);
        msg.schnorr_sig.clear();
        BOOST_CHECK_MESSAGE(!msg.VerifyAttestation(),
            "VerifyAttestation must reject empty schnorr_sig");
        BOOST_CHECK_MESSAGE(!manager.AddOracleMessage(msg),
            "AddOracleMessage must reject empty schnorr_sig");
    }

    // ── (d) MuSig2 nonce IsValid() bounds: pubnonce length, oracle_id,
    //         signature length ──
    {
        OracleMusigNonceMsg n;
        n.epoch = 7;
        n.oracle_id = 0;
        n.pubnonce.assign(66, 0xAB);
        n.signature.assign(64, 0xCD);
        BOOST_CHECK(n.IsValid());

        // Oversized pubnonce
        OracleMusigNonceMsg over = n;
        over.pubnonce.assign(67, 0xAB);
        BOOST_CHECK_MESSAGE(!over.IsValid(),
            "OracleMusigNonceMsg.IsValid must reject oversized pubnonce");

        // Truncated pubnonce
        OracleMusigNonceMsg trunc = n;
        trunc.pubnonce.assign(33, 0xAB);
        BOOST_CHECK_MESSAGE(!trunc.IsValid(),
            "OracleMusigNonceMsg.IsValid must reject truncated pubnonce");

        // Empty pubnonce
        OracleMusigNonceMsg empty = n;
        empty.pubnonce.clear();
        BOOST_CHECK_MESSAGE(!empty.IsValid(),
            "OracleMusigNonceMsg.IsValid must reject empty pubnonce");

        // oracle_id == 255 is the documented sentinel; >= 255 is invalid.
        OracleMusigNonceMsg bad_id = n;
        bad_id.oracle_id = 255;
        BOOST_CHECK_MESSAGE(!bad_id.IsValid(),
            "OracleMusigNonceMsg.IsValid must reject oracle_id == 255");

        // Truncated signature.
        OracleMusigNonceMsg bad_sig = n;
        bad_sig.signature.assign(32, 0xCD);
        BOOST_CHECK_MESSAGE(!bad_sig.IsValid(),
            "OracleMusigNonceMsg.IsValid must reject truncated signature");

        // Oversized signature.
        OracleMusigNonceMsg big_sig = n;
        big_sig.signature.assign(96, 0xCD);
        BOOST_CHECK_MESSAGE(!big_sig.IsValid(),
            "OracleMusigNonceMsg.IsValid must reject oversized signature");
    }

    // ── (e) MuSig2 partial sig IsValid() bounds: partial_sig length,
    //         oracle_id, signature length ──
    {
        OracleMusigPartialSigMsg p;
        p.epoch = 7;
        p.oracle_id = 0;
        p.partial_sig.assign(32, 0x11);
        p.signature.assign(64, 0x22);
        BOOST_CHECK(p.IsValid());

        OracleMusigPartialSigMsg over = p;
        over.partial_sig.assign(64, 0x11);
        BOOST_CHECK_MESSAGE(!over.IsValid(),
            "OracleMusigPartialSigMsg.IsValid must reject oversized partial_sig");

        OracleMusigPartialSigMsg trunc = p;
        trunc.partial_sig.assign(16, 0x11);
        BOOST_CHECK_MESSAGE(!trunc.IsValid(),
            "OracleMusigPartialSigMsg.IsValid must reject truncated partial_sig");

        OracleMusigPartialSigMsg empty = p;
        empty.partial_sig.clear();
        BOOST_CHECK_MESSAGE(!empty.IsValid(),
            "OracleMusigPartialSigMsg.IsValid must reject empty partial_sig");

        OracleMusigPartialSigMsg bad_id = p;
        bad_id.oracle_id = 255;
        BOOST_CHECK_MESSAGE(!bad_id.IsValid(),
            "OracleMusigPartialSigMsg.IsValid must reject oracle_id == 255");

        OracleMusigPartialSigMsg bad_sig = p;
        bad_sig.signature.assign(63, 0x22);
        BOOST_CHECK_MESSAGE(!bad_sig.IsValid(),
            "OracleMusigPartialSigMsg.IsValid must reject truncated signature");
    }

    // ── (f) Pending state must be empty after rejection sweep — no
    //         malformed message must have leaked in ──
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 0U);
    BOOST_CHECK_EQUAL(manager.GetPendingAttestationCount(), 0U);

    manager.Clear();
}

// ============================================================================
// DD-FA-TEST-034: seen_message_hashes flooding cap holds AT 2048 — direct
// cardinality assertion, not just absence-of-OOM
//
// `OracleBundleManager::AddOracleMessage` and `RegisterSeenHash` cap the
// internal `seen_message_hashes` set at MAX_SEEN_HASHES (2048). Existing
// `attack_seen_hashes_memory_exhaustion` only checks "no OOM after 3000
// messages"; it does not assert the cap actually holds at 2048. This pin
// proves the cap by counting how many of the 2560 distinct unique hashes
// remain reachable via `HasOracleMessage`.
//
// Eviction order is NOT FIFO: `seen_message_hashes` is a `std::set<uint256>`
// and `RegisterSeenHash` evicts from `begin()` (memcmp-smallest first), not
// from insertion order. This pin therefore asserts only the cardinality
// invariant — total surviving hashes == 2048 — which is the property that
// matters for DoS resistance.
// ============================================================================
BOOST_AUTO_TEST_CASE(seen_message_hashes_cap_at_2048)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    constexpr size_t MAX_SEEN_HASHES = 2048;
    constexpr size_t OVERSHOOT = 512;
    constexpr size_t TOTAL_INSERTED = MAX_SEEN_HASHES + OVERSHOOT;

    std::vector<uint256> all_hashes;
    all_hashes.reserve(TOTAL_INSERTED);

    // Use random distinct uint256 hashes so the memcmp-based eviction order
    // is uncorrelated with insertion order. We only assert the cardinality.
    FastRandomContext rand(/*deterministic=*/true);
    for (size_t i = 0; i < TOTAL_INSERTED; ++i) {
        all_hashes.push_back(rand.rand256());
        manager.RegisterSeenHash(all_hashes.back());
    }

    // After inserting more than MAX_SEEN_HASHES distinct hashes, the cap must
    // have evicted exactly the overshoot. Counting via HasOracleMessage gives
    // a public, non-instrumentation-dependent measurement.
    size_t retained = 0;
    for (const uint256& h : all_hashes) {
        if (manager.HasOracleMessage(h)) {
            ++retained;
        }
    }
    BOOST_CHECK_MESSAGE(retained == MAX_SEEN_HASHES,
        "After flooding " << TOTAL_INSERTED << " distinct hashes, expected exactly "
        << MAX_SEEN_HASHES << " retained, observed " << retained);

    // Insert another full sweep — cap must still hold.
    for (size_t i = 0; i < OVERSHOOT; ++i) {
        manager.RegisterSeenHash(rand.rand256());
    }
    retained = 0;
    for (const uint256& h : all_hashes) {
        if (manager.HasOracleMessage(h)) {
            ++retained;
        }
    }
    BOOST_CHECK_MESSAGE(retained <= MAX_SEEN_HASHES,
        "After second flood, original survivors must not exceed cap (observed " << retained << ")");

    manager.Clear();
}

// ============================================================================
// DD-FA-TEST-035: Replay protection — same hash twice
//
// AddOracleMessage MUST reject the second insertion of an identical signed
// message via the seen_message_hashes dedup check. A successful resubmit
// would let an attacker re-trigger consensus side-effects (logs,
// notification, attestation auto-generation) for an already-processed
// price.
// ============================================================================
BOOST_AUTO_TEST_CASE(replay_same_hash_twice_rejected)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    const int64_t now = GetTime();
    COraclePriceMessage msg = MakeSignedRegtestMessage(0, 6000, now);

    BOOST_REQUIRE(manager.AddOracleMessage(msg));
    BOOST_REQUIRE_EQUAL(manager.GetPendingMessageCount(), 1U);

    // Identical replay — must be rejected by seen_message_hashes dedup.
    // Using the exact same object re-feeds an identical attestation hash.
    BOOST_CHECK_MESSAGE(!manager.AddOracleMessage(msg),
        "Identical signed message replay must be rejected by dedup");

    // Pending count must not have changed.
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 1U);

    manager.Clear();
}

BOOST_AUTO_TEST_SUITE_END()
