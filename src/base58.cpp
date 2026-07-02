// Copyright (c) 2014-2022 The Bitcoin Core developers
// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <base58.h>

#include <addresstype.h>
#include <hash.h>
#include <chainparams.h>
#include <kernel/chainparams.h>
#include <pubkey.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <assert.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <variant>

/** All alphanumeric characters except for "0", "I", "O", and "l" */
static const char* pszBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const int8_t mapBase58[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6,  7, 8,-1,-1,-1,-1,-1,-1,
    -1, 9,10,11,12,13,14,15, 16,-1,17,18,19,20,21,-1,
    22,23,24,25,26,27,28,29, 30,31,32,-1,-1,-1,-1,-1,
    -1,33,34,35,36,37,38,39, 40,41,42,43,-1,44,45,46,
    47,48,49,50,51,52,53,54, 55,56,57,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
};

[[nodiscard]] static bool DecodeBase58(const char* psz, std::vector<unsigned char>& vch, int max_ret_len)
{
    // Skip leading spaces.
    while (*psz && IsSpace(*psz))
        psz++;
    // Skip and count leading '1's.
    int zeroes = 0;
    int length = 0;
    while (*psz == '1') {
        zeroes++;
        if (zeroes > max_ret_len) return false;
        psz++;
    }
    // Allocate enough space in big-endian base256 representation.
    int size = strlen(psz) * 733 /1000 + 1; // log(58) / log(256), rounded up.
    std::vector<unsigned char> b256(size);
    // Process the characters.
    static_assert(std::size(mapBase58) == 256, "mapBase58.size() should be 256"); // guarantee not out of range
    while (*psz && !IsSpace(*psz)) {
        // Decode base58 character
        int carry = mapBase58[(uint8_t)*psz];
        if (carry == -1)  // Invalid b58 character
            return false;
        int i = 0;
        for (std::vector<unsigned char>::reverse_iterator it = b256.rbegin(); (carry != 0 || i < length) && (it != b256.rend()); ++it, ++i) {
            carry += 58 * (*it);
            *it = carry % 256;
            carry /= 256;
        }
        assert(carry == 0);
        length = i;
        if (length + zeroes > max_ret_len) return false;
        psz++;
    }
    // Skip trailing spaces.
    while (IsSpace(*psz))
        psz++;
    if (*psz != 0)
        return false;
    // Skip leading zeroes in b256.
    std::vector<unsigned char>::iterator it = b256.begin() + (size - length);
    // Copy result into output vector.
    vch.reserve(zeroes + (b256.end() - it));
    vch.assign(zeroes, 0x00);
    while (it != b256.end())
        vch.push_back(*(it++));
    return true;
}

std::string EncodeBase58(Span<const unsigned char> input)
{
    // Skip & count leading zeroes.
    int zeroes = 0;
    int length = 0;
    while (input.size() > 0 && input[0] == 0) {
        input = input.subspan(1);
        zeroes++;
    }
    // Allocate enough space in big-endian base58 representation.
    int size = input.size() * 138 / 100 + 1; // log(256) / log(58), rounded up.
    std::vector<unsigned char> b58(size);
    // Process the bytes.
    while (input.size() > 0) {
        int carry = input[0];
        int i = 0;
        // Apply "b58 = b58 * 256 + ch".
        for (std::vector<unsigned char>::reverse_iterator it = b58.rbegin(); (carry != 0 || i < length) && (it != b58.rend()); it++, i++) {
            carry += 256 * (*it);
            *it = carry % 58;
            carry /= 58;
        }

        assert(carry == 0);
        length = i;
        input = input.subspan(1);
    }
    // Skip leading zeroes in base58 result.
    std::vector<unsigned char>::iterator it = b58.begin() + (size - length);
    while (it != b58.end() && *it == 0)
        it++;
    // Translate the result into a string.
    std::string str;
    str.reserve(zeroes + (b58.end() - it));
    str.assign(zeroes, '1');
    while (it != b58.end())
        str += pszBase58[*(it++)];
    return str;
}

