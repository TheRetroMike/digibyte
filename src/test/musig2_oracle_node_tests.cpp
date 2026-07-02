// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * MuSig2OracleParticipation Tests (TDD — tests before implementation)
 *
 * Tests the oracle's participation in the MuSig2 signing protocol:
 * - Epoch detection and session creation
 * - Nonce generation and P2P broadcast
 * - Remote nonce collection
 * - Partial signature generation after threshold nonces
 * - Partial signature broadcast
 * - Remote partial sig collection
 * - v0x03 bundle creation on signing completion
 * - no legacy fallback when session incomplete
 * - Session timeout handling
 * - Quorum resilience with offline peers
 */

#include <boost/test/unit_test.hpp>

#include <hash.h>
#include <key.h>
#include <oracle/musig2_messages.h>
#include <oracle/musig2_oracle_participation.h>
#include <oracle/musig2_session.h>
#include <protocol.h>
#include <primitives/oracle.h>
#include <random.h>
#include <streams.h>
#include <test/util/setup_common.h>

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <cstring>
#include <functional>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(musig2_oracle_node_tests, BasicTestingSetup)

// ============================================================================
// Test helpers
// ============================================================================

static constexpr size_t TEST_N_ORACLES = 15;
static constexpr uint8_t TEST_MIN_SIGNERS = 9;

static bool MakeRandomKeypair(secp256k1_context* ctx,
                              unsigned char seckey[32],
                              secp256k1_keypair* keypair,
                              secp256k1_pubkey* pubkey)
{
    GetStrongRandBytes(Span{seckey, 32});
    if (!secp256k1_keypair_create(ctx, keypair, seckey)) return false;
    if (!secp256k1_keypair_pub(ctx, pubkey, keypair)) return false;
    return true;
}

static CKey MakeCKey(const unsigned char seckey[32])
{
    CKey key;
    key.Set(seckey, seckey + 32, true);
    return key;
}

struct OracleTestHarness {
    secp256k1_context* ctx;
    unsigned char seckeys[TEST_N_ORACLES][32];
    secp256k1_keypair keypairs[TEST_N_ORACLES];
    secp256k1_pubkey pubkeys[TEST_N_ORACLES];
    CKey ckeys[TEST_N_ORACLES];
    secp256k1_xonly_pubkey agg_pk;
    secp256k1_musig_keyagg_cache cache;

    std::vector<OracleMusigNonceMsg> relayed_nonces;
    std::vector<OracleMusigPartialSigMsg> relayed_partial_sigs;

    OracleTestHarness()
    {
        ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        for (size_t i = 0; i < TEST_N_ORACLES; i++) {
            BOOST_REQUIRE(MakeRandomKeypair(ctx, seckeys[i], &keypairs[i], &pubkeys[i]));
            ckeys[i] = MakeCKey(seckeys[i]);
        }
        std::vector<const secp256k1_pubkey*> pubkey_ptrs(TEST_N_ORACLES);
        for (size_t i = 0; i < TEST_N_ORACLES; i++) pubkey_ptrs[i] = &pubkeys[i];
        BOOST_REQUIRE(secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), TEST_N_ORACLES));
    }

    ~OracleTestHarness()
    {
        if (ctx) secp256k1_context_destroy(ctx);
    }

    std::vector<secp256k1_pubkey> GetPubkeyVector() const
    {
        return std::vector<secp256k1_pubkey>(pubkeys, pubkeys + TEST_N_ORACLES);
    }

    void SetupNode(MuSig2OracleParticipation& node, size_t oracle_idx)
    {
        node.Initialize(ckeys[oracle_idx], static_cast<uint8_t>(oracle_idx),
                        GetPubkeyVector(), TEST_MIN_SIGNERS);
        node.SetRelayCallback([this](const std::string& msg_type, const std::vector<unsigned char>& payload) {
            CDataStream ss(payload, SER_NETWORK, PROTOCOL_VERSION);
            if (msg_type == NetMsgType::ORACLEMUSIGNONCE) {
                OracleMusigNonceMsg msg;
                ss >> msg;
                relayed_nonces.push_back(msg);
            } else if (msg_type == NetMsgType::ORACLEMUSIGPARTIALSIG) {
                OracleMusigPartialSigMsg msg;
                ss >> msg;
                relayed_partial_sigs.push_back(msg);
            }
        });
    }

    void ComputeMsg32(int32_t epoch, uint64_t price, int64_t timestamp, unsigned char msg32[32]) const
    {
        CHashWriter hasher(0);
        hasher << epoch << price << timestamp;
        uint256 hash = hasher.GetHash();
        memcpy(msg32, hash.begin(), 32);
    }

    bool GenerateExternalNonce(size_t oracle_idx,
                               secp256k1_musig_secnonce& secnonce_out,
                               secp256k1_musig_pubnonce& pubnonce_out)
    {
        unsigned char rand[32];
        GetStrongRandBytes(Span{rand, 32});
        return secp256k1_musig_nonce_gen(ctx, &secnonce_out, &pubnonce_out,
                                          rand, seckeys[oracle_idx], &pubkeys[oracle_idx],
                                          nullptr, &cache, nullptr);
    }

    std::vector<unsigned char> SerializePubnonce(const secp256k1_musig_pubnonce& pubnonce) const
    {
        std::vector<unsigned char> out(66);
        BOOST_REQUIRE(secp256k1_musig_pubnonce_serialize(ctx, out.data(), &pubnonce));
        return out;
    }

    std::vector<unsigned char> SerializePartialSig(const secp256k1_musig_partial_sig& psig) const
    {
        std::vector<unsigned char> out(32);
        BOOST_REQUIRE(secp256k1_musig_partial_sig_serialize(ctx, out.data(), &psig));
        return out;
    }
};

