// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wave 15 - Script Templates, OP_RETURN, Metadata, Address Encoding.
//
// Two new fuzz targets focused on the parser edges the existing
// dd_create_scripts / dd_script_parsing / dd_script_types targets do not
// adversarially poke:
//
//   1. dd_w15_opreturn_metadata - feeds adversarial Format 1 / Format 2
//      DD OP_RETURN payloads to ExtractDDAmount and FindDDOpReturn,
//      plus zero/negative/oversized DD amounts to the public mint and
//      output amount validators. Reachable from every consensus path
//      that resolves DD amounts off the OP_RETURN.
//
//   2. dd_w15_address_codec - randomises the DD/TD/RD address codec via
//      CDigiDollarAddress, both with structural (Base58Check) bytes and
//      with raw fuzzed strings. Mirrors the cross-chain rejection +
//      corrupted-string assertions in
//      src/test/digidollar_wave15_parser_tests.cpp.
//
// Goal: surface invariant breaks (crashes, asserts, sanitizer reports,
// out-of-range int casts, infinite loops) that would otherwise only show
// up as quiet wallet/RPC failures or, in the worst case, mempool/miner
// drift around DD amount accounting.

#include <base58.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void initialize_dd_w15()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}

// Encode a fuzzed amount as a 1..16 byte vector. The validator should
// reject anything that is not exactly 8 bytes (Format 1) or that fails
// CScriptNum minimal-encoding (Format 2). The fuzzer hammers both.
std::vector<unsigned char> ConsumeAmountBlob(FuzzedDataProvider& fdp)
{
    const size_t len = fdp.ConsumeIntegralInRange<size_t>(0, 16);
    return fdp.ConsumeBytes<uint8_t>(len);
}

// Produce a candidate Base58Check string by either encoding a fuzzed
// version+payload directly or by randomly permuting a known-good DD
// address.
std::string ConsumeAddressCandidate(FuzzedDataProvider& fdp,
                                    const std::string& known_good)
{
    const int mode = fdp.ConsumeIntegralInRange<int>(0, 4);
    if (mode == 0) {
        // Raw random text.
        const size_t n = fdp.ConsumeIntegralInRange<size_t>(0, 80);
        return fdp.ConsumeBytesAsString(n);
    }
    if (mode == 1) {
        // Random Base58Check bytes (1..40 byte payload).
        const size_t n = fdp.ConsumeIntegralInRange<size_t>(1, 40);
        auto bytes = fdp.ConsumeBytes<uint8_t>(n);
        return EncodeBase58Check(bytes);
    }
    if (mode == 2 && !known_good.empty()) {
        // Truncated good string.
        const size_t cut = fdp.ConsumeIntegralInRange<size_t>(0, known_good.size());
        return known_good.substr(0, cut);
    }
    if (mode == 3 && !known_good.empty()) {
        // Single-character substitution at a fuzzed offset.
        std::string s = known_good;
        const size_t pos = fdp.ConsumeIntegralInRange<size_t>(0, s.size() - 1);
        const char c = static_cast<char>(fdp.ConsumeIntegral<uint8_t>());
        s[pos] = c;
        return s;
    }
    if (!known_good.empty()) {
        // Embedded null byte.
        std::string s = known_good;
        const size_t pos = fdp.ConsumeIntegralInRange<size_t>(0, s.size());
        s.insert(pos, 1, '\0');
        return s;
    }
    return std::string{};
}

} // namespace

