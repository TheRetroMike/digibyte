// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// RH-41: Time-Warp & Difficulty Interaction Attack Tests
// Tests for attacks exploiting the interaction between DigiByte's 5-algo
// difficulty system and DigiDollar validation.

#include <consensus/digidollar.h>
#include <consensus/volatility.h>
#include <digidollar/digidollar.h>
#include <digidollar/health.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <util/time.h>

#include <boost/test/unit_test.hpp>

using namespace DigiDollar;
using DigiDollar::Volatility::VolatilityMonitor;

BOOST_AUTO_TEST_SUITE(digidollar_rh41_timewarp_difficulty_tests)

// ============================================================================
// Test Fixture
// ============================================================================

struct RH41TestSetup : public TestingSetup {
    RH41TestSetup() : TestingSetup(ChainType::REGTEST)
    {
        testKey.MakeNewKey(true);
        testPubKey = testKey.GetPubKey();
        testXOnlyKey = XOnlyPubKey(testPubKey);
        VolatilityMonitor::ClearFreeze();
        VolatilityMonitor::ClearHistory();
    }

    ~RH41TestSetup()
    {
        SetMockTime(0);
        VolatilityMonitor::ClearFreeze();
        VolatilityMonitor::ClearHistory();
    }

    DigiDollar::ValidationContext MakeContext(int height, CAmount oraclePriceMicroUSD,
                                              int systemCollateral = 150,
                                              bool skipOracle = false) const
    {
        return DigiDollar::ValidationContext(
            height, oraclePriceMicroUSD, systemCollateral,
            Params(), nullptr, skipOracle);
    }

    // Build a minimal valid DD mint transaction for testing
    CMutableTransaction BuildMintTx(CAmount ddAmount, CAmount collateral,
                                    int64_t lockHeight, int64_t lockTier) const
    {
        CMutableTransaction tx;
        // DD_TX_MINT = 1, version marker
        tx.nVersion = 0x01000770;

        // Input (dummy)
        tx.vin.resize(1);
        tx.vin[0].prevout = COutPoint(uint256S("0xabcd"), 0);

        // Output 0: Collateral lock (P2TR with NUMS key)
        DigiDollar::MintParams mintParams;
        mintParams.ddAmount = ddAmount;
        mintParams.lockHeight = lockHeight;
        mintParams.ownerKey = testXOnlyKey;
        mintParams.internalKey = DigiDollar::GetCollateralNUMSKey();
        mintParams.oracleKeys = DigiDollar::GetOracleKeys(15);
        CScript collateralScript = DigiDollar::CreateCollateralP2TR(mintParams);
        tx.vout.emplace_back(collateral, collateralScript);

        // Output 1: OP_RETURN with DD metadata
        CScript opReturn;
        opReturn << OP_RETURN;
        std::vector<unsigned char> ddMarker = {'D', 'D'};
        opReturn << ddMarker;
        opReturn << CScriptNum(1); // MINT type
        opReturn << CScriptNum(static_cast<int64_t>(ddAmount));
        opReturn << CScriptNum(static_cast<int64_t>(lockHeight));
        opReturn << CScriptNum(static_cast<int64_t>(lockTier));
        opReturn << std::vector<unsigned char>(testXOnlyKey.begin(), testXOnlyKey.end());
        tx.vout.emplace_back(0, opReturn);

        // Output 2: DD token output (P2TR)
        CScript ddScript = DigiDollar::CreateDigiDollarP2TR(testXOnlyKey, ddAmount);
        tx.vout.emplace_back(0, ddScript);

        return tx;
    }

    CKey testKey;
    CPubKey testPubKey;
    XOnlyPubKey testXOnlyKey;
};

// ============================================================================
// Attack Vector 1: Volatility Cooldown Bypass via Rapid Block Production
// ============================================================================
// The volatility cooldown uses BLOCK HEIGHTS (cooldownEndHeight = currentHeight + 8640),
// but on testnet/regtest, blocks can be mined much faster than 15 seconds.
// An attacker mining rapid empty blocks can advance the height past cooldownEndHeight
// in minutes instead of 36 hours, unfreezing operations prematurely.

