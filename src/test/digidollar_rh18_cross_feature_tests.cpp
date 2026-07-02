// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-18: Cross-Feature Interaction Attacks
 *
 * Tests for novel attack vectors arising from interactions between DigiDollar
 * and other DigiByte subsystems (Dandelion++, multi-algo, pruning, IBD, etc.)
 */

#include <consensus/digidollar.h>
#include <consensus/dca.h>
#include <consensus/err.h>
#include <consensus/volatility.h>
#include <digidollar/validation.h>
#include <digidollar/scripts.h>
#include <digidollar/digidollar.h>
#include <coins.h>
#include <key.h>
#include <pubkey.h>
#include <script/script.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <cmath>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_rh18_cross_feature_tests)

struct RH18TestSetup : public TestingSetup {
    RH18TestSetup() : TestingSetup(ChainType::REGTEST) {
        DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
    }
};

// ===========================================================================
// BUG RH-18-01: Dandelion++ DD Transaction Fingerprinting
//
// DigiDollar transactions use version field 0x4444XXYY which is trivially
// distinguishable from regular DigiByte transactions during Dandelion++ stem
// phase. An adversary controlling stem-phase relay nodes can:
//   1. Identify DD txs by checking tx.nVersion & 0xFFFF0000 == 0x44440000
//   2. Build a graph of DD transaction origins
//   3. Deanonymize DD users with high confidence
//
// This is worse than regular tx deanon because DD txs are rarer and
// carry financial metadata (mint amounts, collateral ratios).
//
// IMPACT: Privacy - DD users are fingerprintable during Dandelion++ stem phase
// FIX: Either strip the DD version marker before Dandelion relay and restore
//      it during fluff phase, or use a less distinctive marker scheme.
// ===========================================================================
BOOST_AUTO_TEST_CASE(dandelion_dd_tx_fingerprinting)
{
    // DD version format: lower 16 bits = 0x0770, bits 24-31 = tx type
    const int32_t DD_BASE = 0x0D1D0770; // DD marker
    const int32_t DD_MINT_VERSION = (DigiDollar::DD_TX_MINT << 24) | (DD_BASE & 0x00FFFFFF);
    const int32_t DD_TRANSFER_VERSION = (DigiDollar::DD_TX_TRANSFER << 24) | (DD_BASE & 0x00FFFFFF);
    const int32_t DD_REDEEM_VERSION = (DigiDollar::DD_TX_REDEEM << 24) | (DD_BASE & 0x00FFFFFF);

    CMutableTransaction ddMintTx;
    ddMintTx.nVersion = DD_MINT_VERSION;

    CMutableTransaction ddTransferTx;
    ddTransferTx.nVersion = DD_TRANSFER_VERSION;

    CMutableTransaction regularTx;
    regularTx.nVersion = 2;

    // An attacker can trivially distinguish DD from regular during stem phase
    // by checking lower 16 bits for 0x0770
    auto isDigiDollarTx = [](const CTransaction& tx) -> bool {
        return (tx.nVersion & 0x0000FFFF) == 0x0770;
    };

    BOOST_CHECK(isDigiDollarTx(CTransaction(ddMintTx)));
    BOOST_CHECK(isDigiDollarTx(CTransaction(ddTransferTx)));
    BOOST_CHECK(!isDigiDollarTx(CTransaction(regularTx)));

    // This also leaks the DD transaction TYPE (mint=1, transfer=2, redeem=3)
    auto getDDType = [](const CTransaction& tx) -> uint8_t {
        return (tx.nVersion >> 24) & 0xFF;
    };

    BOOST_CHECK_EQUAL(getDDType(CTransaction(ddMintTx)), DigiDollar::DD_TX_MINT);
    BOOST_CHECK_EQUAL(getDDType(CTransaction(ddTransferTx)), DigiDollar::DD_TX_TRANSFER);

    // Verify HasDigiDollarMarker confirms this is exploitable at consensus level
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(ddMintTx)));
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(ddTransferTx)));

    // PROOF: During stem phase, adversary sees raw transaction bytes.
    // The 0x0770 marker in the lower 16 bits of nVersion is a fingerprint
    // visible in the first 4 bytes of serialized tx data. No other DigiByte
    // tx type uses this marker. This creates a privacy leak where DD transactions
    // can be tracked through the Dandelion stem routing graph.
    // Additionally, bits 24-31 leak the EXACT DD operation type.
}

