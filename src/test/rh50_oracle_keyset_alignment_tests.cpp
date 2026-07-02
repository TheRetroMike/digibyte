// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// RH50 — Oracle keyset alignment invariant tests
//
// Encodes the consensus invariant that Phase 3 MuSig2 aggregate verification is
// safe: the compressed keys the aggregator reads from `vOracleNodes` MUST be
// slot-aligned with the x-only keys declared in `consensus.vOraclePublicKeys`.
//
// A mismatch at slot `i` (for `i < nOraclePubkeyCount`) means the aggregator
// rebuilds an aggregate pubkey from a different key than the one the operator
// signs under — or worse, from a publicly-known test key whose private key is
// available to anyone. Either case is catastrophic: forgery of a 9-of-N
// aggregate signature becomes possible without any private operator material.
//
// These tests are deliberately minimal. They do not construct signatures,
// aggregate keys, or exploit payloads. They assert only structural equality
// between two declared sets in chainparams — a check any reviewer can verify
// by eye. The test FAILS on mainnet whenever the slot-alignment invariant is
// broken and PASSES on testnet, which serves as the reference implementation.
//
// Related finding: RED_HORNET_LEDGER.md F1 (CRITICAL).

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/params.h>
#include <key_io.h>
#include <kernel/chainparams.h>
#include <primitives/oracle.h>
#include <pubkey.h>
#include <util/strencodings.h>

#include <test/util/setup_common.h>

#include <algorithm>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(rh50_oracle_keyset_alignment_tests, BasicTestingSetup)

namespace {

// Returns the x-only (32-byte) hex form of a compressed CPubKey by stripping
// the first (parity) byte.
std::string XOnlyHexFromCompressed(const CPubKey& cpk)
{
    BOOST_REQUIRE_MESSAGE(cpk.IsValid(), "pubkey must be valid");
    BOOST_REQUIRE_EQUAL(cpk.size(), 33U); // compressed
    return HexStr(std::vector<unsigned char>(cpk.begin() + 1, cpk.end()));
}

// Canonical secp256k1 generator point in compressed form. Private key is 1.
// If any active oracle slot contains this, it is catastrophic.
constexpr const char* kSecp256k1GeneratorCompressed =
    "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";

std::string UniqueXOnlyHex(uint8_t slot)
{
    static constexpr char hexmap[] = "0123456789abcdef";
    std::string hex(64, '0');
    hex[62] = hexmap[(slot >> 4) & 0x0f];
    hex[63] = hexmap[slot & 0x0f];
    return hex;
}

} // namespace

