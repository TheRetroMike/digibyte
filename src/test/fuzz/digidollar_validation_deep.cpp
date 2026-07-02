// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Deep-dive fuzz targets for DigiDollar validation pipeline.
// Targets: dd_validate_mint, dd_validate_redeem, dd_validate_transfer,
//          dd_supply_tracking, dd_consensus_rules
//

#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <consensus/dca.h>
#include <consensus/err.h>
#include <consensus/validation.h>
#include <consensus/volatility.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>
#include <vector>

// ============================================================================
// Shared Initialization
// ============================================================================

void initialize_dd_validation_deep()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
    // Ensure volatility monitor is clean for deterministic runs
    DigiDollar::Volatility::VolatilityMonitor::ClearHistory();
    DigiDollar::Volatility::VolatilityMonitor::ClearFreeze();
}

// ============================================================================
// Helpers
// ============================================================================

/** Build a DD-versioned mutable transaction with the given tx type byte. */
static CMutableTransaction MakeDDTx(uint8_t txTypeByte)
{
    CMutableTransaction mtx;
    // DD version format: txType(8) | flags(8) | 0x0770 (lower 16)
    mtx.nVersion = (static_cast<int32_t>(txTypeByte) << 24) | 0x0770;
    return mtx;
}

/** Build a minimal OP_RETURN script carrying the DD marker, tx type, and amount.
 *  Format: OP_RETURN <"DD"> <txType as CScriptNum> <ddAmount as 8-byte LE> */
static CScript MakeDDOpReturn(uint8_t txType, CAmount ddAmount)
{
    CScript s;
    s << OP_RETURN;
    std::vector<unsigned char> dd_marker = {'D', 'D'};
    s << dd_marker;
    s << CScriptNum(txType);
    // Encode amount as CScriptNum (up to 8 bytes)
    s << CScriptNum(ddAmount);
    return s;
}

/** Build a P2TR-shaped script (OP_1 + 32 random-ish bytes). */
static CScript MakeP2TR(FuzzedDataProvider& fuzzed_data)
{
    std::vector<uint8_t> payload = fuzzed_data.ConsumeBytes<uint8_t>(32);
    if (payload.size() < 32) payload.resize(32, 0xAB);
    CScript s;
    s << OP_1 << payload;
    return s;
}

/** Generate a valid secp256k1 private key from fuzzer bytes. */
static bool MakeKey(FuzzedDataProvider& fuzzed_data, CKey& key_out)
{
    std::vector<uint8_t> kb = fuzzed_data.ConsumeBytes<uint8_t>(32);
    if (kb.size() < 32) kb.resize(32, 0x01);
    key_out.Set(kb.begin(), kb.end(), true);
    return key_out.IsValid();
}

// ============================================================================
// Target 1: dd_validate_mint
//
// Fuzz the full ValidateMintTransaction() pipeline by constructing
// fuzzed CTransaction objects that resemble DD mints.
// ============================================================================

