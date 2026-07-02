// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/script.h>
#include <script/interpreter.h>
#include <script/script_error.h>
#include <util/strencodings.h>
#include <test/util/setup_common.h>
#include <consensus/amount.h>

#include <boost/test/unit_test.hpp>

// Forward declare CastToBool from interpreter.cpp
bool CastToBool(const std::vector<unsigned char>& vch);

BOOST_AUTO_TEST_SUITE(digidollar_opcodes_tests)

// Helper function to create a mock signature checker for testing
class MockSignatureChecker : public BaseSignatureChecker
{
public:
    bool CheckECDSASignature(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode, SigVersion sigversion) const override
    {
        return true; // Mock implementation for testing
    }
};

// RAII helper: install an oracle-price hook as a regression canary. The
// production OP_CHECKPRICE path is reserved/disabled and must ignore the hook.
class ScopedOpcodeOraclePrice
{
public:
    explicit ScopedOpcodeOraclePrice(CAmount price) { s_current_price = price; m_previous = g_get_oracle_consensus_price; g_get_oracle_consensus_price = []() -> CAmount { return s_current_price; }; }
    ~ScopedOpcodeOraclePrice() { g_get_oracle_consensus_price = m_previous; }
private:
    GetOracleConsensusPriceFn m_previous{nullptr};
    static CAmount s_current_price;
};
CAmount ScopedOpcodeOraclePrice::s_current_price = 0;

// Test that DigiDollar opcodes have correct values
BOOST_AUTO_TEST_CASE(digidollar_opcode_values)
{
    // These opcodes use Tapscript OP_SUCCESSx slots for soft-fork compatibility.
    BOOST_CHECK_EQUAL(static_cast<int>(OP_DIGIDOLLAR), 0xbb);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_DDVERIFY), 0xbc);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_CHECKPRICE), 0xbd);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_CHECKCOLLATERAL), 0xbe);

    // Verify OP_CHECKSIGADD exists (BIP342)
    BOOST_CHECK_EQUAL(static_cast<int>(OP_CHECKSIGADD), 0xba);
}

// Test that opcode names are correct
BOOST_AUTO_TEST_CASE(digidollar_opcode_names)
{
    BOOST_CHECK_EQUAL(GetOpName(OP_DIGIDOLLAR), "OP_DIGIDOLLAR");
    BOOST_CHECK_EQUAL(GetOpName(OP_DDVERIFY), "OP_DDVERIFY");
    BOOST_CHECK_EQUAL(GetOpName(OP_CHECKPRICE), "OP_CHECKPRICE");
    BOOST_CHECK_EQUAL(GetOpName(OP_CHECKCOLLATERAL), "OP_CHECKCOLLATERAL");
}

// Test OP_DIGIDOLLAR basic functionality
BOOST_AUTO_TEST_CASE(op_digidollar_basic)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    // Test with valid DigiDollar amount
    // OP_DIGIDOLLAR reads amount from script, not from stack
    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(100000);  // 1.0 DGB in satoshis

    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(stack.size(), 1);
    BOOST_CHECK(CastToBool(stack.back())); // Should push true for valid amount
}

// Test OP_DIGIDOLLAR with zero amount
BOOST_AUTO_TEST_CASE(op_digidollar_zero_amount)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(0);

    // SECURITY FIX: Zero amount now correctly fails instead of pushing false
    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_DD_AMOUNT);
}

// Test OP_DIGIDOLLAR with negative amount (should fail)
BOOST_AUTO_TEST_CASE(op_digidollar_negative_amount)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(-100);

    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_DD_AMOUNT);
}

// Test OP_DIGIDOLLAR with amount exceeding MAX_MONEY (should fail)
BOOST_AUTO_TEST_CASE(op_digidollar_overflow_amount)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(MAX_MONEY + 1);

    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_DD_AMOUNT);
}

// Test OP_DIGIDOLLAR with insufficient stack (should fail)
BOOST_AUTO_TEST_CASE(op_digidollar_insufficient_stack)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << OP_DIGIDOLLAR; // No amount in script

    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_DD_AMOUNT); // Changed from INVALID_STACK_OPERATION
}

// Test OP_DDVERIFY basic functionality
BOOST_AUTO_TEST_CASE(op_ddverify_basic)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    // Test with true value (should pass)
    CScript script;
    script << OP_TRUE << OP_DDVERIFY;

    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(stack.size(), 0); // Should consume the stack item
}

// Test OP_DDVERIFY with false value (should fail)
BOOST_AUTO_TEST_CASE(op_ddverify_false)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << OP_FALSE << OP_DDVERIFY;

    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_DD_VERIFY);
}

// Test OP_DDVERIFY with insufficient stack (should fail)
BOOST_AUTO_TEST_CASE(op_ddverify_insufficient_stack)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << OP_DDVERIFY; // No value on stack

    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_STACK_OPERATION);
}

// Test OP_CHECKPRICE basic functionality
BOOST_AUTO_TEST_CASE(op_checkprice_basic)
{
    // Install a scoped hook returning the witness price; OP_CHECKPRICE must
    // still push false because the opcode is reserved/disabled.
    ScopedOpcodeOraclePrice oracle(100000);

    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << CScriptNum(100000) << OP_CHECKPRICE;

    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(stack.size(), 1);
    // DD-FINAL-005 / AR-0: OP_CHECKPRICE is now deterministically DISABLED (it consulted a
    // non-deterministic node-local/wall-clock price and could fork). It consumes the operand
    // and always pushes FALSE regardless of any oracle price.
    BOOST_CHECK(!CastToBool(stack.back()));
}

