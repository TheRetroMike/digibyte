// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/digidollar.h>
#include <digidollar/validation.h>
#include <digidollar/scripts.h>
#include <digidollar/digidollar.h>
#include <digidollar/txbuilder.h>
#include <key.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/interpreter.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_redeem_tests)

struct DigiDollarRedeemTestSetup : public TestingSetup {
    DigiDollarRedeemTestSetup()
        : TestingSetup(ChainType::REGTEST),
          validationContext(1000, 50000, 150, Params())  // Initialize validationContext here
    {
        // Set up mock oracle price and system state
        mockOraclePrice = 50000; // $500.00 DGB
        mockSystemCollateral = 150; // 150% system-wide collateral
        mockHeight = 1000;
        lockPeriod = 30 * 24 * 60 * 4; // 30 days in blocks

        // Generate test keys
        testKey.MakeNewKey(true);
        testPubKey = testKey.GetPubKey();
        testXOnlyKey = XOnlyPubKey(testPubKey);

        collateralKey.MakeNewKey(true);
        collateralPubKey = collateralKey.GetPubKey();
        collateralXOnlyKey = XOnlyPubKey(collateralPubKey);

        // Set up builder
        builder = std::make_unique<DigiDollar::RedeemTxBuilder>(
            Params(), mockHeight, mockOraclePrice);
    }

    CKey testKey, collateralKey;
    CPubKey testPubKey, collateralPubKey;
    XOnlyPubKey testXOnlyKey, collateralXOnlyKey;
    CAmount mockOraclePrice;
    int mockSystemCollateral;
    int mockHeight;
    int64_t lockPeriod;
    DigiDollar::ValidationContext validationContext;
    std::unique_ptr<DigiDollar::RedeemTxBuilder> builder;

    // Helper to create test collateral position
    CCollateralPosition CreateTestPosition(CAmount ddAmount = 10000, bool expired = true) {
        CCollateralPosition position;
        position.outpoint = COutPoint(uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0);
        position.dgbLocked = (ddAmount * 500 * COIN) / mockOraclePrice; // 500% collateral
        position.ddMinted = ddAmount;
        position.unlockHeight = expired ? mockHeight - 100 : mockHeight + 100; // Expired or future
        position.collateralRatio = 500;
        // NOTE: Only two redemption paths exist: Normal and ERR
        // There is NO partial redemption in DigiDollar
        position.availablePaths = {
            CCollateralPosition::PATH_NORMAL,
            CCollateralPosition::PATH_ERR
        };
        return position;
    }

    // Helper to create redemption parameters
    DigiDollar::TxBuilderRedeemParams CreateRedeemParams(
        DigiDollar::RedemptionPath path = DigiDollar::RedemptionPath::NORMAL,
        CAmount ddAmount = 10000) {

        DigiDollar::TxBuilderRedeemParams params;
        params.collateralOutpoint = COutPoint(uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0);
        params.ddToRedeem = ddAmount;
        params.path = path;
        params.ownerKey = testKey;
        params.feeRate = 1000; // 1 sat/vB

        // Add mock DD UTXOs to burn
        params.ddUtxos.push_back(COutPoint(uint256S("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"), 0));

        // Add mock fee UTXOs
        params.feeUtxos.push_back(COutPoint(uint256S("fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321"), 0));

        return params;
    }
};

// ============================================================================
// TASK 3.7: REDEMPTION TRANSACTION BUILDER TESTS (TDD - RED PHASE)
// ============================================================================

BOOST_FIXTURE_TEST_CASE(test_normal_redemption_after_timelock_expiry, DigiDollarRedeemTestSetup)
{
    // Test normal redemption after timelock has expired
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);

    // Act: Build redemption transaction - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail since BuildRedemptionTransaction is not fully implemented
    BOOST_CHECK(!result.success);
    BOOST_CHECK(!result.error.empty());

    // After GREEN phase implementation:
    // BOOST_CHECK(result.success);
    // BOOST_CHECK(result.error.empty());
    // BOOST_CHECK(result.tx.vin.size() >= 2); // Collateral + DD inputs
    // BOOST_CHECK(result.tx.vout.size() >= 1); // DGB output to owner
    // CAmount expectedDGB = CreateTestPosition().dgbLocked;
    // BOOST_CHECK_EQUAL(result.tx.vout[0].nValue, expectedDGB);
}

