// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <digidollar/txbuilder.h>
#include <digidollar/digidollar.h>
#include <consensus/validation.h>
#include <key.h>
#include <test/util/setup_common.h>

using namespace DigiDollar;

BOOST_FIXTURE_TEST_SUITE(digidollar_transfer_tests, BasicTestingSetup)

// Test basic DD transfer creation
BOOST_AUTO_TEST_CASE(basic_transfer_creation_test)
{
    // Create sender and recipient keys
    CKey senderKey, recipientKey;
    senderKey.MakeNewKey(true);
    recipientKey.MakeNewKey(true);
    
    // Create transfer parameters
    TxBuilderTransferParams params;
    params.ddAmount = 10000; // $100.00
    params.senderKey = senderKey;
    params.recipientPubkey = recipientKey.GetPubKey();
    params.feeRate = 1000; // 1 sat/vB
    
    // Add mock DD UTXO
    CTxOut ddUtxo(0, CScript()); // DD UTXOs have 0 DGB value
    params.ddInputs.push_back(ddUtxo);
    params.inputDDAmounts.push_back(10000);
    
    // Build transfer transaction
    TransferTxBuilder builder(Params(), 100000, 50000);
    TxBuilderResult result = builder.BuildTransferTransaction(params);
    
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.tx.vout.size(), 1); // DD output to recipient
    BOOST_CHECK_EQUAL(result.tx.vout[0].nValue, 0); // DD outputs have 0 DGB
}

// Test transfer with change
BOOST_AUTO_TEST_CASE(transfer_with_change_test)
{
    CKey senderKey, recipientKey;
    senderKey.MakeNewKey(true);
    recipientKey.MakeNewKey(true);
    
    TxBuilderTransferParams params;
    params.ddAmount = 5000; // $50.00
    params.senderKey = senderKey;
    params.recipientPubkey = recipientKey.GetPubKey();
    params.feeRate = 1000;
    
    // Add DD UTXO with more than needed
    params.ddInputs.push_back(CTxOut(0, CScript())); 
    params.inputDDAmounts.push_back(10000); // $100
    
    TransferTxBuilder builder(Params(), 100000, 50000);
    TxBuilderResult result = builder.BuildTransferTransaction(params);
    
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.tx.vout.size(), 2); // Recipient + change
}

// Test insufficient DD balance
BOOST_AUTO_TEST_CASE(insufficient_dd_balance_test)
{
    CKey senderKey, recipientKey;
    senderKey.MakeNewKey(true);
    recipientKey.MakeNewKey(true);
    
    TxBuilderTransferParams params;
    params.ddAmount = 20000; // $200.00
    params.senderKey = senderKey;
    params.recipientPubkey = recipientKey.GetPubKey();
    params.feeRate = 1000;
    
    // Add insufficient DD UTXO
    params.ddInputs.push_back(CTxOut(0, CScript()));
    params.inputDDAmounts.push_back(10000); // Only $100
    
    TransferTxBuilder builder(Params(), 100000, 50000);
    TxBuilderResult result = builder.BuildTransferTransaction(params);
    
    BOOST_CHECK(!result.success);
    BOOST_CHECK(result.error.find("Insufficient") != std::string::npos);
}

// Test zero amount transfer
BOOST_AUTO_TEST_CASE(zero_amount_transfer_test)
{
    CKey senderKey, recipientKey;
    senderKey.MakeNewKey(true);
    recipientKey.MakeNewKey(true);
    
    TxBuilderTransferParams params;
    params.ddAmount = 0; // Invalid
    params.senderKey = senderKey;
    params.recipientPubkey = recipientKey.GetPubKey();
    params.feeRate = 1000;
    
    TransferTxBuilder builder(Params(), 100000, 50000);
    TxBuilderResult result = builder.BuildTransferTransaction(params);
    
    BOOST_CHECK(!result.success);
    BOOST_CHECK(result.error.find("Invalid amount") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
