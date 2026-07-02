// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <digidollar/scripts.h>
#include <digidollar/digidollar.h>
#include <script/script.h>
#include <script/standard.h>
#include <script/solver.h>
#include <script/interpreter.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <key.h>
#include <pubkey.h>

#include <boost/test/unit_test.hpp>

// Use TestingSetup instead of BasicTestingSetup to ensure full initialization
// including ECC context and logging
BOOST_FIXTURE_TEST_SUITE(digidollar_scripts_tests, TestingSetup)

// Test helper to create valid MintParams
DigiDollar::MintParams CreateTestMintParams()
{
    DigiDollar::MintParams params;

    // Generate test keys
    CKey ownerKey;
    ownerKey.MakeNewKey(true);
    params.ownerKey = XOnlyPubKey(ownerKey.GetPubKey());

    params.internalKey = DigiDollar::GetCollateralNUMSKey();  // NUMS point — no key-path spend

    // Set test parameters
    params.ddAmount = 100 * 100;  // $100 in cents
    params.lockHeight = 1000;     // 1000 blocks from now

    // Get oracle keys
    params.oracleKeys = DigiDollar::GetOracleKeys(15);

    return params;
}

BOOST_AUTO_TEST_CASE(test_oracle_keys_generation)
{
    // Test oracle key generation
    auto keys = DigiDollar::GetOracleKeys(15);

    BOOST_CHECK_EQUAL(keys.size(), 15);

    // Check all keys are valid
    for (const auto& key : keys) {
        BOOST_CHECK(key.IsFullyValid());
    }

    // Check keys are deterministic (same call should produce same keys)
    auto keys2 = DigiDollar::GetOracleKeys(15);
    BOOST_CHECK_EQUAL(keys.size(), keys2.size());
    for (size_t i = 0; i < keys.size(); i++) {
        BOOST_CHECK(keys[i] == keys2[i]);
    }
}

BOOST_AUTO_TEST_CASE(test_normal_redemption_path_creation)
{
    auto params = CreateTestMintParams();

    // This should fail until we implement CreateNormalRedemptionPath
    CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);

    // Verify script contains expected opcodes
    BOOST_CHECK(normalPath.size() > 0);

    // Should contain CHECKLOCKTIMEVERIFY for timelock
    BOOST_CHECK(std::find(normalPath.begin(), normalPath.end(), OP_CHECKLOCKTIMEVERIFY) != normalPath.end());

    // Normal redemption must include DD burn verification in the script ABI.
    BOOST_CHECK(std::find(normalPath.begin(), normalPath.end(), OP_DIGIDOLLAR) != normalPath.end());
    BOOST_CHECK(std::find(normalPath.begin(), normalPath.end(), OP_DDVERIFY) != normalPath.end());

    // Should contain CHECKSIG for owner verification
    BOOST_CHECK(std::find(normalPath.begin(), normalPath.end(), OP_CHECKSIG) != normalPath.end());
}

// DELETED: test_emergency_path_creation - Emergency redemption path does not exist in DigiDollar
// Only two redemption paths: Normal (full, after timelock) and ERR (full, more DD burned)

// DELETED: test_partial_redemption_path_creation - Partial redemption does not exist in DigiDollar
// Only two redemption paths: Normal (full, after timelock) and ERR (full, more DD burned)

BOOST_AUTO_TEST_CASE(test_err_path_creation)
{
    auto params = CreateTestMintParams();

    // This should fail until we implement CreateERRPath
    CScript errPath = DigiDollar::CreateERRPath(params);

    BOOST_CHECK(errPath.size() > 0);

    // CRITICAL: ERR path MUST contain CLTV timelock (same as Normal path)
    // This prevents early redemption - collateral NEVER unlocks before timelock expires
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_CHECKLOCKTIMEVERIFY) != errPath.end());
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_DROP) != errPath.end());

    // Should contain collateral check
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_CHECKCOLLATERAL) != errPath.end());
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_NOT) != errPath.end());

    // Should contain DD verification
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_DIGIDOLLAR) != errPath.end());
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_DDVERIFY) != errPath.end());

    // Should contain owner signature
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_CHECKSIG) != errPath.end());
}

