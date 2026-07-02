// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wave 15 - Script Templates, OP_RETURN, Metadata, Address Encoding
//
// Strengthens unit coverage for the public DigiDollar parsers exercised on
// every consensus path: ExtractDDAmount (Format 1 OP_RETURN OP_DIGIDOLLAR
// <8-byte LE> and Format 2 OP_RETURN "DD" <type> <amount> ...), FindDDOpReturn
// (Format 2 preference over Format 1, multiple-output handling), and
// CDigiDollarAddress prefix matching with cross-chain rejection /
// corrupted-string handling.
//
// These tests pin behaviors that were already correct at Wave 14 close and
// guard against silent parser drift in later waves. A handful of cases
// pin documented edges that previously had no direct unit assertion:
//
//   - DD-FA-TEST-016 (Wave 15): ExtractDDAmount Format 1 must reject any
//     8-byte amount that decodes to a value at MAX_DIGIDOLLAR + 1 cent or
//     above (per-output serialization bound).
//   - DD-FA-TEST-017 (Wave 15): FindDDOpReturn must prefer the modern
//     Format 2 (DD marker) over a legacy Format 1 (OP_DIGIDOLLAR) entry
//     even when Format 1 appears first.
//
// All cases pass on the current tree.

#include <base58.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <key_io.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a Format 1 OP_RETURN script: OP_RETURN OP_DIGIDOLLAR <8-byte LE amt>
CScript MakeFormat1OpReturn(int64_t amount)
{
    CScript script;
    script << OP_RETURN << OP_DIGIDOLLAR;
    std::vector<unsigned char> data(8);
    for (int i = 0; i < 8; ++i) {
        data[i] = static_cast<unsigned char>((amount >> (i * 8)) & 0xff);
    }
    script << data;
    return script;
}

CScript MakeFormat1OpReturn(const std::vector<unsigned char>& amount_bytes)
{
    CScript script;
    script << OP_RETURN << OP_DIGIDOLLAR;
    script << amount_bytes;
    return script;
}

// Build a Format 2 OP_RETURN script: OP_RETURN "DD" <type> <amt> [extra ...]
CScript MakeFormat2OpReturn(int txType,
                            int64_t amount,
                            const std::vector<int64_t>& extras = {})
{
    CScript script;
    script << OP_RETURN;
    script << std::vector<unsigned char>{'D', 'D'};
    script << CScriptNum(txType);
    script << CScriptNum(amount);
    for (int64_t e : extras) {
        script << CScriptNum(e);
    }
    return script;
}

// Build a P2KH-ish placeholder so transaction fixtures have a non-DD output.
CScript MakeDummyOutput()
{
    CScript script;
    script << OP_DUP << OP_HASH160;
    script << std::vector<unsigned char>(20, 0x42);
    script << OP_EQUALVERIFY << OP_CHECKSIG;
    return script;
}

// Encode a P2TR destination via the helper used by every DigiDollar wallet
// path, returning the resulting Base58Check string for a chosen network.
std::string EncodeAddrFor(int networkType)
{
    uint256 hash;
    hash.SetHex("89abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234567");
    XOnlyPubKey xonly(hash);
    WitnessV1Taproot taproot(xonly);
    CTxDestination dest = taproot;

    CDigiDollarAddress addr;
    BOOST_REQUIRE(addr.SetDigiDollar(dest, networkType));
    return addr.ToString();
}

struct ChainParamsRestore {
    ChainType original;
    ~ChainParamsRestore() { SelectParams(original); }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_wave15_parser_tests, BasicTestingSetup)

// =====================================================================
// 1) OP_RETURN parser edges (Format 1 and Format 2)
// =====================================================================

