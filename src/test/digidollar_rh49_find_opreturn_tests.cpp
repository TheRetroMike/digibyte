// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <digidollar/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_rh49_find_opreturn_tests)

// Helper: build a DD OP_RETURN script using Format 1 (OP_RETURN OP_DIGIDOLLAR <amount>)
static CScript MakeDDOpReturnFormat1(CAmount amount = 100) {
    CScript script;
    script << OP_RETURN << OP_DIGIDOLLAR;
    // 8-byte LE amount
    std::vector<unsigned char> data(8);
    for (int i = 0; i < 8; i++) data[i] = (amount >> (i * 8)) & 0xff;
    script << data;
    return script;
}

// Helper: build a DD OP_RETURN script using Format 2 (OP_RETURN "DD" ...)
static CScript MakeDDOpReturnFormat2(CAmount amount = 100) {
    CScript script;
    script << OP_RETURN;
    std::vector<unsigned char> marker = {'D', 'D'};
    script << marker;
    std::vector<unsigned char> txType = {0x01}; // MINT
    script << txType;
    CScriptNum amountNum(amount);
    script << amountNum.getvch();
    return script;
}

// Helper: a plain P2PKH-ish output
static CScript MakeDummyOutput() {
    CScript script;
    script << OP_DUP << OP_HASH160;
    std::vector<unsigned char> hash(20, 0x42);
    script << hash << OP_EQUALVERIFY << OP_CHECKSIG;
    return script;
}

// Helper: build a tx with DD OP_RETURN at a specific vout index
static CTransaction MakeTxWithDDAt(int ddIndex, int totalVouts, bool format1 = true) {
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    for (int i = 0; i < totalVouts; i++) {
        CTxOut out;
        if (i == ddIndex) {
            out.scriptPubKey = format1 ? MakeDDOpReturnFormat1() : MakeDDOpReturnFormat2();
            out.nValue = 0;
        } else {
            out.scriptPubKey = MakeDummyOutput();
            out.nValue = 1000;
        }
        mtx.vout.push_back(out);
    }
    return CTransaction(mtx);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_at_vout0) {
    CTransaction tx = MakeTxWithDDAt(0, 4);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), 0);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_at_vout1) {
    CTransaction tx = MakeTxWithDDAt(1, 4);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), 1);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_at_vout2) {
    CTransaction tx = MakeTxWithDDAt(2, 4);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), 2);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_at_vout3) {
    CTransaction tx = MakeTxWithDDAt(3, 4);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), 3);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_format2_at_vout1) {
    CTransaction tx = MakeTxWithDDAt(1, 3, false);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), 1);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_format2_at_vout3) {
    CTransaction tx = MakeTxWithDDAt(3, 4, false);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), 3);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_not_present) {
    // Transaction with no DD OP_RETURN
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    for (int i = 0; i < 3; i++) {
        CTxOut out;
        out.scriptPubKey = MakeDummyOutput();
        out.nValue = 1000;
        mtx.vout.push_back(out);
    }
    CTransaction tx(mtx);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), -1);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_plain_opreturn_ignored) {
    // OP_RETURN that is NOT a DD marker should be ignored
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256::ONE, 0);

    // Add a non-DD OP_RETURN
    CTxOut opretOut;
    CScript nonDD;
    nonDD << OP_RETURN;
    std::vector<unsigned char> randomData = {'X', 'Y'};
    nonDD << randomData;
    opretOut.scriptPubKey = nonDD;
    opretOut.nValue = 0;
    mtx.vout.push_back(opretOut);

    // Add normal output
    CTxOut normalOut;
    normalOut.scriptPubKey = MakeDummyOutput();
    normalOut.nValue = 1000;
    mtx.vout.push_back(normalOut);

    CTransaction tx(mtx);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), -1);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_empty_tx) {
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    CTransaction tx(mtx);
    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(tx), -1);
}

BOOST_AUTO_TEST_SUITE_END()
