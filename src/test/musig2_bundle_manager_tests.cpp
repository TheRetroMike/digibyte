// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/amount.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <oracle/musig2_aggregator.h>
#include <oracle/bundle_manager.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <random.h>
#include <script/script.h>
#include <test/util/setup_common.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <array>
#include <cstring>
#include <vector>

namespace {

COracleBundle MakeV03Bundle()
{
    COracleBundle bundle;
    bundle.version = 3;
    bundle.median_price_micro_usd = 51000;
    bundle.timestamp = 1700000000;
    bundle.participation_bitmap = {0x7F};

    bundle.aggregate_sig.resize(64);
    for (size_t i = 0; i < 64; ++i) {
        bundle.aggregate_sig[i] = static_cast<unsigned char>(i ^ 0x5A);
    }

    return bundle;
}

CTransaction MakeCoinbaseTx(const CScript& oracle_script)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CTxOut oracle_out;
    oracle_out.nValue = 0;
    oracle_out.scriptPubKey = oracle_script;
    coinbase.vout.push_back(oracle_out);

    return CTransaction(coinbase);
}

std::array<unsigned char, 32> RegtestOracleSecret(uint8_t oracle_id)
{
    const std::string seed = "digibyte_regtest_oracle_" + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size()).Finalize(hash.begin());

    std::array<unsigned char, 32> secret{};
    std::memcpy(secret.data(), hash.begin(), secret.size());
    return secret;
}

std::vector<unsigned char> EncodeBitmapForRegtest(const std::vector<uint8_t>& oracle_ids)
{
    const uint16_t total = static_cast<uint16_t>(Params().GetConsensus().nOracleTotalOracles);
    std::vector<unsigned char> bitmap((total + 7) / 8, 0);
    for (uint8_t id : oracle_ids) {
        bitmap[id / 8] |= static_cast<unsigned char>(1U << (id % 8));
    }
    return bitmap;
}

bool SignRegtestV03Bundle(COracleBundle& bundle, const std::vector<uint8_t>& oracle_ids)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx) return false;

    const size_t n_signers = oracle_ids.size();
    std::vector<std::array<unsigned char, 32>> seckeys(n_signers);
    std::vector<secp256k1_keypair> keypairs(n_signers);
    std::vector<secp256k1_pubkey> pubkeys(n_signers);

    for (size_t i = 0; i < n_signers; ++i) {
        seckeys[i] = RegtestOracleSecret(oracle_ids[i]);
        if (!secp256k1_keypair_create(ctx, &keypairs[i], seckeys[i].data())) {
            secp256k1_context_destroy(ctx);
            return false;
        }
        if (!secp256k1_keypair_pub(ctx, &pubkeys[i], &keypairs[i])) {
            secp256k1_context_destroy(ctx);
            return false;
        }
    }

    std::vector<const secp256k1_pubkey*> pubkey_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; ++i) {
        pubkey_ptrs[i] = &pubkeys[i];
    }

    secp256k1_xonly_pubkey agg_pk{};
    secp256k1_musig_keyagg_cache cache{};
    if (!secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), n_signers)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    std::vector<secp256k1_musig_secnonce> secnonces(n_signers);
    std::vector<secp256k1_musig_pubnonce> pubnonces(n_signers);
    for (size_t i = 0; i < n_signers; ++i) {
        unsigned char session_rand[32];
        GetStrongRandBytes(Span{session_rand, 32});
        if (!secp256k1_musig_nonce_gen(ctx, &secnonces[i], &pubnonces[i],
                                       session_rand, seckeys[i].data(), &pubkeys[i],
                                       nullptr, &cache, nullptr)) {
            secp256k1_context_destroy(ctx);
            return false;
        }
    }

    std::vector<const secp256k1_musig_pubnonce*> nonce_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; ++i) {
        nonce_ptrs[i] = &pubnonces[i];
    }

    secp256k1_musig_aggnonce aggnonce{};
    if (!secp256k1_musig_nonce_agg(ctx, &aggnonce, nonce_ptrs.data(), n_signers)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    const uint256 msg_hash = ComputeOracleBundleHash(bundle);
    unsigned char msg32[32];
    std::memcpy(msg32, msg_hash.begin(), sizeof(msg32));

    secp256k1_musig_session session{};
    if (!secp256k1_musig_nonce_process(ctx, &session, &aggnonce, msg32, &cache)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    std::vector<secp256k1_musig_partial_sig> partial_sigs(n_signers);
    std::vector<const secp256k1_musig_partial_sig*> partial_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; ++i) {
        if (!secp256k1_musig_partial_sign(ctx, &partial_sigs[i], &secnonces[i],
                                          &keypairs[i], &cache, &session)) {
            secp256k1_context_destroy(ctx);
            return false;
        }
        partial_ptrs[i] = &partial_sigs[i];
    }

    bundle.participation_bitmap = EncodeBitmapForRegtest(oracle_ids);
    bundle.aggregate_sig.assign(64, 0);
    if (!secp256k1_musig_partial_sig_agg(ctx, bundle.aggregate_sig.data(),
                                         &session, partial_ptrs.data(), n_signers)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    const bool verifies = secp256k1_schnorrsig_verify(ctx, bundle.aggregate_sig.data(), msg32, 32, &agg_pk);
    secp256k1_context_destroy(ctx);
    return verifies;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(musig2_bundle_manager_tests, RegTestingSetup)

BOOST_AUTO_TEST_CASE(create_oracle_script_v03)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    COracleBundle bundle = MakeV03Bundle();

    CScript script = manager.CreateOracleScript(bundle);
    BOOST_REQUIRE(!script.empty());

    // OP_RETURN OP_ORACLE <push:0x03>
    BOOST_CHECK_EQUAL(script[0], OP_RETURN);
    BOOST_CHECK_EQUAL(script[1], OP_ORACLE);
    BOOST_CHECK_EQUAL(script[2], 0x01);
    BOOST_CHECK_EQUAL(script[3], 0x03);
}

BOOST_AUTO_TEST_CASE(extract_oracle_bundle_v03)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    const COracleBundle bundle = MakeV03Bundle();
    const CScript script = manager.CreateOracleScript(bundle);
    BOOST_REQUIRE(!script.empty());

    const CTransaction tx = MakeCoinbaseTx(script);

    COracleBundle extracted;
    BOOST_REQUIRE(manager.ExtractOracleBundle(tx, extracted));

    BOOST_CHECK_EQUAL(extracted.version, 3);
    BOOST_CHECK(extracted.IsMuSig2());
    BOOST_CHECK_EQUAL(extracted.messages.size(), 7);
    BOOST_CHECK_EQUAL(extracted.messages.front().oracle_id, 0);
    BOOST_CHECK_EQUAL(extracted.messages.back().oracle_id, 6);
    BOOST_CHECK_EQUAL(extracted.median_price_micro_usd, bundle.median_price_micro_usd);
    BOOST_CHECK_EQUAL(extracted.timestamp, bundle.timestamp);
    BOOST_CHECK(extracted.participation_bitmap == bundle.participation_bitmap);
    BOOST_CHECK(extracted.aggregate_sig == bundle.aggregate_sig);
}