// ---------------------------------------------------------------------------
// RH50.1 — Active Phase 3 slots must be slot-aligned between the compressed
// roster (`vOracleNodes`) and the x-only roster (`consensus.vOraclePublicKeys`).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mainnet_xonly_slot_alignment)
{
    SelectParams(ChainType::MAIN);
    const CChainParams& params = Params();
    const Consensus::Params& consensus = params.GetConsensus();

    const auto& nodes = params.GetOracleNodes();
    const auto& xonly = consensus.vOraclePublicKeys;

    const int active = consensus.nOraclePubkeyCount;
    BOOST_REQUIRE_GE(active, 1);
    BOOST_REQUIRE_GE(static_cast<int>(nodes.size()), active);
    BOOST_REQUIRE_GE(static_cast<int>(xonly.size()), active);

    for (int i = 0; i < active; ++i) {
        const std::string got = XOnlyHexFromCompressed(nodes[i].pubkey);
        std::string want = xonly[i];
        std::transform(want.begin(), want.end(), want.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        BOOST_CHECK_MESSAGE(
            got == want,
            "Oracle slot " << i << " mismatch: vOracleNodes x-only=" << got
            << " but vOraclePublicKeys=" << want
            << ". MuSig2 aggregation reads vOracleNodes; a mismatch means "
               "the on-chain aggregate pubkey will not match what operators "
               "declare — or (worse) will match a key with a publicly-known "
               "private key.");
    }
}

// ---------------------------------------------------------------------------
// RH50.2 — No active Phase 3 slot may contain the secp256k1 generator G
// (private key = 1, trivially known). Catch the specific failure pattern
// where a placeholder test vector reaches mainnet.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mainnet_vOracleNodes_no_known_test_vectors)
{
    SelectParams(ChainType::MAIN);
    const CChainParams& params = Params();
    const int active = params.GetConsensus().nOraclePubkeyCount;
    const auto& nodes = params.GetOracleNodes();

    for (int i = 0; i < active; ++i) {
        const std::string hex = HexStr(Span<const unsigned char>(
            nodes[i].pubkey.data(), nodes[i].pubkey.size()));
        BOOST_CHECK_MESSAGE(
            hex != kSecp256k1GeneratorCompressed,
            "Oracle slot " << i << " is the secp256k1 generator G "
            "(compressed " << hex << "). Private key is 1. This is a "
            "BIP-340 test vector, not an operator key. Ship this to "
            "mainnet and every DigiDollar minted after Phase 3 "
            "activation is mintable by anyone.");
    }
}

// ---------------------------------------------------------------------------
// RH50.3 — Testnet reference implementation: the invariant MUST hold.
// If this ever starts failing, both networks share the broken state and
// the regression has progressed beyond chainparams into the aggregation
// logic itself.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(testnet_xonly_slot_alignment_reference)
{
    SelectParams(ChainType::TESTNET);
    const CChainParams& params = Params();
    const Consensus::Params& consensus = params.GetConsensus();

    const auto& nodes = params.GetOracleNodes();
    const auto& xonly = consensus.vOraclePublicKeys;

    const int active = consensus.nOraclePubkeyCount;
    if (active == 0 || xonly.empty()) {
        BOOST_TEST_MESSAGE("testnet has no active Phase 3 roster configured; "
                           "skipping reference alignment check.");
        return;
    }

    BOOST_REQUIRE_GE(static_cast<int>(nodes.size()), active);
    BOOST_REQUIRE_GE(static_cast<int>(xonly.size()), active);

    for (int i = 0; i < active; ++i) {
        const std::string got = XOnlyHexFromCompressed(nodes[i].pubkey);
        std::string want = xonly[i];
        std::transform(want.begin(), want.end(), want.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        BOOST_CHECK_EQUAL(got, want);
    }
}

// ---------------------------------------------------------------------------
// RH50.4 — Regtest reference: same per-slot alignment invariant.
//
// Regtest uses a 4-of-7 MuSig2 quorum with deterministic test keys.
// CRegTestParams populates `consensus.vOraclePublicKeys` and `vOracleNodes`
// independently; if their orderings ever drift the regtest signing
// orchestrator will produce aggregate signatures whose subkey order does
// not match what `ValidateMuSig2Bundle` reads, breaking every regtest DD
// block. This test pins the cross-list ordering for the active 7 slots.
// Closes Wave 9 cross-network alignment coverage (Agent C).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(regtest_xonly_slot_alignment_reference)
{
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();
    const Consensus::Params& consensus = params.GetConsensus();

    const auto& nodes = params.GetOracleNodes();
    const auto& xonly = consensus.vOraclePublicKeys;

    const int active = consensus.nOraclePubkeyCount;
    BOOST_REQUIRE_GE(active, 1);
    BOOST_REQUIRE_GE(static_cast<int>(nodes.size()), active);
    BOOST_REQUIRE_GE(static_cast<int>(xonly.size()), active);

    for (int i = 0; i < active; ++i) {
        const std::string got = XOnlyHexFromCompressed(nodes[i].pubkey);
        std::string want = xonly[i];
        std::transform(want.begin(), want.end(), want.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        BOOST_CHECK_MESSAGE(
            got == want,
            "Regtest oracle slot " << i << " mismatch: vOracleNodes x-only="
            << got << " but vOraclePublicKeys=" << want
            << ". A regtest drift here masks mainnet/testnet drift in "
               "the same release because the wave-9 alignment audit relies "
               "on regtest as the single end-to-end signing reference.");
    }
}

// ---------------------------------------------------------------------------
// RH50.5 — Cross-network sanity: ValidateOracleNodeAlignment() agrees.
// This is the same predicate digibyted runs at startup
// (`common::InitConfig` -> chainparams). If this test ever fails for a
// network, that network's binary refuses to launch — which is the correct
// fail-closed behavior, but is also worth catching at unit-test time.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(all_networks_validate_oracle_node_alignment)
{
    for (ChainType ct : {ChainType::MAIN, ChainType::TESTNET, ChainType::REGTEST}) {
        SelectParams(ct);
        const CChainParams& params = Params();
        BOOST_CHECK_MESSAGE(
            params.ValidateOracleNodeAlignment(),
            "ValidateOracleNodeAlignment() must hold for chain type "
            << params.GetChainTypeString()
            << ". Failure means digibyted refuses to start on this network.");
    }
}

// ---------------------------------------------------------------------------
// RH50.6 — Mainnet/testnet expose a fully active 35-slot oracle roster.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mainnet_testnet_have_35_active_slots)
{
    for (ChainType ct : {ChainType::MAIN, ChainType::TESTNET}) {
        SelectParams(ct);
        const CChainParams& params = Params();
        const Consensus::Params& consensus = params.GetConsensus();
        const auto& nodes = params.GetOracleNodes();
        const int expected_active = 35;

        BOOST_CHECK_EQUAL(consensus.nOracleTotalOracles, 35);
        BOOST_CHECK_EQUAL(consensus.nOracleConsensusRequired, 7);
        BOOST_CHECK_EQUAL(consensus.nOraclePubkeyCount, expected_active);
        BOOST_CHECK_EQUAL(consensus.vOraclePublicKeys.size(), static_cast<size_t>(expected_active));
        BOOST_CHECK_EQUAL(nodes.size(), 35U);

        for (size_t slot = 0; slot < nodes.size(); ++slot) {
            BOOST_CHECK_EQUAL(nodes[slot].id, slot);
            BOOST_CHECK_MESSAGE(nodes[slot].is_active,
                "slot " << slot << " must be active on " << params.GetChainTypeString());
        }
    }
}

// ---------------------------------------------------------------------------
// RH50.7 — The config validator must permit an alternate 9-of-35 keyset.
// Quorum changes should require chainparams keys plus a coordinated release,
// not a validator rewrite.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validate_oracle_configuration_accepts_future_9_of_35)
{
    Consensus::Params params = Params().GetConsensus();
    params.nOracleTotalOracles = 35;
    params.nOraclePubkeyCount = 35;
    params.nOracleConsensusRequired = 9;
    params.vOraclePublicKeys.clear();
    for (uint8_t slot = 0; slot < 35; ++slot) {
        params.vOraclePublicKeys.push_back(UniqueXOnlyHex(slot));
    }

    BOOST_CHECK_MESSAGE(
        Consensus::ValidateOracleConfiguration(params),
        "9-of-35 must be a valid MuSig2 oracle configuration for coordinated quorum changes");
}

BOOST_AUTO_TEST_SUITE_END()
