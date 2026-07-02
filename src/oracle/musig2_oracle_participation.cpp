// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <oracle/musig2_oracle_participation.h>

#include <hash.h>
#include <logging.h>
#include <oracle/signing_orchestrator.h>
#include <primitives/oracle.h>
#include <streams.h>
#include <support/cleanse.h>

#include <secp256k1_musig.h>

#include <cassert>
#include <protocol.h>
#include <cstring>

MuSig2OracleParticipation::MuSig2OracleParticipation() = default;

MuSig2OracleParticipation::~MuSig2OracleParticipation()
{
    if (m_ctx) {
        secp256k1_context_destroy(m_ctx);
        m_ctx = nullptr;
    }
}

void MuSig2OracleParticipation::Initialize(const CKey& oracle_key, uint8_t oracle_id,
                                            const std::vector<secp256k1_pubkey>& oracle_pubkeys,
                                            uint8_t min_signers)
{
    LOCK(m_mtx);

    m_oracle_key = oracle_key;
    m_oracle_id = oracle_id;
    m_min_signers = min_signers;
    m_pubkeys = oracle_pubkeys;

    // Create secp256k1 context
    if (!m_ctx) {
        m_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        assert(m_ctx != nullptr);
    }

    // Compute aggregate public key and cache
    std::vector<const secp256k1_pubkey*> pubkey_ptrs(oracle_pubkeys.size());
    for (size_t i = 0; i < oracle_pubkeys.size(); i++) {
        pubkey_ptrs[i] = &oracle_pubkeys[i];
    }

    secp256k1_xonly_pubkey agg_pk;
    if (!secp256k1_musig_pubkey_agg(m_ctx, &agg_pk, &m_cache,
                                     pubkey_ptrs.data(), pubkey_ptrs.size())) {
        LogPrintf("Oracle MuSig2: Failed to aggregate public keys\n");
        return;
    }

    m_initialized = true;
    LogPrintf("Oracle MuSig2: Initialized for oracle %d with %zu pubkeys, min_signers=%d\n",
              oracle_id, oracle_pubkeys.size(), min_signers);
}

bool MuSig2OracleParticipation::IsInitialized() const
{
    return m_initialized;
}

void MuSig2OracleParticipation::OnBlockConnected(int32_t height)
{
    if (!m_initialized) return;

    LOCK(m_mtx);

    // Check timeout on current session
    if (m_session) {
        m_session->CheckTimeout(height);
    }

    // Compute epoch from height
    int32_t new_epoch = GetCurrentEpoch(height);
    if (new_epoch == m_epoch && m_session) {
        return; // Same epoch, session already exists
    }

    // New epoch — start new session
    m_epoch = new_epoch;
    m_has_consensus = false;
    m_consensus_price = 0;
    m_consensus_timestamp = 0;

    m_session = std::make_unique<MuSig2SigningSession>(new_epoch, m_min_signers);

    // Generate our nonce
    secp256k1_musig_pubnonce pubnonce;
    if (!m_session->GenerateNonce(m_oracle_id, m_oracle_key, m_pubkeys[m_oracle_id], m_cache, pubnonce)) {
        LogPrintf("Oracle MuSig2: Failed to generate nonce for epoch %d\n", new_epoch);
        m_session.reset();
        return;
    }

    // Add our own pubnonce to the session
    m_session->AddPubnonce(m_oracle_id, pubnonce);

    // Broadcast our nonce
    BroadcastNonce(pubnonce);

    LogPrint(BCLog::DIGIDOLLAR, "Oracle MuSig2: New session for epoch %d, nonce broadcast\n", new_epoch);
}

