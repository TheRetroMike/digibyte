// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <primitives/transaction.h>
#include <consensus/digidollar_tx.h>
#include <consensus/digidollar_transaction_validation.h>
#include <consensus/amount.h>
#include <chainparams.h>
#include <uint256.h>
#include <streams.h>
#include <util/strencodings.h>
#include <clientversion.h>

#include <boost/test/unit_test.hpp>
#include <set>

BOOST_FIXTURE_TEST_SUITE(digidollar_transaction_tests, BasicTestingSetup)

// =====================================
// Transaction Type Enum Tests
// =====================================

BOOST_AUTO_TEST_CASE(digidollar_tx_type_enum_values)
{
    // Test that all enum values are correct and distinct
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(DD_TX_NONE), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(DD_TX_MINT), 1);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(DD_TX_TRANSFER), 2);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(DD_TX_REDEEM), 3);

    // Test MAX validation boundary
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(DD_TX_MAX), 4);

    // Test all values are distinct
    std::set<uint8_t> values = {
        static_cast<uint8_t>(DD_TX_NONE),
        static_cast<uint8_t>(DD_TX_MINT),
        static_cast<uint8_t>(DD_TX_TRANSFER),
        static_cast<uint8_t>(DD_TX_REDEEM)
    };
    BOOST_CHECK_EQUAL(values.size(), 4);
}

BOOST_AUTO_TEST_CASE(digidollar_version_constants)
{
    // Test the DigiDollar version marker
    BOOST_CHECK_EQUAL(DD_TX_VERSION, 0x0D1D0770);

    // Test bit masks for version encoding
    BOOST_CHECK_EQUAL(DD_VERSION_MASK, 0x0000FFFF);
    BOOST_CHECK_EQUAL(DD_TYPE_MASK, 0xFF000000);
    BOOST_CHECK_EQUAL(DD_FLAGS_MASK, 0x00FF0000);

    // Test that masks don't overlap
    BOOST_CHECK_EQUAL((DD_VERSION_MASK & DD_TYPE_MASK), 0);
    BOOST_CHECK_EQUAL((DD_VERSION_MASK & DD_FLAGS_MASK), 0);
    BOOST_CHECK_EQUAL((DD_TYPE_MASK & DD_FLAGS_MASK), 0);
}

// =====================================
// Version Encoding/Decoding Tests
// =====================================

BOOST_AUTO_TEST_CASE(make_digidollar_version_basic)
{
    // Test basic version construction without flags
    int32_t version = MakeDigiDollarVersion(DD_TX_MINT);

    // Should have marker in lower 16 bits
    BOOST_CHECK_EQUAL((version & DD_VERSION_MASK), (DD_TX_VERSION & DD_VERSION_MASK));

    // Should have type in upper 8 bits
    BOOST_CHECK_EQUAL((version & DD_TYPE_MASK) >> 24, static_cast<uint8_t>(DD_TX_MINT));

    // Should have no flags
    BOOST_CHECK_EQUAL((version & DD_FLAGS_MASK) >> 16, 0);
}

BOOST_AUTO_TEST_CASE(make_digidollar_version_with_flags)
{
    // Test version construction with flags
    uint8_t flags = 0x42; // Some arbitrary flags
    int32_t version = MakeDigiDollarVersion(DD_TX_TRANSFER, flags);

    // Should have marker in lower 16 bits
    BOOST_CHECK_EQUAL((version & DD_VERSION_MASK), (DD_TX_VERSION & DD_VERSION_MASK));

    // Should have type in upper 8 bits
    BOOST_CHECK_EQUAL((version & DD_TYPE_MASK) >> 24, static_cast<uint8_t>(DD_TX_TRANSFER));

    // Should have flags in bits 16-23
    BOOST_CHECK_EQUAL((version & DD_FLAGS_MASK) >> 16, flags);
}

BOOST_AUTO_TEST_CASE(make_digidollar_version_all_types)
{
    // Test version construction for all transaction types
    std::vector<DigiDollarTxType> types = {
        DD_TX_MINT, DD_TX_TRANSFER, DD_TX_REDEEM
    };

    for (auto type : types) {
        int32_t version = MakeDigiDollarVersion(type, 0x55);

        // Verify marker
        BOOST_CHECK_EQUAL((version & DD_VERSION_MASK), (DD_TX_VERSION & DD_VERSION_MASK));

        // Verify type
        BOOST_CHECK_EQUAL((version & DD_TYPE_MASK) >> 24, static_cast<uint8_t>(type));

        // Verify flags
        BOOST_CHECK_EQUAL((version & DD_FLAGS_MASK) >> 16, 0x55);
    }
}

// =====================================
// Transaction Detection Tests
// =====================================