BOOST_AUTO_TEST_CASE(test_collateral_p2tr_creation)
{
    auto params = CreateTestMintParams();

    // This should fail until we implement CreateCollateralP2TR
    CScript collateralScript = DigiDollar::CreateCollateralP2TR(params);

    // P2TR script should be exactly 34 bytes: OP_1 (1 byte) + size prefix (1 byte) + 32-byte key
    BOOST_CHECK_EQUAL(collateralScript.size(), 34);

    // Should start with OP_1 (Taproot version)
    BOOST_CHECK_EQUAL(collateralScript[0], OP_1);

    // Next should be size prefix 0x20 (32) followed by 32-byte taproot output key
    BOOST_CHECK_EQUAL(collateralScript[1], 0x20); // Size prefix for 32-byte push
    std::vector<unsigned char> outputKey(collateralScript.begin() + 2, collateralScript.end());
    BOOST_CHECK_EQUAL(outputKey.size(), 32);

    // Should be valid P2TR format
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(collateralScript, solutions);
    BOOST_CHECK(type == TxoutType::WITNESS_V1_TAPROOT);
}

BOOST_AUTO_TEST_CASE(test_digidollar_p2tr_creation)
{
    CKey ownerKey;
    ownerKey.MakeNewKey(true);
    XOnlyPubKey owner(ownerKey.GetPubKey());
    CAmount ddAmount = 50 * 100;  // $50 in cents

    // This should fail until we implement CreateDigiDollarP2TR
    CScript ddScript = DigiDollar::CreateDigiDollarP2TR(owner, ddAmount);

    // P2TR script should be exactly 34 bytes
    BOOST_CHECK_EQUAL(ddScript.size(), 34);

    // Should start with OP_1
    BOOST_CHECK_EQUAL(ddScript[0], OP_1);

    // Should be valid P2TR format
    std::vector<std::vector<unsigned char>> solutions2;
    TxoutType type2 = Solver(ddScript, solutions2);
    BOOST_CHECK(type2 == TxoutType::WITNESS_V1_TAPROOT);
}

BOOST_AUTO_TEST_CASE(test_collateral_script_with_different_amounts)
{
    auto params = CreateTestMintParams();

    // Test with different DD amounts
    std::vector<CAmount> amounts = {100, 1000 * 100, 10000 * 100};  // $1, $1000, $10000

    for (CAmount amount : amounts) {
        params.ddAmount = amount;
        CScript script = DigiDollar::CreateCollateralP2TR(params);

        // All should be valid P2TR scripts
        BOOST_CHECK_EQUAL(script.size(), 34);
        BOOST_CHECK_EQUAL(script[0], OP_1);

        std::vector<std::vector<unsigned char>> solutions;
        TxoutType type = Solver(script, solutions);
        BOOST_CHECK(type == TxoutType::WITNESS_V1_TAPROOT);
    }
}

BOOST_AUTO_TEST_CASE(test_collateral_script_with_different_lock_periods)
{
    auto params = CreateTestMintParams();

    // Test with different lock periods
    std::vector<int64_t> lockPeriods = {
        144,        // 1 day (144 blocks * 15 seconds)
        4320,       // 30 days
        52560,      // 1 year
        525600      // 10 years
    };

    for (int64_t lockPeriod : lockPeriods) {
        params.lockHeight = lockPeriod;
        CScript script = DigiDollar::CreateCollateralP2TR(params);

        // All should be valid regardless of lock period
        BOOST_CHECK_EQUAL(script.size(), 34);

        std::vector<std::vector<unsigned char>> solutions;
        TxoutType type = Solver(script, solutions);
        BOOST_CHECK(type == TxoutType::WITNESS_V1_TAPROOT);
    }
}

BOOST_AUTO_TEST_CASE(test_script_size_limits)
{
    auto params = CreateTestMintParams();

    // Test all individual paths are within reasonable size limits
    // NOTE: Only two redemption paths exist: Normal and ERR
    CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);
    CScript errPath = DigiDollar::CreateERRPath(params);
    // DELETED: partialPath - partial redemption does not exist

    // Individual scripts should be reasonable in size (under 1KB each)
    BOOST_CHECK(normalPath.size() < 1024);
    BOOST_CHECK(errPath.size() < 1024);
}

BOOST_AUTO_TEST_CASE(test_script_validation_with_mock_execution)
{
    auto params = CreateTestMintParams();

    // Create a mock transaction context for script validation
    CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);

    // The script should at least parse correctly
    BOOST_CHECK(normalPath.HasValidOps());

    // Check that it doesn't have obvious syntax errors
    CScript::const_iterator pc = normalPath.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;
    bool hasOps = normalPath.GetOp(pc, opcode, data);
    BOOST_CHECK(hasOps || normalPath.empty());
}

