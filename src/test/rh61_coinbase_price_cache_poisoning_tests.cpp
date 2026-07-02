// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-61: Miner coinbase oracle price stamping — unguarded
 *        `UpdatePriceCache` contaminates `GetLatestPrice()`
 *        (Wave-9 adversarial PoC — mining attacks, angle B + E fusion)
 *
 * =================================================================
 * Target site
 * =================================================================
 *
 *   src/validation.cpp:2803-2832  (ConnectBlock inner loop)
 *
 *     CAmount blockOraclePrice = 0;
 *     if (!fJustCheck && !block.vtx.empty()) {
 *         OracleBundleManager& oracleManager = OracleBundleManager::GetInstance();
 *         COracleBundle extractedBundle;
 *         if (oracleManager.ExtractOracleBundle(*block.vtx[0], extractedBundle) &&
 *             extractedBundle.median_price_micro_usd > 0) {
 *             blockOraclePrice = static_cast<CAmount>(extractedBundle.median_price_micro_usd);
 *             oracleManager.UpdatePriceCache(pindex->nHeight,
 *                                            extractedBundle.median_price_micro_usd);
 *             ...
 *
 *   src/oracle/bundle_manager.cpp:2188-2212  (UpdatePriceCache)
 *       height_to_price[height] = price_micro_usd;
 *       cached_price              = price_micro_usd;   // <-- global-consumer value
 *       last_update_time          = GetTime();         // <-- re-freshens staleness window
 *
 *   src/oracle/bundle_manager.cpp:1050-1250  (ExtractOracleBundle)
 *       Structural parsing ONLY. No signature/MuSig2 verification.
 *
 *   src/oracle/bundle_manager.cpp:2250-2255  (ValidateBlockOracleData)
 *       Fast-returns `true` on chains that are NOT TESTNET/REGTEST.
 *       Comment: "Oracle validation disabled on mainnet".
 *
 * =================================================================
 * Attack
 * =================================================================
 *
 *   A miner building a mainnet block crafts an OP_ORACLE payload with a
 *   fabricated `median_price_micro_usd`. The miner is NOT a configured
 *   oracle; the miner has NO MuSig2 aggregate signature; the miner is
 *   NOT required to match any network oracle consensus. All that is
 *   required is that `ExtractOracleBundle` (a structural parser) can
 *   deserialize the payload.
 *
 *   The miner publishes the block. Because the mainnet path of
 *   `ValidateBlockOracleData` short-circuits `return true`, no validator
 *   ever checks the signature or whether the price is within any
 *   tolerance of the oracle bundle broadcast on the P2P network.
 *
 *   `ConnectBlock` at :2811 extracts the bundle and unconditionally
 *   runs `UpdatePriceCache(height, attacker_price)`. Two side effects:
 *
 *     (a) `height_to_price[height] = attacker_price` — persisted.
 *     (b) `cached_price            = attacker_price` — reset.
 *     (c) `last_update_time        = now`           — staleness window
 *                                                      is re-armed, so
 *                                                      `GetLatestPrice()`
 *                                                      will NOT reject.
 *
 *   Any subsequent caller of `GetLatestPrice()` now reads attacker_price.
 *
 * =================================================================
 * Downstream consumers of the poisoned price
 * =================================================================
 *
 * Grep confirms at least 8 mainnet-live consumers of
 * `OracleBundleManager::GetInstance().GetLatestPrice()`:
 *
 *   src/consensus/err.cpp:405              ERR/DCA decisioning on mainnet
 *   src/wallet/digidollarwallet.cpp:1322   DD-value tagging at wallet tx creation
 *   src/wallet/digidollarwallet.cpp:4306   DD balance calculation
 *   src/wallet/digidollarwallet.cpp:4431   DD redemption UI / pricing
 *   src/qt/digidollarpositionswidget.cpp:1021  UI display of positions
 *   src/oracle/node.cpp:286,384            oracle node bookkeeping
 *   src/oracle/signing_orchestrator.cpp:477 (ComputeConsensusValues dep)
 *
 * In addition `GetOraclePriceForHeight(nHeight)` is exposed via the
 * RPC `debugoraclestate` (src/rpc/digidollar.cpp:3129) which reads
 * the same `height_to_price` map the attacker has just written.
 *
 * Concrete harm:
 *   - ERR/DCA decisioning uses a miner-chosen price every block. On
 *     mainnet, one attacker-mined block at a spoofed HIGH price masks a
 *     real distressed system — `IsInEmergencyRedemption()` returns false
 *     when it should return true, and users may mint more DD against
 *     DGB that is actually worth less.
 *   - A spoofed LOW price triggers ERR mode (src/consensus/err.cpp),
 *     re-pricing redemptions via DCA multipliers in favor of the
 *     attacker (who can co-time a redemption).
 *   - UI/wallet tagging shows consistently wrong fiat value to holders
 *     until the next honestly-mined block overwrites `cached_price`.
 *
 * =================================================================
 * Novelty vs. priors
 * =================================================================
 *
 *   - Prior C1 (DIGIDOLLAR_BUG_HUNT_REPORT) documents the same
 *     short-circuit in `ValidateBlockOracleData` but characterises
 *     the harm as "post-activation a miner can stamp any oracle price
 *     into coinbase — unbounded DD mint at any price". It does NOT
 *     call out the pre-activation poisoning, nor that the poisoning
 *     persists via `cached_price` for every non-mining `GetLatestPrice()`
 *     consumer.
 *
 *   - Prior suspicion W1-M-04 (wave-1 mapper) flags that
 *     `ConnectBlock:UpdatePriceCache` is "not BIP9-gated" but left it
 *     theoretical ("PoC needed"). This test is that PoC.
 *
 *   - This PoC uses a version-0x01 bundle (the most primitive encoding)
 *     so it reproduces pre-activation and on every chain type,
 *     regardless of whether MuSig2 or phase 2/3 is live. The attacker
 *     never needs to sign anything.
 *
 * =================================================================
 * Fix direction
 * =================================================================
 *
 *   - In `ConnectBlock` (validation.cpp:2807), gate the
 *     `UpdatePriceCache` call on BIP9 `DigiDollar::IsDigiDollarEnabled`
 *     AND require that `ValidateBlockOracleData` has actually validated
 *     the bundle. Do not cache prices extracted from unverified coinbases.
 *
 *   - In `OracleDataValidator::ValidateBlockOracleData`
 *     (bundle_manager.cpp:2253), remove the mainnet short-circuit or
 *     replace it with a strict structural-plus-signature check.
 *
 *   - In `OracleBundleManager::UpdatePriceCache`, either require a
 *     validation token or separate the per-height cache
 *     (`height_to_price`) from the global hot-cache (`cached_price`).
 *     `cached_price` should advance only after at least one signature
 *     check passes — independent of miner stamping.
 *
 * Severity: CRITICAL (peg / free-money / consensus of downstream
 *           ERR decisioning) on mainnet. HIGH pre-activation (no direct
 *           consensus harm, but UI/wallet/ERR contamination demonstrable
 *           the moment a miner includes an OP_ORACLE coinbase output).
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <consensus/volatility.h>
#include <crypto/sha256.h>
#include <digidollar/digidollar.h>
#include <digidollar/health.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <oracle/musig2_aggregator.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <primitives/oracle.h>
#include <pubkey.h>
#include <random.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <timedata.h>
#include <util/time.h>
#include <validation.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct OracleManagerReset
{
    OracleManagerReset() { OracleBundleManager::GetInstance().Clear(); }
    ~OracleManagerReset() { OracleBundleManager::GetInstance().Clear(); }
};

struct VolatilityReset
{
    VolatilityReset()
    {
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
        DigiDollar::Volatility::VolatilityMonitor::ClearHistory();
    }
    ~VolatilityReset()
    {
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
        DigiDollar::Volatility::VolatilityMonitor::ClearHistory();
    }
};

// Build a coinbase transaction whose SECOND output is a
// `OP_RETURN OP_ORACLE <0x01> <oracle_id(1) || price(8LE) || ts(8LE)>`
// payload. This is the Phase-1 compact format that
// `OracleBundleManager::ExtractOracleBundle` parses at
// `src/oracle/bundle_manager.cpp:1155-1204`.
//
// No signature, no MuSig2 aggregate, no chainparams pubkey ever consulted.
CMutableTransaction BuildMaliciousCoinbase(uint64_t attacker_price_micro_usd,
                                           int64_t ts,
                                           uint8_t oracle_id = 0)
{
    CMutableTransaction cb;

    // Coinbase input: prevout is null. Include a BIP34 height push so
    // the tx is structurally a coinbase.
    CTxIn in;
    in.prevout.SetNull();
    in.scriptSig = CScript() << static_cast<int64_t>(100)
                             << std::vector<unsigned char>{'m','i','n','e'};
    cb.vin.push_back(in);

    // vout[0]: normal payout (ignored by the oracle extractor)
    CScript payout;
    payout << OP_1 << std::vector<unsigned char>(32, 0x42);
    cb.vout.push_back(CTxOut(5000 * 100000000LL, payout));

    // vout[1]: the malicious oracle bundle.
    // Layout: OP_RETURN OP_ORACLE <0x01> <compact_data>
    // compact_data: oracle_id(1) + price(8 LE) + timestamp(8 LE) = 17 bytes
    std::vector<unsigned char> version_push = {0x01};

    std::vector<unsigned char> compact;
    compact.reserve(17);
    compact.push_back(oracle_id);
    for (int i = 0; i < 8; ++i) {
        compact.push_back(static_cast<unsigned char>((attacker_price_micro_usd >> (i * 8)) & 0xFF));
    }
    for (int i = 0; i < 8; ++i) {
        compact.push_back(static_cast<unsigned char>((static_cast<uint64_t>(ts) >> (i * 8)) & 0xFF));
    }

    CScript oracle_output;
    oracle_output << OP_RETURN << OP_ORACLE << version_push << compact;

    cb.vout.push_back(CTxOut(0, oracle_output));

    return cb;
}

CScript BuildCompactOracleScript(uint64_t attacker_price_micro_usd,
                                 int64_t ts,
                                 uint8_t oracle_id = 0)
{
    return BuildMaliciousCoinbase(attacker_price_micro_usd, ts, oracle_id)
        .vout[1].scriptPubKey;
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

    bundle.participation_bitmap = MuSig2OracleAggregator::EncodeBitmap(
        oracle_ids, static_cast<uint16_t>(Params().GetConsensus().nOracleTotalOracles));
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

CScript BuildMuSig2OracleScript(uint64_t price, int64_t timestamp, int32_t block_height)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    const int32_t epoch = GetCurrentEpoch(block_height);

    COracleBundle bundle(epoch);
    bundle.version = 3;
    bundle.median_price_micro_usd = price;
    bundle.timestamp = timestamp;

    std::vector<uint8_t> oracle_ids;
    for (uint8_t id = 0; id < consensus.nOracleConsensusRequired; ++id) {
        oracle_ids.push_back(id);
    }
    if (!SignRegtestV03Bundle(bundle, oracle_ids)) {
        return CScript();
    }

    return OracleBundleManager::GetInstance().CreateOracleScript(bundle);
}

CMutableTransaction BuildDigiDollarMint(const COutPoint& prevout,
                                        CAmount collateral_value,
                                        CAmount dd_amount,
                                        int next_height,
                                        int lock_days = 30)
{
    CKey owner_key;
    owner_key.MakeNewKey(true);
    XOnlyPubKey owner_xonly(owner_key.GetPubKey());

    const int64_t lock_blocks = DigiDollar::LockDaysToBlocks(lock_days);
    const int64_t lock_height = next_height + lock_blocks;

    DigiDollar::MintParams params;
    params.ddAmount = dd_amount;
    params.lockHeight = lock_height;
    params.ownerKey = owner_xonly;
    params.internalKey = DigiDollar::GetCollateralNUMSKey();
    params.oracleKeys = DigiDollar::GetOracleKeys(15);

    CMutableTransaction mint;
    mint.SetDigiDollarType(DD_TX_MINT);
    mint.vin.emplace_back(prevout);
    mint.vout.emplace_back(collateral_value, DigiDollar::CreateCollateralP2TR(params));
    mint.vout.emplace_back(0, DigiDollar::CreateDigiDollarP2TR(owner_xonly, dd_amount));

    CScript op_return = CScript() << OP_RETURN
                                  << std::vector<unsigned char>{'D', 'D'}
                                  << CScriptNum(1)
                                  << CScriptNum(dd_amount)
                                  << CScriptNum(lock_height)
                                  << CScriptNum(lock_days == 30 ? 1 : 0)
                                  << std::vector<unsigned char>(owner_xonly.begin(), owner_xonly.end());
    mint.vout.emplace_back(0, op_return);

    return mint;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(rh61_coinbase_price_cache_poisoning_tests, RegTestingSetup)

// RH-61-01: V1 must reject unsigned legacy oracle data at extraction.
BOOST_AUTO_TEST_CASE(rh61_01_extract_rejects_unsigned_legacy_price)
{
    OracleManagerReset reset;
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    const uint64_t ATTACKER_PRICE = 999999999ULL; // ~$999.99 micro-USD
    const int64_t  NOW            = GetTime();

    CMutableTransaction cb = BuildMaliciousCoinbase(ATTACKER_PRICE, NOW, /*oracle_id=*/0);
    CTransaction tx(cb);

    COracleBundle bundle;
    const bool ok = mgr.ExtractOracleBundle(tx, bundle);

    BOOST_CHECK_MESSAGE(!ok,
        "ExtractOracleBundle must reject miner-crafted legacy oracle data "
        "with no MuSig2 aggregate signature.");
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(bundle.median_price_micro_usd), 0ULL);
    BOOST_TEST_MESSAGE("RH-61-01: rejected unsigned legacy oracle price="
        << ATTACKER_PRICE);
}

// RH-61-02: The ConnectBlock extraction path must not cache unsigned
// legacy oracle data.
BOOST_AUTO_TEST_CASE(rh61_02_connectblock_path_rejects_legacy_cache_poison)
{
    OracleManagerReset reset;
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    // Establish a "honest" baseline price so we can observe the overwrite.
    const CAmount HONEST_PRICE    = 50000;      // $0.05 micro-USD
    const uint64_t ATTACKER_PRICE = 7777777ULL; // $7.777777 micro-USD
    const int HEIGHT              = 1234567;

    mgr.UpdatePriceCache(HEIGHT - 1, HONEST_PRICE);
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), HONEST_PRICE);

    // Attacker-mined block arrives with legacy oracle data. V1 must refuse
    // to extract it, so the ConnectBlock cache sink is never reached.
    CMutableTransaction cb = BuildMaliciousCoinbase(ATTACKER_PRICE, GetTime());
    CTransaction tx(cb);

    COracleBundle extracted;
    const bool extracted_ok = mgr.ExtractOracleBundle(tx, extracted);
    BOOST_CHECK_MESSAGE(!extracted_ok,
        "ConnectBlock must not extract unsigned legacy oracle data.");

    const CAmount latest_after  = mgr.GetLatestPrice();
    const uint64_t by_height    = mgr.GetOraclePriceForHeight(HEIGHT);

    BOOST_CHECK_MESSAGE(latest_after == HONEST_PRICE,
        "GetLatestPrice() must keep the prior honest price when legacy "
        "oracle data is rejected. honest=" << HONEST_PRICE
        << " rejected=" << ATTACKER_PRICE
        << " observed=" << latest_after);

    BOOST_CHECK_EQUAL(by_height, 0U);
    BOOST_TEST_MESSAGE("RH-61-02: rejected legacy oracle price "
        << ATTACKER_PRICE << "; GetLatestPrice remains " << latest_after);
}

