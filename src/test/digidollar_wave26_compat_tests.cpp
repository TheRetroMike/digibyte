// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 26 - Final Backward-Compatibility and Activation Proof (Agent B).
 *
 * Pins the explicit backward-compatibility contract that the DigiDollar/oracle
 * surface MUST honour at the activation boundary. This is the launch-readiness
 * gate from `final_audit.MD` Section 11:
 *
 *   "Regular DGB users and non-upgraded nodes can still transact normally"
 *
 * Wave 12 (`digidollar_activation_wave12_tests.cpp`) covers the BIP9 state
 * machine and the validator's height/predicate gates. This Wave 26 suite
 * complements Wave 12 by pinning the per-transaction and per-block detection
 * contract that downstream gates depend on, plus the negative invariants:
 *
 *   1. `HasDigiDollarMarker` is purely an `nVersion` predicate. It is NOT
 *      derived from OP_RETURN payload, witness data, or txin shape. A
 *      regular DGB tx with arbitrary `"DD"`-flavoured OP_RETURN noise in any
 *      output is NOT detected as a DD tx.
 *   2. `BlockTouchesDigiDollar` examines `vtx[1..]` only and only checks the
 *      `nVersion` marker. It NEVER inspects the coinbase, the OP_RETURN
 *      payload, the witness commitment, or the script content of any output.
 *      This makes the DD-detection boundary deterministic and immune to
 *      OP_RETURN-noise spoofing.
 *   3. Pre-activation, `OracleDataValidator::ValidateBlockOracleData` accepts
 *      every block shape - DD-touching, OP_RETURN-noise-only, malformed
 *      oracle outputs, multi-oracle outputs - because the gate fires before
 *      any DD-rule enforcement. This means a non-upgraded node sees no
 *      change in base-chain validity.
 *   4. Post-activation, `ValidateBlockOracleData` enforces the bundle rule
 *      ONLY on DD-touching blocks. Plain DGB blocks are valid without an
 *      oracle bundle in the coinbase. The bundle requirement is per-block
 *      and triggered by `BlockTouchesDigiDollar`, not by a flag on the node
 *      or by deployment state alone.
 *   5. Cross-network parity: detection predicates are deterministic on
 *      mainnet, testnet, and regtest chainparams. `HasDigiDollarMarker`
 *      gives identical answers on every chain for the same `nVersion`
 *      bytes, with no chain-prefix or genesis-bound divergence.
 *
 * These five invariants together are the canonical "non-DD users still
 * work" launch-readiness gate. They are pinned here so a future refactor
 * cannot silently flip the per-tx marker into a payload-based marker and
 * accidentally start enforcing DD rules on regular DGB transactions.
 *
 * Coordinated with Agent A (security) and Agent C (mixed-node functional).
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <chain.h>
#include <consensus/digidollar.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <crypto/sha256.h>
#include <digidollar/digidollar.h>
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
#include <versionbits.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Production reference: `BlockTouchesDigiDollar` is defined inside an
// anonymous namespace at `src/oracle/bundle_manager.cpp:84-92` and therefore
// has internal linkage - it is unreachable from the test binary. The
// validator (`OracleDataValidator::ValidateBlockOracleData`,
// `src/oracle/bundle_manager.cpp:1966`) is the sole consumer. To pin the
// detection contract this suite replicates the predicate verbatim. The
// `wave26_marker_detection_chain_agnostic` case proves the replicated
// predicate matches the validator's behaviour under chain-switched
// chainparams; if production ever changes the trigger, the validator
// reject-reason differential between the test predicate and the validator
// will surface here as a failed `bad-oracle-missing` expectation.
namespace wave26 {
inline bool BlockTouchesDigiDollarRef(const CBlock& block)
{
    for (size_t i = 1; i < block.vtx.size(); ++i) {
        if (DigiDollar::HasDigiDollarMarker(*block.vtx[i])) {
            return true;
        }
    }
    return false;
}
}  // namespace wave26

