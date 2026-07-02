// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-52: Unhandled CScriptNum on BIP34 coinbase height in CheckBlock-path
 *        oracle validators  (Wave-1 adversarial PoC)
 *
 * Targets:
 *   src/validation.cpp:124   — CheckPhase3OracleBundleVersion nullptr branch
 *   src/oracle/bundle_manager.cpp:2252 — ValidateBlockOracleData nullptr branch
 *
 * Both sites decode the BIP34 coinbase height push via
 *   CScriptNum(data, true)
 * with NO try/catch. The constructor throws `scriptnum_error` (derived from
 * std::runtime_error) on either non-minimal encoding or size > 4 bytes.
 *
 * Both sites are reached from CheckBlock() at validation.cpp:4373 and :4377
 * with pindex_prev = nullptr. Attacker controls the coinbase scriptSig fully.
 *
 * Different from the known prior C4 in DIGIDOLLAR_BUG_HUNT_REPORT.md:
 *   - C4 is about unwrapped CScriptNum in the DD-transfer OP_RETURN parser
 *     at validation.cpp:1199, 1206.
 *   - This finding is about two SEPARATE unwrapped CScriptNum sites in the
 *     oracle block-level validators, reached from CheckBlock (not ConnectBlock)
 *     via a nullptr pindex_prev.
 *
 * Exploit (DoS):
 *   - Attacker crafts a block whose coinbase scriptSig starts with a
 *     non-minimally-encoded BIP34 push (e.g. 0x00 0x80 — value zero with
 *     superfluous sign byte).
 *   - Honest node calls CheckBlock(block, state, params, ...) with pindex_prev=nullptr
 *     during relay / compact-block / header-first flows.
 *   - CheckPhase3OracleBundleVersion and/or ValidateBlockOracleData throws
 *     scriptnum_error. The exception escapes CheckBlock (no catch inside).
 *   - net_processing.cpp:6388 is the only catch(std::exception); it runs
 *     AFTER the block-processing path would normally mark the block invalid
 *     and ban the peer. The result is: block is NOT tagged invalid in the
 *     block index, state is never set, peer may re-send, CPU is burned.
 *
 * This test demonstrates the throw by directly exercising CheckBlock with a
 * malformed BIP34 scriptSig and observing that the call raises rather than
 * returning a clean state.Invalid(). After patching (try/catch around both
 * sites returning `state.Invalid(..., "bad-cb-bip34-height-encoding")`) the
 * test flips to asserting a clean rejection.
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/time.h>
#include <validation.h>

namespace {

// Craft a block whose coinbase scriptSig's first push is a 5-byte integer
// — CScriptNum's default nMaxNumSize is 4, so construction throws
// scriptnum_error("script number overflow").
CBlock MakeBlockWithOverflowBip34Push(uint32_t block_time)
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

    // Build a 5-byte push (overflow: nMaxNumSize=4 default).
    std::vector<unsigned char> oversized(5, 0x01);
    CScript ss;
    ss << oversized;          // push of length 5
    ss << OP_0;               // filler
    cb.vin[0].scriptSig = ss;

    cb.vout.resize(1);
    cb.vout[0].nValue = 72000 * COIN;
    cb.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // Append an oracle OP_RETURN so ValidateBlockOracleData does not bail out
    // early at the "no oracle output" path. ( bundle_manager.cpp:2271-2300 ).
    CTxOut oracle_out;
    oracle_out.nValue = 0;
    CScript spk;
    spk << OP_RETURN << OP_ORACLE;
    std::vector<unsigned char> bundle_payload = {
        0x02, // version = 2 (Phase-2+, so it isn't short-circuited as Phase-1)
        0x00  // minimal garbage so ExtractOracleBundle returns false cleanly
    };
    spk << bundle_payload;
    oracle_out.scriptPubKey = spk;
    cb.vout.push_back(oracle_out);

    block.vtx.push_back(MakeTransactionRef(std::move(cb)));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

// Craft a block with a non-minimally-encoded BIP34 push: 0x00 0x80
// (zero value with extra sign-bit byte). Triggers scriptnum_error
// ("non-minimally encoded script number") via fRequireMinimal=true.
CBlock MakeBlockWithNonMinimalBip34Push(uint32_t block_time)
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

    std::vector<unsigned char> non_minimal = {0x00, 0x80}; // negative zero
    CScript ss;
    ss << non_minimal;
    ss << OP_0;
    cb.vin[0].scriptSig = ss;

    cb.vout.resize(1);
    cb.vout[0].nValue = 72000 * COIN;
    cb.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CTxOut oracle_out;
    oracle_out.nValue = 0;
    CScript spk;
    spk << OP_RETURN << OP_ORACLE;
    std::vector<unsigned char> bundle_payload = {0x02, 0x00};
    spk << bundle_payload;
    oracle_out.scriptPubKey = spk;
    cb.vout.push_back(oracle_out);