// RH-61-03: Confirm the staleness window is rearmed. `GetLatestPrice`
// has a freshness guard at `bundle_manager.cpp:1391-1398`
// (ORACLE_MAX_AGE_SECONDS) — the attacker's `UpdatePriceCache` call
// also resets `last_update_time`, so the poison is NOT rejected by
// the staleness guard even if the rest of the oracle network is silent.
BOOST_AUTO_TEST_CASE(rh61_03_staleness_guard_reset_by_attacker)
{
    OracleManagerReset reset;
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    const uint64_t ATTACKER_PRICE = 12345678ULL;

    // Snapshot: the freshness guard is inside GetLatestPrice. The
    // attacker's write path calls `last_update_time = GetTime()`.
    // Therefore immediately after the write, GetLatestPrice returns
    // the value (not 0).
    mgr.UpdatePriceCache(9000, ATTACKER_PRICE);

    const CAmount observed = mgr.GetLatestPrice();
    BOOST_CHECK_MESSAGE(observed == static_cast<CAmount>(ATTACKER_PRICE),
        "Staleness guard in GetLatestPrice should not reject because "
        "the attacker's UpdatePriceCache call re-armed last_update_time.");
    BOOST_TEST_MESSAGE("RH-61-03: freshness window re-armed by attacker; GetLatestPrice="
        << observed);
}

