// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_ORACLE_MUSIG2_SESSION_H
#define DIGIBYTE_ORACLE_MUSIG2_SESSION_H

#include <key.h>
#include <sync.h>
#include <uint256.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>

#include <cstdint>
#include <map>
#include <set>
#include <vector>

/**
 * MuSig2 signing session state.
 *
 * Lifecycle:
 *   CREATED → NONCES_COLLECTING → NONCES_COMPLETE → SIGNING → COMPLETE
 *   Any state may transition to FAILED (timeout or error).
 */
enum class MuSig2SessionState {
    CREATED,              //!< Session constructed, no nonce generated yet
    NONCES_COLLECTING,    //!< Local nonce generated, collecting peer pubnonces
    NONCES_COMPLETE,      //!< Enough pubnonces received (>= min_signers)
    SIGNING,              //!< Nonces aggregated, ready for partial signatures
    COMPLETE,             //!< Final aggregate signature produced
    FAILED                //!< Session failed (timeout, error, etc.)
};

/**
 * MuSig2 Signing Session — in-process state machine
 *
 * Manages the two-round MuSig2 (BIP-327) signing protocol for a single epoch.
 * Each session tracks one local signer's secret nonce and collects pubnonces
 * and partial signatures from all participating signers.
 *
 * Protocol flow:
 *   1. GenerateNonce()          — create local secnonce + pubnonce
 *   2. AddPubnonce() × N        — collect pubnonces from all signers
 *   3. AggregateNonces(msg32)   — aggregate nonces + bind to message
 *   4. CreatePartialSignature() — produce local partial sig (zeroes secnonce)
 *   5. AddPartialSignature() × N — collect partial sigs from all signers
 *   6. AggregateSignature()     — produce final 64-byte BIP-340 Schnorr sig
 *
 * SECURITY:
 *   - secp256k1_musig_secnonce is NEVER copied. It lives solely in this object.
 *   - secnonce is zeroed by secp256k1_musig_partial_sign on success, and
 *     explicitly zeroed via memset in the destructor as a safety net.
 *   - GenerateNonce() can only be called once per session.
 *   - CreatePartialSignature() can only be called once per session.
 *
 * Thread safety: all public methods are guarded by m_mutex.
 * This class has NO P2P dependencies — purely in-process.
 */
class MuSig2SigningSession {
public:
    /**
     * @param[in] epoch       The epoch this session is signing for.
     * @param[in] min_signers Minimum number of signers required (e.g., 7 for 7-of-35).
     */
    MuSig2SigningSession(int32_t epoch, uint8_t min_signers, uint32_t attempt_id = 0);
    ~MuSig2SigningSession();

    // Non-copyable (secnonce must not be copied)
    MuSig2SigningSession(const MuSig2SigningSession&) = delete;
    MuSig2SigningSession& operator=(const MuSig2SigningSession&) = delete;

    // Movable
    MuSig2SigningSession(MuSig2SigningSession&& other) noexcept;
    MuSig2SigningSession& operator=(MuSig2SigningSession&& other) noexcept;

    /** Get current session state. */
    MuSig2SessionState GetState() const;

    /** Get the epoch this session is for. */
    int32_t GetEpoch() const;
    /** Get the retry attempt this session is for. */
    uint32_t GetAttemptId() const;

    /**
     * Initialize passive (non-oracle) session: CREATED -> NONCES_COLLECTING without secnonce.
     * Idempotent once local nonce generation has already started the session.
     */
    bool InitializePassive(const secp256k1_musig_keyagg_cache& cache);

    /**
     * Round 1a: Generate local nonce pair for a specific oracle ID.
     * Transitions: CREATED → NONCES_COLLECTING on first call.
     * Can be called multiple times for different oracle IDs on the same node.
     *
     * @param[in]  oracle_id    Oracle ID this nonce is for
     * @param[in]  signing_key  Local signer's private key
     * @param[in]  pubkey       Local signer's public key (secp256k1 format)
     * @param[in]  cache        Key aggregation cache for this signer set
     * @param[out] pubnonce_out Generated public nonce to share with peers
     * @return true on success
     */
    bool GenerateNonce(uint8_t oracle_id,
                       const CKey& signing_key,
                       const secp256k1_pubkey& pubkey,
                       const secp256k1_musig_keyagg_cache& cache,
                       secp256k1_musig_pubnonce& pubnonce_out);

