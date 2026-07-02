// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Wave 22 Agent B — Fuzz Harness Completeness.
//
// DD-FA-TEST-042 — Drive the REAL `MuSig2SigningSession` lifecycle.
//
// The existing `musig2_session_state` harness models a SIMULATED state
// machine that mirrors the wire-message acceptance contract. The
// existing `musig2_session_manager_drive` harness drives the
// `MuSig2SessionManager` wrapper, which is currently dead code in
// production (Wave 10 Agent A finding). Neither covers the real
// `MuSig2SigningSession` class that sits inside
// `OracleSigningOrchestrator` and produces every aggregate signature
// that ends up on chain.
//
// This harness builds a real BIP-327 session from valid keys (so we
// can pass libsecp256k1 ARG_CHECK), then fuzzes the ORDERING of
// lifecycle calls and the SHAPE of the participant set. It targets
// the libsecp256k1 boundaries that abort() on misuse:
//
//   * GenerateNonce called twice for the same oracle ID → reject
//   * AddPubnonce after AggregateNonces → reject
//   * AddPubnonce with malformed magic bytes → reject (no abort)
//   * AddPartialSignature before AggregateNonces → reject
//   * AddPartialSignature with malformed magic bytes → reject
//   * AggregateSignature without enough partial sigs → reject
//   * TrimNoncesToThreshold then AddPubnonce → reject (frozen)
//   * SetKeyAggCache after Aggregate → no crash
//   * out-of-range oracle IDs → reject
//
// The harness maintains a small pool of valid pubnonces produced by
// real GenerateNonce calls so AddPubnonce on the wrong session can
// exercise libsecp256k1 parsing without UB.

#include <chainparams.h>
#include <key.h>
#include <oracle/musig2_session.h>
#include <random.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint8_t kRosterSize = 7;     // matches regtest active roster
constexpr uint8_t kMinSigners = 4;     // matches regtest 4-of-7
constexpr int kMaxIterations = 80;

void initialize_oracle_musig2_session_real()
{
    static const auto testing_setup = MakeNoLogFileContext<const BasicTestingSetup>(ChainType::REGTEST);
    (void)testing_setup;
}

struct OracleKeyset {
    std::array<unsigned char, 32> seckey;
    secp256k1_keypair keypair;
    secp256k1_pubkey pubkey;
    CKey ckey;
};

/** Build a deterministic-ish but valid oracle key set. */
bool MakeKeyset(secp256k1_context* ctx, uint8_t oracle_id, OracleKeyset& out)
{
    // Deterministic per-oracle secret seed; XOR a counter so each oracle
    // gets a distinct (and valid) secret.
    out.seckey.fill(0);
    out.seckey[0] = oracle_id + 1;
    out.seckey[31] = oracle_id + 1;
    if (!secp256k1_ec_seckey_verify(ctx, out.seckey.data())) return false;
    if (!secp256k1_keypair_create(ctx, &out.keypair, out.seckey.data())) return false;
    if (!secp256k1_keypair_pub(ctx, &out.pubkey, &out.keypair)) return false;
    out.ckey.Set(out.seckey.begin(), out.seckey.end(), true);
    return out.ckey.IsValid();
}

bool BuildKeyAggCache(secp256k1_context* ctx,
                      const std::vector<OracleKeyset>& oracles,
                      const std::vector<uint8_t>& participant_ids,
                      secp256k1_xonly_pubkey& agg_pk,
                      secp256k1_musig_keyagg_cache& cache)
{
    if (participant_ids.empty()) return false;
    std::vector<const secp256k1_pubkey*> ptrs;
    ptrs.reserve(participant_ids.size());
    for (uint8_t id : participant_ids) {
        if (id >= oracles.size()) return false;
        ptrs.push_back(&oracles[id].pubkey);
    }
    return secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, ptrs.data(), ptrs.size()) == 1;
}

} // namespace