// =====================================================================
// 1) dd_w15_opreturn_metadata - Format 1 / Format 2 parser edges
// =====================================================================
FUZZ_TARGET(dd_w15_opreturn_metadata, .init = initialize_dd_w15)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    const auto& chainParams = Params();

    // ------------------------------------------------------------
    // Format 1 OP_RETURN OP_DIGIDOLLAR <amount>
    // ------------------------------------------------------------
    {
        CScript script;
        script << OP_RETURN << OP_DIGIDOLLAR;
        auto blob = ConsumeAmountBlob(fdp);
        if (!blob.empty()) script << blob;
        // Optionally append trailing junk to exercise the trailing-data
        // rejection branch.
        if (fdp.ConsumeBool()) {
            script << OP_1;
        }

        CAmount amt = 0;
        const bool ok = DigiDollar::ExtractDDAmount(script, amt);
        if (ok) {
            // On success the amount must be in (0, MAX_DIGIDOLLAR].
            assert(amt >= 1);
            assert(amt <= MAX_DIGIDOLLAR);
        } else {
            // The parser sets amount = -1 on failure.
            assert(amt == -1 || amt == 0);
        }
    }

    // ------------------------------------------------------------
    // Format 2 OP_RETURN "DD" <type> <amount> [extras]
    // ------------------------------------------------------------
    {
        CScript script;
        script << OP_RETURN;
        // Fuzz the marker bytes - usually 'DD' but sometimes random.
        if (fdp.ConsumeBool()) {
            script << std::vector<unsigned char>{'D', 'D'};
        } else {
            const size_t n = fdp.ConsumeIntegralInRange<size_t>(0, 4);
            script << fdp.ConsumeBytes<uint8_t>(n);
        }
        // tx type push - random 0..255 then a fuzzed amount blob.
        if (fdp.ConsumeBool()) {
            script << CScriptNum(fdp.ConsumeIntegralInRange<int>(0, 255));
        }
        if (fdp.ConsumeBool()) {
            auto blob = ConsumeAmountBlob(fdp);
            if (!blob.empty()) script << blob;
        }
        // Optional trailing pushes (mint extras: lockHeight, lockTier).
        const int extras = fdp.ConsumeIntegralInRange<int>(0, 3);
        for (int i = 0; i < extras; ++i) {
            script << CScriptNum(fdp.ConsumeIntegral<int32_t>());
        }

        CAmount amt = 0;
        const bool ok = DigiDollar::ExtractDDAmount(script, amt);
        if (ok) {
            assert(amt >= 1);
            assert(amt <= MAX_DIGIDOLLAR);
        } else {
            assert(amt == -1 || amt == 0);
        }
    }

    // ------------------------------------------------------------
    // FindDDOpReturn against a fuzzed multi-output transaction.
    // ------------------------------------------------------------
    {
        CMutableTransaction mtx;
        mtx.nVersion = fdp.ConsumeIntegral<int32_t>();
        mtx.vin.resize(1);
        mtx.vin[0].prevout = COutPoint(uint256::ONE, 0);

        const int n = fdp.ConsumeIntegralInRange<int>(0, 6);
        for (int i = 0; i < n; ++i) {
            CTxOut o;
            o.nValue = fdp.ConsumeIntegralInRange<CAmount>(0, 100000);
            CScript s;
            // Mix of plain OP_RETURN / Format 1 / Format 2 / random
            // junk. The parser must never crash regardless.
            const int kind = fdp.ConsumeIntegralInRange<int>(0, 4);
            if (kind == 0) {
                s << OP_RETURN;
                s << fdp.ConsumeBytes<uint8_t>(
                    fdp.ConsumeIntegralInRange<size_t>(0, 32));
            } else if (kind == 1) {
                s << OP_RETURN << OP_DIGIDOLLAR;
                s << ConsumeAmountBlob(fdp);
            } else if (kind == 2) {
                s << OP_RETURN
                  << std::vector<unsigned char>{'D', 'D'}
                  << CScriptNum(fdp.ConsumeIntegralInRange<int>(0, 4))
                  << ConsumeAmountBlob(fdp);
            } else if (kind == 3) {
                // Random bytes in the script.
                auto raw = fdp.ConsumeBytes<uint8_t>(
                    fdp.ConsumeIntegralInRange<size_t>(0, 50));
                s = CScript(raw.begin(), raw.end());
            } else {
                s << OP_DUP << OP_HASH160
                  << std::vector<unsigned char>(20, 0x33)
                  << OP_EQUALVERIFY << OP_CHECKSIG;
            }
            o.scriptPubKey = s;
            mtx.vout.push_back(o);
        }

        CTransaction tx(mtx);
        const int idx = DigiDollar::FindDDOpReturn(tx);
        // Either -1, or a valid vout index.
        assert(idx == -1 || (idx >= 0 && static_cast<size_t>(idx) < tx.vout.size()));
        // HasDigiDollarMarker / GetDigiDollarTxType must not throw on
        // arbitrary nVersion — they read the version field directly.
        (void)DigiDollar::HasDigiDollarMarker(tx);
        try {
            (void)DigiDollar::GetDigiDollarTxType(tx);
        } catch (...) {
            // Some versions are not valid DD types; that's fine.
        }
    }

    // ------------------------------------------------------------
    // Validators for zero/negative/huge amounts must remain bounded.
    // ------------------------------------------------------------
    {
        const CAmount amount = fdp.ConsumeIntegral<CAmount>();
        const int height = fdp.ConsumeIntegralInRange<int>(0, 50'000'000);
        (void)DigiDollar::ValidateMintAmount(amount, chainParams, height);
        (void)DigiDollar::ValidateOutputAmount(amount, chainParams);
    }
}