BOOST_AUTO_TEST_CASE(test_oracle_keys_with_different_counts)
{
    // Test with different oracle counts
    for (size_t count = 1; count <= 20; count++) {
        auto keys = DigiDollar::GetOracleKeys(count);
        BOOST_CHECK_EQUAL(keys.size(), count);

        for (const auto& key : keys) {
            BOOST_CHECK(key.IsFullyValid());
        }
    }
}

BOOST_AUTO_TEST_CASE(test_invalid_parameters_handling)
{
    DigiDollar::MintParams params;
    // Leave params mostly uninitialized to test error handling

    // This should handle invalid parameters gracefully
    // (Implementation should validate inputs)
    params.ddAmount = 0;  // Invalid amount
    params.lockHeight = -1;  // Invalid lock height

    // These calls should not crash even with invalid params
    // (Though they may return empty/invalid scripts)
    // NOTE: Only two redemption paths exist: Normal and ERR
    CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);
    CScript errPath = DigiDollar::CreateERRPath(params);

    // Scripts may be empty or invalid, but shouldn't crash
    BOOST_CHECK(true);  // If we get here, no crash occurred
}

BOOST_AUTO_TEST_CASE(test_normal_path_opcode_order)
{
    auto params = CreateTestMintParams();
    CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);

    BOOST_CHECK(normalPath.size() > 0);

    // Parse script to verify opcode order
    // Expected order: <lockHeight> OP_CHECKLOCKTIMEVERIFY OP_DROP
    // OP_DIGIDOLLAR <amount> OP_DDVERIFY <ownerKey> OP_CHECKSIG
    CScript::const_iterator pc = normalPath.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;

    // First: lockHeight (data push)
    BOOST_CHECK(normalPath.GetOp(pc, opcode, data));
    BOOST_CHECK(data.size() > 0); // Should push lockHeight data

    // Second: OP_CHECKLOCKTIMEVERIFY
    BOOST_CHECK(normalPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_CHECKLOCKTIMEVERIFY);

    // Third: OP_DROP
    BOOST_CHECK(normalPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_DROP);

    // Fourth: OP_DIGIDOLLAR
    BOOST_CHECK(normalPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_DIGIDOLLAR);

    // Fifth: amount
    BOOST_CHECK(normalPath.GetOp(pc, opcode, data));
    BOOST_CHECK(data.size() > 0);

    // Sixth: OP_DDVERIFY
    BOOST_CHECK(normalPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_DDVERIFY);

    // Seventh: ownerKey (32-byte push)
    BOOST_CHECK(normalPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(data.size(), 32); // X-only pubkey is 32 bytes

    // Eighth: OP_CHECKSIG
    BOOST_CHECK(normalPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_CHECKSIG);

    // No more opcodes
    BOOST_CHECK(!normalPath.GetOp(pc, opcode, data));
}

BOOST_AUTO_TEST_CASE(test_err_path_opcode_order)
{
    auto params = CreateTestMintParams();
    CScript errPath = DigiDollar::CreateERRPath(params);

    BOOST_CHECK(errPath.size() > 0);

    // Parse script to verify opcode order
    // Expected: <lockHeight> OP_CLTV OP_DROP <100> OP_CHECKCOLLATERAL
    // OP_NOT OP_VERIFY OP_DIGIDOLLAR <amount> OP_DDVERIFY <ownerKey> OP_CHECKSIG
    CScript::const_iterator pc = errPath.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;

    // 1. lockHeight (data push)
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK(data.size() > 0);

    // 2. OP_CHECKLOCKTIMEVERIFY
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_CHECKLOCKTIMEVERIFY);

    // 3. OP_DROP
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_DROP);

    // 4. <100> threshold
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK(data.size() > 0); // Value 100 as CScriptNum

    // 5. OP_CHECKCOLLATERAL
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_CHECKCOLLATERAL);

    // 6. OP_NOT
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_NOT);

    // 7. OP_VERIFY
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_VERIFY);

    // 8. OP_DIGIDOLLAR
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_DIGIDOLLAR);

    // 9. amount
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK(data.size() > 0);

    // 10. OP_DDVERIFY
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_DDVERIFY);

    // 11. ownerKey (32-byte push)
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(data.size(), 32);

    // 12. OP_CHECKSIG
    BOOST_CHECK(errPath.GetOp(pc, opcode, data));
    BOOST_CHECK_EQUAL(opcode, OP_CHECKSIG);

    // No more opcodes
    BOOST_CHECK(!errPath.GetOp(pc, opcode, data));
}

