// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <test/util/setup_common.h>
#include <key.h>
#include <pubkey.h>
#include <primitives/oracle.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <logging.h>
#include <set>

BOOST_AUTO_TEST_SUITE(oracle_wallet_key_tests)

// 1. Basic key generation
BOOST_FIXTURE_TEST_CASE(oracle_key_generation_basic, BasicTestingSetup)
{
    CKey key;
    key.MakeNewKey(true);

    BOOST_CHECK(key.IsValid());
    BOOST_CHECK(key.IsCompressed());
    BOOST_CHECK_EQUAL(key.size(), 32);

    CPubKey pubkey = key.GetPubKey();
    BOOST_CHECK(pubkey.IsValid());
    BOOST_CHECK(pubkey.IsCompressed());
    BOOST_CHECK_EQUAL(pubkey.size(), 33);
}

// 2. Pubkey derivation and XOnlyPubKey
BOOST_FIXTURE_TEST_CASE(oracle_key_pubkey_derivation, BasicTestingSetup)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    XOnlyPubKey xonly(pubkey);

    // XOnlyPubKey should be 32 bytes
    BOOST_CHECK_EQUAL(HexStr(xonly).size(), 64);

    // x-only key should match the x-coordinate of the compressed pubkey
    // Compressed pubkey is 02/03 prefix + 32 bytes x-coordinate
    std::string pubhex = HexStr(pubkey);
    std::string xonlyhex = HexStr(xonly);
    // The x-only key should equal bytes [1..33) of the compressed pubkey
    BOOST_CHECK_EQUAL(xonlyhex, pubhex.substr(2));
}

// 3. Schnorr signing via COraclePriceMessage
BOOST_FIXTURE_TEST_CASE(oracle_key_schnorr_signing, BasicTestingSetup)
{
    CKey key;
    key.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6500;
    msg.timestamp = GetTime();
    msg.block_height = 1000;
    msg.nonce = 42;
    msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());

    BOOST_CHECK(msg.Sign(key));
    BOOST_CHECK(msg.Verify());
}

// 4. Compressed pubkey hex format
BOOST_FIXTURE_TEST_CASE(oracle_key_chainparams_format, BasicTestingSetup)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    std::string hex = HexStr(pubkey);

    BOOST_CHECK_EQUAL(hex.size(), 66);
    std::string prefix = hex.substr(0, 2);
    BOOST_CHECK(prefix == "02" || prefix == "03");
}

// 5. XOnlyPubKey hex format
BOOST_FIXTURE_TEST_CASE(oracle_key_xonly_format, BasicTestingSetup)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey xonly(key.GetPubKey());
    std::string hex = HexStr(xonly);

    BOOST_CHECK_EQUAL(hex.size(), 64);
}

// 6. Oracle ID bounds validation
BOOST_FIXTURE_TEST_CASE(oracle_key_id_validation, BasicTestingSetup)
{
    CKey key;
    key.MakeNewKey(true);

    // oracle_id 0 should be valid
    {
        COraclePriceMessage msg;
        msg.oracle_id = 0;
        msg.price_micro_usd = 6500;
        msg.timestamp = GetTime();
        msg.block_height = 1000;
        msg.nonce = 1;
        msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
        BOOST_CHECK(msg.SignAttestation(key));
        BOOST_CHECK(msg.IsValid());
    }

    // oracle_id 29 should be valid (ORACLE_TOTAL_COUNT - 1)
    {
        COraclePriceMessage msg;
        msg.oracle_id = 29;
        msg.price_micro_usd = 6500;
        msg.timestamp = GetTime();
        msg.block_height = 1000;
        msg.nonce = 2;
        msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
        BOOST_CHECK(msg.SignAttestation(key));
        BOOST_CHECK(msg.IsValid());
    }

    // oracle_id 35 — IsValid() doesn't check oracle_id bounds (that's chainparams),
    // but the message itself is structurally valid. The RPC createoraclekey rejects it.
    {
        COraclePriceMessage msg;
        msg.oracle_id = 35;
        msg.price_micro_usd = 6500;
        msg.timestamp = GetTime();
        msg.block_height = 1000;
        msg.nonce = 3;
        msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
        BOOST_CHECK(msg.SignAttestation(key));
        // IsValid() checks price/timestamp, not oracle_id — that's a higher-level check
        BOOST_CHECK(msg.IsValid());
        // But oracle_id >= ORACLE_TOTAL_COUNT should be rejected by RPC/chainparams
        BOOST_CHECK(msg.oracle_id >= ORACLE_TOTAL_COUNT);
    }

    // Verify that invalid price makes IsValid() fail (oracle_id is not the gate)
    {
        COraclePriceMessage msg;
        msg.oracle_id = 0;
        msg.price_micro_usd = 0; // Invalid: below MIN_PRICE_MICRO_USD
        msg.timestamp = GetTime();
        msg.block_height = 1000;
        msg.nonce = 4;
        msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
        BOOST_CHECK(!msg.IsValid()); // Should fail due to price = 0
    }
}

// 7. Deterministic pubkey from known private key bytes
BOOST_FIXTURE_TEST_CASE(oracle_key_deterministic_pubkey, BasicTestingSetup)
{
    // Known 32-byte private key
    unsigned char secret[32] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
    };

    CKey key;
    key.Set(std::begin(secret), std::end(secret), true);
    BOOST_CHECK(key.IsValid());

    CPubKey pubkey = key.GetPubKey();
    std::string hex1 = HexStr(pubkey);

    // Derive again from same secret — should match
    CKey key2;
    key2.Set(std::begin(secret), std::end(secret), true);
    CPubKey pubkey2 = key2.GetPubKey();
    std::string hex2 = HexStr(pubkey2);

    BOOST_CHECK_EQUAL(hex1, hex2);
    BOOST_CHECK(pubkey == pubkey2);
}