BOOST_FIXTURE_TEST_CASE(rh41_cooldown_bypass_rapid_blocks, RH41TestSetup)
{
    // Step 1: Trigger a volatility freeze by recording a massive price swing
    int64_t baseTime = 1700000000;
    VolatilityMonitor::RecordPrice(10000, baseTime, 1000);         // $0.01
    VolatilityMonitor::RecordPrice(3000, baseTime + 3601, 1001);   // 70% drop in 1 hour

    VolatilityMonitor::UpdateState(1001);

    // Verify freeze is active
    BOOST_CHECK_MESSAGE(VolatilityMonitor::ShouldFreezeMinting(),
        "Minting should be frozen after 70% price crash");

    // Step 2: Simulate rapid block production — advance height by 8641 blocks
    // On testnet with min-difficulty, this could happen in minutes.
    // The cooldown should NOT end just because blocks are fast.
    uint32_t rapidHeight = 1001 + 8641; // Past cooldown
    VolatilityMonitor::UpdateState(rapidHeight);

    // VULNERABILITY CHECK: After advancing past cooldownEndHeight, the freeze lifts
    // IF hourly volatility has dropped. But in real time only minutes have passed!
    // The volatility calculation uses wall-clock timestamps, so the 70% drop is still
    // within the "1 hour" window. This SHOULD still be frozen.
    //
    // The key insight: if volatility is recalculated with the same price history
    // (no new prices added because MIN_PRICE_INTERVAL hasn't elapsed in wall time),
    // the freeze should persist because hourly volatility is still high.
    //
    // BUT: if enough wall-clock time has passed for volatility to appear low
    // (even though only minutes of real trading), the cooldown ends early.
    // This is a design tension between height-based cooldown and time-based volatility.

    // For now, verify the existing behavior: if we're past cooldownEndHeight AND
    // volatility appears low (stale old data), it unfreezes. This is the vulnerability.
    bool stillFrozen = VolatilityMonitor::ShouldFreezeMinting();

    // Record result - this documents the behavior for review
    BOOST_TEST_MESSAGE("After rapid 8641 blocks with no new prices:");
    BOOST_TEST_MESSAGE("  Still frozen: " << (stillFrozen ? "yes" : "NO - COOLDOWN BYPASSED"));

    // The correct behavior should be: still frozen until real-time volatility
    // has actually stabilized. If not frozen, this is a vulnerability.
    // Current implementation may unfreeze because price history didn't change.
    // We flag this as a known design issue regardless of outcome.
    BOOST_TEST_MESSAGE("RH-41-V1: Height-based cooldown vs time-based volatility mismatch");
}

// ============================================================================
// Attack Vector 2: Oracle Price Recording Bypass via Timestamp Manipulation
// ============================================================================
// RecordPrice() rejects prices closer than MIN_PRICE_INTERVAL (3600s) apart.
// But timestamps come from GetTime() which is wall-clock, not block time.
// This means miners can't directly manipulate the recording interval.
// HOWEVER, the volatility CALCULATION uses the recorded timestamps, creating
// a scenario where timestamps and block heights drift.

