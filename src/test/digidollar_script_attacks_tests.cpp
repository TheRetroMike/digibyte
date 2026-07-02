// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// RH-38: Script Interpreter DD Opcode Attack Tests
// Security audit round 2 — exploit tests for DigiDollar opcodes

#include <script/script.h>
#include <script/interpreter.h>
#include <script/script_error.h>
#include <consensus/amount.h>
#include <digidollar/digidollar.h>
#include <digidollar/validation.h>
#include <util/strencodings.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

bool CastToBool(const std::vector<unsigned char>& vch);

namespace {

class MockSignatureChecker : public BaseSignatureChecker
{
public:
    bool CheckECDSASignature(const std::vector<unsigned char>&, const std::vector<unsigned char>&,
                              const CScript&, SigVersion) const override { return true; }
};

// Helper to evaluate a script with DD flags enabled
bool EvalDD(const CScript& script, std::vector<std::vector<unsigned char>>& stack, ScriptError& error)
{
    MockSignatureChecker checker;
    ScriptExecutionData execdata;
    return EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error);
}

// Helper to evaluate Tapscript before DD activation
bool EvalNonDD(const CScript& script, std::vector<std::vector<unsigned char>>& stack, ScriptError& error)
{
    MockSignatureChecker checker;
    ScriptExecutionData execdata;
    return EvalScript(stack, script, SCRIPT_VERIFY_NONE, checker, SigVersion::TAPSCRIPT, execdata, &error);
}

} // namespace

BOOST_AUTO_TEST_SUITE(digidollar_script_attacks_tests)

// ============================================================================
// ATTACK VECTOR 1: Zero-amount DD mint pushes false instead of failing
// ============================================================================
// OP_DIGIDOLLAR with amount=0 pushes false but doesn't fail the script.
// An attacker can use OP_DROP to discard the false and continue execution,
// effectively creating a zero-value DD operation that passes script validation.
BOOST_AUTO_TEST_CASE(attack_zero_amount_dd_drop)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // OP_DIGIDOLLAR <0> should FAIL — zero DD amount is invalid
    CScript attack;
    attack << OP_DIGIDOLLAR << CScriptNum(0) << OP_DROP << OP_1;

    bool result = EvalDD(attack, stack, error);
    // FIXED: Zero amount now fails with SCRIPT_ERR_INVALID_DD_AMOUNT
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_DD_AMOUNT);
}

// ============================================================================
// ATTACK VECTOR 2: MAX_MONEY vs MAX_DIGIDOLLAR mismatch
// ============================================================================
// The interpreter and OP_RETURN extractor must reject amounts above
// MAX_DIGIDOLLAR, the per-output serialization bound.
BOOST_AUTO_TEST_CASE(attack_amount_range_mismatch)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // Amount > MAX_DIGIDOLLAR should be rejected by interpreter.
    CAmount too_large_for_dd = MAX_DIGIDOLLAR + 1;
    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(too_large_for_dd);

    bool result = EvalDD(script, stack, error);
    // FIXED: Interpreter now uses MAX_DIGIDOLLAR, not MAX_MONEY
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_DD_AMOUNT);

    // Also test ExtractDDAmount rejects this amount
    CScript opReturnScript;
    opReturnScript << OP_RETURN << OP_DIGIDOLLAR;
    // Manually encode 8-byte LE amount
    std::vector<unsigned char> amountBytes(8, 0);
    int64_t val = too_large_for_dd;
    for (int i = 0; i < 8; i++) {
        amountBytes[i] = (val >> (i * 8)) & 0xFF;
    }
    opReturnScript << amountBytes;

    CAmount extracted = 0;
    bool extractResult = DigiDollar::ExtractDDAmount(opReturnScript, extracted);
    BOOST_CHECK(!extractResult);
}

// ============================================================================
// ATTACK VECTOR 3: Multiple OP_DIGIDOLLAR in one script (double mint)
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_double_dd_opcode)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // Two OP_DIGIDOLLARs — an attacker might try to encode two different amounts
    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(100000);   // First: 100000 cents
    script << OP_DIGIDOLLAR << CScriptNum(500000);   // Second: 500000 cents

    bool result = EvalDD(script, stack, error);
    if (result) {
        BOOST_TEST_MESSAGE("BUG CONFIRMED: Multiple OP_DIGIDOLLAR opcodes accepted in one script");
        // Stack should have two true values — both amounts "validated"
        BOOST_CHECK_EQUAL(stack.size(), 2U);
    }
}

