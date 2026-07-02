// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 12 — Activation Gates and Consensus-Split Risks (Agent B)
 *
 * Pins the exact pre/post-activation contract enforced by the DigiDollar
 * runtime gates:
 *   - DigiDollar::IsDigiDollarEnabled(pindexPrev, ...)         (BIP9 ACTIVE-after gate)
 *   - Consensus::IsOracleActive(params, height)                (height gate)
 *   - Consensus::IsMuSig2Active(params, height)                (height gate)
 *   - OracleDataValidator::ValidateBlockOracleData             (block-level gate)
 *   - CheckMuSig2OracleBundleVersion                           (block-level gate)
 *
 * The contract under test:
 *   1. At height = nDDActivationHeight - 1, none of the height gates fire and
 *      ValidateBlockOracleData / CheckMuSig2OracleBundleVersion short-circuit
 *      true regardless of the block contents (no enforcement, backward
 *      compatible). This holds whether or not the block contains DD-marker
 *      transactions or oracle-shaped OP_RETURN outputs.
 *   2. At height = nDDActivationHeight, all gates fire and DD-touching blocks
 *      must include a valid v0x03 MuSig2 bundle. The first DD-touching block
 *      at activation MUST succeed when given a valid bundle, and MUST be
 *      rejected when missing/legacy/malformed.
 *   3. Non-DD blocks remain accepted on either side of the boundary, and
 *      regular DGB transactions never require oracle data.
 *   4. Pre-activation OP_ORACLE-looking outputs are ignored (no DD reject
 *      reason); post-activation the same script shape is enforced under V1
 *      MuSig2-only rules.
 *   5. Deployment-state predicates flow DEFINED → STARTED → LOCKED_IN →
 *      ACTIVE; IsDigiDollarEnabled is false in DEFINED/STARTED/LOCKED_IN and
 *      true in ACTIVE, gated by the BIP9 versionbits cache.
 *
 * Coordinated with Agent A (security/exploit) and Agent C
 * (multi-node functional `digidollar_activation_multinode.py`). This suite
 * focuses on unit-level boundary enforcement that the functional test cannot
 * cheaply pin.
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <chain.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <crypto/sha256.h>
#include <deploymentstatus.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <kernel/chainparams.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <random.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/time.h>
#include <validation.h>
#include <versionbits.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace wave12 {

constexpr uint32_t WAVE12_BITS = 0x207fffff;
constexpr uint64_t ORACLE_PRICE = 50000;

// Storage for synthetic block index chains used in this suite. Unique to
// Wave 12 to avoid leaking state into the existing
// digidollar_activation_tests g_test_blocks reservoir.
static std::vector<std::unique_ptr<CBlockIndex>> g_w12_blocks;

CBlockIndex* MakeIndex(CBlockIndex* prev, int height, uint32_t nTime, int32_t nVersion = VERSIONBITS_TOP_BITS)
{
    auto idx = std::make_unique<CBlockIndex>();
    idx->pprev = prev;
    idx->nHeight = height;
    idx->nTime = nTime;
    idx->nBits = WAVE12_BITS;
    idx->nVersion = nVersion;
    idx->BuildSkip();
    CBlockIndex* raw = idx.get();
    g_w12_blocks.push_back(std::move(idx));
    return raw;
}

// Builds a chain of `count` synthetic CBlockIndex starting at `start_height`
// with monotonically increasing timestamps. Each block carries the supplied
// nVersion so signaling vs non-signaling can be controlled per slot.
CBlockIndex* MakeChain(int start_height, int count, int64_t base_time, int32_t nVersion)
{
    CBlockIndex* tip = nullptr;
    int64_t t = base_time;
    for (int i = 0; i < count; ++i) {
        tip = MakeIndex(tip, start_height + i, static_cast<uint32_t>(t), nVersion);
        t += 600;
    }
    return tip;
}

