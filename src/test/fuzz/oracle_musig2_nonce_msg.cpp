// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <oracle/musig2_messages.h>
#include <chainparams.h>
#include <key.h>
#include <protocol.h>
#include <serialize.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void initialize_musig2_nonce_message()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}

} // namespace

FUZZ_TARGET(musig2_nonce_message, .init = initialize_musig2_nonce_message)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // 1. Deserialize random bytes into an OracleMusigNonceMsg
    {
        auto raw = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, 256));
        CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
        try {
            OracleMusigNonceMsg msg;
            ss >> msg;

            // If deserialization succeeded, exercise accessors
            (void)msg.IsValid();
            (void)msg.GetHash();

            // Re-serialize and verify roundtrip
            CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
            ss2 << msg;

            OracleMusigNonceMsg msg2;
            ss2 >> msg2;

            assert(msg.epoch == msg2.epoch);
            assert(msg.oracle_id == msg2.oracle_id);
            assert(msg.pubnonce == msg2.pubnonce);
            assert(msg.signature == msg2.signature);
        } catch (const std::exception&) {
            // Deserialization failure on fuzz input is expected
        }
    }

    // 2. Construct with fuzzed field values
    {
        OracleMusigNonceMsg msg;
        msg.epoch = fdp.ConsumeIntegral<int32_t>();
        msg.oracle_id = fdp.ConsumeIntegral<uint8_t>();

        size_t nonce_len = fdp.ConsumeIntegralInRange<size_t>(0, 128);
        msg.pubnonce = fdp.ConsumeBytes<unsigned char>(nonce_len);
        size_t signature_len = fdp.ConsumeIntegralInRange<size_t>(0, 128);
        msg.signature = fdp.ConsumeBytes<unsigned char>(signature_len);

        (void)msg.IsValid();
        (void)msg.GetHash();
        (void)msg.GetSignatureHash();

        // Serialize / deserialize roundtrip
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << msg;

        OracleMusigNonceMsg decoded;
        ss >> decoded;
        assert(decoded.epoch == msg.epoch);
        assert(decoded.oracle_id == msg.oracle_id);
        assert(decoded.pubnonce == msg.pubnonce);
        assert(decoded.signature == msg.signature);
    }
}
