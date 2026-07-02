// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_ORACLE_SIGNING_ORCHESTRATOR_H
#define DIGIBYTE_ORACLE_SIGNING_ORCHESTRATOR_H

#include <chain.h>
#include <key.h>
#include <oracle/musig2_aggregator.h>
#include <oracle/musig2_messages.h>
#include <oracle/musig2_session.h>
#include <validationinterface.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

class CBlock;
class CConnman;

/**
 * MuSig2 Signing Orchestrator
 *
 * Drives the MuSig2 signing protocol on every block tick via
 * CValidationInterface::BlockConnected.
 *
 * Oracle nodes:  generate nonces, create partial sigs, broadcast.
 * Non-oracle:    collect nonces/sigs, aggregate final signature.
 */
class OracleSigningOrchestrator : public CValidationInterface
{
public:
    OracleSigningOrchestrator();
    ~OracleSigningOrchestrator();

    OracleSigningOrchestrator(const OracleSigningOrchestrator&) = delete;
    OracleSigningOrchestrator& operator=(const OracleSigningOrchestrator&) = delete;

    // Oracle detection
    bool IsOracleNode() const;
    const CKey* GetOracleSigningKey() const;
    uint8_t GetOracleId() const;

    // Session management
    MuSig2SigningSession* GetOrCreateSigningSession(int32_t epoch, int32_t block_height = 0);
    uint8_t GetActiveAttemptId(int32_t epoch) const;
    void CleanupOldSessions(int32_t current_epoch);
    /** Read-only session existence check — used by tests and diagnostics. */
    bool HasSession(int32_t epoch) const;

    // Block-tick orchestration
    void OnBlockConnected(const std::shared_ptr<const CBlock>& block, int32_t block_height);

    // P2P broadcast
    bool BroadcastMusigNonce(const OracleMusigNonceMsg& msg);
    bool BroadcastMusigContext(const OracleMusigContextMsg& msg);
    bool BroadcastMusigPartialSig(const OracleMusigPartialSigMsg& msg);
    void SetConnman(CConnman* connman) { m_connman = connman; }

    // Query completed session for block assembly
    bool GetCompletedSession(int32_t epoch,
                             std::vector<unsigned char>& aggregate_sig_out,
                             std::vector<unsigned char>& participation_bitmap_out,
                             uint64_t& signed_price_out,
                             int64_t& signed_timestamp_out) const;

    /**
     * Wave 10 (Agent C) — operator/diagnostic visibility into MuSig2 session
     * state for an epoch. Operators reading the RPC surface need to know
     * whether the orchestrator has reached COMPLETE for the current epoch,
     * is stuck waiting for nonces, has timed out (FAILED), or has not yet
     * created a session at all.
     */
    struct SessionStatus {
        MuSig2SessionState state;
        size_t nonce_count;
        size_t partial_sig_count;
        int32_t creation_height;
    };
    std::optional<SessionStatus> GetSessionStateForEpoch(int32_t epoch) const;

    // Utilities
    static int32_t ComputeEpoch(int32_t block_height, int32_t epoch_length);
    static void ComputeOracleMessageHash(int32_t epoch, uint64_t price,
                                         int64_t timestamp,
                                         unsigned char hash32[32]);
    static std::vector<uint8_t> GetConsensusOracleIdsForSigning();

    // Lifecycle
    void Start();
    void Stop();
    void Clear();

    /** Inject a pre-built session (test use only). Takes ownership. */
    void InjectSession(int32_t epoch, std::unique_ptr<MuSig2SigningSession> session);
    /** Test/diagnostic visibility for bounded pending context proposals. */
    size_t GetPendingContextProposalCountForTesting(int32_t epoch) const;

    /** Ingest remote nonce from P2P (called from net_processing). */
    void IngestRemoteNonce(const OracleMusigNonceMsg& msg);
    /** Ingest remote context proposal from P2P (called from net_processing). */
    void IngestRemoteContext(const OracleMusigContextMsg& msg);
    /** Ingest remote partial sig from P2P (called from net_processing). */
    void IngestRemotePartialSig(const OracleMusigPartialSigMsg& msg);

    static OracleSigningOrchestrator& GetInstance();
    static void Initialize();
    static void Shutdown();

protected:
    void BlockConnected(ChainstateRole role,
                        const std::shared_ptr<const CBlock>& block,
                        const CBlockIndex* pindex) override;

private:
    /**
     * Run one tick of the MuSig2 ceremony for a specific epoch's session.
     * Called by OnBlockConnected for both the current epoch and, in the
     * last K blocks of an epoch, for the upcoming epoch so the ceremony
     * reaches COMPLETE before the first block of the next epoch is
     * templated by miners.
     */
    void TickEpochSession(int32_t epoch, int32_t block_height);
    bool RestartEpochAttemptIfNeeded(int32_t epoch, int32_t block_height, MuSig2SigningSession*& session);
    bool HasSelectionSeedForEpoch(int32_t epoch) const;
    uint256 GetSelectionSeedForEpoch(int32_t epoch) const;
    void SetEpochSelectionSeed(int32_t epoch, const uint256& seed);
    bool IsEligibleContextProposer(int32_t epoch, uint8_t proposer_id, int32_t block_height) const;
    bool ValidateContextProposal(const OracleMusigContextMsg& msg,
                                 MuSig2SigningSession& session) const;
    bool AddNonceEvidenceToSession(const OracleMusigNonceMsg& msg,
                                   MuSig2SigningSession& session) const;
    std::optional<OracleMusigContextMsg> SelectReadyContextProposal(int32_t epoch,
                                                                    int32_t block_height,
                                                                    MuSig2SigningSession& session) const;
    std::optional<OracleMusigContextMsg> BuildLocalContextProposal(int32_t epoch,
                                                                   int32_t block_height,
                                                                   MuSig2SigningSession& session);
    bool TryApplyRemotePartialSig(const OracleMusigPartialSigMsg& msg,
                                  MuSig2SigningSession& session) const;
    void BufferPendingPartialSig(const OracleMusigPartialSigMsg& msg);
    size_t DrainPendingPartialSigsForEpoch(int32_t epoch, MuSig2SigningSession& session);

    CConnman* m_connman{nullptr};
    std::map<int32_t, std::unique_ptr<MuSig2SigningSession>> m_signing_sessions;
    mutable std::mutex m_sessions_mutex;
    std::unique_ptr<MuSig2OracleAggregator> m_aggregator;
    std::map<int32_t, std::set<uint8_t>> m_nonce_broadcast_tracker;
    std::map<int32_t, std::set<uint8_t>> m_context_broadcast_tracker;
    std::map<int32_t, std::set<uint8_t>> m_partialsig_broadcast_tracker;
    std::map<int32_t, std::map<uint256, OracleMusigContextMsg>> m_pending_contexts;
    std::map<int32_t, uint256> m_epoch_selection_seeds;
    std::map<int32_t, uint8_t> m_epoch_attempts;
    std::map<int32_t, std::map<uint8_t, OracleMusigNonceMsg>> m_nonce_evidence;
    // Buffer context-bound partial sigs that arrive before local session enters SIGNING.
    std::map<int32_t, std::map<uint256, std::vector<OracleMusigPartialSigMsg>>> m_pending_partialsigs;
    mutable std::unique_ptr<CKey> m_cached_oracle_key;
    mutable uint8_t m_cached_oracle_id{255};
    mutable bool m_oracle_key_cached{false};
    bool m_started{false};
};

extern std::unique_ptr<OracleSigningOrchestrator> g_signing_orchestrator;

#endif // DIGIBYTE_ORACLE_SIGNING_ORCHESTRATOR_H