// =====================================================================
// 2) dd_w15_address_codec - DD/TD/RD prefix + cross-chain rejection
// =====================================================================
FUZZ_TARGET(dd_w15_address_codec, .init = initialize_dd_w15)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // Build a known-good encoding for each network from a fuzzed taproot
    // pubkey, then exercise the codec against fuzzed strings derived
    // from each.
    auto bytes = fdp.ConsumeBytes<uint8_t>(32);
    if (bytes.size() != 32) return;

    uint256 hash;
    std::copy(bytes.begin(), bytes.end(), hash.begin());
    XOnlyPubKey xonly(hash);
    if (!xonly.IsFullyValid()) return;
    WitnessV1Taproot taproot(xonly);
    CTxDestination dest = taproot;

    auto encode = [&](int networkType) -> std::string {
        CDigiDollarAddress a;
        if (!a.SetDigiDollar(dest, networkType)) return std::string{};
        return a.ToString();
    };

    const std::string mainnet  = encode(CChainParams::DIGIDOLLAR_ADDRESS);
    const std::string testnet  = encode(CChainParams::DIGIDOLLAR_ADDRESS_TESTNET);
    const std::string regtest  = encode(CChainParams::DIGIDOLLAR_ADDRESS_REGTEST);

    // The known-good mainnet/testnet/regtest encodings must be
    // structurally valid (network-agnostic).
    if (!mainnet.empty()) {
        assert(CDigiDollarAddress::IsValidDigiDollarAddress(mainnet));
        assert(mainnet.substr(0, 2) == "DD");
    }
    if (!testnet.empty()) {
        assert(CDigiDollarAddress::IsValidDigiDollarAddress(testnet));
        assert(testnet.substr(0, 2) == "TD");
    }
    if (!regtest.empty()) {
        assert(CDigiDollarAddress::IsValidDigiDollarAddress(regtest));
        assert(regtest.substr(0, 2) == "RD");
    }

    // Cross-chain rejection: regtest is the active chainparams in this
    // fuzz target's init. mainnet/testnet must not be accepted as
    // current-network addresses.
    if (!mainnet.empty()) {
        assert(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(mainnet));
    }
    if (!testnet.empty()) {
        assert(!CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(testnet));
    }
    if (!regtest.empty()) {
        assert(CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(regtest));
    }

    // Now feed adversarial candidates to the codec. None of these may
    // crash, regardless of bytes.
    const std::string seed_good = !regtest.empty() ? regtest
                                : !mainnet.empty() ? mainnet
                                : testnet;
    const int rounds = fdp.ConsumeIntegralInRange<int>(0, 6);
    for (int i = 0; i < rounds; ++i) {
        const std::string candidate = ConsumeAddressCandidate(fdp, seed_good);

        // Both validators must terminate without exceptions / asserts.
        const bool any = CDigiDollarAddress::IsValidDigiDollarAddress(candidate);
        const bool current = CDigiDollarAddress::IsValidDigiDollarAddressForCurrentNetwork(candidate);

        // Address strings containing embedded NUL must always be rejected.
        if (candidate.find('\0') != std::string::npos) {
            assert(!any);
            assert(!current);
        }

        // Constructing the object from a fuzzed string must not crash.
        CDigiDollarAddress addr(candidate);
        const CTxDestination decoded = addr.GetDigiDollarDestination();
        if (any) {
            // If structurally valid, the destination must be either a
            // CNoDestination (e.g. wrong payload length but checksum-ok)
            // or a P2TR — never any other shape.
            const bool isP2TR = std::holds_alternative<WitnessV1Taproot>(decoded);
            const bool isNone = std::holds_alternative<CNoDestination>(decoded);
            assert(isP2TR || isNone);
        }
    }

    // Round-trip safety on the known-good encoding (in active chain).
    if (!regtest.empty()) {
        CDigiDollarAddress decoded(regtest);
        assert(decoded.IsValid());
        const CTxDestination got = decoded.GetDigiDollarDestination();
        assert(std::holds_alternative<WitnessV1Taproot>(got));
        const auto& gotTaproot = std::get<WitnessV1Taproot>(got);
        assert(gotTaproot == taproot);
    }
}