BOOST_FIXTURE_TEST_CASE(test_normal_redemption_before_timelock, DigiDollarRedeemTestSetup)
{
    // Test normal redemption before timelock expires (should fail)
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);

    // Simulate position that hasn't expired yet
    mockHeight = 500; // Position unlocks at 1100, we're at 500
    builder = std::make_unique<DigiDollar::RedeemTxBuilder>(
        Params(), mockHeight, mockOraclePrice);

    // Act: Build redemption transaction - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase: Should fail due to timelock
    // BOOST_CHECK(!result.success);
    // BOOST_CHECK(result.error.find("timelock") != std::string::npos);
}

// DELETED: test_emergency_redemption_oracle_approval - Emergency redemption path does not exist in DigiDollar
// Only two redemption paths: Normal (full, after timelock) and ERR (full, more DD burned)

// DELETED: test_partial_redemption_keep_position_open - Partial redemption does not exist in DigiDollar
// Only two redemption paths: Normal (full, after timelock) and ERR (full, more DD burned)

BOOST_FIXTURE_TEST_CASE(test_full_redemption_entire_position, DigiDollarRedeemTestSetup)
{
    // Test full redemption of entire position
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000); // Full amount

    // Act: Build full redemption - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // BOOST_CHECK(result.success);
    // Should fully unlock collateral, no remainder
    // CAmount fullCollateral = CreateTestPosition().dgbLocked;
    // BOOST_CHECK_EQUAL(result.tx.vout[0].nValue, fullCollateral);
}

BOOST_FIXTURE_TEST_CASE(test_err_redemption_system_unhealthy, DigiDollarRedeemTestSetup)
{
    // Test ERR redemption when system is under-collateralized
    validationContext.systemCollateral = 80; // 80% system collateral (unhealthy)
    builder = std::make_unique<DigiDollar::RedeemTxBuilder>(
        Params(), mockHeight, mockOraclePrice);

    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::ERR, 10000);

    // Act: Build ERR redemption - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // BOOST_CHECK(result.success);
    // ERR should return reduced collateral (e.g., 90% of locked amount)
    // CAmount errCollateral = CreateTestPosition().dgbLocked * 90 / 100;
    // BOOST_CHECK_LE(result.tx.vout[0].nValue, errCollateral);
}

BOOST_FIXTURE_TEST_CASE(test_err_redemption_system_healthy_rejection, DigiDollarRedeemTestSetup)
{
    // Test ERR redemption when system is healthy (should be rejected)
    validationContext.systemCollateral = 150; // 150% system collateral (healthy)
    builder = std::make_unique<DigiDollar::RedeemTxBuilder>(
        Params(), mockHeight, mockOraclePrice);

    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::ERR, 10000);

    // Act: Build ERR redemption when healthy - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase: Should fail due to healthy system
    // BOOST_CHECK(!result.success);
    // BOOST_CHECK(result.error.find("ERR not available") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(test_taproot_script_path_spending, DigiDollarRedeemTestSetup)
{
    // Test that redemption uses proper Taproot script path spending
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 12000);

    // Act: Build transaction with script path - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // BOOST_CHECK(result.success);
    // Verify transaction inputs have proper witness stack for script path spending
    // Check MAST path selection is correct
}

BOOST_FIXTURE_TEST_CASE(test_collateral_release_calculation, DigiDollarRedeemTestSetup)
{
    // Test accurate collateral release calculation based on current price
    mockOraclePrice = 60000; // $600.00 DGB (20% price increase)
    builder = std::make_unique<DigiDollar::RedeemTxBuilder>(
        Params(), mockHeight, mockOraclePrice);

    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);

    // Act: Calculate collateral return - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // Price increase should affect collateral calculation
    // BOOST_CHECK(result.success);
    // Verify correct collateral amount considering price change
}