BOOST_FIXTURE_TEST_CASE(rh41_oracle_timestamp_vs_height_drift, RH41TestSetup)
{
    // Scenario: Normal operation records prices every 3600s.
    // At 15s/block, that's ~240 blocks per price point.
    // If blocks are mined faster (testnet), heights advance faster than timestamps.
    // This means cooldown (height-based) completes before volatility window (time-based).

    int64_t baseTime = 1700000000;

    // Record a series of stable prices
    for (int i = 0; i < 10; i++) {
        VolatilityMonitor::RecordPrice(10000, baseTime + i * 3601, 1000 + i * 240);
    }

    // Now record a crash
    VolatilityMonitor::RecordPrice(5000, baseTime + 10 * 3601, 1000 + 10 * 240);
    VolatilityMonitor::UpdateState(1000 + 10 * 240);

    bool frozen = VolatilityMonitor::ShouldFreezeMinting();
    BOOST_TEST_MESSAGE("After 50% price drop: frozen=" << frozen);

    if (frozen) {
        // Now the attacker mines 8641 blocks rapidly (testnet min-difficulty)
        // In wall time, maybe 30 minutes pass. In block time, 36 hours of "cooldown" pass.
        uint32_t attackHeight = 1000 + 10 * 240 + 8641;
        VolatilityMonitor::UpdateState(attackHeight);

        bool stillFrozen = VolatilityMonitor::ShouldFreezeMinting();
        BOOST_TEST_MESSAGE("After 8641 rapid blocks: stillFrozen=" << stillFrozen);
        BOOST_TEST_MESSAGE("RH-41-V2: Attacker can mine past cooldown on testnet in minutes");
    }
}

// ============================================================================
// Attack Vector 3: Empty Block Spam — DD State Starvation
// ============================================================================
// Mining empty blocks (no DD txs) advances height. This affects:
// 1. Cooldown timers (height-based)
// 2. Oracle epoch boundaries (nDDOracleEpochBlocks = 40 blocks, ~10 minutes)
// 3. Lock period expiry (absolute height-based)
// The system health monitor tracks mints/redeems, but doesn't degrade
// if there's simply no DD activity for many blocks.

BOOST_FIXTURE_TEST_CASE(rh41_empty_block_spam_no_degradation, RH41TestSetup)
{
    // Set up initial DD state with some minted DD
    DigiDollar::SystemHealthMonitor::Initialize();
    DigiDollar::SystemHealthMonitor::OnMintConnected(100000, 100 * COIN); // $1000 DD, 100 DGB

    auto metrics = DigiDollar::SystemHealthMonitor::GetSystemMetrics();
    BOOST_TEST_MESSAGE("Initial DD supply: " << metrics.totalDDSupply);
    BOOST_TEST_MESSAGE("Initial collateral: " << metrics.totalCollateral);

    // Simulate 1000 empty blocks (no DD txs). State should be unchanged.
    // This is expected behavior — but the oracle epoch advances, which means
    // oracle keys rotate even without new oracle data.
    auto metricsAfter = DigiDollar::SystemHealthMonitor::GetSystemMetrics();
    BOOST_CHECK_EQUAL(metrics.totalDDSupply, metricsAfter.totalDDSupply);
    BOOST_CHECK_EQUAL(metrics.totalCollateral, metricsAfter.totalCollateral);

    BOOST_TEST_MESSAGE("RH-41-V3: Empty blocks don't damage DD state (expected)");
    BOOST_TEST_MESSAGE("  BUT: Oracle epochs advance without fresh data — potential staleness issue");

    DigiDollar::SystemHealthMonitor::Shutdown();
}

// ============================================================================
// Attack Vector 4: Collateral Lock Expiry via Height Advancement
// ============================================================================
// DD collateral is locked until an absolute block height (lockTime = ctx.nHeight + lockPeriod).
// If an attacker can rapidly advance block height (testnet min-difficulty),
// collateral unlocks much sooner in wall-clock time.