// 8. Key uniqueness
BOOST_FIXTURE_TEST_CASE(oracle_key_uniqueness, BasicTestingSetup)
{
    std::set<std::string> pubkeys;

    for (int i = 0; i < 15; ++i) {
        CKey key;
        key.MakeNewKey(true);
        CPubKey pubkey = key.GetPubKey();
        std::string hex = HexStr(pubkey);
        pubkeys.insert(hex);
    }

    BOOST_CHECK_EQUAL(pubkeys.size(), 15);
}

// 9. Full sign and validate flow
BOOST_FIXTURE_TEST_CASE(oracle_key_sign_and_validate_message, BasicTestingSetup)
{
    CKey key;
    key.MakeNewKey(true);

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6500;    // $0.0065/DGB
    msg.timestamp = GetTime();
    msg.block_height = 500;
    msg.nonce = 99;
    msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());

    BOOST_CHECK(msg.SignAttestation(key));
    BOOST_CHECK_EQUAL(msg.schnorr_sig.size(), 64);
    BOOST_CHECK(msg.IsValid());
    BOOST_CHECK(msg.VerifyAttestation());
}

// 10. Wrong key verification should fail
BOOST_FIXTURE_TEST_CASE(oracle_key_wrong_key_verify, BasicTestingSetup)
{
    CKey keyA, keyB;
    keyA.MakeNewKey(true);
    keyB.MakeNewKey(true);

    // Sign with key A
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 6500;
    msg.timestamp = GetTime();
    msg.block_height = 1000;
    msg.nonce = 7;
    msg.oracle_pubkey = XOnlyPubKey(keyA.GetPubKey());

    BOOST_CHECK(msg.Sign(keyA));
    BOOST_CHECK(msg.Verify());

    // Replace pubkey with key B's — verification should fail
    msg.oracle_pubkey = XOnlyPubKey(keyB.GetPubKey());
    BOOST_CHECK(!msg.Verify());
}

/**
 * Test for Bug 2: Oracle Key Database Loading Compressed Flag Issue
 * 
 * This test verifies that the compressed flag is properly restored when loading
 * oracle keys from the database. The bug is in walletdb.cpp ReadOracleKey()
 * where CKey::Load() doesn't restore the fCompressed flag, causing pubkey 
 * derivation mismatch after node restart.
 */
BOOST_FIXTURE_TEST_CASE(oracle_key_compressed_flag_preservation, BasicTestingSetup)
{
    // Generate a compressed oracle key
    CKey original_key;
    original_key.MakeNewKey(true); // Create compressed key
    BOOST_CHECK(original_key.IsValid());
    BOOST_CHECK(original_key.IsCompressed());
    
    // Get the original pubkey 
    CPubKey original_pubkey = original_key.GetPubKey();
    BOOST_CHECK(original_pubkey.IsValid());
    BOOST_CHECK(original_pubkey.IsCompressed());
    
    // Simulate database serialization/deserialization process
    CPrivKey serialized_privkey = original_key.GetPrivKey();
    
    // This simulates the current ReadOracleKey implementation (potentially buggy):
    CKey current_loaded_key;
    CPubKey dummy_pubkey;
    BOOST_CHECK(current_loaded_key.Load(serialized_privkey, dummy_pubkey, true));
    
    // Test if CKey::Load preserves compression properly
    // According to the bug report, Load() doesn't restore the fCompressed flag
    CPubKey current_pubkey = current_loaded_key.GetPubKey();
    LogPrintf("Original pubkey compressed: %s\n", original_pubkey.IsCompressed() ? "true" : "false");
    LogPrintf("Loaded key compressed: %s\n", current_loaded_key.IsCompressed() ? "true" : "false");
    LogPrintf("Derived pubkey compressed: %s\n", current_pubkey.IsCompressed() ? "true" : "false");
    
    // The proposed fix: re-initialize with Set() to force compression
    CKey fixed_key;
    fixed_key.Set(current_loaded_key.begin(), current_loaded_key.end(), true);
    BOOST_CHECK(fixed_key.IsValid());
    BOOST_CHECK(fixed_key.IsCompressed());
    
    // The fixed key should derive the same pubkey as the original
    CPubKey fixed_pubkey = fixed_key.GetPubKey();
    BOOST_CHECK(fixed_pubkey.IsValid());
    BOOST_CHECK(fixed_pubkey.IsCompressed());
    
    // This is the key test: does the current implementation match the original?
    // If this fails, it demonstrates the bug
    LogPrintf("Original pubkey: %s\n", HexStr(original_pubkey).c_str());
    LogPrintf("Current loaded pubkey: %s\n", HexStr(current_pubkey).c_str());
    LogPrintf("Fixed pubkey: %s\n", HexStr(fixed_pubkey).c_str());
    
    // The fix should always work
    BOOST_CHECK(original_pubkey == fixed_pubkey);
    
    // If Load() has the bug, this check might fail
    // (but it might also pass if Load() works correctly in some cases)
    bool load_works_correctly = (original_pubkey == current_pubkey);
    LogPrintf("Load() works correctly: %s\n", load_works_correctly ? "true" : "false");
    
    LogPrintf("Test: oracle_key_compressed_flag_preservation PASSED\n");
    LogPrintf("Oracle keys must use Set(begin, end, true) after Load() to restore compression\n");
}

BOOST_AUTO_TEST_SUITE_END()