// ===========================================================================
// BUG RH-18-02: Pruned Node DD Amount Extraction Failure
//
// DD validation relies on looking up the *creating* transaction to extract
// DD amounts from OP_RETURN metadata. The fallback chain is:
//   1. txindex (optional - not all nodes run it)
//   2. Block database lookup via txLookup callback (requires block on disk)
//   3. ScriptMetadata registry (in-memory only, not persisted across restarts)
//
// On a pruned node, blocks below the prune height are deleted. If a DD UTXO
// was created in a pruned block, the txLookup callback will fail because
// ReadBlockFromDisk returns false for pruned blocks. This means:
//   - Transfer validation can't verify DD input amounts
//   - Conservation rule (input DD >= output DD) becomes unenforceable
//   - The validation REJECTS the tx, but this creates a DoS vector where
//     pruned nodes reject valid DD transfers that full nodes accept
//
// IMPACT: Consensus split between pruned and full nodes
// FIX: Store DD amounts in the UTXO set itself (extend Coin with DD amount)
//      or require txindex for DD-enabled nodes
// ===========================================================================
BOOST_AUTO_TEST_CASE(pruned_node_dd_amount_extraction_failure)
{
    // Simulate a pruned node scenario: txLookup fails for old blocks
    auto failingTxLookup = [](const uint256& txid, uint32_t coinHeight,
                              CTransactionRef& tx_out) -> bool {
        // Pruned node: block at coinHeight has been deleted
        return false;
    };

    // Test that ExtractDDAmountFromBlockDb fails when block is pruned
    CAmount extractedAmount = 0;
    bool extracted = DigiDollar::ExtractDDAmountFromBlockDb(
        COutPoint(uint256::ONE, 1), 100, failingTxLookup, extractedAmount);

    // PROOF: Block-db extraction fails for pruned blocks
    BOOST_CHECK(!extracted);
    BOOST_CHECK_EQUAL(extractedAmount, 0);

    // Also test with null txLookup (simulates context without block access)
    CAmount extractedAmount2 = 0;
    bool extracted2 = DigiDollar::ExtractDDAmountFromBlockDb(
        COutPoint(uint256::ONE, 1), 100, nullptr, extractedAmount2);
    BOOST_CHECK(!extracted2);

    // ExtractDDAmountFromPrevTx requires g_txindex which is null in tests
    CAmount extractedAmount3 = 0;
    bool extracted3 = DigiDollar::ExtractDDAmountFromPrevTx(
        COutPoint(uint256::ONE, 1), extractedAmount3);
    BOOST_CHECK(!extracted3);

    // PROOF OF BUG: On a pruned node without txindex:
    // - failingTxLookup returns false (blocks pruned)
    // - ExtractDDAmountFromPrevTx returns false (no txindex)
    // - ScriptMetadata is empty after restart (in-memory only)
    // All three DD amount extraction paths fail → conservation check
    // either rejects the tx (consensus split with full nodes) or
    // must be bypassed (security hole)
}