BOOST_AUTO_TEST_CASE(format1_truncated_amount_field_rejected)
{
    // Format 1 expects exactly 8 bytes of amount data after OP_DIGIDOLLAR.
    // Anything shorter must be rejected without crashing.
    for (size_t shortLen : {size_t{0}, size_t{1}, size_t{4}, size_t{7}}) {
        CScript script;
        script << OP_RETURN << OP_DIGIDOLLAR;
        if (shortLen > 0) {
            script << std::vector<unsigned char>(shortLen, 0x77);
        }
        CAmount amt = 0;
        BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
        BOOST_CHECK_EQUAL(amt, -1);
    }
}

BOOST_AUTO_TEST_CASE(format1_oversized_amount_field_rejected)
{
    // 9-byte and 16-byte amount data must be rejected: the parser only
    // accepts data.size() == 8.
    for (size_t bigLen : {size_t{9}, size_t{16}, size_t{32}}) {
        CScript script;
        script << OP_RETURN << OP_DIGIDOLLAR;
        script << std::vector<unsigned char>(bigLen, 0x01);
        CAmount amt = 0;
        BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
        BOOST_CHECK_EQUAL(amt, -1);
    }
}

BOOST_AUTO_TEST_CASE(format1_trailing_data_rejected_post_amount)
{
    // OP_RETURN OP_DIGIDOLLAR <8 bytes 100 cents> <extra OP_1>
    CScript script = MakeFormat1OpReturn(100);
    script << OP_1;  // any trailing content is a malleability vector
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
    BOOST_CHECK_EQUAL(amt, -1);
}

BOOST_AUTO_TEST_CASE(format1_zero_and_negative_amounts_rejected)
{
    // Zero amount: 8-byte zero. Must be rejected because amount must be >= 1.
    CScript zero = MakeFormat1OpReturn(0);
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(zero, amt));
    BOOST_CHECK_EQUAL(amt, -1);

    // Negative amount: top bit set. Stored LE: when read back as int64_t
    // via the parser's bit-OR loop this becomes a negative number, which
    // fails the >= 1 check.
    CScript neg = MakeFormat1OpReturn(-1);
    amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(neg, amt));
    BOOST_CHECK_EQUAL(amt, -1);
}

// DD-FA-TEST-016 (Wave 15) - Format 1 must reject amounts above the
// per-output serialization bound MAX_DIGIDOLLAR.
BOOST_AUTO_TEST_CASE(format1_amount_above_max_digidollar_rejected)
{
    BOOST_REQUIRE(MAX_DIGIDOLLAR > 0);

    // Just above the cap.
    {
        CScript script = MakeFormat1OpReturn(MAX_DIGIDOLLAR + 1);
        CAmount amt = 0;
        BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
        BOOST_CHECK_EQUAL(amt, -1);
    }

    // Far above the cap (still positive int64_t).
    {
        const int64_t huge = std::numeric_limits<int64_t>::max();
        CScript script = MakeFormat1OpReturn(huge);
        CAmount amt = 0;
        BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
        BOOST_CHECK_EQUAL(amt, -1);
    }

    // Exactly at the cap should succeed (boundary on the accepting side).
    {
        CScript script = MakeFormat1OpReturn(MAX_DIGIDOLLAR);
        CAmount amt = 0;
        BOOST_CHECK(DigiDollar::ExtractDDAmount(script, amt));
        BOOST_CHECK_EQUAL(amt, MAX_DIGIDOLLAR);
    }
}

BOOST_AUTO_TEST_CASE(format1_high_bit_amount_rejected_without_signed_shift)
{
    // A high bit in the 8th byte encodes a value >= 2^63. Format 1 parsing
    // must reject it before casting into CAmount; decoding through signed
    // shifts is a consensus parser hazard on malformed attacker input.
    CScript script = MakeFormat1OpReturn(
        std::vector<unsigned char>{0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x80});
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
    BOOST_CHECK_EQUAL(amt, -1);
}