bool DecodeBase58(const std::string& str, std::vector<unsigned char>& vchRet, int max_ret_len)
{
    if (!ContainsNoNUL(str)) {
        return false;
    }
    return DecodeBase58(str.c_str(), vchRet, max_ret_len);
}

std::string EncodeBase58Check(Span<const unsigned char> input)
{
    // add 4-byte hash check to the end
    std::vector<unsigned char> vch(input.begin(), input.end());
    uint256 hash = Hash(vch);
    vch.insert(vch.end(), (unsigned char*)&hash, (unsigned char*)&hash + 4);
    return EncodeBase58(vch);
}

[[nodiscard]] static bool DecodeBase58Check(const char* psz, std::vector<unsigned char>& vchRet, int max_ret_len)
{
    if (!DecodeBase58(psz, vchRet, max_ret_len > std::numeric_limits<int>::max() - 4 ? std::numeric_limits<int>::max() : max_ret_len + 4) ||
        (vchRet.size() < 4)) {
        vchRet.clear();
        return false;
    }
    // re-calculate the checksum, ensure it matches the included 4-byte checksum
    uint256 hash = Hash(Span{vchRet}.first(vchRet.size() - 4));
    if (memcmp(&hash, &vchRet[vchRet.size() - 4], 4) != 0) {
        vchRet.clear();
        return false;
    }
    vchRet.resize(vchRet.size() - 4);
    return true;
}

bool DecodeBase58Check(const std::string& str, std::vector<unsigned char>& vchRet, int max_ret)
{
    if (!ContainsNoNUL(str)) {
        return false;
    }
    return DecodeBase58Check(str.c_str(), vchRet, max_ret);
}

//
// DigiDollar address implementation
//

// Static constants for version bytes that generate correct prefixes
const std::vector<unsigned char> CDigiDollarAddress::DD_P2TR_MAINNET = {0x52, 0x85};  // "DD"
const std::vector<unsigned char> CDigiDollarAddress::DD_P2TR_TESTNET = {0xb1, 0x29};  // "TD"
const std::vector<unsigned char> CDigiDollarAddress::DD_P2TR_REGTEST = {0xa3, 0xa4};  // "RD"

CDigiDollarAddress::CDigiDollarAddress() : fValid(false)
{
}

CDigiDollarAddress::CDigiDollarAddress(const std::string& str) : fValid(false), original_str(str)
{
    // DD-FA-FUNC-019 (Wave 15): DecodeBase58 transparently strips leading and
    // trailing ASCII whitespace, so a base58check address wrapped in spaces
    // would otherwise decode silently. Reject any whitespace anywhere in the
    // input so the canonical address echoed by validateddaddress, the
    // to_address echoed by senddigidollar, and any other consumer cannot be a
    // whitespace-corrupted variant of the user's intended target. This also
    // catches embedded \t / \r / \n that copy-paste flows can introduce.
    for (unsigned char c : str) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            return;
        }
    }

    std::vector<unsigned char> vchTemp;
    if (DecodeBase58Check(str, vchTemp, 256)) {
        if (vchTemp.size() >= 2) {
            if (vchTemp.size() == 34) { // 2 byte version + 32 bytes data
                vchVersion.assign(vchTemp.begin(), vchTemp.begin() + 2);
                vchData.assign(vchTemp.begin() + 2, vchTemp.end());
                fValid = (vchVersion == DD_P2TR_MAINNET ||
                         vchVersion == DD_P2TR_TESTNET ||
                         vchVersion == DD_P2TR_REGTEST);
                // Clear original_str for valid addresses (not needed)
                if (fValid) {
                    original_str.clear();
                }
            }
        }
    }
}

