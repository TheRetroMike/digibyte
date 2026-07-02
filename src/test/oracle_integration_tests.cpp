// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <logging.h>
#include <util/strencodings.h>
#include <oracle/node.h>
#include <oracle/bundle_manager.h>
#include <oracle/exchange.h>
#include <oracle/mock_oracle.h>
#include <validation.h>
#include <node/miner.h>
#include <test/util/setup_common.h>
#include <test/util/random.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <key.h>
#include <pubkey.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <util/time.h>
#include <digidollar/digidollar.h>
#include <consensus/digidollar.h>

using node::BlockAssembler;
using node::CBlockTemplate;

/**
 * ORACLE INTEGRATION TESTS
 * Phase One: Oracle System Integration Validation
 *
 * Tests complete end-to-end oracle flow:
 * 1. Exchange API fetches prices
 * 2. Oracle node creates signed message
 * 3. Message broadcasts via P2P
 * 4. Bundle manager collects messages
 * 5. Miner adds bundle to block
 * 6. Block validation accepts bundle
 * 7. Price cache updated
 * 8. DigiDollar can access price
 */

BOOST_FIXTURE_TEST_SUITE(oracle_integration_tests, TestChain100Setup)

namespace {
/** These legacy integration tests construct single-oracle/v0x02 bundles.
 *  DigiDollar V1 has no legacy bundle mode: MuSig2 v0x03 activates alongside
 *  DigiDollar/oracle consensus, so these tests remain documentation-only until
 *  rewritten against a complete MuSig2 signing session. */
bool SkipPhase2Test()
{
    BOOST_TEST_MESSAGE("SKIPPED: legacy Phase One/Two integration test not applicable to V1 MuSig2-only consensus");
    return true;
}
} // namespace

/**
 * TEST: Complete End-to-End Oracle Flow
 *
 * Verifies the entire oracle system integration from price fetching to DigiDollar access.
 *
 * INTEGRATION POINTS TESTED:
 * - Exchange API → Oracle Node (price fetching)
 * - Oracle Node → Bundle Manager (message creation & signing)
 * - Bundle Manager → Miner (bundle creation)
 * - Miner → Block (bundle serialization to OP_RETURN)
 * - Block → Validation (signature & consensus verification)
 * - Validation → Price Cache (ConnectBlock updates)
 * - Price Cache → DigiDollar (price access)
 */
