// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_CONSENSUS_ERR_H
#define DIGIBYTE_CONSENSUS_ERR_H

#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations
class XOnlyPubKey;
class COraclePriceMessage;
class COracleBundle;
namespace Consensus { struct Params; }

namespace DigiDollar {

namespace ERR {

/**
 * ERR (Emergency Redemption Ratio) State
 *
 * Tracks the current state of the Emergency Redemption Ratio system,
 * which provides protection for DigiDollar holders during system
 * under-collateralization events.
 */
struct ERRState {
    bool isActive;                  // Whether ERR is currently active
    int systemHealth;               // Current system health percentage (0-30000)
    int adjustmentRatioBps;         // Current ERR adjustment ratio in basis points (8000-10000)
    double adjustmentRatio;         // Display/backcompat mirror of adjustmentRatioBps (0.8-1.0)
    uint32_t activationHeight;      // Block height when ERR was activated
    uint256 oracleConsensusHash;    // Hash of oracle consensus that triggered ERR
    uint64_t activationTimestamp;   // Timestamp when ERR was activated

    ERRState() : isActive(false), systemHealth(0), adjustmentRatioBps(0), adjustmentRatio(0.0),
                 activationHeight(0), activationTimestamp(0) {}
};

/**
 * Emergency Redemption Ratio (ERR) System
 *
 * The ERR system activates when the DigiDollar system becomes under-collateralized
 * (system health < 100%). During ERR, users can still redeem their FULL collateral,
 * but they must burn MORE DigiDollars than they originally minted.
 *
 * KEY CONCEPT: ERR increases DD burn requirement, NOT reduces collateral return!
 * - Normal redemption: Burn 100 DD → Get full collateral back
 * - ERR at 80%: Burn 125 DD (100/0.80) → Get full collateral back
 *
 * This creates buying pressure on DD during crises (people need more DD to redeem),
 * which helps stabilize the system.
 *
 * ERR Activation:
 * - Triggers when system health falls below 100% collateralization
 * - Requires oracle consensus to activate
 * - Automatically deactivates when system health recovers above 100%
 *
 * ERR Adjustment Tiers (DD burn multiplier):
 * - 95-100% health: Burn 105.3% DD (1/0.95) to get full collateral
 * - 90-95% health: Burn 111.1% DD (1/0.90) to get full collateral
 * - 85-90% health: Burn 117.6% DD (1/0.85) to get full collateral
 * - <85% health: Burn 125% DD (1/0.80) to get full collateral (max multiplier)
 *
 * Emergency Procedures:
 * - Blocks new mints during ERR period
 * - Prioritizes ERR redemptions over normal redemptions
 * - Implements fair pro-rata distribution for all redeemers
 */
class EmergencyRedemptionRatio {
public:
    /**
     * Check if ERR should be activated based on system health.
     *
     * @param systemHealth Current system health percentage (0-30000)
     * @return True if ERR should be activated (health < 100%)
     *
     * ERR activates when the DigiDollar system becomes under-collateralized,
     * providing emergency protection for holders while the system recovers.
     */
    static bool ShouldActivateERR(int systemHealth);

    /**
     * Calculate ERR adjustment ratio based on system health severity.
     *
     * @param systemHealth Current system health percentage
     * @return Display/backcompat adjustment ratio (0.80-1.00)
     *
     * Consensus burn math uses CalculateERRRatioBps() instead.
     * Adjustment tiers:
     * - Healthy:  1.00 → Burn original DD
     * - 95-100%: 0.95 → Burn 105.3% DD (1/0.95)
     * - 90-95%:  0.90 → Burn 111.1% DD (1/0.90)
     * - 85-90%:  0.85 → Burn 117.6% DD (1/0.85)
     * - <85%:    0.80 → Burn 125% DD (1/0.80) - maximum multiplier
     */
    static double CalculateERRAdjustment(int systemHealth);

    /**
     * Calculate ERR adjustment ratio in basis points.
     *
     * @param systemHealth Current system health percentage
     * @return Ratio in basis points: 10000 healthy, then 9500/9000/8500/8000 during ERR
     *
     * Consensus-visible ERR burn math must use this integer representation.
     */
    static int CalculateERRRatioBps(int systemHealth);

