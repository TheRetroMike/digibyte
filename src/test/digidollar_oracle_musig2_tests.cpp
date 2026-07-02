// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <crypto/sha256.h>
#include <hash.h>
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
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int32_t POST_DD_HEIGHT = 700;
constexpr uint32_t BLOCK_TIME = 1735689600; // 2025-01-01T00:00:00Z
constexpr uint64_t ORACLE_PRICE = 50000;

std::array<unsigned char, 32> OracleSecret(const std::string& seed_prefix, uint8_t oracle_id)
{
    const std::string seed = seed_prefix + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size()).Finalize(hash.begin());

    std::array<unsigned char, 32> secret{};
    std::memcpy(secret.data(), hash.begin(), secret.size());
    return secret;
}

CKey RegtestOracleKey(uint8_t oracle_id)
{
    const auto secret = OracleSecret("digibyte_regtest_oracle_", oracle_id);
    CKey key;
    key.Set(secret.begin(), secret.end(), true);
    return key;
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
                   uint16_t total_oracles,
                   const std::optional<uint256>& override_hash = std::nullopt)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx) return false;

    const size_t n_signers = oracle_ids.size();
    std::vector<std::array<unsigned char, 32>> seckeys(n_signers);
    std::vector<secp256k1_keypair> keypairs(n_signers);
    std::vector<secp256k1_pubkey> pubkeys(n_signers);

    for (size_t i = 0; i < n_signers; ++i) {
        seckeys[i] = OracleSecret("digibyte_regtest_oracle_", oracle_ids[i]);
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

    const uint256 msg_hash = override_hash ? *override_hash : ComputeOracleBundleHash(bundle);
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

uint256 WrongDomainHash(const COracleBundle& bundle)
{
    CHashWriter ss(0);
    ss << std::string{"DigiDollar/WrongOracleDomain"};
    ss << bundle.epoch;
    ss << bundle.median_price_micro_usd;
    ss << bundle.timestamp;
    return ss.GetHash();
}

COraclePriceMessage MakePhaseTwoMessage(uint8_t oracle_id, uint64_t price, int64_t timestamp)
{
    CKey key = RegtestOracleKey(oracle_id);
    COraclePriceMessage msg(oracle_id, price, timestamp);
    msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
    BOOST_REQUIRE(msg.SignAttestation(key));
    return msg;
}

COracleBundle MakePhaseTwoBundle(int32_t height, int64_t timestamp)
{
    COracleBundle bundle;
    bundle.version = 2;
    bundle.epoch = GetCurrentEpoch(height);
    bundle.median_price_micro_usd = ORACLE_PRICE;
    bundle.timestamp = timestamp;
    for (uint8_t id = 0; id < 4; ++id) {
        bundle.messages.push_back(MakePhaseTwoMessage(id, ORACLE_PRICE, timestamp));
    }
    return bundle;
}

CScript MakeRawV02Script(const COracleBundle& bundle)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{0x02};

    std::vector<unsigned char> data;
    data.push_back(static_cast<unsigned char>(bundle.messages.size()));
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<unsigned char>((bundle.median_price_micro_usd >> (i * 8)) & 0xff));
    }
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<unsigned char>((bundle.timestamp >> (i * 8)) & 0xff));
    }
    for (const auto& msg : bundle.messages) {
        data.push_back(static_cast<unsigned char>(msg.oracle_id & 0xff));
        if (msg.schnorr_sig.size() == 64) {
            data.insert(data.end(), msg.schnorr_sig.begin(), msg.schnorr_sig.end());
        } else {
            data.insert(data.end(), 64, 0x00);
        }
    }
    script << data;
    return script;
}

COracleBundle MakePhaseOneBundle(int64_t timestamp)
{
    COracleBundle bundle;
    bundle.version = 1;
    bundle.epoch = GetCurrentEpoch(POST_DD_HEIGHT);
    bundle.median_price_micro_usd = ORACLE_PRICE;
    bundle.timestamp = timestamp;
    bundle.messages.push_back(MakePhaseTwoMessage(0, ORACLE_PRICE, timestamp));
    return bundle;
}

COracleBundle MakeV03Bundle(int32_t height, int64_t timestamp)
{
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = GetCurrentEpoch(height);
    bundle.median_price_micro_usd = ORACLE_PRICE;
    bundle.timestamp = timestamp;
    return bundle;
}

CScript MakeRawV03Script(const COracleBundle& bundle)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{0x03};
    script << bundle.SerializeV03Data();
    return script;
}

CScript MakeMalformedV03Script()
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{0x03, 0x01, 0x02};
    return script;
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
                 const CScript* oracle_script,
                 const std::vector<CTransactionRef>& extra_txs)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << static_cast<int64_t>(height);
    coinbase.vout.emplace_back(50 * COIN, CScript() << OP_TRUE);
    if (oracle_script) {
        coinbase.vout.emplace_back(0, *oracle_script);
    }

    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime = block_time;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.vtx.insert(block.vtx.end(), extra_txs.begin(), extra_txs.end());
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

struct ValidationResult {
    bool ok;
    std::string reject_reason;
};