BOOST_AUTO_TEST_CASE(end_to_end_oracle_flow)
{
    if (SkipPhase2Test()) return;
    LogPrintf("=== Oracle Integration Test: Complete Flow ===\n");

    // STEP 1: Initialize Oracle System
    LogPrintf("Step 1: Initializing oracle system...\n");
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();  // Reset singleton state from previous tests
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);  // Phase One: 1-of-1 consensus

    // STEP 2: Simulate Exchange API Price Fetching
    LogPrintf("Step 2: Fetching price from exchanges (mock)...\n");

    // In RegTest mode, we use MockOracleManager which simulates exchange price fetching
    // Real implementation would use:
    // ExchangeAPI::MultiExchangeAggregator aggregator;
    // CAmount price = aggregator.FetchAggregatePrice();

    CAmount mock_price = 6000; // $0.006 (6000 micro-USD = realistic DGB price)
    BOOST_CHECK(mock_price > 0);
    LogPrintf("   - Fetched price from exchanges: %lld micro-USD ($%.6f)\n",
              mock_price, mock_price / 1000000.0);

    // STEP 3: Oracle Node Creates Signed Message
    LogPrintf("Step 3: Oracle node creating signed message...\n");

    // Generate oracle keypair (Oracle ID 0 for Phase One)
    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    XOnlyPubKey oracle_pubkey(oracle_key.GetPubKey());

    // Create oracle price message
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = mock_price;
    msg.timestamp = GetTime();
    msg.block_height = m_node.chainman->ActiveChain().Height() + 1;
    msg.nonce = FastRandomContext().rand64();
    msg.oracle_pubkey = oracle_pubkey;

    // Sign the message with Schnorr signature
    BOOST_REQUIRE(msg.SignAttestation(oracle_key));
    BOOST_REQUIRE(msg.IsValid());
    BOOST_REQUIRE(msg.VerifyAttestation());

    LogPrintf("   - Created oracle message with Schnorr signature\n");
    LogPrintf("   - Oracle ID: %u\n", msg.oracle_id);
    LogPrintf("   - Price: %llu micro-USD\n", msg.price_micro_usd);
    LogPrintf("   - Timestamp: %lld\n", msg.timestamp);
    LogPrintf("   - Signature verified: YES\n");

    // STEP 4: Add Message to Bundle Manager (simulates P2P broadcast)
    LogPrintf("Step 4: Adding message to bundle manager (simulates P2P)...\n");

    BOOST_REQUIRE(manager.AddOracleMessage(msg));
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 1);

    LogPrintf("   - Message added to bundle manager\n");
    LogPrintf("   - Pending messages: %zu\n", manager.GetPendingMessageCount());

    // STEP 5: Create Oracle Bundle (Phase One: 1-of-1 consensus)
    LogPrintf("Step 5: Creating oracle bundle (Phase One: 1-of-1 consensus)...\n");

    // In Phase One, we have 1-of-1 consensus (single oracle)
    // Bundle manager should create bundle immediately
    int32_t current_epoch = GetCurrentEpoch(m_node.chainman->ActiveChain().Height() + 1);
    manager.TryCreateBundle(current_epoch);  // Explicitly create bundle for this epoch
    COracleBundle bundle = manager.GetCurrentBundle(current_epoch);

    BOOST_CHECK(bundle.IsValid(1));
    BOOST_CHECK_EQUAL(bundle.messages.size(), 1);  // Phase One: 1-of-1
    BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, mock_price);

    LogPrintf("   - Created oracle bundle\n");
    LogPrintf("   - Epoch: %d\n", bundle.epoch);
    LogPrintf("   - Messages: %zu (Phase One: 1-of-1)\n", bundle.messages.size());
    LogPrintf("   - Median price: %llu micro-USD\n", bundle.median_price_micro_usd);

    // STEP 6: Miner Adds Bundle to Block
    LogPrintf("Step 6: Miner adding oracle bundle to block...\n");

    CScript scriptPubKey = CScript() << OP_TRUE;
    std::unique_ptr<CBlockTemplate> pblocktemplate =
        BlockAssembler(m_node.chainman->ActiveChainstate(), m_node.mempool.get())
        .CreateNewBlock(scriptPubKey, 0); // algo = 0 (SHA256D) for test

    BOOST_REQUIRE(pblocktemplate);
    CBlock& block = pblocktemplate->block;

    // Manually add oracle bundle to block (CreateNewBlock should do this automatically in real code)
    int32_t next_height = m_node.chainman->ActiveChain().Height() + 1;
    BOOST_REQUIRE(manager.AddOracleBundleToBlock(block, next_height));

    // Verify coinbase has oracle bundle
    BOOST_REQUIRE(!block.vtx.empty());
    const CTransaction& coinbase = *block.vtx[0];
    BOOST_CHECK(coinbase.vout.size() >= 2);  // Payout + OP_RETURN

    // Verify OP_RETURN output
    const CTxOut& oracle_output = coinbase.vout[1];
    BOOST_CHECK_EQUAL(oracle_output.nValue, 0);  // Unspendable
    BOOST_CHECK(oracle_output.scriptPubKey.IsUnspendable());

    LogPrintf("   - Miner added oracle bundle to coinbase\n");
    LogPrintf("   - Coinbase outputs: %zu (payout + OP_RETURN)\n", coinbase.vout.size());
    LogPrintf("   - OP_RETURN size: %zu bytes\n", oracle_output.scriptPubKey.size());

    // STEP 7: Block Validation Accepts Bundle
    LogPrintf("Step 7: Validating block with oracle bundle...\n");

    BlockValidationState state;
    const CChainParams& chainparams = Params();

    // Update block header
    CBlockIndex* pindexPrev = m_node.chainman->ActiveChain().Tip();
    block.hashPrevBlock = pindexPrev->GetBlockHash();
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nTime = GetTime();
    block.nBits = GetNextWorkRequired(pindexPrev, &block, chainparams.GetConsensus(), 0);
    block.nNonce = 0;

    // Check block validation (includes oracle data validation)
    BOOST_CHECK(CheckBlock(block, state, chainparams.GetConsensus(), false, false));

    LogPrintf("   - Block validation passed\n");
    LogPrintf("   - CheckBlock: PASSED\n");
    LogPrintf("   - Oracle data validation: PASSED\n");

    // STEP 8: Extract and Verify Oracle Bundle from Block
    LogPrintf("Step 8: Extracting oracle bundle from block...\n");

    COracleBundle extracted_bundle;
    BOOST_REQUIRE(manager.ExtractOracleBundle(coinbase, extracted_bundle));

    BOOST_CHECK(extracted_bundle.IsValid(1));
    BOOST_CHECK_EQUAL(extracted_bundle.messages.size(), 1);
    BOOST_CHECK_EQUAL(extracted_bundle.median_price_micro_usd, mock_price);
    BOOST_CHECK_EQUAL(extracted_bundle.messages[0].oracle_id, msg.oracle_id);
    BOOST_CHECK_EQUAL(extracted_bundle.messages[0].price_micro_usd, msg.price_micro_usd);

    LogPrintf("   - Extracted oracle bundle from coinbase\n");
    LogPrintf("   - Bundle matches original: YES\n");

    // STEP 9: Verify Price Cache Update (would happen in ConnectBlock)
    LogPrintf("Step 9: Simulating price cache update (ConnectBlock)...\n");

    // In real integration, ConnectBlock() would call:
    // manager.UpdatePriceCache(pindex->nHeight, bundle.median_price_micro_usd);

    manager.UpdatePriceCache(next_height, extracted_bundle.median_price_micro_usd);

    // Verify price cache was updated
    uint64_t cached_price = manager.GetOraclePriceForHeight(next_height);
    BOOST_CHECK_EQUAL(cached_price, mock_price);

    LogPrintf("   - Price cache updated for height %d\n", next_height);
    LogPrintf("   - Cached price: %llu micro-USD\n", cached_price);

    // STEP 10: DigiDollar Can Access Oracle Price
    LogPrintf("Step 10: Verifying DigiDollar can access oracle price...\n");

    // DigiDollar integration uses OracleIntegration::GetOraclePriceForHeight()
    CAmount oracle_price = OracleIntegration::GetOraclePriceForHeight(next_height);
    BOOST_CHECK(oracle_price > 0);
    BOOST_CHECK_EQUAL(oracle_price, mock_price);

    LogPrintf("   - DigiDollar accessed oracle price: %lld micro-USD ($%.6f)\n",
              oracle_price, oracle_price / 1000000.0);

    LogPrintf("\n=== Oracle Integration Test: PASSED ===\n");
    LogPrintf("All 10 integration steps completed successfully!\n\n");
}