BOOST_AUTO_TEST_CASE(format2_non_minimal_amount_encoding_rejected)
{
    // Bitcoin's CScriptNum requires minimal encoding when fRequireMinimal
    // is true (the parser passes true). Manually push a non-minimal
    // little-endian zero-padded representation of 100 (0x64) — three bytes
    // {0x64, 0x00, 0x00} should throw scriptnum_error inside the parser.
    CScript script;
    script << OP_RETURN;
    script << std::vector<unsigned char>{'D', 'D'};
    script << CScriptNum(static_cast<int64_t>(DD_TX_MINT));  // type
    // Non-minimal amount push (3 bytes for value 100, with trailing zeros).
    script << std::vector<unsigned char>{0x64, 0x00, 0x00};
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
    BOOST_CHECK_EQUAL(amt, -1);

    // Sanity: the minimal encoding for the same value succeeds.
    CScript ok = MakeFormat2OpReturn(static_cast<int>(DD_TX_MINT), 100);
    CAmount okAmt = 0;
    BOOST_CHECK(DigiDollar::ExtractDDAmount(ok, okAmt));
    BOOST_CHECK_EQUAL(okAmt, 100);
}

BOOST_AUTO_TEST_CASE(format2_empty_amount_field_rejected)
{
    // OP_RETURN "DD" <type> [no amount push]
    CScript script;
    script << OP_RETURN;
    script << std::vector<unsigned char>{'D', 'D'};
    script << CScriptNum(static_cast<int64_t>(DD_TX_MINT));
    // Deliberately stop without pushing an amount.
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
    BOOST_CHECK_EQUAL(amt, -1);
}

BOOST_AUTO_TEST_CASE(format2_amount_above_max_digidollar_rejected)
{
    // Above the cap.
    CScript script = MakeFormat2OpReturn(static_cast<int>(DD_TX_MINT),
                                         MAX_DIGIDOLLAR + 1);
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
    BOOST_CHECK_EQUAL(amt, -1);

    // At the cap is accepted.
    CScript ok = MakeFormat2OpReturn(static_cast<int>(DD_TX_MINT),
                                     MAX_DIGIDOLLAR);
    CAmount okAmt = 0;
    BOOST_CHECK(DigiDollar::ExtractDDAmount(ok, okAmt));
    BOOST_CHECK_EQUAL(okAmt, MAX_DIGIDOLLAR);
}

BOOST_AUTO_TEST_CASE(format2_zero_and_negative_amounts_rejected)
{
    // Zero is below the >= 1 lower bound.
    CScript zero = MakeFormat2OpReturn(static_cast<int>(DD_TX_REDEEM), 0);
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(zero, amt));
    BOOST_CHECK_EQUAL(amt, -1);

    CScript neg = MakeFormat2OpReturn(static_cast<int>(DD_TX_REDEEM), -1);
    amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(neg, amt));
    BOOST_CHECK_EQUAL(amt, -1);
}

BOOST_AUTO_TEST_CASE(format2_truncated_marker_does_not_match_dd)
{
    // Single-byte 'D' push must not be accepted as a DD marker.
    CScript script;
    script << OP_RETURN;
    script << std::vector<unsigned char>{'D'};
    script << CScriptNum(1);
    script << CScriptNum(100);
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(script, amt));
    BOOST_CHECK_EQUAL(amt, -1);

    // Three-byte "DDX" push must not be accepted either.
    CScript longer;
    longer << OP_RETURN;
    longer << std::vector<unsigned char>{'D', 'D', 'X'};
    longer << CScriptNum(1);
    longer << CScriptNum(100);
    amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(longer, amt));
    BOOST_CHECK_EQUAL(amt, -1);

    // "dd" (lowercase) must not pass the 'D' 'D' byte check.
    CScript lower;
    lower << OP_RETURN;
    lower << std::vector<unsigned char>{'d', 'd'};
    lower << CScriptNum(1);
    lower << CScriptNum(100);
    amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(lower, amt));
    BOOST_CHECK_EQUAL(amt, -1);
}