    /**
     * Calculate the required DD burn amount for ERR redemption.
     *
     * @param originalDDMinted The DD amount originally minted for this position
     * @param systemHealth Current system health percentage
     * @return Required DD amount to burn to get FULL collateral back
     *
     * Example: 100 DD minted at 80% health → Must burn 125 DD to get full collateral
     * Formula: RequiredDD = OriginalDD / ERRRatio
     */
    static CAmount GetRequiredDDBurn(CAmount originalDDMinted, int systemHealth);

    /**
     * DEPRECATED: Use GetRequiredDDBurn instead.
     * This function name is misleading - ERR doesn't reduce collateral return.
     * Kept for backwards compatibility but will be removed.
     */
    static CAmount GetAdjustedRedemption(CAmount normalRedemption, int systemHealth);

    /**
     * Check if oracle consensus exists for ERR activation.
     *
     * @param bundle Oracle bundle containing price messages
     * @return True if sufficient configured oracle consensus exists
     *
     * Validates that:
     * - At least 8 valid oracle signatures
     * - Signatures are from authorized oracle keys
     * - Messages are within acceptable time window
     * - Price consensus exists among oracles
     */
    static bool HasOracleConsensus(const COracleBundle& bundle, const Consensus::Params& params);

    /**
     * Get the current ERR system state.
     *
     * @return Current ERRState with activation status, health, and parameters
     *
     * Includes:
     * - Activation status and timestamp
     * - Current system health and adjustment ratio
     * - Oracle consensus hash that triggered activation
     * - Block height of activation
     */
    static ERRState GetCurrentState();

    /**
     * Get the current ERR redemption queue.
     *
     * @return Vector of COutPoints representing pending ERR redemptions
     *
     * During ERR activation:
     * - Normal redemptions are queued for ERR processing
     * - Queue is processed in FIFO order for fairness
     * - All redemptions receive pro-rata ERR adjustment
     * - Queue is cleared when ERR deactivates
     */
    static std::vector<COutPoint> GetERRQueue();

    /**
     * Add a redemption request to the ERR queue.
     *
     * @param outpoint The collateral UTXO being redeemed
     * @param ddAmount The amount of DigiDollars being burned
     * @param requestHeight Block height of the redemption request
     * @return True if successfully queued
     *
     * Called when:
     * - ERR is active and normal redemption is attempted
     * - Redemption request meets basic validation requirements
     * - Queue has space for additional requests
     */
    static bool QueueERRRedemption(const COutPoint& outpoint, CAmount ddAmount, uint32_t requestHeight);

    /**
     * Process pending ERR redemptions from the queue.
     *
     * @param maxRedemptions Maximum number of redemptions to process
     * @return Number of redemptions successfully processed
     *
     * Processing logic:
     * - FIFO order to ensure fairness
     * - Apply current ERR adjustment ratio
     * - Update UTXO set with adjusted collateral return
     * - Remove processed items from queue
     */
    static size_t ProcessERRQueue(size_t maxRedemptions = 100);

    /**
     * Activate ERR system with oracle consensus.
     *
     * @param oracleBundle Oracle bundle providing consensus
     * @param activationHeight Block height of activation
     * @return True if ERR successfully activated
     *
     * Activation requirements:
     * - System health below 100%
     * - Valid configured oracle consensus
     * - ERR not already active
     * - Oracle messages within time window
     */
    static bool ActivateERR(const COracleBundle& oracleBundle, uint32_t activationHeight, const Consensus::Params& params);

    /**
     * Deactivate ERR system when health recovers.
     *
     * @param currentHealth Current system health percentage
     * @return True if ERR successfully deactivated
     *
     * Deactivation triggers:
     * - System health recovers to 100% or above
     * - All pending ERR redemptions processed
     * - System stability confirmed over time period
     */
    static bool DeactivateERR(int currentHealth);

    /**
     * Reconstruct ERR state from chain data after restart
     * If system health < 100%, activates ERR; otherwise ensures ERR is inactive
     * @param currentSystemHealth Current system health percentage
     * @param currentHeight Current block height
     */
    static void ReconstructERRState(int currentSystemHealth, uint32_t currentHeight);