// Same with contradictory ops — mint followed by redeem-like pattern
BOOST_AUTO_TEST_CASE(attack_dd_followed_by_ddverify)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // OP_DIGIDOLLAR pushes true, OP_DDVERIFY pops and verifies it
    // Then another OP_DIGIDOLLAR with different amount
    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(100000);
    script << OP_DDVERIFY;
    script << OP_DIGIDOLLAR << CScriptNum(999999);

    bool result = EvalDD(script, stack, error);
    if (result) {
        BOOST_TEST_MESSAGE("BUG CONFIRMED: OP_DIGIDOLLAR after OP_DDVERIFY accepted");
        BOOST_CHECK_EQUAL(stack.size(), 1U);
    }
}

// ============================================================================
// ATTACK VECTOR 4: pre-activation OP_SUCCESSx stack divergence
// ============================================================================
// Without SCRIPT_VERIFY_DIGIDOLLAR, OP_DIGIDOLLAR remains a BIP342 OP_SUCCESSx
// and succeeds immediately without executing the following amount push.
// With the flag, OP_DIGIDOLLAR consumes the amount and pushes true/false.
// This means the same script produces different stack states.
BOOST_AUTO_TEST_CASE(attack_nop_mode_stack_divergence)
{
    ScriptError error;

    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(100000);

    // With DD flags
    std::vector<std::vector<unsigned char>> stack_dd;
    bool dd_result = EvalDD(script, stack_dd, error);
    BOOST_CHECK(dd_result);

    // Without DD flags
    std::vector<std::vector<unsigned char>> stack_nop;
    bool nop_result = EvalNonDD(script, stack_nop, error);
    BOOST_CHECK(nop_result);

    // Document the divergence — this is expected for soft-fork OP_SUCCESSx
    // activation, but we need to verify it doesn't break consensus rules.
    BOOST_TEST_MESSAGE("DD-mode stack size: " << stack_dd.size());
    BOOST_TEST_MESSAGE("Pre-activation OP_SUCCESSx stack size: " << stack_nop.size());

    // In pre-activation OP_SUCCESSx mode: execution short-circuits immediately.
    // In DD mode: stack has [true] (OP_DIGIDOLLAR consumed the push and output true)
    // This is CORRECT for Tapscript OP_SUCCESSx-based soft forks.
    BOOST_CHECK_EQUAL(stack_nop.size(), 0U);
    BOOST_CHECK_EQUAL(stack_dd.size(), 1U);  // The validation result

    // DD-mode should have true.
    if (stack_dd.size() == 1) {
        BOOST_CHECK(CastToBool(stack_dd[0])); // true
    }
}

// ============================================================================
// ATTACK VECTOR 5: Negative amount accepted
// ============================================================================
// The check is `amount < 0` which rejects negatives, but what about -0?
// CScriptNum encodes -0 differently than 0.
BOOST_AUTO_TEST_CASE(attack_negative_zero_amount)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // CScriptNum for -0: single byte 0x80
    CScript script;
    script << OP_DIGIDOLLAR;
    // Manually push -0 encoding: 0x80
    std::vector<unsigned char> neg_zero = {0x80};
    script << neg_zero;

    bool result = EvalDD(script, stack, error);
    // fRequireMinimal should reject 0x80 as non-minimal encoding of 0
    BOOST_TEST_MESSAGE("Negative-zero DD amount result: " << (result ? "ACCEPTED (BUG)" : "rejected"));
    if (result) {
        BOOST_TEST_MESSAGE("BUG: Negative-zero amount not caught by minimal encoding check");
    }
}