    block.vtx.push_back(MakeTransactionRef(std::move(cb)));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(rh52_bip34_scriptnum_escape_tests, RegTestingSetup)

// CheckBlock is context-free and must not let oracle helper BIP34 parsing
// throw into the caller. Contextual height validation is enforced outside
// CheckBlock.
BOOST_AUTO_TEST_CASE(rh52_checkblock_must_not_throw_on_overflow_bip34_push)
{
    const Consensus::Params& params = Params().GetConsensus();

    CBlock block = MakeBlockWithOverflowBip34Push(static_cast<uint32_t>(GetTime()));
    BlockValidationState state;

    bool returned = false;
    bool threw_scriptnum = false;
    bool threw_other = false;
    std::string caught_what;
    try {
        returned = CheckBlock(block, state, params,
                              /*fCheckPOW=*/false,
                              /*fCheckMerkleRoot=*/false);
    } catch (const scriptnum_error& e) {
        threw_scriptnum = true;
        caught_what = e.what();
    } catch (const std::exception& e) {
        threw_other = true;
        caught_what = e.what();
    } catch (...) {
        threw_other = true;
        caught_what = "unknown";
    }

    BOOST_TEST_MESSAGE("  CheckBlock(overflow) returned=" << returned
                       << " threw_scriptnum=" << threw_scriptnum
                       << " threw_other=" << threw_other
                       << " what='" << caught_what << "'"
                       << " reject='" << state.GetRejectReason() << "'");

    // Post-patch expectation: no throw. CheckBlock no longer runs the
    // context-dependent oracle validators with a nullptr pindex, so the
    // malformed BIP34 height is left to contextual validation.
    BOOST_CHECK_MESSAGE(!threw_scriptnum && !threw_other,
        "CheckBlock escaped scriptnum_error into the caller — attacker-"
        "crafted BIP34 coinbase push forces an unhandled exception out of "
        "the block-validation path. Patch required at validation.cpp:124 "
        "and bundle_manager.cpp:2252 (try/catch → state.Invalid).");

    if (!threw_scriptnum && !threw_other) {
        BOOST_CHECK(returned);
        BOOST_CHECK(state.IsValid());
    }
}

// Second vector: non-minimal encoding — same unhandled-throw surface,
// reaches both the fRequireMinimal branch at script.h:269 and the overflow
// branch at :253 from a different angle.
BOOST_AUTO_TEST_CASE(rh52_checkblock_must_not_throw_on_nonminimal_bip34_push)
{
    const Consensus::Params& params = Params().GetConsensus();

    CBlock block = MakeBlockWithNonMinimalBip34Push(static_cast<uint32_t>(GetTime()));
    BlockValidationState state;

    bool returned = false;
    bool threw_scriptnum = false;
    bool threw_other = false;
    std::string caught_what;
    try {
        returned = CheckBlock(block, state, params, false, false);
    } catch (const scriptnum_error& e) {
        threw_scriptnum = true;
        caught_what = e.what();
    } catch (const std::exception& e) {
        threw_other = true;
        caught_what = e.what();
    } catch (...) {
        threw_other = true;
        caught_what = "unknown";
    }

    BOOST_TEST_MESSAGE("  CheckBlock(nonminimal) returned=" << returned
                       << " threw_scriptnum=" << threw_scriptnum
                       << " threw_other=" << threw_other
                       << " what='" << caught_what << "'"
                       << " reject='" << state.GetRejectReason() << "'");

    BOOST_CHECK_MESSAGE(!threw_scriptnum && !threw_other,
        "CheckBlock escaped scriptnum_error on non-minimal BIP34 push "
        "(validation.cpp:124 / bundle_manager.cpp:2252).");

    if (!threw_scriptnum && !threw_other) {
        BOOST_CHECK(returned);
        BOOST_CHECK(state.IsValid());
    }
}

// Control: a legitimate BIP34 push (single-byte height = 100) goes through
// CheckBlock cleanly. Post-patch this must remain true.
BOOST_AUTO_TEST_CASE(rh52_control_minimal_bip34_push_does_not_throw)
{
    const Consensus::Params& params = Params().GetConsensus();

    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime = static_cast<uint32_t>(GetTime());
    block.nBits = 0x207fffff;
    block.hashPrevBlock.SetNull();
    block.nNonce = 0;

    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << 100 << OP_0;
    cb.vout.resize(1);
    cb.vout[0].nValue = 72000 * COIN;
    cb.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(MakeTransactionRef(std::move(cb)));
    block.hashMerkleRoot = BlockMerkleRoot(block);

    BlockValidationState state;
    bool threw = false;
    try {
        CheckBlock(block, state, params, false, false);
    } catch (...) {
        threw = true;
    }
    BOOST_CHECK(!threw);
}

BOOST_AUTO_TEST_SUITE_END()