// MuSig2 signing helpers shared with digidollar_oracle_bundle_matrix_tests.
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
    for (uint8_t id : oracle_ids) bitmap[id / 8] |= static_cast<unsigned char>(1U << (id % 8));
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
    for (size_t i = 0; i < n_signers; ++i) pubkey_ptrs[i] = &pubkeys[i];
    secp256k1_xonly_pubkey agg_pk{};
    secp256k1_musig_keyagg_cache cache{};
    if (!secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache, pubkey_ptrs.data(), n_signers)) {
        secp256k1_context_destroy(ctx);
        return false;
    }
    std::vector<secp256k1_musig_secnonce> secnonces(n_signers);
    std::vector<secp256k1_musig_pubnonce> pubnonces(n_signers);
    for (size_t i = 0; i < n_signers; ++i) {
        unsigned char rnd[32];
        GetStrongRandBytes(Span{rnd, 32});
        if (!secp256k1_musig_nonce_gen(ctx, &secnonces[i], &pubnonces[i],
                                       rnd, seckeys[i].data(), &pubkeys[i], nullptr, &cache, nullptr)) {
            secp256k1_context_destroy(ctx);
            return false;
        }
    }
    std::vector<const secp256k1_musig_pubnonce*> nonce_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; ++i) nonce_ptrs[i] = &pubnonces[i];
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
    std::vector<secp256k1_musig_partial_sig> partials(n_signers);
    std::vector<const secp256k1_musig_partial_sig*> partial_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; ++i) {
        if (!secp256k1_musig_partial_sign(ctx, &partials[i], &secnonces[i],
                                          &keypairs[i], &cache, &session)) {
            secp256k1_context_destroy(ctx);
            return false;
        }
        partial_ptrs[i] = &partials[i];
    }
    bundle.participation_bitmap = EncodeBitmap(oracle_ids, total_oracles);
    bundle.aggregate_sig.assign(64, 0);
    if (!secp256k1_musig_partial_sig_agg(ctx, bundle.aggregate_sig.data(),
                                         &session, partial_ptrs.data(), n_signers)) {
        secp256k1_context_destroy(ctx);
        return false;
    }
    const bool ok = secp256k1_schnorrsig_verify(ctx, bundle.aggregate_sig.data(), msg32, 32, &agg_pk);
    secp256k1_context_destroy(ctx);
    return ok;
}

COracleBundle MakeValidV03Bundle(int32_t height, int64_t timestamp)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = GetCurrentEpoch(height);
    bundle.median_price_micro_usd = ORACLE_PRICE;
    bundle.timestamp = timestamp;
    std::vector<uint8_t> ids;
    for (uint8_t id = 0; id < consensus.nOracleConsensusRequired; ++id) ids.push_back(id);
    BOOST_REQUIRE(SignV03Bundle(bundle, ids, static_cast<uint16_t>(consensus.nOracleTotalOracles)));
    return bundle;
}

CScript MakeRawV03Script(const COracleBundle& bundle)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{0x03};
    script << bundle.SerializeV03Data();
    return script;
}

CScript MakeOracleLookalikeScript()
{
    // OP_RETURN OP_ORACLE with a single arbitrary push but no recognised
    // version byte. Pre-activation this must be ignored entirely. Post-
    // activation the validator extracts/dispatches and must reject.
    CScript s;
    s << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{0xAB, 0xCD};
    return s;
}

CTransactionRef MakeDDTransaction(DigiDollarTxType type, uint8_t flags = 0)
{
    CMutableTransaction tx;
    tx.nVersion = MakeDigiDollarVersion(type, flags);
    tx.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx.vout.emplace_back(0, CScript() << OP_TRUE);
    return MakeTransactionRef(std::move(tx));
}

CBlock MakeBlock(int32_t bip34_height,
                 uint32_t block_time,
                 const std::vector<CScript>& oracle_scripts,
                 const std::vector<CTransactionRef>& extra_txs)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << static_cast<int64_t>(bip34_height);
    coinbase.vout.emplace_back(50 * COIN, CScript() << OP_TRUE);
    for (const auto& s : oracle_scripts) coinbase.vout.emplace_back(0, s);

    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime = block_time;
    block.nBits = WAVE12_BITS;
    block.hashPrevBlock.SetNull();
    block.nNonce = 0;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.vtx.insert(block.vtx.end(), extra_txs.begin(), extra_txs.end());
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

struct OracleResult {
    bool ok;
    std::string reject_reason;
};

OracleResult RunValidator(const CBlock& block, const Consensus::Params& params, const CBlockIndex* pindex_prev = nullptr)
{
    BlockValidationState state;
    bool ok = OracleDataValidator::ValidateBlockOracleData(block, pindex_prev, params, state);
    return {ok, state.GetRejectReason()};
}

// Drives BIP9 from DEFINED through ACTIVE on a synthetic chain, returning a
// reference (params, end-of-ACTIVE-tip) so callers can probe IsDigiDollarEnabled
// at boundary heights. Threshold/window are intentionally tiny to keep tests
// deterministic and fast.
struct StateMachineFixture {
    Consensus::Params params;
    CBlockIndex* tip_defined{nullptr};
    CBlockIndex* tip_started{nullptr};
    CBlockIndex* tip_locked_in{nullptr};
    CBlockIndex* tip_active{nullptr};
};