void MuSig2OracleParticipation::OnOracleMusigNonce(const OracleMusigNonceMsg& msg)
{
    if (!m_initialized) return;
    if (!msg.IsValid()) return;

    LOCK(m_mtx);

    if (!m_session) return;
    if (msg.epoch != m_epoch) return;

    // Deserialize the pubnonce
    secp256k1_musig_pubnonce pubnonce;
    if (!secp256k1_musig_pubnonce_parse(m_ctx, &pubnonce, msg.pubnonce.data())) {
        LogPrint(BCLog::DIGIDOLLAR, "Oracle MuSig2: Failed to parse pubnonce from oracle %d\n", msg.oracle_id);
        return;
    }

    // Add to session
    if (!m_session->AddPubnonce(msg.oracle_id, pubnonce)) {
        return; // Duplicate or wrong state
    }

    LogPrint(BCLog::DIGIDOLLAR, "Oracle MuSig2: Added nonce from oracle %d for epoch %d\n",
             msg.oracle_id, msg.epoch);

    // Try to advance to signing if we have consensus and enough nonces
    TryAdvanceToSigning();
}

void MuSig2OracleParticipation::OnOracleMusigPartialSig(const OracleMusigPartialSigMsg& msg)
{
    if (!m_initialized) return;
    if (!msg.IsValid()) return;

    LOCK(m_mtx);

    if (!m_session) return;
    if (msg.epoch != m_epoch) return;

    // Deserialize the partial sig
    secp256k1_musig_partial_sig psig;
    if (!secp256k1_musig_partial_sig_parse(m_ctx, &psig, msg.partial_sig.data())) {
        LogPrint(BCLog::DIGIDOLLAR, "Oracle MuSig2: Failed to parse partial sig from oracle %d\n", msg.oracle_id);
        return;
    }

    // Add to session
    if (!m_session->AddPartialSignature(msg.oracle_id, psig)) {
        return; // Duplicate or wrong state
    }

    LogPrint(BCLog::DIGIDOLLAR, "Oracle MuSig2: Added partial sig from oracle %d for epoch %d\n",
             msg.oracle_id, msg.epoch);

    // Try to aggregate
    TryAggregateSignature();
}

void MuSig2OracleParticipation::SetConsensusValues(int32_t epoch, uint64_t price, int64_t timestamp)
{
    LOCK(m_mtx);

    if (epoch != m_epoch) return;
    if (!m_session) return;

    m_has_consensus = true;
    m_consensus_price = price;
    m_consensus_timestamp = timestamp;

    LogPrint(BCLog::DIGIDOLLAR, "Oracle MuSig2: Consensus set for epoch %d: price=%llu, timestamp=%lld\n",
             epoch, price, timestamp);

    // Try to advance if we have enough nonces
    TryAdvanceToSigning();
}

void MuSig2OracleParticipation::TryAdvanceToSigning()
{
    if (!m_session) return;
    if (!m_has_consensus) return;

    MuSig2SessionState state = m_session->GetState();
    if (state != MuSig2SessionState::NONCES_COMPLETE) return;

    // Compute message hash via the canonical orchestrator helper. The
    // helper binds "DigiDollar/OracleBundle" tag and the chain's
    // hashGenesisBlock, so signer + validator agree byte-for-byte and
    // cross-chain replay is rejected (DD-FA-SEC-008).
    unsigned char msg32[32];
    OracleSigningOrchestrator::ComputeOracleMessageHash(
        m_epoch, m_consensus_price, m_consensus_timestamp, msg32);
    uint256 msg_hash;
    std::memcpy(msg_hash.begin(), msg32, 32);

    // Aggregate nonces with message
    if (!m_session->AggregateNonces(msg_hash.begin())) {
        LogPrintf("Oracle MuSig2: Failed to aggregate nonces for epoch %d\n", m_epoch);
        return;
    }

    // Create our partial signature
    secp256k1_musig_partial_sig psig;
    if (!m_session->CreatePartialSignature(m_oracle_id, m_oracle_key, psig)) {
        LogPrintf("Oracle MuSig2: Failed to create partial signature for epoch %d\n", m_epoch);
        return;
    }

    // Add our own partial sig
    m_session->AddPartialSignature(m_oracle_id, psig);

    // Broadcast our partial sig
    BroadcastPartialSig(psig);

    LogPrint(BCLog::DIGIDOLLAR, "Oracle MuSig2: Partial sig created and broadcast for epoch %d\n", m_epoch);

    // Check if we already have enough partial sigs to aggregate
    TryAggregateSignature();
}