BOOST_FIXTURE_TEST_CASE(test_dd_burning_verification, DigiDollarRedeemTestSetup)
{
    // Test that DD tokens are properly burned (inputs > outputs)
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 8000);

    // Act: Build transaction with DD burning - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // BOOST_CHECK(result.success);
    // Verify DD inputs are consumed but no DD outputs created (burning)
    // NOTE: Partial redemption does not exist - all DD is burned in full redemption
}

BOOST_FIXTURE_TEST_CASE(test_oracle_price_integration, DigiDollarRedeemTestSetup)
{
    // Test redemption with different oracle prices
    std::vector<CAmount> testPrices = {30000, 50000, 80000}; // $300, $500, $800

    for (CAmount price : testPrices) {
        builder = std::make_unique<DigiDollar::RedeemTxBuilder>(
            Params(), mockHeight, price);

        auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);

        // Act: Build with different prices - EXPECTED TO FAIL (RED phase)
        auto result = builder->BuildRedemptionTransaction(params);

        // Assert: Should fail in RED phase
        BOOST_CHECK(!result.success);

        // After GREEN phase:
        // Different prices should affect collateral calculations
        // BOOST_CHECK(result.success);
    }
}

BOOST_FIXTURE_TEST_CASE(test_invalid_redemption_zero_amount, DigiDollarRedeemTestSetup)
{
    // Test redemption with zero DD amount (should fail)
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 0);

    // Act: Build with zero amount - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase: Should still fail due to zero amount
    // BOOST_CHECK(!result.success);
    // BOOST_CHECK(result.error.find("amount") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(test_invalid_redemption_negative_amount, DigiDollarRedeemTestSetup)
{
    // Test redemption with negative DD amount (should fail)
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, -1000);

    // Act: Build with negative amount - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase: Should still fail due to negative amount
    // BOOST_CHECK(!result.success);
    // BOOST_CHECK(result.error.find("amount") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(test_invalid_redemption_no_collateral_utxo, DigiDollarRedeemTestSetup)
{
    // Test redemption without collateral UTXO
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);
    params.collateralOutpoint = COutPoint(); // Invalid outpoint

    // Act: Build without collateral - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase: Should still fail due to missing collateral
    // BOOST_CHECK(!result.success);
    // BOOST_CHECK(result.error.find("collateral") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(test_invalid_redemption_no_dd_utxos, DigiDollarRedeemTestSetup)
{
    // Test redemption without DD UTXOs to burn
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);
    params.ddUtxos.clear(); // No DD to burn

    // Act: Build without DD UTXOs - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase: Should still fail due to missing DD UTXOs
    // BOOST_CHECK(!result.success);
    // BOOST_CHECK(result.error.find("DD") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(test_redemption_path_determination_logic, DigiDollarRedeemTestSetup)
{
    // Test DetermineRedemptionPath function logic
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);

    // Test each path determination - EXPECTED TO FAIL (RED phase)

    // Normal path (timelock expired)
    auto normalPath = builder->DetermineRedemptionPath(params);
    // Should fail since method not implemented

    // ERR path (unhealthy system)
    validationContext.systemCollateral = 90;
    params.path = DigiDollar::RedemptionPath::ERR;
    auto errPath = builder->DetermineRedemptionPath(params);

    // After GREEN phase:
    // BOOST_CHECK_EQUAL(normalPath, DigiDollar::RedemptionPath::NORMAL);
    // BOOST_CHECK_EQUAL(errPath, DigiDollar::RedemptionPath::ERR);
}

BOOST_FIXTURE_TEST_CASE(test_calculate_collateral_return_function, DigiDollarRedeemTestSetup)
{
    // Test CalculateCollateralReturn function
    CAmount ddAmount = 10000; // $100.00
    CAmount originalCollateral = 100 * COIN; // 100 DGB
    CAmount currentPrice = 60000; // $600 (vs original $500)

    // Act: Calculate return
    CAmount returned = builder->CalculateCollateralReturn(ddAmount, originalCollateral, currentPrice);

    // Assert: Should return valid collateral amount
    BOOST_CHECK_GT(returned, 0);
    // Verify calculation is reasonable (should not exceed original collateral)
    BOOST_CHECK_LE(returned, originalCollateral);
}

