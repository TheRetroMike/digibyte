// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Wave 9 Agent A — Oracle Roster Expansion, Quorum, Domain Separation.
//
// DD-FA-SEC-008 — A v0x03 MuSig2 oracle bundle is signed over
// H(epoch || price || timestamp). The aggregate hash carries no chain or
// genesis identifier, and mainnet+testnet share an identical oracle
// roster (`vOraclePublicKeys` slots 0..16, see
// `src/kernel/chainparams.cpp:329-346` vs `:588-604`). A bundle whose
// payload epoch matches a target chain's `GetCurrentEpoch(block_height)`
// therefore verifies against any chain that uses the same oracle keys —
// a cross-chain replay vector that violates Wave-9 invariant
// "domain tags include genesis/chain-id and deployment so cross-chain or
// cross-deployment replay is impossible".
//
// This file pins the desired domain-separated behavior: the same
// epoch+price+timestamp values, signed by the same oracles under one
// chain's `hashGenesisBlock`, must NOT verify when re-presented to a
// validator running with a different `hashGenesisBlock`.
//
// Pre-fix this test fails (the cross-chain bundle verifies). Post-fix it
// passes (the bundle is rejected by the alternate-chain validator).

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <oracle/musig2_aggregator.h>
#include <primitives/oracle.h>
#include <random.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <secp256k1.h>
#include <secp256k1_musig.h>
#include <secp256k1_schnorrsig.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Reuse the easypow / local-mini-testnet roster the existing
// digidollar_oracle_roster_tests harness already exercises. These keys
// are activated when CTestNetParams sees -easypow=1 and rewrite both
// `vOraclePublicKeys` and `vOracleNodes` to a deterministic set whose
// private keys are derivable here.
constexpr int32_t DOMAIN_TEST_HEIGHT = 650;
constexpr uint64_t DOMAIN_TEST_PRICE = 51000;
constexpr int64_t DOMAIN_TEST_TIMESTAMP = 1735689600;
constexpr uint16_t DOMAIN_TEST_TOTAL_SLOTS = 35;
constexpr int DOMAIN_TEST_QUORUM = 9;

struct LocalMiniTestnetSetup : public BasicTestingSetup {
    LocalMiniTestnetSetup()
        : BasicTestingSetup{ChainType::TESTNET, {"-easypow=1"}}
    {
    }
};

std::array<unsigned char, 32> TestnetOracleSecret(uint8_t oracle_id)
{
    const std::string seed = "digibyte_testnet_oracle_" + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size()).Finalize(hash.begin());

    std::array<unsigned char, 32> secret{};
    std::memcpy(secret.data(), hash.begin(), secret.size());

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    secp256k1_pubkey pubkey;
    if (ctx && secp256k1_ec_pubkey_create(ctx, &pubkey, secret.data())) {
        unsigned char serialized[33];
        size_t serialized_len = sizeof(serialized);
        if (secp256k1_ec_pubkey_serialize(ctx, serialized, &serialized_len, &pubkey,
                                          SECP256K1_EC_COMPRESSED) &&
            serialized_len == sizeof(serialized) && serialized[0] == 0x03) {
            const int negated = secp256k1_ec_seckey_negate(ctx, secret.data());
            BOOST_REQUIRE(negated == 1);
        }
    }
    if (ctx) secp256k1_context_destroy(ctx);
    return secret;
}

std::vector<unsigned char> EncodeBitmapUnchecked(const std::vector<uint8_t>& oracle_ids,
                                                 uint16_t total_oracles)
{
    std::vector<unsigned char> bitmap((total_oracles + 7) / 8, 0);
    for (uint8_t id : oracle_ids) {
        bitmap[id / 8] |= static_cast<unsigned char>(1U << (id % 8));
    }
    return bitmap;
}

