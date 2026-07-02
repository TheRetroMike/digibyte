// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
/**
 * Why base-58 instead of standard base-64 encoding?
 * - Don't want 0OIl characters that look the same in some fonts and
 *      could be used to create visually identical looking data.
 * - A string with non-alphanumeric characters is not as easily accepted as input.
 * - E-mail usually won't line-break if there's no punctuation to break at.
 * - Double-clicking selects the whole string as one word if it's all alphanumeric.
 */
#ifndef DIGIBYTE_BASE58_H
#define DIGIBYTE_BASE58_H

#include <addresstype.h>
#include <span.h>

#include <string>
#include <vector>

/**
 * Encode a byte span as a base58-encoded string
 */
std::string EncodeBase58(Span<const unsigned char> input);

/**
 * Decode a base58-encoded string (str) into a byte vector (vchRet).
 * return true if decoding is successful.
 */
[[nodiscard]] bool DecodeBase58(const std::string& str, std::vector<unsigned char>& vchRet, int max_ret_len);

/**
 * Encode a byte span into a base58-encoded string, including checksum
 */
std::string EncodeBase58Check(Span<const unsigned char> input);

/**
 * Decode a base58-encoded string (str) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
[[nodiscard]] bool DecodeBase58Check(const std::string& str, std::vector<unsigned char>& vchRet, int max_ret_len);

/**
 * DigiDollar address encoding class
 * Supports DD (mainnet), TD (testnet), and RD (regtest) prefixes
 * Only works with P2TR (Taproot) destinations
 */
class CDigiDollarAddress
{
private:
    std::vector<unsigned char> vchData;
    std::vector<unsigned char> vchVersion;
    bool fValid;
    std::string original_str;  // Store original string for invalid addresses (testing support)

public:
    // Version bytes for DigiDollar addresses (2-byte prefixes)
    static const std::vector<unsigned char> DD_P2TR_MAINNET;  // Generates "DD" prefix
    static const std::vector<unsigned char> DD_P2TR_TESTNET;  // Generates "TD" prefix
    static const std::vector<unsigned char> DD_P2TR_REGTEST;  // Generates "RD" prefix

    CDigiDollarAddress();
    explicit CDigiDollarAddress(const std::string& str);

    bool SetDigiDollar(const CTxDestination& dest, int type);
    CTxDestination GetDigiDollarDestination() const;
    std::string ToString() const;
    bool IsValid() const;
    bool IsValidForCurrentNetwork() const;

    static bool IsValidDigiDollarAddress(const std::string& str);
    static bool IsValidDigiDollarAddressForCurrentNetwork(const std::string& str);

    // Serialization support
    template<typename Stream>
    void Serialize(Stream& s) const {
        std::string str = fValid ? ToString() : original_str;
        s << str;
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        std::string str;
        s >> str;
        *this = CDigiDollarAddress(str);
    }

    // Comparison operator for testing
    bool operator==(const CDigiDollarAddress& other) const {
        return vchData == other.vchData && vchVersion == other.vchVersion && fValid == other.fValid && original_str == other.original_str;
    }
};

// Helper functions for DigiDollar addresses
std::string EncodeDigiDollarAddress(const CTxDestination& dest);
CTxDestination DecodeDigiDollarAddress(const std::string& str);

#endif // DIGIBYTE_BASE58_H