// ============================================================================
// test_oracle_node_create_new_session_on_epoch_tick
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_create_new_session_on_epoch_tick)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::FAILED);
    BOOST_CHECK_EQUAL(node.GetSessionEpoch(), -1);

    node.OnBlockConnected(100);

    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::NONCES_COLLECTING);
    BOOST_CHECK(node.GetSessionEpoch() > 0);
}

// ============================================================================
// test_oracle_node_nonce_broadcast
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_nonce_broadcast)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    node.OnBlockConnected(100);

    BOOST_CHECK_EQUAL(h.relayed_nonces.size(), 1U);
    BOOST_CHECK_EQUAL(h.relayed_nonces[0].oracle_id, 0);
    BOOST_CHECK_EQUAL(h.relayed_nonces[0].pubnonce.size(), 66U);
    BOOST_CHECK(h.relayed_nonces[0].IsValid());
    BOOST_CHECK_EQUAL(h.relayed_nonces[0].epoch, node.GetSessionEpoch());
}

// ============================================================================
// test_oracle_node_collect_remote_nonces
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_collect_remote_nonces)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    node.OnBlockConnected(100);
    int32_t epoch = node.GetSessionEpoch();

    for (size_t i = 1; i <= 8; i++) {
        secp256k1_musig_secnonce sn;
        secp256k1_musig_pubnonce pn;
        BOOST_REQUIRE(h.GenerateExternalNonce(i, sn, pn));

        OracleMusigNonceMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.pubnonce = h.SerializePubnonce(pn);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigNonce(msg);
    }

    // 1 local + 8 remote = 9 >= threshold
    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::NONCES_COMPLETE ||
                node.GetSessionState() == MuSig2SessionState::SIGNING);
}

// ============================================================================
// test_oracle_node_partial_sig_generation
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_partial_sig_generation)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    node.OnBlockConnected(100);
    int32_t epoch = node.GetSessionEpoch();

    secp256k1_musig_secnonce ext_secnonces[TEST_N_ORACLES];
    secp256k1_musig_pubnonce ext_pubnonces[TEST_N_ORACLES];
    for (size_t i = 1; i <= 8; i++) {
        BOOST_REQUIRE(h.GenerateExternalNonce(i, ext_secnonces[i], ext_pubnonces[i]));
        OracleMusigNonceMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.pubnonce = h.SerializePubnonce(ext_pubnonces[i]);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigNonce(msg);
    }

    uint64_t price = 1500000;
    int64_t timestamp = 1700000000;
    node.SetConsensusValues(epoch, price, timestamp);

    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::SIGNING);
    BOOST_CHECK_EQUAL(h.relayed_partial_sigs.size(), 1U);
    BOOST_CHECK_EQUAL(h.relayed_partial_sigs[0].oracle_id, 0);
    BOOST_CHECK_EQUAL(h.relayed_partial_sigs[0].partial_sig.size(), 32U);
    BOOST_CHECK(h.relayed_partial_sigs[0].IsValid());
}

