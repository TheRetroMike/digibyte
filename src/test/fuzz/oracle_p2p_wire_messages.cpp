// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 20 (Agent B) — fuzz coverage for the P2P wire-wrapper oracle
 * messages declared in `src/protocol.h`:
 *
 *   - OraclePriceMsg       (wraps COraclePriceMessage)
 *   - OracleBundleMsg      (wraps COracleBundle + block_hash)
 *   - GetOracleDataMsg     (epoch + oracle_id request)
 *   - OracleConsensusMsg   (epoch + consensus_price + consensus_timestamp)
 *   - OracleAttestationMsg (wraps COraclePriceMessage attestation)
 *   - OracleVersionHeartbeatMsg (signed operator version/status heartbeat)
 *
 * Existing oracle fuzz harnesses cover the *inner* COraclePriceMessage and
 * COracleBundle types, but not the P2P wire wrappers that net_processing
 * actually deserializes off the wire. Malformed wrappers are the realistic
 * attack surface: an attacker can replay or mutate the outer envelope
 * (e.g. block_hash on OracleBundleMsg) without touching the inner bundle.
 *
 * This target deserializes random bytes into each wrapper, exercises the
 * GetHash() invariants, and verifies that GetHash() does not depend on
 * unauthenticated outer fields where appropriate (the `OracleBundleMsg`
 * pin is already covered by `bundle_hash_ignores_unauthenticated_block_hash`
 * in `oracle_bundle_manager_tests.cpp` — the fuzz target re-exercises the
 * same invariant under random inputs to defend against deserialization
 * regressions).
 */

#include <chainparams.h>
#include <key.h>
#include <primitives/oracle.h>
#include <protocol.h>
#include <serialize.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void initialize_oracle_p2p_wire_messages()
{
    ECC_Start();
    SelectParams(ChainType::REGTEST);
}

template <typename Msg>
void fuzz_roundtrip(const std::vector<uint8_t>& raw)
{
    DataStream ds{raw};
    Msg msg;
    try {
        ds >> msg;
    } catch (const std::exception&) {
        return; // malformed input — expected
    }

    // Exercise hash invariants — these MUST be deterministic and not crash
    // on any deserialized message.
    (void)msg.GetHash();

    // Re-serialize and confirm the round-trip is stable.
    DataStream ds2{};
    try {
        ds2 << msg;
    } catch (const std::exception&) {
        return;
    }

    Msg decoded;
    try {
        ds2 >> decoded;
    } catch (const std::exception&) {
        return;
    }

    // Hash equality on round-trip is a structural invariant.
    assert(msg.GetHash() == decoded.GetHash());
}

} // namespace

FUZZ_TARGET(oracle_p2p_wire_messages, .init = initialize_oracle_p2p_wire_messages)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // Bucket the input across the six wrapper types so a single fuzz buffer
    // can probe any wrapper. Each wrapper consumes its own slice of the input.
    const int which = fdp.ConsumeIntegralInRange<int>(0, 5);
    const size_t take = fdp.ConsumeIntegralInRange<size_t>(0, fdp.remaining_bytes());
    const std::vector<uint8_t> slice = fdp.ConsumeBytes<uint8_t>(take);

    switch (which) {
    case 0:
        fuzz_roundtrip<OraclePriceMsg>(slice);
        break;
    case 1: {
        // OracleBundleMsg — additional invariant: GetHash() must NOT depend
        // on the unauthenticated block_hash field. This is the runtime
        // analogue of `bundle_hash_ignores_unauthenticated_block_hash`.
        DataStream ds{slice};
        OracleBundleMsg msg;
        try {
            ds >> msg;
        } catch (const std::exception&) {
            break;
        }

        const uint256 h0 = msg.GetHash();
        OracleBundleMsg mutated = msg;
        if (mutated.block_hash.size() > 0) {
            mutated.block_hash.data()[0] ^= 0xFF;
        }
        const uint256 h1 = mutated.GetHash();
        assert(h0 == h1);
        break;
    }
    case 2:
        fuzz_roundtrip<OracleConsensusMsg>(slice);
        break;
    case 3:
        fuzz_roundtrip<OracleAttestationMsg>(slice);
        break;
    case 4: {
        // GetOracleDataMsg — has no GetHash(), but exercise serialize/parse
        // for crash safety.
        DataStream ds{slice};
        GetOracleDataMsg req;
        try {
            ds >> req;
        } catch (const std::exception&) {
            break;
        }
        DataStream ds2{};
        try {
            ds2 << req;
        } catch (const std::exception&) {
            break;
        }
        GetOracleDataMsg decoded;
        try {
            ds2 >> decoded;
        } catch (const std::exception&) {
            break;
        }
        assert(req.epoch == decoded.epoch);
        assert(req.oracle_id == decoded.oracle_id);

        const bool expected_valid = decoded.epoch >= 0 &&
            (decoded.oracle_id == 0xFFFFFFFF || decoded.oracle_id < ORACLE_TOTAL_COUNT);
        assert(OracleP2P::ValidateGetOracleRequest(decoded) == expected_valid);
        break;
    }
    case 5:
        fuzz_roundtrip<OracleVersionHeartbeatMsg>(slice);
        break;
    }
}