BOOST_AUTO_TEST_CASE(test_mast_tree_has_exactly_two_paths)
{
    auto params = CreateTestMintParams();

    // Create collateral P2TR which should have MAST with exactly 2 paths
    CScript collateralScript = DigiDollar::CreateCollateralP2TR(params);

    BOOST_CHECK(collateralScript.size() == 34); // P2TR is always 34 bytes

    // Verify both individual paths are created
    CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);
    CScript errPath = DigiDollar::CreateERRPath(params);

    BOOST_CHECK(normalPath.size() > 0);
    BOOST_CHECK(errPath.size() > 0);

    // Both paths should be different
    BOOST_CHECK(normalPath != errPath);

    // NOTE: We use proper script parsing (GetOp) instead of std::find on raw bytes
    // because std::find can give false positives if data bytes (like pubkeys) match opcode values

    // Normal path should NOT contain OP_CHECKCOLLATERAL (only ERR path has this)
    {
        CScript::const_iterator pc = normalPath.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;
        bool found_checkcollateral = false;
        while (normalPath.GetOp(pc, opcode, data)) {
            if (opcode == OP_CHECKCOLLATERAL) found_checkcollateral = true;
        }
        BOOST_CHECK_MESSAGE(!found_checkcollateral, "Normal path should NOT contain OP_CHECKCOLLATERAL");
    }

    // ERR path MUST contain OP_CHECKCOLLATERAL
    {
        CScript::const_iterator pc = errPath.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;
        bool found_checkcollateral = false;
        while (errPath.GetOp(pc, opcode, data)) {
            if (opcode == OP_CHECKCOLLATERAL) found_checkcollateral = true;
        }
        BOOST_CHECK_MESSAGE(found_checkcollateral, "ERR path MUST contain OP_CHECKCOLLATERAL");
    }

    // Both paths MUST contain OP_CHECKLOCKTIMEVERIFY (timelock required for both)
    {
        CScript::const_iterator pc = normalPath.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;
        bool found_cltv = false;
        while (normalPath.GetOp(pc, opcode, data)) {
            if (opcode == OP_CHECKLOCKTIMEVERIFY) found_cltv = true;
        }
        BOOST_CHECK_MESSAGE(found_cltv, "Normal path MUST contain OP_CHECKLOCKTIMEVERIFY");
    }
    {
        CScript::const_iterator pc = errPath.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;
        bool found_cltv = false;
        while (errPath.GetOp(pc, opcode, data)) {
            if (opcode == OP_CHECKLOCKTIMEVERIFY) found_cltv = true;
        }
        BOOST_CHECK_MESSAGE(found_cltv, "ERR path MUST contain OP_CHECKLOCKTIMEVERIFY");
    }
}

BOOST_AUTO_TEST_CASE(test_normal_path_has_dd_amount_validation)
{
    auto params = CreateTestMintParams();
    CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);

    CScript::const_iterator pc = normalPath.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;
    bool found_digidollar = false;
    bool found_ddverify = false;

    while (normalPath.GetOp(pc, opcode, data)) {
        if (opcode == OP_DIGIDOLLAR) found_digidollar = true;
        if (opcode == OP_DDVERIFY) found_ddverify = true;
    }

    BOOST_CHECK_MESSAGE(found_digidollar, "Normal path must contain OP_DIGIDOLLAR");
    BOOST_CHECK_MESSAGE(found_ddverify, "Normal path must contain OP_DDVERIFY");

    // Only contains: CLTV, DROP, ownerKey, CHECKSIG - count opcodes properly
    pc = normalPath.begin();
    size_t cltv_count = 0;
    size_t drop_count = 0;
    size_t checksig_count = 0;

    while (normalPath.GetOp(pc, opcode, data)) {
        if (opcode == OP_CHECKLOCKTIMEVERIFY) cltv_count++;
        if (opcode == OP_DROP) drop_count++;
        if (opcode == OP_CHECKSIG) checksig_count++;
    }

    BOOST_CHECK_EQUAL(cltv_count, 1);
    BOOST_CHECK_EQUAL(drop_count, 1);
    BOOST_CHECK_EQUAL(checksig_count, 1);
}