// RH-61-04: Confirm the cache is also exposed via
// `GetOraclePriceForHeight` (RPC `debugoraclestate` path). Any RPC or
// downstream caller that reads per-height prices sees the attacker's
// data.
BOOST_AUTO_TEST_CASE(rh61_04_per_height_cache_exposed)
{
    OracleManagerReset reset;
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    const uint64_t ATTACKER_PRICE_A = 111111ULL;
    const uint64_t ATTACKER_PRICE_B = 222222ULL;

    // Attacker mines two consecutive blocks.
    mgr.UpdatePriceCache(1001, ATTACKER_PRICE_A);
    mgr.UpdatePriceCache(1002, ATTACKER_PRICE_B);

    BOOST_CHECK_EQUAL(mgr.GetOraclePriceForHeight(1001), ATTACKER_PRICE_A);
    BOOST_CHECK_EQUAL(mgr.GetOraclePriceForHeight(1002), ATTACKER_PRICE_B);

    // `cached_price` also follows the latest write.
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), static_cast<CAmount>(ATTACKER_PRICE_B));
    BOOST_TEST_MESSAGE("RH-61-04: per-height cache matches attacker writes: "
        "h=1001->" << mgr.GetOraclePriceForHeight(1001)
        << " h=1002->" << mgr.GetOraclePriceForHeight(1002));
}

