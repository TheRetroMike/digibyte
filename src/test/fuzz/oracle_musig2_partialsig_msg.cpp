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

void initialize_musig2_partialsig_message()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}

} // namespace

FUZZ_TARGET(musig2_partialsig_message, .init = initialize_musig2_partialsig_message)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // 1. Deserialize random bytes into an OracleMusigPartialSigMsg
    {
        auto raw = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, 256));
        CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
        try {
            OracleMusigPartialSigMsg msg;
            ss >> msg;

            (void)msg.IsValid();
            (void)msg.GetHash();

            // Re-serialize roundtrip
            CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
            ss2 << msg;

            OracleMusigPartialSigMsg msg2;
            ss2 >> msg2;

            assert(msg.epoch == msg2.epoch);
            assert(msg.oracle_id == msg2.oracle_id);
            assert(msg.partial_sig == msg2.partial_sig);
            assert(msg.signature == msg2.signature);
        } catch (const std::exception&) {
            // Expected on malformed fuzz input
        }
    }

    // 2. Construct with fuzzed field values
    {
        OracleMusigPartialSigMsg msg;
        msg.epoch = fdp.ConsumeIntegral<int32_t>();
        msg.oracle_id = fdp.ConsumeIntegral<uint8_t>();

        size_t sig_len = fdp.ConsumeIntegralInRange<size_t>(0, 128);
        msg.partial_sig = fdp.ConsumeBytes<unsigned char>(sig_len);
        size_t signature_len = fdp.ConsumeIntegralInRange<size_t>(0, 128);
        msg.signature = fdp.ConsumeBytes<unsigned char>(signature_len);

        (void)msg.IsValid();
        (void)msg.GetHash();
        (void)msg.GetSignatureHash();

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << msg;

        OracleMusigPartialSigMsg decoded;
        ss >> decoded;
        assert(decoded.epoch == msg.epoch);
        assert(decoded.oracle_id == msg.oracle_id);
        assert(decoded.partial_sig == msg.partial_sig);
        assert(decoded.signature == msg.signature);
    }
}