ValidationResult ValidateOracleBlock(const CBlock& block)
{
    BlockValidationState state;
    const bool ok = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, Params().GetConsensus(), state);
    return {ok, state.GetRejectReason()};
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_oracle_musig2_tests, RegTestingSetup)

BOOST_AUTO_TEST_CASE(legacy_oracle_formats_are_not_mined_as_fallback)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    const COracleBundle v01 = MakePhaseOneBundle(BLOCK_TIME);
    const COracleBundle v02 = MakePhaseTwoBundle(POST_DD_HEIGHT, BLOCK_TIME);

    const CScript v01_script = manager.CreateOracleScript(v01);
    const CScript v02_script = manager.CreateOracleScript(v02);

    BOOST_CHECK_MESSAGE(v01_script.empty(),
        "miners must not emit legacy v0x01 oracle scripts after DigiDollar activation");
    BOOST_CHECK_MESSAGE(v02_script.empty(),
        "miners must not emit legacy v0x02 oracle scripts after DigiDollar activation");
}

BOOST_AUTO_TEST_CASE(legacy_v02_oracle_format_rejected_for_dd_block)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    const COracleBundle v02 = MakePhaseTwoBundle(POST_DD_HEIGHT, BLOCK_TIME);
    const CScript v02_script = MakeRawV02Script(v02);
    BOOST_REQUIRE(!v02_script.empty());

    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, &v02_script,
                             {MakeDigiDollarTx(DD_TX_MINT)});
    ValidationResult result = ValidateOracleBlock(block);
    BOOST_TEST_MESSAGE("legacy v0x02 DD block: ok=" << result.ok
                       << " reject='" << result.reject_reason << "'");

    BOOST_CHECK_MESSAGE(!result.ok,
        "DD-touching blocks must reject v0x02 oracle bundles instead of treating them as a fallback");
    if (!result.ok) {
        BOOST_CHECK_MESSAGE(!result.reject_reason.empty(), "legacy-format rejection must set a reject reason");
    }
}

// Wave 8 (DD-FA-SEC-008): pin the *exact* reject reason text emitted by
// ValidateBlockOracleData for each documented error class so future drift in
// the rejection string causes a test failure. CLAUDE.md and the oracle
// bundle architecture treat these strings as the consensus contract.
BOOST_AUTO_TEST_CASE(wave8_validate_block_oracle_data_reject_reasons_pinned)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    // Case A: DD mint/redeem block with no oracle output -> "bad-oracle-missing".
    {
        CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, /*oracle_script=*/nullptr,
                                 {MakeDigiDollarTx(DD_TX_MINT)});
        ValidationResult result = ValidateOracleBlock(block);
        BOOST_TEST_MESSAGE("missing-oracle DD block: ok=" << result.ok
                           << " reject='" << result.reject_reason << "'");
        BOOST_CHECK(!result.ok);
        BOOST_CHECK_EQUAL(result.reject_reason, "bad-oracle-missing");
    }

    // Case B: DD-touching block with a v0x01 OP_RETURN payload -> the
    // extractor refuses legacy versions so ValidateBlockOracleData sees a
    // present-but-unparseable oracle output and emits "bad-oracle-malformed".
    {
        const COracleBundle v01 = MakePhaseOneBundle(BLOCK_TIME);
        const CScript v01_script = MakeRawV02Script(v01); // shape only; body parses as legacy.
        // Construct a raw v0x01 payload (legacy-shaped) by hand because
        // CreateOracleScript no longer emits v0x01.
        CScript legacy;
        legacy << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{0x01};
        std::vector<unsigned char> body(64, 0x42);
        legacy << body;
        CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, &legacy,
                                 {MakeDigiDollarTx(DD_TX_MINT)});
        ValidationResult result = ValidateOracleBlock(block);
        BOOST_TEST_MESSAGE("legacy v0x01 DD block: ok=" << result.ok
                           << " reject='" << result.reject_reason << "'");
        BOOST_CHECK(!result.ok);
        BOOST_CHECK_EQUAL(result.reject_reason, "bad-oracle-malformed");
    }

    // Case C: DD-touching block with a v0x02 OP_RETURN payload -> same
    // path: "bad-oracle-malformed" (legacy bodies are rejected at extraction).
    {
        const COracleBundle v02 = MakePhaseTwoBundle(POST_DD_HEIGHT, BLOCK_TIME);
        const CScript v02_script = MakeRawV02Script(v02);
        CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, &v02_script,
                                 {MakeDigiDollarTx(DD_TX_MINT)});
        ValidationResult result = ValidateOracleBlock(block);
        BOOST_TEST_MESSAGE("legacy v0x02 DD block: ok=" << result.ok
                           << " reject='" << result.reject_reason << "'");
        BOOST_CHECK(!result.ok);
        BOOST_CHECK_EQUAL(result.reject_reason, "bad-oracle-malformed");
    }

    // Case D: DD-touching block with a malformed v0x03 payload -> same
    // ExtractOracleBundle failure path -> "bad-oracle-malformed".
    {
        CScript malformed = MakeMalformedV03Script();
        CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, &malformed,
                                 {MakeDigiDollarTx(DD_TX_MINT)});
        ValidationResult result = ValidateOracleBlock(block);
        BOOST_TEST_MESSAGE("malformed v0x03 DD block: ok=" << result.ok
                           << " reject='" << result.reject_reason << "'");
        BOOST_CHECK(!result.ok);
        BOOST_CHECK_EQUAL(result.reject_reason, "bad-oracle-malformed");
    }
}

