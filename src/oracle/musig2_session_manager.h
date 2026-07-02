// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_ORACLE_MUSIG2_SESSION_MANAGER_H
#define DIGIBYTE_ORACLE_MUSIG2_SESSION_MANAGER_H

#include <oracle/musig2_session.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>

class OracleMusigNonceMsg;
class OracleMusigPartialSigMsg;

/**
 * MuSig2 Session Manager - manages per-epoch signing sessions for P2P collection.
 * Thread-safe. All public methods are guarded by m_mutex.
 */
class MuSig2SessionManager
{
public:
    explicit MuSig2SessionManager(uint8_t min_signers = 9, int32_t timeout_blocks = 50);

    /** Process an incoming pubnonce from P2P. Creates session if needed. */
    bool OnNonceReceived(int32_t epoch, uint8_t oracle_id,
                         const std::vector<unsigned char>& pubnonce_bytes);

    /** Process an incoming partial signature from P2P. */
    bool OnPartialSigReceived(int32_t epoch, uint8_t oracle_id,
                              const std::vector<unsigned char>& partial_sig_bytes);

    /** Check all active sessions for timeout. Called once per new block tip. */
    void CheckTimeouts(int32_t current_height);

    bool HasSession(int32_t epoch) const;
    MuSig2SessionState GetSessionState(int32_t epoch) const;
    bool HasEnoughNonces(int32_t epoch) const;
    bool HasEnoughPartialSigs(int32_t epoch) const;
    MuSig2SigningSession* GetSession(int32_t epoch);
    size_t GetActiveSessionCount() const;

    bool HasSeenNonce(const uint256& hash) const;
    bool HasSeenPartialSig(const uint256& hash) const;
    bool RegisterSeenNonce(const uint256& hash);
    bool RegisterSeenPartialSig(const uint256& hash);

    void CleanupOldSessions(int32_t current_epoch);
    void Clear();

    static MuSig2SessionManager& GetInstance();

private:
    mutable Mutex m_mutex;
    uint8_t m_min_signers;
    int32_t m_timeout_blocks;
    std::map<int32_t, std::unique_ptr<MuSig2SigningSession>> m_sessions GUARDED_BY(m_mutex);
    std::set<uint256> m_seen_nonces GUARDED_BY(m_mutex);
    std::set<uint256> m_seen_partial_sigs GUARDED_BY(m_mutex);

    MuSig2SigningSession& GetOrCreateSession(int32_t epoch) EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
};

extern std::unique_ptr<MuSig2SessionManager> g_musig2_session_manager;

#endif // DIGIBYTE_ORACLE_MUSIG2_SESSION_MANAGER_H
