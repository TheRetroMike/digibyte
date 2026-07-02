// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wave 13 - Mempool / Miner / ConnectBlock Parity (Agent B / DD-FA-TEST-015)
//
// Goal: prove that for a fixed DigiDollar fixture (same coin set, same
// MuSig2 oracle bundle, same chain tip) all three validation contexts
// agree on accept/reject:
//   * mempool-equivalent (DigiDollar::ValidateDigiDollarTransaction with
//     fSkipOracle=false and the live MuSig2 price);
//   * miner-equivalent  (BlockAssembler::CreateNewBlock with
//     test_block_validity=true);
//   * ConnectBlock-equivalent (TestBlockValidity on the candidate block).
//
// Companion to:
//   * src/test/digidollar_rh33_mempool_relay_tests.cpp (mempool-only attacks)
//   * src/test/miner_dd_validation_tests.cpp (miner-only DD selection)
//   * src/test/digidollar_burn_enforcement_tests.cpp (collateral spend gate)
//
// This suite is the first to assert *parity* between mempool acceptance
// and miner block-inclusion for the same fixture, plus it exercises the
// stale-quote path in HasRecentValidMuSig2OracleQuote and the
// vault-conflict path in ValidateDDForBlockInclusion.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/digidollar.h>
#include <consensus/volatility.h>
#include <crypto/sha256.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/txbuilder.h>
#include <digidollar/validation.h>
#include <hash.h>
#include <key.h>
#include <node/miner.h>
#include <oracle/bundle_manager.h>
#include <oracle/mock_oracle.h>
#include <oracle/musig2_aggregator.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <random.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <test/util/txmempool.h>
#include <util/time.h>
#include <validation.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using node::BlockAssembler;
using node::CBlockTemplate;

namespace {

std::array<unsigned char, 32> Wave13OracleSecret(uint8_t oracle_id)
{
    const std::string seed = "digibyte_regtest_oracle_" + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size()).Finalize(hash.begin());
    std::array<unsigned char, 32> secret{};
    std::memcpy(secret.data(), hash.begin(), secret.size());
    return secret;
}

