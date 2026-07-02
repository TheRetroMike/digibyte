// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <oracle/musig2_session_manager.h>

#include <logging.h>
#include <protocol.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>

#include <cassert>
#include <cstring>

std::unique_ptr<MuSig2SessionManager> g_musig2_session_manager;

MuSig2SessionManager::MuSig2SessionManager(uint8_t min_signers, int32_t timeout_blocks)
    : m_min_signers(min_signers), m_timeout_blocks(timeout_blocks) {}

MuSig2SigningSession& MuSig2SessionManager::GetOrCreateSession(int32_t epoch)
{
    AssertLockHeld(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it != m_sessions.end()) {
        return *it->second;
    }
    auto session = std::make_unique<MuSig2SigningSession>(epoch, m_min_signers);
    session->SetTimeoutBlocks(m_timeout_blocks);
    auto [inserted_it, success] = m_sessions.emplace(epoch, std::move(session));
    assert(success);
    return *inserted_it->second;
}

bool MuSig2SessionManager::OnNonceReceived(int32_t epoch, uint8_t oracle_id,
                                            const std::vector<unsigned char>& pubnonce_bytes)
{
    if (pubnonce_bytes.size() != 66) return false;

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    secp256k1_musig_pubnonce pubnonce;
    int parse_ok = secp256k1_musig_pubnonce_parse(ctx, &pubnonce, pubnonce_bytes.data());
    secp256k1_context_destroy(ctx);

    if (!parse_ok) return false;

    LOCK(m_mutex);
    MuSig2SigningSession& session = GetOrCreateSession(epoch);
    return session.AddPubnonce(oracle_id, pubnonce);
}

bool MuSig2SessionManager::OnPartialSigReceived(int32_t epoch, uint8_t oracle_id,
                                                  const std::vector<unsigned char>& partial_sig_bytes)
{
    if (partial_sig_bytes.size() != 32) return false;

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    secp256k1_musig_partial_sig partial_sig;
    int parse_ok = secp256k1_musig_partial_sig_parse(ctx, &partial_sig, partial_sig_bytes.data());
    secp256k1_context_destroy(ctx);

    if (!parse_ok) return false;

    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    return it->second->AddPartialSignature(oracle_id, partial_sig);
}

void MuSig2SessionManager::CheckTimeouts(int32_t current_height)
{
    LOCK(m_mutex);
    for (auto& [epoch, session] : m_sessions) {
        session->CheckTimeout(current_height);
    }
}

bool MuSig2SessionManager::HasSession(int32_t epoch) const
{
    LOCK(m_mutex);
    return m_sessions.count(epoch) > 0;
}

MuSig2SessionState MuSig2SessionManager::GetSessionState(int32_t epoch) const
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return MuSig2SessionState::FAILED;
    return it->second->GetState();
}

bool MuSig2SessionManager::HasEnoughNonces(int32_t epoch) const
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    return it->second->HasEnoughNonces();
}

bool MuSig2SessionManager::HasEnoughPartialSigs(int32_t epoch) const
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    return it->second->HasEnoughPartialSigs();
}

MuSig2SigningSession* MuSig2SessionManager::GetSession(int32_t epoch)
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return nullptr;
    return it->second.get();
}

size_t MuSig2SessionManager::GetActiveSessionCount() const
{
    LOCK(m_mutex);
    return m_sessions.size();
}

bool MuSig2SessionManager::HasSeenNonce(const uint256& hash) const
{
    LOCK(m_mutex);
    return m_seen_nonces.count(hash) > 0;
}

bool MuSig2SessionManager::HasSeenPartialSig(const uint256& hash) const
{
    LOCK(m_mutex);
    return m_seen_partial_sigs.count(hash) > 0;
}

bool MuSig2SessionManager::RegisterSeenNonce(const uint256& hash)
{
    LOCK(m_mutex);
    return m_seen_nonces.insert(hash).second;
}

bool MuSig2SessionManager::RegisterSeenPartialSig(const uint256& hash)
{
    LOCK(m_mutex);
    return m_seen_partial_sigs.insert(hash).second;
}

void MuSig2SessionManager::CleanupOldSessions(int32_t current_epoch)
{
    LOCK(m_mutex);
    bool removed_any_session = false;
    auto it = m_sessions.begin();
    while (it != m_sessions.end()) {
        MuSig2SessionState state = it->second->GetState();
        bool is_old = it->first < current_epoch;
        bool is_terminal = (state == MuSig2SessionState::COMPLETE || state == MuSig2SessionState::FAILED);
        if (is_old && is_terminal) {
            removed_any_session = true;
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }

    // These replay filters are only useful for live sessions. Once old
    // terminal sessions are pruned, clear the global seen sets as well so they
    // cannot grow without bound across epochs.
    if (removed_any_session || m_sessions.empty()) {
        m_seen_nonces.clear();
        m_seen_partial_sigs.clear();
    }
}

void MuSig2SessionManager::Clear()
{
    LOCK(m_mutex);
    m_sessions.clear();
    m_seen_nonces.clear();
    m_seen_partial_sigs.clear();
}

MuSig2SessionManager& MuSig2SessionManager::GetInstance()
{
    assert(g_musig2_session_manager);
    return *g_musig2_session_manager;
}
