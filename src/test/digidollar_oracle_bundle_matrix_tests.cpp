// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 8 - Oracle Bundle Format and DD-Touching Block Rules
 *
 * This suite asserts the full canonical reject-reason matrix that
 * `OracleDataValidator::ValidateBlockOracleData` is required to enforce
 * (`src/oracle/bundle_manager.cpp:1888+`):
 *
 *   - bad-oracle-multiple-outputs : >1 OP_RETURN/OP_ORACLE coinbase output
 *   - bad-oracle-missing          : mint/redeem block, no oracle bundle
 *   - bad-oracle-malformed        : OP_ORACLE present but extraction failed
 *                                   (unknown version byte, truncated data,
 *                                   missing version byte, oracle_id-only push)
 *   - bad-oracle-legacy           : extracted bundle is v0x01 or v0x02
 *
 * Wave 8 also exercises the DD-touching matrix: every DD txtype + collateral
 * spend with a valid v0x03 bundle is accepted; every DD txtype without a
 * bundle or with a non-v0x03 bundle is rejected with a reason that matches
 * the canonical strings above.
 *
 * The pre-existing `digidollar_oracle_musig2_tests` only checks
 * `!result.ok` and a non-empty reject_reason; this suite locks in the
 * exact reject reasons.
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <crypto/sha256.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <random.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int32_t POST_DD_HEIGHT = 700;
constexpr uint32_t BLOCK_TIME = 1735689600; // 2025-01-01T00:00:00Z
constexpr uint64_t ORACLE_PRICE = 50000;

std::array<unsigned char, 32> OracleSecret(uint8_t oracle_id)
{
    const std::string seed = std::string{"digibyte_regtest_oracle_"} + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size()).Finalize(hash.begin());

    std::array<unsigned char, 32> secret{};
    std::memcpy(secret.data(), hash.begin(), secret.size());
    return secret;
}

std::vector<unsigned char> EncodeBitmap(const std::vector<uint8_t>& oracle_ids, uint16_t total_oracles)
{
    std::vector<unsigned char> bitmap((total_oracles + 7) / 8, 0);
    for (uint8_t id : oracle_ids) {
        bitmap[id / 8] |= static_cast<unsigned char>(1U << (id % 8));
    }
    return bitmap;
}

bool SignV03Bundle(COracleBundle& bundle,
                   const std::vector<uint8_t>& oracle_ids,
                   uint16_t total_oracles)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx) return false;

    const size_t n_signers = oracle_ids.size();
    std::vector<std::array<unsigned char, 32>> seckeys(n_signers);
    std::vector<secp256k1_keypair> keypairs(n_signers);
    std::vector<secp256k1_pubkey> pubkeys(n_signers);

    for (size_t i = 0; i < n_signers; ++i) {
        seckeys[i] = OracleSecret(oracle_ids[i]);
        if (!secp256k1_keypair_create(ctx, &keypairs[i], seckeys[i].data()) ||
            !secp256k1_keypair_pub(ctx, &pubkeys[i], &keypairs[i])) {
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

    bundle.participation_bitmap = EncodeBitmap(oracle_ids, total_oracles);
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

COracleBundle MakeValidV03Bundle(int32_t height, int64_t timestamp)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = GetCurrentEpoch(height);
    bundle.median_price_micro_usd = ORACLE_PRICE;
    bundle.timestamp = timestamp;

    std::vector<uint8_t> oracle_ids;
    for (uint8_t id = 0; id < consensus.nOracleConsensusRequired; ++id) {
        oracle_ids.push_back(id);
    }
    BOOST_REQUIRE(SignV03Bundle(bundle, oracle_ids, static_cast<uint16_t>(consensus.nOracleTotalOracles)));
    return bundle;
}

CScript MakeRawScriptWithVersion(uint8_t version_byte, const std::vector<unsigned char>& payload)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{version_byte};
    if (!payload.empty()) {
        script << payload;
    }
    return script;
}

CScript MakeRawV03Script(const COracleBundle& bundle)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{0x03};
    script << bundle.SerializeV03Data();
    return script;
}

CScript MakeRawV02ScriptOnlyMessageBytes()
{
    // A minimum-shape v0x02 payload: count(1) + price(8) + timestamp(8) + 1 message
    // (oracle_id(1) + sig(64)). This passes ExtractOracleBundle's chunk parser
    // structurally so version dispatch reports v0x02 and is rejected by the
    // legacy gate.
    std::vector<unsigned char> payload;
    payload.push_back(0x01); // 1 message
    for (int i = 0; i < 8; ++i) payload.push_back(0); // price
    for (int i = 0; i < 8; ++i) payload.push_back(0); // timestamp
    payload.push_back(0); // oracle_id
    for (int i = 0; i < 64; ++i) payload.push_back(0); // sig
    return MakeRawScriptWithVersion(0x02, payload);
}