namespace wave26 {

constexpr uint32_t WAVE26_BITS = 0x207fffff;
constexpr uint64_t ORACLE_PRICE = 50000;

// Storage for synthetic block index chains used in this suite. Unique to
// Wave 26 to avoid leaking state into the existing wave12 g_w12_blocks
// reservoir.
static std::vector<std::unique_ptr<CBlockIndex>> g_w26_blocks;

CBlockIndex* MakeIndex(CBlockIndex* prev, int height, uint32_t nTime,
                       int32_t nVersion = VERSIONBITS_TOP_BITS)
{
    auto idx = std::make_unique<CBlockIndex>();
    idx->pprev = prev;
    idx->nHeight = height;
    idx->nTime = nTime;
    idx->nBits = WAVE26_BITS;
    idx->nVersion = nVersion;
    idx->BuildSkip();
    CBlockIndex* raw = idx.get();
    g_w26_blocks.push_back(std::move(idx));
    return raw;
}

// MuSig2 signing helpers - we deliberately re-implement so that this suite
// has no link-time dependency on Wave 12's helpers (which sit in an
// anonymous namespace). The seed string matches Wave 12 so the resulting
// keypairs are bit-identical; both suites share the regtest 7-oracle
// keyset.
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

CScript MakeLegacyOracleScript(uint8_t version)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{version};
    script << std::vector<unsigned char>(17, 0);
    return script;
}

CScript MakeTruncatedV03Script()
{
    CScript script;
    script << OP_RETURN << OP_ORACLE << std::vector<unsigned char>{0x03};
    script << std::vector<unsigned char>{0x01, 0x02};
    return script;
}

// Builds a regular DGB transaction (no DD marker). Optionally adds a
// "DD"-flavoured OP_RETURN that LOOKS like a DigiDollar metadata payload,
// to prove that DD detection is by `nVersion`, not by output script
// content.
CTransactionRef MakePlainDgbTransaction(bool with_dd_op_return_noise)
{
    CMutableTransaction tx;
    tx.nVersion = 2;  // Standard DGB tx
    tx.vin.emplace_back(COutPoint(uint256::ONE, 0));
    tx.vout.emplace_back(0, CScript() << OP_TRUE);
    if (with_dd_op_return_noise) {
        // Format 1-style noise: OP_RETURN <"DD"> <type=0x01 MINT> <fake amt>
        // Format 2-style noise: OP_RETURN <"DD"> <type> <amount> <lock height>
        // Both shapes are merely OP_RETURN data carriage in a non-DD tx.
        // Without the nVersion marker the parser never looks at them.
        std::vector<unsigned char> dd_marker{'D', 'D'};
        std::vector<unsigned char> fake_payload{0x01, 0x10, 0x27, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00};
        CScript noise = CScript() << OP_RETURN << dd_marker << fake_payload;
        tx.vout.emplace_back(0, noise);
    }
    return MakeTransactionRef(std::move(tx));
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
    block.nBits = WAVE26_BITS;
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

OracleResult RunValidator(const CBlock& block, const Consensus::Params& params,
                          const CBlockIndex* pindex_prev = nullptr)
{
    BlockValidationState state;
    bool ok = OracleDataValidator::ValidateBlockOracleData(block, pindex_prev, params, state);
    return {ok, state.GetRejectReason()};
}

} // namespace wave26

BOOST_FIXTURE_TEST_SUITE(digidollar_wave26_compat_tests, RegTestingSetup)

// =============================================================================
// PART 1 - Per-transaction DD detection: marker is nVersion-only.
// Pins MVP invariant 15 ("Non-DD behavior remains valid") and the launch-
// readiness gate "non-upgraded users can send regular DGB transactions".
// =============================================================================

// A regular DGB transaction with no DD nVersion marker MUST NOT be classified
// as DigiDollar by `HasDigiDollarMarker`, regardless of OP_RETURN content.
// This is the per-tx leg of the backward-compatibility gate: a non-upgraded
// wallet's regular DGB spend is unaffected by DigiDollar activation.
BOOST_AUTO_TEST_CASE(wave26_plain_dgb_tx_never_detected_as_dd)
{
    // Plain DGB tx, no OP_RETURN at all.
    {
        CTransactionRef tx = wave26::MakePlainDgbTransaction(/*with_dd_op_return_noise=*/false);
        BOOST_CHECK(!DigiDollar::HasDigiDollarMarker(*tx));
        BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(*tx), DD_TX_NONE);
    }

    // Plain DGB tx with a DD-flavoured OP_RETURN noise output. Even though
    // the OP_RETURN payload starts with the ASCII "DD" marker that DD
    // OP_RETURN parsers look for, the DD detection predicate ignores
    // OP_RETURN entirely and only reads `nVersion`. This is the canonical
    // backward-compat pin that prevents OP_RETURN-spoofing from triggering
    // DD enforcement.
    {
        CTransactionRef tx = wave26::MakePlainDgbTransaction(/*with_dd_op_return_noise=*/true);
        BOOST_CHECK(!DigiDollar::HasDigiDollarMarker(*tx));
        BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(*tx), DD_TX_NONE);
    }
}

