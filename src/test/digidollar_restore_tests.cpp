// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <wallet/digidollarwallet.h>
#include <test/util/setup_common.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/interpreter.h>
#include <consensus/digidollar.h>
#include <key.h>
#include <pubkey.h>
#include <random.h>

using namespace DigiDollar;

BOOST_FIXTURE_TEST_SUITE(digidollar_restore_tests, TestingSetup)

// Helper function to create a test key
static CKey CreateTestKey() {
    CKey key;
    key.MakeNewKey(true);
    return key;
}

// Helper function to create a mock mint transaction with OP_RETURN (v2 format with tier)
static CMutableTransaction CreateMockMintTx(CAmount dd_amount, int64_t unlock_height, uint32_t lock_tier) {
    CMutableTransaction mtx;
    mtx.SetDigiDollarType(::DD_TX_MINT);

    // Add a dummy input
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0);

    // vout[0]: P2TR Collateral Lock (nValue = dgb_collateral in satoshis)
    // Using a simple P2TR output for testing
    CKey ownerKey = CreateTestKey();
    XOnlyPubKey xonly(ownerKey.GetPubKey());
    WitnessV1Taproot taproot_dest(xonly);
    CScript collateralScript = GetScriptForDestination(taproot_dest);
    mtx.vout.push_back(CTxOut(100 * COIN, collateralScript));

    // vout[1]: P2TR DD Token (nValue = 0, DD amount in OP_RETURN)
    CScript ddScript = GetScriptForDestination(taproot_dest);
    mtx.vout.push_back(CTxOut(0, ddScript));

    // vout[2]: OP_RETURN Metadata (v2 format with explicit tier)
    // Format: OP_RETURN <"DD"> <txType> <ddAmount> <lockHeight> <lockTier>
    CScript metadataScript = CScript() << OP_RETURN
                                       << std::vector<unsigned char>{'D', 'D'}
                                       << CScriptNum(1)  // 1 = MINT transaction
                                       << CScriptNum(dd_amount)  // DD amount in cents
                                       << CScriptNum(unlock_height)  // Lock height in blocks
                                       << CScriptNum(lock_tier);  // Lock tier (0-8)
    mtx.vout.push_back(CTxOut(0, metadataScript));

    return mtx;
}

// ============================================================================
// Test 1: Extract DD amount from OP_RETURN metadata
// ============================================================================

BOOST_AUTO_TEST_CASE(extract_dd_amount_from_opreturn)
{
    // Test extracting DD amount from a mint transaction OP_RETURN
    CAmount expected_amount = 10000;  // $100.00 in cents
    int64_t unlock_height = 100000;
    uint32_t lock_tier = 1;  // 30 days

    CMutableTransaction mtx = CreateMockMintTx(expected_amount, unlock_height, lock_tier);
    CTransaction tx(mtx);

    // Test extraction
    CAmount extracted_amount = 0;
    bool result = DigiDollarWallet::ExtractDDAmountFromOpReturn(tx, extracted_amount);

    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(extracted_amount, expected_amount);
}

BOOST_AUTO_TEST_CASE(extract_dd_amount_invalid_tx)
{
    // Test with a transaction that has no OP_RETURN
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0);

    // Just add a normal output, no OP_RETURN
    CScript normalScript = CScript() << OP_TRUE;
    mtx.vout.push_back(CTxOut(100 * COIN, normalScript));

    CTransaction tx(mtx);
    CAmount extracted_amount = 0;
    bool result = DigiDollarWallet::ExtractDDAmountFromOpReturn(tx, extracted_amount);

    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(extracted_amount, 0);
}

BOOST_AUTO_TEST_CASE(extract_dd_amount_wrong_marker)
{
    // Test with wrong marker (not "DD")
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0);

    CScript badOpReturn = CScript() << OP_RETURN
                                    << std::vector<unsigned char>{'B', 'T'}
                                    << CScriptNum(1)
                                    << CScriptNum(10000);
    mtx.vout.push_back(CTxOut(0, badOpReturn));

    CTransaction tx(mtx);
    CAmount extracted_amount = 0;
    bool result = DigiDollarWallet::ExtractDDAmountFromOpReturn(tx, extracted_amount);

    BOOST_CHECK(!result);
}

// ============================================================================
// Test 2: Extract unlock height from OP_RETURN metadata
// ============================================================================

BOOST_AUTO_TEST_CASE(extract_unlock_height_from_opreturn)
{
    // Test extracting unlock height from a mint transaction OP_RETURN
    CAmount dd_amount = 10000;  // $100.00 in cents
    int64_t expected_unlock_height = 518640;  // 90 days from height 240 (518640 - 240 = 518400 blocks)
    uint32_t lock_tier = 2;  // 90 days

    CMutableTransaction mtx = CreateMockMintTx(dd_amount, expected_unlock_height, lock_tier);
    CTransaction tx(mtx);

    // Test extraction
    int64_t extracted_height = 0;
    bool result = DigiDollarWallet::ExtractUnlockHeightFromOpReturn(tx, extracted_height);

    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(extracted_height, expected_unlock_height);
}