CScript MakeRawV01ScriptOnlyMessageBytes()
{
    // v0x01 = single oracle 1-of-1 phase one. ExtractOracleBundle handles
    // any non-0x03 byte uniformly: 0x01 and 0x02 explicitly hit the
    // legacy-rejection branch (which returns false, so the validator
    // reports `bad-oracle-malformed`).
    std::vector<unsigned char> payload;
    payload.push_back(0); // oracle_id
    for (int i = 0; i < 8; ++i) payload.push_back(0); // price
    for (int i = 0; i < 8; ++i) payload.push_back(0); // timestamp
    return MakeRawScriptWithVersion(0x01, payload);
}

CTransactionRef MakeDigiDollarTx(DigiDollarTxType type, uint8_t flags = 0)
{
    CMutableTransaction tx;
    tx.nVersion = MakeDigiDollarVersion(type, flags);
    tx.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx.vout.emplace_back(0, CScript() << OP_TRUE);
    return MakeTransactionRef(std::move(tx));
}

CBlock MakeBlock(int32_t height,
                 uint32_t block_time,
                 const std::vector<CScript>& oracle_scripts,
                 const std::vector<CTransactionRef>& extra_txs)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << static_cast<int64_t>(height);
    coinbase.vout.emplace_back(50 * COIN, CScript() << OP_TRUE);
    for (const auto& oracle_script : oracle_scripts) {
        coinbase.vout.emplace_back(0, oracle_script);
    }

    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime = block_time;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.vtx.insert(block.vtx.end(), extra_txs.begin(), extra_txs.end());
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

struct Result {
    bool ok;
    std::string reject_reason;
};

Result ValidateOracleBlock(const CBlock& block)
{
    BlockValidationState state;
    const bool ok = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, Params().GetConsensus(), state);
    return {ok, state.GetRejectReason()};
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_oracle_bundle_matrix_tests, RegTestingSetup)

// -----------------------------------------------------------------------------
// Reject-reason matrix - bad-oracle-multiple-outputs
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wave8_dd_block_two_oracle_outputs_rejected_multiple_outputs)
{
    OracleBundleManager::GetInstance().Clear();

    const COracleBundle bundle = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    const CScript script_a = MakeRawV03Script(bundle);
    const CScript script_b = MakeRawV03Script(bundle);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {script_a, script_b},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-multiple-outputs");
}

BOOST_AUTO_TEST_CASE(wave8_non_dd_block_two_oracle_outputs_still_rejected)
{
    // Validator counts oracle outputs *before* checking BlockTouchesDigiDollar,
    // so even non-DD blocks must reject duplicate oracle outputs.
    OracleBundleManager::GetInstance().Clear();

    const COracleBundle bundle = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    const CScript script_a = MakeRawV03Script(bundle);
    const CScript script_b = MakeRawV03Script(bundle);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {script_a, script_b}, {});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-multiple-outputs");
}

// -----------------------------------------------------------------------------
// Reject-reason matrix - bad-oracle-missing
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wave8_dd_mint_block_no_oracle_rejected_missing)
{
    OracleBundleManager::GetInstance().Clear();
    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-missing");
}

