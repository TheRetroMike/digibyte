// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_ORACLE_MUSIG2_MESSAGES_H
#define DIGIBYTE_ORACLE_MUSIG2_MESSAGES_H

#include <hash.h>
#include <key.h>
#include <primitives/oracle.h>
#include <pubkey.h>
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

class CChainParams;

bool IsAuthorizedMuSig2OracleIdForRelay(const CChainParams& params, uint32_t oracle_id);
bool IsMuSig2RelayEpochInRange(int32_t message_epoch, int32_t current_epoch);
static constexpr uint8_t ORACLE_MUSIG2_SESSION_CONTEXT_VERSION = 2;

/**
 * MuSig2 Nonce Message for P2P Network (Round 1)
 * Carries an oracle's public nonce for the MuSig2 signing protocol.
 */
class OracleMusigNonceMsg
{
public:
    int32_t epoch{0};
    uint8_t attempt_id{0};
    uint8_t oracle_id{0};
    std::vector<unsigned char> pubnonce;  // 66 bytes serialized secp256k1_musig_pubnonce
    std::vector<unsigned char> signature; // 64 bytes Schnorr signature (RH-24)

    SERIALIZE_METHODS(OracleMusigNonceMsg, obj)
    {
        READWRITE(obj.epoch, obj.attempt_id, obj.oracle_id, obj.pubnonce, obj.signature);
    }

    uint256 GetHash() const;

    /** Hash of fields covered by the authentication signature (excludes signature itself). */
    uint256 GetSignatureHash() const;

    /** Sign this message with the oracle's private key. */
    bool Sign(const CKey& key);

    /** Verify the authentication signature against the given oracle pubkey. */
    bool VerifySignature(const XOnlyPubKey& pubkey) const;

    bool IsValid() const { return pubnonce.size() == 66 && oracle_id < 255 && signature.size() == 64; }
};

/**
 * MuSig2 Partial Signature Message for P2P Network (Round 2)
 * Carries an oracle's partial signature for the MuSig2 signing protocol.
 */
class OracleMusigPartialSigMsg
{
public:
    int32_t epoch{0};
    uint8_t attempt_id{0};
    uint8_t context_version{ORACLE_MUSIG2_SESSION_CONTEXT_VERSION};
    uint256 session_context_id;
    uint8_t oracle_id{0};
    std::vector<unsigned char> partial_sig;  // 32 bytes serialized secp256k1_musig_partial_sig
    std::vector<unsigned char> signature;    // 64 bytes Schnorr signature (RH-24)

    SERIALIZE_METHODS(OracleMusigPartialSigMsg, obj)
    {
        READWRITE(obj.epoch, obj.attempt_id, obj.context_version, obj.session_context_id,
                  obj.oracle_id, obj.partial_sig, obj.signature);
    }

    uint256 GetHash() const;

    /** Hash of fields covered by the authentication signature (excludes signature itself). */
    uint256 GetSignatureHash() const;

    /** Sign this message with the oracle's private key. */
    bool Sign(const CKey& key);

    /** Verify the authentication signature against the given oracle pubkey. */
    bool VerifySignature(const XOnlyPubKey& pubkey) const;

    bool IsValid() const { return partial_sig.size() == 32 && oracle_id < 255 && signature.size() == 64; }
};

/**
 * MuSig2 Context Proposal Message
 *
 * Announces the exact signer set and price/timestamp that define a MuSig2
 * session context before partial signatures are broadcast. This gives honest
 * nodes one verifiable transcript to join instead of independently freezing
 * different local nonce views.
 */
class OracleMusigContextMsg
{
public:
    int32_t epoch{0};
    uint8_t attempt_id{0};
    uint8_t context_version{ORACLE_MUSIG2_SESSION_CONTEXT_VERSION};
    uint256 epoch_selection_seed;
    uint8_t proposer_id{0};
    std::vector<uint8_t> participant_ids;
    uint256 nonce_set_hash;
    uint256 quote_set_hash;
    uint64_t consensus_price{0};
    int64_t consensus_timestamp{0};
    uint256 session_context_id;
    std::vector<OracleMusigNonceMsg> nonce_evidence;
    std::vector<COraclePriceMessage> price_evidence;
    std::vector<unsigned char> signature; // 64 bytes Schnorr signature

    SERIALIZE_METHODS(OracleMusigContextMsg, obj)
    {
        READWRITE(obj.epoch, obj.attempt_id, obj.context_version, obj.epoch_selection_seed,
                  obj.proposer_id, obj.participant_ids, obj.nonce_set_hash,
                  obj.quote_set_hash, obj.consensus_price, obj.consensus_timestamp,
                  obj.session_context_id, obj.nonce_evidence, obj.price_evidence,
                  obj.signature);
    }

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;
    bool Sign(const CKey& key);
    bool VerifySignature(const XOnlyPubKey& pubkey) const;

    bool IsValid() const
    {
        return context_version == ORACLE_MUSIG2_SESSION_CONTEXT_VERSION &&
               !epoch_selection_seed.IsNull() &&
               proposer_id < 255 &&
               !participant_ids.empty() &&
               participant_ids.size() <= 32 &&
               nonce_evidence.size() <= 32 &&
               price_evidence.size() <= 32 &&
               !session_context_id.IsNull() &&
               signature.size() == 64;
    }
};

#endif // DIGIBYTE_ORACLE_MUSIG2_MESSAGES_H