BOOST_AUTO_TEST_CASE(v03_round_trip_serialization)
{
    COracleBundle original = MakeV03Bundle();

    const std::vector<unsigned char> encoded = original.SerializeV03Data();
    BOOST_REQUIRE(!encoded.empty());

    COracleBundle decoded;
    BOOST_REQUIRE(COracleBundle::DeserializeV03Data(encoded, decoded));

    BOOST_CHECK_EQUAL(decoded.median_price_micro_usd, original.median_price_micro_usd);
    BOOST_CHECK_EQUAL(decoded.timestamp, original.timestamp);
    BOOST_CHECK(decoded.participation_bitmap == original.participation_bitmap);
    BOOST_CHECK(decoded.aggregate_sig == original.aggregate_sig);
}

BOOST_AUTO_TEST_CASE(validate_v03_rejects_signed_out_of_range_price)
{
    const Consensus::Params& params = Params().GetConsensus();
    BOOST_REQUIRE_EQUAL(params.nOracleRequiredMessages, 4);
    BOOST_REQUIRE_EQUAL(params.nOracleTotalOracles, 7);

    const int active_height = params.nDigiDollarMuSig2Height;

    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = GetCurrentEpoch(active_height);
    bundle.median_price_micro_usd = 0;
    bundle.timestamp = 1700000000;

    const std::vector<uint8_t> oracle_ids{0, 1, 2, 3};
    BOOST_REQUIRE(SignRegtestV03Bundle(bundle, oracle_ids));

    std::string error;
    BOOST_CHECK(!OracleBundleManager::ValidateMuSig2Bundle(bundle, active_height, params, error));
    BOOST_CHECK(error.find("price") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_v03_rejects_unused_bitmap_bits)
{
    const Consensus::Params& params = Params().GetConsensus();
    BOOST_REQUIRE_EQUAL(params.nOracleRequiredMessages, 4);
    BOOST_REQUIRE_EQUAL(params.nOracleTotalOracles, 7);

    const int active_height = params.nDigiDollarMuSig2Height;

    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = GetCurrentEpoch(active_height);
    bundle.median_price_micro_usd = 51000;
    bundle.timestamp = 1700000000;

    const std::vector<uint8_t> oracle_ids{0, 1, 2, 3};
    BOOST_REQUIRE(SignRegtestV03Bundle(bundle, oracle_ids));
    BOOST_REQUIRE_EQUAL(bundle.participation_bitmap.size(), 1U);
    BOOST_REQUIRE_EQUAL(bundle.participation_bitmap[0], 0x0f);

    // Regtest has 7 oracle slots, so bit 7 in the final bitmap byte is unused.
    // Flipping it must not be a second valid encoding for the same signer set.
    bundle.participation_bitmap[0] |= 0x80;

    std::string error;
    BOOST_CHECK(!OracleBundleManager::ValidateMuSig2Bundle(bundle, active_height, params, error));
    BOOST_CHECK(error.find("bitmap") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