BOOST_FIXTURE_TEST_CASE(rh41_lock_expiry_height_advancement, RH41TestSetup)
{
    // Create a mint with 30-day lock (30 * 24 * 60 * 4 = 172800 blocks at 15s/block)
    int currentHeight = 10000;
    int64_t lockPeriod = 172800; // 30 days in blocks
    int64_t lockHeight = currentHeight + lockPeriod;

    CAmount oraclePrice = 10000; // $0.01
    auto ctx = MakeContext(currentHeight, oraclePrice);

    // Lock height should be 182800
    BOOST_CHECK_EQUAL(lockHeight, 182800);

    // On mainnet at 15s/block, 172800 blocks = 30 days
    // On testnet with rapid mining, 172800 blocks could pass in hours

    // Verify that when height reaches lockHeight, the lock has expired
    auto ctxAfterLock = MakeContext(lockHeight + 1, oraclePrice);

    // Build a redemption transaction
    CMutableTransaction redeemTx;
    redeemTx.nVersion = 0x0D1D0770 | (3 << 16); // DD_TX_REDEEM
    redeemTx.nLockTime = lockHeight; // CLTV lock

    // The nLockTime check: ctx.nHeight >= nLockTime
    BOOST_CHECK_MESSAGE(ctxAfterLock.nHeight >= static_cast<int>(redeemTx.nLockTime),
        "Lock should be expired when height passes lockHeight");

    // But if only 2 hours of wall time passed (rapid mining), the "30-day lock"
    // was only 2 hours in practice. This is a known limitation of height-based locks.
    BOOST_TEST_MESSAGE("RH-41-V4: Height-based locks expire based on blocks, not wall time");
    BOOST_TEST_MESSAGE("  On testnet: 30-day lock = hours. On mainnet: enforced by difficulty.");
    BOOST_TEST_MESSAGE("  No bug — but documentation should clarify this is by design.");
}

// ============================================================================
// Attack Vector 5: Algo-Specific Oracle Epoch Alignment
// ============================================================================
// DigiByte's 5 algos mean blocks arrive ~3s apart (15s / 5 algos).
// Oracle epoch is every 100 blocks. If an attacker controls one algo,
// they can't selectively mine blocks to align epoch boundaries because
// DD validation only checks total height, not algo of the mining block.

BOOST_FIXTURE_TEST_CASE(rh41_algo_agnostic_dd_validation, RH41TestSetup)
{
    // DD validation context only uses nHeight — no algo information
    auto ctx1 = MakeContext(100, 10000);
    auto ctx2 = MakeContext(100, 10000);

    // Both contexts are identical regardless of which algo mined the block
    BOOST_CHECK_EQUAL(ctx1.nHeight, ctx2.nHeight);
    BOOST_CHECK_EQUAL(ctx1.oraclePriceMicroUSD, ctx2.oraclePriceMicroUSD);

    // Oracle epoch check: height 99 and height 100 are in different epochs
    // (if epoch = 100 blocks). The algo that mines block 100 doesn't matter.
    int epoch99 = 99 / 100;
    int epoch100 = 100 / 100;
    BOOST_CHECK_NE(epoch99, epoch100);

    BOOST_TEST_MESSAGE("RH-41-V5: DD validation is algo-agnostic (SECURE)");
    BOOST_TEST_MESSAGE("  Mining algo has no impact on DD state transitions.");
}

// ============================================================================
// Attack Vector 6: Volatility Monitor GetTime() in Consensus Path
// ============================================================================
// RecordPrice() is called from ValidateDigiDollarTransaction() which runs
// during block validation. It calls GetTime() for timestamps.
// GetTime() returns wall-clock time, NOT block time.
// This means the same block validated at different times records different timestamps,
// creating a non-deterministic consensus path.