BOOST_FIXTURE_TEST_CASE(test_verify_redemption_conditions_function, DigiDollarRedeemTestSetup)
{
    // Test VerifyRedemptionConditions function
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);
    auto path = DigiDollar::RedemptionPath::NORMAL;
    auto position = CreateTestPosition(10000, true); // expired position

    // Act: Verify conditions for normal redemption (position is expired by default)
    bool valid = builder->VerifyRedemptionConditions(params, path, position);

    // Assert: Should be valid for normal redemption with expired position
    BOOST_CHECK(valid); // Valid normal redemption

    // Test invalid conditions - ERR path on healthy system
    path = DigiDollar::RedemptionPath::ERR;
    validationContext.systemCollateral = 150; // Healthy system
    bool invalid = builder->VerifyRedemptionConditions(params, path, position);
    BOOST_CHECK(!invalid); // ERR not available on healthy system
}

BOOST_FIXTURE_TEST_CASE(test_create_redemption_script_function, DigiDollarRedeemTestSetup)
{
    // Test CreateRedemptionScript function for each path
    // NOTE: Only two redemption paths exist in DigiDollar: Normal and ERR
    // There is NO partial redemption and NO separate emergency path
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);

    // Test each redemption path script creation
    auto normalScript = builder->CreateRedemptionScript(DigiDollar::RedemptionPath::NORMAL, testKey);
    auto errScript = builder->CreateRedemptionScript(DigiDollar::RedemptionPath::ERR, testKey);

    // Scripts should be created properly
    BOOST_CHECK(!normalScript.empty());
    BOOST_CHECK(!errScript.empty());

    // NOTE: PARTIAL and EMERGENCY paths were removed - they do not exist in DigiDollar
    // Only Normal (full redemption after timelock) and ERR (full redemption with more DD burned)
}

BOOST_FIXTURE_TEST_CASE(test_multiple_dd_inputs_burning, DigiDollarRedeemTestSetup)
{
    // Test redemption with multiple DD UTXOs being burned
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 15000);

    // Add multiple DD UTXOs
    params.ddUtxos.push_back(COutPoint(uint256S("1111111111111111111111111111111111111111111111111111111111111111"), 0));
    params.ddUtxos.push_back(COutPoint(uint256S("2222222222222222222222222222222222222222222222222222222222222222"), 1));
    params.ddUtxos.push_back(COutPoint(uint256S("3333333333333333333333333333333333333333333333333333333333333333"), 2));

    // Act: Build with multiple inputs - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // BOOST_CHECK(result.success);
    // Should have all DD inputs included
    // BOOST_CHECK_EQUAL(result.tx.vin.size(), 5); // 1 collateral + 4 DD + potential fee inputs
}

BOOST_FIXTURE_TEST_CASE(test_fee_calculation_and_handling, DigiDollarRedeemTestSetup)
{
    // Test proper fee calculation and change handling
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);
    params.feeRate = 2000; // Higher fee rate

    // Act: Build with fees - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // BOOST_CHECK(result.success);
    // BOOST_CHECK_GT(result.totalFees, 0);
    // Verify fee calculation is accurate
}

BOOST_FIXTURE_TEST_CASE(test_dust_threshold_handling, DigiDollarRedeemTestSetup)
{
    // Test handling of dust amounts in redemption
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);

    // Create scenario with potential dust outputs
    // Act: Build transaction - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // Should handle dust properly (either combine with fees or reject)
    // BOOST_CHECK(result.success);
}

BOOST_FIXTURE_TEST_CASE(test_edge_case_maximum_redemption_amount, DigiDollarRedeemTestSetup)
{
    // Test redemption of maximum possible amount
    CAmount maxAmount = 10000000; // $100,000 (max transaction limit)
    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, maxAmount);

    // Act: Build maximum redemption - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase
    BOOST_CHECK(!result.success);

    // After GREEN phase:
    // BOOST_CHECK(result.success);
    // Should handle large amounts correctly
}

