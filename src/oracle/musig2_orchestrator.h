// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_ORACLE_MUSIG2_ORCHESTRATOR_H
#define DIGIBYTE_ORACLE_MUSIG2_ORCHESTRATOR_H

#include <key.h>
#include <oracle/musig2_session.h>
#include <sync.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>

#include <cstdint>
#include <map>
#include <set>
#include <vector>

/**
 * Completed session result: aggregate signature + participation bitmap.
 */
struct MuSig2CompletedResult {
    std::vector<unsigned char> aggregate_sig;       //!< 64-byte BIP-340 Schnorr signature
    std::vector<unsigned char> participation_bitmap; //!< Variable-length bitmap of participating oracle IDs
};

/**
 * MuSig2 Session Manager — orchestrates signing session lifecycle.
 *
 * Manages one MuSig2SigningSession per oracle epoch. Responsible for:
 * - Creating sessions at epoch start
 * - Generating nonces when local node is an active oracle
 * - Feeding peer nonces and partial signatures to the correct session
 * - Advancing sessions through the signing state machine
 * - Pruning expired sessions (only current + next epoch kept)
 * - Providing completed signature data for block construction
 *
 * Thread safety: all public methods are guarded by m_mutex.
 *
 * Typical lifecycle per epoch:
 *   1. ConnectBlock detects new epoch → CreateSessionForEpoch()
 *   2. GenerateNonceForEpoch() → produces pubnonce for P2P broadcast
 *   3. P2P handler calls AddNonceForEpoch() for each peer nonce
 *   4. Timer tick calls TryAdvanceToSigning() when nonces collected
 *   5. P2P handler calls AddPartialSigForEpoch() for each peer sig
 *   6. AggregateForEpoch() produces final signature
 *   7. Block template requests GetCompletedSessionData()
 *   8. PruneOldSessions() cleans up on epoch boundary
 */
class MuSig2Orchestrator
{
public:
    MuSig2Orchestrator();
    ~MuSig2Orchestrator();

    MuSig2Orchestrator(const MuSig2Orchestrator&) = delete;
    MuSig2Orchestrator& operator=(const MuSig2Orchestrator&) = delete;

    // ---- Session lifecycle ----

    bool CreateSessionForEpoch(int32_t epoch, uint8_t min_signers);
    MuSig2SigningSession* GetSession(int32_t epoch);
    void PruneOldSessions(int32_t current_epoch);

    // ---- Round 1: Nonce generation and collection ----

    bool GenerateNonceForEpoch(int32_t epoch,
                               uint8_t oracle_id,
                               const CKey& signing_key,
                               const secp256k1_pubkey& pubkey,
                               const secp256k1_musig_keyagg_cache& cache,
                               secp256k1_musig_pubnonce& pubnonce_out);

    bool AddNonceForEpoch(int32_t epoch, uint8_t oracle_id,
                          const secp256k1_musig_pubnonce& pubnonce);

    // ---- Nonce aggregation → Signing ----

    bool AdvanceToSigning(int32_t epoch, const unsigned char* msg32);
    bool TryAdvanceToSigning(int32_t epoch, const unsigned char* msg32);

    // ---- Round 2: Partial signature collection ----

    bool AddPartialSigForEpoch(int32_t epoch, uint8_t oracle_id,
                               const secp256k1_musig_partial_sig& partial_sig);

    bool AggregateForEpoch(int32_t epoch, std::vector<unsigned char>& sig64_out);

    // ---- Completed session data ----

    bool GetCompletedSessionData(int32_t epoch,
                                 std::vector<unsigned char>& sig_out,
                                 std::vector<unsigned char>& bitmap_out);

    // ---- Local oracle identity ----

    void SetLocalOracle(uint8_t oracle_id, const CKey& signing_key);
    void ClearLocalOracle();
    bool IsOracleActive() const;
    uint8_t GetLocalOracleId() const;

private:
    mutable Mutex m_mutex;

    std::map<int32_t, MuSig2SigningSession> m_sessions GUARDED_BY(m_mutex);
    std::map<int32_t, std::set<uint8_t>> m_partial_sig_ids GUARDED_BY(m_mutex);
    std::map<int32_t, MuSig2CompletedResult> m_completed GUARDED_BY(m_mutex);

    bool m_is_oracle GUARDED_BY(m_mutex){false};
    uint8_t m_oracle_id GUARDED_BY(m_mutex){0};
    CKey m_signing_key GUARDED_BY(m_mutex);

    std::vector<unsigned char> BuildBitmap(const std::set<uint8_t>& oracle_ids) const;
};

/** Global MuSig2 signing sessions indexed by epoch */
extern std::map<int32_t, MuSig2SigningSession> g_oracle_signing_sessions;
extern Mutex g_oracle_signing_sessions_mutex;

#endif // DIGIBYTE_ORACLE_MUSIG2_ORCHESTRATOR_H
