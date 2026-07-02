// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <base58.h>
#include <addresstype.h>
#include <chainparams.h>
#include <key_io.h>
#include <pubkey.h>
#include <script/script.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(digidollar_address_tests, BasicTestingSetup)

// Test that CDigiDollarAddress class exists and can be instantiated
BOOST_AUTO_TEST_CASE(digidollar_address_class_exists)
{
    // This test will fail until we implement CDigiDollarAddress
    CDigiDollarAddress addr;
    BOOST_CHECK(true); // Will fail to compile initially
}

// Test DD prefix for mainnet addresses
BOOST_AUTO_TEST_CASE(dd_mainnet_prefix_test)
{
    // Create a sample P2TR destination for testing
    uint256 hash;
    hash.SetHex("1234567890123456789012345678901234567890123456789012345678901234");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);
    CTxDestination dest = taproot_dest;

    CDigiDollarAddress addr;
    // Should be able to set a DigiDollar address from P2TR destination
    BOOST_CHECK(addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS));

    std::string dd_address = addr.ToString();

    // Should start with "DD" for mainnet
    BOOST_CHECK(dd_address.length() >= 2);
    BOOST_CHECK_EQUAL(dd_address.substr(0, 2), "DD");
}

// Test TD prefix for testnet addresses
BOOST_AUTO_TEST_CASE(td_testnet_prefix_test)
{
    // Create a sample P2TR destination for testing
    uint256 hash;
    hash.SetHex("abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);
    CTxDestination dest = taproot_dest;

    CDigiDollarAddress addr;
    // Should be able to set a DigiDollar address from P2TR destination
    BOOST_CHECK(addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS_TESTNET));

    std::string td_address = addr.ToString();

    // Should start with "TD" for testnet
    BOOST_CHECK(td_address.length() >= 2);
    BOOST_CHECK_EQUAL(td_address.substr(0, 2), "TD");
}

// Test RD prefix for regtest addresses
BOOST_AUTO_TEST_CASE(rd_regtest_prefix_test)
{
    // Create a sample P2TR destination for testing
    uint256 hash;
    hash.SetHex("fedcbafedcbafedcbafedcbafedcbafedcbafedcbafedcbafedcbafedcbafed");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);
    CTxDestination dest = taproot_dest;

    CDigiDollarAddress addr;
    // Should be able to set a DigiDollar address from P2TR destination
    BOOST_CHECK(addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS_REGTEST));

    std::string rd_address = addr.ToString();

    // Should start with "RD" for regtest
    BOOST_CHECK(rd_address.length() >= 2);
    BOOST_CHECK_EQUAL(rd_address.substr(0, 2), "RD");
}

// Test encoding from P2TR destinations only
BOOST_AUTO_TEST_CASE(p2tr_only_encoding_test)
{
    CDigiDollarAddress addr;

    // Test with non-P2TR destinations - should fail

    // PKHash destination
    uint160 pubkey_hash;
    pubkey_hash.SetHex("1234567890123456789012345678901234567890");
    PKHash pk_dest(pubkey_hash);
    BOOST_CHECK(!addr.SetDigiDollar(pk_dest, CChainParams::DIGIDOLLAR_ADDRESS));

    // ScriptHash destination
    uint160 script_hash;
    script_hash.SetHex("abcdefabcdefabcdefabcdefabcdefabcdefabcd");
    ScriptHash script_dest(script_hash);
    BOOST_CHECK(!addr.SetDigiDollar(script_dest, CChainParams::DIGIDOLLAR_ADDRESS));

    // WitnessV0KeyHash destination
    WitnessV0KeyHash witness_key_dest(pubkey_hash);
    BOOST_CHECK(!addr.SetDigiDollar(witness_key_dest, CChainParams::DIGIDOLLAR_ADDRESS));

    // Only P2TR should work
    uint256 hash;
    hash.SetHex("1234567890123456789012345678901234567890123456789012345678901234");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);
    CTxDestination dest = taproot_dest;
    BOOST_CHECK(addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS));
}