// ============================================================================
// test_oracle_node_partial_sig_broadcast
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_partial_sig_broadcast)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    node.OnBlockConnected(100);
    int32_t epoch = node.GetSessionEpoch();

    secp256k1_musig_secnonce ext_secnonces[TEST_N_ORACLES];
    secp256k1_musig_pubnonce ext_pubnonces[TEST_N_ORACLES];
    for (size_t i = 1; i <= 8; i++) {
        BOOST_REQUIRE(h.GenerateExternalNonce(i, ext_secnonces[i], ext_pubnonces[i]));
        OracleMusigNonceMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.pubnonce = h.SerializePubnonce(ext_pubnonces[i]);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigNonce(msg);
    }

    node.SetConsensusValues(epoch, 1500000, 1700000000);

    BOOST_REQUIRE_EQUAL(h.relayed_partial_sigs.size(), 1U);
    const auto& psig_msg = h.relayed_partial_sigs[0];
    BOOST_CHECK_EQUAL(psig_msg.epoch, epoch);
    BOOST_CHECK_EQUAL(psig_msg.oracle_id, 0);
    BOOST_CHECK_EQUAL(psig_msg.partial_sig.size(), 32U);
}

// ============================================================================
// test_oracle_node_collect_remote_partial_sigs
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_collect_remote_partial_sigs)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    node.OnBlockConnected(100);
    int32_t epoch = node.GetSessionEpoch();

    uint64_t price = 1500000;
    int64_t timestamp = 1700000000;

    // Collect nonces from remote oracles 1-8
    secp256k1_musig_secnonce ext_secnonces[TEST_N_ORACLES];
    secp256k1_musig_pubnonce all_pubnonces[TEST_N_ORACLES];
    for (size_t i = 1; i <= 8; i++) {
        BOOST_REQUIRE(h.GenerateExternalNonce(i, ext_secnonces[i], all_pubnonces[i]));
        OracleMusigNonceMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.pubnonce = h.SerializePubnonce(all_pubnonces[i]);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigNonce(msg);
    }

    // Set consensus → triggers partial sig generation
    node.SetConsensusValues(epoch, price, timestamp);
    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::SIGNING);

    // Get oracle 0's pubnonce from relay capture
    BOOST_REQUIRE_EQUAL(h.relayed_nonces.size(), 1U);
    secp256k1_musig_pubnonce local_pubnonce;
    BOOST_REQUIRE(secp256k1_musig_pubnonce_parse(h.ctx, &local_pubnonce, h.relayed_nonces[0].pubnonce.data()));
    all_pubnonces[0] = local_pubnonce;

    // Build external signing session for simulated remote oracles
    std::vector<const secp256k1_musig_pubnonce*> pn_ptrs(9);
    for (size_t i = 0; i < 9; i++) pn_ptrs[i] = &all_pubnonces[i];
    secp256k1_musig_aggnonce aggnonce;
    BOOST_REQUIRE(secp256k1_musig_nonce_agg(h.ctx, &aggnonce, pn_ptrs.data(), 9));

    unsigned char msg32[32];
    h.ComputeMsg32(epoch, price, timestamp, msg32);
    secp256k1_musig_session raw_session;
    BOOST_REQUIRE(secp256k1_musig_nonce_process(h.ctx, &raw_session, &aggnonce, msg32, &h.cache));

    // Deliver partial sigs from oracles 1-8
    for (size_t i = 1; i <= 8; i++) {
        secp256k1_musig_partial_sig psig;
        BOOST_REQUIRE(secp256k1_musig_partial_sign(h.ctx, &psig, &ext_secnonces[i],
                                                    &h.keypairs[i], &h.cache, &raw_session));
        OracleMusigPartialSigMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.partial_sig = h.SerializePartialSig(psig);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigPartialSig(msg);
    }

    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::COMPLETE);
}

