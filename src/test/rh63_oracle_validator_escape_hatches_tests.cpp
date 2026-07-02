// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-63: ValidateBlockOracleData V1 fail-closed regression coverage.
 *
 * =================================================================
 * Target site
 * =================================================================
 *
 *   src/oracle/bundle_manager.cpp:2250-2329  OracleDataValidator::ValidateBlockOracleData
 *
 *   V1 accepts only MuSig2 oracle bundles encoded as v0x03. Legacy v0x01 /
 *   v0x02, unknown-version, and malformed oracle outputs must fail closed
 *   whenever an OP_ORACLE output is present. Non-DigiDollar blocks may omit
 *   oracle data; DigiDollar-touching blocks require a valid MuSig2 bundle.
 *
 * =================================================================
 * Attacker model
 * =================================================================
 *
 *   - Single malicious miner.
 *   - DigiDollar is BIP9-active for the block they are building.
 *   - No oracle-roster access, no MuSig2 key, no collusion required.
 *
 * Regression cases:
 *
 *   A. Post-activation non-DD block with NO OP_RETURN OP_ORACLE output.
 *      This remains valid; non-DD block templates are allowed to omit
 *      oracle data while MuSig2 sessions are incomplete.
 *
 *   B. Post-activation block with OP_RETURN OP_ORACLE + unknown version
 *      byte (e.g. 0x04).  ExtractOracleBundle returns false and block
 *      validation returns state.Invalid("bad-oracle-malformed").
 *
 *   C. Post-activation block with OP_RETURN OP_ORACLE + truncated v0x01
 *      payload.  ExtractOracleBundle rejects legacy v0x01 and block
 *      validation returns state.Invalid("bad-oracle-malformed").
 *
 * =================================================================
 * Concrete harm
 * =================================================================
 *
 * Concrete invariant:
 *
 *   If a post-activation coinbase includes OP_RETURN OP_ORACLE, it must
 *   contain a parseable and fully valid MuSig2 v0x03 bundle. Anything else
 *   is a consensus failure and must not update the oracle cache.
 *
 * =================================================================
 * Novelty vs. priors
 * =================================================================
 *
 * This file used to document the vulnerable behavior. It now locks in the
 * fixed V1 policy.
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <cstdint>
#include <vector>

namespace {

// Build a coinbase transaction whose scriptSig carries a BIP34 height push.
// Callers pick the height so that block_height >= nDDActivationHeight (650
// on regtest) to exercise the post-activation path inside
// ValidateBlockOracleData when pindex_prev is nullptr.
CMutableTransaction MakeCoinbaseWithBip34Height(int32_t height)
{
    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << static_cast<int64_t>(height);

    // vout[0]: trivial miner reward, not relevant to the oracle validator.
    cb.vout.resize(1);
    cb.vout[0].nValue = 50 * COIN;
    cb.vout[0].scriptPubKey = CScript() << OP_TRUE;

    return cb;
}

CBlock MakeBlock(CMutableTransaction&& coinbase)
{
    CBlock block;
    block.nVersion = 0x20000000;
    block.nTime    = static_cast<uint32_t>(1735689600); // 2025-01-01
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

// Non-DD block with no oracle output. V1 permits this when no completed
// MuSig2 v0x03 session is ready.
CBlock BuildBlock_NoOracleOutput(int32_t height)
{
    CMutableTransaction cb = MakeCoinbaseWithBip34Height(height);
    return MakeBlock(std::move(cb));
}

// OP_RETURN OP_ORACLE followed by a push that starts with an UNKNOWN
// version byte (0x04). V1 must reject the block because an oracle output is
// present but not a valid MuSig2 v0x03 bundle.
CBlock BuildBlock_UnknownVersion(int32_t height, uint8_t bogus_version)
{
    CMutableTransaction cb = MakeCoinbaseWithBip34Height(height);

    // Payload: bogus_version byte plus some plausible-looking bytes so the
    // push is non-trivial.  Attacker can pick anything here; a 17-byte body
    // mimics the V01 shape for maximum "looks real" optics.
    std::vector<unsigned char> payload;
    payload.reserve(18);
    payload.push_back(bogus_version);
    for (int i = 0; i < 17; ++i) payload.push_back(static_cast<unsigned char>(i));

    CScript oracle_spk;
    oracle_spk << OP_RETURN << OP_ORACLE << payload;

    cb.vout.push_back(CTxOut(0, oracle_spk));
    return MakeBlock(std::move(cb));
}

// OP_RETURN OP_ORACLE + legacy v0x01 marker with a truncated body. V1
// rejects legacy oracle versions before attempting Phase 1 parsing.
CBlock BuildBlock_TruncatedV01(int32_t height)
{
    CMutableTransaction cb = MakeCoinbaseWithBip34Height(height);

    std::vector<unsigned char> payload;
    payload.push_back(0x01);       // claim V01
    // Only 4 body bytes -- far short of the 17 V01 requires.
    payload.push_back(0x00);
    payload.push_back(0xAA);
    payload.push_back(0xBB);
    payload.push_back(0xCC);

    CScript oracle_spk;
    oracle_spk << OP_RETURN << OP_ORACLE << payload;

    cb.vout.push_back(CTxOut(0, oracle_spk));
    return MakeBlock(std::move(cb));
}

// Legacy v0x01-shaped body. V1 must reject it instead of parsing it as a
// Phase 1 oracle bundle.
CBlock BuildBlock_CorrectV01Shape(int32_t height, uint8_t oracle_id, uint64_t price)
{
    CMutableTransaction cb = MakeCoinbaseWithBip34Height(height);

    std::vector<unsigned char> payload;
    payload.reserve(18);
    payload.push_back(0x01);       // version
    payload.push_back(oracle_id);  // oracle_id
    for (int i = 0; i < 8; ++i) payload.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));
    const int64_t ts = 1735689500;
    for (int i = 0; i < 8; ++i) payload.push_back(static_cast<unsigned char>((static_cast<uint64_t>(ts) >> (i * 8)) & 0xFF));