// Test decoding back to destinations
BOOST_AUTO_TEST_CASE(bidirectional_encoding_test)
{
    // Create original P2TR destination
    uint256 original_hash;
    original_hash.SetHex("1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff");
    XOnlyPubKey original_xonly(original_hash);
    WitnessV1Taproot original_taproot(original_xonly);
    CTxDestination original_dest = original_taproot;

    // Encode to DigiDollar address
    CDigiDollarAddress addr;
    BOOST_CHECK(addr.SetDigiDollar(original_dest, CChainParams::DIGIDOLLAR_ADDRESS));

    // Decode back to destination
    CTxDestination decoded_dest = addr.GetDigiDollarDestination();

    // Should match original
    BOOST_CHECK(std::holds_alternative<WitnessV1Taproot>(decoded_dest));
    WitnessV1Taproot decoded_taproot = std::get<WitnessV1Taproot>(decoded_dest);

    // The decoded pubkey should match the original
    BOOST_CHECK(decoded_taproot == original_taproot);
}

// Test validation of DD address format
BOOST_AUTO_TEST_CASE(address_format_validation_test)
{
    // Generate valid DD/TD/RD addresses via the encoder to test round-trip
    // Create a dummy 32-byte taproot key
    XOnlyPubKey xpk;
    {
        std::vector<unsigned char> dummy(32, 0x42);
        std::copy(dummy.begin(), dummy.end(), xpk.begin());
    }
    WitnessV1Taproot taproot(xpk);
    CTxDestination dest(taproot);

    CDigiDollarAddress ddAddr, tdAddr, rdAddr;
    BOOST_CHECK(ddAddr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS));
    BOOST_CHECK(tdAddr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS_TESTNET));
    BOOST_CHECK(rdAddr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS_REGTEST));

    std::string ddStr = ddAddr.ToString();
    std::string tdStr = tdAddr.ToString();
    std::string rdStr = rdAddr.ToString();

    // Valid DD addresses should be detected
    BOOST_CHECK(CDigiDollarAddress::IsValidDigiDollarAddress(ddStr));
    BOOST_CHECK(CDigiDollarAddress::IsValidDigiDollarAddress(tdStr));
    BOOST_CHECK(CDigiDollarAddress::IsValidDigiDollarAddress(rdStr));

    // Invalid prefixes should be rejected
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("BD1234567890abcdef"));  // Bad prefix
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("AD1234567890abcdef"));  // Bad prefix
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("D1234567890abcdef"));   // Too short
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress(""));                    // Empty
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("1DD234567890abcdef"));  // Wrong position
    // Fake prefix+junk (no valid base58check)
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("DD1234567890abcdef"));
}

// Test rejection of invalid addresses
BOOST_AUTO_TEST_CASE(invalid_address_rejection_test)
{
    // Invalid destinations should be rejected
    CNoDestination no_dest;
    CDigiDollarAddress addr;
    BOOST_CHECK(!addr.SetDigiDollar(no_dest, CChainParams::DIGIDOLLAR_ADDRESS));

    // Invalid address type should be rejected
    uint256 hash = uint256S("1234567890123456789012345678901234567890123456789012345678901234");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);
    CTxDestination dest = taproot_dest;

    // Should fail with invalid address type
    BOOST_CHECK(!addr.SetDigiDollar(dest, CChainParams::SECRET_KEY)); // Wrong type
}