// ============================================================================
// ATTACK VECTOR 6: OP_CHECKCOLLATERAL with extreme values
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_collateral_overflow)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // Push max 4-byte CScriptNum as ratio — test boundary
    CScript script;
    script << CScriptNum(2147483647LL)
           << CScriptNum(1)
           << OP_CHECKCOLLATERAL;

    bool result = EvalDD(script, stack, error);
    BOOST_TEST_MESSAGE("Max CScriptNum collateral ratio result: " << (result ? "accepted" : "rejected"));
    if (result && stack.size() == 1) {
        BOOST_CHECK(CastToBool(stack.back())); // 2^31-1 >= 1 should be true
    }

    // INT64_MAX exceeds CScriptNum 4-byte limit — caught by scriptnum_error
    // OP_CHECKCOLLATERAL silently pushes false instead of failing — this is a concern
    std::vector<std::vector<unsigned char>> stack2;
    CScript script2;
    // Manually push 8-byte value that exceeds CScriptNum range
    std::vector<unsigned char> big_val = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00};
    script2 << big_val << CScriptNum(1) << OP_CHECKCOLLATERAL;
    bool result2 = EvalDD(script2, stack2, error);
    if (result2 && stack2.size() == 1 && !CastToBool(stack2.back())) {
        BOOST_TEST_MESSAGE("CONCERN: Oversized collateral ratio silently pushes false instead of failing");
    }
}

// Negative ratio — should a negative collateral ratio be valid?
BOOST_AUTO_TEST_CASE(attack_negative_collateral_ratio)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    CScript script;
    script << CScriptNum(-100)
           << CScriptNum(150)
           << OP_CHECKCOLLATERAL;

    bool result = EvalDD(script, stack, error);
    if (result && stack.size() == 1) {
        bool collateral_ok = CastToBool(stack.back());
        BOOST_TEST_MESSAGE("Negative collateral ratio accepted as: " << (collateral_ok ? "true (BUG!)" : "false"));
        // -100 >= 150 should be false, but the opcode should reject negative ratios entirely
        BOOST_CHECK(!collateral_ok);
    }
}

// ============================================================================
// ATTACK VECTOR 7: ExtractDDAmount malleability via non-minimal encoding
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_dd_amount_malleability)
{
    // ExtractDDAmount Format 2 uses CScriptNum(data, false, 8) — fRequireMinimal=false!
    // This means 100 can be encoded as [0x64] or [0x64, 0x00] — both extract the same amount
    // Two different scripts can represent the same DD amount = malleability

    // Minimal encoding of 100: [0x64]
    CScript script1;
    script1 << OP_RETURN;
    std::vector<unsigned char> dd_marker = {'D', 'D'};
    script1 << dd_marker;
    std::vector<unsigned char> txType = {0x01}; // mint
    script1 << txType;
    std::vector<unsigned char> amount_minimal = {0x64}; // 100 minimal
    script1 << amount_minimal;

    CAmount amount1 = 0;
    bool r1 = DigiDollar::ExtractDDAmount(script1, amount1);

    // Non-minimal encoding of 100: [0x64, 0x00]
    CScript script2;
    script2 << OP_RETURN;
    script2 << dd_marker;
    script2 << txType;
    std::vector<unsigned char> amount_nonminimal = {0x64, 0x00}; // 100 non-minimal
    script2 << amount_nonminimal;

    CAmount amount2 = 0;
    bool r2 = DigiDollar::ExtractDDAmount(script2, amount2);

    BOOST_TEST_MESSAGE("Minimal encoding result: " << r1 << " amount: " << amount1);
    BOOST_TEST_MESSAGE("Non-minimal encoding result: " << r2 << " amount: " << amount2);

    // FIXED: Non-minimal encoding now rejected
    BOOST_CHECK(r1);  // Minimal should still work
    BOOST_CHECK(!r2); // Non-minimal should be rejected
    BOOST_TEST_MESSAGE("Malleability fix verified: non-minimal encoding rejected");
}

// ============================================================================
// ATTACK VECTOR 8: OP_RETURN trailing data after DD metadata
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_trailing_opreturn_data)
{
    // ExtractDDAmount doesn't verify no extra data follows — attacker can append arbitrary data
    CScript script;
    script << OP_RETURN << OP_DIGIDOLLAR;

    // 8-byte LE amount: 100000 cents
    std::vector<unsigned char> amountBytes(8, 0);
    int64_t val = 100000;
    for (int i = 0; i < 8; i++) amountBytes[i] = (val >> (i * 8)) & 0xFF;
    script << amountBytes;

    // Append arbitrary trailing data
    std::vector<unsigned char> junk = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    script << junk;

    CAmount amount = 0;
    bool result = DigiDollar::ExtractDDAmount(script, amount);
    // FIXED: Trailing data now rejected
    BOOST_CHECK(!result);
    BOOST_TEST_MESSAGE("Trailing data correctly rejected after fix");
}

