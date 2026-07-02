// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <oracle/musig2_orchestrator.h>

#include <chainparams.h>

#include <algorithm>

/** Global MuSig2 signing sessions indexed by epoch */
std::map<int32_t, MuSig2SigningSession> g_oracle_signing_sessions;
Mutex g_oracle_signing_sessions_mutex;

#include <secp256k1_musig.h>

MuSig2Orchestrator::MuSig2Orchestrator() = default;
MuSig2Orchestrator::~MuSig2Orchestrator() = default;

bool MuSig2Orchestrator::CreateSessionForEpoch(int32_t epoch, uint8_t min_signers)
{
    LOCK(m_mutex);
    if (m_sessions.count(epoch)) return false;
    m_sessions.emplace(epoch, MuSig2SigningSession(epoch, min_signers));
    return true;
}

MuSig2SigningSession* MuSig2Orchestrator::GetSession(int32_t epoch)
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return nullptr;
    return &it->second;
}

void MuSig2Orchestrator::PruneOldSessions(int32_t current_epoch)
{
    LOCK(m_mutex);
    auto it = m_sessions.begin();
    while (it != m_sessions.end()) {
        if (it->first < current_epoch) {
            m_partial_sig_ids.erase(it->first);
            m_completed.erase(it->first);
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }
}

bool MuSig2Orchestrator::GenerateNonceForEpoch(int32_t epoch,
                                                   uint8_t oracle_id,
                                                   const CKey& signing_key,
                                                   const secp256k1_pubkey& pubkey,
                                                   const secp256k1_musig_keyagg_cache& cache,
                                                   secp256k1_musig_pubnonce& pubnonce_out)
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    return it->second.GenerateNonce(oracle_id, signing_key, pubkey, cache, pubnonce_out);
}

bool MuSig2Orchestrator::AddNonceForEpoch(int32_t epoch, uint8_t oracle_id,
                                              const secp256k1_musig_pubnonce& pubnonce)
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    return it->second.AddPubnonce(oracle_id, pubnonce);
}

bool MuSig2Orchestrator::AdvanceToSigning(int32_t epoch, const unsigned char* msg32)
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    return it->second.AggregateNonces(msg32);
}

bool MuSig2Orchestrator::TryAdvanceToSigning(int32_t epoch, const unsigned char* msg32)
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    auto& session = it->second;
    if (session.GetState() != MuSig2SessionState::NONCES_COMPLETE) return false;
    if (!session.HasEnoughNonces()) return false;
    return session.AggregateNonces(msg32);
}

bool MuSig2Orchestrator::AddPartialSigForEpoch(int32_t epoch, uint8_t oracle_id,
                                                   const secp256k1_musig_partial_sig& partial_sig)
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    if (!it->second.AddPartialSignature(oracle_id, partial_sig)) return false;
    m_partial_sig_ids[epoch].insert(oracle_id);
    return true;
}

bool MuSig2Orchestrator::AggregateForEpoch(int32_t epoch, std::vector<unsigned char>& sig64_out)
{
    LOCK(m_mutex);
    auto it = m_sessions.find(epoch);
    if (it == m_sessions.end()) return false;
    if (!it->second.AggregateSignature(sig64_out)) return false;

    MuSig2CompletedResult result;
    result.aggregate_sig = sig64_out;
    auto ids_it = m_partial_sig_ids.find(epoch);
    if (ids_it != m_partial_sig_ids.end()) {
        result.participation_bitmap = BuildBitmap(ids_it->second);
    }
    m_completed[epoch] = std::move(result);
    return true;
}

bool MuSig2Orchestrator::GetCompletedSessionData(int32_t epoch,
                                                     std::vector<unsigned char>& sig_out,
                                                     std::vector<unsigned char>& bitmap_out)
{
    LOCK(m_mutex);
    auto comp_it = m_completed.find(epoch);
    if (comp_it != m_completed.end()) {
        sig_out = comp_it->second.aggregate_sig;
        bitmap_out = comp_it->second.participation_bitmap;
        return !sig_out.empty() && !bitmap_out.empty();
    }
    return false;
}

void MuSig2Orchestrator::SetLocalOracle(uint8_t oracle_id, const CKey& signing_key)
{
    LOCK(m_mutex);
    m_is_oracle = true;
    m_oracle_id = oracle_id;
    m_signing_key = signing_key;
}

void MuSig2Orchestrator::ClearLocalOracle()
{
    LOCK(m_mutex);
    m_is_oracle = false;
    m_oracle_id = 0;
    m_signing_key = CKey();
}

bool MuSig2Orchestrator::IsOracleActive() const
{
    LOCK(m_mutex);
    return m_is_oracle && m_signing_key.IsValid();
}

uint8_t MuSig2Orchestrator::GetLocalOracleId() const
{
    LOCK(m_mutex);
    return m_oracle_id;
}

std::vector<unsigned char> MuSig2Orchestrator::BuildBitmap(const std::set<uint8_t>& oracle_ids) const
{
    if (oracle_ids.empty()) return {};

    const Consensus::Params& consensus = Params().GetConsensus();
    const int configured_total = std::max(consensus.nOracleTotalOracles, consensus.nOraclePubkeyCount);
    if (configured_total <= 0 || configured_total > 256) return {};
    if (consensus.nOraclePubkeyCount <= 0 || consensus.nOraclePubkeyCount > configured_total) return {};
    const uint16_t total_oracles = static_cast<uint16_t>(configured_total);
    const uint16_t active_oracles = static_cast<uint16_t>(consensus.nOraclePubkeyCount);

    const size_t bitmap_bytes = (total_oracles + 7) / 8;
    std::vector<unsigned char> bitmap(bitmap_bytes, 0);
    for (uint8_t id : oracle_ids) {
        if (id >= active_oracles) return {};
        bitmap[id / 8] |= (1 << (id % 8));
    }
    return bitmap;
}