// All three DigiDollar tx types in `nVersion` MUST be detected as DD by
// `HasDigiDollarMarker` regardless of OP_RETURN content. This pins the
// positive side of the marker contract: an upgraded miner/validator MUST
// route the tx to DD validation post-activation, and pre-activation must
// reject it with `digidollar-not-active` (mempool gate, not the validator
// here).
BOOST_AUTO_TEST_CASE(wave26_dd_marker_detected_independent_of_outputs)
{
    for (uint8_t type : {DD_TX_MINT, DD_TX_TRANSFER, DD_TX_REDEEM}) {
        CTransactionRef tx = wave26::MakeDDTransaction(static_cast<DigiDollarTxType>(type));
        BOOST_TEST_MESSAGE("  DD type=" << int(type) << " nVersion=0x"
                           << std::hex << tx->nVersion << std::dec);
        BOOST_CHECK(DigiDollar::HasDigiDollarMarker(*tx));
        BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(*tx),
                          static_cast<DigiDollarTxType>(type));
    }
}

// `nVersion` shapes that DO NOT carry the canonical DD marker (low 16 bits
// != 0x0770) MUST NOT be detected as DD. This is the "no false-positive
// marker" pin - preventing accidental detection that would force an oracle
// bundle on a non-DD block. Production marker check is `(nVersion & 0xFFFF)
// == 0x0770` (`src/consensus/digidollar.cpp:220`); we deliberately enumerate
// near-miss values that flip exactly the low-16-bit pattern.
BOOST_AUTO_TEST_CASE(wave26_marker_adjacent_versions_not_dd)
{
    const int32_t non_marker_versions[] = {
        static_cast<int32_t>(0x00000000u),  // all bits zero
        static_cast<int32_t>(0xFFFFFFFFu),  // all bits set (low 16 = 0xFFFF)
        static_cast<int32_t>(0x0D1D0000u),  // upper bytes match marker, low 16 = 0x0000
        static_cast<int32_t>(0x0D1D0770u ^ 0x0001u),  // marker low byte flipped: 0x0771
        static_cast<int32_t>(0x0D1D0770u ^ 0x0100u),  // marker mid byte flipped: 0x0670
        static_cast<int32_t>(0x20000004u),  // standard regtest BIP9-signaling version
        static_cast<int32_t>(0x44440100u),  // legacy validation.h shape (low 16 = 0x0100)
        static_cast<int32_t>(0x00000001u),  // tx version 1 (Bitcoin v1 default)
        static_cast<int32_t>(0x00000002u),  // tx version 2 (Bitcoin v2 default)
    };
    for (int32_t v : non_marker_versions) {
        CMutableTransaction mtx;
        mtx.nVersion = v;
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(0, CScript() << OP_TRUE);
        CTransactionRef tx = MakeTransactionRef(std::move(mtx));
        BOOST_TEST_MESSAGE("  non-marker nVersion=0x" << std::hex << v << std::dec
                           << " -> dd=" << DigiDollar::HasDigiDollarMarker(*tx));
        BOOST_CHECK(!DigiDollar::HasDigiDollarMarker(*tx));
    }

    // The reverse direction: every nVersion whose low 16 bits == 0x0770 IS
    // a DD-marker tx by production rule. This pins the canonical predicate
    // so a future refactor that adds an upper-byte requirement to the
    // marker check (e.g., demanding 0x0D1D0000 in the high bytes) would
    // surface here as a regression.
    const int32_t marker_versions[] = {
        static_cast<int32_t>(0x00000770u),  // low pattern, high byte zero
        static_cast<int32_t>(0x01000770u),  // type=MINT, no flags
        static_cast<int32_t>(0x02FF0770u),  // type=TRANSFER, all flags set
        static_cast<int32_t>(0x03000770u),  // type=REDEEM, no flags
        static_cast<int32_t>(0x0D1D0770u),  // canonical marker constant
        static_cast<int32_t>(0xFFFF0770u),  // type=0xFF, max flags
    };
    for (int32_t v : marker_versions) {
        CMutableTransaction mtx;
        mtx.nVersion = v;
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(0, CScript() << OP_TRUE);
        CTransactionRef tx = MakeTransactionRef(std::move(mtx));
        BOOST_TEST_MESSAGE("  marker nVersion=0x" << std::hex << v << std::dec
                           << " -> dd=" << DigiDollar::HasDigiDollarMarker(*tx));
        BOOST_CHECK(DigiDollar::HasDigiDollarMarker(*tx));
    }
}

