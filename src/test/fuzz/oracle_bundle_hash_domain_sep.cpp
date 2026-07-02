// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Wave 22 Agent B — Fuzz Harness Completeness.
//
// DD-FA-TEST-041 — Mutation fuzz for the v0x03 MuSig2 bundle hash
// domain separation introduced by Wave 9 commit `aa1a238241`
// (DD-FA-SEC-008). The signer (`OracleSigningOrchestrator::
// ComputeOracleMessageHash`) and the validator (`ComputeOracleBundleHash`)
// must produce byte-identical hashes when fed the same inputs and MUST
// diverge whenever any of the four domain-separation inputs changes:
//
//   tag           — labeled "DigiDollar/OracleBundle"
//   chain_hash    — params.hashGenesisBlock
//   epoch         — int32_t
//   price         — uint64_t (median_price_micro_usd)
//   timestamp     — int64_t
//
// The harness fuzzes:
//   1. signer/validator parity for arbitrary (epoch, price, timestamp)
//      under the active chain's genesis hash.
//   2. chain_hash mutation: flip any single bit (or arbitrary mutation)
//      of the genesis hash and assert the bundle hash diverges.
//   3. payload field mutation: flipping epoch, price, or timestamp must
//      diverge the hash regardless of chain hash.
//   4. cross-chain replay: a hash bound to chain A must not equal the
//      same payload bound to chain B (even when both chains share the
//      oracle roster).
//
// The hash is fed through `secp256k1_schnorrsig_verify` in production,
// so any silent collision would be a release blocker — pre-fix the
// validator dropped this domain separation entirely (cross-chain
// replay was reachable). This harness pins the post-fix invariants.

#include <chainparams.h>
#include <consensus/params.h>
#include <hash.h>
#include <oracle/bundle_manager.h>
#include <oracle/signing_orchestrator.h>
#include <primitives/oracle.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void initialize_oracle_bundle_hash_domain_sep()
{
    // Bundle hash construction depends on Params() for the convenience
    // overload only; the explicit overload accepts an arbitrary chain
    // hash so the fuzz target can mutate freely.
    SelectParams(ChainType::REGTEST);
}

uint256 ConsumeUint256(FuzzedDataProvider& fdp)
{
    uint256 hash;
    auto bytes = fdp.ConsumeBytes<uint8_t>(32);
    if (bytes.size() == 32) {
        std::memcpy(hash.begin(), bytes.data(), 32);
    }
    return hash;
}

COracleBundle MakeBundle(int32_t epoch, uint64_t price, int64_t timestamp)
{
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = epoch;
    bundle.median_price_micro_usd = price;
    bundle.timestamp = timestamp;
    return bundle;
}

uint256 SignerHash(int32_t epoch, uint64_t price, int64_t timestamp)
{
    unsigned char buf[32]{};
    OracleSigningOrchestrator::ComputeOracleMessageHash(epoch, price, timestamp, buf);
    uint256 out;
    std::memcpy(out.begin(), buf, 32);
    return out;
}

} // namespace

FUZZ_TARGET(oracle_bundle_hash_domain_sep, .init = initialize_oracle_bundle_hash_domain_sep)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    const int32_t epoch = fdp.ConsumeIntegral<int32_t>();
    const uint64_t price = fdp.ConsumeIntegral<uint64_t>();
    const int64_t timestamp = fdp.ConsumeIntegral<int64_t>();

    const COracleBundle bundle = MakeBundle(epoch, price, timestamp);

    // -- Invariant 1: signer/validator parity under the active chain hash.
    //
    // The signer always binds Params().GetConsensus().hashGenesisBlock and
    // the convenience validator overload does the same; they must agree.
    {
        const uint256 signer = SignerHash(epoch, price, timestamp);
        const uint256 validator = ComputeOracleBundleHash(bundle);
        assert(signer == validator);
    }

    // -- Invariant 2: same payload, different chain hash → divergent hash.
    {
        const uint256 chain_a = ConsumeUint256(fdp);
        uint256 chain_b = ConsumeUint256(fdp);
        // Force chain_b distinct from chain_a (otherwise the test is vacuous).
        if (chain_a == chain_b) {
            chain_b.begin()[0] ^= 0x01;
        }

        const uint256 hash_a = ComputeOracleBundleHash(bundle, chain_a);
        const uint256 hash_b = ComputeOracleBundleHash(bundle, chain_b);
        assert(hash_a != hash_b);
    }

    // -- Invariant 3: bit-flip mutations of the chain hash must diverge.
    //
    // The hash domain claim is "any change to chain_hash changes the
    // resulting bundle hash". Pick any single byte and apply an
    // arbitrary nonzero XOR.
    {
        uint256 base_chain = ConsumeUint256(fdp);
        const uint256 base_hash = ComputeOracleBundleHash(bundle, base_chain);

        const size_t byte_index = fdp.ConsumeIntegralInRange<size_t>(0, 31);
        uint8_t mask = fdp.ConsumeIntegral<uint8_t>();
        if (mask == 0) mask = 0x80;

        uint256 mutated = base_chain;
        mutated.begin()[byte_index] ^= mask;
        const uint256 mutated_hash = ComputeOracleBundleHash(bundle, mutated);
        assert(base_hash != mutated_hash);
    }

    // -- Invariant 4: payload field mutations must diverge.
    //
    // Flip epoch, price, OR timestamp — the validator hash must diverge
    // even when the chain hash is held constant.
    {
        const uint256 chain = ConsumeUint256(fdp);
        const uint256 base_hash = ComputeOracleBundleHash(bundle, chain);

        const uint8_t which = fdp.ConsumeIntegralInRange<uint8_t>(0, 2);
        COracleBundle mutated = bundle;
        switch (which) {
        case 0:
            mutated.epoch ^= 0x01;
            break;
        case 1: {
            // XOR with a non-zero value to guarantee a change.
            uint64_t delta = fdp.ConsumeIntegral<uint64_t>();
            if (delta == 0) delta = 1;
            mutated.median_price_micro_usd ^= delta;
            break;
        }
        case 2: {
            int64_t delta = fdp.ConsumeIntegral<int64_t>();
            if (delta == 0) delta = 1;
            mutated.timestamp ^= delta;
            break;
        }
        }
        const uint256 mutated_hash = ComputeOracleBundleHash(mutated, chain);
        assert(base_hash != mutated_hash);
    }

    // -- Invariant 5: payload-field collision search.
    //
    // Construct a second (epoch, price, timestamp) triple that differs
    // by exactly one field from the original and assert the validator's
    // hash diverges — protects against any future regression that drops
    // a field from the hash construction.
    {
        const int32_t epoch2 = fdp.ConsumeIntegral<int32_t>();
        const uint64_t price2 = fdp.ConsumeIntegral<uint64_t>();
        const int64_t timestamp2 = fdp.ConsumeIntegral<int64_t>();

        if (epoch2 != epoch || price2 != price || timestamp2 != timestamp) {
            const COracleBundle other = MakeBundle(epoch2, price2, timestamp2);
            const uint256 hash1 = ComputeOracleBundleHash(bundle);
            const uint256 hash2 = ComputeOracleBundleHash(other);
            assert(hash1 != hash2);
        }
    }
}