StateMachineFixture BuildStateMachine()
{
    StateMachineFixture fx;
    fx.params = Params().GetConsensus();
    fx.params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = 1'000'000'000;
    fx.params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = 3'000'000'000;
    fx.params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 0;
    fx.params.nRuleChangeActivationThreshold = 3;
    fx.params.nMinerConfirmationWindow = 4;

    const uint32_t signal_bit = 1U << fx.params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].bit;
    const int32_t signaling_version = VERSIONBITS_TOP_BITS | static_cast<int32_t>(signal_bit);

    // Phase 1: 11 blocks before nStartTime → DEFINED at the next period boundary.
    CBlockIndex* tip = nullptr;
    for (int i = 0; i < 11; ++i) {
        tip = MakeIndex(tip, i, static_cast<uint32_t>(999'999'900 + i * 10));
    }
    fx.tip_defined = tip;

    // Phase 2: 10 blocks past nStartTime → STARTED at next period boundary.
    for (int i = 0; i < 10; ++i) {
        tip = MakeIndex(tip, tip->nHeight + 1, static_cast<uint32_t>(1'000'000'100 + i * 10));
    }
    fx.tip_started = tip;

    // Phase 3: signal in 3 of 4 slots in the next period → LOCKED_IN.
    for (int i = 0; i < 3; ++i) {
        tip = MakeIndex(tip, tip->nHeight + 1, static_cast<uint32_t>(1'500'000'001 + i * 10), signaling_version);
    }
    tip = MakeIndex(tip, tip->nHeight + 1, 1'500'000'031);
    fx.tip_locked_in = tip;

    // Phase 4: another full period → ACTIVE.
    for (int i = 0; i < 4; ++i) {
        tip = MakeIndex(tip, tip->nHeight + 1, static_cast<uint32_t>(1'500'000'100 + i * 10));
    }
    fx.tip_active = tip;
    return fx;
}

} // namespace wave12

BOOST_FIXTURE_TEST_SUITE(digidollar_activation_wave12_tests, RegTestingSetup)

// =============================================================================
// PART 1 — Activation predicate boundary (BIP9-driven)
// =============================================================================

// State predicate flow: every transition is exact and IsDigiDollarEnabled is
// false until ACTIVE, true on ACTIVE.
BOOST_AUTO_TEST_CASE(wave12_state_machine_predicates_match_bip9_states)
{
    wave12::StateMachineFixture fx = wave12::BuildStateMachine();
    VersionBitsCache cache;

    BOOST_CHECK_EQUAL(cache.State(fx.tip_defined, fx.params, Consensus::DEPLOYMENT_DIGIDOLLAR), ThresholdState::DEFINED);
    BOOST_CHECK(!DigiDollar::IsDigiDollarEnabled(fx.tip_defined, fx.params));

    BOOST_CHECK_EQUAL(cache.State(fx.tip_started, fx.params, Consensus::DEPLOYMENT_DIGIDOLLAR), ThresholdState::STARTED);
    BOOST_CHECK(!DigiDollar::IsDigiDollarEnabled(fx.tip_started, fx.params));

    BOOST_CHECK_EQUAL(cache.State(fx.tip_locked_in, fx.params, Consensus::DEPLOYMENT_DIGIDOLLAR), ThresholdState::LOCKED_IN);
    BOOST_CHECK(!DigiDollar::IsDigiDollarEnabled(fx.tip_locked_in, fx.params));

    BOOST_CHECK_EQUAL(cache.State(fx.tip_active, fx.params, Consensus::DEPLOYMENT_DIGIDOLLAR), ThresholdState::ACTIVE);
    BOOST_CHECK(DigiDollar::IsDigiDollarEnabled(fx.tip_active, fx.params));
}

// Off-by-one boundary on the BIP9-active side: the very first block AFTER the
// transition has IsDigiDollarEnabled=true; the last block before has false.
BOOST_AUTO_TEST_CASE(wave12_bip9_off_by_one_boundary)
{
    wave12::StateMachineFixture fx = wave12::BuildStateMachine();

    // tip_locked_in is height 24, the period boundary that flipped to ACTIVE
    // at height 27 in BuildStateMachine. DigiDollar enforces activation
    // *after* ACTIVE — so:
    //   prev=tip_locked_in     -> still LOCKED_IN  -> false
    //   prev=tip_active        -> ACTIVE           -> true
    BOOST_CHECK(!DigiDollar::IsDigiDollarEnabled(fx.tip_locked_in, fx.params));
    BOOST_CHECK(DigiDollar::IsDigiDollarEnabled(fx.tip_active, fx.params));

    // Walk every height from tip_locked_in up to tip_active and confirm that
    // IsDigiDollarEnabled is monotone — at most one transition false→true
    // and never true→false. CBlockIndex does not maintain a forward pointer
    // in this test fixture, so we collect the chain by walking pprev from
    // tip_active and reversing. (synthetic chains do not have pnext set.)
    std::vector<CBlockIndex*> chain;
    for (CBlockIndex* p = fx.tip_active; p != nullptr; p = p->pprev) chain.push_back(p);
    std::reverse(chain.begin(), chain.end());

    bool prev_state = false;
    int transitions = 0;
    bool seen_true = false;
    for (CBlockIndex* idx : chain) {
        const bool now = DigiDollar::IsDigiDollarEnabled(idx, fx.params);
        if (idx != chain.front() && now != prev_state) ++transitions;
        BOOST_CHECK_MESSAGE(!(seen_true && !now),
            "IsDigiDollarEnabled must be monotone — observed true→false at h=" << idx->nHeight);
        if (now) seen_true = true;
        prev_state = now;
    }
    BOOST_CHECK_LE(transitions, 1);
    BOOST_CHECK(DigiDollar::IsDigiDollarEnabled(fx.tip_active, fx.params));
}

// =============================================================================
// PART 2 — Height-gate predicates (oracle/MuSig2)
// =============================================================================

// Regtest intentionally has two activation surfaces:
//   - oracle P2P/height gate remains at nDDActivationHeight=650
//   - MuSig2 follows the effective DigiDollar BIP9 boundary, which is 0 for
//     default ALWAYS_ACTIVE regtest
// Off-by-one in either direction is asserted explicitly.
BOOST_AUTO_TEST_CASE(wave12_height_gates_off_by_one_regtest)
{
    const Consensus::Params& params = Params().GetConsensus();
    BOOST_REQUIRE_EQUAL(params.nDDActivationHeight, 650);
    BOOST_REQUIRE_EQUAL(params.nOracleActivationHeight, params.nDDActivationHeight);
    BOOST_REQUIRE_EQUAL(
        params.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime,
        Consensus::BIP9Deployment::ALWAYS_ACTIVE);
    BOOST_REQUIRE_EQUAL(params.nDigiDollarMuSig2Height, 0);

    BOOST_CHECK(!Consensus::IsOracleActive(params, params.nDDActivationHeight - 1));
    BOOST_CHECK(Consensus::IsOracleActive(params, params.nDDActivationHeight));
    BOOST_CHECK(Consensus::IsOracleActive(params, params.nDDActivationHeight + 1));

    BOOST_CHECK(!Consensus::IsMuSig2Active(params, params.nDigiDollarMuSig2Height - 1));
    BOOST_CHECK(Consensus::IsMuSig2Active(params, params.nDigiDollarMuSig2Height));
    BOOST_CHECK(Consensus::IsMuSig2Active(params, params.nDDActivationHeight - 1));
    BOOST_CHECK(Consensus::IsMuSig2Active(params, params.nDDActivationHeight));
    BOOST_CHECK(Consensus::IsMuSig2Active(params, params.nDDActivationHeight + 100));
}

// =============================================================================
// PART 3 — ValidateBlockOracleData boundary (BIP34 height path, nullptr prev)
// =============================================================================

// At h = nDDActivationHeight - 1, every shape of block (DD-touching, missing
// oracle, malformed oracle, multi-oracle, etc.) is accepted. The validator
// MUST short-circuit before any DD reject reason is emitted.
BOOST_AUTO_TEST_CASE(wave12_preactivation_validator_short_circuits_all_shapes)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_pre = params.nDDActivationHeight - 1;
    BOOST_REQUIRE_GE(h_pre, 1);
    const uint32_t t = static_cast<uint32_t>(GetTime());

    // Shape A: DD-touching block, no oracle output, pre-activation.
    {
        CBlock block = wave12::MakeBlock(h_pre, t, {}, {wave12::MakeDDTransaction(DD_TX_MINT)});
        wave12::OracleResult r = wave12::RunValidator(block, params);
        BOOST_TEST_MESSAGE("  pre-activation DD mint, no oracle: ok=" << r.ok << " reject='" << r.reject_reason << "'");
        BOOST_CHECK(r.ok);
        BOOST_CHECK_EQUAL(r.reject_reason, "");
    }

    // Shape B: non-DD block with two oracle outputs (would be rejected with
    // bad-oracle-multiple-outputs post-activation).
    {
        const COracleBundle good = wave12::MakeValidV03Bundle(params.nDDActivationHeight + 1, t);
        const CScript a = wave12::MakeRawV03Script(good);
        const CScript b = wave12::MakeRawV03Script(good);
        CBlock block = wave12::MakeBlock(h_pre, t, {a, b}, {});
        wave12::OracleResult r = wave12::RunValidator(block, params);
        BOOST_TEST_MESSAGE("  pre-activation multi-oracle, no DD: ok=" << r.ok << " reject='" << r.reject_reason << "'");
        BOOST_CHECK(r.ok);
        BOOST_CHECK_EQUAL(r.reject_reason, "");
    }

    // Shape C: pre-activation OP_ORACLE-shaped output with invalid version.
    {
        CBlock block = wave12::MakeBlock(h_pre, t, {wave12::MakeOracleLookalikeScript()}, {});
        wave12::OracleResult r = wave12::RunValidator(block, params);
        BOOST_CHECK(r.ok);
        BOOST_CHECK_EQUAL(r.reject_reason, "");
    }
}

// At h = nDDActivationHeight, the FIRST DD-touching block must succeed when
// the bundle is valid v0x03. This pins the activation-block success path:
// no off-by-one allows DD blocks to be rejected at the boundary.
BOOST_AUTO_TEST_CASE(wave12_first_dd_block_at_activation_succeeds_with_valid_bundle)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    const COracleBundle good = wave12::MakeValidV03Bundle(h_act, t);
    const CScript good_script = wave12::MakeRawV03Script(good);
    CBlock block = wave12::MakeBlock(h_act, t, {good_script},
                                     {wave12::MakeDDTransaction(DD_TX_MINT)});
    wave12::OracleResult r = wave12::RunValidator(block, params);
    BOOST_TEST_MESSAGE("  first DD block at h=" << h_act << ": ok=" << r.ok << " reject='" << r.reject_reason << "'");
    BOOST_CHECK_MESSAGE(r.ok, "first DD-touching block at activation must accept valid v0x03 bundle, got reject='"
                              << r.reject_reason << "'");
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// At h = nDDActivationHeight, a DD-touching block missing the bundle MUST be
// rejected (negative boundary). Mirrors Wave 8 acceptance matrix but pinned to
// the exact activation height.
BOOST_AUTO_TEST_CASE(wave12_first_dd_block_at_activation_missing_bundle_rejected)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    CBlock block = wave12::MakeBlock(h_act, t, {}, {wave12::MakeDDTransaction(DD_TX_MINT)});
    wave12::OracleResult r = wave12::RunValidator(block, params);
    BOOST_TEST_MESSAGE("  first DD block at h=" << h_act << " no bundle: ok=" << r.ok << " reject='" << r.reject_reason << "'");
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-missing");
}

// At h = nDDActivationHeight - 1, a DD-marker tx in a block ALONE does not
// trigger any DD reject reason from ValidateBlockOracleData. This is the
// "non-DD backward compatibility" surface for the validator hook.
BOOST_AUTO_TEST_CASE(wave12_pre_activation_dd_marker_block_not_rejected_for_dd)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_pre = params.nDDActivationHeight - 1;
    BOOST_REQUIRE_GE(h_pre, 1);
    const uint32_t t = static_cast<uint32_t>(GetTime());

    CBlock block = wave12::MakeBlock(h_pre, t, {}, {wave12::MakeDDTransaction(DD_TX_TRANSFER)});
    wave12::OracleResult r = wave12::RunValidator(block, params);

    // Even though the block touches DD, the pre-activation gate exits before
    // BlockTouchesDigiDollar is consulted. Reject reason must be empty.
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// =============================================================================
// PART 4 — ValidateBlockOracleData via pindex_prev (BIP9 path)
// =============================================================================

// When the validator is given pindex_prev, it consults
// IsDigiDollarEnabled(pindex_prev, params) instead of the BIP34 height fall-
// back. On regtest BIP9 is ALWAYS_ACTIVE, so even at heights below
// nDDActivationHeight the BIP9 gate fires — and a DD-touching block without a
// bundle is rejected. This documents the rh51 split: the nullptr-path uses
// nDDActivationHeight as the height gate but the pindex_prev-path uses BIP9
// directly, so the two paths diverge on regtest for blocks 0..649.
//
// The expected behaviour is therefore intentionally asymmetric: the
// nullptr-path test (PART 3) accepts pre-activation DD blocks; the prev-path
// test rejects them when BIP9 already considers DD enabled. Pinning both
// halves prevents accidental refactors that flatten the split into one
// behaviour.
BOOST_AUTO_TEST_CASE(wave12_validator_prev_path_uses_bip9_not_height_gate)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_pre = params.nDDActivationHeight - 1;
    BOOST_REQUIRE_GE(h_pre, 1);
    BOOST_REQUIRE(DigiDollar::IsDigiDollarEnabled(/*pindexPrev=*/nullptr, params));
    const uint32_t t = static_cast<uint32_t>(GetTime());

    CBlockIndex* prev = wave12::MakeIndex(nullptr, h_pre - 1, t - 600);
    BOOST_REQUIRE(DigiDollar::IsDigiDollarEnabled(prev, params));
    CBlock block = wave12::MakeBlock(h_pre, t, {}, {wave12::MakeDDTransaction(DD_TX_MINT)});
    wave12::OracleResult r = wave12::RunValidator(block, params, prev);

    // On regtest with BIP9 ALWAYS_ACTIVE, the prev-path validator considers
    // DD enabled and enforces the bundle requirement. This is the rh51-
    // documented asymmetry vs the nullptr-path height short-circuit at
    // h < 650 that PART 3 exercises.
    BOOST_TEST_MESSAGE("  regtest prev-path @ h=" << h_pre
                       << " (BIP9 active): ok=" << r.ok
                       << " reject='" << r.reject_reason << "'");
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-missing");
}

// The mirror of the prev-path test at exactly the boundary: prev->nHeight =
// nDDActivationHeight - 1, building block at nDDActivationHeight. Whether the
// height-gate or the BIP9-gate fires, the FIRST DD-touching block at the
// activation height MUST be enforced. This is the canonical "first DD block"
// boundary check on the prev path.
BOOST_AUTO_TEST_CASE(wave12_validator_with_pindex_prev_at_activation_enforces)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    CBlockIndex* prev = wave12::MakeIndex(nullptr, h_act - 1, t - 600);
    CBlock block = wave12::MakeBlock(h_act, t, {}, {wave12::MakeDDTransaction(DD_TX_MINT)});
    wave12::OracleResult r = wave12::RunValidator(block, params, prev);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-missing");
}

// =============================================================================
// PART 5 — Non-DD blocks remain valid across the boundary (no oracle work)
// =============================================================================

// At every height around the boundary, a plain DGB block (no DD tx, no oracle
// output) is accepted. Pins the "non-DD users still work" property called out
// in MVP invariant 15 / launch readiness clause.
BOOST_AUTO_TEST_CASE(wave12_non_dd_blocks_accepted_across_boundary)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    for (int delta : {-2, -1, 0, +1, +2}) {
        const int32_t h = h_act + delta;
        if (h < 1) continue;
        CBlock block = wave12::MakeBlock(h, t, {}, {});
        wave12::OracleResult r = wave12::RunValidator(block, params);
        BOOST_TEST_MESSAGE("  non-DD block at h=" << h << " (delta=" << delta << "): ok=" << r.ok
                           << " reject='" << r.reject_reason << "'");
        BOOST_CHECK_MESSAGE(r.ok, "non-DD block at h=" << h << " must validate, reject='"
                                  << r.reject_reason << "'");
        BOOST_CHECK_EQUAL(r.reject_reason, "");
    }
}