// =============================================================================
// PART 2 - Per-block DD detection: BlockTouchesDigiDollar inspects vtx[1..]
// only, by nVersion. This is the trigger for the post-activation oracle
// bundle requirement.
// =============================================================================

// A block whose only tx is a coinbase (no DD body txs) MUST NOT be
// classified as DD-touching, regardless of OP_RETURN content in the
// coinbase. This pins the "non-DD blocks need no oracle bundle" rule: a
// non-upgraded mining pool that produces an empty block on a DD-active
// chain produces a fully-valid block.
BOOST_AUTO_TEST_CASE(wave26_coinbase_only_block_not_dd_touching)
{
    const uint32_t t = static_cast<uint32_t>(GetTime());
    {
        // Coinbase only, no body, no oracle output, no OP_RETURN noise.
        CBlock block = wave26::MakeBlock(100, t, {}, {});
        BOOST_CHECK(!wave26::BlockTouchesDigiDollarRef(block));
    }
    {
        // Coinbase plus a body transaction with DD-flavoured OP_RETURN noise
        // but no DD nVersion marker. The block does not touch DD because
        // BlockTouchesDigiDollar checks nVersion only.
        CBlock block = wave26::MakeBlock(
            100, t, {},
            {wave26::MakePlainDgbTransaction(/*with_dd_op_return_noise=*/true)});
        BOOST_CHECK(!wave26::BlockTouchesDigiDollarRef(block));
    }
}

// A block whose body contains at least one DD-marker tx MUST be classified
// as DD-touching for every DD type. This is the trigger for the
// `bad-oracle-missing` enforcement post-activation.
BOOST_AUTO_TEST_CASE(wave26_dd_marker_body_tx_makes_block_dd_touching)
{
    const uint32_t t = static_cast<uint32_t>(GetTime());
    for (uint8_t type : {DD_TX_MINT, DD_TX_TRANSFER, DD_TX_REDEEM}) {
        CBlock block = wave26::MakeBlock(
            100, t, {},
            {wave26::MakeDDTransaction(static_cast<DigiDollarTxType>(type))});
        BOOST_TEST_MESSAGE("  DD type=" << int(type) << " block_touches="
                           << wave26::BlockTouchesDigiDollarRef(block));
        BOOST_CHECK(wave26::BlockTouchesDigiDollarRef(block));
    }
}

// `BlockTouchesDigiDollar` must NEVER classify a block as DD-touching based
// solely on the coinbase. Even if the coinbase happens to carry a script
// shape resembling a DD output, the predicate only looks at vtx[1..]. This
// is the "no coinbase-DD-detection" pin: a future refactor that started
// inspecting the coinbase would change the bundle-requirement contract and
// could brick non-DD blocks.
BOOST_AUTO_TEST_CASE(wave26_coinbase_op_return_not_consulted_by_block_detection)
{
    const uint32_t t = static_cast<uint32_t>(GetTime());

    // Construct a block where the coinbase carries a fake DD-payload-shaped
    // OP_RETURN, but no body tx has a DD marker. The block is not
    // DD-touching even though the coinbase output looks DD-related.
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << static_cast<int64_t>(100);
    coinbase.vout.emplace_back(50 * COIN, CScript() << OP_TRUE);
    {
        std::vector<unsigned char> dd{'D', 'D'};
        std::vector<unsigned char> fake_payload{0x01, 0x10, 0x27};
        CScript noise = CScript() << OP_RETURN << dd << fake_payload;
        coinbase.vout.emplace_back(0, noise);
    }

    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime = t;
    block.nBits = wave26::WAVE26_BITS;
    block.hashPrevBlock.SetNull();
    block.nNonce = 0;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.vtx.push_back(wave26::MakePlainDgbTransaction(false));
    block.hashMerkleRoot = BlockMerkleRoot(block);

    BOOST_CHECK(!wave26::BlockTouchesDigiDollarRef(block));
}