BOOST_AUTO_TEST_CASE(extract_unlock_height_invalid_tx)
{
    // Test with a transaction that has no OP_RETURN
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0);

    CScript normalScript = CScript() << OP_TRUE;
    mtx.vout.push_back(CTxOut(100 * COIN, normalScript));

    CTransaction tx(mtx);
    int64_t extracted_height = 0;
    bool result = DigiDollarWallet::ExtractUnlockHeightFromOpReturn(tx, extracted_height);

    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(extracted_height, 0);
}

// ============================================================================
// Test 2.5: Extract lock tier from OP_RETURN metadata (new v2 format)
// ============================================================================

BOOST_AUTO_TEST_CASE(extract_tier_from_opreturn)
{
    // Test extracting lock tier from a v2 mint transaction OP_RETURN
    CAmount dd_amount = 10000;
    int64_t unlock_height = 173800;
    uint32_t expected_tier = 1;  // 30 days

    CMutableTransaction mtx = CreateMockMintTx(dd_amount, unlock_height, expected_tier);
    CTransaction tx(mtx);

    uint32_t extracted_tier = 0;
    bool result = DigiDollarWallet::ExtractTierFromOpReturn(tx, extracted_tier);

    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(extracted_tier, expected_tier);
}

BOOST_AUTO_TEST_CASE(extract_tier_from_opreturn_all_tiers)
{
    // Test tier extraction for all valid tiers (0-8)
    for (uint32_t tier = 0; tier <= 8; ++tier) {
        CMutableTransaction mtx = CreateMockMintTx(10000, 100000, tier);
        CTransaction tx(mtx);

        uint32_t extracted_tier = 99;  // Invalid value to verify extraction works
        bool result = DigiDollarWallet::ExtractTierFromOpReturn(tx, extracted_tier);

        BOOST_CHECK_MESSAGE(result, "Failed to extract tier " + std::to_string(tier));
        BOOST_CHECK_EQUAL(extracted_tier, tier);
    }
}

BOOST_AUTO_TEST_CASE(extract_tier_invalid_tx)
{
    // Test with a transaction that has no tier in OP_RETURN (old format)
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0);

    // Create old format OP_RETURN (without tier)
    CScript metadataScript = CScript() << OP_RETURN
                                       << std::vector<unsigned char>{'D', 'D'}
                                       << CScriptNum(1)
                                       << CScriptNum(10000)
                                       << CScriptNum(100000);  // No tier field
    mtx.vout.push_back(CTxOut(0, metadataScript));

    CTransaction tx(mtx);
    uint32_t extracted_tier = 0;
    bool result = DigiDollarWallet::ExtractTierFromOpReturn(tx, extracted_tier);

    // Should fail for old format - tier is REQUIRED
    BOOST_CHECK(!result);
}

// ============================================================================
// Test 3: Derive lock tier from mint height and unlock height (DEPRECATED)
// ============================================================================

BOOST_AUTO_TEST_CASE(derive_lock_tier_from_heights)
{
    // Test tier derivation for all standard lock tiers

    // Tier 0: 1 hour (240 blocks)
    uint32_t tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 1240);
    BOOST_CHECK_EQUAL(tier, 0);

    // Tier 1: 30 days (172,800 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 173800);
    BOOST_CHECK_EQUAL(tier, 1);

    // Tier 2: 90 days (518,400 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 519400);
    BOOST_CHECK_EQUAL(tier, 2);

    // Tier 3: 180 days (1,036,800 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 1037800);
    BOOST_CHECK_EQUAL(tier, 3);

    // Tier 4: 365 days (2,102,400 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 2103400);
    BOOST_CHECK_EQUAL(tier, 4);

    // Tier 5: 3 years = 1095 days (6,307,200 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 6308200);
    BOOST_CHECK_EQUAL(tier, 5);

    // Tier 6: 5 years = 1825 days (10,512,000 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 10513000);
    BOOST_CHECK_EQUAL(tier, 6);

    // Tier 7: 7 years = 2555 days (14,716,800 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 14717800);
    BOOST_CHECK_EQUAL(tier, 7);

    // Tier 8: 10 years = 3650 days (21,024,000 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 21025000);
    BOOST_CHECK_EQUAL(tier, 8);
}