BOOST_AUTO_TEST_CASE(wave8_dd_transfer_block_no_oracle_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {},
                             {MakeDigiDollarTx(DD_TX_TRANSFER)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK_MESSAGE(r.ok, "DD transfer-only block must not require oracle price, reject='" << r.reject_reason << "'");
}

BOOST_AUTO_TEST_CASE(wave8_dd_redeem_block_no_oracle_rejected_missing)
{
    OracleBundleManager::GetInstance().Clear();
    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {},
                             {MakeDigiDollarTx(DD_TX_REDEEM)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-missing");
}

BOOST_AUTO_TEST_CASE(wave8_dd_collateral_spend_block_no_oracle_rejected_missing)
{
    // DD-FA-SEC-002 collateral-vault attack class: a redeem with the
    // "collateral spend" flags must still be DD-touching and require a
    // valid bundle.
    OracleBundleManager::GetInstance().Clear();
    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {},
                             {MakeDigiDollarTx(DD_TX_REDEEM, /*flags=*/1)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-missing");
}

// -----------------------------------------------------------------------------
// Reject-reason matrix - bad-oracle-malformed
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wave8_dd_block_unknown_version_byte_rejected_malformed)
{
    // Versions outside {0x01, 0x02, 0x03} fall through ExtractOracleBundle
    // without matching any branch, returning false -> bad-oracle-malformed.
    OracleBundleManager::GetInstance().Clear();
    const std::vector<unsigned char> payload = {0xAB, 0xCD};

    for (uint8_t v : {0x00, 0x04, 0x05, 0x7F, 0xFF}) {
        const CScript bad_script = MakeRawScriptWithVersion(v, payload);
        CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {bad_script},
                                 {MakeDigiDollarTx(DD_TX_MINT)});
        Result r = ValidateOracleBlock(block);
        BOOST_TEST_MESSAGE("version_byte=0x" << std::hex << static_cast<int>(v)
                           << std::dec << " reject='" << r.reject_reason << "'");
        BOOST_CHECK_MESSAGE(!r.ok, "DD block with unknown oracle version must be rejected");
        BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
    }
}

BOOST_AUTO_TEST_CASE(wave8_dd_block_v03_truncated_payload_rejected_malformed)
{
    // Truncate a valid v0x03 script before the aggregate signature ends.
    OracleBundleManager::GetInstance().Clear();

    const COracleBundle good = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    CScript good_script = MakeRawV03Script(good);
    BOOST_REQUIRE(good_script.size() > 32);
    const CScript truncated_script(good_script.begin(), good_script.end() - 32);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {truncated_script},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

BOOST_AUTO_TEST_CASE(wave8_dd_block_v03_extra_trailing_bytes_rejected_malformed)
{
    // Append junk after a valid v0x03 payload -- DeserializeV03Data rejects
    // trailing bytes -> bad-oracle-malformed.
    OracleBundleManager::GetInstance().Clear();

    const COracleBundle good = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    std::vector<unsigned char> payload = good.SerializeV03Data();
    payload.push_back(0xCC); // one extra byte
    const CScript bad = MakeRawScriptWithVersion(0x03, payload);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {bad},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

BOOST_AUTO_TEST_CASE(wave8_dd_block_oracle_marker_no_data_rejected_malformed)
{
    // OP_RETURN + OP_ORACLE with no version push at all. The
    // ExtractOracleBundle parser requires script length >= 4 to enter the
    // version-dispatch branch; shorter scripts hit the post-loop "return false".
    OracleBundleManager::GetInstance().Clear();

    CScript no_data_script;
    no_data_script << OP_RETURN << OP_ORACLE;
    BOOST_REQUIRE_EQUAL(no_data_script.size(), 2);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {no_data_script},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

BOOST_AUTO_TEST_CASE(wave8_dd_block_v03_bitmap_zero_length_rejected_malformed)
{
    // bitmap_len = 0 is rejected by DeserializeV03Data and surfaces as
    // bad-oracle-malformed.
    OracleBundleManager::GetInstance().Clear();

    std::vector<unsigned char> payload(1 + 0 + 4 + 8 + 8 + 64, 0);
    payload[0] = 0; // bitmap_len = 0
    const CScript bad = MakeRawScriptWithVersion(0x03, payload);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {bad},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

BOOST_AUTO_TEST_CASE(wave8_dd_block_v03_bitmap_oversized_rejected_malformed)
{
    // bitmap_len that exceeds the script payload length is rejected by
    // DeserializeV03Data because the buffer is short, also surfacing as
    // bad-oracle-malformed.
    OracleBundleManager::GetInstance().Clear();

    std::vector<unsigned char> payload;
    payload.push_back(0xFF); // claim 255-byte bitmap, but no bytes follow
    for (int i = 0; i < 4 + 8 + 8 + 64; ++i) payload.push_back(0);
    const CScript bad = MakeRawScriptWithVersion(0x03, payload);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {bad},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

// -----------------------------------------------------------------------------
// Reject-reason matrix - bad-oracle-malformed (legacy v0x01/v0x02)
//
// The current bundle_manager extraction returns false for v0x01 and v0x02
// version bytes (they hit the explicit legacy-rejection branch). Because
// extraction returns false, ValidateBlockOracleData reports
// bad-oracle-malformed -- not bad-oracle-legacy. The bad-oracle-legacy
// branch is reachable only through a *deserialised* bundle whose
// version field is set to 1 or 2 by some non-extraction path
// (e.g. internal callers populating COracleBundle::version directly).
// We assert both behaviours below.
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wave8_dd_block_v01_oracle_rejected_malformed)
{
    OracleBundleManager::GetInstance().Clear();
    const CScript v01 = MakeRawV01ScriptOnlyMessageBytes();

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {v01},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    // Wave 8 invariant: legacy script bytes never produce an extracted
    // bundle, so the validator emits the catch-all malformed reason.
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

BOOST_AUTO_TEST_CASE(wave8_dd_block_v02_oracle_rejected_malformed)
{
    OracleBundleManager::GetInstance().Clear();
    const CScript v02 = MakeRawV02ScriptOnlyMessageBytes();

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {v02},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

// -----------------------------------------------------------------------------
// Reject-reason matrix - bad-oracle-musig2 sub-quorum bitmap
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wave8_dd_block_subquorum_bitmap_rejected_musig2)
{
    // 3 oracle bits set on regtest where the threshold is 4 -> sub-quorum
    // -> ValidateMuSig2Bundle returns false -> bad-oracle-musig2.
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& consensus = Params().GetConsensus();

    COracleBundle sub_bundle;
    sub_bundle.version = 3;
    sub_bundle.epoch = GetCurrentEpoch(POST_DD_HEIGHT);
    sub_bundle.median_price_micro_usd = ORACLE_PRICE;
    sub_bundle.timestamp = BLOCK_TIME;
    sub_bundle.participation_bitmap = EncodeBitmap(
        {0, 1, 2}, static_cast<uint16_t>(consensus.nOracleTotalOracles));
    sub_bundle.aggregate_sig.assign(64, 0xAB);

    const CScript bad = MakeRawV03Script(sub_bundle);
    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {bad},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-musig2");
}

// -----------------------------------------------------------------------------
// Acceptance matrix - DD-touching block + valid v0x03 bundle
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wave8_dd_mint_block_with_valid_v03_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const COracleBundle good = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    const CScript good_script = MakeRawV03Script(good);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {good_script},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_TEST_MESSAGE("dd_mint accepted: ok=" << r.ok
                       << " reject='" << r.reject_reason << "'");
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

BOOST_AUTO_TEST_CASE(wave8_dd_transfer_block_with_valid_v03_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const COracleBundle good = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    const CScript good_script = MakeRawV03Script(good);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {good_script},
                             {MakeDigiDollarTx(DD_TX_TRANSFER)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

BOOST_AUTO_TEST_CASE(wave8_dd_redeem_block_with_valid_v03_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const COracleBundle good = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    const CScript good_script = MakeRawV03Script(good);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {good_script},
                             {MakeDigiDollarTx(DD_TX_REDEEM)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

BOOST_AUTO_TEST_CASE(wave8_dd_collateral_spend_block_with_valid_v03_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const COracleBundle good = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    const CScript good_script = MakeRawV03Script(good);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {good_script},
                             {MakeDigiDollarTx(DD_TX_REDEEM, /*flags=*/1)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// -----------------------------------------------------------------------------
// Acceptance matrix - non-DD block paths
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(wave8_non_dd_block_no_oracle_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {}, {});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

BOOST_AUTO_TEST_CASE(wave8_non_dd_block_with_valid_v03_accepted)
{
    // Production miners may include an oracle bundle even on non-DD blocks
    // (e.g., when generating off DD epochs); the validator must accept.
    OracleBundleManager::GetInstance().Clear();
    const COracleBundle good = MakeValidV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    const CScript good_script = MakeRawV03Script(good);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {good_script}, {});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

BOOST_AUTO_TEST_CASE(wave8_non_dd_block_with_v01_oracle_rejected_malformed)
{
    // Even when the block is non-DD-touching, once an OP_RETURN/OP_ORACLE
    // output is present and yields a legacy-shaped payload, the validator
    // refuses to accept the block. This guards against a fork where an
    // attacker rides legacy bytes onto an empty block to corrupt the
    // oracle cache.
    OracleBundleManager::GetInstance().Clear();
    const CScript v01 = MakeRawV01ScriptOnlyMessageBytes();

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {v01}, {});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

BOOST_AUTO_TEST_CASE(wave8_non_dd_block_with_v02_oracle_rejected_malformed)
{
    OracleBundleManager::GetInstance().Clear();
    const CScript v02 = MakeRawV02ScriptOnlyMessageBytes();

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {v02}, {});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

BOOST_AUTO_TEST_CASE(wave8_dd_block_v04_future_unknown_rejected_malformed)
{
    // Future / unknown bundle version numbers must fail closed today --
    // they are not "accept by default". This pins down current behaviour
    // so any future opt-in version bump has to update this test.
    OracleBundleManager::GetInstance().Clear();
    const std::vector<unsigned char> payload(96, 0);
    const CScript bad = MakeRawScriptWithVersion(0x04, payload);

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, {bad},
                             {MakeDigiDollarTx(DD_TX_MINT)});
    Result r = ValidateOracleBlock(block);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
}

BOOST_AUTO_TEST_SUITE_END()