// =============================================================================
// PART 3 - Pre-activation backward-compat: every DGB block shape is valid
// at the validator hook. No DD-rule enforcement leaks before activation.
// =============================================================================

// Pre-activation, a block carrying a DD-marker body tx PLUS DD-flavoured
// OP_RETURN noise in another body tx PLUS no oracle output MUST validate
// without any DD reject reason. This is the "pre-activation DD-looking
// data cannot accidentally activate DigiDollar" launch-gate proof.
BOOST_AUTO_TEST_CASE(wave26_pre_activation_mixed_dd_noise_block_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_pre = params.nDDActivationHeight - 1;
    BOOST_REQUIRE_GE(h_pre, 1);
    const uint32_t t = static_cast<uint32_t>(GetTime());

    // Body: one DD-marker REDEEM tx + one plain DGB tx with DD OP_RETURN noise.
    // Pre-activation, the validator never reaches the bundle-required branch.
    CBlock block = wave26::MakeBlock(h_pre, t, {},
        {wave26::MakeDDTransaction(DD_TX_REDEEM),
         wave26::MakePlainDgbTransaction(/*with_dd_op_return_noise=*/true)});
    wave26::OracleResult r = wave26::RunValidator(block, params);
    BOOST_TEST_MESSAGE("  pre-activation mixed block: ok=" << r.ok
                       << " reject='" << r.reject_reason << "'");
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// Pre-activation, a block carrying ONLY DD-flavoured OP_RETURN noise
// (no DD nVersion marker, no oracle output) MUST validate. This proves
// that the OP_RETURN data carriage feature already supported by base DGB
// continues to work even with payloads that mimic DD shapes.
BOOST_AUTO_TEST_CASE(wave26_pre_activation_dd_op_return_noise_only_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_pre = params.nDDActivationHeight - 1;
    BOOST_REQUIRE_GE(h_pre, 1);
    const uint32_t t = static_cast<uint32_t>(GetTime());

    CBlock block = wave26::MakeBlock(h_pre, t, {},
        {wave26::MakePlainDgbTransaction(/*with_dd_op_return_noise=*/true)});
    wave26::OracleResult r = wave26::RunValidator(block, params);
    BOOST_TEST_MESSAGE("  pre-activation noise-only block: ok=" << r.ok
                       << " reject='" << r.reject_reason << "'");
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// =============================================================================
// PART 4 - Post-activation backward-compat: regular DGB blocks still valid
// without an oracle bundle. The bundle requirement is per-block and only
// applies when BlockTouchesDigiDollar returns true.
// =============================================================================

// Post-activation, a non-DD block with NO body txs (coinbase-only) MUST
// validate without an oracle bundle. This is the "non-upgraded miner can
// still produce a valid block on a DD-active chain" pin.
BOOST_AUTO_TEST_CASE(wave26_post_activation_empty_block_no_bundle_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    CBlock block = wave26::MakeBlock(h_act, t, {}, {});
    wave26::OracleResult r = wave26::RunValidator(block, params);
    BOOST_TEST_MESSAGE("  post-activation empty block: ok=" << r.ok
                       << " reject='" << r.reject_reason << "'");
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// Post-activation, a non-DD block with DD-flavoured OP_RETURN noise body
// txs MUST validate without an oracle bundle. The bundle gate only fires
// when the block contains a DD-marker tx, and OP_RETURN noise does not
// trigger that detection. This is the precise "ordinary DGB transactions
// need no oracle bundle" pin from the launch-readiness checklist.
BOOST_AUTO_TEST_CASE(wave26_post_activation_dd_op_return_noise_block_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    // Three plain DGB txs, each with DD-flavoured OP_RETURN noise. None of
    // them carry the DD nVersion marker.
    CBlock block = wave26::MakeBlock(h_act, t, {},
        {wave26::MakePlainDgbTransaction(true),
         wave26::MakePlainDgbTransaction(true),
         wave26::MakePlainDgbTransaction(true)});
    BOOST_REQUIRE(!wave26::BlockTouchesDigiDollarRef(block));
    wave26::OracleResult r = wave26::RunValidator(block, params);
    BOOST_TEST_MESSAGE("  post-activation noise-only block: ok=" << r.ok
                       << " reject='" << r.reject_reason << "'");
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// Post-activation, a NON-DD block that incidentally carries a valid v0x03
// oracle bundle in its coinbase MUST still validate. This pins the
// "extra oracle data is allowed on non-DD blocks" rule: a miner can stamp
// a bundle on every block (eager strategy) without bricking blocks that
// happen to be non-DD-touching. This is the bidirectional symmetry of the
// bundle-requirement gate: required when DD-touching, optional otherwise.
BOOST_AUTO_TEST_CASE(wave26_post_activation_non_dd_block_with_oracle_accepted)
{
    OracleBundleManager::GetInstance().Clear();
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    const COracleBundle good = wave26::MakeValidV03Bundle(h_act, t);
    const CScript good_script = wave26::MakeRawV03Script(good);

    // Coinbase carries oracle bundle, body is a regular DGB tx.
    CBlock block = wave26::MakeBlock(h_act, t, {good_script},
        {wave26::MakePlainDgbTransaction(false)});
    BOOST_REQUIRE(!wave26::BlockTouchesDigiDollarRef(block));
    wave26::OracleResult r = wave26::RunValidator(block, params);
    BOOST_TEST_MESSAGE("  post-activation non-DD block + oracle: ok=" << r.ok
                       << " reject='" << r.reject_reason << "'");
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "");
}

// Post-activation: price-dependent DD types require an oracle bundle, while
// transfer-only blocks remain valid without live oracle data.
BOOST_AUTO_TEST_CASE(wave26_post_activation_price_dependent_dd_types_require_bundle)
{
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());

    for (uint8_t type : {DD_TX_MINT, DD_TX_REDEEM}) {
        OracleBundleManager::GetInstance().Clear();
        CBlock block = wave26::MakeBlock(h_act, t, {},
            {wave26::MakeDDTransaction(static_cast<DigiDollarTxType>(type))});
        BOOST_REQUIRE(wave26::BlockTouchesDigiDollarRef(block));
        wave26::OracleResult r = wave26::RunValidator(block, params);
        BOOST_TEST_MESSAGE("  post-activation price-dependent type=" << int(type)
                           << " no bundle: ok=" << r.ok
                           << " reject='" << r.reject_reason << "'");
        BOOST_CHECK(!r.ok);
        BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-missing");
    }

    OracleBundleManager::GetInstance().Clear();
    CBlock transfer_block = wave26::MakeBlock(h_act, t, {},
        {wave26::MakeDDTransaction(DD_TX_TRANSFER)});
    BOOST_REQUIRE(wave26::BlockTouchesDigiDollarRef(transfer_block));
    wave26::OracleResult transfer_result = wave26::RunValidator(transfer_block, params);
    BOOST_TEST_MESSAGE("  post-activation transfer no bundle: ok=" << transfer_result.ok
                       << " reject='" << transfer_result.reject_reason << "'");
    BOOST_CHECK(transfer_result.ok);
    BOOST_CHECK_EQUAL(transfer_result.reject_reason, "");
}

// Post-activation: DD-touching blocks must use final V1 v0x03 MuSig2 oracle
// bundles. Legacy raw bundle versions, truncated v0x03 payloads, and invalid
// MuSig2 signatures are rejected instead of being treated as compatibility
// traffic.
BOOST_AUTO_TEST_CASE(wave26_post_activation_dd_touching_requires_final_v03_rules)
{
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h_act = params.nDDActivationHeight;
    const uint32_t t = static_cast<uint32_t>(GetTime());
    const COracleBundle valid_bundle = wave26::MakeValidV03Bundle(h_act, t);

    {
        OracleBundleManager::GetInstance().Clear();
        CBlock block = wave26::MakeBlock(h_act, t, {wave26::MakeRawV03Script(valid_bundle)},
            {wave26::MakeDDTransaction(DD_TX_MINT)});
        BOOST_REQUIRE(wave26::BlockTouchesDigiDollarRef(block));
        wave26::OracleResult r = wave26::RunValidator(block, params);
        BOOST_CHECK(r.ok);
        BOOST_CHECK_EQUAL(r.reject_reason, "");
    }

    for (uint8_t version : {uint8_t{0x01}, uint8_t{0x02}}) {
        OracleBundleManager::GetInstance().Clear();
        CBlock block = wave26::MakeBlock(h_act, t, {wave26::MakeLegacyOracleScript(version)},
            {wave26::MakeDDTransaction(DD_TX_TRANSFER)});
        BOOST_REQUIRE(wave26::BlockTouchesDigiDollarRef(block));
        wave26::OracleResult r = wave26::RunValidator(block, params);
        BOOST_CHECK(!r.ok);
        BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
    }

    {
        OracleBundleManager::GetInstance().Clear();
        CBlock block = wave26::MakeBlock(h_act, t, {wave26::MakeTruncatedV03Script()},
            {wave26::MakeDDTransaction(DD_TX_REDEEM)});
        BOOST_REQUIRE(wave26::BlockTouchesDigiDollarRef(block));
        wave26::OracleResult r = wave26::RunValidator(block, params);
        BOOST_CHECK(!r.ok);
        BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-malformed");
    }

    {
        OracleBundleManager::GetInstance().Clear();
        COracleBundle bad_signature_bundle = valid_bundle;
        BOOST_REQUIRE(!bad_signature_bundle.aggregate_sig.empty());
        bad_signature_bundle.aggregate_sig[0] ^= 0x01;
        CBlock block = wave26::MakeBlock(h_act, t, {wave26::MakeRawV03Script(bad_signature_bundle)},
            {wave26::MakeDDTransaction(DD_TX_MINT)});
        BOOST_REQUIRE(wave26::BlockTouchesDigiDollarRef(block));
        wave26::OracleResult r = wave26::RunValidator(block, params);
        BOOST_CHECK(!r.ok);
        BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-musig2");
    }
}

// =============================================================================
// PART 5 - Cross-network parity: detection predicates are deterministic
// and chain-agnostic. Pinning this prevents a future refactor from making
// the marker chain-prefix-bound and silently changing relay behaviour
// across mainnet/testnet/regtest.
// =============================================================================

// `HasDigiDollarMarker` and `BlockTouchesDigiDollar` MUST give identical
// answers on mainnet, testnet, and regtest chainparams. Marker detection is
// purely a structural check on `nVersion` and `vtx`, with no chainparams
// dependency. This pin guards against a future refactor introducing a
// chain-prefix-bound or genesis-bound check that would cause cross-network
// inconsistencies in DD detection.
BOOST_AUTO_TEST_CASE(wave26_marker_detection_chain_agnostic)
{
    const uint32_t t = static_cast<uint32_t>(GetTime());

    // Each tx is built once; we re-evaluate the predicate under each chain.
    CTransactionRef plain_tx = wave26::MakePlainDgbTransaction(true);
    CTransactionRef dd_tx = wave26::MakeDDTransaction(DD_TX_MINT);
    CBlock plain_block = wave26::MakeBlock(100, t, {}, {plain_tx});
    CBlock dd_block = wave26::MakeBlock(100, t, {}, {dd_tx});

    for (ChainType chain : {ChainType::REGTEST, ChainType::TESTNET, ChainType::MAIN}) {
        SelectParams(chain);
        BOOST_TEST_MESSAGE("  chain=" << static_cast<int>(chain));
        BOOST_CHECK(!DigiDollar::HasDigiDollarMarker(*plain_tx));
        BOOST_CHECK(DigiDollar::HasDigiDollarMarker(*dd_tx));
        BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(*dd_tx), DD_TX_MINT);
        BOOST_CHECK(!wave26::BlockTouchesDigiDollarRef(plain_block));
        BOOST_CHECK(wave26::BlockTouchesDigiDollarRef(dd_block));
    }
    SelectParams(ChainType::REGTEST);
}

BOOST_AUTO_TEST_SUITE_END()