BOOST_AUTO_TEST_CASE(is_digidollar_transaction_detection)
{
    CMutableTransaction tx;

    // Standard transaction should not be detected as DigiDollar
    tx.nVersion = CTransaction::CURRENT_VERSION;
    CTransaction stdTx(tx);
    BOOST_CHECK(!IsDigiDollarTransaction(stdTx));

    // DigiDollar transaction should be detected
    tx.nVersion = MakeDigiDollarVersion(DD_TX_MINT);
    CTransaction ddTx(tx);
    BOOST_CHECK(IsDigiDollarTransaction(ddTx));

    // Test different DigiDollar types
    std::vector<DigiDollarTxType> types = {
        DD_TX_MINT, DD_TX_TRANSFER, DD_TX_REDEEM
    };

    for (auto type : types) {
        tx.nVersion = MakeDigiDollarVersion(type, 0x12);
        CTransaction testTx(tx);
        BOOST_CHECK(IsDigiDollarTransaction(testTx));
    }
}

BOOST_AUTO_TEST_CASE(get_digidollar_tx_type_extraction)
{
    CMutableTransaction tx;

    // Standard transaction should return DD_TX_NONE
    tx.nVersion = CTransaction::CURRENT_VERSION;
    CTransaction stdTx(tx);
    BOOST_CHECK_EQUAL(GetDigiDollarTxType(stdTx), DD_TX_NONE);

    // Test type extraction for each DigiDollar type
    std::vector<DigiDollarTxType> types = {
        DD_TX_MINT, DD_TX_TRANSFER, DD_TX_REDEEM
    };

    for (auto expectedType : types) {
        tx.nVersion = MakeDigiDollarVersion(expectedType, 0x99);
        CTransaction ddTx(tx);
        BOOST_CHECK_EQUAL(GetDigiDollarTxType(ddTx), expectedType);
    }
}

BOOST_AUTO_TEST_CASE(get_digidollar_flags_extraction)
{
    CMutableTransaction tx;

    // Standard transaction should return 0 flags
    tx.nVersion = CTransaction::CURRENT_VERSION;
    CTransaction stdTx(tx);
    BOOST_CHECK_EQUAL(GetDigiDollarFlags(stdTx), 0);

    // Test flag extraction
    std::vector<uint8_t> testFlags = {0x00, 0x01, 0x42, 0x80, 0xFF};

    for (auto expectedFlags : testFlags) {
        tx.nVersion = MakeDigiDollarVersion(DD_TX_MINT, expectedFlags);
        CTransaction ddTx(tx);
        BOOST_CHECK_EQUAL(GetDigiDollarFlags(ddTx), expectedFlags);
    }
}

// =====================================
// CMutableTransaction Helper Tests
// =====================================

BOOST_AUTO_TEST_CASE(cmutable_transaction_set_digidollar_type)
{
    CMutableTransaction tx;

    // Test setting DigiDollar type without flags
    tx.SetDigiDollarType(DD_TX_MINT);
    BOOST_CHECK(tx.IsDigiDollar());
    BOOST_CHECK_EQUAL(tx.GetDDType(), DD_TX_MINT);

    // Test setting DigiDollar type with flags
    tx.SetDigiDollarType(DD_TX_TRANSFER, 0x33);
    BOOST_CHECK(tx.IsDigiDollar());
    BOOST_CHECK_EQUAL(tx.GetDDType(), DD_TX_TRANSFER);

    // Verify the version field directly
    BOOST_CHECK_EQUAL((tx.nVersion & DD_VERSION_MASK), (DD_TX_VERSION & DD_VERSION_MASK));
    BOOST_CHECK_EQUAL((tx.nVersion & DD_TYPE_MASK) >> 24, static_cast<uint8_t>(DD_TX_TRANSFER));
    BOOST_CHECK_EQUAL((tx.nVersion & DD_FLAGS_MASK) >> 16, 0x33);
}

BOOST_AUTO_TEST_CASE(cmutable_transaction_is_digidollar)
{
    CMutableTransaction tx;

    // Standard transaction
    tx.nVersion = CTransaction::CURRENT_VERSION;
    BOOST_CHECK(!tx.IsDigiDollar());

    // Convert to DigiDollar
    tx.SetDigiDollarType(DD_TX_REDEEM, 0x77);
    BOOST_CHECK(tx.IsDigiDollar());

    // Reset to standard
    tx.nVersion = CTransaction::CURRENT_VERSION;
    BOOST_CHECK(!tx.IsDigiDollar());
}

BOOST_AUTO_TEST_CASE(cmutable_transaction_get_dd_type)
{
    CMutableTransaction tx;

    // Standard transaction should return DD_TX_NONE
    tx.nVersion = CTransaction::CURRENT_VERSION;
    BOOST_CHECK_EQUAL(tx.GetDDType(), DD_TX_NONE);

    // Test all DigiDollar types
    std::vector<DigiDollarTxType> types = {
        DD_TX_MINT, DD_TX_TRANSFER, DD_TX_REDEEM
    };

    for (auto expectedType : types) {
        tx.SetDigiDollarType(expectedType);
        BOOST_CHECK_EQUAL(tx.GetDDType(), expectedType);
    }
}