// RH-61-05: Legacy oracle data must not drive ERR/DCA state by replacing
// the current MuSig2-backed price.
BOOST_AUTO_TEST_CASE(rh61_05_legacy_price_cannot_drive_err_toggle)
{
    OracleManagerReset reset;
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    const CAmount HEALTHY_PRICE   = 100000000LL; // $100 micro-USD
    const uint64_t CRUSH_PRICE    = 1ULL;        // $0.000001 micro-USD

    mgr.UpdatePriceCache(2000, HEALTHY_PRICE);
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), HEALTHY_PRICE);

    // Attacker-mined block at height 2001 with a legacy CRUSH price.
    CMutableTransaction cb = BuildMaliciousCoinbase(CRUSH_PRICE, GetTime());
    CTransaction tx(cb);

    COracleBundle extracted;
    BOOST_CHECK_MESSAGE(!mgr.ExtractOracleBundle(tx, extracted),
        "Legacy oracle data must not be extractable for cache updates.");

    const CAmount now = mgr.GetLatestPrice();
    BOOST_CHECK_EQUAL(now, HEALTHY_PRICE);

    BOOST_TEST_MESSAGE("RH-61-05: rejected legacy crush price "
        << CRUSH_PRICE << "; ERR/DCA-visible price remains " << now);
}

