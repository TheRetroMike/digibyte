// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-65: Mainnet and testnet oracle block validation must behave identically.
 *
 * Target:
 *   src/oracle/bundle_manager.cpp::OracleDataValidator::ValidateBlockOracleData
 *
 * Pre-fix behavior (the bug):
 *   ValidateBlockOracleData started with a chain-type short-circuit:
 *
 *     if (Params().GetChainType() != ChainType::TESTNET &&
 *         Params().GetChainType() != ChainType::REGTEST) {
 *         return true; // Oracle validation disabled on mainnet
 *     }
 *
 *   On mainnet (and signet), *every* block passed oracle validation
 *   unconditionally — defense primitives like the multi-output rejection
 *   at :2309-2313 (which rejects blocks with more than one OP_ORACLE
 *   output) never ran on mainnet. Testnet ran the full validator. The
 *   two chains did not share the same consensus semantics for oracle data.
 *
 * Post-fix invariant:
 *   Testnet and mainnet behave identically. Mainnet pre-activation blocks
 *   still return true via the BIP9/height gate at :2287-2295. Post-
 *   activation, mainnet runs the same phase-aware validator that testnet
 *   does, including the multi-oracle-output rejection.
 *
 * The escape hatches at :2316-2320 (block has no OP_ORACLE output) and
 * :2325-2328 (bundle extraction fails) intentionally stay: they are the
 * liveness guarantee that lets the chain keep producing blocks when the
 * oracle network is temporarily unavailable.
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/validation.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/time.h>

namespace {

// Build a coinbase with BIP34 height and N OP_RETURN OP_ORACLE outputs.
CBlock MakeBlockWithOracleOutputs(int32_t bip34_height,
                                  size_t n_oracle_outputs,
                                  uint32_t block_time)
{
    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime = block_time;
    block.nBits = 0x207fffff;
    block.hashPrevBlock.SetNull();
    block.nNonce = 0;

    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << bip34_height << OP_0;

    cb.vout.resize(1);
    cb.vout[0].nValue = 72000 * COIN;
    cb.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // Append N oracle OP_RETURN outputs. Two or more triggers the
    // multi-output rejection at bundle_manager.cpp:2309-2313.
    for (size_t i = 0; i < n_oracle_outputs; ++i) {
        CTxOut oracle_out;
        oracle_out.nValue = 0;
        CScript spk;
        spk << OP_RETURN << OP_ORACLE;
        // Minimal v0x01 payload; content doesn't matter for the
        // multi-output rejection path.
        std::vector<unsigned char> payload = {0x01, 0x00};
        spk << payload;
        oracle_out.scriptPubKey = spk;
        cb.vout.push_back(oracle_out);
    }

    block.vtx.push_back(MakeTransactionRef(std::move(cb)));
    return block;
}

struct ValidatorResult {
    bool ok;
    std::string reject_reason;
};

ValidatorResult RunValidator(const CBlock& block, const Consensus::Params& params)
{
    BlockValidationState state;
    bool ok = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, params, state);
    return {ok, state.GetRejectReason()};
}

