// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-54: DigiDollar Tapscript OP_SUCCESSx activation compatibility
 *
 * DigiDollar uses opcode bytes 0xbb..0xbf. Under BIP342 those bytes are
 * OP_SUCCESSx before the soft fork activates, so old Taproot nodes accept a
 * revealed leaf containing them unconditionally. Upgraded nodes must preserve
 * that pre-activation behavior and only remove the bytes from OP_SUCCESSx when
 * SCRIPT_VERIFY_DIGIDOLLAR is active.
 */

#include <script/interpreter.h>
#include <script/script.h>
#include <script/standard.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(rh54_op_oracle_opsuccess_tests, BasicTestingSetup)

static constexpr unsigned int TAPROOT_VERIFY_FLAGS =
    SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT;

static CScript MakeSingleLeafTaproot(const CScript& leaf, CScriptWitness& witness)
{
    static const std::vector<unsigned char> NUMS_PK{ParseHex("50929b74c1a04954b78b4b6035e97a5e078a5a0f28ec96d547bfee9ace803ac0")};

    TaprootBuilder builder;
    builder.Add(0, leaf, TAPROOT_LEAF_TAPSCRIPT);
    builder.Finalize(XOnlyPubKey{NUMS_PK});

    witness.stack.emplace_back(leaf.begin(), leaf.end());
    witness.stack.push_back(*builder.GetSpendData().scripts.begin()->second.begin());
    return GetScriptForDestination(builder.GetOutput());
}

BOOST_AUTO_TEST_CASE(rh54_dd_opcodes_remain_raw_bip342_opsuccess)
{
    BOOST_CHECK(IsOpSuccess(OP_DIGIDOLLAR));
    BOOST_CHECK(IsOpSuccess(OP_DDVERIFY));
    BOOST_CHECK(IsOpSuccess(OP_CHECKPRICE));
    BOOST_CHECK(IsOpSuccess(OP_CHECKCOLLATERAL));
    BOOST_CHECK(IsOpSuccess(OP_ORACLE));
}

BOOST_AUTO_TEST_CASE(rh54_opcode_numeric_invariants)
{
    BOOST_CHECK_EQUAL(static_cast<int>(OP_DIGIDOLLAR),      0xbb);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_DDVERIFY),        0xbc);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_CHECKPRICE),      0xbd);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_CHECKCOLLATERAL), 0xbe);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_ORACLE),          0xbf);

    BOOST_CHECK_GE(static_cast<int>(OP_DIGIDOLLAR), 187);
    BOOST_CHECK_LE(static_cast<int>(OP_ORACLE), 254);
}

BOOST_AUTO_TEST_CASE(rh54_preactivation_tapscript_matches_old_opsuccess)
{
    // Invalid under active DigiDollar rules, but valid pre-activation because
    // OP_DIGIDOLLAR is still OP_SUCCESSx and short-circuits the entire leaf.
    CScript leaf;
    leaf << OP_DIGIDOLLAR << CScriptNum(0) << OP_FALSE;

    CScriptWitness witness;
    const CScript script_pubkey = MakeSingleLeafTaproot(leaf, witness);

    ScriptError error;
    BOOST_CHECK(VerifyScript(CScript{}, script_pubkey, &witness,
        TAPROOT_VERIFY_FLAGS,
        BaseSignatureChecker{}, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(rh54_activation_removes_dd_opcodes_from_opsuccess)
{
    CScript leaf;
    leaf << OP_DIGIDOLLAR << CScriptNum(0) << OP_FALSE;

    CScriptWitness witness;
    const CScript script_pubkey = MakeSingleLeafTaproot(leaf, witness);

    ScriptError error;
    BOOST_CHECK(!VerifyScript(CScript{}, script_pubkey, &witness,
        TAPROOT_VERIFY_FLAGS | SCRIPT_VERIFY_DIGIDOLLAR,
        BaseSignatureChecker{}, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_INVALID_DD_AMOUNT);
}

BOOST_AUTO_TEST_CASE(rh54_op_oracle_is_opsuccess_before_activation_bad_opcode_after)
{
    CScript leaf;
    leaf << OP_ORACLE;

    CScriptWitness witness_pre;
    const CScript script_pubkey_pre = MakeSingleLeafTaproot(leaf, witness_pre);
    ScriptError error_pre;
    BOOST_CHECK(VerifyScript(CScript{}, script_pubkey_pre, &witness_pre,
        TAPROOT_VERIFY_FLAGS,
        BaseSignatureChecker{}, &error_pre));
    BOOST_CHECK_EQUAL(error_pre, SCRIPT_ERR_OK);

    CScriptWitness witness_post;
    const CScript script_pubkey_post = MakeSingleLeafTaproot(leaf, witness_post);
    ScriptError error_post;
    BOOST_CHECK(!VerifyScript(CScript{}, script_pubkey_post, &witness_post,
        TAPROOT_VERIFY_FLAGS | SCRIPT_VERIFY_DIGIDOLLAR,
        BaseSignatureChecker{}, &error_post));
    BOOST_CHECK_EQUAL(error_post, SCRIPT_ERR_BAD_OPCODE);
}

BOOST_AUTO_TEST_CASE(rh54_non_dd_neighbors_keep_existing_meaning)
{
    // 0xba == OP_CHECKSIGADD (real Tapscript opcode), not OP_SUCCESSx.
    BOOST_CHECK(!IsOpSuccess(static_cast<opcodetype>(0xba)));
    // Neighbor after OP_ORACLE remains a reserved OP_SUCCESSx.
    BOOST_CHECK(IsOpSuccess(static_cast<opcodetype>(0xc0)));
    BOOST_CHECK(IsOpSuccess(static_cast<opcodetype>(0xfe)));
}

BOOST_AUTO_TEST_SUITE_END()