bool CDigiDollarAddress::SetDigiDollar(const CTxDestination& dest, int type)
{
    // Reset state
    vchData.clear();
    vchVersion.clear();
    fValid = false;

    // Check if destination is valid
    if (!IsValidDestination(dest)) {
        return false;
    }

    // Only P2TR addresses can be DigiDollar addresses
    if (!std::holds_alternative<WitnessV1Taproot>(dest)) {
        return false;
    }

    const WitnessV1Taproot& taproot = std::get<WitnessV1Taproot>(dest);

    // Set appropriate version based on network type
    switch (type) {
        case CChainParams::DIGIDOLLAR_ADDRESS:
            vchVersion = DD_P2TR_MAINNET;
            break;
        case CChainParams::DIGIDOLLAR_ADDRESS_TESTNET:
            vchVersion = DD_P2TR_TESTNET;
            break;
        case CChainParams::DIGIDOLLAR_ADDRESS_REGTEST:
            vchVersion = DD_P2TR_REGTEST;
            break;
        default:
            return false;
    }

    // Copy the 32-byte pubkey data
    vchData.resize(32);
    std::copy(taproot.begin(), taproot.end(), vchData.begin());
    fValid = true;

    return true;
}

CTxDestination CDigiDollarAddress::GetDigiDollarDestination() const
{
    if (!IsValid()) {
        return CNoDestination();
    }

    if (vchData.size() != 32) {
        return CNoDestination();
    }

    // Convert to P2TR destination
    uint256 hash;
    std::copy(vchData.begin(), vchData.end(), hash.begin());
    return WitnessV1Taproot(XOnlyPubKey(hash));
}

std::string CDigiDollarAddress::ToString() const
{
    if (!IsValid()) {
        return "";
    }

    // Create version + data vector
    std::vector<unsigned char> vch;
    vch.reserve(34);
    vch.insert(vch.end(), vchVersion.begin(), vchVersion.end());
    vch.insert(vch.end(), vchData.begin(), vchData.end());

    return EncodeBase58Check(vch);
}

bool CDigiDollarAddress::IsValid() const
{
    return fValid && vchData.size() == 32 && vchVersion.size() == 2;
}

bool CDigiDollarAddress::IsValidForCurrentNetwork() const
{
    if (!IsValid()) {
        return false;
    }

    switch (Params().GetChainType()) {
    case ChainType::REGTEST:
        return vchVersion == DD_P2TR_REGTEST;
    case ChainType::TESTNET:
        return vchVersion == DD_P2TR_TESTNET;
    case ChainType::MAIN:
    case ChainType::SIGNET:
        return vchVersion == DD_P2TR_MAINNET;
    }

    return false;
}

bool CDigiDollarAddress::IsValidDigiDollarAddress(const std::string& str)
{
    // Reject embedded null bytes
    if (str.find('\0') != std::string::npos) {
        return false;
    }

    return CDigiDollarAddress(str).IsValid();
}

bool CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(const std::string& str)
{
    // Reject embedded null bytes
    if (str.find('\0') != std::string::npos) {
        return false;
    }

    return CDigiDollarAddress(str).IsValidForCurrentNetwork();
}

//
// Helper functions
//

std::string EncodeDigiDollarAddress(const CTxDestination& dest)
{
    CDigiDollarAddress addr;
    // Determine network type from global chain params
    const CChainParams& chainParams = Params();
    std::string chainType = chainParams.GetChainTypeString();
    int networkType;
    if (chainType == "regtest") {
        networkType = CChainParams::DIGIDOLLAR_ADDRESS_REGTEST;
    } else if (chainType == "test") {
        networkType = CChainParams::DIGIDOLLAR_ADDRESS_TESTNET;
    } else {
        networkType = CChainParams::DIGIDOLLAR_ADDRESS;
    }
    if (!addr.SetDigiDollar(dest, networkType)) {
        return "";
    }
    return addr.ToString();
}

CTxDestination DecodeDigiDollarAddress(const std::string& str)
{
    CDigiDollarAddress addr(str);
    return addr.GetDigiDollarDestination();
}