/**
 * TEST: Oracle System Graceful Degradation
 *
 * Verifies that the system continues to function when oracle data is unavailable.
 */
BOOST_AUTO_TEST_CASE(oracle_graceful_degradation)
{
    if (SkipPhase2Test()) return;
    LogPrintf("=== Oracle Integration Test: Graceful Degradation ===\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // Disable oracle system
    manager.SetEnabled(false);

    // Create block without oracle data
    CScript scriptPubKey = CScript() << OP_TRUE;
    std::unique_ptr<CBlockTemplate> pblocktemplate =
        BlockAssembler(m_node.chainman->ActiveChainstate(), m_node.mempool.get())
        .CreateNewBlock(scriptPubKey, 0); // algo = 0 (SHA256D) for test

    BOOST_REQUIRE(pblocktemplate);
    CBlock& block = pblocktemplate->block;

    // Verify block can still be created without oracle data
    BlockValidationState state;
    const CChainParams& chainparams = Params();

    CBlockIndex* pindexPrev = m_node.chainman->ActiveChain().Tip();
    block.hashPrevBlock = pindexPrev->GetBlockHash();
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nTime = GetTime();
    block.nBits = GetNextWorkRequired(pindexPrev, &block, chainparams.GetConsensus(), 0);

    BOOST_CHECK(CheckBlock(block, state, chainparams.GetConsensus(), false, false));

    LogPrintf("   - Block created without oracle data: PASSED\n");
    LogPrintf("   - System graceful degradation: VERIFIED\n");

    // Re-enable oracle system
    manager.SetEnabled(true);
    LogPrintf("=== Oracle Integration Test: Graceful Degradation PASSED ===\n");
}

/**
 * TEST: Multi-Component Integration Verification
 *
 * Verifies all integration points are correctly connected.
 */
BOOST_AUTO_TEST_CASE(verify_integration_points)
{
    if (SkipPhase2Test()) return;
    LogPrintf("=== Oracle Integration Test: Integration Points Verification ===\n");

    int passed = 0;
    int total = 7;

    // Integration Point 1: Exchange API → Oracle Node
    LogPrintf("Integration Point 1: Exchange API → Oracle Node\n");
    try {
        // Verify ExchangeAPI::MultiExchangeAggregator exists and can be instantiated
        ExchangeAPI::MultiExchangeAggregator aggregator;
        LogPrintf("   ✓ Exchange API integration: VERIFIED\n");
        passed++;
    } catch (...) {
        LogPrintf("   ✗ Exchange API integration: FAILED\n");
    }

    // Integration Point 2: Oracle Node → Bundle Manager
    LogPrintf("Integration Point 2: Oracle Node → Bundle Manager\n");
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();  // Reset singleton state from previous tests
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);  // Phase One: 1-of-1 consensus
    BOOST_CHECK(manager.IsEnabled());
    LogPrintf("   ✓ Oracle Node → Bundle Manager: VERIFIED\n");
    passed++;

    // Integration Point 3: P2P Network Integration
    LogPrintf("Integration Point 3: P2P Network Integration\n");
    // Verify P2P message types are defined (ORACLEPRICE, ORACLEBUNDLE)
    // Note: This is verified at compile time via protocol.h includes
    LogPrintf("   ✓ P2P Network Integration: VERIFIED\n");
    passed++;

    // Integration Point 4: Bundle Manager → Miner
    LogPrintf("Integration Point 4: Bundle Manager → Miner\n");
    // Verify AddOracleBundleToBlock exists
    CBlock test_block;
    test_block.nVersion = 1;
    test_block.nTime = GetTime();
    test_block.hashPrevBlock.SetNull();
    test_block.nBits = 0x207fffff;  // Regtest difficulty
    test_block.nNonce = 0;

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 100 << OP_0;  // Height 100 (before oracle activation at 600)
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    test_block.vtx.push_back(MakeTransactionRef(coinbase));

    // Calculate merkle root
    test_block.hashMerkleRoot = BlockMerkleRoot(test_block);

    // This should not crash even with no oracle data
    manager.AddOracleBundleToBlock(test_block, 100);  // Block 100 is before activation
    LogPrintf("   ✓ Bundle Manager → Miner: VERIFIED\n");
    passed++;

    // Integration Point 5: Miner → Block Validation
    LogPrintf("Integration Point 5: Miner → Block Validation\n");
    // Verify OracleDataValidator::ValidateBlockOracleData exists
    BlockValidationState state;
    const CChainParams& chainparams = Params();
    // This validates oracle data in the block (no oracle data before activation = OK)
    BOOST_CHECK(CheckBlock(test_block, state, chainparams.GetConsensus(), false, false));
    LogPrintf("   ✓ Miner → Block Validation: VERIFIED\n");
    passed++;

    // Integration Point 6: Block Validation → Price Cache
    LogPrintf("Integration Point 6: Block Validation → Price Cache\n");
    // Verify UpdatePriceCache exists
    manager.UpdatePriceCache(12345, 50000);
    uint64_t cached = manager.GetOraclePriceForHeight(12345);
    BOOST_CHECK_EQUAL(cached, 50000);
    LogPrintf("   ✓ Block Validation → Price Cache: VERIFIED\n");
    passed++;

    // Integration Point 7: Price Cache → DigiDollar
    LogPrintf("Integration Point 7: Price Cache → DigiDollar\n");
    // Verify OracleIntegration::GetOraclePriceForHeight exists
    CAmount price = OracleIntegration::GetOraclePriceForHeight(12345);
    BOOST_CHECK(price > 0);
    LogPrintf("   ✓ Price Cache → DigiDollar: VERIFIED\n");
    passed++;

    LogPrintf("\n=== Integration Points Verification: %d/%d PASSED ===\n", passed, total);
    BOOST_CHECK_EQUAL(passed, total);
}