BOOST_AUTO_TEST_CASE(non_dd_block_without_oracle_data_remains_valid)
{
    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, /*oracle_script=*/nullptr, {});
    ValidationResult result = ValidateOracleBlock(block);

    BOOST_CHECK_MESSAGE(result.ok,
        "non-DigiDollar blocks without oracle data must remain valid");
    BOOST_CHECK_EQUAL(result.reject_reason, "");
}

BOOST_AUTO_TEST_CASE(price_dependent_dd_blocks_without_oracle_data_are_rejected)
{
    struct Case {
        const char* label;
        DigiDollarTxType type;
        uint8_t flags;
    };

    const std::vector<Case> rejected_cases{
        {"mint", DD_TX_MINT, 0},
        {"redeem", DD_TX_REDEEM, 0},
        {"collateral-spend", DD_TX_REDEEM, 1},
    };

    for (const auto& c : rejected_cases) {
        CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, /*oracle_script=*/nullptr,
                                 {MakeDigiDollarTx(c.type, c.flags)});
        ValidationResult result = ValidateOracleBlock(block);
        BOOST_TEST_MESSAGE(c.label << " without oracle: ok=" << result.ok
                           << " reject='" << result.reject_reason << "'");

        BOOST_CHECK_MESSAGE(!result.ok,
            std::string{"DD "} + c.label + " block without oracle data must be invalid");
        BOOST_CHECK_MESSAGE(!result.reject_reason.empty(),
            std::string{"DD "} + c.label + " missing-oracle rejection must set a reject reason");
    }
}

BOOST_AUTO_TEST_CASE(dd_transfer_block_without_oracle_data_is_accepted)
{
    CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, /*oracle_script=*/nullptr,
                             {MakeDigiDollarTx(DD_TX_TRANSFER)});
    ValidationResult result = ValidateOracleBlock(block);
    BOOST_TEST_MESSAGE("transfer without oracle: ok=" << result.ok
                       << " reject='" << result.reject_reason << "'");

    BOOST_CHECK_MESSAGE(result.ok,
        "DD transfer-only block without oracle data must remain valid");
    BOOST_CHECK_EQUAL(result.reject_reason, "");
}

BOOST_AUTO_TEST_CASE(dd_touching_blocks_reject_bad_oracle_bundles)
{
    const std::vector<CTransactionRef> dd_tx{MakeDigiDollarTx(DD_TX_MINT)};
    const std::vector<uint8_t> quorum_ids{0, 1, 2, 3};
    const uint16_t total_oracles = static_cast<uint16_t>(Params().GetConsensus().nOracleTotalOracles);

    CScript malformed_script = MakeMalformedV03Script();

    COracleBundle stale = MakeV03Bundle(POST_DD_HEIGHT, BLOCK_TIME - ORACLE_MAX_AGE_SECONDS - 1);
    BOOST_REQUIRE(SignV03Bundle(stale, quorum_ids, total_oracles));
    CScript stale_script = MakeRawV03Script(stale);

    COracleBundle wrong_domain = MakeV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    BOOST_REQUIRE(SignV03Bundle(wrong_domain, quorum_ids, total_oracles, WrongDomainHash(wrong_domain)));
    CScript wrong_domain_script = MakeRawV03Script(wrong_domain);

    COracleBundle insufficient = MakeV03Bundle(POST_DD_HEIGHT, BLOCK_TIME);
    insufficient.participation_bitmap = EncodeBitmap({0, 1, 2}, total_oracles);
    insufficient.aggregate_sig.assign(64, 0x11);
    CScript insufficient_script = MakeRawV03Script(insufficient);

    struct Case {
        const char* label;
        const CScript* script;
    };

    const std::vector<Case> cases{
        {"malformed", &malformed_script},
        {"stale", &stale_script},
        {"wrong-domain", &wrong_domain_script},
        {"insufficient-quorum", &insufficient_script},
    };

    for (const auto& c : cases) {
        CBlock block = MakeBlock(POST_DD_HEIGHT, BLOCK_TIME, c.script, dd_tx);
        ValidationResult result = ValidateOracleBlock(block);
        BOOST_TEST_MESSAGE(c.label << " oracle DD block: ok=" << result.ok
                           << " reject='" << result.reject_reason << "'");

        BOOST_CHECK_MESSAGE(!result.ok,
            std::string{"DD-touching block with "} + c.label + " oracle data must be invalid");
        if (!result.ok) {
            BOOST_CHECK_MESSAGE(!result.reject_reason.empty(),
                std::string{"DD-touching "} + c.label + " oracle rejection must set a reject reason");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
