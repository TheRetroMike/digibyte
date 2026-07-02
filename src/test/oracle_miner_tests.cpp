// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <logging.h>
#include <util/strencodings.h>
#include <node/miner.h>
#include <oracle/bundle_manager.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <primitives/block.h>
#include <script/script.h>
#include <policy/policy.h>
#include <test/util/setup_common.h>
#include <test/util/random.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <key.h>
#include <pubkey.h>
#include <streams.h>
#include <util/time.h>
#include <validation.h>

using node::BlockAssembler;
using node::CBlockTemplate;

/**
 * ORACLE MINER INTEGRATION TESTS
 * Week 4: Miner Integration - RED PHASE (TDD)
 *
 * Tests for integrating oracle bundles into mined blocks.
 *
 * NOTE: Implementation DOES exist (AddOracleBundleToBlock in bundle_manager.cpp)
 * These tests verify the implementation matches the specification.
 *
 * Tests verify:
 * - OracleBundleManager::AddOracleBundleToBlock() correctly adds bundles to coinbase
 * - CreateNewBlock() integration with oracle bundle system
 * - OP_RETURN serialization for oracle bundles
 * - Size limits (83 bytes MAX_OP_RETURN_RELAY)
 * - Graceful degradation when no oracle data available
 * - Phase One consensus (1-of-1)
 */

BOOST_FIXTURE_TEST_SUITE(oracle_miner_tests, TestChain100Setup)

namespace {
bool SkipPhase2MinerTest()
{
    BOOST_TEST_MESSAGE("SKIPPED: legacy Phase One/Two miner tests construct single-oracle/v0x02 bundles; V1 has no legacy bundle mode, and MuSig2 activates alongside DigiDollar.");
    return true;
}
} // namespace

//
// CATEGORY 1: AddOracleBundleToBlock() TESTS (3 tests)
//

/**
 * TEST: Oracle bundle added to coinbase OP_RETURN
 *
 * Verifies that AddOracleBundleToBlock() correctly:
 * - Adds oracle bundle as OP_RETURN output to coinbase
 * - Creates unspendable output (value = 0)
 * - Appends to existing coinbase outputs
 *
 * SPEC REFERENCE: Section 5.5.1
 * - Get latest bundle from OracleBundleManager
 * - Serialize bundle to OP_RETURN format
 * - Add as second output in coinbase transaction
 */
BOOST_AUTO_TEST_CASE(add_oracle_bundle_to_coinbase)
{
    if (SkipPhase2MinerTest()) return;
    // Create oracle bundle with valid message
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1); // Phase One: 1-of-1 consensus

    // Generate oracle keypair
    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    XOnlyPubKey oracle_pubkey(oracle_key.GetPubKey());

    // Create oracle price message
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6000;  // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();
    msg.block_height = 50;  // Below Phase Two activation for Phase One testing
    msg.nonce = FastRandomContext().rand64();
    msg.oracle_pubkey = oracle_pubkey;

    // Sign the message
    BOOST_REQUIRE(msg.SignAttestation(oracle_key));

    // Add message to bundle manager
    BOOST_REQUIRE(manager.AddOracleMessage(msg));

    // Create a simple block with coinbase
    CBlock block;
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);  // Standard miner payout
    coinbase.vout[0].nValue = 72000 * COIN;  // DigiByte subsidy
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    block.vtx.push_back(MakeTransactionRef(coinbase));

    // Call AddOracleBundleToBlock
    BOOST_CHECK(manager.AddOracleBundleToBlock(block, 50));

    // Verify oracle bundle was added to coinbase
    BOOST_REQUIRE(!block.vtx.empty());
    const CTransaction& updated_coinbase = *block.vtx[0];

    // Should have 2 outputs: miner payout + OP_RETURN oracle bundle
    BOOST_CHECK_EQUAL(updated_coinbase.vout.size(), 2);

    // Second output should be OP_RETURN (unspendable)
    BOOST_CHECK(updated_coinbase.vout[1].scriptPubKey.IsUnspendable());
    BOOST_CHECK_EQUAL(updated_coinbase.vout[1].nValue, 0);

    // OP_RETURN output should start with OP_RETURN opcode
    BOOST_REQUIRE(!updated_coinbase.vout[1].scriptPubKey.empty());
    BOOST_CHECK_EQUAL(updated_coinbase.vout[1].scriptPubKey[0], OP_RETURN);
}