// =====================================
// Transaction Type Name Tests
// =====================================

BOOST_AUTO_TEST_CASE(get_digidollar_tx_type_name)
{
    // Test all type names
    BOOST_CHECK_EQUAL(GetDigiDollarTxTypeName(DD_TX_NONE), "NONE");
    BOOST_CHECK_EQUAL(GetDigiDollarTxTypeName(DD_TX_MINT), "MINT");
    BOOST_CHECK_EQUAL(GetDigiDollarTxTypeName(DD_TX_TRANSFER), "TRANSFER");
    BOOST_CHECK_EQUAL(GetDigiDollarTxTypeName(DD_TX_REDEEM), "REDEEM");

    // Test invalid type
    BOOST_CHECK_EQUAL(GetDigiDollarTxTypeName(static_cast<DigiDollarTxType>(99)), "UNKNOWN");
}

BOOST_AUTO_TEST_CASE(ctransaction_get_digidollar_info)
{
    CMutableTransaction mtx;

    // Standard transaction info
    mtx.nVersion = CTransaction::CURRENT_VERSION;
    CTransaction stdTx(mtx);
    BOOST_CHECK_EQUAL(stdTx.GetDigiDollarInfo(), "Not a DigiDollar transaction");

    // DigiDollar transaction info
    mtx.SetDigiDollarType(DD_TX_MINT, 0x12);
    CTransaction ddTx(mtx);
    std::string info = ddTx.GetDigiDollarInfo();
    BOOST_CHECK(info.find("DigiDollar MINT") != std::string::npos);
    BOOST_CHECK(info.find("0x12") != std::string::npos);
}

// =====================================
// Validation Tests
// =====================================

BOOST_AUTO_TEST_CASE(is_valid_digidollar_type_validation)
{
    // Test valid types
    // Test valid types
    BOOST_CHECK(IsValidDigiDollarType(DigiDollar::DD_TX_MINT));
    BOOST_CHECK(IsValidDigiDollarType(DigiDollar::DD_TX_TRANSFER));
    BOOST_CHECK(IsValidDigiDollarType(DigiDollar::DD_TX_REDEEM));

    // Test invalid types
    BOOST_CHECK(!IsValidDigiDollarType(static_cast<DigiDollar::DigiDollarTxType>(0))); // Invalid enum value
    BOOST_CHECK(!IsValidDigiDollarType(static_cast<DigiDollar::DigiDollarTxType>(255)));
}

BOOST_AUTO_TEST_CASE(validate_digidollar_tx_structure_basic)
{
    CMutableTransaction mtx;
    std::string strError;

    // Standard transaction should pass (not DD, so skipped)
    mtx.nVersion = CTransaction::CURRENT_VERSION;
    CTransaction stdTx(mtx);
    BOOST_CHECK(ValidateDigiDollarTxStructure(stdTx, strError));
    BOOST_CHECK(strError.empty());

    // Valid DigiDollar transaction should pass basic validation
    mtx.SetDigiDollarType(DD_TX_MINT);
    CTransaction ddTx(mtx);
    BOOST_CHECK(ValidateDigiDollarTxStructure(ddTx, strError));

    // Invalid DigiDollar type should fail
    mtx.nVersion = MakeDigiDollarVersion(static_cast<DigiDollarTxType>(99));
    CTransaction invalidTx(mtx);
    strError.clear();
    BOOST_CHECK(!ValidateDigiDollarTxStructure(invalidTx, strError));
    BOOST_CHECK(!strError.empty());
    BOOST_CHECK(strError.find("Invalid DigiDollar transaction type") != std::string::npos);
}

// =====================================
// Serialization Tests
// =====================================

BOOST_AUTO_TEST_CASE(digidollar_transaction_serialization)
{
    // Create a DigiDollar transaction
    CMutableTransaction originalTx;
    originalTx.SetDigiDollarType(DD_TX_TRANSFER, 0xAB);

    // Add some inputs and outputs for completeness
    originalTx.vin.resize(1);
    originalTx.vin[0].prevout = COutPoint(uint256S("0x1234"), 0);
    originalTx.vout.resize(1);
    originalTx.vout[0].nValue = 1000;
    originalTx.nLockTime = 100;

    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    CTransaction original(originalTx);
    ss << original;

    // Deserialize
    CTransaction deserialized(deserialize, ss);

    // Verify DigiDollar properties preserved
    BOOST_CHECK(IsDigiDollarTransaction(deserialized));
    BOOST_CHECK_EQUAL(GetDigiDollarTxType(deserialized), DD_TX_TRANSFER);
    BOOST_CHECK_EQUAL(GetDigiDollarFlags(deserialized), 0xAB);

    // Verify other properties
    BOOST_CHECK_EQUAL(original.GetHash(), deserialized.GetHash());
    BOOST_CHECK_EQUAL(original.nLockTime, deserialized.nLockTime);
}