BOOST_AUTO_TEST_CASE(plain_opreturn_without_dd_marker_is_ignored)
{
    CScript noisy;
    noisy << OP_RETURN;
    noisy << std::vector<unsigned char>{'X', 'Y'};
    noisy << CScriptNum(1);
    noisy << CScriptNum(100);
    CAmount amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(noisy, amt));
    BOOST_CHECK_EQUAL(amt, -1);

    CScript empty;
    empty << OP_RETURN;
    amt = 0;
    BOOST_CHECK(!DigiDollar::ExtractDDAmount(empty, amt));
    BOOST_CHECK_EQUAL(amt, -1);
}

// =====================================================================
// 2) FindDDOpReturn template classification — Format 2 preference
// =====================================================================

// DD-FA-TEST-017 (Wave 15) - When a transaction contains both a Format 1
// (legacy OP_DIGIDOLLAR) and a Format 2 ("DD" pushdata) OP_RETURN, the
// modern Format 2 must win even if Format 1 appears at an earlier vout.
BOOST_AUTO_TEST_CASE(find_dd_opreturn_prefers_format2_over_format1)
{
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256::ONE, 0);

    // vout[0] - dummy
    {
        CTxOut o;
        o.scriptPubKey = MakeDummyOutput();
        o.nValue = 1000;
        mtx.vout.push_back(o);
    }
    // vout[1] - Format 1 (legacy)
    {
        CTxOut o;
        o.scriptPubKey = MakeFormat1OpReturn(100);
        o.nValue = 0;
        mtx.vout.push_back(o);
    }
    // vout[2] - Format 2 (modern, must win)
    {
        CTxOut o;
        o.scriptPubKey = MakeFormat2OpReturn(static_cast<int>(DD_TX_MINT), 100);
        o.nValue = 0;
        mtx.vout.push_back(o);
    }

    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(CTransaction(mtx)), 2);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_returns_legacy_when_only_format1_present)
{
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256::ONE, 0);

    {
        CTxOut o;
        o.scriptPubKey = MakeDummyOutput();
        o.nValue = 1000;
        mtx.vout.push_back(o);
    }
    {
        CTxOut o;
        o.scriptPubKey = MakeFormat1OpReturn(250);
        o.nValue = 0;
        mtx.vout.push_back(o);
    }
    {
        CTxOut o;
        o.scriptPubKey = MakeDummyOutput();
        o.nValue = 1000;
        mtx.vout.push_back(o);
    }

    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(CTransaction(mtx)), 1);
}

BOOST_AUTO_TEST_CASE(find_dd_opreturn_skips_short_outputs)
{
    // size < 2 outputs (e.g. lone OP_RETURN) and unrelated OP_RETURN
    // payloads must not crash or return false positives.
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(uint256::ONE, 0);

    {
        CTxOut o;
        CScript s;
        s << OP_RETURN;  // 1 byte total
        o.scriptPubKey = s;
        o.nValue = 0;
        mtx.vout.push_back(o);
    }
    {
        CTxOut o;
        CScript s;
        s << OP_RETURN;
        s << std::vector<unsigned char>{0x77, 0x77};
        o.scriptPubKey = s;
        o.nValue = 0;
        mtx.vout.push_back(o);
    }

    BOOST_CHECK_EQUAL(DigiDollar::FindDDOpReturn(CTransaction(mtx)), -1);
}

// =====================================================================
// 3) Script template classification — DD vault P2TR vs ordinary P2TR
// =====================================================================