// =============================================================================
// PART 6 — CheckMuSig2OracleBundleVersion boundary
// =============================================================================

// Forward-declare the static helper exported through validation.cpp's
// translation unit; we reach it via a thin wrapper rather than re-implementing
// it. Direct call isn't visible at link time because it's static, so we
// exercise its branch through ValidateBlockOracleData (which shares the
// nullptr/pindex_prev dispatch and BIP9/height gate). Track DD-FA-TEST-014 for
// a future direct probe if we ever export the helper.

// Pre-activation, a coinbase that already carries an OP_ORACLE bundle of any
// shape must still leave block validation untouched at the ValidateBlockOracle
// hook. This exercises the same gate CheckMuSig2OracleBundleVersion uses.
BOOST_AUTO_TEST_CASE(wave12_preactivation_oracle_lookalike_coinbase_ignored)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_pre = params.nDDActivationHeight - 1;
    BOOST_REQUIRE_GE(h_pre, 1);

    // Even an oracle-shaped output looks like a valid v0x03 (which would be
    // post-activation enforceable) is ignored at h_pre because the gate
    // returns true before extraction.
    const COracleBundle good = wave12::MakeValidV03Bundle(params.nDDActivationHeight + 1, GetTime());
    const CScript good_script = wave12::MakeRawV03Script(good);
    CBlock block = wave12::MakeBlock(h_pre, static_cast<uint32_t>(GetTime()),
                                     {good_script}, {});
    wave12::OracleResult r = wave12::RunValidator(block, params);
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// =============================================================================
// PART 7 — Multi-version interactions (DD-marker tx + invalid OP_RETURN)
// =============================================================================