// =====================================
// Edge Cases and Error Handling
// =====================================

BOOST_AUTO_TEST_CASE(version_encoding_edge_cases)
{
    // Test with maximum values
    uint8_t maxFlags = 0xFF;
    int32_t version = MakeDigiDollarVersion(DD_TX_REDEEM, maxFlags);

    BOOST_CHECK_EQUAL((version & DD_VERSION_MASK), (DD_TX_VERSION & DD_VERSION_MASK));
    BOOST_CHECK_EQUAL((version & DD_TYPE_MASK) >> 24, static_cast<uint8_t>(DD_TX_REDEEM));
    BOOST_CHECK_EQUAL((version & DD_FLAGS_MASK) >> 16, maxFlags);
}

BOOST_AUTO_TEST_CASE(backward_compatibility)
{
    // Ensure DigiDollar transactions don't interfere with standard transaction processing
    CMutableTransaction stdTx;
    stdTx.nVersion = 1; // Old version

    CTransaction tx1(stdTx);
    BOOST_CHECK(!IsDigiDollarTransaction(tx1));
    BOOST_CHECK_EQUAL(GetDigiDollarTxType(tx1), DD_TX_NONE);
    BOOST_CHECK_EQUAL(GetDigiDollarFlags(tx1), 0);

    stdTx.nVersion = 2; // Current version
    CTransaction tx2(stdTx);
    BOOST_CHECK(!IsDigiDollarTransaction(tx2));
    BOOST_CHECK_EQUAL(GetDigiDollarTxType(tx2), DD_TX_NONE);
    BOOST_CHECK_EQUAL(GetDigiDollarFlags(tx2), 0);
}

BOOST_AUTO_TEST_CASE(collision_detection)
{
    // Ensure DigiDollar version marker doesn't collide with standard versions
    std::vector<int32_t> standardVersions = {1, 2, 3};

    for (auto ver : standardVersions) {
        // No standard version should match DigiDollar marker
        BOOST_CHECK_NE((ver & DD_VERSION_MASK), (DD_TX_VERSION & DD_VERSION_MASK));
    }

    // Test that all possible DigiDollar versions maintain the marker
    for (uint8_t type = 1; type < 4; ++type) {
        for (uint8_t flags = 0; flags < 3; ++flags) { // Test a few flag values
            int32_t ddVersion = MakeDigiDollarVersion(static_cast<DigiDollarTxType>(type), flags);
            BOOST_CHECK_EQUAL((ddVersion & DD_VERSION_MASK), (DD_TX_VERSION & DD_VERSION_MASK));
        }
    }
}

// =====================================
// Phase 3: Comprehensive Transaction Tests (TDD Implementation)
// =====================================

// Test fixture for DigiDollar transaction testing
struct DigiDollarTransactionTestFixture {
    DigiDollar::ConsensusParams consensusParams;

    DigiDollarTransactionTestFixture() {
        // Initialize with default consensus parameters
    }

    // Helper to create test amounts
    // NOTE: Returns cents (100 cents = $1.00), NOT satoshis
    CAmount CreateTestAmount(double dollars) {
        return static_cast<CAmount>(dollars * 100);
    }

    // Helper to create test lock periods
    int64_t CreateTestLockPeriod(int days) {
        return DigiDollar::LockDaysToBlocks(days);
    }

    // Helper to verify collateral calculations
    bool VerifyCollateralSufficiency(CAmount ddAmount, CAmount dgbCollateral, CAmount dgbPrice, int requiredRatio) {
        return ValidateCollateralRatio(ddAmount, dgbCollateral, dgbPrice, requiredRatio);
    }
};

BOOST_FIXTURE_TEST_CASE(test_mint_amount_validation, DigiDollarTransactionTestFixture)
{
    // Test amount boundaries with clear test cases
    struct MintAmountTest {
        double amount;
        bool shouldPass;
        std::string description;
    };

    std::vector<MintAmountTest> testCases = {
        {50.0,   false, "Below minimum ($100)"},
        {99.99,  false, "Just below minimum"},
        {100.0,  true,  "Minimum valid amount"},
        {1000.0, true,  "Standard amount"},
        {99999.0, true, "Just below maximum"},
        {100000.0, true, "Maximum valid amount"},
        {100001.0, false, "Just above maximum"},
        {200000.0, false, "Well above maximum"}
    };

    const auto& ddParams = Params().GetDigiDollarParams();
    for (const auto& test : testCases) {
        CAmount amount = CreateTestAmount(test.amount);
        bool result = ValidateMintAmount(amount, ddParams);
        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "Amount validation failed for " + test.description +
            " ($" + std::to_string(test.amount) + ")");
    }
}