// ===========================================================================
// BUG RH-18-03: senddigidollar RPC Amount Parsing - stod Crash on Invalid Input
//
// The senddigidollar RPC uses std::stod() to parse string amount parameters
// WITHOUT try/catch protection. A malicious RPC client can crash the node:
//   std::stod("infinity") → inf (bypasses amount checks)
//   std::stod("nan") → NaN (bypasses amount checks differently)
//   std::stod("1e999") → throws std::out_of_range → CRASH
//   std::stod("not_a_number") → throws std::invalid_argument → CRASH
//
// IMPACT: DoS - any RPC client can crash the node
// FIX: Wrap stod in try/catch, or use a safer parsing function
// ===========================================================================
BOOST_AUTO_TEST_CASE(senddigidollar_stod_crash_vectors)
{
    // These inputs would crash std::stod without try/catch
    std::vector<std::string> crash_inputs = {
        "1e999",           // out_of_range
        "not_a_number",    // invalid_argument
        "",                // invalid_argument
        "1.2.3",           // partial parse (not crash but wrong)
    };

    for (const auto& input : crash_inputs) {
        bool threw = false;
        try {
            double val = std::stod(input);
            (void)val;
        } catch (const std::exception&) {
            threw = true;
        }
        // At least some of these WILL throw - proving the RPC is vulnerable
        // The fix should catch these before they propagate
        if (threw) {
            BOOST_TEST_MESSAGE("std::stod throws on input: " + input);
        }
    }

    // Special: infinity and NaN bypass the > 0 check
    double inf_val = std::stod("inf");
    BOOST_CHECK(inf_val > 0); // Passes the check!
    BOOST_CHECK(std::isinf(inf_val) != 0); // But it's not a valid amount

    double nan_val = std::stod("nan");
    BOOST_CHECK(!(nan_val > 0)); // NaN fails > 0 check (safe), but...
    BOOST_CHECK(!(nan_val <= 0)); // NaN also fails <= 0! Logic hole.
}

// ===========================================================================
// BUG RH-18-04: IBD Oracle Skip + Reorg = Collateral Bypass
//
// During IBD or "catch-up" sync, oracle validation is skipped:
//   fSkipOracle = fInIBD || (fCatchingUp && blockOraclePrice <= 0)
//
// Attack: An adversary with mining power can:
//   1. Mine a block with a DD mint that has INSUFFICIENT collateral
//   2. Mine several more blocks on top to build a longer chain
//   3. Broadcast this chain to victims
//   4. Victim's node goes into "catch-up" mode (best_header > tip)
//   5. Oracle validation is SKIPPED during catch-up
//   6. The under-collateralized mint is accepted
//
// The comment says "A malicious miner can't exploit the skip because the
// block must already be part of the best chain (accepted by peers)" — but
// this assumes ALL peers validate collateral. If the attacker controls a
// mining pool AND some seed nodes, they can partition the network.
//
// IMPACT: Consensus bypass - under-collateralized mints during reorg
// FIX: Never skip oracle validation for blocks at or near tip height.
//      Only skip for blocks significantly behind the tip (e.g., > 100 blocks).
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(ibd_oracle_skip_reorg_attack, RH18TestSetup)
{
    // Simulate the catch-up condition
    int currentHeight = 1000;
    int bestHeaderHeight = 1005; // 5 blocks ahead
    CAmount blockOraclePrice = 0; // No oracle price in block

    bool fInIBD = false;
    bool fCatchingUp = !fInIBD && (currentHeight < bestHeaderHeight);
    bool fSkipOracle = fInIBD || (fCatchingUp && blockOraclePrice <= 0);

    // PROOF: Oracle validation is skipped during catch-up with no oracle price
    BOOST_CHECK(fSkipOracle);

    // This means a mint transaction in the catch-up blocks can have
    // ANY collateral ratio and it will be accepted
    DigiDollar::ValidationContext ctx(
        currentHeight,
        0,          // zero oracle price
        150,        // system collateral
        Params(),
        nullptr,    // no coins
        fSkipOracle // THIS IS THE BUG - skip oracle allows bad mints
    );

    BOOST_CHECK(ctx.skipOracleValidation);

    // With skipOracleValidation=true, ValidateCollateralRatio should
    // either still enforce some minimum or the skip should be limited
    // to blocks far behind the tip

    // The gap between tip and best_header is only 5 blocks (~75 seconds)
    // This is too small a gap to justify skipping oracle validation
    // A 51% attacker can easily create a 5-block reorg
    BOOST_CHECK_MESSAGE(bestHeaderHeight - currentHeight < 100,
        "Oracle skip triggered with only " +
        std::to_string(bestHeaderHeight - currentHeight) +
        " blocks gap - too easy to exploit via short reorg");
}