// Pre-activation: a block whose only DD-shaped artefact is the nVersion
// marker (no DD OP_RETURN, no oracle output) must NOT be rejected for any
// DD-related reason. The non-upgraded base chain semantics apply.
BOOST_AUTO_TEST_CASE(wave12_preactivation_dd_marker_only_not_rejected_for_dd)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_pre = params.nDDActivationHeight - 1;
    BOOST_REQUIRE_GE(h_pre, 1);
    const uint32_t t = static_cast<uint32_t>(GetTime());

    // DD-marker tx with absolutely no DD OP_RETURN. Pre-activation, the
    // ValidateBlockOracleData hook should never reach the
    // BlockTouchesDigiDollar branch. This pins the contract that the BIP9
    // gate is the ONLY boundary, not the marker.
    CBlock block = wave12::MakeBlock(h_pre, t, {}, {wave12::MakeDDTransaction(DD_TX_REDEEM)});
    wave12::OracleResult r = wave12::RunValidator(block, params);
    BOOST_CHECK(r.ok);
    BOOST_CHECK_NE(r.reject_reason, "bad-oracle-missing");
    BOOST_CHECK_NE(r.reject_reason, "bad-oracle-malformed");
    BOOST_CHECK_NE(r.reject_reason, "bad-oracle-legacy");
    BOOST_CHECK_NE(r.reject_reason, "bad-oracle-musig2");
}