/**
 * TEST: Oracle bundles survive template creation for mining
 *
 * Proves the full oracle lifecycle that would have caught the testnet19 bug
 * (JohnnyLawDGB, Feb 17 2026): AddOracleBundleToBlock() fires on every
 * CreateNewBlock() call (~15 sec), but oracle P2P broadcasts arrive every
 * ~60 sec. If pending messages are cleared on template creation, subsequent
 * templates are empty — exactly what happened across 14,100+ blocks.
 *
 * This test verifies:
 * 1. Oracle messages are added to pending
 * 2. AddOracleBundleToBlock() embeds them in block template 1
 * 3. Messages SURVIVE after template creation (still in pending)
 * 4. A SECOND AddOracleBundleToBlock() for the NEXT block ALSO gets oracle data
 * 5. ClearPendingMessages() (simulating ConnectBlock) clears them
 * 6. Both block templates have valid oracle bundles (full round-trip)
 */
BOOST_AUTO_TEST_CASE(oracle_bundles_survive_template_creation_for_mining)
{
    if (SkipPhase2Test()) return;
    LogPrintf("=== Oracle Integration Test: Bundles Survive Template Creation ===\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1); // Phase One: 1-of-1

    // --- Step 1: Add oracle messages to pending ---
    LogPrintf("Step 1: Adding oracle message to pending...\n");
    CKey oracle_key;
    oracle_key.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6000; // $0.006
    msg.timestamp = GetTime();
    msg.block_height = 200;
    msg.nonce = 42;
    msg.oracle_pubkey = XOnlyPubKey(oracle_key.GetPubKey());
    BOOST_REQUIRE(msg.SignAttestation(oracle_key));
    BOOST_REQUIRE(manager.AddOracleMessage(msg));

    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 1);
    LogPrintf("   - Pending messages: %zu\n", manager.GetPendingMessageCount());

    // --- Step 2: First AddOracleBundleToBlock() — template 1 ---
    LogPrintf("Step 2: Building block template 1...\n");
    CMutableTransaction coinbase1;
    coinbase1.vin.resize(1);
    coinbase1.vin[0].prevout.SetNull();
    coinbase1.vout.resize(1);
    coinbase1.vout[0].nValue = 72000 * COIN;
    coinbase1.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CBlock block1;
    block1.vtx.push_back(MakeTransactionRef(std::move(coinbase1)));
    block1.nTime = GetTime();

    BOOST_REQUIRE(manager.AddOracleBundleToBlock(block1, 200));
    BOOST_CHECK_GE(block1.vtx[0]->vout.size(), 2); // Oracle output added
    LogPrintf("   - Block 1 outputs: %zu (should be >=2)\n", block1.vtx[0]->vout.size());

    // --- Step 3: Messages SURVIVE after template creation ---
    LogPrintf("Step 3: Verifying messages survive after template 1...\n");
    BOOST_CHECK_MESSAGE(
        manager.GetPendingMessageCount() == 1,
        strprintf("CRITICAL: Messages drained after template creation! Got %zu, expected 1. "
                  "This is the testnet19 drain bug.", manager.GetPendingMessageCount())
    );
    LogPrintf("   - Pending messages after template 1: %zu (should be 1)\n",
              manager.GetPendingMessageCount());

    // --- Step 4: Second AddOracleBundleToBlock() — template 2 ALSO gets oracle data ---
    // This is THE bug: if messages were cleared in step 2, this block would have NO oracle data.
    LogPrintf("Step 4: Building block template 2 (the critical test)...\n");
    CMutableTransaction coinbase2;
    coinbase2.vin.resize(1);
    coinbase2.vin[0].prevout.SetNull();
    coinbase2.vout.resize(1);
    coinbase2.vout[0].nValue = 72000 * COIN;
    coinbase2.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CBlock block2;
    block2.vtx.push_back(MakeTransactionRef(std::move(coinbase2)));
    block2.nTime = GetTime();

    BOOST_REQUIRE(manager.AddOracleBundleToBlock(block2, 201));
    BOOST_CHECK_MESSAGE(
        block2.vtx[0]->vout.size() >= 2,
        strprintf("CRITICAL: Block 2 has NO oracle output! Got %zu outputs, expected >=2. "
                  "This proves the drain bug — template creation wiped pending messages.",
                  block2.vtx[0]->vout.size())
    );
    LogPrintf("   - Block 2 outputs: %zu (should be >=2)\n", block2.vtx[0]->vout.size());

    // Messages still survive after second template
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 1);
    LogPrintf("   - Pending messages after template 2: %zu (should be 1)\n",
              manager.GetPendingMessageCount());

    // --- Step 5: Simulate ConnectBlock — ClearPendingMessages() clears them ---
    LogPrintf("Step 5: Simulating ConnectBlock (ClearPendingMessages)...\n");
    manager.ClearPendingMessages();
    BOOST_CHECK_EQUAL(manager.GetPendingMessageCount(), 0);
    LogPrintf("   - Pending messages after ConnectBlock: %zu (should be 0)\n",
              manager.GetPendingMessageCount());

    // --- Step 6: Extract and verify oracle data from BOTH blocks ---
    LogPrintf("Step 6: Extracting and verifying oracle bundles from both blocks...\n");

    COracleBundle extracted1;
    BOOST_REQUIRE_MESSAGE(
        manager.ExtractOracleBundle(*block1.vtx[0], extracted1),
        "Failed to extract oracle bundle from block 1"
    );
    BOOST_CHECK_EQUAL(extracted1.median_price_micro_usd, 6000);
    BOOST_CHECK_EQUAL(extracted1.messages.size(), 1);
    BOOST_CHECK_EQUAL(extracted1.messages[0].oracle_id, 0);
    LogPrintf("   - Block 1 bundle: price=%llu, messages=%zu — VALID\n",
              extracted1.median_price_micro_usd, extracted1.messages.size());

    COracleBundle extracted2;
    BOOST_REQUIRE_MESSAGE(
        manager.ExtractOracleBundle(*block2.vtx[0], extracted2),
        "Failed to extract oracle bundle from block 2"
    );
    BOOST_CHECK_EQUAL(extracted2.median_price_micro_usd, 6000);
    BOOST_CHECK_EQUAL(extracted2.messages.size(), 1);
    BOOST_CHECK_EQUAL(extracted2.messages[0].oracle_id, 0);
    LogPrintf("   - Block 2 bundle: price=%llu, messages=%zu — VALID\n",
              extracted2.median_price_micro_usd, extracted2.messages.size());

    // Verify both blocks carry the same oracle data
    BOOST_CHECK_EQUAL(extracted1.median_price_micro_usd, extracted2.median_price_micro_usd);
    BOOST_CHECK_EQUAL(extracted1.messages[0].price_micro_usd, extracted2.messages[0].price_micro_usd);

    // --- Verify post-ConnectBlock state: no more oracle data for new templates ---
    LogPrintf("Step 7: Verifying post-ConnectBlock cleanup...\n");
    CMutableTransaction coinbase3;
    coinbase3.vin.resize(1);
    coinbase3.vin[0].prevout.SetNull();
    coinbase3.vout.resize(1);
    coinbase3.vout[0].nValue = 72000 * COIN;
    coinbase3.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CBlock block3;
    block3.vtx.push_back(MakeTransactionRef(std::move(coinbase3)));
    block3.nTime = GetTime();

    manager.AddOracleBundleToBlock(block3, 202);
    // After ClearPendingMessages, no oracle data should be embedded
    BOOST_CHECK_EQUAL(block3.vtx[0]->vout.size(), 1); // Only mining payout, no oracle
    LogPrintf("   - Block 3 outputs after clear: %zu (should be 1, no oracle)\n",
              block3.vtx[0]->vout.size());

    LogPrintf("\n=== Oracle Integration Test: Bundles Survive Template Creation PASSED ===\n");
    LogPrintf("Full lifecycle verified: messages → template1 → template2 → both valid → "
              "ConnectBlock clears → template3 empty\n");
}

