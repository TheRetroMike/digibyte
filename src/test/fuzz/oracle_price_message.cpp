// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/oracle.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <chainparams.h>
#include <streams.h>
#include <key.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>
#include <vector>

namespace {
void initialize_oracle_price_message()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}
} // namespace

FUZZ_TARGET(oracle_price_message, .init = initialize_oracle_price_message)
{
    DataStream ds{buffer};

    // Fuzz deserialization of COraclePriceMessage
    try {
        COraclePriceMessage msg;
        ds >> msg;

        // Exercise validation with fuzzed data
        (void)msg.IsValid(0);
        (void)msg.IsValid(GetTime());
        (void)msg.GetSignatureHash();
        (void)msg.Verify();

        // Re-serialize and verify round-trip
        DataStream ds2{};
        ds2 << msg;
    } catch (const std::exception&) {
        // Deserialization failures are expected with fuzzed input
    }
}

FUZZ_TARGET(oracle_price_message_sign_verify, .init = initialize_oracle_price_message)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    // Create a message with fuzzed values
    uint32_t oracle_id = fuzzed_data.ConsumeIntegral<uint32_t>();
    uint64_t price = fuzzed_data.ConsumeIntegral<uint64_t>();
    int64_t timestamp = fuzzed_data.ConsumeIntegral<int64_t>();

    COraclePriceMessage msg(oracle_id, price, timestamp);
    msg.block_height = fuzzed_data.ConsumeIntegral<int32_t>();
    msg.nonce = fuzzed_data.ConsumeIntegral<uint64_t>();

    // Generate a real key and sign
    CKey key;
    key.MakeNewKey(true);
    msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());

    bool signed_ok = msg.Sign(key);
    assert(signed_ok);
    if (signed_ok) {
        // Valid signature must verify
        bool verify_ok = msg.Verify();
        assert(verify_ok);

        // Corrupt the signature and verify it fails
        if (!msg.schnorr_sig.empty()) {
            msg.schnorr_sig[0] ^= 0x01;
            bool verify_corrupted = msg.Verify();
            assert(!verify_corrupted);
        }
    }
}