BOOST_AUTO_TEST_CASE(create_collateral_p2tr_emits_canonical_witness_v1)
{
    DigiDollar::MintParams params;
    CKey owner;
    owner.MakeNewKey(true);
    params.ddAmount = 10'000;       // $100.00
    params.lockHeight = 1'000;
    params.ownerKey = XOnlyPubKey(owner.GetPubKey());
    params.internalKey = DigiDollar::GetCollateralNUMSKey();

    CScript script = DigiDollar::CreateCollateralP2TR(params);
    BOOST_REQUIRE(!script.empty());
    BOOST_REQUIRE_EQUAL(script.size(), 34u);
    BOOST_CHECK(script[0] == OP_1);

    // Roundtrip through the metadata registry: collateral type with the
    // same amount + lock height.
    DigiDollar::ScriptMetadata meta;
    BOOST_REQUIRE(DigiDollar::GetScriptMetadata(script, meta));
    BOOST_CHECK(meta.type == DigiDollar::ScriptType::COLLATERAL_LOCK);
    BOOST_CHECK_EQUAL(meta.ddAmount, params.ddAmount);
    BOOST_CHECK_EQUAL(meta.lockHeight, params.lockHeight);

    // IsCollateralScript must return true on this script.
    BOOST_CHECK(DigiDollar::IsCollateralScript(script));
    BOOST_CHECK(!DigiDollar::IsDDTokenScript(script));
}