// Test OP_CHECKPRICE with non-matching price
BOOST_AUTO_TEST_CASE(op_checkprice_mismatch)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << CScriptNum(50000) << OP_CHECKPRICE; // Different from mock oracle price

    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(stack.size(), 1);
    BOOST_CHECK(!CastToBool(stack.back())); // Should push false for non-matching price
}

// Test OP_CHECKPRICE with insufficient stack (should fail)
BOOST_AUTO_TEST_CASE(op_checkprice_insufficient_stack)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << OP_CHECKPRICE; // No price on stack

    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_STACK_OPERATION);
}

// Test OP_CHECKCOLLATERAL basic functionality
BOOST_AUTO_TEST_CASE(op_checkcollateral_basic)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    // Test with sufficient collateral (ratio >= threshold)
    CScript script;
    script << CScriptNum(150) << CScriptNum(120) << OP_CHECKCOLLATERAL; // ratio=150, threshold=120

    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(stack.size(), 1);
    BOOST_CHECK(CastToBool(stack.back())); // Should push true for sufficient collateral
}

// Test OP_CHECKCOLLATERAL with insufficient collateral
BOOST_AUTO_TEST_CASE(op_checkcollateral_insufficient)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << CScriptNum(100) << CScriptNum(120) << OP_CHECKCOLLATERAL; // ratio=100, threshold=120

    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(stack.size(), 1);
    BOOST_CHECK(!CastToBool(stack.back())); // Should push false for insufficient collateral
}

// Test OP_CHECKCOLLATERAL with equal values (should pass)
BOOST_AUTO_TEST_CASE(op_checkcollateral_equal)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << CScriptNum(120) << CScriptNum(120) << OP_CHECKCOLLATERAL; // ratio=120, threshold=120

    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(stack.size(), 1);
    BOOST_CHECK(CastToBool(stack.back())); // Should push true for equal values
}

// Test OP_CHECKCOLLATERAL with insufficient stack (should fail)
BOOST_AUTO_TEST_CASE(op_checkcollateral_insufficient_stack)
{
    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    CScript script;
    script << CScriptNum(120) << OP_CHECKCOLLATERAL; // Only one value on stack

    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_STACK_OPERATION);
}

// In legacy and witness-v0 scripts, DD opcode bytes are not old-node NOPs.
// They must remain bad opcodes so activation is not a hard fork for those script versions.
BOOST_AUTO_TEST_CASE(opcodes_are_bad_opcode_outside_tapscript)
{
    MockSignatureChecker checker;
    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(100000);

    {
        std::vector<std::vector<unsigned char>> stack;
        ScriptError error;
        ScriptExecutionData execdata;
        BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::BASE, execdata, &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_BAD_OPCODE);
    }

    {
        std::vector<std::vector<unsigned char>> stack;
        ScriptError error;
        ScriptExecutionData execdata;
        BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::WITNESS_V0, execdata, &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_BAD_OPCODE);
    }
}

// Test complex DigiDollar script combining multiple opcodes
BOOST_AUTO_TEST_CASE(complex_digidollar_script)
{
    // Post-fix: register oracle hook returning 100000 so OP_CHECKPRICE with
    // witness 100000 evaluates to TRUE. The test's intent is unchanged.
    ScopedOpcodeOraclePrice oracle(100000);

    std::vector<std::vector<unsigned char>> stack;
    MockSignatureChecker checker;
    ScriptError error;
    ScriptExecutionData execdata;

    // Create a complex script: OP_DIGIDOLLAR -> OP_DDVERIFY -> price check -> collateral check
    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(100000); // Mark as DD output (pushes true)
    script << OP_DDVERIFY;                          // Verify the DD condition (pops true)
    script << CScriptNum(100000) << OP_CHECKPRICE; // DD-FINAL-005: OP_CHECKPRICE now disabled -> pushes FALSE
    script << CScriptNum(150) << CScriptNum(120) << OP_CHECKCOLLATERAL; // Check collateral (pushes true)
    script << OP_BOOLAND; // Combine last two conditions with AND

    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(stack.size(), 1);
    // DD-FINAL-005 / AR-0: with OP_CHECKPRICE deterministically disabled (always FALSE), the
    // BOOLAND of (FALSE oracle-price-check AND TRUE collateral-check) is FALSE. OP_CHECKPRICE
    // is reserved/inert and never contributes a TRUE to a tapscript.
    BOOST_CHECK(!CastToBool(stack.back()));
}

// Test script error string representation
BOOST_AUTO_TEST_CASE(digidollar_script_error_strings)
{
    BOOST_CHECK_EQUAL(ScriptErrorString(SCRIPT_ERR_INVALID_DD_AMOUNT), "Invalid DigiDollar amount");
    BOOST_CHECK_EQUAL(ScriptErrorString(SCRIPT_ERR_DD_VERIFY), "DigiDollar verification failed");
    BOOST_CHECK_EQUAL(ScriptErrorString(SCRIPT_ERR_ORACLE_PRICE_STALE), "Oracle price is stale");
    BOOST_CHECK_EQUAL(ScriptErrorString(SCRIPT_ERR_INSUFFICIENT_COLLATERAL), "Insufficient collateral ratio");
}

// Test that activated DD opcodes execute only under Tapscript.
BOOST_AUTO_TEST_CASE(opcodes_tapscript_only)
{
    MockSignatureChecker checker;
    CScript script;
    script << OP_DIGIDOLLAR << CScriptNum(100000);

    {
        std::vector<std::vector<unsigned char>> stack;
        ScriptError error;
        ScriptExecutionData execdata;
        BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::TAPSCRIPT, execdata, &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    }

    {
        std::vector<std::vector<unsigned char>> stack;
        ScriptError error;
        ScriptExecutionData execdata;
        BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR, checker, SigVersion::BASE, execdata, &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_BAD_OPCODE);
    }
}

BOOST_AUTO_TEST_SUITE_END()