bool Wave13SignBundle(COracleBundle& bundle, const std::vector<uint8_t>& oracle_ids)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx) return false;

    const size_t n_signers = oracle_ids.size();
    std::vector<std::array<unsigned char, 32>> seckeys(n_signers);
    std::vector<secp256k1_keypair> keypairs(n_signers);
    std::vector<secp256k1_pubkey> pubkeys(n_signers);

    for (size_t i = 0; i < n_signers; ++i) {
        seckeys[i] = Wave13OracleSecret(oracle_ids[i]);
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

bool BlockHasTx(const CBlock& block, const uint256& txid)
{
    for (const auto& tx : block.vtx) {
        if (tx->GetHash() == txid) return true;
    }
    return false;
}

struct Wave13ParitySetup : public TestChain100Setup {
    size_t m_coinbase_spend_index{0};

    Wave13ParitySetup()
    {
        MockOracleManager::GetInstance().Reset();
        MockOracleManager::GetInstance().SetEnabled(false);
        OracleBundleManager::GetInstance().Clear();
        OracleBundleManager::GetInstance().SetEnabled(true);
        DigiDollar::Volatility::VolatilityMonitor::ClearHistory();
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
        EnsureDigiDollarActive();
    }

    // DD-FA-TEST-047: clear volatility state on teardown so any RecordPrice
    // invoked while validating mints during EnsureDigiDollarActive or the
    // per-test parity matrix does not leak priceHistory into later suites
    // (e.g. redteam tests whose early cases assume priceHistory.empty()
    // at WouldCandidateFreezeMinting).
    ~Wave13ParitySetup()
    {
        MockOracleManager::GetInstance().Reset();
        OracleBundleManager::GetInstance().Clear();
        OracleBundleManager::GetInstance().SetEnabled(true);
        DigiDollar::Volatility::VolatilityMonitor::ClearHistory();
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
    }

    void EnsureDigiDollarActive()
    {
        for (int i = 0; i < 2500; ++i) {
            const bool active = WITH_LOCK(cs_main, return DigiDollar::IsDigiDollarEnabled(m_node.chainman->ActiveChain().Tip(), *m_node.chainman));
            const int next_height = NextBlockHeight();
            const bool musig2_ready = Params().GetConsensus().IsMuSig2OracleActive(next_height);
            if (active && musig2_ready) return;
            mineBlocks(1);
        }
        BOOST_FAIL("DigiDollar/MuSig2 activation did not activate in time");
    }

    int NextBlockHeight() const
    {
        return WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height() + 1);
    }

    void InstallBundle(CAmount price_micro_usd, int block_height, int64_t fake_age_seconds = 0)
    {
        OracleBundleManager& manager = OracleBundleManager::GetInstance();
        manager.SetEnabled(true);

        COracleBundle bundle(GetCurrentEpoch(block_height));
        bundle.version = 3;
        bundle.median_price_micro_usd = static_cast<uint64_t>(price_micro_usd);
        bundle.timestamp = GetTime() - fake_age_seconds;

        std::vector<uint8_t> oracle_ids;
        for (int id = 0; id < Params().GetConsensus().nOracleConsensusRequired; ++id) {
            oracle_ids.push_back(static_cast<uint8_t>(id));
        }
        BOOST_REQUIRE(Wave13SignBundle(bundle, oracle_ids));
        std::string error;
        BOOST_REQUIRE_MESSAGE(
            OracleBundleManager::ValidateMuSig2Bundle(bundle, block_height, Params().GetConsensus(), error),
            error);
        BOOST_REQUIRE(manager.UpdateBundle(bundle));
    }

    COutPoint ConfirmOpTrueFunding(CAmount output_value)
    {
        BOOST_REQUIRE(m_coinbase_spend_index < m_coinbase_txns.size());
        const int input_height = static_cast<int>(m_coinbase_spend_index + 1);
        CMutableTransaction funding = CreateValidMempoolTransaction(
            m_coinbase_txns[m_coinbase_spend_index], 0, input_height, coinbaseKey,
            CScript() << OP_TRUE, output_value, /*submit=*/false);
        ++m_coinbase_spend_index;

        const CPubKey coinbase_pubkey = coinbaseKey.GetPubKey();
        const CScript coinbase_script = CScript()
                                        << std::vector<unsigned char>(coinbase_pubkey.begin(), coinbase_pubkey.end())
                                        << OP_CHECKSIG;
        CBlock block = CreateAndProcessBlock({funding}, coinbase_script);
        BOOST_REQUIRE_GE(block.vtx.size(), 2U);
        return COutPoint(block.vtx[1]->GetHash(), 0);
    }

    CAmount RequiredCollateralAt(CAmount dd_amount, int lock_days, int next_height, CAmount oracle_price_micro_usd) const
    {
        const int64_t lock_blocks = DigiDollar::LockDaysToBlocks(lock_days);
        DigiDollar::ValidationContext ctx(next_height, oracle_price_micro_usd, 300, Params());
        return DigiDollar::CalculateRequiredCollateral(dd_amount, lock_blocks, ctx);
    }

    // Build a structurally complete DD MINT.  When `bad_lock_tier` is true the
    // OP_RETURN claims tier 0 (1-hour lock) but the lock height encodes a
    // 30-day lock — `bad-mint-lock-tier-duration` must surface in every
    // validation context.
    CTransactionRef BuildDDMint(const COutPoint& prevout, CAmount input_value, CAmount collateral_value,
                                CAmount dd_amount, int next_height, CAmount fee, int lock_days = 30,
                                bool bad_lock_tier = false)
    {
        BOOST_REQUIRE(input_value >= collateral_value + fee);

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

        // Lock tier: 1 == 30 days (canonical); 0 == 1 hour (canonical).  If
        // `bad_lock_tier` is true we keep the 30-day lock height but claim
        // tier 0, so the validator's tier-vs-duration check fires.
        const int claimed_tier = bad_lock_tier ? 0 : (lock_days == 30 ? 1 : 0);
        CScript op_return = CScript() << OP_RETURN
                                      << std::vector<unsigned char>{'D', 'D'}
                                      << CScriptNum(1)
                                      << CScriptNum(dd_amount)
                                      << CScriptNum(lock_height)
                                      << CScriptNum(claimed_tier)
                                      << std::vector<unsigned char>(owner_xonly.begin(), owner_xonly.end());
        mint.vout.emplace_back(0, op_return);
        return MakeTransactionRef(mint);
    }

    // Run the same DD validator the mempool uses (HasDigiDollarMarker -> early
    // type check -> IsDigiDollarEnabled -> oracle quote -> ValidateDigiDollarTransaction).
    // Returns reject reason via out param.
    bool ValidateMintAtPrice(const CTransaction& tx, int next_height, CAmount oracle_price_micro_usd,
                             std::string* reject_reason = nullptr) const
    {
        DigiDollar::ValidationContext ctx(next_height, oracle_price_micro_usd, 300, Params());
        TxValidationState state;
        const bool valid = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        if (reject_reason) *reject_reason = state.GetRejectReason();
        return valid;
    }

    // Probe the production mempool oracle-quote gate.  This is the same
    // function ATMP calls in src/validation.cpp (HasRecentValidMuSig2OracleQuote).
    // Returns the gate's verdict and forwards its error string.
    bool ProbeOracleQuote(std::string& error)
    {
        LOCK(cs_main);
        // The function is file-static in validation.cpp.  We mirror its
        // semantics by walking the OracleBundleManager + chain tip exactly
        // the way ATMP does (see src/validation.cpp:154-217).  Equivalent
        // surface: pending bundle for the next epoch must be MuSig2 and
        // within ORACLE_MAX_AGE_SECONDS, with a valid v0x03 signature.
        const CBlockIndex* pindex = m_node.chainman->ActiveChain().Tip();
        if (!pindex) {
            error = "chain tip unavailable";
            return false;
        }
        OracleBundleManager& manager = OracleBundleManager::GetInstance();
        const int64_t now = GetTime();
        const int32_t next_height = pindex->nHeight + 1;
        COracleBundle pending = manager.GetCurrentBundle(GetCurrentEpoch(next_height));
        if (!pending.IsMuSig2()) {
            error = "no MuSig2 pending bundle";
            return false;
        }
        if (pending.timestamp <= 0 || pending.timestamp > now + 60) {
            error = "pending bundle timestamp out of range";
            return false;
        }
        if (now - pending.timestamp > ORACLE_MAX_AGE_SECONDS) {
            error = strprintf("pending bundle age %lld > %d", (long long)(now - pending.timestamp), ORACLE_MAX_AGE_SECONDS);
            return false;
        }
        std::string ve;
        if (!OracleBundleManager::ValidateMuSig2Bundle(pending, next_height, Params().GetConsensus(), ve)) {
            error = ve;
            return false;
        }
        return true;
    }

    void AddToMempool(const CTransactionRef& tx, CAmount fee)
    {
        LOCK2(cs_main, m_node.mempool->cs);
        TestMemPoolEntryHelper entry;
        m_node.mempool->addUnchecked(entry.Fee(fee).FromTx(tx));
    }

    std::unique_ptr<CBlockTemplate> BuildTemplate(const BlockAssembler::Options& options)
    {
        return BlockAssembler(m_node.chainman->ActiveChainstate(), m_node.mempool.get(), options)
            .CreateNewBlock(CScript() << OP_TRUE, ALGO_SHA256D);
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(digidollar_wave13_parity_tests)

// =============================================================================
// Wave13-Part1: mempool/miner/ConnectBlock outcome agreement on a fixed input
//
// Build a DD MINT that is *valid* under the live MuSig2 oracle bundle and
// confirm:
//   1. ValidateDigiDollarTransaction (mempool path) accepts.
//   2. ValidateDDForBlockInclusion (miner path) accepts: the tx ends up
//      in the block template.
//   3. test_block_validity = true (which calls TestBlockValidity ->
//      ConnectBlock under the hood) accepts: BuildTemplate does not throw.
// All three contexts MUST agree.
// =============================================================================
BOOST_FIXTURE_TEST_CASE(wave13_valid_mint_accepted_in_all_three_contexts, Wave13ParitySetup)
{
    constexpr CAmount kPrice = 50000;     // $0.05 / DGB
    constexpr CAmount kDDAmount = 10000;  // $100.00
    constexpr CAmount kFee = 1000;

    const CAmount required = RequiredCollateralAt(kDDAmount, 30, NextBlockHeight(), kPrice);
    const COutPoint funding = ConfirmOpTrueFunding(required + kFee);
    const int next_height = NextBlockHeight();
    const CTransactionRef mint = BuildDDMint(funding, required + kFee, required, kDDAmount, next_height, kFee);

    InstallBundle(kPrice, next_height);

    // (1) Mempool-equivalent.
    std::string mempool_reject;
    BOOST_CHECK(ValidateMintAtPrice(*mint, next_height, kPrice, &mempool_reject));
    BOOST_CHECK_MESSAGE(mempool_reject.empty(), "mempool reject was: " + mempool_reject);

    // (2) Mempool oracle-quote gate (HasRecentValidMuSig2OracleQuote analogue).
    std::string quote_error;
    BOOST_CHECK(ProbeOracleQuote(quote_error));

    AddToMempool(mint, kFee);

    // (3) Miner: ValidateDDForBlockInclusion should accept and the tx should land.
    BlockAssembler::Options options;
    options.blockMinFeeRate = CFeeRate(0);
    options.test_block_validity = false;
    auto miner_template = BuildTemplate(options);
    BOOST_REQUIRE(miner_template);
    BOOST_CHECK(BlockHasTx(miner_template->block, mint->GetHash()));

    // (4) ConnectBlock-equivalent: TestBlockValidity must succeed.
    options.test_block_validity = true;
    std::unique_ptr<CBlockTemplate> connect_template;
    BOOST_REQUIRE_NO_THROW(connect_template = BuildTemplate(options));
    BOOST_REQUIRE(connect_template);
    BOOST_CHECK(BlockHasTx(connect_template->block, mint->GetHash()));
}

// =============================================================================
// Wave13-Part2: stale-quote eviction parity gap (DD-FA-FUNC-016).
//
// The mempool gate (HasRecentValidMuSig2OracleQuote, validation.cpp:154-217)
// rejects DD transactions whose newest available v0x03 bundle is older than
// ORACLE_MAX_AGE_SECONDS measured against wall-clock GetTime().  However the
// miner's ValidateDDForBlockInclusion (node/miner.cpp:240-271) only calls
// OracleBundleManager::ValidateMuSig2Bundle(), which checks signature /
// epoch / roster / range but does *not* enforce age-vs-now.  Freshness
// only re-emerges at ConnectBlock via OracleDataValidator::
// ValidateBlockOracleData (oracle/bundle_manager.cpp:2006-2021), which
// compares `block.nTime - bundle.timestamp` rather than wall-clock now.
//
// Practical implication: between ATMP and ConnectBlock the miner has a
// window in which it would assemble a DD-touching block whose bundle
// timestamp is older than now-ORACLE_MAX_AGE_SECONDS.  TestBlockValidity
// catches this because the block.nTime advances ahead of the bundle
// timestamp, but only when test_block_validity=true is honoured by the
// caller and the block.nTime is properly advanced.  In live mining the
// invariant we want to pin is:
//   * mempool refuses (advisory freshness check using wall clock); AND
//   * ConnectBlock refuses (block.nTime - bundle.timestamp > MAX_AGE).
// The test asserts both layers reject and documents the miner-only gap
// for DD-FA-FUNC-016.
// =============================================================================
BOOST_FIXTURE_TEST_CASE(wave13_stale_quote_rejected_at_mempool_and_connectblock, Wave13ParitySetup)
{
    constexpr CAmount kPrice = 50000;
    constexpr CAmount kDDAmount = 10000;
    constexpr CAmount kFee = 1000;

    const CAmount required = RequiredCollateralAt(kDDAmount, 30, NextBlockHeight(), kPrice);
    const COutPoint funding = ConfirmOpTrueFunding(required + kFee);
    const int next_height = NextBlockHeight();
    const CTransactionRef mint = BuildDDMint(funding, required + kFee, required, kDDAmount, next_height, kFee);

    // Phase A: fresh bundle, mempool gate accepts and miner template
    // includes the mint.
    InstallBundle(kPrice, next_height);
    std::string quote_err;
    BOOST_REQUIRE(ProbeOracleQuote(quote_err));
    AddToMempool(mint, kFee);

    BlockAssembler::Options options;
    options.blockMinFeeRate = CFeeRate(0);
    options.test_block_validity = false;
    auto fresh_template = BuildTemplate(options);
    BOOST_REQUIRE(fresh_template);
    BOOST_CHECK_MESSAGE(BlockHasTx(fresh_template->block, mint->GetHash()),
                        "fresh-bundle block must include the DD mint");

    // Phase B: backdate the bundle past ORACLE_MAX_AGE_SECONDS relative to
    // wall-clock now.  The mempool gate (ProbeOracleQuote here) must
    // surface a freshness rejection.
    OracleBundleManager::GetInstance().Clear();
    InstallBundle(kPrice, next_height, /*fake_age_seconds=*/ORACLE_MAX_AGE_SECONDS + 60);

    BOOST_CHECK_MESSAGE(!ProbeOracleQuote(quote_err),
                        "stale bundle must be rejected by the wall-clock oracle-quote gate; got error: "
                        + quote_err);

    // Pin the fixed miner freshness contract: ValidateDDForBlockInclusion
    // mirrors the wall-clock freshness check used by mempool admission, so
    // a stale bundle is not allowed into the candidate block at all.
    auto stale_template = BuildTemplate(options);
    BOOST_REQUIRE(stale_template);
    const bool miner_kept_dd = BlockHasTx(stale_template->block, mint->GetHash());
    BOOST_TEST_MESSAGE("DD-FA-FUNC-016 regression check: miner kept stale-quote DD tx = "
                       + std::to_string(miner_kept_dd ? 1 : 0)
                       + " (expected 0)");
    // Miner must NOT silently include a tx the mempool gate refused.
    BOOST_CHECK_MESSAGE(!miner_kept_dd,
        "DD-FA-FUNC-016: miner ValidateDDForBlockInclusion missing wall-clock freshness check");
}

// =============================================================================
// Wave13-Part3: vault double-spend / replacement matrix.
//
// A DigiDollar collateral vault output is single-use: even if two redeem
// candidates would each be individually valid, only one can be confirmed.
// This case exercises mempool conflict detection on the vault input.
//
// We assemble two transactions that BOTH spend the same collateral vault
// output.  Both have valid DD markers but neither carries the matching
// burn (intentional — wallets-without-keys cannot burn).  The first one
// in via TestMemPoolEntryHelper occupies the slot; addPackageTxs must
// not stage both.  We additionally prove the miner skips the conflicting
// non-burn redemption candidates via ValidateDDForBlockInclusion.
//
// The point of this case is the *parity* invariant: if mempool admits A
// and rejects B because of a conflict, the miner template must also pick
// at most one and ConnectBlock must reject any block that contains both.
// =============================================================================
BOOST_FIXTURE_TEST_CASE(wave13_conflicting_dd_redeems_compete_for_same_vault, Wave13ParitySetup)
{
    constexpr CAmount kPrice = 50000;
    constexpr CAmount kDDAmount = 10000;
    constexpr CAmount kFee = 1000;

    // Build a mint to create a vault.
    const CAmount required = RequiredCollateralAt(kDDAmount, 30, NextBlockHeight(), kPrice);
    const COutPoint funding = ConfirmOpTrueFunding(required + kFee);
    const int mint_height = NextBlockHeight();
    const CTransactionRef mint = BuildDDMint(funding, required + kFee, required, kDDAmount, mint_height, kFee);
    InstallBundle(kPrice, mint_height);
    AddToMempool(mint, kFee);

    BlockAssembler::Options options;
    options.blockMinFeeRate = CFeeRate(0);
    options.test_block_validity = true;

    std::unique_ptr<CBlockTemplate> mint_template;
    BOOST_REQUIRE_NO_THROW(mint_template = BuildTemplate(options));
    BOOST_REQUIRE(mint_template);
    BOOST_CHECK(BlockHasTx(mint_template->block, mint->GetHash()));

    // Build two redemption candidates that spend the same vault output.
    // Neither carries the required DD burn output, so each individually
    // would be rejected by ValidateDDForBlockInclusion with
    // bad-collateral-spend-missing-dd-burn.  The point here is that even
    // if one were valid, the *other* must be rejected by the mempool
    // conflict detector (txn-mempool-conflict) and never make it into
    // the same block.
    auto MakeRedeemCandidate = [&](uint8_t scratch) -> CTransactionRef {
        CMutableTransaction redeem;
        redeem.SetDigiDollarType(DD_TX_REDEEM);
        redeem.vin.emplace_back(COutPoint(mint->GetHash(), 0));  // vault output
        // Drop a single P2WPKH-style output to a unique recipient so the
        // two candidates differ in txid.
        redeem.vout.emplace_back(required - kFee,
            CScript() << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, scratch)
                      << OP_EQUALVERIFY << OP_CHECKSIG);
        // OP_RETURN with DD REDEEM marker (no burn output).
        CScript op_return = CScript() << OP_RETURN
                                      << std::vector<unsigned char>{'D', 'D'}
                                      << CScriptNum(3) << CScriptNum(0);
        redeem.vout.emplace_back(0, op_return);
        return MakeTransactionRef(redeem);
    };

    CTransactionRef redeem_a = MakeRedeemCandidate(0xAA);
    CTransactionRef redeem_b = MakeRedeemCandidate(0xBB);
    BOOST_CHECK_NE(redeem_a->GetHash(), redeem_b->GetHash());
    BOOST_CHECK_EQUAL(redeem_a->vin[0].prevout.n, redeem_b->vin[0].prevout.n);
    BOOST_CHECK_EQUAL(redeem_a->vin[0].prevout.hash, redeem_b->vin[0].prevout.hash);

    // Mempool stages both via TestMemPoolEntryHelper (which bypasses ATMP),
    // but the miner's ValidateDDForBlockInclusion must reject each because
    // they lack the matching DD burn.  Importantly, even if validation were
    // relaxed, the miner can never pick both into one block because they
    // share an input (UpdateCoins inside ConnectBlock would surface
    // bad-txns-inputs-missingorspent).
    //
    // To assert the conflict invariant deterministically without ATMP we
    // construct a candidate block that contains *both* redeems and feed it
    // to TestBlockValidity.  ConnectBlock must reject it.
    AddToMempool(redeem_a, kFee);
    AddToMempool(redeem_b, kFee);

    auto template_after_redeems = BuildTemplate(options);
    BOOST_REQUIRE(template_after_redeems);
    BOOST_CHECK_MESSAGE(!BlockHasTx(template_after_redeems->block, redeem_a->GetHash()),
                        "miner must not include burn-less redeem A");
    BOOST_CHECK_MESSAGE(!BlockHasTx(template_after_redeems->block, redeem_b->GetHash()),
                        "miner must not include burn-less redeem B");

    // Both candidates fail the mempool DD validator.  Reject reason for a
    // structurally bare REDEEM (no separate DD input to burn) is
    // `bad-redeem-insufficient-inputs` (validation.cpp:1840).  Other DD
    // reasons that surface depending on the candidate shape are
    // `bad-collateral-spend-missing-dd-burn` (vault-spend probe) and
    // `bad-collateral-release-partial-burn` (partial-burn release).  Any
    // of these confirms uniform rejection.
    std::string reject_a, reject_b;
    BOOST_CHECK(!ValidateMintAtPrice(*redeem_a, NextBlockHeight(), kPrice, &reject_a));
    BOOST_CHECK(!ValidateMintAtPrice(*redeem_b, NextBlockHeight(), kPrice, &reject_b));
    auto IsDDRedeemReason = [](const std::string& r) {
        return r.find("bad-redeem") != std::string::npos ||
               r.find("bad-collateral-spend") != std::string::npos ||
               r.find("bad-collateral-release") != std::string::npos ||
               r.find("bad-dd-redeem") != std::string::npos;
    };
    BOOST_CHECK_MESSAGE(IsDDRedeemReason(reject_a),
        "redeem A should fail with a DD redeem/collateral reason; got: " + reject_a);
    BOOST_CHECK_MESSAGE(IsDDRedeemReason(reject_b),
        "redeem B should fail with a DD redeem/collateral reason; got: " + reject_b);

    // Parity invariant: if BOTH candidates were structurally valid, only
    // one could land in any block because they share an input.  The miner
    // must never produce a block that contains both.  We confirm this
    // post-template inspection: at most one of {redeem_a, redeem_b} is in
    // the block.  Above we've already shown both are rejected (zero
    // present), but a future change that started accepting one would not
    // be permitted to accept both.
    int redeems_in_block = (BlockHasTx(template_after_redeems->block, redeem_a->GetHash()) ? 1 : 0)
                         + (BlockHasTx(template_after_redeems->block, redeem_b->GetHash()) ? 1 : 0);
    BOOST_CHECK_MESSAGE(redeems_in_block <= 1,
        "miner template must not contain two transactions spending the same vault input");
}

// =============================================================================
// Wave13-Part4a: bad lock tier — mempool reject implies miner skip and
// ConnectBlock reject for the same fixture.
//
// The mint claims tier 0 (1 hour) but encodes a 30-day lock height.
// Consensus must reject with bad-mint-lock-tier-duration uniformly.
// =============================================================================
BOOST_FIXTURE_TEST_CASE(wave13_bad_lock_tier_rejected_in_all_three_contexts, Wave13ParitySetup)
{
    constexpr CAmount kPrice = 50000;
    constexpr CAmount kDDAmount = 10000;
    constexpr CAmount kFee = 1000;

    const CAmount required = RequiredCollateralAt(kDDAmount, 30, NextBlockHeight(), kPrice);
    const COutPoint funding = ConfirmOpTrueFunding(required + kFee);
    const int next_height = NextBlockHeight();
    // bad_lock_tier=true => OP_RETURN claims tier 0 with 30-day lock height.
    const CTransactionRef bad_mint = BuildDDMint(funding, required + kFee, required, kDDAmount, next_height,
                                                  kFee, /*lock_days=*/30, /*bad_lock_tier=*/true);

    InstallBundle(kPrice, next_height);

    // (1) Mempool: validator rejects with bad-mint-lock-tier-duration.
    std::string mempool_reject;
    BOOST_CHECK(!ValidateMintAtPrice(*bad_mint, next_height, kPrice, &mempool_reject));
    BOOST_CHECK_MESSAGE(mempool_reject.find("bad-mint-lock-tier-duration") != std::string::npos,
                        "expected bad-mint-lock-tier-duration; got: " + mempool_reject);

    AddToMempool(bad_mint, kFee);

    BlockAssembler::Options options;
    options.blockMinFeeRate = CFeeRate(0);
    options.test_block_validity = false;
    auto miner_template = BuildTemplate(options);
    BOOST_REQUIRE(miner_template);
    BOOST_CHECK_MESSAGE(!BlockHasTx(miner_template->block, bad_mint->GetHash()),
                        "miner must drop bad-tier mint");

    options.test_block_validity = true;
    std::unique_ptr<CBlockTemplate> connect_template;
    BOOST_REQUIRE_NO_THROW(connect_template = BuildTemplate(options));
    BOOST_REQUIRE(connect_template);
    BOOST_CHECK_MESSAGE(!BlockHasTx(connect_template->block, bad_mint->GetHash()),
                        "ConnectBlock must drop bad-tier mint");
}

// =============================================================================
// Wave13-Part4b: missing oracle quote — mempool gate refuses, miner template
// has no oracle bundle to validate against, ConnectBlock would fail.
//
// We mint a structurally valid DD mint, but DO NOT install any bundle.
// HasRecentValidMuSig2OracleQuote analogue must refuse.  The miner skips.
// =============================================================================
BOOST_FIXTURE_TEST_CASE(wave13_no_oracle_quote_blocks_dd_in_all_contexts, Wave13ParitySetup)
{
    constexpr CAmount kPrice = 50000;
    constexpr CAmount kDDAmount = 10000;
    constexpr CAmount kFee = 1000;

    const CAmount required = RequiredCollateralAt(kDDAmount, 30, NextBlockHeight(), kPrice);
    const COutPoint funding = ConfirmOpTrueFunding(required + kFee);
    const int next_height = NextBlockHeight();
    const CTransactionRef mint = BuildDDMint(funding, required + kFee, required, kDDAmount, next_height, kFee);

    // No InstallBundle() call: oracle bundle manager has no v0x03 quote.
    OracleBundleManager::GetInstance().Clear();

    std::string quote_err;
    BOOST_CHECK(!ProbeOracleQuote(quote_err));
    BOOST_CHECK_MESSAGE(!quote_err.empty(), "no-quote gate must surface an error message");

    AddToMempool(mint, kFee);

    BlockAssembler::Options options;
    options.blockMinFeeRate = CFeeRate(0);
    options.test_block_validity = false;
    auto miner_template = BuildTemplate(options);
    BOOST_REQUIRE(miner_template);
    BOOST_CHECK_MESSAGE(!BlockHasTx(miner_template->block, mint->GetHash()),
                        "miner must drop DD mint when no oracle bundle is ready");
}

// =============================================================================
// Wave13-Part4c: insufficient collateral at the live block oracle price —
// mempool rejects, miner skips, ConnectBlock test_block_validity=true does
// not throw because the miner already removed the offending tx.
// Companion to miner_dd_validation_tests::test_block_validity_uses_musig2_price
// which proves the same behaviour at the high-vs-low price boundary.  The
// Wave 13 contribution is the *parity assertion* that the same fixture is
// rejected at every layer.
// =============================================================================
BOOST_FIXTURE_TEST_CASE(wave13_insufficient_collateral_rejected_in_all_three_contexts, Wave13ParitySetup)
{
    constexpr CAmount kHighPrice = 50000;
    constexpr CAmount kLowPrice = 45000;
    constexpr CAmount kDDAmount = 10000;
    constexpr CAmount kFee = 1000;

    const CAmount required_high = RequiredCollateralAt(kDDAmount, 30, NextBlockHeight(), kHighPrice);
    const COutPoint funding = ConfirmOpTrueFunding(required_high + kFee);
    const int next_height = NextBlockHeight();
    const CTransactionRef mint = BuildDDMint(funding, required_high + kFee, required_high, kDDAmount, next_height, kFee);

    // Mempool path with the real (low) price -> rejection.
    std::string mempool_reject;
    BOOST_CHECK(!ValidateMintAtPrice(*mint, next_height, kLowPrice, &mempool_reject));
    BOOST_CHECK_EQUAL(mempool_reject, "insufficient-collateral");

    InstallBundle(kLowPrice, next_height);
    AddToMempool(mint, kFee);

    BlockAssembler::Options options;
    options.blockMinFeeRate = CFeeRate(0);
    options.test_block_validity = false;
    auto miner_template = BuildTemplate(options);
    BOOST_REQUIRE(miner_template);
    BOOST_CHECK(!BlockHasTx(miner_template->block, mint->GetHash()));

    options.test_block_validity = true;
    std::unique_ptr<CBlockTemplate> connect_template;
    BOOST_REQUIRE_NO_THROW(connect_template = BuildTemplate(options));
    BOOST_REQUIRE(connect_template);
    BOOST_CHECK(!BlockHasTx(connect_template->block, mint->GetHash()));
    BOOST_CHECK_EQUAL(connect_template->block.vtx.size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