// Post-activation: same DD-marker tx + missing DD OP_RETURN reaches the V1
// rejection (bad-oracle-missing — the validator never gets to OP_RETURN
// validation because the bundle gate fires first).
BOOST_AUTO_TEST_CASE(wave12_postactivation_dd_marker_no_bundle_rejected_for_oracle)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    CBlock block = wave12::MakeBlock(h_act, t, {}, {wave12::MakeDDTransaction(DD_TX_REDEEM)});
    wave12::OracleResult r = wave12::RunValidator(block, params);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-missing");
}

// =============================================================================
// PART 8 — Mainnet/testnet activation parameter sanity (no consensus split)
// =============================================================================

// Pin the chainparams so that MuSig2 follows the effective DigiDollar
// activation boundary on every network. On mainnet/testnet that is the same
// numeric height as nDDActivationHeight; default regtest is special because
// DigiDollar BIP9 is ALWAYS_ACTIVE at height 0 while the oracle P2P height gate
// remains at 650 for local testing.
BOOST_AUTO_TEST_CASE(wave12_chainparams_collapsed_activation_triggers)
{
    struct Expected {
        ChainType chain;
        int dd_height;
        int oracle_height;
        int musig2_height;
    } cases[] = {
        {ChainType::REGTEST, 650, 650, 0},
        {ChainType::TESTNET, 600, 600, 600},
        {ChainType::MAIN, 23627520, 23627520, 23627520},
    };

    for (const auto& c : cases) {
        SelectParams(c.chain);
        const auto& p = Params().GetConsensus();
        BOOST_TEST_MESSAGE("  chain=" << static_cast<int>(c.chain)
                           << " dd=" << p.nDDActivationHeight
                           << " oracle=" << p.nOracleActivationHeight
                           << " musig2=" << p.nDigiDollarMuSig2Height);
        BOOST_CHECK_EQUAL(p.nDDActivationHeight, c.dd_height);
        BOOST_CHECK_EQUAL(p.nOracleActivationHeight, c.oracle_height);
        BOOST_CHECK_EQUAL(p.nDigiDollarMuSig2Height, c.musig2_height);
        // Oracle P2P height never lives below the DD height gate; otherwise
        // the oracle message surface could open before the static DD gate.
        BOOST_CHECK_GE(p.nOracleActivationHeight, p.nDDActivationHeight);
        if (c.chain == ChainType::REGTEST) {
            BOOST_CHECK_EQUAL(
                p.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime,
                Consensus::BIP9Deployment::ALWAYS_ACTIVE);
            BOOST_CHECK_EQUAL(
                p.nDigiDollarMuSig2Height,
                p.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height);
        } else {
            BOOST_CHECK_EQUAL(p.nDigiDollarMuSig2Height, p.nDDActivationHeight);
        }
    }

    // Restore regtest at the end of the case so the suite fixture's
    // expectations are not perturbed for subsequent cases.
    SelectParams(ChainType::REGTEST);
}