    CScript oracle_spk;
    oracle_spk << OP_RETURN << OP_ORACLE << payload;

    cb.vout.push_back(CTxOut(0, oracle_spk));
    return MakeBlock(std::move(cb));
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(rh63_oracle_validator_escape_hatches_tests, RegTestingSetup)

// =====================================================================
// RH-63-01: No OP_RETURN OP_ORACLE output at all. Non-DD blocks may omit
//           oracle data while V1 waits for a completed MuSig2 session.
// =====================================================================
BOOST_AUTO_TEST_CASE(rh63_01_escape_hatch_no_oracle_output_post_activation)
{
    const int32_t HEIGHT = 700; // regtest nDDActivationHeight = 650

    CBlock block = BuildBlock_NoOracleOutput(HEIGHT);
    BlockValidationState state;

    // pindex_prev nullptr -> validator falls back to BIP34-height-from-scriptSig
    // logic at bundle_manager.cpp:2269-2283, yielding HEIGHT=700.
    // Then at :2291-2296 pindex_prev==nullptr is the nullptr branch:
    //   if (block_height < params.nDDActivationHeight) return true;
    // 700 >= 650 so we DO NOT bail on the pre-activation early-return --
    // we proceed into the oracle-output-count check.
    const bool ok = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, Params().GetConsensus(), state);

    BOOST_CHECK_MESSAGE(ok,
        "Non-DD block without oracle output should remain valid; V1 miners "
        "omit oracle data until a completed MuSig2 v0x03 session exists.");
    BOOST_CHECK_MESSAGE(!state.IsInvalid(),
        "Non-DD block without oracle output should not invalidate state. "
        "Observed: " << state.ToString());

    BOOST_TEST_MESSAGE("RH-63-01: post-activation non-DD block with zero "
        "oracle outputs remained valid. validator.return = " << ok
        << " state.IsInvalid = " << state.IsInvalid());
}