// RAII: pin chain selection to a given chain and restore on exit.
class ScopedChainParams {
public:
    explicit ScopedChainParams(ChainType chain)
    {
        SelectParams(chain);
    }
    ~ScopedChainParams()
    {
        SelectParams(ChainType::REGTEST);
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(rh65_mainnet_testnet_validator_parity_tests, BasicTestingSetup)

// Post-activation mainnet block with TWO OP_ORACLE outputs must be REJECTED.
// Pre-fix: returns true because of the mainnet short-circuit.
// Post-fix: returns false with "bad-oracle-multiple-outputs" — same as testnet.
BOOST_AUTO_TEST_CASE(rh65_mainnet_rejects_multi_oracle_post_activation)
{
    ScopedChainParams chain(ChainType::MAIN);
    const auto& params = Params().GetConsensus();

    // Height well past mainnet activation — no pindex_prev so the
    // validator's nullptr path uses the BIP34 coinbase height gate.
    const int32_t height = params.nDDActivationHeight + 1000;
    BOOST_REQUIRE_GE(height, params.nDDActivationHeight);

    CBlock block = MakeBlockWithOracleOutputs(height, /*n_oracle_outputs=*/2,
                                              static_cast<uint32_t>(GetTime()));

    ValidatorResult r = RunValidator(block, params);
    BOOST_TEST_MESSAGE("  mainnet @ " << height << " (multi-output): ok="
                       << r.ok << " reject='" << r.reject_reason << "'");

    BOOST_CHECK_MESSAGE(!r.ok,
        "Mainnet validator must reject blocks with multiple OP_ORACLE outputs "
        "once DD is BIP9-active — same behavior as testnet.");
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-multiple-outputs");
}

// Testnet baseline: same block shape rejected by testnet validator.
BOOST_AUTO_TEST_CASE(rh65_testnet_rejects_multi_oracle_post_activation)
{
    ScopedChainParams chain(ChainType::TESTNET);
    const auto& params = Params().GetConsensus();

    const int32_t height = params.nDDActivationHeight + 100;
    BOOST_REQUIRE_GE(height, params.nDDActivationHeight);

    CBlock block = MakeBlockWithOracleOutputs(height, /*n_oracle_outputs=*/2,
                                              static_cast<uint32_t>(GetTime()));

    ValidatorResult r = RunValidator(block, params);
    BOOST_TEST_MESSAGE("  testnet @ " << height << " (multi-output): ok="
                       << r.ok << " reject='" << r.reject_reason << "'");

    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.reject_reason, "bad-oracle-multiple-outputs");
}

// Parity invariant: for the same input, mainnet and testnet return the same
// (ok, reject_reason) tuple. This is the core property of the fix.
BOOST_AUTO_TEST_CASE(rh65_mainnet_testnet_parity_multi_oracle)
{
    // Multi-oracle block exists BEFORE we switch chains so the two calls
    // see an identical tx structure.
    const uint32_t t = static_cast<uint32_t>(GetTime());

    ValidatorResult mainnet_result, testnet_result;
    {
        ScopedChainParams chain(ChainType::MAIN);
        const auto& p = Params().GetConsensus();
        CBlock b = MakeBlockWithOracleOutputs(p.nDDActivationHeight + 500, 2, t);
        mainnet_result = RunValidator(b, p);
    }
    {
        ScopedChainParams chain(ChainType::TESTNET);
        const auto& p = Params().GetConsensus();
        CBlock b = MakeBlockWithOracleOutputs(p.nDDActivationHeight + 500, 2, t);
        testnet_result = RunValidator(b, p);
    }

    BOOST_TEST_MESSAGE("  mainnet: ok=" << mainnet_result.ok
                       << " reject='" << mainnet_result.reject_reason << "'");
    BOOST_TEST_MESSAGE("  testnet: ok=" << testnet_result.ok
                       << " reject='" << testnet_result.reject_reason << "'");

    BOOST_CHECK_EQUAL(mainnet_result.ok, testnet_result.ok);
    BOOST_CHECK_EQUAL(mainnet_result.reject_reason, testnet_result.reject_reason);
}

// Pre-activation mainnet block stays accepted (BIP9/height gate). The
// escape hatches at :2316-2328 (no oracle output, malformed bundle) are
// preserved as the liveness guarantee when oracles are temporarily down.
BOOST_AUTO_TEST_CASE(rh65_mainnet_preactivation_still_accepts)
{
    ScopedChainParams chain(ChainType::MAIN);
    const auto& params = Params().GetConsensus();

    // Pre-activation height (well below nDDActivationHeight = 23627520).
    const int32_t height = 100;
    BOOST_REQUIRE_LT(height, params.nDDActivationHeight);

    CBlock block = MakeBlockWithOracleOutputs(height, /*n_oracle_outputs=*/2,
                                              static_cast<uint32_t>(GetTime()));
    ValidatorResult r = RunValidator(block, params);
    BOOST_TEST_MESSAGE("  mainnet @ " << height << " (pre-activation): ok=" << r.ok);

    // Pre-activation: BIP9/height gate at :2287-2295 short-circuits true.
    // Multi-output is NOT rejected because the validator never runs the
    // post-activation defensive checks pre-activation.
    BOOST_CHECK(r.ok);
}

// Post-activation mainnet block with NO oracle output stays accepted via
// the "transition period" escape hatch. The user's explicit design intent:
// "there are times when there may not be oracles" — this is the liveness
// guarantee that must not regress.
BOOST_AUTO_TEST_CASE(rh65_mainnet_no_oracle_output_still_accepts)
{
    ScopedChainParams chain(ChainType::MAIN);
    const auto& params = Params().GetConsensus();

    const int32_t height = params.nDDActivationHeight + 50;
    CBlock block = MakeBlockWithOracleOutputs(height, /*n_oracle_outputs=*/0,
                                              static_cast<uint32_t>(GetTime()));

    ValidatorResult r = RunValidator(block, params);
    BOOST_TEST_MESSAGE("  mainnet @ " << height << " (no oracle): ok=" << r.ok);

    BOOST_CHECK_MESSAGE(r.ok,
        "Post-activation mainnet block with no oracle output must be accepted "
        "via the transition-period escape hatch — oracles may be temporarily "
        "unavailable, and the chain must keep producing blocks.");
}

BOOST_AUTO_TEST_SUITE_END()