// RH-61-06: ExtractOracleBundle must scan every coinbase output for a
// valid MuSig2 bundle, not assume it is at vout[1].
BOOST_AUTO_TEST_CASE(rh61_06_vout_position_flexibility_docs_W1_H_01)
{
    OracleManagerReset reset;
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    const uint64_t ATTACKER_PRICE = 55555555ULL;

    // Build a coinbase where vout[1] is a 38-byte witness-commitment
    // lookalike (scriptPubKey = OP_RETURN 0x24 0xaa 0x21 0xa9 0xed <32B>)
    // and vout[2] is a valid MuSig2 oracle bundle. This mirrors the
    // post-SegWit ordering that `GenerateCoinbaseCommitment` produces
    // and matches the W1-H-01 flag condition.
    CMutableTransaction cb = BuildMaliciousCoinbase(ATTACKER_PRICE, GetTime());
    cb.vout[1].scriptPubKey = BuildMuSig2OracleScript(ATTACKER_PRICE, GetTime(), 100);
    BOOST_REQUIRE(!cb.vout[1].scriptPubKey.empty());

    // Insert a synthetic witness commitment at vout[1].
    CScript wc;
    wc.resize(38);
    wc[0] = OP_RETURN;
    wc[1] = 0x24;
    wc[2] = 0xaa; wc[3] = 0x21; wc[4] = 0xa9; wc[5] = 0xed;
    for (int i = 0; i < 32; ++i) wc[6 + i] = 0xCC;

    CTxOut wc_out(0, wc);
    cb.vout.insert(cb.vout.begin() + 1, wc_out);
    // Layout is now:
    //   vout[0] payout
    //   vout[1] witness commitment
    //   vout[2] MuSig2 oracle bundle

    CTransaction tx(cb);
    COracleBundle bundle;
    const bool ok = mgr.ExtractOracleBundle(tx, bundle);

    BOOST_CHECK_MESSAGE(ok,
        "ExtractOracleBundle must still find a valid MuSig2 bundle at "
        "vout[2] when vout[1] is the witness commitment.");
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(bundle.median_price_micro_usd),
                      ATTACKER_PRICE);

    BOOST_TEST_MESSAGE("RH-61-06: MuSig2 bundle at vout[2] accepted while "
        "vout[1] is witness commitment.");
}

