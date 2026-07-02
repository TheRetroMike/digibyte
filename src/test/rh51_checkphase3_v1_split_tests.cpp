// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-51: Regtest activation-gate asymmetry — documentation / hardening test
 *
 * Target:
 *   src/validation.cpp::CheckMuSig2OracleBundleVersion
 *   src/oracle/bundle_manager.cpp::OracleDataValidator::ValidateBlockOracleData
 *
 * Context:
 *   On regtest, BIP9 DEPLOYMENT_DIGIDOLLAR is ALWAYS_ACTIVE (min_activation_height=0)
 *   but `nDDActivationHeight = 650`. On mainnet and testnet,
 *   `min_activation_height == nDDActivationHeight` — gates align.
 *
 *   Both CheckMuSig2OracleBundleVersion and ValidateBlockOracleData follow a
 *   two-branch pattern:
 *     if (pindex_prev)  -> gate on IsDigiDollarEnabled (BIP9)
 *     else              -> gate on block_height < nDDActivationHeight
 *
 *   The two gates are synonymous on production chains but diverge on regtest
 *   for heights 0..649. This test documents the gating behavior so it cannot
 *   silently drift under future refactors.
 *
 *   Note: this is NOT an exploit. It is a hardening/regression fixture showing
 *   how the two gates relate on each chain type. The adversarial-PoC variant
 *   originally filed here was re-analyzed and found to conflate the activation
 *   boundary (expected rejection at V1 activation of a legacy v=1 bundle) with a
 *   consensus split — reclassified to LOW hardening.
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <digidollar/digidollar.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/time.h>
#include <validation.h>

#include <limits>