BOOST_FIXTURE_TEST_CASE(test_lock_tier_validation, DigiDollarTransactionTestFixture)
{
    // Test all 8 lock tiers with expected collateral ratios
    struct LockTierTest {
        int days;
        int expectedMinRatio;
        int expectedMaxRatio;
        std::string description;
    };

    std::vector<LockTierTest> lockTiers = {
        {30,   450, 550, "30 days (shortest)"},
        {90,   350, 450, "3 months"},
        {180,  300, 400, "6 months"},
        {365,  250, 350, "1 year"},
        {1095, 200, 300, "3 years"},
        {1825, 200, 250, "5 years"},
        {2555, 200, 225, "7 years"},
        {3650, 200, 225, "10 years (longest)"}
    };

    for (const auto& tier : lockTiers) {
        int64_t lockBlocks = CreateTestLockPeriod(tier.days);
        int ratio = DigiDollar::GetCollateralRatioForLockTime(lockBlocks, consensusParams);

        BOOST_CHECK_MESSAGE(ratio >= tier.expectedMinRatio && ratio <= tier.expectedMaxRatio,
            "Collateral ratio out of expected range for " + tier.description +
            " (got " + std::to_string(ratio) + "%, expected " +
            std::to_string(tier.expectedMinRatio) + "-" + std::to_string(tier.expectedMaxRatio) + "%)");
    }
}

BOOST_FIXTURE_TEST_CASE(test_collateral_ratio_validation, DigiDollarTransactionTestFixture)
{
    CAmount ddAmount = CreateTestAmount(1000.0); // $1000 DD = 100000 cents
    CAmount oraclePrice = 5; // $0.05 per DGB (5 cents per DGB)

    struct CollateralTest {
        double dgbAmount;
        int requiredRatio;
        bool shouldPass;
        std::string description;
    };

    std::vector<CollateralTest> tests = {
        {50000.0,  200, true,  "Sufficient collateral (200% ratio)"},
        {75000.0,  300, true,  "Sufficient collateral (300% ratio)"},
        {25000.0,  200, false, "Insufficient collateral (200% ratio)"},
        {40000.0,  300, false, "Insufficient collateral (300% ratio)"},
        {100000.0, 400, true,  "Excess collateral (400% ratio)"}
    };

    for (const auto& test : tests) {
        CAmount dgbCollateral = static_cast<CAmount>(test.dgbAmount * COIN);
        bool result = VerifyCollateralSufficiency(ddAmount, dgbCollateral, oraclePrice, test.requiredRatio);

        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "Collateral validation failed for " + test.description);
    }
}

BOOST_FIXTURE_TEST_CASE(test_oracle_price_validation, DigiDollarTransactionTestFixture)
{
    struct PriceTest {
        CAmount price;
        bool shouldPass;
        std::string description;
    };

    std::vector<PriceTest> priceTests = {
        {0,        false, "Zero price"},
        {-100,     false, "Negative price"},
        {50,       false, "Below minimum (0.5 cents)"},
        {100,      true,  "Minimum valid (1 cent)"},
        {5000,     true,  "Normal price (50 cents)"},
        {10000,    true,  "High price ($1.00)"},
        {1000000,  true,  "Maximum price ($10.00)"},
        {1000001,  false, "Above maximum price"},
        {2000000,  false, "Way above maximum"}
    };

    for (const auto& test : priceTests) {
        bool result = ValidateOraclePrice(test.price);
        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "Oracle price validation failed for " + test.description +
            " (price: " + std::to_string(test.price) + " cents)");
    }
}

BOOST_FIXTURE_TEST_CASE(test_dd_conservation_validation, DigiDollarTransactionTestFixture)
{
    // Test DigiDollar conservation laws (inputs = outputs + fees)
    struct ConservationTest {
        CAmount inputs;
        CAmount outputs;
        CAmount fees;
        bool shouldPass;
        std::string description;
    };

    std::vector<ConservationTest> tests = {
        {CreateTestAmount(1000), CreateTestAmount(900), CreateTestAmount(100), true,  "Balanced transaction"},
        {CreateTestAmount(1000), CreateTestAmount(1000), CreateTestAmount(0), true,  "No-fee transaction"},
        {CreateTestAmount(800),  CreateTestAmount(900), CreateTestAmount(100), false, "Insufficient inputs"},
        {CreateTestAmount(1200), CreateTestAmount(900), CreateTestAmount(100), false, "Excessive inputs (money creation)"},
        {CreateTestAmount(500),  CreateTestAmount(600), CreateTestAmount(0),   false, "Outputs exceed inputs"},
        {CreateTestAmount(1000), CreateTestAmount(950), CreateTestAmount(50),  true,  "Small fee transaction"}
    };

    for (const auto& test : tests) {
        bool result = ValidateDDConservation(test.inputs, test.outputs, test.fees);
        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "DD conservation validation failed for " + test.description);
    }
}

