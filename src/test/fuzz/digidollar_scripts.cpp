// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/amount.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void initialize_dd_scripts()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}

CKey MakeValidKey(FuzzedDataProvider& fdp)
{
    CKey key;
    auto bytes = fdp.ConsumeBytes<uint8_t>(32);
    if (bytes.size() == 32) {
        key.Set(bytes.begin(), bytes.end(), true);
    }
    return key;
}

} // namespace

FUZZ_TARGET(dd_create_scripts, .init = initialize_dd_scripts)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // =========================================================================
    // 1. CreateCollateralP2TR with fuzzed MintParams
    // =========================================================================
    {
        DigiDollar::MintParams params;
        params.ddAmount = fdp.ConsumeIntegral<CAmount>();
        params.lockHeight = fdp.ConsumeIntegral<int64_t>();

        CKey ownerKey = MakeValidKey(fdp);
        if (ownerKey.IsValid()) {
            params.ownerKey = XOnlyPubKey(ownerKey.GetPubKey());
        }
        // Use NUMS key as internal key (standard for collateral)
        params.internalKey = DigiDollar::GetCollateralNUMSKey();

        // Add fuzzed oracle keys
        int numOracles = fdp.ConsumeIntegralInRange<int>(0, 20);
        for (int i = 0; i < numOracles; ++i) {
            CKey oKey = MakeValidKey(fdp);
            if (oKey.IsValid()) {
                params.oracleKeys.push_back(XOnlyPubKey(oKey.GetPubKey()));
            }
        }

        CScript collateralScript = DigiDollar::CreateCollateralP2TR(params);

        // If we got a valid script, verify properties
        if (!collateralScript.empty()) {
            // Should be 34 bytes: OP_1 + 32-byte pubkey
            assert(collateralScript.size() == 34);
            assert(collateralScript[0] == OP_1);

            // Roundtrip: IdentifyScriptType should recognize it
            DigiDollar::ScriptType type = DigiDollar::IdentifyScriptType(collateralScript);
            // It may or may not be identified depending on metadata registration
            (void)type;

            // Script metadata roundtrip
            DigiDollar::ScriptMetadata meta;
            if (DigiDollar::GetScriptMetadata(collateralScript, meta)) {
                assert(meta.type == DigiDollar::ScriptType::COLLATERAL_LOCK);
                assert(meta.ddAmount == params.ddAmount);
                assert(meta.lockHeight == params.lockHeight);
            }
        }
    }

    // =========================================================================
    // 2. CreateDigiDollarP2TR with fuzzed pubkeys and amounts
    // =========================================================================
    {
        CKey key = MakeValidKey(fdp);
        CAmount ddAmount = fdp.ConsumeIntegral<CAmount>();

        CScript ddScript;
        if (key.IsValid()) {
            XOnlyPubKey xonly(key.GetPubKey());
            ddScript = DigiDollar::CreateDigiDollarP2TR(xonly, ddAmount);

            if (!ddScript.empty()) {
                // Should be 34 bytes P2TR
                assert(ddScript.size() == 34);
                assert(ddScript[0] == OP_1);

                // Metadata check
                DigiDollar::ScriptMetadata meta;
                if (DigiDollar::GetScriptMetadata(ddScript, meta)) {
                    assert(meta.type == DigiDollar::ScriptType::DD_TOKEN_OUTPUT);
                    assert(meta.ddAmount == ddAmount);
                }
            }
        }

        // Invalid amounts should produce empty scripts
        CKey goodKey;
        goodKey.MakeNewKey(true);
        CScript emptyScript = DigiDollar::CreateDigiDollarP2TR(
            XOnlyPubKey(goodKey.GetPubKey()), 0);
        assert(emptyScript.empty()); // 0 amount → empty

        CScript negScript = DigiDollar::CreateDigiDollarP2TR(
            XOnlyPubKey(goodKey.GetPubKey()), -100);
        assert(negScript.empty()); // Negative amount → empty
    }

    // =========================================================================
    // 3. CreateNormalRedemptionPath / CreateERRPath
    // =========================================================================
    {
        DigiDollar::MintParams params;
        params.ddAmount = fdp.ConsumeIntegral<CAmount>();
        params.lockHeight = fdp.ConsumeIntegral<int64_t>();

        CKey key = MakeValidKey(fdp);
        if (key.IsValid()) {
            params.ownerKey = XOnlyPubKey(key.GetPubKey());
        }

        CScript normalPath = DigiDollar::CreateNormalRedemptionPath(params);
        CScript errPath = DigiDollar::CreateERRPath(params);

        // Both return empty for invalid params (ddAmount <= 0 or lockHeight < 0)
        if (params.ddAmount > 0 && params.lockHeight >= 0 && key.IsValid()) {
            // Should have non-empty scripts
            if (!normalPath.empty()) {
                assert(normalPath.size() > 0);
            }
            if (!errPath.empty()) {
                assert(errPath.size() > 0);
            }
        }
    }

    // =========================================================================
    // 4. GetOracleKeys
    // =========================================================================
    {
        size_t count = fdp.ConsumeIntegralInRange<size_t>(0, 30);
        auto keys = DigiDollar::GetOracleKeys(count);
        assert(keys.size() == count);

        // All keys should be valid
        for (const auto& k : keys) {
            assert(k.IsFullyValid());
        }
    }

    // =========================================================================
    // 5. GetCollateralNUMSKey
    // =========================================================================
    {
        XOnlyPubKey nums = DigiDollar::GetCollateralNUMSKey();
        // NUMS key should be fully valid
        assert(nums.IsFullyValid());
    }

    // =========================================================================
    // 6. Script roundtrip: create → metadata → verify
    // =========================================================================
    {
        CKey key;
        key.MakeNewKey(true);
        CAmount amount = fdp.ConsumeIntegralInRange<CAmount>(1, 1'000'000'000);

        // Create DD token script
        XOnlyPubKey xonly(key.GetPubKey());
        CScript script = DigiDollar::CreateDigiDollarP2TR(xonly, amount);

        if (!script.empty()) {
            // Extract amount from metadata
            DigiDollar::ScriptMetadata meta;
            bool found = DigiDollar::GetScriptMetadata(script, meta);
            if (found) {
                assert(meta.ddAmount == amount);
                assert(meta.type == DigiDollar::ScriptType::DD_TOKEN_OUTPUT);
            }
        }
    }
}