// ============================================================================
// test_oracle_node_no_legacy_fallback
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_no_legacy_fallback)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    COracleBundle v02_bundle;
    v02_bundle.version = 2;
    v02_bundle.epoch = 10;
    v02_bundle.median_price_micro_usd = 1500000;
    v02_bundle.timestamp = 1700000000;
    node.SetLatestV02Bundle(v02_bundle, 100);

    node.OnBlockConnected(100);

    COracleBundle bundle = node.GetCurrentBundle(100);
    BOOST_CHECK_EQUAL(bundle.version, 3);
    BOOST_CHECK(bundle.IsMuSig2());
    BOOST_CHECK(bundle.aggregate_sig.empty());
    BOOST_CHECK(bundle.participation_bitmap.empty());
    BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 0U);
}

// ============================================================================
// test_oracle_node_v03_bundle_preference
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_v03_bundle_preference)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    uint64_t price = 1500000;
    int64_t timestamp = 1700000000;

    COracleBundle v02_bundle;
    v02_bundle.version = 2;
    v02_bundle.epoch = 10;
    v02_bundle.median_price_micro_usd = price;
    v02_bundle.timestamp = timestamp;
    node.SetLatestV02Bundle(v02_bundle, 100);

    node.OnBlockConnected(100);
    int32_t epoch = node.GetSessionEpoch();

    // Deliver nonces from 8 remote oracles
    secp256k1_musig_secnonce ext_secnonces[TEST_N_ORACLES];
    secp256k1_musig_pubnonce all_pubnonces[TEST_N_ORACLES];
    for (size_t i = 1; i <= 8; i++) {
        BOOST_REQUIRE(h.GenerateExternalNonce(i, ext_secnonces[i], all_pubnonces[i]));
        OracleMusigNonceMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.pubnonce = h.SerializePubnonce(all_pubnonces[i]);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigNonce(msg);
    }

    node.SetConsensusValues(epoch, price, timestamp);

    // Build external signing session
    BOOST_REQUIRE_EQUAL(h.relayed_nonces.size(), 1U);
    secp256k1_musig_pubnonce local_pubnonce;
    BOOST_REQUIRE(secp256k1_musig_pubnonce_parse(h.ctx, &local_pubnonce, h.relayed_nonces[0].pubnonce.data()));
    all_pubnonces[0] = local_pubnonce;

    std::vector<const secp256k1_musig_pubnonce*> pn_ptrs(9);
    for (size_t i = 0; i < 9; i++) pn_ptrs[i] = &all_pubnonces[i];
    secp256k1_musig_aggnonce aggnonce;
    BOOST_REQUIRE(secp256k1_musig_nonce_agg(h.ctx, &aggnonce, pn_ptrs.data(), 9));

    unsigned char msg32[32];
    h.ComputeMsg32(epoch, price, timestamp, msg32);
    secp256k1_musig_session raw_session;
    BOOST_REQUIRE(secp256k1_musig_nonce_process(h.ctx, &raw_session, &aggnonce, msg32, &h.cache));

    for (size_t i = 1; i <= 8; i++) {
        secp256k1_musig_partial_sig psig;
        BOOST_REQUIRE(secp256k1_musig_partial_sign(h.ctx, &psig, &ext_secnonces[i],
                                                    &h.keypairs[i], &h.cache, &raw_session));
        OracleMusigPartialSigMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.partial_sig = h.SerializePartialSig(psig);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigPartialSig(msg);
    }

    BOOST_REQUIRE(node.GetSessionState() == MuSig2SessionState::COMPLETE);

    COracleBundle bundle = node.GetCurrentBundle(100);
    BOOST_CHECK_EQUAL(bundle.version, 3);
    BOOST_CHECK(bundle.IsMuSig2());
    BOOST_CHECK_EQUAL(bundle.aggregate_sig.size(), 64U);
    BOOST_CHECK(!bundle.participation_bitmap.empty());
    BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, price);
    BOOST_CHECK_EQUAL(bundle.timestamp, timestamp);
}

