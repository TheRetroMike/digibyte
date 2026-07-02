// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_ORACLE_MUSIG2_ORACLE_PARTICIPATION_H
#define DIGIBYTE_ORACLE_MUSIG2_ORACLE_PARTICIPATION_H

#include <functional>
#include <memory>
#include <vector>

#include <key.h>
#include <oracle/musig2_messages.h>
#include <oracle/musig2_session.h>
#include <primitives/oracle.h>
#include <sync.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>

/**
 * MuSig2 Oracle Participation — manages an oracle's participation in
 * the two-round MuSig2 signing protocol for V1 oracle bundles.
 *
 * Lifecycle per epoch:
 *   1. OnBlockConnected() detects new epoch → generates nonce, broadcasts it
 *   2. OnOracleMusigNonce() collects remote nonces
 *   3. SetConsensusValues() triggers signing when consensus + nonces ready
 *   4. OnOracleMusigPartialSig() collects remote partial sigs
 *   5. GetCurrentBundle() returns a completed v0x03 bundle or an empty v0x03 placeholder
 *
 * Thread-safe: all public methods are guarded by m_mtx.
 */
class MuSig2OracleParticipation
{
public:
    MuSig2OracleParticipation();
    ~MuSig2OracleParticipation();

    // Non-copyable
    MuSig2OracleParticipation(const MuSig2OracleParticipation&) = delete;
    MuSig2OracleParticipation& operator=(const MuSig2OracleParticipation&) = delete;

    /**
     * Initialize with oracle key and the full set of oracle pubkeys.
     * @param[in] oracle_key     This oracle's private key
     * @param[in] oracle_id      This oracle's ID (index into pubkeys)
     * @param[in] oracle_pubkeys All oracle public keys in deterministic order
     * @param[in] min_signers    Minimum signers for quorum (default 9)
     */
    void Initialize(const CKey& oracle_key, uint8_t oracle_id,
                    const std::vector<secp256k1_pubkey>& oracle_pubkeys,
                    uint8_t min_signers = 9);

    /**
     * Called on every new block tip.
     * Checks epoch, starts new session if needed, checks timeouts.
     */
    void OnBlockConnected(int32_t height);

    /** Process an incoming MuSig2 nonce from a remote oracle (P2P Round 1). */
    void OnOracleMusigNonce(const OracleMusigNonceMsg& msg);

    /** Process an incoming MuSig2 partial sig from a remote oracle (P2P Round 2). */
    void OnOracleMusigPartialSig(const OracleMusigPartialSigMsg& msg);

    /**
     * Set consensus values for current epoch. When nonces are ready,
     * triggers AggregateNonces + CreatePartialSignature + broadcast.
     */
    void SetConsensusValues(int32_t epoch, uint64_t price, int64_t timestamp);

    /**
     * Get the completed MuSig2 bundle, or an empty v0x03 bundle if none is ready.
     * Called by bundle_manager::AddOracleBundleToBlock().
     */
    COracleBundle GetCurrentBundle(int32_t height);

    /** Get session state (FAILED if no session). */
    MuSig2SessionState GetSessionState() const;

    /** Get current session epoch (-1 if no session). */
    int32_t GetSessionEpoch() const;

    /** Deprecated no-op retained for old callers; V1 stores only MuSig2 bundles. */
    void SetLatestV02Bundle(const COracleBundle& bundle, int32_t height);

    /** Relay callback for broadcasting MuSig2 messages to P2P. */
    using RelayCallback = std::function<void(const std::string& msg_type,
                                              const std::vector<unsigned char>& payload)>;
    void SetRelayCallback(RelayCallback callback);

    /** Check if initialized. */
    bool IsInitialized() const;

private:
    void TryAdvanceToSigning() EXCLUSIVE_LOCKS_REQUIRED(m_mtx);
    void TryAggregateSignature() EXCLUSIVE_LOCKS_REQUIRED(m_mtx);
    void BroadcastNonce(const secp256k1_musig_pubnonce& pubnonce) EXCLUSIVE_LOCKS_REQUIRED(m_mtx);
    void BroadcastPartialSig(const secp256k1_musig_partial_sig& psig) EXCLUSIVE_LOCKS_REQUIRED(m_mtx);

    mutable Mutex m_mtx;

    bool m_initialized{false};
    CKey m_oracle_key;
    uint8_t m_oracle_id{0};
    uint8_t m_min_signers{9};
    std::vector<secp256k1_pubkey> m_pubkeys;
    secp256k1_musig_keyagg_cache m_cache;
    secp256k1_context* m_ctx{nullptr};

    // Current session state
    std::unique_ptr<MuSig2SigningSession> m_session GUARDED_BY(m_mtx);
    int32_t m_epoch GUARDED_BY(m_mtx){-1};

    // Consensus values for current epoch
    bool m_has_consensus GUARDED_BY(m_mtx){false};
    uint64_t m_consensus_price GUARDED_BY(m_mtx){0};
    int64_t m_consensus_timestamp GUARDED_BY(m_mtx){0};

    // P2P relay callback
    RelayCallback m_relay;
};

#endif // DIGIBYTE_ORACLE_MUSIG2_ORACLE_PARTICIPATION_H