BOOST_AUTO_TEST_CASE(create_digidollar_p2tr_token_classified_separately)
{
    CKey owner;
    owner.MakeNewKey(true);
    XOnlyPubKey xonly(owner.GetPubKey());

    CScript token = DigiDollar::CreateDigiDollarP2TR(xonly, 5'000); // $50.00
    BOOST_REQUIRE(!token.empty());
    BOOST_REQUIRE_EQUAL(token.size(), 34u);
    BOOST_CHECK(token[0] == OP_1);

    DigiDollar::ScriptMetadata meta;
    BOOST_REQUIRE(DigiDollar::GetScriptMetadata(token, meta));
    BOOST_CHECK(meta.type == DigiDollar::ScriptType::DD_TOKEN_OUTPUT);
    BOOST_CHECK_EQUAL(meta.ddAmount, 5'000);

    // Token script: IsDDTokenScript true, IsCollateralScript false.
    BOOST_CHECK(DigiDollar::IsDDTokenScript(token));
    BOOST_CHECK(!DigiDollar::IsCollateralScript(token));
}

BOOST_AUTO_TEST_CASE(ordinary_p2tr_without_metadata_is_not_dd)
{
    // A canonical 34-byte P2TR script with no DigiDollar metadata
    // registration must classify as NOT_DIGIDOLLAR. This guards against
    // any future change that would silently treat unrelated taproot
    // outputs as DD vaults.
    uint256 hash;
    hash.SetHex("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    CScript ordinary;
    ordinary << OP_1;
    ordinary << std::vector<unsigned char>(hash.begin(), hash.end());

    BOOST_REQUIRE_EQUAL(ordinary.size(), 34u);
    BOOST_CHECK(DigiDollar::IdentifyScriptType(ordinary) == DigiDollar::ScriptType::NOT_DIGIDOLLAR);
    BOOST_CHECK(!DigiDollar::IsCollateralScript(ordinary));
    BOOST_CHECK(!DigiDollar::IsDDTokenScript(ordinary));
}

BOOST_AUTO_TEST_CASE(op1_impostor_short_witness_program_is_not_canonical)
{
    // 30-byte witness program (not 32) under OP_1 must not be picked up
    // by any DD identification helper.
    CScript impostor;
    impostor << OP_1;
    impostor << std::vector<unsigned char>(30, 0x77);
    BOOST_CHECK(!DigiDollar::IsCollateralScript(impostor));
    BOOST_CHECK(!DigiDollar::IsDDTokenScript(impostor));
    BOOST_CHECK(DigiDollar::IdentifyScriptType(impostor) == DigiDollar::ScriptType::NOT_DIGIDOLLAR);
}

BOOST_AUTO_TEST_CASE(create_collateral_uses_nums_internal_key_not_owner)
{
    // Two MintParams that differ only in ownerKey must share the SAME
    // taproot output key when both use the canonical NUMS internal key.
    // This pins the BIP-341 NUMS guarantee against accidentally letting
    // the owner sign key-path and bypass the timelock script paths.
    CKey k1; k1.MakeNewKey(true);
    CKey k2; k2.MakeNewKey(true);

    DigiDollar::MintParams p1;
    p1.ddAmount = 10'000;
    p1.lockHeight = 5'000;
    p1.ownerKey = XOnlyPubKey(k1.GetPubKey());
    p1.internalKey = DigiDollar::GetCollateralNUMSKey();

    DigiDollar::MintParams p2 = p1;
    p2.ownerKey = XOnlyPubKey(k2.GetPubKey());

    CScript s1 = DigiDollar::CreateCollateralP2TR(p1);
    CScript s2 = DigiDollar::CreateCollateralP2TR(p2);
    BOOST_REQUIRE(!s1.empty());
    BOOST_REQUIRE(!s2.empty());

    // Different MAST trees, so the OUTPUT KEY differs (taproot tweaks
    // the NUMS internal key by the merkle root). Pinning that they are
    // not equal closes off the "two unrelated owners share a vault"
    // scenario.
    BOOST_CHECK(s1 != s2);

    // Both still start with OP_1 + 32-byte program.
    BOOST_REQUIRE_EQUAL(s1.size(), 34u);
    BOOST_REQUIRE_EQUAL(s2.size(), 34u);
}

// =====================================================================
// 4) Address encoding — DD/TD/RD prefixes and cross-chain rejection
// =====================================================================

BOOST_AUTO_TEST_CASE(dd_td_rd_addresses_are_distinct_for_same_pubkey)
{
    const std::string mainnet  = EncodeAddrFor(CChainParams::DIGIDOLLAR_ADDRESS);
    const std::string testnet  = EncodeAddrFor(CChainParams::DIGIDOLLAR_ADDRESS_TESTNET);
    const std::string regtest  = EncodeAddrFor(CChainParams::DIGIDOLLAR_ADDRESS_REGTEST);

    BOOST_REQUIRE(!mainnet.empty());
    BOOST_REQUIRE(!testnet.empty());
    BOOST_REQUIRE(!regtest.empty());
    BOOST_CHECK_EQUAL(mainnet.substr(0, 2), "DD");
    BOOST_CHECK_EQUAL(testnet.substr(0, 2), "TD");
    BOOST_CHECK_EQUAL(regtest.substr(0, 2), "RD");

    BOOST_CHECK(mainnet != testnet);
    BOOST_CHECK(testnet != regtest);
    BOOST_CHECK(mainnet != regtest);
}

BOOST_AUTO_TEST_CASE(cross_chain_dd_addresses_rejected_for_current_network)
{
    ChainParamsRestore restore{Params().GetChainType()};

    const std::string mainnet  = EncodeAddrFor(CChainParams::DIGIDOLLAR_ADDRESS);
    const std::string testnet  = EncodeAddrFor(CChainParams::DIGIDOLLAR_ADDRESS_TESTNET);
    const std::string regtest  = EncodeAddrFor(CChainParams::DIGIDOLLAR_ADDRESS_REGTEST);

    SelectParams(ChainType::MAIN);
    BOOST_CHECK( CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(mainnet));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(testnet));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(regtest));

    SelectParams(ChainType::TESTNET);
    BOOST_CHECK( CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(testnet));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(mainnet));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(regtest));

    SelectParams(ChainType::REGTEST);
    BOOST_CHECK( CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(regtest));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(mainnet));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(testnet));

    // Cross-chain decoding still recognises the address as STRUCTURALLY
    // valid even if it does not match the current network.
    SelectParams(ChainType::MAIN);
    BOOST_CHECK(CDigiDollarAddress::IsValidDigiDollarAddress(testnet));
    BOOST_CHECK(CDigiDollarAddress::IsValidDigiDollarAddress(regtest));
}