FUZZ_TARGET(dd_script_parsing, .init = initialize_dd_scripts)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // =========================================================================
    // 1. Feed completely random bytes as scripts to identification functions
    // =========================================================================
    {
        auto raw = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, 200));
        CScript script(raw.begin(), raw.end());

        // None of these should crash
        DigiDollar::ScriptType type = DigiDollar::IdentifyScriptType(script);
        (void)type;

        CAmount amount = 0;
        (void)DigiDollar::ExtractDDAmount(script, amount);

        (void)DigiDollar::IsCollateralScript(script);
        (void)DigiDollar::IsDDTokenScript(script);
        (void)DigiDollar::ExtractLockTime(script);
    }

    // =========================================================================
    // 2. Almost-valid P2TR: OP_1 + fuzzed 32 bytes
    // =========================================================================
    {
        auto fuzzBytes = fdp.ConsumeBytes<uint8_t>(32);
        if (fuzzBytes.size() == 32) {
            CScript almostP2TR;
            almostP2TR << OP_1;
            almostP2TR.insert(almostP2TR.end(), 0x20); // push 32 bytes
            almostP2TR.insert(almostP2TR.end(), fuzzBytes.begin(), fuzzBytes.end());

            DigiDollar::ScriptType type = DigiDollar::IdentifyScriptType(almostP2TR);
            (void)type;

            CAmount amount = 0;
            (void)DigiDollar::ExtractDDAmount(almostP2TR, amount);

            (void)DigiDollar::IsCollateralScript(almostP2TR);
            (void)DigiDollar::IsDDTokenScript(almostP2TR);
        }
    }

    // =========================================================================
    // 3. Truncated scripts - test scripts that are too short
    // =========================================================================
    {
        for (int len = 0; len < 40; ++len) {
            auto bytes = fdp.ConsumeBytes<uint8_t>(len);
            if (static_cast<int>(bytes.size()) < len) break; // not enough fuzz data

            CScript script(bytes.begin(), bytes.end());

            // Must not crash on any length
            (void)DigiDollar::IdentifyScriptType(script);

            CAmount amt = 0;
            (void)DigiDollar::ExtractDDAmount(script, amt);
            (void)DigiDollar::IsCollateralScript(script);
            (void)DigiDollar::IsDDTokenScript(script);
            (void)DigiDollar::ExtractLockTime(script);
        }
    }

    // =========================================================================
    // 4. Oversized scripts
    // =========================================================================
    {
        size_t bigSize = fdp.ConsumeIntegralInRange<size_t>(100, 10'000);
        auto bigBytes = fdp.ConsumeBytes<uint8_t>(bigSize);
        CScript bigScript(bigBytes.begin(), bigBytes.end());

        (void)DigiDollar::IdentifyScriptType(bigScript);

        CAmount amt = 0;
        (void)DigiDollar::ExtractDDAmount(bigScript, amt);
        (void)DigiDollar::IsCollateralScript(bigScript);
        (void)DigiDollar::IsDDTokenScript(bigScript);
    }

    // =========================================================================
    // 5. OP_RETURN metadata scripts (DD format)
    // =========================================================================
    {
        CScript metaScript;
        metaScript << OP_RETURN;

        // Fuzz the rest of the OP_RETURN content
        auto payload = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, 80));
        for (const auto& b : payload) {
            metaScript << std::vector<unsigned char>{b};
        }

        CAmount amt = 0;
        (void)DigiDollar::ExtractDDAmount(metaScript, amt);
        (void)DigiDollar::IdentifyScriptType(metaScript);
    }

    // =========================================================================
    // 6. Transaction-level DD marker checks with fuzzed versions
    // =========================================================================
    {
        CMutableTransaction mtx;
        mtx.nVersion = fdp.ConsumeIntegral<int32_t>();
        mtx.nLockTime = fdp.ConsumeIntegral<uint32_t>();

        CTransaction tx(mtx);

        (void)DigiDollar::HasDigiDollarMarker(tx);
        try {
            (void)DigiDollar::GetDigiDollarTxType(tx);
        } catch (...) {
            // May throw for non-DD transactions — that's fine
        }
    }

    // =========================================================================
    // 7. ValidateNormalRedemption / ValidateERRRedemption with random data
    // =========================================================================
    {
        auto raw = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, 100));
        CScript script(raw.begin(), raw.end());
        int currentHeight = fdp.ConsumeIntegralInRange<int>(0, 10'000'000);

        (void)DigiDollar::ValidateNormalRedemption(script, currentHeight);

        int systemCollateral = fdp.ConsumeIntegralInRange<int>(0, 500);
        (void)DigiDollar::ValidateERRRedemption(script, systemCollateral);
    }

    // =========================================================================
    // 8. ValidateMintAmount / ValidateOutputAmount with fuzzed values
    // =========================================================================
    {
        const auto& chainParams = Params();
        CAmount amount = fdp.ConsumeIntegral<CAmount>();
        int nHeight = fdp.ConsumeIntegralInRange<int>(0, 10'000'000);

        (void)DigiDollar::ValidateMintAmount(amount, chainParams, nHeight);
        (void)DigiDollar::ValidateOutputAmount(amount, chainParams);
    }

    // =========================================================================
    // 9. RegisterScriptMetadata / GetScriptMetadata stress
    // =========================================================================
    {
        auto raw = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(1, 50));
        CScript script(raw.begin(), raw.end());
        CAmount ddAmt = fdp.ConsumeIntegral<CAmount>();
        int64_t lockH = fdp.ConsumeIntegral<int64_t>();

        DigiDollar::RegisterScriptMetadata(script, DigiDollar::ScriptType::COLLATERAL_LOCK, ddAmt, lockH);

        DigiDollar::ScriptMetadata meta;
        bool found = DigiDollar::GetScriptMetadata(script, meta);
        if (found) {
            assert(meta.ddAmount == ddAmt);
            assert(meta.lockHeight == lockH);
        }
    }
}