void MuSig2OracleParticipation::TryAggregateSignature()
{
    if (!m_session) return;
    if (m_session->GetState() != MuSig2SessionState::SIGNING) return;
    if (!m_session->HasEnoughPartialSigs()) return;

    std::vector<unsigned char> sig64;
    if (!m_session->AggregateSignature(sig64)) {
        LogPrintf("Oracle MuSig2: Failed to aggregate signature for epoch %d\n", m_epoch);
        return;
    }

    LogPrint(BCLog::DIGIDOLLAR, "Oracle MuSig2: Signing COMPLETE for epoch %d, sig=%d bytes\n",
             m_epoch, sig64.size());
}

COracleBundle MuSig2OracleParticipation::GetCurrentBundle(int32_t height)
{
    LOCK(m_mtx);

    if (m_session && m_session->GetState() == MuSig2SessionState::COMPLETE) {
        COracleBundle bundle;
        bundle.version = 3;
        bundle.epoch = m_epoch;
        bundle.median_price_micro_usd = m_consensus_price;
        bundle.timestamp = m_consensus_timestamp;
        bundle.aggregate_sig = m_session->GetAggregateSig();
        bundle.participation_bitmap = m_session->GetParticipationBitmap();
        return bundle;
    }

    COracleBundle empty;
    empty.version = 3;
    empty.epoch = m_epoch >= 0 ? m_epoch : height;
    return empty;
}

MuSig2SessionState MuSig2OracleParticipation::GetSessionState() const
{
    LOCK(m_mtx);
    if (!m_session) return MuSig2SessionState::FAILED;
    return m_session->GetState();
}

int32_t MuSig2OracleParticipation::GetSessionEpoch() const
{
    LOCK(m_mtx);
    return m_epoch;
}

void MuSig2OracleParticipation::SetLatestV02Bundle(const COracleBundle& bundle, int32_t height)
{
    LOCK(m_mtx);
    (void)bundle;
    (void)height;
}

void MuSig2OracleParticipation::SetRelayCallback(RelayCallback callback)
{
    m_relay = std::move(callback);
}

void MuSig2OracleParticipation::BroadcastNonce(const secp256k1_musig_pubnonce& pubnonce)
{
    if (!m_relay) return;

    OracleMusigNonceMsg msg;
    msg.epoch = m_epoch;
    msg.oracle_id = m_oracle_id;
    msg.pubnonce.resize(66);
    secp256k1_musig_pubnonce_serialize(m_ctx, msg.pubnonce.data(), &pubnonce);

    // RH-24: Sign with oracle private key for P2P authentication
    if (!msg.Sign(m_oracle_key)) {
        LogPrintf("Oracle MuSig2: Failed to sign nonce message for epoch %d\n", m_epoch);
        return;
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << msg;
    std::vector<unsigned char> payload(reinterpret_cast<const unsigned char*>(ss.data()),
                                       reinterpret_cast<const unsigned char*>(ss.data()) + ss.size());
    m_relay(NetMsgType::ORACLEMUSIGNONCE, payload);
}

void MuSig2OracleParticipation::BroadcastPartialSig(const secp256k1_musig_partial_sig& psig)
{
    if (!m_relay) return;

    OracleMusigPartialSigMsg msg;
    msg.epoch = m_epoch;
    msg.oracle_id = m_oracle_id;
    msg.partial_sig.resize(32);
    secp256k1_musig_partial_sig_serialize(m_ctx, msg.partial_sig.data(), &psig);

    // RH-24: Sign with oracle private key for P2P authentication
    if (!msg.Sign(m_oracle_key)) {
        LogPrintf("Oracle MuSig2: Failed to sign partial sig message for epoch %d\n", m_epoch);
        return;
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << msg;
    std::vector<unsigned char> payload(reinterpret_cast<const unsigned char*>(ss.data()),
                                       reinterpret_cast<const unsigned char*>(ss.data()) + ss.size());
    m_relay(NetMsgType::ORACLEMUSIGPARTIALSIG, payload);
}