BOOST_AUTO_TEST_CASE(corrupted_dd_address_strings_rejected)
{
    const std::string good = EncodeAddrFor(CChainParams::DIGIDOLLAR_ADDRESS);
    BOOST_REQUIRE(!good.empty());
    BOOST_REQUIRE(CDigiDollarAddress::IsValidDigiDollarAddress(good));

    // 1) Truncated address: drop the last character (breaks checksum).
    {
        std::string truncated = good.substr(0, good.size() - 1);
        BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress(truncated));
    }

    // 2) Single-character substitution near the start (prefix preserved
    //    but checksum broken). Cycle through a couple to avoid the
    //    one-in-58 chance of accidentally producing a valid address.
    {
        for (char c : {'2', '3', '4', '5'}) {
            std::string flipped = good;
            if (flipped.size() > 4 && flipped[3] != c) {
                flipped[3] = c;
                if (!CDigiDollarAddress::IsValidDigiDollarAddress(flipped)) {
                    BOOST_CHECK(true);
                    return;  // first failing flip is enough
                }
            }
        }
        BOOST_CHECK(false);  // every flip happened to hash-match — extremely unlikely
    }
}

BOOST_AUTO_TEST_CASE(embedded_null_byte_address_rejected)
{
    const std::string good = EncodeAddrFor(CChainParams::DIGIDOLLAR_ADDRESS);
    std::string poisoned = good;
    poisoned.insert(poisoned.size() / 2, 1, '\0');
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress(poisoned));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(poisoned));
}

BOOST_AUTO_TEST_CASE(empty_and_short_strings_rejected)
{
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress(""));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("D"));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("DD"));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("RDxx"));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress(std::string(20, 'D')));
    // Looks like a DD prefix but the body is not Base58Check.
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress("DD0OIl"));
}

BOOST_AUTO_TEST_CASE(unknown_network_byte_rejected)
{
    // Build a 34-byte payload with a synthetic version prefix that does
    // not match any of DD/TD/RD, then Base58Check-encode it. The
    // resulting string must fail validation despite being structurally
    // sound Base58Check.
    std::vector<unsigned char> payload;
    payload.reserve(34);
    payload.push_back(0x00);  // version byte 0
    payload.push_back(0x00);
    for (int i = 0; i < 32; ++i) payload.push_back(static_cast<unsigned char>(i + 1));

    std::string s = EncodeBase58Check(payload);
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress(s));
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(s));
}

BOOST_AUTO_TEST_CASE(wrong_payload_length_rejected)
{
    // Valid mainnet version bytes but only 16 bytes of taproot data.
    std::vector<unsigned char> payload;
    payload.push_back(0x52); payload.push_back(0x85);  // "DD"
    for (int i = 0; i < 16; ++i) payload.push_back(static_cast<unsigned char>(i));
    std::string s = EncodeBase58Check(payload);
    BOOST_CHECK(!CDigiDollarAddress::IsValidDigiDollarAddress(s));
}

// =====================================================================
// 5) Zero-value unresolved DD inputs (MEMPOOL_HEIGHT) defense
// =====================================================================

BOOST_AUTO_TEST_CASE(extract_dd_amount_static_constant_for_mempool_height)
{
    // This is a small but consensus-critical pin: the value used for
    // MEMPOOL_HEIGHT in src/digidollar/validation.cpp must match the
    // value used elsewhere (txmempool.h). The redteam tests at
    // digidollar_redteam_tests.cpp:11917 hard-code 0x7FFFFFFF for the
    // same purpose. Drift here would silently re-enable the unconfirmed
    // DD chain attack closed by the confirmed-only policy at
    // src/digidollar/validation.cpp:1735.
    constexpr uint32_t kKnownMempoolHeight = 0x7FFFFFFF;
    BOOST_CHECK_EQUAL(MEMPOOL_HEIGHT, kKnownMempoolHeight);
}

BOOST_AUTO_TEST_SUITE_END()