    /**
     * @brief Clear the state-reconstructed lock, allowing GetCurrentState() to read DCA cache again.
     * Call this after the first real health update from block processing.
     */
    static void ClearStateReconstructed();

    /**
     * Validate an ERR redemption transaction.
     *
     * @param tx The ERR redemption transaction
     * @param expectedDDAmount Expected DigiDollar amount being burned
     * @param expectedCollateral Expected collateral amount before ERR adjustment
     * @return True if ERR redemption is valid
     *
     * Validation includes:
     * - ERR is currently active
     * - Correct ERR adjustment ratio applied
     * - Valid DD burning (inputs > outputs)
     * - Proper collateral release calculation
     * - Transaction in ERR queue or processed
     */
    static bool ValidateERRRedemption(const CTransaction& tx, CAmount expectedDDAmount, CAmount expectedCollateral);

    /**
     * Check if minting should be blocked during ERR.
     *
     * @return True if new mints should be blocked
     *
     * Minting is blocked during ERR to:
     * - Prevent further system destabilization
     * - Focus on processing redemptions
     * - Allow system health to recover
     * - Protect existing DD holders
     */
    static bool ShouldBlockMinting(CAmount oraclePriceOverride = 0);

    /**
     * Get ERR statistics for monitoring and reporting.
     *
     * @return Formatted string with ERR system statistics
     *
     * Statistics include:
     * - Current ERR status and duration
     * - System health and adjustment ratio
     * - Queue size and processing rate
     * - Total redemptions processed during ERR
     * - Estimated recovery timeline
     */
    static std::string GetERRStatistics();

    /**
     * Validate ERR configuration parameters.
     *
     * @param error Output parameter for error message
     * @return True if ERR configuration is valid
     *
     * Validates:
     * - Adjustment ratio thresholds are logical
     * - Oracle consensus requirements are achievable
     * - Minimum protection levels are adequate
     * - System parameters are consistent
     */
    static bool ValidateERRConfig(std::string& error);

    /**
     * Format ERR adjustment ratio for display.
     *
     * @param ratio ERR adjustment ratio (0.8-0.95)
     * @return Human-readable string (e.g., "90%", "80%")
     */
    static std::string FormatERRAdjustment(double ratio);

    /**
     * Format ERR system health for display.
     *
     * @param health System health percentage
     * @param isERRActive Whether ERR is currently active
     * @return Formatted health status string
     */
    static std::string FormatERRHealth(int health, bool isERRActive);

    // ============================================================================
    // Extreme Scenario Testing Functions (for RED phase tests)
    // ============================================================================

    /**
     * Handle health oscillation around activation threshold.
     *
     * @param healthValues Vector of oscillating health values
     * @param activationResults Vector of activation results
     * @return True if oscillation handled properly
     */
    static bool HandleHealthOscillation(const std::vector<int>& healthValues, const std::vector<bool>& activationResults);

    /**
     * Validate precision boundaries for activation.
     *
     * @param preciseHealth Vector of precise health values
     * @return True if precision boundaries handled
     */
    static bool ValidatePrecisionBoundaries(const std::vector<double>& preciseHealth);

    /**
     * Validate extreme health values.
     *
     * @param extremeValues Vector of extreme health values
     * @return True if extreme values handled properly
     */
    static bool ValidateExtremeHealthValues(const std::vector<int>& extremeValues);

    /**
     * Validate thread safety of ERR operations.
     *
     * @param concurrentResults Results from concurrent operations
     * @return True if operations are thread safe
     */
    static bool ValidateThreadSafety(const std::vector<bool>& concurrentResults);

    /**
     * Handle oracle consensus failure scenarios.
     *
     * @param systemHealth Current system health
     * @param messages Insufficient oracle messages
     * @return True if consensus failure handled
     */
    static bool HandleConsensusFailure(int systemHealth, const std::vector<COraclePriceMessage>& messages);

    /**
     * Check ERR activation with corrupted state.
     *
     * @param systemHealth Current system health
     * @param stateCorrupted Whether state is corrupted
     * @return True if can activate with corruption
     */
    static bool ShouldActivateERRWithCorruptedState(int systemHealth, bool stateCorrupted);