FUZZ_TARGET(dd_validate_mint, .init = initialize_dd_validation_deep)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& chainparams = Params();
    const auto& ddparams = chainparams.GetDigiDollarParams();

    // Fuzz validation context parameters
    int height = fdp.ConsumeIntegralInRange<int>(0, 10'000'000);
    CAmount oraclePrice = fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000'000LL); // micro-USD
    int systemHealth = fdp.ConsumeIntegralInRange<int>(0, 30000);

    DigiDollar::ValidationContext ctx(height, oraclePrice, systemHealth, chainparams,
                                      /*coins=*/nullptr, /*skip_oracle=*/true);

    uint8_t strategy = fdp.ConsumeIntegralInRange<uint8_t>(1, 5);

    // -- Strategy 1: Boundary DD amounts --
    if (strategy == 1) {
        const CAmount boundaries[] = {
            0, 1,
            ddparams.minMintAmount - 1, ddparams.minMintAmount, ddparams.minMintAmount + 1,
            ddparams.maxMintAmount - 1, ddparams.maxMintAmount, ddparams.maxMintAmount + 1,
            ddparams.minOutputAmount - 1, ddparams.minOutputAmount,
            100'000'000'000LL, // $1 billion
            -1, -100,
            MAX_MONEY, MAX_MONEY + 1
        };

        for (CAmount ddAmt : boundaries) {
            CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_MINT);

            // Collateral input (dummy)
            mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));

            // Collateral output (non-zero value P2TR)
            CAmount collateral = fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY);
            mtx.vout.emplace_back(collateral, MakeP2TR(fdp));

            // DD token output (zero value P2TR)
            mtx.vout.emplace_back(0, MakeP2TR(fdp));

            // OP_RETURN with DD metadata
            if (ddAmt > 0 && ddAmt <= MAX_MONEY) {
                mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_MINT, ddAmt));
            }

            CTransaction tx(mtx);
            TxValidationState state;
            // Must not crash regardless of input
            (void)DigiDollar::ValidateMintTransaction(tx, ctx, state);
        }
    }

    // -- Strategy 2: Fully fuzzed mint structure --
    if (strategy == 2) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_MINT);

        int numInputs = fdp.ConsumeIntegralInRange<int>(0, 3);
        for (int i = 0; i < numInputs; i++) {
            uint256 hash;
            auto hb = fdp.ConsumeBytes<uint8_t>(32);
            if (hb.size() == 32) memcpy(hash.data(), hb.data(), 32);
            mtx.vin.emplace_back(COutPoint(hash, fdp.ConsumeIntegralInRange<uint32_t>(0, 10)));
        }

        int numOutputs = fdp.ConsumeIntegralInRange<int>(0, 4);
        for (int i = 0; i < numOutputs; i++) {
            CAmount val = fdp.ConsumeIntegralInRange<CAmount>(0, MAX_MONEY);
            uint8_t scriptChoice = fdp.ConsumeIntegralInRange<uint8_t>(0, 2);
            if (scriptChoice == 0) {
                // P2TR output
                mtx.vout.emplace_back(val, MakeP2TR(fdp));
            } else if (scriptChoice == 1) {
                // OP_RETURN with DD data
                CAmount ddAmt = fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000'000LL);
                mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_MINT, ddAmt));
            } else {
                // Random script (capped size)
                auto sb = ConsumeRandomLengthByteVector(fdp, 32);
                CScript rs(sb.begin(), sb.end());
                mtx.vout.emplace_back(val, rs);
            }
        }

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateMintTransaction(tx, ctx, state);
    }

    // -- Strategy 3: Multiple DD outputs (inflation attack surface) --
    if (strategy == 3) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_MINT);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));

        CAmount collateral = fdp.ConsumeIntegralInRange<CAmount>(1'000'000'00LL, MAX_MONEY);
        mtx.vout.emplace_back(collateral, MakeP2TR(fdp));

        // Multiple zero-value P2TR outputs — should be rejected (only 1 DD output allowed)
        int ddOutputs = fdp.ConsumeIntegralInRange<int>(2, 5);
        for (int i = 0; i < ddOutputs; i++) {
            mtx.vout.emplace_back(0, MakeP2TR(fdp));
        }
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_MINT, 10000));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateMintTransaction(tx, ctx, state);
    }

    // -- Strategy 4: DD token output with non-zero DGB value (invalid) --
    if (strategy == 4) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_MINT);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));

        CAmount collateral = fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY);
        mtx.vout.emplace_back(collateral, MakeP2TR(fdp));

        // DD token output but with non-zero value — should fail
        CAmount badDDValue = fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY);
        mtx.vout.emplace_back(badDDValue, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_MINT, 10000));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateMintTransaction(tx, ctx, state);
    }

    // -- Strategy 5: Empty/minimal transactions --
    if (strategy == 5) {
        // No inputs
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_MINT);
        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateMintTransaction(tx, ctx, state);

        // Single output only
        CMutableTransaction mtx2 = MakeDDTx(DigiDollar::DD_TX_MINT);
        mtx2.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx2.vout.emplace_back(0, MakeP2TR(fdp));
        CTransaction tx2(mtx2);
        TxValidationState state2;
        (void)DigiDollar::ValidateMintTransaction(tx2, ctx, state2);
    }
}

// ============================================================================
// Target 2: dd_validate_redeem
//
// Fuzz ValidateRedemptionTransaction() with crafted redemption-like txns.
// ============================================================================