BOOST_FIXTURE_TEST_CASE(test_dd_address_validation, DigiDollarTransactionTestFixture)
{
    struct AddressTest {
        std::string address;
        bool shouldPass;
        std::string description;
    };

    std::vector<AddressTest> addressTests = {
        {"dd1qw508d6qejxtdg4y5r3zarvary0c5xw7k3k4k4k", true,  "Valid DD address"},
        {"dd1q", true, "Short valid DD address"},
        {"dd1" + std::string(50, 'a'), true, "Long valid DD address"},
        {"invalid_dd_address", false, "Invalid format"},
        {"dgb1qw508d6qejxtdg4y5r3zarvary0c5xw7k3k4k4k", false, "DGB address (not DD)"},
        {"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7k3k4k4k", false, "Bitcoin address"},
        {"dd", false, "Too short"},
        {"", false, "Empty address"},
        {"DD1qw508d6qejxtdg4y5r3zarvary0c5xw7k3k4k4k", false, "Wrong case"}
    };

    for (const auto& test : addressTests) {
        bool result = ValidateDDAddress(test.address);
        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "Address validation failed for " + test.description + " (" + test.address + ")");
    }
}

BOOST_FIXTURE_TEST_CASE(test_double_spend_detection, DigiDollarTransactionTestFixture)
{
    // Create test outpoints
    COutPoint outpoint1{uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0};
    COutPoint outpoint2{uint256S("fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321"), 1};
    COutPoint outpoint3{uint256S("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"), 2};

    struct DoubleSpendTest {
        std::vector<COutPoint> inputs1;
        std::vector<COutPoint> inputs2;
        bool shouldDetectConflict;
        std::string description;
    };

    std::vector<DoubleSpendTest> tests = {
        {{outpoint1}, {outpoint2}, false, "No common inputs"},
        {{outpoint1}, {outpoint1}, true,  "Identical single input"},
        {{outpoint1, outpoint2}, {outpoint1}, true, "Overlapping inputs"},
        {{outpoint1, outpoint2}, {outpoint2, outpoint3}, true, "One common input"},
        {{outpoint1}, {outpoint2, outpoint3}, false, "Disjoint input sets"},
        {{}, {outpoint1}, false, "Empty first set"},
        {{outpoint1}, {}, false, "Empty second set"},
        {{}, {}, false, "Both sets empty"}
    };

    for (const auto& test : tests) {
        bool hasConflict = ValidateNoDoubleSpend(test.inputs1, test.inputs2);
        BOOST_CHECK_MESSAGE(hasConflict == test.shouldDetectConflict,
            "Double spend detection failed for " + test.description);
    }
}

BOOST_FIXTURE_TEST_CASE(test_utxo_selection_algorithm, DigiDollarTransactionTestFixture)
{
    struct UTXOSelectionTest {
        std::vector<CAmount> availableAmounts;
        CAmount targetAmount;
        bool shouldSucceed;
        std::string description;
    };

    std::vector<UTXOSelectionTest> tests = {
        {{500, 300, 200, 100}, 750, true,  "Standard selection"},
        {{1000}, 500, true, "Single large UTXO"},
        {{100, 200, 300}, 550, true, "Multiple small UTXOs"},
        {{100, 200}, 500, false, "Insufficient total"},
        {{}, 100, false, "No UTXOs available"},
        {{1000, 500, 300}, 0, true, "Zero target (should succeed)"},
        {{250, 250, 250, 250}, 1000, true, "Exact match possible"}
    };

    for (const auto& test : tests) {
        std::vector<size_t> selected = SelectDDUTXOs(test.availableAmounts, test.targetAmount);

        CAmount selectedTotal = 0;
        for (size_t idx : selected) {
            if (idx < test.availableAmounts.size()) {
                selectedTotal += test.availableAmounts[idx];
            }
        }

        bool succeeded = selectedTotal >= test.targetAmount;
        BOOST_CHECK_MESSAGE(succeeded == test.shouldSucceed,
            "UTXO selection failed for " + test.description +
            " (selected: " + std::to_string(selectedTotal) +
            ", target: " + std::to_string(test.targetAmount) + ")");
    }
}

BOOST_FIXTURE_TEST_CASE(test_redemption_path_validation, DigiDollarTransactionTestFixture)
{
    const int currentHeight = 1000000;
    const int expiredLock = 999000;   // 1000 blocks ago
    const int activeLock = 1001000;   // 1000 blocks in future

    struct RedemptionPathTest {
        DigiDollarTxType type;
        int lockHeight;
        bool errActive;
        bool shouldPass;
        std::string description;
    };

    std::vector<RedemptionPathTest> tests = {
        {DD_TX_REDEEM, expiredLock, false, true,  "Normal redeem with expired lock"},
        {DD_TX_REDEEM, activeLock,  false, false, "Normal redeem with active lock"}
    };

    for (const auto& test : tests) {
        bool result = ValidateRedemptionPath(test.type, currentHeight, test.lockHeight, test.errActive);
        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "Redemption path validation failed for " + test.description);
    }
}