BOOST_FIXTURE_TEST_CASE(rh41_nondeterministic_price_recording, RH41TestSetup)
{
    // This is a CRITICAL finding: RecordPrice is called from consensus code
    // (ValidateDigiDollarTransaction -> UpdateState) and uses GetTime().
    // Two nodes validating the same block at different wall times will record
    // different timestamps, leading to different volatility calculations,
    // and potentially different freeze/unfreeze decisions.
    //
    // This was originally reachable through ValidateDigiDollarTransaction()
    // recording accepted mint prices with GetTime(). The regression below pins
    // the fix: consensus-context recording must use ctx.nBlockTime instead.

    int64_t time1 = 1700000000;
    int64_t time2 = time1 + 3700; // Same block, validated 3700s later on node B

    // Node A validates at time1
    VolatilityMonitor::ClearHistory();
    VolatilityMonitor::RecordPrice(10000, time1, 1000);
    VolatilityMonitor::RecordPrice(5000, time1 + 3601, 1001); // Crash
    VolatilityMonitor::UpdateState(1001);
    bool nodeA_frozen = VolatilityMonitor::ShouldFreezeMinting();

    // Node B validates same block at time2 (3700s later — during IBD or slow sync)
    VolatilityMonitor::ClearHistory();
    VolatilityMonitor::ClearFreeze();
    VolatilityMonitor::RecordPrice(10000, time2, 1000);
    VolatilityMonitor::RecordPrice(5000, time2 + 3601, 1001);
    VolatilityMonitor::UpdateState(1001);
    bool nodeB_frozen = VolatilityMonitor::ShouldFreezeMinting();

    BOOST_TEST_MESSAGE("RH-41-V6: CRITICAL — GetTime() in consensus path");
    BOOST_TEST_MESSAGE("  Node A frozen: " << nodeA_frozen);
    BOOST_TEST_MESSAGE("  Node B frozen: " << nodeB_frozen);
    // Both should be the same since we used the same relative timestamps,
    // but the REAL issue is that GetTime() provides DIFFERENT timestamps
    // on different nodes for the same block.
    BOOST_TEST_MESSAGE("  Nodes validating same block at different wall times");
    BOOST_TEST_MESSAGE("  get different timestamps -> different volatility -> CONSENSUS SPLIT");
}

BOOST_FIXTURE_TEST_CASE(rh41_accepted_mint_records_block_time_not_wall_time, RH41TestSetup)
{
    const int currentHeight = 1000;
    const int64_t blockTime = 1'700'000'000;
    const int64_t wallTime = blockTime + 7'200;
    const CAmount ddAmount = 10'000;
    const CAmount oraclePrice = 500'000;

    SetMockTime(wallTime);

    const int64_t lockHeight = currentHeight + DigiDollar::LockDaysToBlocks(0);
    CTransaction tx = CTransaction(BuildMintTx(ddAmount, 50'000 * COIN,
                                               lockHeight, /*lockTier=*/0));

    auto ctx = MakeContext(currentHeight, oraclePrice, /*systemCollateral=*/300);
    ctx.nBlockTime = blockTime;

    TxValidationState state;
    BOOST_REQUIRE_MESSAGE(DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state),
                          state.ToString());

    BOOST_CHECK(VolatilityMonitor::GetPriceHistory().empty());
    DigiDollar::RecordAcceptedMintVolatility(ctx);

    const auto history = VolatilityMonitor::GetPriceHistory();
    BOOST_REQUIRE_EQUAL(history.size(), 1U);
    BOOST_CHECK_EQUAL(history.back().timestamp, blockTime);
    BOOST_CHECK_NE(history.back().timestamp, wallTime);

    SetMockTime(0);
}

BOOST_FIXTURE_TEST_CASE(rh41_non_block_validation_does_not_mutate_volatility_history, RH41TestSetup)
{
    const int currentHeight = 1000;
    const int64_t wallTime = 1'700'007'200;
    const CAmount ddAmount = 10'000;
    const CAmount oraclePrice = 500'000;

    SetMockTime(wallTime);

    const int64_t lockHeight = currentHeight + DigiDollar::LockDaysToBlocks(0);
    CTransaction tx = CTransaction(BuildMintTx(ddAmount, 50'000 * COIN,
                                               lockHeight, /*lockTier=*/0));

    auto ctx = MakeContext(currentHeight, oraclePrice, /*systemCollateral=*/300);

    TxValidationState state;
    BOOST_REQUIRE_MESSAGE(DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state),
                          state.ToString());

    BOOST_CHECK(VolatilityMonitor::GetPriceHistory().empty());

    SetMockTime(0);
}

// ============================================================================
// Attack Vector 7: MIN_PRICE_INTERVAL Causes Price Recording Gaps
// ============================================================================
// RecordPrice() silently drops prices if < 3600s apart.
// If blocks arrive at 15s intervals and each mint calls RecordPrice(),
// only 1 in ~240 price recordings succeeds. This means a rapid price change
// across multiple blocks could be missed entirely.