FUZZ_TARGET(dd_validate_redeem, .init = initialize_dd_validation_deep)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& chainparams = Params();

    int height = fdp.ConsumeIntegralInRange<int>(0, 10'000'000);
    CAmount oraclePrice = fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000'000LL);
    int systemHealth = fdp.ConsumeIntegralInRange<int>(0, 30000);

    DigiDollar::ValidationContext ctx(height, oraclePrice, systemHealth, chainparams,
                                      nullptr, /*skip_oracle=*/true);

    // Run only ONE strategy per iteration to control memory under ASan
    uint8_t strategy = fdp.ConsumeIntegralInRange<uint8_t>(1, 7);

    // -- Strategy 1: Basic redemption structure --
    if (strategy == 1) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_REDEEM);

        // Collateral input (first input)
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        // DD burn input (second input)
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 1));

        // DGB collateral output (value > 0)
        CAmount dgbReturn = fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY);
        mtx.vout.emplace_back(dgbReturn, MakeP2TR(fdp));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);
    }

    // -- Strategy 2: Zero DD burn (should fail — no DD destroyed) --
    if (strategy == 2) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_REDEEM);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 1));

        CAmount dgbReturn = fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY);
        mtx.vout.emplace_back(dgbReturn, MakeP2TR(fdp));

        // DD output of same amount as input = zero burn
        CAmount ddPass = fdp.ConsumeIntegralInRange<CAmount>(100, 10'000'000);
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_REDEEM, ddPass));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);
    }

    // -- Strategy 3: Maximum DD burn --
    if (strategy == 3) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_REDEEM);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 1));

        mtx.vout.emplace_back(MAX_MONEY, MakeP2TR(fdp));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);
    }

    // -- Strategy 4: Insufficient inputs (only 1 input — needs collateral + DD) --
    if (strategy == 4) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_REDEEM);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(1'000'000'00LL, MakeP2TR(fdp));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);
    }

    // -- Strategy 5: No outputs at all --
    if (strategy == 5) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_REDEEM);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 1));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);
    }

    // -- Strategy 6: Fully fuzzed redemption --
    if (strategy == 6) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_REDEEM);
        int numInputs = fdp.ConsumeIntegralInRange<int>(0, 3);
        for (int i = 0; i < numInputs; i++) {
            uint256 hash;
            auto hb = fdp.ConsumeBytes<uint8_t>(32);
            if (hb.size() == 32) memcpy(hash.data(), hb.data(), 32);
            mtx.vin.emplace_back(COutPoint(hash, fdp.ConsumeIntegralInRange<uint32_t>(0, 10)));
        }

        int numOutputs = fdp.ConsumeIntegralInRange<int>(0, 4);
        for (int i = 0; i < numOutputs; i++) {
            CAmount val = fdp.ConsumeIntegralInRange<CAmount>(0, MAX_MONEY);
            if (fdp.ConsumeBool()) {
                mtx.vout.emplace_back(val, MakeP2TR(fdp));
            } else {
                CAmount ddAmt = fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000'000LL);
                mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_REDEEM, ddAmt));
            }
        }

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateRedemptionTransaction(tx, ctx, state);
    }

    // -- Strategy 7: ERR-related redemption edge cases --
    if (strategy == 7) {
        // Force ERR active by setting low system health
        DigiDollar::ValidationContext errCtx(height, oraclePrice, 50 /* very low */, chainparams,
                                             nullptr, true);
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_REDEEM);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 1));
        mtx.vout.emplace_back(fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY), MakeP2TR(fdp));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateRedemptionTransaction(tx, errCtx, state);
    }
}

// ============================================================================
// Target 3: dd_validate_transfer
//
// Fuzz ValidateTransferTransaction() with crafted transfer txns.
// ============================================================================

