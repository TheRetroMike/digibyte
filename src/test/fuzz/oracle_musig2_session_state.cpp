// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <oracle/musig2_messages.h>
#include <primitives/oracle.h>
#include <protocol.h>
#include <serialize.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>

#include <cassert>
#include <cstdint>
#include <map>
#include <vector>

/**
 * Simulated MuSig2 session state machine for fuzz testing.
 * Models the lifecycle: IDLE → NONCES_COLLECTING → SIGNING → COMPLETE/FAILED
 */
namespace {

enum class SessionState { IDLE, NONCES_COLLECTING, SIGNING, COMPLETE, FAILED };

struct SimulatedSession {
    SessionState state{SessionState::IDLE};
    int32_t epoch{0};
    std::map<uint8_t, std::vector<unsigned char>> nonces;     // oracle_id → pubnonce
    std::map<uint8_t, std::vector<unsigned char>> partial_sigs; // oracle_id → partial_sig
    size_t required_signers{8};

    bool AddNonce(const OracleMusigNonceMsg& msg)
    {
        if (state != SessionState::IDLE && state != SessionState::NONCES_COLLECTING)
            return false;
        if (!msg.IsValid()) return false;
        if (msg.epoch != epoch) return false;

        state = SessionState::NONCES_COLLECTING;
        nonces[msg.oracle_id] = msg.pubnonce;

        if (nonces.size() >= required_signers) {
            state = SessionState::SIGNING;
        }
        return true;
    }

    bool AddPartialSig(const OracleMusigPartialSigMsg& msg)
    {
        if (state != SessionState::SIGNING) return false;
        if (!msg.IsValid()) return false;
        if (msg.epoch != epoch) return false;
        if (nonces.find(msg.oracle_id) == nonces.end()) return false;

        partial_sigs[msg.oracle_id] = msg.partial_sig;

        if (partial_sigs.size() >= required_signers) {
            state = SessionState::COMPLETE;
        }
        return true;
    }

    void Reset(int32_t new_epoch)
    {
        state = SessionState::IDLE;
        epoch = new_epoch;
        nonces.clear();
        partial_sigs.clear();
    }
};

} // namespace

FUZZ_TARGET(musig2_session_state)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    SimulatedSession session;
    session.epoch = fdp.ConsumeIntegral<int32_t>();
    session.required_signers = fdp.ConsumeIntegralInRange<size_t>(1, ORACLE_ACTIVE_COUNT);

    LIMITED_WHILE(fdp.remaining_bytes() > 0, 500) {
        switch (fdp.ConsumeIntegralInRange<int>(0, 4)) {
        case 0: {
            // Add a nonce message
            OracleMusigNonceMsg msg;
            msg.epoch = fdp.ConsumeBool() ? session.epoch : fdp.ConsumeIntegral<int32_t>();
            msg.oracle_id = fdp.ConsumeIntegral<uint8_t>();
            size_t len = fdp.ConsumeIntegralInRange<size_t>(0, 128);
            msg.pubnonce = fdp.ConsumeBytes<unsigned char>(len);

            (void)session.AddNonce(msg);
            break;
        }
        case 1: {
            // Add a partial signature
            OracleMusigPartialSigMsg msg;
            msg.epoch = fdp.ConsumeBool() ? session.epoch : fdp.ConsumeIntegral<int32_t>();
            msg.oracle_id = fdp.ConsumeIntegral<uint8_t>();
            size_t len = fdp.ConsumeIntegralInRange<size_t>(0, 64);
            msg.partial_sig = fdp.ConsumeBytes<unsigned char>(len);

            (void)session.AddPartialSig(msg);
            break;
        }
        case 2: {
            // Reset the session to a new epoch
            session.Reset(fdp.ConsumeIntegral<int32_t>());
            break;
        }
        case 3: {
            // Deserialize a nonce message from raw fuzz bytes
            auto raw = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, 128));
            CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
            try {
                OracleMusigNonceMsg msg;
                ss >> msg;
                (void)session.AddNonce(msg);
            } catch (const std::exception&) {
            }
            break;
        }
        case 4: {
            // Deserialize a partial sig message from raw fuzz bytes
            auto raw = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, 128));
            CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
            try {
                OracleMusigPartialSigMsg msg;
                ss >> msg;
                (void)session.AddPartialSig(msg);
            } catch (const std::exception&) {
            }
            break;
        }
        }
    }

    // Verify state machine invariants
    if (session.state == SessionState::COMPLETE) {
        assert(session.partial_sigs.size() >= session.required_signers);
        assert(session.nonces.size() >= session.required_signers);
    }
    if (session.state == SessionState::SIGNING) {
        assert(session.nonces.size() >= session.required_signers);
    }
    if (session.state == SessionState::IDLE) {
        assert(session.nonces.empty());
        assert(session.partial_sigs.empty());
    }
}