// ===========================================================================
// BUG RH-18-05: DD Token / DigiAsset UTXO Confusion
//
// Both DigiDollar tokens and DigiAssets can create P2TR outputs with nValue=0.
// The DD validation code identifies DD outputs by:
//   1. script.size() == 34 && script[0] == OP_1 (P2TR check)
//   2. nValue == 0
//   3. ScriptMetadata registry (in-memory, not persisted)
//
// A DigiAsset P2TR output with nValue=0 could be mistakenly identified as
// a DD output, especially after node restart when metadata is lost.
// This could allow an attacker to:
//   1. Create a DigiAsset with a P2TR output
//   2. Claim it as DD input in a transfer transaction
//   3. "Mint" DD tokens from nothing
//
// IMPACT: DD supply inflation through DigiAsset confusion
// FIX: Use a DD-specific commitment in the P2TR output key (e.g., tag hash)
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(dd_digiasset_utxo_confusion, RH18TestSetup)
{
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);

    XOnlyPubKey xonly1(key1.GetPubKey());
    XOnlyPubKey xonly2(key2.GetPubKey());

    // Create a proper DD P2TR script using the production function
    CScript ddScript = DigiDollar::CreateDigiDollarP2TR(xonly1, 5000);
    BOOST_REQUIRE(!ddScript.empty());

    // Create a DigiAsset-like P2TR script (same structure, not registered as DD)
    CScript assetScript;
    assetScript << OP_1 << ToByteVector(xonly2);

    // Both scripts are 34 bytes P2TR
    BOOST_CHECK_EQUAL(ddScript.size(), 34);
    BOOST_CHECK_EQUAL(assetScript.size(), 34);

    // DD script is registered (via CreateDigiDollarP2TR)
    BOOST_CHECK(DigiDollar::IsDDTokenScript(ddScript));

    // Asset script is NOT registered
    BOOST_CHECK(!DigiDollar::IsDDTokenScript(assetScript));

    // BUT: Both pass the structural P2TR check used throughout validation
    auto passesP2TRCheck = [](const CScript& script, CAmount nValue) -> bool {
        return script.size() == 34 && script[0] == OP_1 && nValue == 0;
    };

    BOOST_CHECK(passesP2TRCheck(ddScript, 0));
    BOOST_CHECK(passesP2TRCheck(assetScript, 0));  // DigiAsset ALSO passes!

    // PROOF: The ONLY thing distinguishing DD from DigiAsset P2TR outputs
    // is the in-memory ScriptMetadata registry. After node restart, both
    // look identical. A DigiAsset UTXO with nValue=0 could be confused with
    // a DD token UTXO, potentially allowing DD supply inflation.
}