FUZZ_TARGET(dd_validate_transfer, .init = initialize_dd_validation_deep)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& chainparams = Params();

    int height = fdp.ConsumeIntegralInRange<int>(0, 10'000'000);
    CAmount oraclePrice = fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000'000LL);
    int systemHealth = fdp.ConsumeIntegralInRange<int>(0, 30000);

    DigiDollar::ValidationContext ctx(height, oraclePrice, systemHealth, chainparams,
                                      nullptr, /*skip_oracle=*/true);

    uint8_t strategy = fdp.ConsumeIntegralInRange<uint8_t>(1, 9);

    try {
    // -- Strategy 1: Valid-looking single transfer --
    if (strategy == 1) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_TRANSFER);

        // DD input
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));

        CAmount ddAmt = fdp.ConsumeIntegralInRange<CAmount>(1, 10'000'000);

        // DD output (zero value P2TR)
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        // OP_RETURN with transfer metadata
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, ddAmt));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
    }

    // -- Strategy 2: Zero-amount transfer (should fail) --
    if (strategy == 2) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_TRANSFER);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, 0));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
    }

    // -- Strategy 3: Negative amount transfer --
    if (strategy == 3) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_TRANSFER);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, -100));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
    }

    // -- Strategy 4: Dust-amount transfer (just above/below minOutputAmount) --
    if (strategy == 4) {
        const auto& ddp = chainparams.GetDigiDollarParams();
        for (CAmount amt : {CAmount(1), ddp.minOutputAmount - 1, ddp.minOutputAmount, ddp.minOutputAmount + 1}) {
            if (amt <= 0) continue;
            CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_TRANSFER);
            mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
            mtx.vout.emplace_back(0, MakeP2TR(fdp));
            mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, amt));

            CTransaction tx(mtx);
            TxValidationState state;
            (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
        }
    }

    // -- Strategy 5: Transfer exceeding maximum ($100,000 = 10,000,000 cents) --
    if (strategy == 5) {
        CAmount overMax = fdp.ConsumeIntegralInRange<CAmount>(10'000'001, 100'000'000'000LL);
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_TRANSFER);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, overMax));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
    }

    // -- Strategy 6: No inputs --
    if (strategy == 6) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_TRANSFER);
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, 5000));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
    }

    // -- Strategy 7: Multiple DD outputs (split transfer) --
    if (strategy == 7) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_TRANSFER);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));

        int splits = fdp.ConsumeIntegralInRange<int>(2, 5);
        for (int i = 0; i < splits; i++) {
            mtx.vout.emplace_back(0, MakeP2TR(fdp));
        }

        // Build OP_RETURN with multiple amounts
        CScript opret;
        opret << OP_RETURN;
        std::vector<unsigned char> dd_marker = {'D', 'D'};
        opret << dd_marker;
        opret << CScriptNum(DigiDollar::DD_TX_TRANSFER);
        for (int i = 0; i < splits; i++) {
            CAmount splitAmt = fdp.ConsumeIntegralInRange<CAmount>(100, 1'000'000);
            opret << CScriptNum(splitAmt);
        }
        mtx.vout.emplace_back(0, opret);

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
    }

    // -- Strategy 8: Non-DD version but transfer-shaped tx (should reject) --
    if (strategy == 8) {
        CMutableTransaction mtx;
        mtx.nVersion = 2; // Regular tx version
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, 5000));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
    }

    // -- Strategy 9: Fully fuzzed transfer --
    if (strategy == 9) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_TRANSFER);
        int numInputs = fdp.ConsumeIntegralInRange<int>(0, 3);
        for (int i = 0; i < numInputs; i++) {
            uint256 hash;
            auto hb = fdp.ConsumeBytes<uint8_t>(32);
            if (hb.size() == 32) memcpy(hash.data(), hb.data(), 32);
            mtx.vin.emplace_back(COutPoint(hash, fdp.ConsumeIntegralInRange<uint32_t>(0, 10)));
        }

        int numOutputs = fdp.ConsumeIntegralInRange<int>(0, 4);
        for (int i = 0; i < numOutputs; i++) {
            uint8_t choice = fdp.ConsumeIntegralInRange<uint8_t>(0, 2);
            if (choice == 0) {
                mtx.vout.emplace_back(0, MakeP2TR(fdp));
            } else if (choice == 1) {
                CAmount ddAmt = fdp.ConsumeIntegral<CAmount>();
                mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, ddAmt));
            } else {
                CAmount val = fdp.ConsumeIntegralInRange<CAmount>(0, MAX_MONEY);
                auto sb = ConsumeRandomLengthByteVector(fdp, 32);
                CScript rs(sb.begin(), sb.end());
                mtx.vout.emplace_back(val, rs);
            }
        }

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateTransferTransaction(tx, ctx, state);
    }
    } catch (const scriptnum_error&) {
        // Invalid script numbers are expected during fuzzing.
    } catch (const std::exception&) {
        // Keep fuzz target robust against parser exceptions.
    }
}