bool SignBundleUnderActiveConsensus(COracleBundle& bundle,
                                    const std::vector<uint8_t>& oracle_ids,
                                    uint16_t total_oracles)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!ctx) return false;

    const size_t n_signers = oracle_ids.size();
    std::vector<std::array<unsigned char, 32>> seckeys(n_signers);
    std::vector<secp256k1_keypair> keypairs(n_signers);
    std::vector<secp256k1_pubkey> pubkeys(n_signers);

    for (size_t i = 0; i < n_signers; ++i) {
        seckeys[i] = TestnetOracleSecret(oracle_ids[i]);
        if (!secp256k1_keypair_create(ctx, &keypairs[i], seckeys[i].data()) ||
            !secp256k1_keypair_pub(ctx, &pubkeys[i], &keypairs[i])) {
            secp256k1_context_destroy(ctx);
            return false;
        }
    }

    std::vector<const secp256k1_pubkey*> pubkey_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; ++i) pubkey_ptrs[i] = &pubkeys[i];

    secp256k1_xonly_pubkey agg_pk{};
    secp256k1_musig_keyagg_cache cache{};
    if (!secp256k1_musig_pubkey_agg(ctx, &agg_pk, &cache,
                                    pubkey_ptrs.data(), n_signers)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    std::vector<secp256k1_musig_secnonce> secnonces(n_signers);
    std::vector<secp256k1_musig_pubnonce> pubnonces(n_signers);
    for (size_t i = 0; i < n_signers; ++i) {
        unsigned char session_rand[32];
        GetStrongRandBytes(Span{session_rand, 32});
        if (!secp256k1_musig_nonce_gen(ctx, &secnonces[i], &pubnonces[i],
                                       session_rand, seckeys[i].data(), &pubkeys[i],
                                       nullptr, &cache, nullptr)) {
            secp256k1_context_destroy(ctx);
            return false;
        }
    }

    std::vector<const secp256k1_musig_pubnonce*> nonce_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; ++i) nonce_ptrs[i] = &pubnonces[i];

    secp256k1_musig_aggnonce aggnonce{};
    if (!secp256k1_musig_nonce_agg(ctx, &aggnonce, nonce_ptrs.data(), n_signers)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    // Sign under the *active* chain's consensus hash (the testnet validator
    // path). Post-fix this hash will bind hashGenesisBlock; pre-fix it is
    // just H(epoch || price || timestamp).
    const uint256 msg_hash = ComputeOracleBundleHash(bundle);
    unsigned char msg32[32];
    std::memcpy(msg32, msg_hash.begin(), sizeof(msg32));

    secp256k1_musig_session session{};
    if (!secp256k1_musig_nonce_process(ctx, &session, &aggnonce, msg32, &cache)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    std::vector<secp256k1_musig_partial_sig> partial_sigs(n_signers);
    std::vector<const secp256k1_musig_partial_sig*> partial_ptrs(n_signers);
    for (size_t i = 0; i < n_signers; ++i) {
        if (!secp256k1_musig_partial_sign(ctx, &partial_sigs[i], &secnonces[i],
                                          &keypairs[i], &cache, &session)) {
            secp256k1_context_destroy(ctx);
            return false;
        }
        partial_ptrs[i] = &partial_sigs[i];
    }

    bundle.participation_bitmap = EncodeBitmapUnchecked(oracle_ids, total_oracles);
    bundle.aggregate_sig.assign(64, 0);
    if (!secp256k1_musig_partial_sig_agg(ctx, bundle.aggregate_sig.data(),
                                         &session, partial_ptrs.data(), n_signers)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    const bool verifies = secp256k1_schnorrsig_verify(ctx, bundle.aggregate_sig.data(),
                                                      msg32, 32, &agg_pk);
    secp256k1_context_destroy(ctx);
    return verifies;
}

COracleBundle MakeDomainTestBundle()
{
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = GetCurrentEpoch(DOMAIN_TEST_HEIGHT);
    bundle.median_price_micro_usd = DOMAIN_TEST_PRICE;
    bundle.timestamp = DOMAIN_TEST_TIMESTAMP;
    return bundle;
}

// A consensus snapshot whose oracle roster is byte-for-byte identical to
// the active testnet, but whose hashGenesisBlock differs. Stand-in for
// "another DigiByte chain (mainnet) using the same oracle operators".
Consensus::Params AlternateChainParams()
{
    Consensus::Params params = Params().GetConsensus();
    // Force a different chain identity than the easypow testnet whose
    // genesis the active context is using. Any non-zero, distinct hash works.
    uint256 alt_genesis = uint256S("0x1111111111111111111111111111111111111111111111111111111111111111");
    BOOST_REQUIRE(alt_genesis != params.hashGenesisBlock);
    params.hashGenesisBlock = alt_genesis;
    return params;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_oracle_domain_separation_tests, LocalMiniTestnetSetup)

// Baseline sanity: a freshly signed v0x03 bundle verifies under a
// copy of the active testnet consensus. We pass a copy (not the live
// reference) so ValidateMuSig2Bundle takes the
// ComputeAggregatePubkeyFromConsensusParams path and parses x-only
// pubkeys directly out of params.vOraclePublicKeys — that matches the
// 0x02-prefix canonical x-only keys produced by the easypow secret
// derivation in TestnetOracleSecret(). This guards the test harness so
// a future aggregator change cannot mask the cross-chain failure.
BOOST_AUTO_TEST_CASE(bundle_verifies_under_signing_chain)
{
    COracleBundle bundle = MakeDomainTestBundle();
    const std::vector<uint8_t> signers{0, 1, 2, 3, 4, 5, 6, 7, 8};
    BOOST_REQUIRE_EQUAL(static_cast<int>(signers.size()), DOMAIN_TEST_QUORUM);
    BOOST_REQUIRE(SignBundleUnderActiveConsensus(bundle, signers, DOMAIN_TEST_TOTAL_SLOTS));

    const Consensus::Params same_chain_params = Params().GetConsensus();
    std::string error;
    const bool ok = OracleBundleManager::ValidateMuSig2Bundle(
        bundle, DOMAIN_TEST_HEIGHT, same_chain_params, error);
    BOOST_TEST_MESSAGE("baseline same-chain validation: ok=" << ok << " error='" << error << "'");
    BOOST_CHECK_MESSAGE(ok,
        "v0x03 bundle must verify under the chain it was signed for");
}

// DD-FA-SEC-008 — cross-chain replay must be rejected.
//
// Re-presenting a v0x03 bundle that was signed for chain A (active
// testnet) to a validator running on chain B (synthesized via
// AlternateChainParams: same oracle keys, different hashGenesisBlock)
// must FAIL. Today it passes because the message hash carries no chain
// identifier — that is exactly the bug this case pins.
BOOST_AUTO_TEST_CASE(rejects_bundle_signed_for_different_chain)
{
    COracleBundle bundle = MakeDomainTestBundle();
    const std::vector<uint8_t> signers{0, 1, 2, 3, 4, 5, 6, 7, 8};
    BOOST_REQUIRE(SignBundleUnderActiveConsensus(bundle, signers, DOMAIN_TEST_TOTAL_SLOTS));

    // Sanity: the same bundle verifies under a copy of the signing chain
    // first, so failure under AlternateChainParams can only be due to the
    // hashGenesisBlock domain change (post-fix). Pre-fix this asserts the
    // bug is reachable.
    {
        const Consensus::Params same_chain = Params().GetConsensus();
        std::string err;
        BOOST_REQUIRE(OracleBundleManager::ValidateMuSig2Bundle(
            bundle, DOMAIN_TEST_HEIGHT, same_chain, err));
    }

    const Consensus::Params alt_params = AlternateChainParams();
    std::string error;
    const bool ok = OracleBundleManager::ValidateMuSig2Bundle(
        bundle, DOMAIN_TEST_HEIGHT, alt_params, error);
    BOOST_TEST_MESSAGE("cross-chain replay: ok=" << ok << " error='" << error << "'");

    BOOST_CHECK_MESSAGE(!ok,
        "DD-FA-SEC-008: a v0x03 bundle signed for one DigiByte chain must"
        " not validate under another chain (differing hashGenesisBlock),"
        " even when the oracle roster is identical. Domain-separate the"
        " MuSig2 message hash with the chain's hashGenesisBlock.");
    BOOST_CHECK_MESSAGE(error.find("signature") != std::string::npos ||
                        error.find("chain") != std::string::npos,
        "cross-chain rejection should be a signature/chain mismatch, got: '" + error + "'");
}

BOOST_AUTO_TEST_SUITE_END()