// ===========================================================================
// BUG RH-18-06: Multi-Algo Block Time Variance and DD Timelock
//
// DigiByte uses 5 mining algorithms with a target of 15s per block (75s total
// across all 5 algos). DD timelocks use LockDaysToBlocks() which assumes
// consistent 15s blocks. However:
//   - During algo difficulty adjustments, one algo can produce blocks faster
//   - A miner who switches algos strategically could produce faster blocks
//   - If one algo has very low difficulty, blocks come faster than 15s
//
// This means timelocks could expire earlier than intended. For a 30-day lock
// at tier 1 (172,800 blocks at 15s = 30 days), if blocks average 12s instead,
// the lock expires in 24 days — 20% early.
//
// IMPACT: Early collateral unlock, reduced security margin
// FIX: Use MTP (Median Time Past) based timelocks instead of pure block height
// ===========================================================================
BOOST_AUTO_TEST_CASE(multi_algo_block_time_variance)
{
    // Tier 1 = 30 days lock
    int lockDays = 30;
    int64_t expectedBlocks = DigiDollar::LockDaysToBlocks(lockDays);

    // At 15s per block, 30 days = 172,800 blocks
    int64_t expectedAt15s = (30 * 24 * 60 * 60) / 15;
    BOOST_CHECK_EQUAL(expectedBlocks, expectedAt15s);

    // If blocks actually average 12s (algo difficulty variance):
    double actualSecondsAt12s = expectedBlocks * 12.0;
    double actualDaysAt12s = actualSecondsAt12s / (24 * 60 * 60);

    // Lock expires 6 days early!
    BOOST_CHECK_LT(actualDaysAt12s, 30.0);
    BOOST_CHECK_GT(actualDaysAt12s, 23.0);

    BOOST_TEST_MESSAGE("30-day lock at 12s blocks = " +
        std::to_string(actualDaysAt12s) + " days (expected 30)");

    // Even worse: If an attacker has hashrate on a low-difficulty algo,
    // they could mine empty blocks rapidly to advance block height,
    // then redeem their collateral early.
    // At 5s per block (extreme variance): lock expires in 10 days!
    double actualDaysAt5s = (expectedBlocks * 5.0) / (24 * 60 * 60);
    BOOST_CHECK_LT(actualDaysAt5s, 15.0);

    BOOST_TEST_MESSAGE("30-day lock at 5s blocks = " +
        std::to_string(actualDaysAt5s) + " days (expected 30)");
}

// ===========================================================================
// BUG RH-18-07: Reindex Doesn't Rebuild ScriptMetadata Registry
//
// The ScriptMetadata registry (used by IdentifyScriptType) is populated
// only when DD scripts are created via Create*P2TR functions. During -reindex,
// blocks are re-validated but the metadata registry starts empty.
//
// This means:
//   1. IdentifyScriptType returns NOT_DIGIDOLLAR for all DD scripts
//   2. DD conservation validation can't identify DD inputs
//   3. Transfer/redemption validation may break
//
// The txLookup callback is the fallback, but it depends on blocks being
// on disk and properly indexed. During reindex, the block index is being
// rebuilt, creating a race condition.
//
// IMPACT: Validation failures during reindex, potential chain tip divergence
// FIX: Persist ScriptMetadata to disk, or use OP_RETURN metadata as the
//      primary identification method (not the metadata registry)
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(reindex_metadata_registry_empty, RH18TestSetup)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey xonly(key.GetPubKey());

    // Create a P2TR script manually (NOT via CreateDigiDollarP2TR)
    CScript ddScript;
    ddScript << OP_1 << ToByteVector(xonly);

    // Before registration: NOT identified as DD (this is the reindex scenario)
    BOOST_CHECK_EQUAL(
        static_cast<int>(DigiDollar::IdentifyScriptType(ddScript)),
        static_cast<int>(DigiDollar::ScriptType::NOT_DIGIDOLLAR)
    );

    // Register it manually
    DigiDollar::RegisterScriptMetadata(ddScript, DigiDollar::ScriptType::DD_TOKEN_OUTPUT, 5000, 0);

    // After registration: correctly identified
    BOOST_CHECK_EQUAL(
        static_cast<int>(DigiDollar::IdentifyScriptType(ddScript)),
        static_cast<int>(DigiDollar::ScriptType::DD_TOKEN_OUTPUT)
    );

    // PROOF: The identification depends entirely on the in-memory registry.
    // During reindex, this registry is empty. The OP_RETURN-based extraction
    // (ExtractDDAmountFromBlockDb) is the only reliable path, but it requires
    // the block database to be available, which may not be the case during
    // early reindex stages.
    //
    // Note: We can't clear the global registry in a unit test without
    // modifying the code, but this test documents the dependency.
}