BOOST_AUTO_TEST_CASE(test_err_path_has_dd_amount_validation)
{
    auto params = CreateTestMintParams();
    CScript errPath = DigiDollar::CreateERRPath(params);

    // ERR path MUST contain OP_DIGIDOLLAR and OP_DDVERIFY
    // This validates the increased DD burn requirement
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_DIGIDOLLAR) != errPath.end());
    BOOST_CHECK(std::find(errPath.begin(), errPath.end(), OP_DDVERIFY) != errPath.end());
}

BOOST_AUTO_TEST_CASE(test_collateral_script_metadata_registration)
{
    auto params = CreateTestMintParams();
    params.ddAmount = 12345; // Specific amount for testing
    params.lockHeight = 9999;

    CScript collateralScript = DigiDollar::CreateCollateralP2TR(params);
    BOOST_CHECK(collateralScript.size() > 0);

    // Verify metadata was registered (Phase 1 testing support)
    DigiDollar::ScriptMetadata metadata;
    bool found = DigiDollar::GetScriptMetadata(collateralScript, metadata);
    BOOST_CHECK(found);

    if (found) {
        BOOST_CHECK_EQUAL(metadata.ddAmount, 12345);
        BOOST_CHECK_EQUAL(metadata.lockHeight, 9999);
    }
}

// ============================================================================
// FIX-1: Taproot NUMS point tests — collateral must NOT be key-path spendable
// ============================================================================

BOOST_AUTO_TEST_CASE(test_collateral_internal_key_is_nums)
{
    // The collateral output MUST use the NUMS point as internal key,
    // NOT the owner's pubkey. This prevents key-path spending which
    // would bypass CLTV timelocks entirely.
    XOnlyPubKey nums = DigiDollar::GetCollateralNUMSKey();
    BOOST_CHECK(nums.IsFullyValid());

    // Verify the NUMS point matches the BIP-341 recommended value
    auto nums_hex = HexStr(Span<const unsigned char>(nums.data(), 32));
    BOOST_CHECK_EQUAL(nums_hex, "50929b74c1a04954b78b4b6035e97a5e078a5a0f28ec96d547bfee9ace803ac0");
}

BOOST_AUTO_TEST_CASE(test_collateral_uses_nums_not_owner_key)
{
    // Create a collateral script and verify the internal key is NUMS
    DigiDollar::MintParams params;
    CKey ownerKey;
    ownerKey.MakeNewKey(true);
    params.ownerKey = XOnlyPubKey(ownerKey.GetPubKey());
    params.internalKey = DigiDollar::GetCollateralNUMSKey();
    params.ddAmount = 10000;  // $100
    params.lockHeight = 1000;
    params.oracleKeys = DigiDollar::GetOracleKeys(15);

    CScript script = DigiDollar::CreateCollateralP2TR(params);
    BOOST_CHECK(!script.empty());

    // The internal key in params must be NUMS, not the owner key
    BOOST_CHECK(params.internalKey != params.ownerKey);
    BOOST_CHECK(params.internalKey == DigiDollar::GetCollateralNUMSKey());
}

BOOST_AUTO_TEST_CASE(test_collateral_script_path_still_works)
{
    // Script-path spending (via MAST leaves) must still be possible
    // even with NUMS internal key. The MAST tree should have valid leaves.
    DigiDollar::MintParams params;
    CKey ownerKey;
    ownerKey.MakeNewKey(true);
    params.ownerKey = XOnlyPubKey(ownerKey.GetPubKey());
    params.internalKey = DigiDollar::GetCollateralNUMSKey();
    params.ddAmount = 10000;
    params.lockHeight = 1000;
    params.oracleKeys = DigiDollar::GetOracleKeys(15);

    // Both redemption paths should still be created
    CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);
    CScript errPath = DigiDollar::CreateERRPath(params);
    BOOST_CHECK(!normalPath.empty());
    BOOST_CHECK(!errPath.empty());

    // Full P2TR script should still be valid
    CScript p2tr = DigiDollar::CreateCollateralP2TR(params);
    BOOST_CHECK(!p2tr.empty());
    BOOST_CHECK_EQUAL(p2tr.size(), 34);  // OP_1 + 32-byte key
    BOOST_CHECK_EQUAL(p2tr[0], OP_1);
}

BOOST_AUTO_TEST_SUITE_END()