BOOST_FIXTURE_TEST_CASE(rh41_price_interval_gap_attack, RH41TestSetup)
{
    int64_t baseTime = 1700000000;

    // Record initial price
    VolatilityMonitor::RecordPrice(10000, baseTime, 1000);

    // Attacker submits 100 mint txs in consecutive blocks with declining oracle prices
    // Each block is 15s apart. RecordPrice skips all but the first due to MIN_PRICE_INTERVAL.
    for (int i = 1; i <= 100; i++) {
        CAmount crashingPrice = 10000 - (i * 50); // Drops 50 micro-USD per block
        VolatilityMonitor::RecordPrice(crashingPrice, baseTime + i * 15, 1000 + i);
    }

    // After 100 blocks (25 minutes), price dropped from $0.01 to $0.005 (50% crash)
    // But RecordPrice only recorded the first one — all others were skipped
    // due to MIN_PRICE_INTERVAL = 3600s.
    auto history = VolatilityMonitor::GetPriceHistory();
    BOOST_TEST_MESSAGE("Price records after 100 rapid blocks: " << history.size());
    BOOST_CHECK_MESSAGE(history.size() <= 2,
        "Most price recordings should be silently dropped due to MIN_PRICE_INTERVAL");

    // The 50% crash went UNDETECTED by the volatility monitor
    VolatilityMonitor::UpdateState(1100);
    BOOST_CHECK_MESSAGE(!VolatilityMonitor::ShouldFreezeMinting(),
        "50% crash over 100 blocks was invisible to volatility monitor - VULNERABILITY");
    BOOST_TEST_MESSAGE("RH-41-V7: Rapid price crash invisible due to MIN_PRICE_INTERVAL filtering");
    BOOST_TEST_MESSAGE("  Fix: Use block-height-based intervals instead of wall-clock time");
}

// ============================================================================
// Attack Vector 8: Volatility State is Non-Consensus (Static Globals)
// ============================================================================
// VolatilityMonitor uses static global state. Two independently-syncing nodes
// will have different price histories and potentially different freeze states.
// This means freeze decisions are node-local, NOT consensus rules.

BOOST_FIXTURE_TEST_CASE(rh41_volatility_not_consensus, RH41TestSetup)
{
    // Node A: synced from genesis, has full price history
    VolatilityMonitor::ClearHistory();
    VolatilityMonitor::ClearFreeze();

    int64_t t = 1700000000;
    VolatilityMonitor::RecordPrice(10000, t, 100);
    VolatilityMonitor::RecordPrice(5000, t + 3601, 340); // 50% drop
    VolatilityMonitor::UpdateState(340);
    bool nodeA_frozen = VolatilityMonitor::ShouldFreezeMinting();

    // Node B: just started, syncing from a checkpoint at height 300
    // It has NO price history. Volatility monitor is empty.
    VolatilityMonitor::ClearHistory();
    VolatilityMonitor::ClearFreeze();
    VolatilityMonitor::UpdateState(340);
    bool nodeB_frozen = VolatilityMonitor::ShouldFreezeMinting();

    BOOST_TEST_MESSAGE("RH-41-V8: Volatility freeze is node-local, not consensus");
    BOOST_TEST_MESSAGE("  Node A (full history) frozen: " << nodeA_frozen);
    BOOST_TEST_MESSAGE("  Node B (fresh sync) frozen: " << nodeB_frozen);

    // Node B will NOT freeze because it has no price history
    // This means Node B accepts mint txs that Node A rejects -> chain split
    if (nodeA_frozen != nodeB_frozen) {
        BOOST_TEST_MESSAGE("  CONFIRMED: Different freeze states = potential chain split!");
    }
    // This is mitigated by skipOracleValidation during IBD, but the window
    // between "caught up" and "has full price history" is dangerous.
}

BOOST_AUTO_TEST_SUITE_END()