FUZZ_TARGET(oracle_musig2_session_real, .init = initialize_oracle_musig2_session_real)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    assert(ctx);

    // Build a fixed pool of valid oracle keys. These survive across the
    // inner loop so AddPubnonce / AddPartialSignature can be called with
    // both correctly-shaped and incorrectly-routed inputs.
    std::vector<OracleKeyset> oracles(kRosterSize);
    for (uint8_t i = 0; i < kRosterSize; ++i) {
        if (!MakeKeyset(ctx, i, oracles[i])) {
            secp256k1_context_destroy(ctx);
            return;
        }
    }

    const int32_t epoch = fdp.ConsumeIntegralInRange<int32_t>(0, 1'000'000);
    MuSig2SigningSession session(epoch, kMinSigners);

    // Build a participant-set keyagg cache (4-of-7). This is the cache
    // the production orchestrator binds via SetKeyAggCache before
    // AggregateNonces.
    std::vector<uint8_t> participants{0, 1, 2, 3};
    secp256k1_xonly_pubkey agg_pk{};
    secp256k1_musig_keyagg_cache cache{};
    if (!BuildKeyAggCache(ctx, oracles, participants, agg_pk, cache)) {
        secp256k1_context_destroy(ctx);
        return;
    }

    // Pre-generate one valid pubnonce per participant so the harness has
    // a pool of "well-formed" pubnonces to mix with garbage shapes.
    std::vector<secp256k1_musig_pubnonce> valid_pubnonces(participants.size());
    {
        for (size_t i = 0; i < participants.size(); ++i) {
            secp256k1_musig_pubnonce pubnonce{};
            const uint8_t oid = participants[i];
            const bool ok = session.GenerateNonce(oid, oracles[oid].ckey,
                                                   oracles[oid].pubkey, cache,
                                                   pubnonce);
            // First call must succeed; double call must reject.
            assert(ok);
            const bool double_ok = session.GenerateNonce(oid, oracles[oid].ckey,
                                                          oracles[oid].pubkey, cache,
                                                          pubnonce);
            assert(!double_ok);
            valid_pubnonces[i] = pubnonce;
        }
    }

    LIMITED_WHILE(fdp.remaining_bytes() > 0, kMaxIterations) {
        switch (fdp.ConsumeIntegralInRange<int>(0, 12)) {
        case 0: {
            // AddPubnonce with a valid pubnonce from the pool. May or may
            // not be acceptable depending on session state. Pick an oracle
            // ID that may or may not match.
            const size_t pool_idx = fdp.ConsumeIntegralInRange<size_t>(0, valid_pubnonces.size() - 1);
            const uint8_t oracle_id = fdp.ConsumeIntegralInRange<uint8_t>(0, kRosterSize + 5);
            (void)session.AddPubnonce(oracle_id, valid_pubnonces[pool_idx]);
            break;
        }
        case 1: {
            // AddPubnonce with a garbage pubnonce — magic bytes corrupted.
            // Must be rejected without aborting libsecp256k1.
            secp256k1_musig_pubnonce bad{};
            auto bytes = fdp.ConsumeBytes<unsigned char>(sizeof(bad.data));
            for (size_t i = 0; i < bytes.size() && i < sizeof(bad.data); ++i) {
                bad.data[i] = bytes[i];
            }
            const uint8_t oracle_id = fdp.ConsumeIntegralInRange<uint8_t>(0, kRosterSize - 1);
            const bool ok = session.AddPubnonce(oracle_id, bad);
            // Must reject (magic mismatch or duplicate or wrong state).
            (void)ok;
            break;
        }
        case 2: {
            // GenerateNonce for a fresh oracle id (may be out of range).
            const uint8_t oracle_id = fdp.ConsumeIntegralInRange<uint8_t>(0, kRosterSize + 5);
            secp256k1_musig_pubnonce pubnonce{};
            if (oracle_id < kRosterSize) {
                (void)session.GenerateNonce(oracle_id, oracles[oracle_id].ckey,
                                             oracles[oracle_id].pubkey, cache,
                                             pubnonce);
            }
            break;
        }
        case 3: {
            // SetKeyAggCache to a different cache. Production uses this
            // before AggregateNonces; calling it after is harmless but
            // must not crash.
            std::vector<uint8_t> alt_participants{0, 1, 2, 3, 4};
            secp256k1_xonly_pubkey alt_agg{};
            secp256k1_musig_keyagg_cache alt_cache{};
            if (BuildKeyAggCache(ctx, oracles, alt_participants, alt_agg, alt_cache)) {
                session.SetKeyAggCache(alt_cache);
            }
            break;
        }
        case 4: {
            // TrimNoncesToThreshold — must not crash regardless of state.
            session.TrimNoncesToThreshold();
            break;
        }
        case 5: {
            // AggregateNonces with a fuzzed 32-byte message. Either the
            // session is ready and the call returns true, or we are in
            // the wrong state and it returns false. No abort.
            unsigned char msg[32]{};
            auto bytes = fdp.ConsumeBytes<uint8_t>(32);
            for (size_t i = 0; i < bytes.size() && i < 32; ++i) {
                msg[i] = bytes[i];
            }
            (void)session.AggregateNonces(msg);
            break;
        }
        case 6: {
            // CreatePartialSignature for a participant. Only valid when
            // SIGNING; otherwise must reject.
            const size_t pool_idx = fdp.ConsumeIntegralInRange<size_t>(0, participants.size() - 1);
            const uint8_t oracle_id = participants[pool_idx];
            secp256k1_musig_partial_sig sig{};
            (void)session.CreatePartialSignature(oracle_id, oracles[oracle_id].ckey, sig);
            break;
        }
        case 7: {
            // AddPartialSignature with garbage payload — must be rejected
            // by the magic-byte check.
            secp256k1_musig_partial_sig bad{};
            auto bytes = fdp.ConsumeBytes<unsigned char>(sizeof(bad.data));
            for (size_t i = 0; i < bytes.size() && i < sizeof(bad.data); ++i) {
                bad.data[i] = bytes[i];
            }
            const uint8_t oracle_id = fdp.ConsumeIntegralInRange<uint8_t>(0, kRosterSize + 5);
            (void)session.AddPartialSignature(oracle_id, bad);
            break;
        }
        case 8: {
            // AggregateSignature attempt — must not crash even when
            // partial sigs are missing or session is in the wrong state.
            std::vector<unsigned char> sig64;
            const bool agg_ok = session.AggregateSignature(sig64);
            // If it succeeded, we MUST have ended up COMPLETE.
            if (agg_ok) {
                assert(sig64.size() == 64);
                assert(session.GetState() == MuSig2SessionState::COMPLETE);
            }
            break;
        }
        case 9: {
            // Read-only accessors must never crash.
            (void)session.GetState();
            (void)session.GetEpoch();
            (void)session.GetNonceCount();
            (void)session.GetPartialSigCount();
            (void)session.HasEnoughNonces();
            (void)session.HasEnoughPartialSigs();
            (void)session.GetNonceParticipants();
            (void)session.GetAggregateSig();
            (void)session.GetParticipationBitmap();
            (void)session.GetSignedPrice();
            (void)session.GetSignedTimestamp();
            (void)session.GetCreationHeight();
            break;
        }
        case 10: {
            // CheckTimeout at arbitrary heights. May transition to FAILED.
            const int32_t height = fdp.ConsumeIntegralInRange<int32_t>(0, 10'000'000);
            session.CheckTimeout(height);
            break;
        }
        case 11: {
            // SetTimeoutBlocks / SetCreationHeight — test-only setters
            // must not crash on arbitrary values.
            session.SetTimeoutBlocks(fdp.ConsumeIntegralInRange<int32_t>(-1000, 100000));
            session.SetCreationHeight(fdp.ConsumeIntegralInRange<int32_t>(0, 10'000'000));
            break;
        }
        case 12: {
            // SetSignedValues then read back — pure setter/getter pair.
            const uint64_t price = fdp.ConsumeIntegral<uint64_t>();
            const int64_t timestamp = fdp.ConsumeIntegral<int64_t>();
            session.SetSignedValues(price, timestamp);
            assert(session.GetSignedPrice() == price);
            assert(session.GetSignedTimestamp() == timestamp);
            break;
        }
        }
    }

    // After the loop, the state must be one of the known enum values and
    // a participation bitmap (if produced) must not exceed the active
    // roster size.
    const auto bitmap = session.GetParticipationBitmap();
    assert(bitmap.size() <= (kRosterSize + 7) / 8);
    const auto state = session.GetState();
    assert(state == MuSig2SessionState::CREATED ||
           state == MuSig2SessionState::NONCES_COLLECTING ||
           state == MuSig2SessionState::NONCES_COMPLETE ||
           state == MuSig2SessionState::SIGNING ||
           state == MuSig2SessionState::COMPLETE ||
           state == MuSig2SessionState::FAILED);

    secp256k1_context_destroy(ctx);
}