// RH-61-07: The old mainnet validator short-circuit must not leave a
// cache path for legacy oracle data.
BOOST_AUTO_TEST_CASE(rh61_07_legacy_mainnet_shortcircuit_removed)
{
    OracleManagerReset reset;
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();
    const uint64_t ATTACKER_PRICE = 42424242ULL;

    CMutableTransaction cb = BuildMaliciousCoinbase(ATTACKER_PRICE, GetTime());
    CTransaction tx(cb);

    COracleBundle extracted;
    BOOST_CHECK_MESSAGE(!mgr.ExtractOracleBundle(tx, extracted),
        "Legacy oracle data must be rejected before any chain-specific "
        "validator path can cache it.");
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 0);

    BOOST_TEST_MESSAGE(
        "RH-61-07: legacy oracle data rejected before cache update.");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(rh68_test_block_validity_health_metrics_side_effect_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(test_block_validity_does_not_update_health_metrics)
{
    VolatilityReset volatility_reset;
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();
    mgr.Clear();
    mgr.SetEnabled(false);
    DigiDollar::SystemHealthMonitor::ResetMetrics();

    const Consensus::Params& consensus = Params().GetConsensus();
    const int32_t active_height = consensus.nDDActivationHeight + 10;
    while (m_node.chainman->ActiveChain().Height() < active_height) {
        mineBlocks(1);
    }
    BOOST_REQUIRE(DigiDollar::IsDigiDollarEnabled(m_node.chainman->ActiveChain().Tip(), *m_node.chainman));

    const CAmount oracle_price = 10000000; // $10.00 per DGB
    const CAmount dd_amount = 10000;       // $100.00 DD
    const CAmount fee = 1000;
    const int next_height = m_node.chainman->ActiveChain().Height() + 1;
    const int64_t lock_blocks = DigiDollar::LockDaysToBlocks(30);
    DigiDollar::ValidationContext dd_context(next_height, oracle_price, 300, Params());
    const CAmount collateral = DigiDollar::CalculateRequiredCollateral(dd_amount, lock_blocks, dd_context);
    BOOST_REQUIRE_GT(collateral, 0);

    const CPubKey coinbase_pubkey = coinbaseKey.GetPubKey();
    const CScript coinbase_script = CScript()
        << std::vector<unsigned char>(coinbase_pubkey.begin(), coinbase_pubkey.end())
        << OP_CHECKSIG;

    CMutableTransaction funding = CreateValidMempoolTransaction(
        m_coinbase_txns.front(), 0, 1, coinbaseKey,
        CScript() << OP_TRUE, collateral + fee, /*submit=*/false);
    CBlock funding_block = CreateAndProcessBlock({funding}, coinbase_script);
    BOOST_REQUIRE_GE(funding_block.vtx.size(), 2U);

    const COutPoint funding_out(funding_block.vtx[1]->GetHash(), 0);
    const int32_t candidate_height = m_node.chainman->ActiveChain().Height() + 1;
    DigiDollar::ValidationContext candidate_context(candidate_height, oracle_price, 300, Params());
    CMutableTransaction mint = BuildDigiDollarMint(funding_out, collateral, dd_amount, candidate_height);
    {
        const CTransaction mint_tx(mint);
        TxValidationState state;
        const bool mint_valid = DigiDollar::ValidateDigiDollarTransaction(mint_tx, candidate_context, state);
        BOOST_REQUIRE_MESSAGE(mint_valid, state.ToString());
    }

    const DigiDollar::SystemMetrics before = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_REQUIRE_EQUAL(before.totalDDSupply, 0);
    BOOST_REQUIRE_EQUAL(before.totalCollateral, 0);

    CBlock block = CreateBlock({mint}, coinbase_script, m_node.chainman->ActiveChainstate());

    CMutableTransaction coinbase(*block.vtx[0]);
    CScript oracle_script = BuildMuSig2OracleScript(oracle_price, block.nTime, candidate_height);
    BOOST_REQUIRE(!oracle_script.empty());
    coinbase.vout.push_back(CTxOut(0, oracle_script));
    block.vtx[0] = MakeTransactionRef(std::move(coinbase));

    COracleBundle extracted;
    BOOST_REQUIRE(mgr.ExtractOracleBundle(*block.vtx[0], extracted));
    BOOST_REQUIRE_EQUAL(extracted.median_price_micro_usd, static_cast<uint64_t>(oracle_price));

    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nNonce = 0;
    while (!CheckProofOfWork(GetPoWAlgoHash(block), block.nBits, m_node.chainman->GetConsensus())) {
        ++block.nNonce;
    }

    BlockValidationState state;
    {
        LOCK(cs_main);
        BOOST_REQUIRE_MESSAGE(TestBlockValidity(state,
                                                Params(),
                                                m_node.chainman->ActiveChainstate(),
                                                block,
                                                m_node.chainman->ActiveChain().Tip(),
                                                GetAdjustedTime,
                                                /*fCheckPOW=*/false,
                                                /*fCheckMerkleRoot=*/false),
                              state.ToString());
    }

    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), candidate_height - 1);
    const DigiDollar::SystemMetrics after = DigiDollar::SystemHealthMonitor::GetCachedMetrics();
    BOOST_CHECK_EQUAL(after.totalDDSupply, before.totalDDSupply);
    BOOST_CHECK_EQUAL(after.totalCollateral, before.totalCollateral);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(rh66_startup_oracle_price_loading_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(load_prices_from_chain_skips_recent_pre_activation_oracle_outputs)
{
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();
    mgr.Clear();
    mgr.SetEnabled(false);

    const Consensus::Params& consensus = Params().GetConsensus();
    const int32_t activation_height = consensus.nDDActivationHeight;
    BOOST_REQUIRE_EQUAL(activation_height, 650);

    const int32_t poisoned_height = activation_height - 5;
    const int32_t final_height = activation_height + 10;
    const uint64_t attacker_price = 42424242ULL;

    while (m_node.chainman->ActiveChain().Height() < poisoned_height - 1) {
        mineBlocks(1);
    }

    CScript coinbase_script = CScript() << OP_TRUE;
    CBlock block = CreateBlock({}, coinbase_script, m_node.chainman->ActiveChainstate());

    CMutableTransaction coinbase(*block.vtx[0]);
    coinbase.vout.push_back(CTxOut(0, BuildCompactOracleScript(attacker_price, GetTime())));
    block.vtx[0] = MakeTransactionRef(std::move(coinbase));
    COracleBundle inserted_bundle;
    BOOST_CHECK_MESSAGE(!mgr.ExtractOracleBundle(*block.vtx[0], inserted_bundle),
        "Pre-activation legacy oracle data must not be extracted in V1.");
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nNonce = 0;
    while (!CheckProofOfWork(GetPoWAlgoHash(block), block.nBits, m_node.chainman->GetConsensus())) {
        ++block.nNonce;
    }

    bool new_block = false;
    BOOST_REQUIRE(m_node.chainman->ProcessNewBlock(std::make_shared<const CBlock>(block),
                                                   /*force_processing=*/true,
                                                   /*min_pow_checked=*/true,
                                                   &new_block));
    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), poisoned_height - 1);

    while (m_node.chainman->ActiveChain().Height() < final_height) {
        mineBlocks(1);
    }
    BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Height(), final_height);
    BOOST_REQUIRE(DigiDollar::IsDigiDollarEnabled(m_node.chainman->ActiveChain().Tip(), *m_node.chainman));

    CBlock disk_block;
    CBlockIndex* poisoned_index = m_node.chainman->ActiveChain()[poisoned_height];
    BOOST_REQUIRE(poisoned_index != nullptr);
    BOOST_REQUIRE(m_node.chainman->m_blockman.ReadBlockFromDisk(disk_block, *poisoned_index));
    COracleBundle disk_bundle;
    BOOST_CHECK_MESSAGE(!mgr.ExtractOracleBundle(*disk_block.vtx[0], disk_bundle),
        "Legacy oracle data stored in old-shaped test blocks must remain "
        "unusable when prices are loaded from disk.");

    mgr.Clear();
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), 0);

    OracleBundleManager::LoadPricesFromChain(*m_node.chainman);

    BOOST_CHECK_EQUAL(mgr.GetOraclePriceForHeight(poisoned_height), 0U);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), 0);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(rh67_invalid_block_oracle_cache_side_effect_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(rejected_block_does_not_update_oracle_cache)
{
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();
    mgr.Clear();
    mgr.SetEnabled(false);

    const uint64_t baseline_price = 123456ULL;
    const uint64_t attacker_price = 654321ULL;
    mgr.UpdatePriceCache(100, baseline_price, GetTime());
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), static_cast<CAmount>(baseline_price));

    CScript coinbase_script = CScript() << OP_TRUE;
    CBlock block = CreateBlock({}, coinbase_script, m_node.chainman->ActiveChainstate());
    const int32_t rejected_height = m_node.chainman->ActiveChain().Height() + 1;

    CMutableTransaction coinbase(*block.vtx[0]);
    coinbase.vout.push_back(CTxOut(0, BuildCompactOracleScript(attacker_price, GetTime())));
    coinbase.vout[0].nValue = MAX_MONEY;
    block.vtx[0] = MakeTransactionRef(std::move(coinbase));

    COracleBundle extracted;
    BOOST_CHECK_MESSAGE(!mgr.ExtractOracleBundle(*block.vtx[0], extracted),
        "Rejected legacy oracle data must not be extractable before block "
        "processing.");

    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nNonce = 0;
    while (!CheckProofOfWork(GetPoWAlgoHash(block), block.nBits, m_node.chainman->GetConsensus())) {
        ++block.nNonce;
    }

    bool new_block = false;
    (void)m_node.chainman->ProcessNewBlock(std::make_shared<const CBlock>(block),
                                           /*force_processing=*/true,
                                           /*min_pow_checked=*/true,
                                           &new_block);
    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Height(), rejected_height - 1);
    BOOST_CHECK_EQUAL(mgr.GetOraclePriceForHeight(rejected_height), 0U);
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), static_cast<CAmount>(baseline_price));
}

BOOST_AUTO_TEST_SUITE_END()