BOOST_FIXTURE_TEST_CASE(test_redemption_transaction_version_markers, DigiDollarRedeemTestSetup)
{
    // Test that different redemption paths use correct version markers
    // NOTE: Only two redemption paths exist in DigiDollar: Normal and ERR
    // There is NO partial redemption and NO separate emergency path
    std::vector<DigiDollar::RedemptionPath> paths = {
        DigiDollar::RedemptionPath::NORMAL,
        DigiDollar::RedemptionPath::ERR
    };

    for (auto path : paths) {
        auto params = CreateRedeemParams(path, 10000);

        // Act: Build for each path - EXPECTED TO FAIL (RED phase)
        auto result = builder->BuildRedemptionTransaction(params);

        // Assert: Should fail in RED phase
        BOOST_CHECK(!result.success);

        // After GREEN phase:
        // Each path should have appropriate transaction version marker
        // BOOST_CHECK(result.success);
        // Verify version field contains correct DD transaction type
    }
}

// ============================================================================
// ISSUE 7: TEST SEPARATE COLLATERAL RETURN AND DGB CHANGE OUTPUTS
// ============================================================================

BOOST_FIXTURE_TEST_CASE(test_collateral_and_change_separate_outputs, DigiDollarRedeemTestSetup)
{
    // Issue 7: Verify that collateral return and DGB change are SEPARATE outputs
    // Problem: Previously both were merged into the same output using the same destination
    // Expected: Output 0 = collateral, Output N = DGB change (different destinations)

    auto params = CreateRedeemParams(DigiDollar::RedemptionPath::NORMAL, 10000);

    // Pre-populate position data to avoid UTXO lookup
    params.collateralAmount = 100 * COIN;  // 100 DGB collateral
    params.ddMinted = 10000;  // 100 DD (cents)
    params.unlockHeight = mockHeight - 100;  // Expired

    // Provide DD amounts for burning
    params.ddAmounts.push_back(10000);  // Burn 100 DD

    // Provide fee UTXO amounts (slightly more than needed to create change)
    params.feeAmounts.push_back(1 * COIN);  // 1 DGB for fees (more than needed)

    // CRITICAL: Set DIFFERENT destinations for collateral and DGB change
    CKey collateralKey, changeKey;
    collateralKey.MakeNewKey(true);
    changeKey.MakeNewKey(true);

    params.collateralDest = CTxDestination{WitnessV1Taproot(XOnlyPubKey(collateralKey.GetPubKey()))};
    params.dgbChangeDest = CTxDestination{WitnessV1Taproot(XOnlyPubKey(changeKey.GetPubKey()))};

    // Act: Build redemption transaction - EXPECTED TO FAIL (RED phase)
    auto result = builder->BuildRedemptionTransaction(params);

    // Assert: Should fail in RED phase (builder not fully implemented)
    BOOST_CHECK(!result.success);

    // After GREEN phase (when builder is complete):
    // BOOST_CHECK(result.success);
    // BOOST_CHECK_GE(result.tx.vout.size(), 2);  // At least 2 outputs

    // Verify collateral output (vout[0]) uses collateralDest
    // CScript expectedCollateralScript = GetScriptForDestination(*params.collateralDest);
    // BOOST_CHECK(result.tx.vout[0].scriptPubKey == expectedCollateralScript);
    // BOOST_CHECK_EQUAL(result.tx.vout[0].nValue, params.collateralAmount);

    // Find DGB change output (should use dgbChangeDest, NOT collateralDest)
    // bool foundChangeOutput = false;
    // CScript expectedChangeScript = GetScriptForDestination(*params.dgbChangeDest);
    // for (const auto& vout : result.tx.vout) {
    //     if (vout.scriptPubKey == expectedChangeScript && vout.nValue > 0) {
    //         foundChangeOutput = true;
    //         BOOST_CHECK_NE(vout.scriptPubKey, expectedCollateralScript);  // Must be DIFFERENT
    //         break;
    //     }
    // }
    // BOOST_CHECK(foundChangeOutput);  // Must have DGB change output
}

BOOST_AUTO_TEST_SUITE_END()