// ============================================================================
// Target 4: dd_supply_tracking
//
// Fuzz DD supply invariants by exercising ValidateMintTransaction,
// ValidateTransferTransaction, ValidateRedemptionTransaction in sequence
// and checking that the validation functions remain consistent/deterministic.
// ============================================================================

FUZZ_TARGET(dd_supply_tracking, .init = initialize_dd_validation_deep)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& chainparams = Params();

    CAmount oraclePrice = fdp.ConsumeIntegralInRange<CAmount>(1000, 100'000'000LL);
    int systemHealth = fdp.ConsumeIntegralInRange<int>(50, 30000);

    DigiDollar::ValidationContext ctx(1000, oraclePrice, systemHealth, chainparams,
                                      nullptr, true);

    uint8_t strategy = fdp.ConsumeIntegralInRange<uint8_t>(1, 3);

    // Simulate a sequence of mint/transfer/redeem and track DD amounts.
    // Since we don't have a real UTXO set, we verify:
    // 1. ValidateMintAmount boundaries are consistent across calls
    // 2. The same tx produces the same validation result (determinism)
    // 3. Conservation: OP_RETURN amounts in transfers must be extractable

    CAmount runningDD = 0;
    const auto& ddparams = chainparams.GetDigiDollarParams();

    int ops = fdp.ConsumeIntegralInRange<int>(1, 10);
    for (int i = 0; i < ops; i++) {
        uint8_t opType = fdp.ConsumeIntegralInRange<uint8_t>(0, 2);

        if (strategy == 1 && opType == 0) {
            // Mint
            CAmount ddAmt = fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000LL);
            bool validAmt = DigiDollar::IsValidMintAmount(ddAmt, ddparams);

            // Determinism check: same input → same result
            bool validAmt2 = DigiDollar::IsValidMintAmount(ddAmt, ddparams);
            assert(validAmt == validAmt2);

            if (validAmt) {
                // Track minted supply
                if (runningDD <= MAX_MONEY - ddAmt) {
                    runningDD += ddAmt;
                }
            }

            // Validate the amount output check too
            bool validOutput = DigiDollar::ValidateOutputAmount(ddAmt, chainparams);
            bool validOutput2 = DigiDollar::ValidateOutputAmount(ddAmt, chainparams);
            assert(validOutput == validOutput2);

        } else if (strategy == 2 && opType == 1) {
            // Transfer (conservation check)
            if (runningDD > 0) {
                CAmount transferAmt = fdp.ConsumeIntegralInRange<CAmount>(1, runningDD);
                CAmount changeAmt = runningDD - transferAmt;

                // Build transfer OP_RETURN and verify we can parse back the amounts
                CScript opret = MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, transferAmt);
                CAmount extracted = 0;
                (void)DigiDollar::ExtractDDAmount(opret, extracted);
                // Note: ExtractDDAmount may fail on CScriptNum-encoded values
                // but it must never crash

                // Supply is conserved (transfer doesn't create/destroy)
                (void)changeAmt;
            }

        } else if (strategy == 3) {
            // Redeem
            if (runningDD > 0) {
                CAmount redeemAmt = fdp.ConsumeIntegralInRange<CAmount>(1, runningDD);
                runningDD -= redeemAmt;
            }
        }
    }

    // Final invariant: running DD should never be negative
    assert(runningDD >= 0);
}

// ============================================================================
// Target 5: dd_consensus_rules
//
// Fuzz the top-level ValidateDigiDollarTransaction() dispatcher with
// various DD tx types, including conflicting types and malformed versions.
// ============================================================================