// ============================================================================
// test_oracle_node_session_timeout
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_session_timeout)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    COracleBundle v02_bundle;
    v02_bundle.version = 2;
    v02_bundle.epoch = 10;
    v02_bundle.median_price_micro_usd = 1500000;
    v02_bundle.timestamp = 1700000000;
    node.SetLatestV02Bundle(v02_bundle, 100);

    const int32_t start_height = 100;
    const int32_t timeout_height = start_height + 1;
    BOOST_REQUIRE_EQUAL(GetCurrentEpoch(start_height), GetCurrentEpoch(timeout_height));

    node.OnBlockConnected(start_height);
    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::NONCES_COLLECTING);

    // Keep the timeout check in the same RC34 oracle epoch. Advancing to 121
    // would start the next 40-block epoch and correctly replace the session.
    node.OnBlockConnected(timeout_height);

    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::FAILED);

    COracleBundle bundle = node.GetCurrentBundle(timeout_height);
    BOOST_CHECK_EQUAL(bundle.version, 3);
    BOOST_CHECK(bundle.IsMuSig2());
    BOOST_CHECK(bundle.aggregate_sig.empty());
    BOOST_CHECK(bundle.participation_bitmap.empty());
    BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 0U);
}

// ============================================================================
// test_oracle_node_offline_peer_9_of_15_quorum
// ============================================================================
BOOST_AUTO_TEST_CASE(test_oracle_node_offline_peer_9_of_15_quorum)
{
    OracleTestHarness h;
    MuSig2OracleParticipation node;
    h.SetupNode(node, 0);

    node.OnBlockConnected(100);
    int32_t epoch = node.GetSessionEpoch();

    uint64_t price = 1500000;
    int64_t timestamp = 1700000000;

    // Only 8 remote + 1 local = 9 exactly at threshold, 6 offline
    secp256k1_musig_secnonce ext_secnonces[TEST_N_ORACLES];
    secp256k1_musig_pubnonce all_pubnonces[TEST_N_ORACLES];
    for (size_t i = 1; i <= 8; i++) {
        BOOST_REQUIRE(h.GenerateExternalNonce(i, ext_secnonces[i], all_pubnonces[i]));
        OracleMusigNonceMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.pubnonce = h.SerializePubnonce(all_pubnonces[i]);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigNonce(msg);
    }

    node.SetConsensusValues(epoch, price, timestamp);
    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::SIGNING);

    BOOST_REQUIRE_EQUAL(h.relayed_nonces.size(), 1U);
    secp256k1_musig_pubnonce local_pubnonce;
    BOOST_REQUIRE(secp256k1_musig_pubnonce_parse(h.ctx, &local_pubnonce, h.relayed_nonces[0].pubnonce.data()));
    all_pubnonces[0] = local_pubnonce;

    std::vector<const secp256k1_musig_pubnonce*> pn_ptrs(9);
    for (size_t i = 0; i < 9; i++) pn_ptrs[i] = &all_pubnonces[i];
    secp256k1_musig_aggnonce aggnonce;
    BOOST_REQUIRE(secp256k1_musig_nonce_agg(h.ctx, &aggnonce, pn_ptrs.data(), 9));

    unsigned char msg32[32];
    h.ComputeMsg32(epoch, price, timestamp, msg32);
    secp256k1_musig_session raw_session;
    BOOST_REQUIRE(secp256k1_musig_nonce_process(h.ctx, &raw_session, &aggnonce, msg32, &h.cache));

    for (size_t i = 1; i <= 8; i++) {
        secp256k1_musig_partial_sig psig;
        BOOST_REQUIRE(secp256k1_musig_partial_sign(h.ctx, &psig, &ext_secnonces[i],
                                                    &h.keypairs[i], &h.cache, &raw_session));
        OracleMusigPartialSigMsg msg;
        msg.epoch = epoch;
        msg.oracle_id = static_cast<uint8_t>(i);
        msg.partial_sig = h.SerializePartialSig(psig);
        BOOST_REQUIRE(msg.Sign(h.ckeys[i]));
        node.OnOracleMusigPartialSig(msg);
    }

    BOOST_CHECK(node.GetSessionState() == MuSig2SessionState::COMPLETE);

    COracleBundle bundle = node.GetCurrentBundle(100);
    BOOST_CHECK_EQUAL(bundle.version, 3);
    BOOST_CHECK(bundle.IsMuSig2());
    BOOST_CHECK_EQUAL(bundle.aggregate_sig.size(), 64U);
}

BOOST_AUTO_TEST_SUITE_END()