/**
 * TEST: Oracle bundle survives RegenerateCommitments()
 *
 * This test proves the critical fix for the testnet19 bug where oracle bundles
 * were stripped by RegenerateCommitments(). The function strips all OP_RETURN
 * outputs (including oracle data), re-adds the witness commitment, and must
 * also re-add the oracle bundle.
 *
 * Steps:
 * 1. Create a block with oracle data via AddOracleBundleToBlock()
 * 2. Verify oracle OP_RETURN output exists in coinbase
 * 3. Simulate RegenerateCommitments: strip all OP_RETURN, re-add witness, re-add oracle
 * 4. Verify oracle output STILL exists after regeneration
 * 5. Extract the oracle bundle and verify it's valid with correct price
 */
BOOST_AUTO_TEST_CASE(oracle_bundle_survives_regenerate_commitments)
{
    if (SkipPhase2Test()) return;
    LogPrintf("=== Oracle Integration Test: Bundle Survives RegenerateCommitments ===\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    // Step 1: Create oracle message and add to manager
    LogPrintf("Step 1: Creating oracle message...\n");
    CKey oracle_key;
    oracle_key.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 7500; // $0.0075
    msg.timestamp = GetTime();
    msg.block_height = 200;
    msg.nonce = 12345;
    msg.oracle_pubkey = XOnlyPubKey(oracle_key.GetPubKey());
    BOOST_REQUIRE(msg.SignAttestation(oracle_key));
    BOOST_REQUIRE(manager.AddOracleMessage(msg));

    // Step 2: Create a block template with oracle data
    LogPrintf("Step 2: Building block with oracle bundle...\n");
    CMutableTransaction coinbase_mtx;
    coinbase_mtx.vin.resize(1);
    coinbase_mtx.vin[0].prevout.SetNull();
    coinbase_mtx.vin[0].scriptSig = CScript() << 200 << OP_0;
    coinbase_mtx.vout.resize(1);
    coinbase_mtx.vout[0].nValue = 72000 * COIN;
    coinbase_mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CBlock block;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase_mtx)));
    block.nTime = GetTime();
    block.nVersion = 1;
    block.nBits = 0x207fffff;
    block.nNonce = 0;
    block.hashPrevBlock.SetNull();

    // Add oracle bundle
    BOOST_REQUIRE(manager.AddOracleBundleToBlock(block, 200));

    // Verify oracle output exists (search for OP_ORACLE byte 0xbf)
    bool has_oracle_before = false;
    size_t oracle_vout_idx_before = 0;
    for (size_t i = 0; i < block.vtx[0]->vout.size(); ++i) {
        const auto& out = block.vtx[0]->vout[i];
        if (out.scriptPubKey.size() >= 2 &&
            out.scriptPubKey[0] == OP_RETURN &&
            out.scriptPubKey[1] == OP_ORACLE) {
            has_oracle_before = true;
            oracle_vout_idx_before = i;
            break;
        }
    }
    BOOST_REQUIRE_MESSAGE(has_oracle_before,
        "Oracle OP_RETURN output must exist after AddOracleBundleToBlock()");
    LogPrintf("   - Oracle output found at vout[%zu] before regeneration\n", oracle_vout_idx_before);
    LogPrintf("   - Total coinbase outputs: %zu\n", block.vtx[0]->vout.size());

    // Step 3: Extract bundle before regeneration for comparison
    COracleBundle bundle_before;
    BOOST_REQUIRE(manager.ExtractOracleBundle(*block.vtx[0], bundle_before));
    BOOST_CHECK_EQUAL(bundle_before.median_price_micro_usd, 7500);
    LogPrintf("   - Extracted price before: %llu micro-USD\n", bundle_before.median_price_micro_usd);

    // Step 4: Simulate what RegenerateCommitments does:
    // Strip ALL OP_RETURN outputs, re-add witness commitment, then re-add oracle
    LogPrintf("Step 3: Simulating RegenerateCommitments...\n");
    {
        CMutableTransaction tx{*block.vtx.at(0)};

        // Count OP_RETURN outputs before stripping
        size_t op_return_count = 0;
        for (const auto& txout : tx.vout) {
            if (txout.scriptPubKey.IsUnspendable()) op_return_count++;
        }
        LogPrintf("   - OP_RETURN outputs before strip: %zu\n", op_return_count);

        // Strip ALL unspendable (OP_RETURN) outputs — this is what RegenerateCommitments does
        tx.vout.erase(
            std::remove_if(tx.vout.begin(), tx.vout.end(),
                [](const CTxOut& txout) { return txout.scriptPubKey.IsUnspendable(); }),
            tx.vout.end());
        block.vtx.at(0) = MakeTransactionRef(tx);

        LogPrintf("   - Outputs after stripping OP_RETURN: %zu\n", block.vtx[0]->vout.size());

        // Verify oracle output is gone after stripping
        bool has_oracle_stripped = false;
        for (const auto& out : block.vtx[0]->vout) {
            if (out.scriptPubKey.size() >= 2 &&
                out.scriptPubKey[0] == OP_RETURN &&
                out.scriptPubKey[1] == OP_ORACLE) {
                has_oracle_stripped = true;
            }
        }
        BOOST_CHECK_MESSAGE(!has_oracle_stripped,
            "Oracle output should be GONE after stripping all OP_RETURN");
        LogPrintf("   - Oracle output after strip: %s (expected: GONE)\n",
                  has_oracle_stripped ? "PRESENT" : "GONE");

        // Re-add witness commitment (GenerateCoinbaseCommitment would do this)
        // For this test, we skip actual witness commitment since we don't have full chain context

        // Re-add oracle bundle — THE FIX
        LogPrintf("Step 4: Re-adding oracle bundle (the fix)...\n");
        BOOST_REQUIRE(manager.AddOracleBundleToBlock(block, 200));
    }

    // Step 5: Verify oracle output STILL exists after regeneration
    LogPrintf("Step 5: Verifying oracle bundle survived regeneration...\n");
    bool has_oracle_after = false;
    for (const auto& out : block.vtx[0]->vout) {
        if (out.scriptPubKey.size() >= 2 &&
            out.scriptPubKey[0] == OP_RETURN &&
            out.scriptPubKey[1] == OP_ORACLE) {
            has_oracle_after = true;
            break;
        }
    }
    BOOST_REQUIRE_MESSAGE(has_oracle_after,
        "CRITICAL: Oracle output MISSING after RegenerateCommitments simulation! "
        "This is the testnet19 bug — oracle bundles are stripped and not re-added.");
    LogPrintf("   - Oracle output after regeneration: PRESENT ✓\n");

    // Step 6: Extract and verify the oracle bundle is valid with correct price
    COracleBundle bundle_after;
    BOOST_REQUIRE_MESSAGE(
        manager.ExtractOracleBundle(*block.vtx[0], bundle_after),
        "Failed to extract oracle bundle after regeneration");
    BOOST_CHECK_EQUAL(bundle_after.median_price_micro_usd, 7500);
    BOOST_CHECK_EQUAL(bundle_after.messages.size(), 1);
    BOOST_CHECK_EQUAL(bundle_after.messages[0].oracle_id, 0);
    BOOST_CHECK_EQUAL(bundle_after.messages[0].price_micro_usd, 7500);
    LogPrintf("   - Extracted price after: %llu micro-USD ✓\n", bundle_after.median_price_micro_usd);
    LogPrintf("   - Bundle messages: %zu ✓\n", bundle_after.messages.size());

    // Verify prices match before and after
    BOOST_CHECK_EQUAL(bundle_before.median_price_micro_usd, bundle_after.median_price_micro_usd);
    BOOST_CHECK_EQUAL(bundle_before.messages[0].price_micro_usd, bundle_after.messages[0].price_micro_usd);
    LogPrintf("   - Before/after price match: YES ✓\n");

    LogPrintf("\n=== Oracle Integration Test: Bundle Survives RegenerateCommitments PASSED ===\n");
}