    /**
     * Round 1b: Add a peer's public nonce.
     * Transitions: NONCES_COLLECTING → NONCES_COMPLETE when count >= min_signers.
     *
     * @param[in] oracle_id  Unique signer ID (0-255)
     * @param[in] pubnonce   The signer's public nonce
     * @return true if accepted (false if duplicate ID, wrong state, or invalid)
     */
    bool AddPubnonce(uint8_t oracle_id, const secp256k1_musig_pubnonce& pubnonce);

    /** Check if enough pubnonces have been collected. */
    bool HasEnoughNonces() const;
    size_t GetNonceCount() const;

    /** Override the key aggregation cache (for threshold MuSig2: set to
     *  participants-only aggregate BEFORE calling AggregateNonces). */
    void SetKeyAggCache(const secp256k1_musig_keyagg_cache& cache);
    /** Set the blockchain-derived epoch selection seed used for committee priority. */
    void SetEpochSelectionSeed(const uint256& seed);
    uint256 GetEpochSelectionSeed() const;
    /** Return sorted oracle IDs that contributed nonces. */
    std::vector<uint8_t> GetNonceParticipants() const;
    /** Return the deterministic threshold participant set selected from collected nonces. */
    std::vector<uint8_t> GetRequiredParticipants() const;
    /** True when the supplied participant list is the current deterministic threshold set. */
    bool MatchesRequiredParticipants(const std::vector<uint8_t>& participants) const;
    /**
     * Trim nonces to exactly m_min_signers. Keeps the epoch-scored committee from the collected set.
     * Must be called BEFORE AggregateNonces so the session is bound to
     * exactly the threshold number of participants.
     */
    void TrimNoncesToThreshold();
    /** Freeze an already-selected participant set. */
    bool TrimNoncesToParticipants(const std::vector<uint8_t>& participants);
    bool ComputeContextIdForParticipants(const std::vector<uint8_t>& participants,
                                         const unsigned char* msg32,
                                         uint256& nonce_set_hash_out,
                                         uint256& context_id_out) const;
    /**
     * Aggregate collected pubnonces and initialize signing session.
     * Transitions: NONCES_COMPLETE -> SIGNING.
     *
     * @param[in] msg32 The 32-byte message to sign
     * @return true on success
     */
    bool AggregateNonces(const unsigned char* msg32);

    /**
     * Round 2a: Create local partial signature for a specific oracle ID.
     * Consumes and zeroes that oracle's secret nonce.
     * Can be called once per oracle ID. Requires state == SIGNING.
     *
     * @param[in]  oracle_id       Oracle ID to sign for
     * @param[in]  signing_key     Local signer's private key
     * @param[out] partial_sig_out The produced partial signature
     * @return true on success
     */
    bool CreatePartialSignature(uint8_t oracle_id,
                                const CKey& signing_key,
                                secp256k1_musig_partial_sig& partial_sig_out);

    /**
     * Round 2b: Add a peer's partial signature.
     *
     * @param[in] oracle_id   Unique signer ID (0-255)
     * @param[in] partial_sig The signer's partial signature
     * @return true if accepted (false if duplicate ID or wrong state)
     */
    bool AddPartialSignature(uint8_t oracle_id,
                             const secp256k1_musig_partial_sig& partial_sig);

    /**
     * RC30: verifying variant used on remote partial-sig ingestion.
     * Calls secp256k1_musig_partial_sig_verify against this session's
     * keyagg_cache + aggregate nonce + message. Returns false if the
     * remote signed under a different cache (participant-set mismatch),
     * which prevents silent corruption of the aggregate signature.
     */
    bool AddPartialSignatureVerified(uint8_t oracle_id,
                                     const secp256k1_musig_partial_sig& partial_sig,
                                     const secp256k1_pubkey& signer_pk);

    /** Check if enough partial signatures have been collected. */
    bool HasEnoughPartialSigs() const;
    size_t GetPartialSigCount() const;