// ============================================================================
// ATTACK VECTOR 9: Stack manipulation before OP_DDVERIFY
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_stack_manipulation_before_ddverify)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // Push attacker-controlled true onto stack, then OP_DDVERIFY accepts it
    // without any prior OP_DIGIDOLLAR
    CScript script;
    script << OP_1 << OP_DDVERIFY; // Push true, DDVERIFY accepts it

    bool result = EvalDD(script, stack, error);
    if (result) {
        BOOST_TEST_MESSAGE("BUG CONFIRMED: OP_DDVERIFY accepts non-DD stack values");
        // DDVERIFY should only accept values from OP_DIGIDOLLAR, not arbitrary pushes
    }
}

// ============================================================================
// ATTACK VECTOR 10: DD opcodes interleaved with control flow
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_dd_in_conditional)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // Use OP_IF to conditionally execute DD opcodes
    // This could allow selective execution based on witness data
    CScript script;
    script << OP_1 << OP_IF;
    script << OP_DIGIDOLLAR << CScriptNum(100000);
    script << OP_ELSE;
    script << OP_DIGIDOLLAR << CScriptNum(999999);
    script << OP_ENDIF;

    bool result = EvalDD(script, stack, error);
    if (result) {
        BOOST_TEST_MESSAGE("DD opcodes in conditionals accepted, stack size: " << stack.size());
        // This means witness data can control which DD amount is used
    }
}

// ============================================================================
// ATTACK VECTOR 11: OP_CHECKPRICE without real oracle — stack state attack
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_checkprice_stack_pollution)
{
    std::vector<std::vector<unsigned char>> stack;
    ScriptError error;

    // OP_CHECKPRICE catches scriptnum_error and pushes false without failing
    // Attacker can use non-numeric data to get predictable false on stack
    CScript script;
    // Push data that's too large for CScriptNum (>4 bytes in non-DD context)
    std::vector<unsigned char> big_data(5, 0xFF);
    script << big_data << OP_CHECKPRICE;

    bool result = EvalDD(script, stack, error);
    BOOST_TEST_MESSAGE("Invalid price data result: " << (result ? "accepted (false pushed)" : "rejected"));
    if (result && stack.size() == 1) {
        BOOST_CHECK(!CastToBool(stack.back())); // Should be false
        BOOST_TEST_MESSAGE("CONCERN: Invalid data silently pushes false instead of failing");
    }
}

// ============================================================================
// ATTACK VECTOR 12: Amount exactly at boundary values
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_boundary_amounts)
{
    // Test amount = 1 (minimum valid)
    {
        std::vector<std::vector<unsigned char>> stack;
        ScriptError error;
        CScript script;
        script << OP_DIGIDOLLAR << CScriptNum(1);
        bool result = EvalDD(script, stack, error);
        BOOST_CHECK(result);
        BOOST_CHECK(CastToBool(stack.back()));
    }

    // Test amount = MAX_MONEY (should be upper bound in interpreter)
    {
        std::vector<std::vector<unsigned char>> stack;
        ScriptError error;
        CScript script;
        script << OP_DIGIDOLLAR << CScriptNum(MAX_MONEY);
        bool result = EvalDD(script, stack, error);
        BOOST_TEST_MESSAGE("MAX_MONEY amount: " << (result ? "accepted" : "rejected"));
        // MAX_MONEY in interpreter but MAX_DIGIDOLLAR should be the real limit
    }

    // Test amount = MAX_MONEY + 1 (should fail)
    {
        std::vector<std::vector<unsigned char>> stack;
        ScriptError error;
        CScript script;
        script << OP_DIGIDOLLAR << CScriptNum(MAX_MONEY + 1);
        bool result = EvalDD(script, stack, error);
        BOOST_CHECK(!result);
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_DD_AMOUNT);
    }
}

BOOST_AUTO_TEST_SUITE_END()