// =====================================================================
// RH-63-02: Unknown version byte (0x04). Any present oracle output that is
//           not parseable as MuSig2 v0x03 must fail closed.
// =====================================================================
BOOST_AUTO_TEST_CASE(rh63_02_escape_hatch_unknown_version_byte)
{
    const int32_t HEIGHT = 800;

    CBlock block = BuildBlock_UnknownVersion(HEIGHT, /*bogus_version=*/0x04);
    BlockValidationState state;

    // Sanity: prove that ExtractOracleBundle actually fails on this input.
    // If it succeeded we'd be measuring a different attack.
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();
    COracleBundle dummy;
    const bool extract_ok = mgr.ExtractOracleBundle(*block.vtx[0], dummy);
    BOOST_REQUIRE_MESSAGE(!extract_ok,
        "Precondition: ExtractOracleBundle must FAIL on unknown version 0x04. "
        "If this assertion fires, ExtractOracleBundle grew a new version "
        "handler and this test needs to pick a different bogus byte.");

    const bool ok = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, Params().GetConsensus(), state);

    BOOST_CHECK_MESSAGE(!ok,
        "Unknown-version oracle output must be rejected, not accepted as a "
        "transition-period fallback.");
    BOOST_CHECK_MESSAGE(state.IsInvalid(),
        "Unknown-version oracle output must produce state.Invalid. "
        "Observed: " << state.ToString());

    BOOST_TEST_MESSAGE("RH-63-02: post-activation (h=800) block with "
        "OP_RETURN OP_ORACLE + version=0x04 (unknown) rejected.");
}

// =====================================================================
// RH-63-03: Truncated legacy v0x01 body. Legacy/malformed oracle outputs
//           must fail closed.
// =====================================================================
BOOST_AUTO_TEST_CASE(rh63_03_escape_hatch_truncated_v01_body)
{
    const int32_t HEIGHT = 900;

    CBlock block = BuildBlock_TruncatedV01(HEIGHT);
    BlockValidationState state;

    OracleBundleManager& mgr = OracleBundleManager::GetInstance();
    COracleBundle dummy;
    const bool extract_ok = mgr.ExtractOracleBundle(*block.vtx[0], dummy);
    BOOST_REQUIRE_MESSAGE(!extract_ok,
        "Precondition: truncated V01 payload must fail ExtractOracleBundle.");

    const bool ok = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, Params().GetConsensus(), state);

    BOOST_CHECK_MESSAGE(!ok,
        "Truncated legacy v0x01 oracle output must be rejected.");
    BOOST_CHECK_MESSAGE(state.IsInvalid(),
        "Observed: " << state.ToString());

    BOOST_TEST_MESSAGE("RH-63-03: post-activation (h=900) block with "
        "truncated V01 oracle payload rejected.");
}

// =====================================================================
// RH-63-04: Correctly-shaped legacy v0x01 body is still rejected. V1 does
//           not parse or validate Phase 1 oracle bundles as fallback.
// =====================================================================
BOOST_AUTO_TEST_CASE(rh63_04_correct_v01_shape_is_rejected)
{
    const int32_t HEIGHT = 1000;

    CBlock block = BuildBlock_CorrectV01Shape(HEIGHT,
                                              /*oracle_id=*/0,
                                              /*price=*/100000ULL);
    BlockValidationState state;

    OracleBundleManager& mgr = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    const bool extract_ok = mgr.ExtractOracleBundle(*block.vtx[0], bundle);
    BOOST_CHECK_MESSAGE(!extract_ok,
        "V1 must not extract legacy v0x01 oracle bundles.");

    const bool ok = OracleDataValidator::ValidateBlockOracleData(
        block, /*pindex_prev=*/nullptr, Params().GetConsensus(), state);

    BOOST_CHECK_MESSAGE(!ok, "Legacy v0x01 oracle output must be rejected.");
    BOOST_CHECK_MESSAGE(state.IsInvalid(), "Observed: " << state.ToString());

    BOOST_TEST_MESSAGE("RH-63-04: correctly-shaped legacy v0x01 at h=1000 "
        "was rejected. validator.return=" << ok
        << " state.IsInvalid=" << state.IsInvalid());
}