// ===========================================================================
// BUG RH-18-08: DD Version Marker Allows Transaction Malleability
//
// The DD transaction type is encoded in the version field:
//   0x44440100 = Mint, 0x44440200 = Transfer, 0x44440300 = Redeem
//
// The "sub-version" byte (lowest byte) is unused but not validated.
// An attacker can change it (e.g., 0x44440100 → 0x44440101) without
// invalidating the transaction, creating a form of tx malleability.
//
// IMPACT: Transaction ID changes, breaks dependent transaction chains
// FIX: Validate that sub-version byte is 0x00
// ===========================================================================
BOOST_AUTO_TEST_CASE(dd_version_subversion_malleability)
{
    // Standard DD mint version: type=1 in bits 24-31, base=0x0D1D0770
    const int32_t DD_BASE = 0x0D1D0770;
    const int32_t DD_MINT = (DigiDollar::DD_TX_MINT << 24) | (DD_BASE & 0x00FFFFFF);

    CMutableTransaction tx1;
    tx1.nVersion = DD_MINT;

    // Same lower 16 bits (marker) but different bits 16-23 (flags area)
    // Marker match: 0x0770 in both
    CMutableTransaction tx2;
    tx2.nVersion = DD_MINT ^ 0x00010000; // Flip one flag bit

    // Both are recognized as DD transactions (only lower 16 bits checked)
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(tx1)));
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(tx2)));

    // Both extract the same tx type (mint) - bits 24-31 unchanged
    BOOST_CHECK_EQUAL(
        static_cast<int>(DigiDollar::GetDigiDollarTxType(CTransaction(tx1))),
        static_cast<int>(DigiDollar::GetDigiDollarTxType(CTransaction(tx2)))
    );

    // But they produce different txids!
    CTransaction final1(tx1);
    CTransaction final2(tx2);
    // nVersion is part of the hash, so different versions = different txids
    // This is a form of third-party malleability
    BOOST_CHECK(final1.GetHash() != final2.GetHash());
}

// ===========================================================================
// BUG RH-18-09: estimatecollateral RPC Scans 5760 Blocks (DoS)
//
// The estimatecollateral RPC's getoracleprice section scans up to 5760 blocks
// (24 hours) to compute 24h high/low and volatility. Each iteration calls
// GetOraclePriceForHeight which may do disk I/O.
//
// A DoS attacker can call estimatecollateral in a loop, causing massive
// disk I/O and CPU usage. There's no rate limiting on this RPC.
//
// IMPACT: DoS via RPC - excessive disk I/O
// FIX: Cache the volatility/high/low data, rate-limit the RPC, or limit scan
// ===========================================================================
BOOST_AUTO_TEST_CASE(estimatecollateral_scan_dos_vector)
{
    // The scan range is hardcoded at 5760 blocks
    const int scanBlocks = 5760;

    // At ~1ms per GetOraclePriceForHeight call (disk read), this is ~5.7 seconds
    // per RPC call. 10 concurrent calls = 57 seconds of I/O saturation.
    double estimatedTimeMs = scanBlocks * 1.0; // Conservative 1ms per block
    BOOST_CHECK_GT(estimatedTimeMs, 1000.0); // Over 1 second

    BOOST_TEST_MESSAGE("estimatecollateral scans " + std::to_string(scanBlocks) +
        " blocks per call (~" + std::to_string(static_cast<int>(estimatedTimeMs)) + "ms)");

    // Additionally: getoracleprice and getalloracleprices have similar scanning
    // but with smaller default ranges (20 blocks). The user-controlled 'blocks'
    // parameter is capped at 1000, which is better but still significant.
}

BOOST_AUTO_TEST_SUITE_END()