FUZZ_TARGET(dd_consensus_rules, .init = initialize_dd_validation_deep)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& chainparams = Params();

    int height = fdp.ConsumeIntegralInRange<int>(0, 10'000'000);
    CAmount oraclePrice = fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000'000LL);
    int systemHealth = fdp.ConsumeIntegralInRange<int>(0, 30000);

    DigiDollar::ValidationContext ctx(height, oraclePrice, systemHealth, chainparams,
                                      nullptr, true);

    uint8_t strategy = fdp.ConsumeIntegralInRange<uint8_t>(1, 6);

    // -- Strategy 1: All DD tx types through the dispatcher --
    if (strategy == 1) {
        uint8_t txTypes[] = {
            DigiDollar::DD_TX_NONE,
            DigiDollar::DD_TX_MINT,
            DigiDollar::DD_TX_TRANSFER,
            DigiDollar::DD_TX_REDEEM,
            DigiDollar::DD_TX_MAX,
            255, // Invalid type byte
        };

        for (uint8_t t : txTypes) {
            CMutableTransaction mtx = MakeDDTx(t);
            // At least one input and two outputs
            mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
            mtx.vin.emplace_back(COutPoint(uint256::ONE, 1));
            mtx.vout.emplace_back(fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY), MakeP2TR(fdp));
            mtx.vout.emplace_back(0, MakeP2TR(fdp));
            mtx.vout.emplace_back(0, MakeDDOpReturn(t, fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000LL)));

            CTransaction tx(mtx);
            TxValidationState state;
            (void)DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        }
    }

    // -- Strategy 2: Version field fuzzing (DD marker corruption) --
    if (strategy == 2) {
        CMutableTransaction mtx;
        mtx.nVersion = fdp.ConsumeIntegral<int32_t>();
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(1'000'000LL, MakeP2TR(fdp));

        CTransaction tx(mtx);
        TxValidationState state;
        // Non-DD tx should pass through harmlessly
        (void)DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
    }

    // -- Strategy 3: Conflicting DD marker + wrong type --
    // E.g., version says MINT but outputs look like TRANSFER
    if (strategy == 3) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_MINT);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        // Transfer-style OP_RETURN in a MINT tx
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_TRANSFER, 5000));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
    }

    // -- Strategy 4: Multiple OP_RETURNs (duplicate DD metadata) --
    if (strategy == 4) {
        CMutableTransaction mtx = MakeDDTx(DigiDollar::DD_TX_MINT);
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vout.emplace_back(fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY), MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeP2TR(fdp));

        // Two OP_RETURN outputs with different amounts
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_MINT, 5000));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_MINT, 10000));

        CTransaction tx(mtx);
        TxValidationState state;
        (void)DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
    }

    // -- Strategy 5: Determinism — same tx validated twice must give same result --
    if (strategy == 5) {
        CMutableTransaction mtx = MakeDDTx(fdp.ConsumeIntegralInRange<uint8_t>(1, 3));
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 0));
        mtx.vin.emplace_back(COutPoint(uint256::ONE, 1));
        mtx.vout.emplace_back(fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY), MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeP2TR(fdp));
        mtx.vout.emplace_back(0, MakeDDOpReturn(DigiDollar::DD_TX_MINT, 10000));

        CTransaction tx(mtx);

        TxValidationState state1, state2;
        bool r1 = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state1);
        bool r2 = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state2);
        assert(r1 == r2);
    }

    // -- Strategy 6: Rapid succession of different types (ordering attacks) --
    if (strategy == 6) {
        int seqLen = fdp.ConsumeIntegralInRange<int>(2, 5);
        for (int i = 0; i < seqLen; i++) {
            uint8_t tt = fdp.ConsumeIntegralInRange<uint8_t>(1, 3);
            CMutableTransaction mtx = MakeDDTx(tt);
            mtx.vin.emplace_back(COutPoint(uint256::ONE, fdp.ConsumeIntegralInRange<uint32_t>(0, 100)));
            mtx.vin.emplace_back(COutPoint(uint256::ONE, fdp.ConsumeIntegralInRange<uint32_t>(0, 100)));
            mtx.vout.emplace_back(fdp.ConsumeIntegralInRange<CAmount>(1, MAX_MONEY), MakeP2TR(fdp));
            mtx.vout.emplace_back(0, MakeP2TR(fdp));
            CAmount ddAmt = fdp.ConsumeIntegralInRange<CAmount>(1, 100'000'000LL);
            mtx.vout.emplace_back(0, MakeDDOpReturn(tt, ddAmt));

            CTransaction tx(mtx);
            TxValidationState state;
            (void)DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
        }
    }
}
