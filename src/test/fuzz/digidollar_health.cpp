// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <digidollar/health.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

void initialize_dd_health()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}

} // namespace

FUZZ_TARGET(dd_health_checks, .init = initialize_dd_health)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // =========================================================================
    // 1. Fuzz CalculateHealthRatio (pure function, no global state)
    // =========================================================================
    {
        CAmount ddAmount = fdp.ConsumeIntegral<CAmount>();
        CAmount dgbAmount = fdp.ConsumeIntegral<CAmount>();
        CAmount dgbPrice = fdp.ConsumeIntegral<CAmount>();

        int ratio = DigiDollar::HealthUtils::CalculateHealthRatio(ddAmount, dgbAmount, dgbPrice);
        // Ratio should be non-negative (0 or capped at 300)
        assert(ratio >= 0 && ratio <= 300);
    }

    // =========================================================================
    // 2. Fuzz CalculateHealthRatio edge cases
    // =========================================================================
    {
        // Zero DD → 300% (perfect health)
        int ratio_zero_dd = DigiDollar::HealthUtils::CalculateHealthRatio(0, 1'000'000'000, 100);
        assert(ratio_zero_dd == 300);

        // Both zero → 300%
        int ratio_both_zero = DigiDollar::HealthUtils::CalculateHealthRatio(0, 0, 0);
        assert(ratio_both_zero == 300);

        // Fuzz with extreme values
        CAmount big_dd = fdp.ConsumeIntegralInRange<CAmount>(1, std::numeric_limits<CAmount>::max() / 2);
        CAmount big_dgb = fdp.ConsumeIntegralInRange<CAmount>(0, std::numeric_limits<CAmount>::max() / 2);
        CAmount big_price = fdp.ConsumeIntegralInRange<CAmount>(0, std::numeric_limits<CAmount>::max() / 2);
        int ratio_big = DigiDollar::HealthUtils::CalculateHealthRatio(big_dd, big_dgb, big_price);
        assert(ratio_big >= 0 && ratio_big <= 300);
    }

    // =========================================================================
    // 3. Fuzz FormatHealthStatus / GetRecommendedAction (pure functions)
    // =========================================================================
    {
        int health = fdp.ConsumeIntegralInRange<int>(-100, 500);
        std::string status = DigiDollar::HealthUtils::FormatHealthStatus(health);
        assert(!status.empty());
        assert(status == "Healthy" || status == "Warning" || status == "Critical");

        std::string action = DigiDollar::HealthUtils::GetRecommendedAction(health);
        assert(!action.empty());
    }

    // =========================================================================
    // 4. Fuzz GetTierIndex / GetTierLockDays roundtrip
    // =========================================================================
    {
        int lockDays = fdp.ConsumeIntegralInRange<int>(0, 10'000);
        int tierIndex = DigiDollar::HealthUtils::GetTierIndex(lockDays);
        assert(tierIndex >= 0);

        int recoveredDays = DigiDollar::HealthUtils::GetTierLockDays(tierIndex);
        assert(recoveredDays >= 0);
    }

    // =========================================================================
    // 5. Fuzz GetTierLockDays with out-of-range tier indices
    // =========================================================================
    {
        int tierIndex = fdp.ConsumeIntegralInRange<int>(-5, 20);
        int days = DigiDollar::HealthUtils::GetTierLockDays(tierIndex);
        assert(days >= 0);
    }

    // =========================================================================
    // 6. Fuzz AlertThresholds constants (compile-time checks)
    // =========================================================================
    {
        // Verify all alert threshold constants are reasonable
        static_assert(DigiDollar::AlertThresholds::ALERT_DD_SUPPLY > 0);
        static_assert(DigiDollar::AlertThresholds::MIN_HEALTH_RATIO > 0);
        static_assert(DigiDollar::AlertThresholds::CRITICAL_HEALTH_RATIO > 0);
        static_assert(DigiDollar::AlertThresholds::MIN_HEALTH_RATIO > DigiDollar::AlertThresholds::CRITICAL_HEALTH_RATIO);
    }

    // =========================================================================
    // 7. Fuzz SystemMetrics struct construction with arbitrary values
    // =========================================================================
    {
        DigiDollar::SystemMetrics metrics;
        metrics.totalDDSupply = fdp.ConsumeIntegral<CAmount>();
        metrics.totalCollateral = fdp.ConsumeIntegral<CAmount>();
        metrics.systemHealth = fdp.ConsumeIntegralInRange<int>(0, 500);
        metrics.dcaMultiplier = fdp.ConsumeFloatingPointInRange<double>(0.0, 20.0);
        metrics.errActive = fdp.ConsumeBool();
        metrics.volatility = fdp.ConsumeFloatingPointInRange<double>(0.0, 200.0);
        metrics.mintingFrozen = fdp.ConsumeBool();
        metrics.activeOracles = fdp.ConsumeIntegralInRange<int>(0, 50);
        metrics.lastOraclePrice = fdp.ConsumeIntegral<CAmount>();
        metrics.lastOracleUpdate = fdp.ConsumeIntegral<int64_t>();

        // Add fuzzed tier metrics
        int numTiers = fdp.ConsumeIntegralInRange<int>(0, 12);
        for (int i = 0; i < numTiers; ++i) {
            DigiDollar::SystemMetrics::TierMetrics tier;
            tier.lockDays = fdp.ConsumeIntegralInRange<int>(0, 3650);
            tier.ddMinted = fdp.ConsumeIntegral<CAmount>();
            tier.dgbLocked = fdp.ConsumeIntegral<CAmount>();
            tier.positions = fdp.ConsumeIntegralInRange<int>(0, 100'000);
            tier.healthRatio = fdp.ConsumeIntegralInRange<int>(0, 500);
            metrics.tiers.push_back(tier);
        }

        // The struct should be well-formed (no crash)
        (void)metrics.totalDDSupply;
        (void)metrics.tiers.size();
    }
}

FUZZ_TARGET(dd_health_protection, .init = initialize_dd_health)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // =========================================================================
    // 1. Test health status classification at all boundary values
    // =========================================================================
    {
        // Test exact threshold boundaries
        std::string status_120 = DigiDollar::HealthUtils::FormatHealthStatus(
            DigiDollar::AlertThresholds::MIN_HEALTH_RATIO);
        assert(status_120 == "Healthy");

        std::string status_119 = DigiDollar::HealthUtils::FormatHealthStatus(
            DigiDollar::AlertThresholds::MIN_HEALTH_RATIO - 1);
        assert(status_119 == "Warning");

        std::string status_110 = DigiDollar::HealthUtils::FormatHealthStatus(
            DigiDollar::AlertThresholds::CRITICAL_HEALTH_RATIO);
        assert(status_110 == "Warning");

        std::string status_109 = DigiDollar::HealthUtils::FormatHealthStatus(
            DigiDollar::AlertThresholds::CRITICAL_HEALTH_RATIO - 1);
        assert(status_109 == "Critical");
    }

    // =========================================================================
    // 2. Sweep health from 0% to 200%+ and verify monotonic classification
    // =========================================================================
    {
        // Test a range of health values to verify correct classification
        for (int h = 0; h <= 200; ++h) {
            std::string status = DigiDollar::HealthUtils::FormatHealthStatus(h);
            std::string action = DigiDollar::HealthUtils::GetRecommendedAction(h);
            assert(!status.empty());
            assert(!action.empty());

            if (h >= DigiDollar::AlertThresholds::MIN_HEALTH_RATIO) {
                assert(status == "Healthy");
                assert(action == "Monitor");
            } else if (h >= DigiDollar::AlertThresholds::CRITICAL_HEALTH_RATIO) {
                assert(status == "Warning");
                assert(action == "Add Collateral");
            } else {
                assert(status == "Critical");
            }
        }
    }

    // =========================================================================
    // 3. Fuzz CalculateHealthRatio with price movements (protection sim)
    // =========================================================================
    {
        // Simulate collateral locked at a certain price, then price drops
        CAmount ddMinted = fdp.ConsumeIntegralInRange<CAmount>(100, 10'000'000); // $1 - $100k in cents
        CAmount dgbLocked = fdp.ConsumeIntegralInRange<CAmount>(100'000'000, MAX_MONEY / 2); // 1+ DGB

        // Test at various price levels
        int numPrices = fdp.ConsumeIntegralInRange<int>(1, 20);
        for (int i = 0; i < numPrices; ++i) {
            CAmount price = fdp.ConsumeIntegralInRange<CAmount>(1, 10'000); // $0.01 - $100 in cents
            int health = DigiDollar::HealthUtils::CalculateHealthRatio(ddMinted, dgbLocked, price);
            assert(health >= 0 && health <= 300);
        }
    }

    // =========================================================================
    // 4. Fuzz consensus parameter helpers
    // =========================================================================
    {
        DigiDollar::ConsensusParams ddParams;

        CAmount mintAmount = fdp.ConsumeIntegral<CAmount>();
        (void)DigiDollar::IsValidMintAmount(mintAmount, ddParams);

        CAmount minOutput = DigiDollar::GetMinimumDDOutput(ddParams);
        assert(minOutput > 0);

        int lockDays = fdp.ConsumeIntegralInRange<int>(0, 10'000);
        int64_t lockBlocks = DigiDollar::LockDaysToBlocks(lockDays);
        assert(lockBlocks >= 0);

        // Test BlocksToLockDays roundtrip approximate
        int recoveredDays = DigiDollar::BlocksToLockDays(lockBlocks);
        assert(recoveredDays >= 0);

        // Test GetCollateralRatioForLockTime with fuzzed lock periods
        int64_t fuzzBlocks = fdp.ConsumeIntegralInRange<int64_t>(0, 50'000'000);
        int ratio = DigiDollar::GetCollateralRatioForLockTime(fuzzBlocks, ddParams);
        int min_ratio = std::numeric_limits<int>::max();
        int max_ratio = 0;
        for (const auto& [_, tier_ratio] : ddParams.collateralRatios) {
            min_ratio = std::min(min_ratio, tier_ratio);
            max_ratio = std::max(max_ratio, tier_ratio);
        }
        assert(ratio == 0 || (ratio >= min_ratio && ratio <= max_ratio));

        // Test GetDCAMultiplier
        int systemCollateral = fdp.ConsumeIntegralInRange<int>(0, 500);
        double dcaMult = DigiDollar::GetDCAMultiplier(systemCollateral, ddParams);
        assert(dcaMult >= 1.0); // DCA only increases requirements
    }

    // =========================================================================
    // 5. Fuzz ValidateConsensusParams
    // =========================================================================
    {
        DigiDollar::ConsensusParams params;

        // Fuzz some parameters
        params.minMintAmount = fdp.ConsumeIntegral<CAmount>();
        params.maxMintAmount = fdp.ConsumeIntegral<CAmount>();
        params.minOutputAmount = fdp.ConsumeIntegral<CAmount>();
        params.oracleCount = fdp.ConsumeIntegralInRange<uint32_t>(0, 100);
        params.activeOracles = fdp.ConsumeIntegralInRange<uint32_t>(0, 100);
        params.oracleThreshold = fdp.ConsumeIntegralInRange<uint32_t>(0, 100);

        std::string error;
        (void)DigiDollar::ValidateConsensusParams(params, error);
        // Must not crash regardless of parameter values
    }
}