// =====================================================================
// RH-63-05: Malformed oracle outputs do not update the price cache and do
//           not pass validation. A non-DD block without oracle data remains
//           valid and also leaves the cache unchanged.
// =====================================================================
BOOST_AUTO_TEST_CASE(rh63_05_composed_oracle_dos_stretch)
{
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();

    // Seed the cache so we can observe staleness.
    const CAmount SEED_PRICE = 12345678LL;
    mgr.UpdatePriceCache(999, SEED_PRICE);
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), SEED_PRICE);

    const int32_t H_BASE = 2000;

    // One no-oracle non-DD block and two malformed oracle-bearing blocks.
    CBlock b1 = BuildBlock_NoOracleOutput(H_BASE);
    CBlock b2 = BuildBlock_UnknownVersion(H_BASE + 1, /*bogus_version=*/0xAB);
    CBlock b3 = BuildBlock_TruncatedV01(H_BASE + 2);

    BlockValidationState s1, s2, s3;
    BOOST_CHECK(OracleDataValidator::ValidateBlockOracleData(b1, nullptr, Params().GetConsensus(), s1));
    BOOST_CHECK(!OracleDataValidator::ValidateBlockOracleData(b2, nullptr, Params().GetConsensus(), s2));
    BOOST_CHECK(!OracleDataValidator::ValidateBlockOracleData(b3, nullptr, Params().GetConsensus(), s3));
    BOOST_CHECK(!s1.IsInvalid());
    BOOST_CHECK(s2.IsInvalid());
    BOOST_CHECK(s3.IsInvalid());

    // ExtractOracleBundle (the same call ConnectBlock makes before
    // UpdatePriceCache at validation.cpp:2832) fails on all three.
    COracleBundle dummy;
    BOOST_CHECK(!mgr.ExtractOracleBundle(*b1.vtx[0], dummy));
    BOOST_CHECK(!mgr.ExtractOracleBundle(*b2.vtx[0], dummy));
    BOOST_CHECK(!mgr.ExtractOracleBundle(*b3.vtx[0], dummy));

    // No valid MuSig2 bundle was extracted, so the seed price survives.
    BOOST_CHECK_EQUAL(mgr.GetLatestPrice(), SEED_PRICE);

    BOOST_TEST_MESSAGE("RH-63-05: missing oracle data on a non-DD block did "
        "not update cache; malformed oracle-bearing blocks were rejected. "
        "GetLatestPrice remained " << SEED_PRICE << ".");
}

// =====================================================================
// RH-63-06: Future/unknown version bytes are rejected. V1 does not silently
//           accept an OP_ORACLE marker unless it is valid MuSig2 v0x03.
// =====================================================================
BOOST_AUTO_TEST_CASE(rh63_06_future_version_silent_acceptance)
{
    const int32_t HEIGHT = 3000;

    for (uint8_t v : {uint8_t{0x00}, uint8_t{0x04}, uint8_t{0x10}, uint8_t{0x7F}, uint8_t{0xFF}}) {
        CBlock block = BuildBlock_UnknownVersion(HEIGHT, v);
        BlockValidationState state;

        const bool ok = OracleDataValidator::ValidateBlockOracleData(
            block, /*pindex_prev=*/nullptr, Params().GetConsensus(), state);

        BOOST_CHECK_MESSAGE(!ok,
            "Unknown-version byte " << static_cast<int>(v)
            << " must be rejected.");
        BOOST_CHECK(state.IsInvalid());
    }

    BOOST_TEST_MESSAGE("RH-63-06: all of {0x00,0x04,0x10,0x7F,0xFF} version "
        "bytes rejected by ValidateBlockOracleData at h=3000.");
}

// =====================================================================
// DD-RH-052 / RH-63-07: Block validation must never fall back to local
//           oracle state. If no valid MuSig2 bundle was extracted for the
//           block, DD transaction validation must see price=0 and fail
//           closed instead of consulting node-local P2P/mock/cache prices.
// =====================================================================
BOOST_AUTO_TEST_CASE(rh63_07_block_path_rejects_local_oracle_fallback)
{
    OracleBundleManager& mgr = OracleBundleManager::GetInstance();
    const CAmount SEED_CACHE_PRICE = 23456789LL;
    mgr.UpdatePriceCache(4000, SEED_CACHE_PRICE);
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), SEED_CACHE_PRICE);

    CMutableTransaction dummy_mut;
    CTransaction dummy_tx(dummy_mut);

    BOOST_CHECK_EQUAL(
        GetOraclePriceForTransaction(dummy_tx, /*nHeight=*/4001, /*blockOraclePrice=*/0),
        0);
    BOOST_CHECK_EQUAL(
        GetOraclePriceForTransaction(dummy_tx, /*nHeight=*/4001, /*blockOraclePrice=*/987654LL),
        987654LL);

    BOOST_TEST_MESSAGE("DD-RH-052: block validation path with no extracted "
        "oracle bundle must return price=0 even when local cached oracle "
        "state is populated. The DD transaction validator must fail closed "
        "instead of using nondeterministic local state.");
}

BOOST_AUTO_TEST_SUITE_END()