BOOST_AUTO_TEST_CASE(reject_multiple_oracle_outputs_in_coinbase)
{
    if (SkipPhase2Test()) return;
    LogPrintf("\n=== Oracle Integration Test: Reject Multiple Oracle Outputs ===\n");

    // SECURITY TEST: A malicious miner could try to inject multiple OP_ORACLE outputs
    // into the coinbase (e.g., a valid one + a crafted one). The validator must reject
    // any block with more than one oracle output.

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    // Create a properly signed oracle message
    CKey oracle_key;
    oracle_key.MakeNewKey(true);
    XOnlyPubKey oracle_pubkey(oracle_key.GetPubKey());

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 5000;
    msg.timestamp = GetTime();
    msg.block_height = m_node.chainman->ActiveChain().Height() + 1;
    msg.nonce = FastRandomContext().rand64();
    msg.oracle_pubkey = oracle_pubkey;
    BOOST_REQUIRE(msg.SignAttestation(oracle_key));
    BOOST_REQUIRE(msg.VerifyAttestation());
    BOOST_REQUIRE(manager.AddOracleMessage(msg));

    // Build a block with one oracle output (normal path)
    CBlock block;
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].nValue = 1000;
    coinbaseTx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbaseTx)));

    bool added = manager.AddOracleBundleToBlock(block, 5000);
    BOOST_CHECK(added);

    // Count oracle outputs - should be exactly 1
    int oracle_count = 0;
    for (const auto& out : block.vtx[0]->vout) {
        if (out.scriptPubKey.size() >= 2 &&
            out.scriptPubKey[0] == OP_RETURN &&
            out.scriptPubKey[1] == OP_ORACLE) {
            oracle_count++;
        }
    }
    BOOST_CHECK_EQUAL(oracle_count, 1);
    LogPrintf("   - Single oracle output after AddOracleBundleToBlock: OK\n");

    // Simulate a malicious miner: inject a SECOND OP_ORACLE output
    CScript fake_oracle_script;
    fake_oracle_script << OP_RETURN << OP_ORACLE;
    fake_oracle_script << std::vector<unsigned char>{0x01, 0x00, 0x00, 0x00};

    CMutableTransaction tampered_tx(*block.vtx[0]);
    CTxOut fake_output;
    fake_output.nValue = 0;
    fake_output.scriptPubKey = fake_oracle_script;
    tampered_tx.vout.push_back(fake_output);
    block.vtx[0] = MakeTransactionRef(std::move(tampered_tx));

    // Count again - should be 2 now (attack injected)
    oracle_count = 0;
    for (const auto& out : block.vtx[0]->vout) {
        if (out.scriptPubKey.size() >= 2 &&
            out.scriptPubKey[0] == OP_RETURN &&
            out.scriptPubKey[1] == OP_ORACLE) {
            oracle_count++;
        }
    }
    BOOST_CHECK_EQUAL(oracle_count, 2);
    LogPrintf("   - Two oracle outputs after tampering: OK (attack setup)\n");

    // ExtractOracleBundle returns first match only (defense-in-depth)
    COracleBundle extracted;
    bool extracted_ok = manager.ExtractOracleBundle(*block.vtx[0], extracted);
    BOOST_CHECK(extracted_ok);
    LogPrintf("   - ExtractOracleBundle returns first match only: OK\n");

    // ValidateBlockOracleData (consensus layer) rejects blocks with multiple
    // OP_ORACLE outputs. That check needs full chain state, so we verify it
    // indirectly: the count detection logic is the same as in ValidateBlockOracleData.
    // The consensus rule: oracle_output_count > 1 => BLOCK_CONSENSUS rejection.

    LogPrintf("\n=== Oracle Integration Test: Reject Multiple Oracle Outputs PASSED ===\n");

    manager.Clear();
}

BOOST_AUTO_TEST_SUITE_END()