BOOST_AUTO_TEST_CASE(derive_lock_tier_edge_cases)
{
    // Test edge case: exact tier boundaries
    //
    // NOTE: DeriveLockTierFromHeight is DEPRECATED for position reconstruction.
    // New MINT transactions store tier explicitly in OP_RETURN.
    // This function uses exact thresholds for diagnostic/validation purposes.
    //
    // Lock tier block thresholds:
    //   Tier 0: < 172,800 blocks (< 30 days)
    //   Tier 1: >= 172,800 blocks (30 days)
    //   Tier 2: >= 518,400 blocks (90 days)
    //   etc.

    // Well under 30 days (172,799 blocks) - tier 0
    uint32_t tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 173799);
    BOOST_CHECK_EQUAL(tier, 0);

    // Exactly 30 days (172,800 blocks) - tier 1
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 173800);
    BOOST_CHECK_EQUAL(tier, 1);

    // Just over 30 days (172,801 blocks) - tier 1
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 173801);
    BOOST_CHECK_EQUAL(tier, 1);

    // Between tiers - values in this range are mapped to the lower tier
    // (300,000 blocks is between 172,800 and 518,400 so tier 1)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 300000);  // Between 30d and 90d
    BOOST_CHECK_EQUAL(tier, 1);

    // Exactly 90 days (518,400 blocks) - tier 2
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 519400);
    BOOST_CHECK_EQUAL(tier, 2);

    // Test tier 0 boundary (1 hour = 240 blocks)
    tier = DigiDollarWallet::DeriveLockTierFromHeight(1000, 1240);
    BOOST_CHECK_EQUAL(tier, 0);
}

// ============================================================================
// Test 4: Build complete WalletCollateralPosition from mint transaction
// ============================================================================

BOOST_AUTO_TEST_CASE(build_position_from_mint_tx)
{
    // Create a test wallet
    DigiDollarWallet wallet;

    // Test parameters
    CAmount dd_amount = 50000;  // $500.00 in cents
    int64_t mint_height = 1000;
    int64_t unlock_height = mint_height + LockDaysToBlocks(90);  // 90 day lock
    uint32_t expected_tier = 2;  // 90 days = tier 2

    // Create mock mint transaction (v2 format with explicit tier)
    CMutableTransaction mtx = CreateMockMintTx(dd_amount, unlock_height, expected_tier);
    CTransaction tx(mtx);

    // Extract position
    WalletCollateralPosition position;
    bool result = wallet.ExtractPositionFromMintTx(tx, mint_height, position);

    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(position.dd_timelock_id, tx.GetHash());
    BOOST_CHECK_EQUAL(position.dd_minted, dd_amount);
    BOOST_CHECK_EQUAL(position.dgb_collateral, 100 * COIN);  // From vout[0]
    BOOST_CHECK_EQUAL(position.lock_tier, expected_tier);  // Tier from OP_RETURN
    BOOST_CHECK_EQUAL(position.unlock_height, unlock_height);
    BOOST_CHECK(position.is_active);
}

BOOST_AUTO_TEST_CASE(build_position_invalid_tx)
{
    // Create a test wallet
    DigiDollarWallet wallet;

    // Create invalid transaction (no OP_RETURN)
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"), 0);

    CScript normalScript = CScript() << OP_TRUE;
    mtx.vout.push_back(CTxOut(100 * COIN, normalScript));

    CTransaction tx(mtx);

    // Try to extract position
    WalletCollateralPosition position;
    bool result = wallet.ExtractPositionFromMintTx(tx, 1000, position);

    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(build_position_different_tiers)
{
    // Test position extraction for different lock tiers
    // Tier is now stored explicitly in OP_RETURN (not derived from block heights)
    DigiDollarWallet wallet;

    int64_t mint_height = 5000;

    // Test tier 1 (30 days)
    {
        CAmount dd_amount = 10000;
        uint32_t expected_tier = 1;
        int64_t unlock_height = mint_height + LockDaysToBlocks(30);
        CMutableTransaction mtx = CreateMockMintTx(dd_amount, unlock_height, expected_tier);
        CTransaction tx(mtx);

        WalletCollateralPosition position;
        bool result = wallet.ExtractPositionFromMintTx(tx, mint_height, position);

        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(position.lock_tier, expected_tier);
        BOOST_CHECK_EQUAL(position.dd_minted, dd_amount);
    }

    // Test tier 4 (365 days)
    {
        CAmount dd_amount = 100000;
        uint32_t expected_tier = 4;
        int64_t unlock_height = mint_height + LockDaysToBlocks(365);
        CMutableTransaction mtx = CreateMockMintTx(dd_amount, unlock_height, expected_tier);
        CTransaction tx(mtx);

        WalletCollateralPosition position;
        bool result = wallet.ExtractPositionFromMintTx(tx, mint_height, position);

        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(position.lock_tier, expected_tier);
        BOOST_CHECK_EQUAL(position.dd_minted, dd_amount);
    }

    // Test tier 8 (10 years)
    {
        CAmount dd_amount = 500000;
        uint32_t expected_tier = 8;
        int64_t unlock_height = mint_height + LockDaysToBlocks(3650);
        CMutableTransaction mtx = CreateMockMintTx(dd_amount, unlock_height, expected_tier);
        CTransaction tx(mtx);

        WalletCollateralPosition position;
        bool result = wallet.ExtractPositionFromMintTx(tx, mint_height, position);

        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(position.lock_tier, expected_tier);
        BOOST_CHECK_EQUAL(position.dd_minted, dd_amount);
    }
}

BOOST_AUTO_TEST_SUITE_END()