BOOST_FIXTURE_TEST_CASE(test_redemption_amount_validation, DigiDollarTransactionTestFixture)
{
    const CAmount totalHeld = CreateTestAmount(1000.0); // $1000 DD held

    struct RedemptionAmountTest {
        double redeemAmount;
        bool isFullRedeem;
        bool shouldPass;
        std::string description;
    };

    // NOTE: DigiDollar only supports FULL redemption - no partial redemption allowed
    std::vector<RedemptionAmountTest> tests = {
        {1000.0, true,  true,  "Full redemption (exact match)"},
        // DELETED: Partial redemption tests - partial redemption does not exist
        // Only full redemption is allowed in DigiDollar
        {1200.0, false, false, "Over-redemption (120%) - MUST FAIL"},
        {999.0,  true,  false, "Full redemption (not exact) - MUST FAIL"},
        {1001.0, true,  false, "Full redemption (over amount) - MUST FAIL"},
        {0.0,    false, false, "Zero redemption - MUST FAIL"},
        {-100.0, false, false, "Negative redemption - MUST FAIL"},
        {500.0,  false, false, "Partial redemption (50%) - MUST FAIL (no partial allowed)"},
        {100.0,  false, false, "Small partial redemption (10%) - MUST FAIL (no partial allowed)"}
    };

    for (const auto& test : tests) {
        CAmount redeemAmount = CreateTestAmount(test.redeemAmount);
        bool result = ValidateRedemptionAmount(redeemAmount, totalHeld, test.isFullRedeem);
        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "Redemption amount validation failed for " + test.description +
            " ($" + std::to_string(test.redeemAmount) + ")");
    }
}

BOOST_FIXTURE_TEST_CASE(test_timelock_validation, DigiDollarTransactionTestFixture)
{
    const int currentHeight = 1000000;
    const int shortLockBlocks = CreateTestLockPeriod(30);   // 30 days
    const int longLockBlocks = CreateTestLockPeriod(365);   // 1 year

    struct TimelockTest {
        DigiDollarTxType type;
        int lockHeight;
        bool errActive;
        bool shouldPass;
        std::string description;
    };

    std::vector<TimelockTest> tests = {
        {DD_TX_REDEEM, currentHeight - shortLockBlocks, false, true,  "Normal redeem, short lock expired"},
        {DD_TX_REDEEM, currentHeight + shortLockBlocks, false, false, "Normal redeem, short lock active"},
        {DD_TX_REDEEM, currentHeight - longLockBlocks,  false, true,  "Normal redeem, long lock expired"},
        {DD_TX_REDEEM, currentHeight + longLockBlocks,  false, false, "Normal redeem, long lock active"}
    };

    for (const auto& test : tests) {
        bool result = ValidateTimelockForRedeem(test.type, currentHeight, test.lockHeight, test.errActive);
        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "Timelock validation failed for " + test.description);
    }
}

BOOST_FIXTURE_TEST_CASE(test_err_activation_conditions, DigiDollarTransactionTestFixture)
{
    struct ERRTest {
        int collateralPercentage;
        bool shouldActivate;
        std::string description;
    };

    std::vector<ERRTest> tests = {
        {90,  true,  "Well below threshold (90%)"},
        {95,  true,  "Below threshold (95%)"},
        {99,  true,  "Just below threshold (99%)"},
        {100, false, "At threshold (100%)"},
        {101, false, "Just above threshold (101%)"},
        {150, false, "Well above threshold (150%)"},
        {200, false, "Very high collateral (200%)"},
        {50,  true,  "Critically low collateral (50%)"},
        {0,   true,  "Zero collateral (emergency)"}
    };

    for (const auto& test : tests) {
        bool result = ShouldActivateERR(test.collateralPercentage);
        BOOST_CHECK_MESSAGE(result == test.shouldActivate,
            "ERR activation check failed for " + test.description +
            " (" + std::to_string(test.collateralPercentage) + "%)");
    }
}

BOOST_FIXTURE_TEST_CASE(test_dd_script_creation_and_validation, DigiDollarTransactionTestFixture)
{
    struct ScriptTest {
        double amount;
        int lockDays;
        bool shouldCreateValid;
        std::string description;
    };

    std::vector<ScriptTest> tests = {
        {1000.0, 365, true,  "Standard 1-year lock"},
        {100.0,  30,  true,  "Minimum amount, short lock"},
        {99999.0, 3650, true, "Large amount, long lock"},
        {0.0,    365, false, "Zero amount (invalid)"},
        {1000.0, 0,   false, "Zero lock time (invalid)"}
    };

    for (const auto& test : tests) {
        CAmount amount = CreateTestAmount(test.amount);
        int64_t lockBlocks = CreateTestLockPeriod(test.lockDays);

        CScript ddScript = CreateDDOutputScript(amount, lockBlocks);

        if (test.shouldCreateValid) {
            BOOST_CHECK_MESSAGE(!ddScript.empty(),
                "DD script creation failed for " + test.description);
            BOOST_CHECK_MESSAGE(ValidateDDScript(ddScript),
                "DD script validation failed for " + test.description);
        }
    }
}