namespace {

CScript MakeMinerV01Script(uint8_t oracle_id, uint64_t price_micro_usd, int64_t timestamp)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE;
    std::vector<unsigned char> compact;
    compact.reserve(18);
    compact.push_back(0x01);
    compact.push_back(oracle_id);
    for (int i = 0; i < 8; ++i) compact.push_back((price_micro_usd >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) {
        compact.push_back((static_cast<uint64_t>(timestamp) >> (i * 8)) & 0xFF);
    }
    script << compact;
    return script;
}

CBlock MakeRegtestBlock(const CScript& oracle_spk, int32_t height, uint32_t block_time)
{
    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime = block_time;
    block.nBits = 0x207fffff;
    block.hashPrevBlock.SetNull();
    block.nNonce = 0;

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << height << OP_0;

    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CTxOut oracle_out;
    oracle_out.nValue = 0;
    oracle_out.scriptPubKey = oracle_spk;
    coinbase.vout.push_back(oracle_out);

    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(rh51_checkphase3_v1_split_tests, RegTestingSetup)

// Regtest: BIP9 ALWAYS_ACTIVE vs height-gate @ 650 diverge for blocks 0..649.
BOOST_AUTO_TEST_CASE(rh51_sanity_regtest_gates_disagree)
{
    const Consensus::Params& params = Params().GetConsensus();
    const bool bip9_active_at_genesis =
        DigiDollar::IsDigiDollarEnabled(/*pindexPrev=*/nullptr, params);
    const int32_t height_gate = params.nDDActivationHeight;

    BOOST_TEST_MESSAGE("  regtest BIP9 active at genesis : " << bip9_active_at_genesis);
    BOOST_TEST_MESSAGE("  regtest nDDActivationHeight    : " << height_gate);
    BOOST_TEST_MESSAGE("  regtest deprecated legacy height: " << params.nDDActivationHeight);

    BOOST_CHECK(bip9_active_at_genesis);
    BOOST_CHECK_GT(height_gate, 1);
    BOOST_CHECK_EQUAL(height_gate, params.nDDActivationHeight);
}

// V1 parser behavior: legacy v=1 oracle data never parses as a valid bundle.
// The pre-activation validation path may ignore oracle outputs because DD is
// inactive, but the parser itself must not accept legacy oracle data.
BOOST_AUTO_TEST_CASE(rh51_legacy_v01_rejected_by_parser)
{
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t height = 100;
    BOOST_REQUIRE_LT(height, params.nDDActivationHeight);

    const int64_t now = GetTime();
    const CScript v01_spk = MakeMinerV01Script(0, 50'000'000ULL, now);
    const CBlock block = MakeRegtestBlock(v01_spk, height, static_cast<uint32_t>(now));

    {
        OracleBundleManager& mgr = OracleBundleManager::GetInstance();
        COracleBundle bundle;
        const bool extracted = mgr.ExtractOracleBundle(*block.vtx[0], bundle);
        BOOST_CHECK_MESSAGE(!extracted,
            "DigiDollar V1 must not parse legacy v0x01 oracle outputs");
    }

    BlockValidationState state;
    const bool ok = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, params, state);

    BOOST_TEST_MESSAGE("  pre-activation ValidateBlockOracleData legacy v=1 @ h=" << height
                       << " => " << (ok ? "ACCEPT" : "REJECT")
                       << " reason=" << state.GetRejectReason());
    BOOST_CHECK_MESSAGE(ok,
        "Pre-activation non-DD blocks are ignored by DD oracle validation, but legacy data is still unparseable");
}

// Activation boundary: CheckBlock has no pindex/BIP9 context, so it must not
// enforce oracle rules from static height alone. Contextual validation with
// pindex_prev still rejects the same legacy v=1 bundle at V1 activation.
BOOST_AUTO_TEST_CASE(rh51_checkblock_defers_oracle_phase_rules_to_context)
{
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t height = params.nDDActivationHeight;
    BOOST_REQUIRE_EQUAL(height, 650);

    const int64_t now = GetTime();
    const CScript v01_spk = MakeMinerV01Script(0, 50'000'000ULL, now);
    CBlock block = MakeRegtestBlock(v01_spk, height, static_cast<uint32_t>(now));

    BlockValidationState state;
    const bool ok = CheckBlock(block, state, params, false, false);

    BOOST_TEST_MESSAGE("  CheckBlock v=1 @ h=" << height
                       << " => " << (ok ? "ACCEPT" : "REJECT")
                       << " reason=" << state.GetRejectReason());
    BOOST_CHECK_MESSAGE(ok,
        "CheckBlock must not enforce oracle bundle rules without BIP9 context; got '"
        << state.GetRejectReason() << "'");

    CBlockIndex prev;
    prev.nHeight = height - 1;
    BlockValidationState contextual_state;
    const bool contextual_ok = OracleDataValidator::ValidateBlockOracleData(
        block, &prev, params, contextual_state);

    BOOST_TEST_MESSAGE("  contextual ValidateBlockOracleData v=1 @ h=" << height
                       << " => " << (contextual_ok ? "ACCEPT" : "REJECT")
                       << " reason=" << contextual_state.GetRejectReason());
    BOOST_CHECK(!contextual_ok);
    const std::string reason = contextual_state.GetRejectReason();
    const bool is_dd_related =
        (reason == "bad-oracle-malformed") ||
        (reason == "bad-oracle-legacy") ||
        (reason == "bad-oracle-bundle");
    BOOST_CHECK_MESSAGE(is_dd_related,
        "Expected contextual V1 MuSig2-only rejection reason; got '" << reason << "'");
}

// Pre-activation acceptance: at h=100 (< V1 activation), CheckBlock accepts
// legacy oracle-looking OP_RETURN data via the nullptr path's height-gate short-circuit.
BOOST_AUTO_TEST_CASE(rh51_preactivation_checkblock_ignores_legacy_v01)
{
    const Consensus::Params& params = Params().GetConsensus();
    const int32_t h = 100;
    BOOST_REQUIRE_LT(h, params.nDDActivationHeight);

    const int64_t now = GetTime();
    const CScript v01_spk = MakeMinerV01Script(0, 50'000'000ULL, now);
    CBlock block = MakeRegtestBlock(v01_spk, h, static_cast<uint32_t>(now));

    BlockValidationState state;
    const bool ok = CheckBlock(block, state, params, false, false);
    BOOST_TEST_MESSAGE("  CheckBlock v=1 @ h=" << h
                       << " => " << (ok ? "ACCEPT" : "REJECT")
                       << " reason=" << state.GetRejectReason());
    BOOST_CHECK(ok);
}

BOOST_AUTO_TEST_SUITE_END()