// On mainnet, BIP9 min_activation_height MUST equal nDDActivationHeight
// because the runtime checks both predicates and a mismatch creates a
// pre-/post-activation window where the two disagree. Regression-pin this
// invariant so tunings keep them aligned.
BOOST_AUTO_TEST_CASE(wave12_mainnet_bip9_minheight_matches_nDDActivationHeight)
{
    SelectParams(ChainType::MAIN);
    const auto& p = Params().GetConsensus();
    const int min_height = p.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height;
    BOOST_CHECK_EQUAL(min_height, p.nDDActivationHeight);
    SelectParams(ChainType::REGTEST);
}

// On testnet, same invariant: the BIP9 min activation height matches
// nDDActivationHeight so a node that boots with stale chainparams cannot
// straddle the boundary.
BOOST_AUTO_TEST_CASE(wave12_testnet_bip9_minheight_matches_nDDActivationHeight)
{
    SelectParams(ChainType::TESTNET);
    const auto& p = Params().GetConsensus();
    const int min_height = p.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height;
    BOOST_CHECK_EQUAL(min_height, p.nDDActivationHeight);
    SelectParams(ChainType::REGTEST);
}

// =============================================================================
// PART 9 — DD-FA-SEC-010: SpendsDigiDollarCollateralVault activation-height
// optimization must not falsely skip vaults that BIP9 already considers
// post-activation.
// =============================================================================
//
// `SpendsDigiDollarCollateralVault` short-circuits when `coin.nHeight <
// nDDActivationHeight` as a performance optimization (skip the
// `IsMintCollateralOutput` tx-db lookup for coins minted before DigiDollar
// could possibly exist).
//
// On mainnet/testnet `nDDActivationHeight == BIP9 min_activation_height`, so
// the optimization is equivalent to "BIP9 was not yet ACTIVE for this coin"
// and is safe.
//
// On regtest with default settings BIP9 is ALWAYS_ACTIVE
// (min_activation_height=0) but `nDDActivationHeight=650`. A vault minted
// during the IBD/catch-up window where ConnectBlock skips oracle validation
// could land at coin.nHeight < 650. The optimization then *skips* the vault
// detection for that coin, and a later non-DD spend bypasses the
// `bad-collateral-spend-missing-dd-burn` consensus check.
//
// The fix uses BIP9's min_activation_height when the deployment is
// ALWAYS_ACTIVE, so the optimization never crosses the BIP9 boundary.
// This test pins the post-fix behaviour: a registered vault at coin.nHeight
// = 100 (well below nDDActivationHeight=650, but BIP9-active on regtest)
// MUST be detected as a vault by SpendsDigiDollarCollateralVault.
BOOST_AUTO_TEST_CASE(wave12_regtest_low_height_vault_detection_consistent_with_bip9)
{
    SelectParams(ChainType::REGTEST);
    const CChainParams& chainparams = Params();
    const Consensus::Params& consensus = chainparams.GetConsensus();
    BOOST_REQUIRE_EQUAL(consensus.nDDActivationHeight, 650);
    BOOST_REQUIRE_EQUAL(
        consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime,
        Consensus::BIP9Deployment::ALWAYS_ACTIVE);
    BOOST_REQUIRE(DigiDollar::IsDigiDollarEnabled(/*pindexPrev=*/nullptr, consensus));

    // Forge a vault script and register it. Real mints would set this in
    // RegisterScriptMetadata via ConnectBlock; here we register directly so
    // the test exercises only the SpendsDigiDollarCollateralVault path.
    CScript vault_script;
    vault_script << OP_1 << std::vector<unsigned char>(32, 0xAB);
    DigiDollar::RegisterScriptMetadata(vault_script,
                                       DigiDollar::ScriptType::COLLATERAL_LOCK,
                                       /*ddAmount=*/100'000,
                                       /*lockHeight=*/200);

    BOOST_REQUIRE(DigiDollar::IsRegisteredCollateralVaultScript(vault_script));

    constexpr uint32_t COIN_HEIGHT = 100;
    BOOST_REQUIRE_LT(COIN_HEIGHT, static_cast<uint32_t>(consensus.nDDActivationHeight));

    CCoinsView dummy;
    CCoinsViewCache coins(&dummy);
    const COutPoint vault_outpoint(uint256::ONE, 0);
    CTxOut vault_out(10 * COIN, vault_script);
    coins.AddCoin(vault_outpoint, Coin(vault_out, COIN_HEIGHT, /*fCoinBaseIn=*/false), false);

    CMutableTransaction non_dd_spend;
    non_dd_spend.nVersion = 2;  // Plain DGB tx, no DD marker
    non_dd_spend.vin.emplace_back(vault_outpoint);
    non_dd_spend.vout.emplace_back(9 * COIN, CScript() << OP_TRUE);
    CTransactionRef ref = MakeTransactionRef(std::move(non_dd_spend));

    DigiDollar::ValidationContext ctx(
        /*nHeight=*/1000,
        /*oraclePriceMicroUSD=*/0,
        /*systemCollateral=*/100,
        chainparams,
        &coins,
        /*skipOracle=*/true);

    const bool spends_vault = DigiDollar::SpendsDigiDollarCollateralVault(*ref, ctx);
    BOOST_TEST_MESSAGE("  regtest BIP9-active vault @ coin.nHeight=" << COIN_HEIGHT
                       << " (nDDActivationHeight=" << consensus.nDDActivationHeight
                       << ") detected_as_vault=" << spends_vault);

    BOOST_CHECK_MESSAGE(spends_vault,
        "DD-FA-SEC-010: registered collateral vault at BIP9-active height "
        << COIN_HEIGHT << " must be detected as a vault even when "
           "coin.nHeight < nDDActivationHeight; got false (production gates "
           "would let a non-DD tx spend the vault without burning DD).");

    const bool requires_dd = DigiDollar::RequiresDigiDollarValidation(*ref, ctx);
    BOOST_CHECK_MESSAGE(requires_dd,
        "DD-FA-SEC-010: RequiresDigiDollarValidation must return true for a "
        "non-DD tx that spends a registered vault, otherwise mempool/miner/"
        "ConnectBlock skip the vault burn-enforcement gate.");
}