    /**
     * Check ERR activation at specific height.
     *
     * @param systemHealth Current system health
     * @param blockHeight Block height to check
     * @return True if should activate at height
     */
    static bool ShouldActivateERRAtHeight(int systemHealth, uint32_t blockHeight);

    /**
     * Check ERR activation at specific time.
     *
     * @param systemHealth Current system health
     * @param timestamp Time to check
     * @return True if should activate at time
     */
    static bool ShouldActivateERRAtTime(int systemHealth, int64_t timestamp);

    /**
     * Check if ERR has activation delay.
     *
     * @param systemHealth Current system health
     * @return True if delay mechanism is active
     */
    static bool HasActivationDelay(int systemHealth);

    /**
     * Validate ratio precision calculations.
     *
     * @param health System health value
     * @param expectedRatio Expected ratio value
     * @return True if precision is valid
     */
    static bool ValidateRatioPrecision(int health, double expectedRatio);

    /**
     * Prevent calculation overflow.
     *
     * @param maxRedemption Maximum redemption amount
     * @param health System health value
     * @return True if overflow prevented
     */
    static bool PreventCalculationOverflow(CAmount maxRedemption, int health);

    /**
     * Validate calculation consistency.
     *
     * @param stressTests Results from stress testing
     * @return True if calculations are consistent
     */
    static bool ValidateCalculationConsistency(const std::vector<std::pair<CAmount, double>>& stressTests);

    /**
     * Handle large oracle message count.
     *
     * @param bundle Oracle bundle with many messages
     * @return True if large count handled
     */
    static bool HandleLargeOracleMessageCount(const COracleBundle& bundle);

    /**
     * Validate malformed message handling.
     *
     * @param malformedMessages Vector of malformed messages
     * @return True if malformed messages handled
     */
    static bool ValidateMalformedMessageHandling(const std::vector<COraclePriceMessage>& malformedMessages);

    /**
     * Validate consensus performance.
     *
     * @param durationMs Duration in milliseconds
     * @return True if performance is acceptable
     */
    static bool ValidateConsensusPerformance(int64_t durationMs);

private:
    // Internal state management
    static ERRState s_currentState;
    static bool s_stateReconstructed;  // RH-36a: prevents DCA cache from overwriting reconstructed ERR state
    static std::vector<COutPoint> s_errQueue;
    static std::map<COutPoint, std::pair<CAmount, uint32_t>> s_queuedRedemptions;
    static std::mutex s_errMutex;  //!< RH-44: Protects all ERR static state from concurrent access

    /** Deactivate ERR while s_errMutex is already held. Prevents self-deadlock
     *  when GetCurrentState() needs to deactivate inline. */
    static bool DeactivateERRLocked(int currentHealth);

public:
    /** Reset all static ERR state — for unit tests only. */
    static void ResetForTesting();

    /** Set ERR active state directly — for unit tests only. */
    static void SetActiveForTesting(bool active);

    /**
     * Internal helper to calculate oracle consensus hash.
     *
     * @param bundle Oracle bundle containing price messages
     * @return Hash representing the oracle consensus
     */
    static uint256 CalculateOracleConsensusHash(const COracleBundle& bundle);

    /**
     * Internal helper to validate oracle message signatures.
     *
     * @param message Oracle price message to validate
     * @return True if signature is valid
     */
    static bool ValidateOracleSignature(const COraclePriceMessage& message);

    /**
     * Internal helper to check if oracle key is authorized.
     *
     * @param oracleKey The oracle public key to check
     * @return True if oracle is authorized for ERR consensus
     */
    static bool IsAuthorizedOracleKey(const XOnlyPubKey& oracleKey);

    /**
     * Internal helper to update ERR state.
     *
     * @param newState The new ERR state to set
     */
    static void UpdateERRState(const ERRState& newState);

    /**
     * Internal helper to clear ERR queue.
     */
    static void ClearERRQueue();
};

} // namespace ERR
} // namespace DigiDollar

#endif // DIGIBYTE_CONSENSUS_ERR_H