// Test base58 checksum validation
BOOST_AUTO_TEST_CASE(base58_checksum_validation_test)
{
    // Create a valid DigiDollar address
    uint256 hash;
    hash.SetHex("1234567890123456789012345678901234567890123456789012345678901234");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);
    CTxDestination dest = taproot_dest;

    CDigiDollarAddress addr;
    BOOST_CHECK(addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS));

    std::string valid_address = addr.ToString();
    BOOST_CHECK(!valid_address.empty());

    // Corrupt the checksum by changing last character
    if (valid_address.length() > 0) {
        std::string corrupted_address = valid_address;
        corrupted_address.back() = (corrupted_address.back() == 'A') ? 'B' : 'A';

        // Should fail to decode with corrupted checksum
        CDigiDollarAddress corrupted_addr(corrupted_address);
        CTxDestination corrupted_dest = corrupted_addr.GetDigiDollarDestination();
        BOOST_CHECK(std::holds_alternative<CNoDestination>(corrupted_dest));
    }
}

// Test helper functions EncodeDigiDollarAddress and DecodeDigiDollarAddress
BOOST_AUTO_TEST_CASE(helper_functions_test)
{
    // Create P2TR destination
    uint256 hash;
    hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);
    CTxDestination dest = taproot_dest;

    // Test encoding helper function
    std::string encoded = EncodeDigiDollarAddress(dest);
    BOOST_CHECK(!encoded.empty());
    BOOST_CHECK(encoded.length() >= 2);

    // Test decoding helper function
    CTxDestination decoded = DecodeDigiDollarAddress(encoded);
    BOOST_CHECK(std::holds_alternative<WitnessV1Taproot>(decoded));

    // Should round-trip correctly
    WitnessV1Taproot decoded_taproot = std::get<WitnessV1Taproot>(decoded);
    BOOST_CHECK(decoded_taproot == taproot_dest);
}

// Test address length and format consistency
BOOST_AUTO_TEST_CASE(address_format_consistency_test)
{
    // Test multiple different P2TR destinations
    std::vector<std::string> test_hashes = {
        "0000000000000000000000000000000000000000000000000000000000000001",
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef",
        "fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321"
    };

    for (const auto& hash_str : test_hashes) {
        uint256 hash;
        hash.SetHex(hash_str);
        XOnlyPubKey xonly_pubkey(hash);
        WitnessV1Taproot taproot_dest(xonly_pubkey);
        CTxDestination dest = taproot_dest;

        CDigiDollarAddress addr;
        BOOST_CHECK(addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS));

        std::string dd_address = addr.ToString();

        // All addresses should start with DD for mainnet
        BOOST_CHECK_EQUAL(dd_address.substr(0, 2), "DD");

        // Should be decodable
        CTxDestination decoded = addr.GetDigiDollarDestination();
        BOOST_CHECK(std::holds_alternative<WitnessV1Taproot>(decoded));
    }
}

BOOST_AUTO_TEST_CASE(network_specific_digidollar_address_validation_test)
{
    struct ParamsRestorer {
        ChainType original;
        ~ParamsRestorer() { SelectParams(original); }
    } restore{Params().GetChainType()};

    uint256 hash;
    hash.SetHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    XOnlyPubKey xonly_pubkey(hash);
    WitnessV1Taproot taproot_dest(xonly_pubkey);
    CTxDestination dest = taproot_dest;

    CDigiDollarAddress main_addr;
    CDigiDollarAddress test_addr;
    CDigiDollarAddress regtest_addr;
    BOOST_REQUIRE(main_addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS));
    BOOST_REQUIRE(test_addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS_TESTNET));
    BOOST_REQUIRE(regtest_addr.SetDigiDollar(dest, CChainParams::DIGIDOLLAR_ADDRESS_REGTEST));

    const std::string mainnet_dd = main_addr.ToString();
    const std::string testnet_td = test_addr.ToString();
    const std::string regtest_rd = regtest_addr.ToString();

    SelectParams(ChainType::MAIN);
    BOOST_CHECK(CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(mainnet_dd));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(testnet_td));

    SelectParams(ChainType::TESTNET);
    BOOST_CHECK(CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(testnet_td));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(mainnet_dd));

    SelectParams(ChainType::REGTEST);
    BOOST_CHECK(CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(regtest_rd));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(mainnet_dd));
}

BOOST_AUTO_TEST_SUITE_END()