    /**
     * Aggregate partial signatures into final 64-byte BIP-340 Schnorr signature.
     * Transitions: SIGNING → COMPLETE.
     *
     * @param[out] sig64_out The 64-byte aggregate signature
     * @return true on success
     */
    bool AggregateSignature(std::vector<unsigned char>& sig64_out);

    /**
     * Check if this session has timed out based on block height.
     * If current_height >= creation_height + timeout_blocks, state → FAILED.
     *
     * @param[in] current_height Current block height
     */
    void CheckTimeout(int32_t current_height);

    /**
     * Set the timeout in blocks from creation height.
     * Default timeout is 20 blocks.
     *
     * @param[in] blocks Number of blocks before timeout
     */
    void SetTimeoutBlocks(int32_t blocks);
    /** TEST ONLY: Set creation height for timeout testing. */
    void SetCreationHeight(int32_t height) { m_creation_height = height; }
    /** Read creation height (lock-protected). Used by operator status. */
    int32_t GetCreationHeight() const;

    /** Get the aggregate signature after COMPLETE state. */
    std::vector<unsigned char> GetAggregateSig() const;
    /** Get participation bitmap from partial sig contributors. */
    std::vector<unsigned char> GetParticipationBitmap() const;

    /** Store the exact values that were signed so the miner can embed them. */
    void SetSignedValues(uint64_t price, int64_t timestamp);
    uint64_t GetSignedPrice() const;
    int64_t GetSignedTimestamp() const;
    uint256 GetSessionContextId() const;
    uint256 GetNonceSetHash() const;
    uint256 GetMessageHash() const;

private:
    mutable Mutex m_mutex;

    std::vector<uint8_t> GetRequiredParticipantsUnsafe() const;

    int32_t m_epoch;                          //!< Epoch this session is signing for
    uint32_t m_attempt_id;                    //!< Retry attempt inside the epoch
    uint8_t m_min_signers;                    //!< Minimum signers required
    MuSig2SessionState m_state GUARDED_BY(m_mutex);

    secp256k1_context* m_ctx;                 //!< secp256k1 context (owned)

    //! Local signer secret nonces — one per oracle ID on this node.
    //! Each secnonce MUST NOT be copied and is zeroed after signing.
    std::map<uint8_t, secp256k1_musig_secnonce> m_secnonces GUARDED_BY(m_mutex);
    std::set<uint8_t> m_secnonces_used GUARDED_BY(m_mutex); //!< Oracle IDs whose nonces are consumed

    //! Key aggregation cache (set during GenerateNonce)
    secp256k1_musig_keyagg_cache m_keyagg_cache GUARDED_BY(m_mutex);

    //! Collected public nonces, keyed by oracle_id
    std::map<uint8_t, secp256k1_musig_pubnonce> m_pubnonces GUARDED_BY(m_mutex);
    bool m_participants_frozen GUARDED_BY(m_mutex){false};
    uint256 m_epoch_selection_seed GUARDED_BY(m_mutex);

    //! Aggregate nonce (computed from collected pubnonces)
    secp256k1_musig_aggnonce m_aggnonce GUARDED_BY(m_mutex);

    //! secp256k1 MuSig2 session state (after nonce_process)
    secp256k1_musig_session m_session GUARDED_BY(m_mutex);

    //! Collected partial signatures, keyed by oracle_id
    std::map<uint8_t, secp256k1_musig_partial_sig> m_partial_sigs GUARDED_BY(m_mutex);

    std::vector<unsigned char> m_aggregate_sig GUARDED_BY(m_mutex); //!< Cached agg sig
    uint64_t m_signed_price{0};     //!< The exact price that was signed
    int64_t m_signed_timestamp{0};  //!< The exact timestamp that was signed
    uint256 m_message_hash GUARDED_BY(m_mutex);
    uint256 m_nonce_set_hash GUARDED_BY(m_mutex);
    uint256 m_session_context_id GUARDED_BY(m_mutex);

    //! Timeout configuration
    int32_t m_creation_height GUARDED_BY(m_mutex); //!< Height at which session was created (= epoch)
    int32_t m_timeout_blocks GUARDED_BY(m_mutex);  //!< Blocks before timeout (default 20)
};

#endif // DIGIBYTE_ORACLE_MUSIG2_SESSION_H