BOOST_AUTO_TEST_CASE(wave26_regtest_startup_oracle_cache_uses_bip9_boundary)
{
    SelectParams(ChainType::REGTEST);
    const Consensus::Params& consensus = Params().GetConsensus();
    BOOST_REQUIRE_EQUAL(consensus.nDDActivationHeight, 650);
    BOOST_REQUIRE_EQUAL(
        consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime,
        Consensus::BIP9Deployment::ALWAYS_ACTIVE);

    const uint32_t t = static_cast<uint32_t>(GetTime());
    CBlockIndex* block = wave12::MakeChain(0, 101, t - 600 * 100, VERSIONBITS_TOP_BITS);
    CBlockIndex* prev = block->pprev;
    BOOST_REQUIRE(DigiDollar::IsDigiDollarEnabled(prev, consensus));
    BOOST_REQUIRE_LT(block->nHeight, consensus.nDDActivationHeight);

    BOOST_CHECK_MESSAGE(
        OracleBundleManager::ShouldLoadStartupOraclePriceForBlock(block->nHeight, block, consensus),
        "Wave 26: startup oracle cache reconstruction must follow the BIP9 "
        "activation predicate used by ConnectBlock, not raw nDDActivationHeight; "
        "otherwise default-regtest blocks accepted while BIP9 is ALWAYS_ACTIVE "
        "are skipped on restart/reindex.");
}

BOOST_AUTO_TEST_SUITE_END()