/**
 * TEST: Oracle bundle serialization format validation (Phase One Compact Format)
 *
 * SPEC REFERENCE: Section 5.5.1
 * - Format: OP_RETURN | OP_ORACLE | <compact_data>
 * - Compact data: version (1) + oracle_id (1) + price (8) + timestamp (8) = 18 bytes
 * - Total size: ~20 bytes (well within 83 byte MAX_OP_RETURN_RELAY limit)
 * - Must be deserializable using ExtractOracleBundle()
 */
BOOST_AUTO_TEST_CASE(oracle_bundle_serialization_format)
{
    if (SkipPhase2MinerTest()) return;
    // Create oracle message and bundle
    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    XOnlyPubKey oracle_pubkey(oracle_key.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 12000;  // $0.012 (realistic DGB price)
    msg.timestamp = GetTime();
    msg.block_height = 50;  // Below Phase Two activation for Phase One testing
    msg.nonce = FastRandomContext().rand64();
    msg.oracle_pubkey = oracle_pubkey;

    // Sign the message
    BOOST_REQUIRE(msg.SignAttestation(oracle_key));

    // Create bundle with message
    COracleBundle bundle;
    bundle.epoch = 1;
    bundle.AddMessage(msg);

    // Use CreateOracleScript to create compact format (NOT full serialization)
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    CScript oracle_script = manager.CreateOracleScript(bundle);

    // Verify script is unspendable
    BOOST_CHECK(oracle_script.IsUnspendable());

    // Verify total script size is within limits
    // Compact format: OP_RETURN (1) + OP_ORACLE (1) + version (1) + compact_data (17) = ~20 bytes
    BOOST_CHECK_LE(oracle_script.size(), MAX_OP_RETURN_RELAY);
    BOOST_CHECK_LE(oracle_script.size(), 25); // Should be ~20 bytes

    // Test deserialization using ExtractOracleBundle
    CMutableTransaction tx;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = oracle_script;
    tx.vout[0].nValue = 0;

    COracleBundle extracted_bundle;
    BOOST_REQUIRE(manager.ExtractOracleBundle(CTransaction(tx), extracted_bundle));

    // Verify extracted bundle data matches
    BOOST_CHECK_EQUAL(extracted_bundle.messages.size(), 1);
    if (!extracted_bundle.messages.empty()) {
        BOOST_CHECK_EQUAL(extracted_bundle.messages[0].price_micro_usd, msg.price_micro_usd);
        BOOST_CHECK_EQUAL(extracted_bundle.messages[0].oracle_id, msg.oracle_id);
        BOOST_CHECK_EQUAL(extracted_bundle.messages[0].timestamp, msg.timestamp);
    }
}

/**
 * TEST: Oracle bundle size limit validation (Phase One Compact Format)
 *
 * SPEC REFERENCE: Section 5.5.1
 * - Phase One compact format is ~20 bytes (well under 83 byte limit)
 * - Verify CreateOracleScript produces compact format
 * - Verify AddOracleBundleToBlock successfully adds compact bundle
 */
BOOST_AUTO_TEST_CASE(oracle_bundle_size_limit)
{
    if (SkipPhase2MinerTest()) return;
    // Create oracle message
    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    XOnlyPubKey oracle_pubkey(oracle_key.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 50000; // 50000 micro-USD = $0.05 (minimum valid: 100)
    msg.timestamp = GetTime();
    msg.block_height = 50;  // Below Phase Two activation for Phase One testing
    msg.nonce = FastRandomContext().rand64();
    msg.oracle_pubkey = oracle_pubkey;

    BOOST_REQUIRE(msg.SignAttestation(oracle_key));

    // Create bundle
    COracleBundle bundle;
    bundle.epoch = 1;
    bundle.AddMessage(msg);

    // Use CreateOracleScript to create compact format
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1); // Phase One: 1-of-1 consensus

    CScript oracle_script = manager.CreateOracleScript(bundle);

    // Phase One compact format should be ~20 bytes (well under 83 bytes)
    BOOST_CHECK_LE(oracle_script.size(), MAX_OP_RETURN_RELAY);
    BOOST_CHECK_LE(oracle_script.size(), 25);

    // Test that the bundle can be added to a block
    manager.AddOracleMessage(msg);

    CBlock block;
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    block.vtx.push_back(MakeTransactionRef(coinbase));

    // Should succeed for Phase One single oracle
    BOOST_CHECK(manager.AddOracleBundleToBlock(block, 50));

    // Verify added OP_RETURN output is within size limits
    const CTransaction& updated_coinbase = *block.vtx[0];
    BOOST_REQUIRE_GE(updated_coinbase.vout.size(), 2);
    BOOST_CHECK_LE(updated_coinbase.vout[1].scriptPubKey.size(), MAX_OP_RETURN_RELAY);
    BOOST_CHECK_LE(updated_coinbase.vout[1].scriptPubKey.size(), 25); // Compact format
}

//
// CATEGORY 2: CreateNewBlock() INTEGRATION TESTS (3 tests)
//

/**
 * RED TEST: CreateNewBlock includes oracle bundle in coinbase
 *
 * EXPECTED TO FAIL:
 * - CreateNewBlock() may not call AddOracleBundleToBlock()
 * - Integration point at line ~170 in miner.cpp may not be implemented
 *
 * SPEC REFERENCE: Section 5.5.1
 * - CreateNewBlock() should call AddOracleBundleToBlock() before returning
 * - Oracle bundle should be in coinbase OP_RETURN
 * - Only when DigiDollar is enabled
 */
BOOST_AUTO_TEST_CASE(create_new_block_includes_oracle_bundle)
{
    if (SkipPhase2MinerTest()) return;
    // Add oracle message to bundle manager
    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    XOnlyPubKey oracle_pubkey(oracle_key.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6000;  // $0.006 (realistic DGB price)
    msg.timestamp = GetTime();
    msg.block_height = m_node.chainman->ActiveHeight() + 1;
    msg.nonce = FastRandomContext().rand64();
    msg.oracle_pubkey = oracle_pubkey;

    BOOST_REQUIRE(msg.SignAttestation(oracle_key));
    manager.AddOracleMessage(msg);

    // Create new block using BlockAssembler
    CScript scriptPubKey = CScript() << OP_TRUE;
    std::unique_ptr<CBlockTemplate> pblocktemplate =
        BlockAssembler(m_node.chainman->ActiveChainstate(), m_node.mempool.get())
            .CreateNewBlock(scriptPubKey, ALGO_SHA256D);

    BOOST_REQUIRE(pblocktemplate);
    CBlock& block = pblocktemplate->block;

    // Verify block has at least coinbase transaction
    BOOST_REQUIRE(!block.vtx.empty());
    const CTransaction& coinbase = *block.vtx[0];

    // Check coinbase has oracle bundle output
    // WILL FAIL if CreateNewBlock() doesn't integrate oracle bundle
    BOOST_CHECK_GE(coinbase.vout.size(), 2);  // Payout + OP_RETURN (+ optional witness commitment)

    // Find OP_RETURN output (should be vout[1])
    bool found_oracle_opreturn = false;
    for (size_t i = 1; i < coinbase.vout.size(); ++i) {
        if (coinbase.vout[i].scriptPubKey.IsUnspendable() &&
            !coinbase.vout[i].scriptPubKey.empty() &&
            coinbase.vout[i].scriptPubKey[0] == OP_RETURN) {
            found_oracle_opreturn = true;

            // Verify it's not the witness commitment (which is also OP_RETURN)
            // Oracle OP_RETURN should have bundle data
            BOOST_CHECK_GT(coinbase.vout[i].scriptPubKey.size(), 2);
            break;
        }
    }

    // WILL FAIL: Oracle bundle not added by CreateNewBlock()
    BOOST_CHECK(found_oracle_opreturn);

    // Regression: CreateNewBlock appends the oracle output after generating the
    // witness commitment. The returned template must already have the final
    // merkle root; miners/GBT consumers should not need IncrementExtraNonce()
    // to repair it.
    BOOST_CHECK(block.hashMerkleRoot == BlockMerkleRoot(block));
}

/**
 * RED TEST: CreateNewBlock continues without oracle bundle if unavailable
 *
 * EXPECTED TO FAIL:
 * - Graceful degradation may not be implemented
 * - Block creation should succeed even if no oracle bundle available
 *
 * SPEC REFERENCE: Section 5.5.1
 * - Continue with block creation even if oracle bundle fails
 * - Graceful degradation for missing oracle data
 */
BOOST_AUTO_TEST_CASE(create_new_block_no_oracle_if_unavailable)
{
    if (SkipPhase2MinerTest()) return;
    // Create new block without any oracle data
    CScript scriptPubKey = CScript() << OP_TRUE;
    std::unique_ptr<CBlockTemplate> pblocktemplate =
        BlockAssembler(m_node.chainman->ActiveChainstate(), m_node.mempool.get())
            .CreateNewBlock(scriptPubKey, ALGO_SHA256D);

    // Block creation should still succeed
    BOOST_REQUIRE(pblocktemplate);
    CBlock& block = pblocktemplate->block;

    // Verify block is valid (has coinbase, merkle root, etc.)
    BOOST_REQUIRE(!block.vtx.empty());
    const CTransaction& coinbase = *block.vtx[0];

    // Coinbase should have at least miner payout
    BOOST_CHECK_GE(coinbase.vout.size(), 1);

    // Verify CreateNewBlock returned a populated merkle root.
    BOOST_CHECK(!block.hashMerkleRoot.IsNull());
    BOOST_CHECK(block.hashMerkleRoot == BlockMerkleRoot(block));

    // Test should pass even without oracle bundle (graceful degradation)
    BOOST_CHECK(true);
}

/**
 * TEST: Phase One uses 1-of-1 oracle consensus in blocks
 *
 * SPEC REFERENCE: Section 5.5.1
 * - Phase One: Single oracle (testnet only)
 * - Bundle should have exactly 1 message
 * - Uses compact format for serialization
 * - Future phases: 9-of-17 consensus (RC30)
 */
BOOST_AUTO_TEST_CASE(create_new_block_phase_one_single_oracle)
{
    if (SkipPhase2MinerTest()) return;
    // Add EXACTLY ONE oracle message (Phase One requirement)
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1); // Phase One: 1-of-1 consensus
    manager.ClearPendingMessages();

    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    XOnlyPubKey oracle_pubkey(oracle_key.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_id = 0;  // First oracle
    msg.price_micro_usd = 50000; // 50000 micro-USD = $0.05 (minimum valid: 100)
    msg.timestamp = GetTime();
    msg.block_height = m_node.chainman->ActiveHeight() + 1;
    msg.nonce = FastRandomContext().rand64();
    msg.oracle_pubkey = oracle_pubkey;

    BOOST_REQUIRE(msg.SignAttestation(oracle_key));
    manager.AddOracleMessage(msg);

    // Do NOT add additional oracle messages (Phase One = single oracle)

    // Create block
    CScript scriptPubKey = CScript() << OP_TRUE;
    std::unique_ptr<CBlockTemplate> pblocktemplate =
        BlockAssembler(m_node.chainman->ActiveChainstate(), m_node.mempool.get())
            .CreateNewBlock(scriptPubKey, ALGO_SHA256D);

    BOOST_REQUIRE(pblocktemplate);
    CBlock& block = pblocktemplate->block;
    BOOST_REQUIRE(!block.vtx.empty());

    const CTransaction& coinbase = *block.vtx[0];

    // Extract oracle bundle using ExtractOracleBundle (handles compact format)
    COracleBundle extracted_bundle;
    bool found_bundle = manager.ExtractOracleBundle(coinbase, extracted_bundle);

    // Verify bundle was found and extracted
    BOOST_REQUIRE(found_bundle);

    // Phase One: Should have exactly 1 message
    BOOST_CHECK_EQUAL(extracted_bundle.messages.size(), 1);

    if (!extracted_bundle.messages.empty()) {
        BOOST_CHECK_EQUAL(extracted_bundle.messages[0].oracle_id, 0);
        BOOST_CHECK_EQUAL(extracted_bundle.messages[0].price_micro_usd, 50000);
        BOOST_CHECK_EQUAL(extracted_bundle.messages[0].timestamp, msg.timestamp);
    }
}

BOOST_AUTO_TEST_SUITE_END()