BOOST_FIXTURE_TEST_CASE(test_dd_witness_stack_validation, DigiDollarTransactionTestFixture)
{
    struct WitnessTest {
        std::vector<size_t> elementSizes;
        bool shouldPass;
        std::string description;
    };

    std::vector<WitnessTest> tests = {
        {{8, 8, 64}, true,  "Valid complete witness stack"},
        {{8, 8, 64, 32}, true, "Valid stack with extra element"},
        {{8, 8}, false, "Missing signature"},
        {{8}, false, "Missing lock time and signature"},
        {{}, false, "Empty witness stack"},
        {{4, 8, 64}, false, "Invalid DD amount size"},
        {{8, 4, 64}, false, "Invalid lock time size"},
        {{8, 8, 32}, false, "Invalid signature size"},
        {{8, 8, 64, 0}, false, "Extra empty element"}
    };

    for (const auto& test : tests) {
        std::vector<std::vector<unsigned char>> witnessStack;

        for (size_t size : test.elementSizes) {
            witnessStack.emplace_back(size, 0x42); // Fill with test data
        }

        bool result = ValidateDDWitnessStack(witnessStack);
        BOOST_CHECK_MESSAGE(result == test.shouldPass,
            "Witness stack validation failed for " + test.description);
    }
}

BOOST_FIXTURE_TEST_CASE(test_dd_script_execution, DigiDollarTransactionTestFixture)
{
    struct ScriptExecutionTest {
        std::vector<opcodetype> opcodes;
        bool shouldSucceed;
        size_t expectedStackSize;
        std::string description;
    };

    std::vector<ScriptExecutionTest> tests = {
        {{OP_1, OP_2, OP_ADD, OP_3, OP_EQUAL}, true, 1, "Simple arithmetic: 1+2==3"},
        {{OP_2, OP_2, OP_ADD, OP_4, OP_EQUAL}, true, 1, "Simple arithmetic: 2+2==4"},
        {{OP_1, OP_2, OP_EQUAL}, false, 1, "False comparison: 1==2"},
        {{OP_3, OP_3, OP_EQUAL}, true, 1, "True comparison: 3==3"},
        {{OP_1}, true, 1, "Single value push"},
        {{OP_1, OP_2}, true, 2, "Two value pushes"},
        {{}, true, 0, "Empty script"},
        {{OP_1, OP_2, OP_ADD}, true, 1, "Addition without comparison"}
    };

    for (const auto& test : tests) {
        CScript script;
        for (opcodetype op : test.opcodes) {
            script << op;
        }

        ScriptExecutionResult result = ExecuteDDScript(script);

        BOOST_CHECK_MESSAGE(result.success == test.shouldSucceed,
            "Script execution success mismatch for " + test.description +
            " (expected: " + (test.shouldSucceed ? "true" : "false") +
            ", got: " + (result.success ? "true" : "false") + ")");

        BOOST_CHECK_MESSAGE(result.stackSize == test.expectedStackSize,
            "Script execution stack size mismatch for " + test.description +
            " (expected: " + std::to_string(test.expectedStackSize) +
            ", got: " + std::to_string(result.stackSize) + ")");
    }
}

BOOST_FIXTURE_TEST_CASE(test_dd_opcode_validation, DigiDollarTransactionTestFixture)
{
    struct OpcodeTest {
        opcodetype opcode;
        bool shouldBeValid;
        std::string description;
    };

    std::vector<OpcodeTest> tests = {
        // Allowed opcodes
        {OP_1, true, "Number constant"},
        {OP_ADD, true, "Arithmetic operation"},
        {OP_EQUAL, true, "Comparison operation"},
        {OP_DUP, true, "Stack manipulation"},
        {OP_CHECKSIG, true, "Signature verification"},
        {OP_HASH256, true, "Hash operation"},
        {OP_IF, true, "Conditional operation"},

        // Disallowed opcodes
        {OP_CHECKMULTISIG, false, "Multisig (not allowed)"},
        {OP_CHECKMULTISIGVERIFY, false, "Multisig verify (not allowed)"},
        {OP_CHECKLOCKTIMEVERIFY, false, "Locktime verify (not allowed)"},
        {OP_CHECKSEQUENCEVERIFY, false, "Sequence verify (not allowed)"}
    };

    for (const auto& test : tests) {
        bool result = ValidateDDOpcode(test.opcode);
        BOOST_CHECK_MESSAGE(result == test.shouldBeValid,
            "Opcode validation failed for " + test.description +
            " (opcode: " + std::to_string(static_cast<int>(test.opcode)) + ")");
    }
}

// Note: Function implementations are now in digidollar_transaction_validation.cpp

BOOST_AUTO_TEST_SUITE_END()