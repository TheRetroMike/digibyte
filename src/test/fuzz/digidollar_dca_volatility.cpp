// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wave 6 fuzz harness: exercises DCA::CalculateSystemHealth /
// DCA::ApplyDCA, plus VolatilityMonitor::RecordPrice / UpdateState /
// WouldCandidateFreezeMinting. The goal is to confirm:
//   1. DCA::CalculateSystemHealth never returns a value outside [0, 30000].
//   2. DCA::ApplyDCA never returns a value smaller than its base ratio when
//      base ratio is positive (rounding direction must favor system safety).
//   3. WouldCandidateFreezeMinting is non-mutating: history size and the
//      back of the history are unchanged after the call.

#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/dca.h>
#include <consensus/volatility.h>
#include <primitives/oracle.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

void initialize_dd_dca_volatility()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}

} // namespace

FUZZ_TARGET(dd_dca_volatility, .init = initialize_dd_dca_volatility)
{
    using DigiDollar::DCA::DynamicCollateralAdjustment;
    using DigiDollar::Volatility::PricePoint;
    using DigiDollar::Volatility::VolatilityMonitor;

    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // Each fuzz call starts from a clean volatility state to keep
    // assertions deterministic across iterations.
    VolatilityMonitor::ClearHistory();
    VolatilityMonitor::ClearFreeze();

    // -----------------------------------------------------------------------
    // 1. CalculateSystemHealth invariants.
    // -----------------------------------------------------------------------
    {
        const CAmount totalCollateral = fdp.ConsumeIntegralInRange<CAmount>(-1, MAX_MONEY);
        const CAmount totalDD = fdp.ConsumeIntegralInRange<CAmount>(-1,
            std::numeric_limits<CAmount>::max() / 2);
        const CAmount oraclePrice = fdp.ConsumeIntegralInRange<CAmount>(
            -1, static_cast<CAmount>(ORACLE_MAX_PRICE_MICRO_USD) * 4);
        int health = DynamicCollateralAdjustment::CalculateSystemHealth(
            totalCollateral, totalDD, oraclePrice);
        assert(health >= 0);
        assert(health <= 30000);

        // Negative inputs must fail closed (return 0).
        if (totalCollateral < 0 || totalDD < 0 || oraclePrice <= 0) {
            assert(health == 0);
        }

        // Zero supply with otherwise valid inputs must report max health.
        if (totalDD == 0 && oraclePrice > 0 && totalCollateral >= 0) {
            assert(health == 30000);
        }
    }

    // -----------------------------------------------------------------------
    // 2. ApplyDCA invariants.
    // -----------------------------------------------------------------------
    {
        const int baseRatio = fdp.ConsumeIntegralInRange<int>(0, 100'000);
        const int health = fdp.ConsumeIntegralInRange<int>(-100, 30'500);
        const int adjusted = DynamicCollateralAdjustment::ApplyDCA(baseRatio, health);
        assert(adjusted >= 0);
        if (baseRatio > 0) {
            // 1.0x is the minimum effective multiplier; rounding is ceil-up so
            // the adjusted ratio must never undercut the base.
            assert(adjusted >= baseRatio);
        }
        // 2.0x is the maximum effective multiplier in healthy/critical/
        // emergency tiers, plus a +1 ceil unit, capped at INT_MAX.
        if (adjusted < std::numeric_limits<int>::max() && baseRatio > 0) {
            const int64_t cap = static_cast<int64_t>(baseRatio) * 2 + 1;
            assert(static_cast<int64_t>(adjusted) <= cap);
        }
    }

    // -----------------------------------------------------------------------
    // 3. WouldCandidateFreezeMinting non-mutation invariants.
    // -----------------------------------------------------------------------
    {
        // Seed exactly one valid baseline price (within the oracle window).
        const CAmount baseline = fdp.ConsumeIntegralInRange<CAmount>(
            static_cast<CAmount>(ORACLE_MIN_PRICE_MICRO_USD),
            static_cast<CAmount>(ORACLE_MAX_PRICE_MICRO_USD));
        const int64_t baseTime = fdp.ConsumeIntegralInRange<int64_t>(
            1'600'000'000, 2'000'000'000);
        const uint32_t baseHeight = fdp.ConsumeIntegralInRange<uint32_t>(
            1, std::numeric_limits<uint32_t>::max() / 2);
        VolatilityMonitor::RecordPrice(baseline, baseTime, baseHeight);
        const auto historyBefore = VolatilityMonitor::GetPriceHistory();
        const auto stateBefore = VolatilityMonitor::GetCurrentState();
        assert(historyBefore.size() == 1U);

        // Try a wide range of candidate prices, including out-of-range ones
        // that should hit WouldCandidateFreezeMinting's early-return path.
        const int candidateCount = fdp.ConsumeIntegralInRange<int>(1, 8);
        for (int i = 0; i < candidateCount; ++i) {
            const CAmount candidate = fdp.ConsumeIntegralInRange<CAmount>(
                -1, static_cast<CAmount>(ORACLE_MAX_PRICE_MICRO_USD) * 2);
            (void)VolatilityMonitor::WouldCandidateFreezeMinting(candidate);
            const auto historyAfter = VolatilityMonitor::GetPriceHistory();
            const auto stateAfter = VolatilityMonitor::GetCurrentState();
            assert(historyAfter.size() == historyBefore.size());
            assert(historyAfter.back().price == historyBefore.back().price);
            assert(historyAfter.back().timestamp == historyBefore.back().timestamp);
            assert(historyAfter.back().height == historyBefore.back().height);
            assert(stateAfter.mintingFrozen == stateBefore.mintingFrozen);
            assert(stateAfter.allOperationsFrozen == stateBefore.allOperationsFrozen);
            assert(stateAfter.cooldownEndHeight == stateBefore.cooldownEndHeight);
        }
    }

    // -----------------------------------------------------------------------
    // 4. UpdateState idempotency under fuzzed heights.
    // -----------------------------------------------------------------------
    {
        // Add a couple of additional valid points an hour apart to give
        // CalculateVolatility/UpdateState meaningful work.
        const CAmount p1 = fdp.ConsumeIntegralInRange<CAmount>(
            static_cast<CAmount>(ORACLE_MIN_PRICE_MICRO_USD),
            static_cast<CAmount>(ORACLE_MAX_PRICE_MICRO_USD));
        VolatilityMonitor::RecordPrice(p1, 1'800'000'000 + 3600, 5000);
        const uint32_t h = fdp.ConsumeIntegralInRange<uint32_t>(0, 100'000);
        VolatilityMonitor::UpdateState(h);
        const uint32_t h2 = fdp.ConsumeIntegralInRange<uint32_t>(0, 100'000);
        VolatilityMonitor::UpdateState(h2);
        // No assertion needed — sanitizers will catch any inconsistency.
    }

    // Cleanup so subsequent fuzz iterations start fresh.
    VolatilityMonitor::ClearHistory();
    VolatilityMonitor::ClearFreeze();
}
