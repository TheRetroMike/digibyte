// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/oracle.h>
#include <oracle/bundle_manager.h>
#include <chainparams.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <script/script.h>
#include <util/chaintype.h>

#include <cstdint>
#include <vector>

namespace {
void initialize_oracle_script_parsing()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}
} // namespace

FUZZ_TARGET(oracle_script_extract, .init = initialize_oracle_script_parsing)
{
    // Fuzz the oracle bundle extraction from coinbase scripts
    // This tests CreateOracleScript/ExtractOracleBundle round-trip
    // and robustness against malformed scripts

    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    // Build a fake coinbase transaction with fuzzed OP_RETURN data
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();

    // Main output
    tx.vout.resize(1);
    tx.vout[0].nValue = 50 * COIN;

    // Add an OP_RETURN output with fuzzed oracle data
    CTxOut oracle_out;
    oracle_out.nValue = 0;

    CScript script{OP_RETURN};
    const bool use_oracle_marker = fuzzed_data.ConsumeBool();
    if (use_oracle_marker) {
        script << OP_ORACLE;
    } else if (fuzzed_data.ConsumeBool()) {
        script << static_cast<opcodetype>(fuzzed_data.ConsumeIntegralInRange<uint8_t>(0, 255));
    }

    auto payload = fuzzed_data.ConsumeBytes<unsigned char>(
        fuzzed_data.ConsumeIntegralInRange<size_t>(0, 1000));
    if (!payload.empty()) {
        if (use_oracle_marker && fuzzed_data.ConsumeBool()) {
            script.insert(script.end(), payload.begin(), payload.end());
        } else {
            script << payload;
        }
    }

    oracle_out.scriptPubKey = script;
    tx.vout.push_back(oracle_out);

    // Try to extract an oracle bundle from this fuzzed transaction
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle extracted;
    (void)manager.ExtractOracleBundle(CTransaction(tx), extracted);
}

FUZZ_TARGET(oracle_script_create_roundtrip, .init = initialize_oracle_script_parsing)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    // Create a valid-ish bundle and test CreateOracleScript
    COracleBundle bundle;
    bundle.epoch = fuzzed_data.ConsumeIntegral<int32_t>();
    bundle.median_price_micro_usd = fuzzed_data.ConsumeIntegral<uint64_t>();
    bundle.timestamp = fuzzed_data.ConsumeIntegral<int64_t>();
    bundle.version = fuzzed_data.ConsumeIntegralInRange<uint8_t>(1, 3);

    if (bundle.version == 3) {
        // v0x03 needs aggregate sig and bitmap
        bundle.aggregate_sig.resize(64);
        auto sig = fuzzed_data.ConsumeBytes<unsigned char>(64);
        if (sig.size() == 64) bundle.aggregate_sig = sig;

        size_t bm_size = fuzzed_data.ConsumeIntegralInRange<size_t>(1, 4);
        bundle.participation_bitmap = fuzzed_data.ConsumeBytes<unsigned char>(bm_size);
        if (bundle.participation_bitmap.empty()) bundle.participation_bitmap = {0xFF};
    } else {
        // v0x02 needs at least one message
        uint8_t msg_count = fuzzed_data.ConsumeIntegralInRange<uint8_t>(1, 11);
        for (uint8_t i = 0; i < msg_count && fuzzed_data.remaining_bytes() > 100; ++i) {
            COraclePriceMessage msg;
            msg.oracle_id = i;
            msg.price_micro_usd = fuzzed_data.ConsumeIntegral<uint64_t>();
            msg.timestamp = fuzzed_data.ConsumeIntegral<int64_t>();
            msg.block_height = fuzzed_data.ConsumeIntegral<int32_t>();
            msg.nonce = fuzzed_data.ConsumeIntegral<uint64_t>();
            msg.schnorr_sig.resize(64);
            auto sig = fuzzed_data.ConsumeBytes<unsigned char>(64);
            if (sig.size() == 64) msg.schnorr_sig = sig;

            CKey key;
            key.MakeNewKey(true);
            msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());

            bundle.AddMessage(msg);
        }
    }

    // Create script and try to extract back
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    CScript oracle_script = manager.CreateOracleScript(bundle);

    if (!oracle_script.empty()) {
        // Build a transaction with this script
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.SetNull();
        tx.vout.resize(1);
        tx.vout[0].nValue = 50 * COIN;

        CTxOut oracle_out;
        oracle_out.nValue = 0;
        oracle_out.scriptPubKey = oracle_script;
        tx.vout.push_back(oracle_out);

        COracleBundle extracted;
        bool ok = manager.ExtractOracleBundle(CTransaction(tx), extracted);
        if (ok) {
            // Exercise extraction but don't assert on round-trip values
            (void)extracted.median_price_micro_usd;
            (void)extracted.timestamp;
        }
    }
}
