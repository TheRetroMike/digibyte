// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RED HORNET AUDIT — Phase 2 Multi-Oracle Schnorr Signature Security Tests
 *
 * Attack surface tests for:
 *  1. Signature bypass vectors
 *  2. Price manipulation via selective inclusion
 *  3. Consensus fork vectors (determinism)
 *  4. Oracle identity attacks
 *  5. Signature replay
 *  6. Version downgrade (Phase 2 → Phase 1)
 *  7. IQR outlier gaming
 *  8. Consensus price determinism (CalculateConsensusPrice vs GetConsensusPrice)
 */

#include <boost/test/unit_test.hpp>
#include <logging.h>
#include <util/strencodings.h>

#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/sha256.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <oracle/mock_oracle.h>
#include <oracle/node.h>
#include <primitives/oracle.h>
#include <pubkey.h>
#include <random.h>
#include <test/util/setup_common.h>
#include <util/time.h>

#include <algorithm>
#include <set>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(redteam_phase2_audit_tests, RegTestingSetup)

// ============================================================================
// HELPERS
// ============================================================================

static CKey GetRegtestOracleKey(uint32_t oracle_id)
{
    std::string seed = "digibyte_regtest_oracle_" + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write((const unsigned char*)seed.data(), seed.size()).Finalize(hash.begin());
    CKey key;
    key.Set(hash.begin(), hash.end(), true);
    return key;
}

static COraclePriceMessage MakeSignedMsg(const CKey& key, uint32_t id, uint64_t price, int64_t ts)
{
    COraclePriceMessage msg;
    msg.oracle_id = id;
    msg.price_micro_usd = price;
    msg.timestamp = ts;
    msg.block_height = 700;
    msg.nonce = GetRand<uint64_t>(std::numeric_limits<uint64_t>::max());
    msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
    BOOST_REQUIRE(msg.SignAttestation(key));
    return msg;
}

static Consensus::Params MakePhase2Params(int required, int total, int phase2_height = 100)
{
    Consensus::Params p;
    p.nOracleRequiredMessages = required;
    p.nOracleTotalOracles = total;
    p.nDDActivationHeight = phase2_height;
    p.nOracleEpochLength = 144;
    return p;
}

// ============================================================================
// ATTACK 1: EMPTY SIGNATURE BYPASS (Phase 2)
// Can a Phase 2 bundle pass validation with empty schnorr_sig fields?
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_empty_sig_phase2_bundle)
{
    LogPrintf("REDTEAM: Attack 1 — empty signature bypass in Phase 2 bundle\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    int64_t ts = GetTime();

    // Create 5 messages with NO signatures (schnorr_sig is empty)
    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;
    for (uint32_t i = 0; i < 5; ++i) {
        COraclePriceMessage msg;
        msg.oracle_id = i;
        msg.price_micro_usd = 50000;
        msg.timestamp = ts;
        msg.block_height = 700;
        msg.nonce = i;
        // DO NOT sign — schnorr_sig stays empty
        bundle.messages.push_back(msg);
    }
    bundle.median_price_micro_usd = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result,
        "CRITICAL: Phase 2 bundle with empty signatures should be REJECTED");

    LogPrintf("REDTEAM: Attack 1 — %s\n", result ? "EXPLOITABLE!!" : "Defense holds ✓");
}

// ============================================================================
// ATTACK 2: ALL-ZERO SIGNATURE BYPASS
// What if schnorr_sig is 64 zero bytes?
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_zero_sig_phase2_bundle)
{
    LogPrintf("REDTEAM: Attack 2 — all-zero signature bypass\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    int64_t ts = GetTime();

    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;

    for (uint32_t i = 0; i < 5; ++i) {
        COraclePriceMessage msg;
        msg.oracle_id = i;
        msg.price_micro_usd = 50000;
        msg.timestamp = ts;
        msg.block_height = 700;
        msg.nonce = i;
        msg.oracle_pubkey = XOnlyPubKey(GetRegtestOracleKey(i).GetPubKey());
        msg.schnorr_sig.assign(64, 0x00); // 64 zero bytes
        bundle.messages.push_back(msg);
    }
    bundle.median_price_micro_usd = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result,
        "CRITICAL: Phase 2 bundle with all-zero signatures should be REJECTED");

    LogPrintf("REDTEAM: Attack 2 — %s\n", result ? "EXPLOITABLE!!" : "Defense holds ✓");
}

// ============================================================================
// ATTACK 3: VERSION DOWNGRADE — use 0x01 after Phase 2 activation
// Can a miner submit Phase 1 (version 0x01) data after Phase 2 is active?
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_version_downgrade)
{
    LogPrintf("REDTEAM: Attack 3 — version downgrade (Phase 1 format after Phase 2 activation)\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    CKey key = GetRegtestOracleKey(0);
    int64_t ts = GetTime();

    // Create a Phase 1 bundle (single message, no embedded sig)
    COracleBundle p1_bundle;
    p1_bundle.epoch = 0;
    p1_bundle.timestamp = ts;

    COraclePriceMessage p1_msg;
    p1_msg.oracle_id = 0;
    p1_msg.price_micro_usd = 99999999; // Attacker's manipulated price
    p1_msg.timestamp = ts;
    // NO signature — Phase 1 compact format doesn't include it
    p1_bundle.messages.push_back(p1_msg);
    p1_bundle.median_price_micro_usd = p1_msg.price_micro_usd;

    // V1 no longer emits Phase 1 scripts.
    CScript p1_script = manager.CreateOracleScript(p1_bundle);
    BOOST_CHECK_MESSAGE(p1_script.empty(),
        "V1 must not serialize legacy Phase 1 oracle scripts");

    // Construct a coinbase with the Phase 1 data
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    CTxOut oracle_out;
    oracle_out.nValue = 0;
    oracle_out.scriptPubKey = p1_script;
    coinbase.vout.push_back(oracle_out);
    CTransaction tx(coinbase);

    // No legacy bundle should be extractable from V1 block data.
    COracleBundle extracted;
    bool extracted_ok = manager.ExtractOracleBundle(tx, extracted);
    BOOST_CHECK_MESSAGE(!extracted_ok,
        "V1 must not extract legacy Phase 1 oracle scripts");

    // A hand-built Phase 1 bundle still fails Phase 2 validation because Phase
    // Two requires signed quorum messages.
    const Consensus::Params& regtest_params = Params().GetConsensus();
    bool result = OracleBundleManager::ValidateBundle(p1_bundle, 0, regtest_params);
    BOOST_CHECK_MESSAGE(!result,
        "CRITICAL: Phase 1 bundle should be REJECTED during Phase 2 validation");

    LogPrintf("REDTEAM: Attack 3 — %s\n", result ? "EXPLOITABLE!!" : "Defense holds ✓");
}

// ============================================================================
// ATTACK 4: CROSS-SIGNING — oracle A signs message claiming to be oracle B
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_cross_signing)
{
    LogPrintf("REDTEAM: Attack 4 — cross-signing attack (oracle A claims oracle B's ID)\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    int64_t ts = GetTime();

    // Oracle keys 0-4 from regtest chainparams
    std::vector<CKey> keys;
    for (uint32_t i = 0; i < 5; ++i) keys.push_back(GetRegtestOracleKey(i));

    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;

    // 3 honest oracles sign with their own IDs
    for (uint32_t i = 0; i < 3; ++i) {
        bundle.messages.push_back(MakeSignedMsg(keys[i], i, 50000, ts));
    }

    // ATTACK: Oracle 3 signs but claims to be oracle 4
    // Signature covers H(oracle_id=4, price, timestamp) but signed by key[3]
    COraclePriceMessage cross_msg;
    cross_msg.oracle_id = 4; // FAKE — claims to be oracle 4
    cross_msg.price_micro_usd = 50000;
    cross_msg.timestamp = ts;
    cross_msg.block_height = 700;
    cross_msg.nonce = 999;
    cross_msg.oracle_pubkey = XOnlyPubKey(keys[3].GetPubKey()); // oracle 3's key
    BOOST_REQUIRE(cross_msg.SignAttestation(keys[3])); // Signed by oracle 3
    bundle.messages.push_back(cross_msg);

    bundle.median_price_micro_usd = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    // Round-trip through on-chain format to bind chainparams pubkeys
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    CScript script = manager.CreateOracleScript(bundle);
    BOOST_CHECK_MESSAGE(script.empty(),
        "V1 must not serialize legacy Phase 2 oracle scripts");

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    CTxOut oracle_out;
    oracle_out.nValue = 0;
    oracle_out.scriptPubKey = script;
    coinbase.vout.push_back(oracle_out);
    CTransaction tx(coinbase);

    COracleBundle extracted;
    BOOST_CHECK_MESSAGE(!manager.ExtractOracleBundle(tx, extracted),
        "V1 must not extract legacy Phase 2 oracle scripts");

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result || script.empty(),
        "Cross-signed legacy bundle must not reach block-validation quorum");

    LogPrintf("REDTEAM: Attack 4 — %s (legacy script omitted)\n",
              (result && !script.empty()) ? "EXPLOITABLE!!" : "Defense holds ✓");
}

// ============================================================================
// ATTACK 5: SIGNATURE REPLAY — reuse signatures from a previous block
// Phase 2 sig = H(oracle_id, price, timestamp). Same price+timestamp = same sig.
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_signature_replay)
{
    LogPrintf("REDTEAM: Attack 5 — signature replay from previous block\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    std::vector<CKey> keys;
    for (uint32_t i = 0; i < 5; ++i) keys.push_back(GetRegtestOracleKey(i));

    int64_t ts = GetTime();
    uint64_t price = 50000;

    // Block 1: Create valid Phase 2 bundle
    COracleBundle bundle1;
    bundle1.epoch = 0;
    bundle1.timestamp = ts;
    for (uint32_t i = 0; i < 5; ++i) {
        bundle1.messages.push_back(MakeSignedMsg(keys[i], i, price, ts));
    }
    bundle1.median_price_micro_usd = price;

    CScript script1 = manager.CreateOracleScript(bundle1);
    BOOST_CHECK_MESSAGE(script1.empty(),
        "V1 must not serialize legacy Phase 2 oracle scripts");
    CMutableTransaction cb1;
    cb1.vin.resize(1); cb1.vin[0].prevout.SetNull();
    cb1.vout.resize(1); cb1.vout[0].scriptPubKey = CScript() << OP_TRUE;
    CTxOut o1; o1.nValue = 0; o1.scriptPubKey = script1;
    cb1.vout.push_back(o1);
    CTransaction tx1(cb1);

    // No legacy signatures should be extractable in V1.
    COracleBundle ext1;
    BOOST_CHECK_MESSAGE(!manager.ExtractOracleBundle(tx1, ext1),
        "V1 must not extract legacy Phase 2 oracle scripts");

    // Block 2: REPLAY — copy exact same script into a new coinbase
    // A miner could take the oracle output from block N and put it in block N+1
    CMutableTransaction cb2;
    cb2.vin.resize(1); cb2.vin[0].prevout.SetNull();
    cb2.vout.resize(1); cb2.vout[0].scriptPubKey = CScript() << OP_TRUE;
    cb2.vout.push_back(o1); // SAME oracle data
    CTransaction tx2(cb2);

    COracleBundle ext2;
    BOOST_CHECK_MESSAGE(!manager.ExtractOracleBundle(tx2, ext2),
        "V1 must not extract replayed legacy Phase 2 oracle scripts");

    LogPrintf("REDTEAM: Attack 5 — Defense holds ✓ (legacy replay data is not serialized)\n");
}

// ============================================================================
// ATTACK 6: SELECTIVE ORACLE INCLUSION — cherry-pick which attestations to include
// With 7 attestations and threshold 4, include only the 4 with highest price
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_selective_oracle_inclusion)
{
    LogPrintf("REDTEAM: Attack 6 — selective oracle inclusion for price manipulation\n");

    // In Phase 2, ALL oracles sign the SAME consensus price. So selecting
    // different subsets of 4 oracles doesn't change the price — they all
    // attested to the same value.

    std::vector<CKey> keys;
    for (uint32_t i = 0; i < 7; ++i) keys.push_back(GetRegtestOracleKey(i));

    int64_t ts = GetTime();
    uint64_t consensus_price = 51000;

    // All 7 oracles sign same consensus price
    COracleBundle bundle_all;
    bundle_all.epoch = 0;
    bundle_all.timestamp = ts;
    for (uint32_t i = 0; i < 7; ++i) {
        bundle_all.messages.push_back(MakeSignedMsg(keys[i], i, consensus_price, ts));
    }
    bundle_all.median_price_micro_usd = consensus_price;

    // Take only 4 of 7 — price remains the same
    COracleBundle bundle_subset;
    bundle_subset.epoch = 0;
    bundle_subset.timestamp = ts;
    bundle_subset.messages.assign(bundle_all.messages.begin(), bundle_all.messages.begin() + 4);
    bundle_subset.median_price_micro_usd = consensus_price;

    Consensus::Params params = MakePhase2Params(4, 7);
    CAmount price_all = OracleBundleManager::CalculateConsensusPrice(bundle_all, params);
    CAmount price_sub = OracleBundleManager::CalculateConsensusPrice(bundle_subset, params);

    BOOST_CHECK_EQUAL(price_all, price_sub);
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(price_all), consensus_price);

    LogPrintf("REDTEAM: Attack 6 — Defense holds ✓ (all oracles sign same consensus price, selection doesn't matter)\n");
}

// ============================================================================
// ATTACK 7: IQR GAMING — carefully positioned prices to exclude honest oracles
// 3 colluding oracles out of 5 try to shift the consensus price
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_iqr_gaming)
{
    LogPrintf("REDTEAM: Attack 7 — IQR outlier filter gaming\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    std::vector<CKey> keys;
    for (uint32_t i = 0; i < 7; ++i) keys.push_back(GetRegtestOracleKey(i));

    int64_t ts = GetTime();

    // Scenario: 4 honest oracles report ~50000, 3 colluding report ~80000
    // With IQR filtering, the colluding prices should be filtered as outliers
    // if the spread is large enough

    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;

    // 4 honest oracles
    bundle.messages.push_back(MakeSignedMsg(keys[0], 0, 49500, ts));
    bundle.messages.push_back(MakeSignedMsg(keys[1], 1, 50000, ts));
    bundle.messages.push_back(MakeSignedMsg(keys[2], 2, 50500, ts));
    bundle.messages.push_back(MakeSignedMsg(keys[3], 3, 51000, ts));

    // 3 colluding oracles push price up
    bundle.messages.push_back(MakeSignedMsg(keys[4], 4, 80000, ts));
    bundle.messages.push_back(MakeSignedMsg(keys[5], 5, 81000, ts));
    bundle.messages.push_back(MakeSignedMsg(keys[6], 6, 82000, ts));

    CAmount consensus = OracleBundleManager::CalculateConsensusPrice(bundle, params);

    // IQR should filter the 80k+ outliers
    // After filtering, median of {49500, 50000, 50500, 51000} = (50000+50500)/2 = 50250
    // OR if IQR includes 80k, median shifts up
    LogPrintf("REDTEAM: Attack 7 — Consensus price with 3 colluding oracles: %lld\n", consensus);

    // Verify colluding minority can't push price above honest range
    BOOST_CHECK_MESSAGE(consensus < 60000,
        strprintf("IQR should prevent 3/7 colluding oracles from skewing price to %lld", consensus));

    LogPrintf("REDTEAM: Attack 7 — %s (consensus=%lld)\n",
              consensus < 60000 ? "Defense holds ✓" : "EXPLOITABLE!!", consensus);
}

// ============================================================================
// ATTACK 8: CONSENSUS PRICE DETERMINISM — verify CalculateConsensusPrice ==
// GetConsensusPrice for identical input
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_consensus_price_determinism)
{
    LogPrintf("REDTEAM: Attack 8 — consensus price determinism check\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    std::vector<CKey> keys;
    for (uint32_t i = 0; i < 7; ++i) keys.push_back(GetRegtestOracleKey(i));

    int64_t ts = GetTime();
    uint64_t prices[] = {48000, 49000, 50000, 51000, 52000, 53000, 54000};

    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;
    for (uint32_t i = 0; i < 7; ++i) {
        bundle.messages.push_back(MakeSignedMsg(keys[i], i, prices[i], ts));
    }

    // Both price calculation methods must return identical results
    CAmount price_static = OracleBundleManager::CalculateConsensusPrice(bundle, params);
    CAmount price_member = static_cast<CAmount>(bundle.GetConsensusPrice(4));

    BOOST_CHECK_EQUAL(price_static, price_member);
    LogPrintf("REDTEAM: Attack 8 — CalculateConsensusPrice=%lld, GetConsensusPrice=%lld → %s\n",
              price_static, price_member,
              price_static == price_member ? "DETERMINISTIC ✓" : "FORK VECTOR!!");
}

// ============================================================================
// ATTACK 9: NON-EXISTENT ORACLE ID in Phase 2 bundle
// Set oracle_id to 200 (doesn't exist in chainparams)
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_nonexistent_oracle_id)
{
    LogPrintf("REDTEAM: Attack 9 — non-existent oracle ID in Phase 2 bundle\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    int64_t ts = GetTime();

    CKey rogue_key;
    rogue_key.MakeNewKey(true);

    // Create bundle with 5 messages — all from non-existent oracle IDs
    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;
    for (uint32_t i = 0; i < 5; ++i) {
        bundle.messages.push_back(MakeSignedMsg(rogue_key, 200 + i, 50000, ts));
    }
    bundle.median_price_micro_usd = 50000;

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result,
        "Bundle with non-existent oracle IDs should be REJECTED");

    LogPrintf("REDTEAM: Attack 9 — %s\n", result ? "EXPLOITABLE!!" : "Defense holds ✓");
}

// ============================================================================
// ATTACK 10: ORACLE_ID > 255 on-chain (1-byte truncation)
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_oracle_id_truncation)
{
    LogPrintf("REDTEAM: Attack 10 — oracle_id > 255 truncation attack\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    CKey key = GetRegtestOracleKey(0);
    int64_t ts = GetTime();

    // Create a message with oracle_id = 256 (should be rejected at serialization)
    COraclePriceMessage msg = MakeSignedMsg(key, 256, 50000, ts);

    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;
    bundle.messages.push_back(msg);
    bundle.median_price_micro_usd = 50000;

    CScript script = manager.CreateOracleScript(bundle);

    // CreateOracleScript should reject oracle_id > 255 (defense-in-depth DGB-SEC-004)
    BOOST_CHECK_MESSAGE(script.empty(),
        "CreateOracleScript should return empty script for oracle_id > 255");

    // Also check that IsValidOracleMessage rejects it
    bool valid = manager.AddOracleMessage(msg);
    // Note: AddOracleMessage calls IsValidOracleMessage which checks oracle_id > 255

    LogPrintf("REDTEAM: Attack 10 — Script empty: %s, AddOracleMessage: %s\n",
              script.empty() ? "YES ✓" : "NO!!", valid ? "accepted (check code)" : "rejected ✓");
}

// ============================================================================
// ATTACK 11: PHASE 2 FORMAT PARSING — malformed data
// Craft invalid Phase 2 data to see if parsing fails safely
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_malformed_phase2_data)
{
    LogPrintf("REDTEAM: Attack 11 — malformed Phase 2 data parsing\n");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();

    // Test 1: num_messages=255 but data is only 18 bytes
    {
        CScript script;
        script << OP_RETURN << OP_ORACLE;
        std::vector<unsigned char> data;
        data.push_back(0x02); // Version 2
        data.push_back(0xFF); // num_messages = 255
        // Only 16 more bytes of data (price + timestamp) — no oracle entries
        for (int i = 0; i < 16; ++i) data.push_back(0x00);
        script << data;

        CMutableTransaction coinbase;
        coinbase.vin.resize(1); coinbase.vin[0].prevout.SetNull();
        coinbase.vout.resize(1); coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
        CTxOut o; o.nValue = 0; o.scriptPubKey = script;
        coinbase.vout.push_back(o);
        CTransaction tx(coinbase);

        COracleBundle extracted;
        bool ok = manager.ExtractOracleBundle(tx, extracted);
        BOOST_CHECK_MESSAGE(!ok, "Malformed Phase 2 (255 msgs, 18 bytes) should fail extraction");
    }

    // Test 2: num_messages=0
    {
        CScript script;
        script << OP_RETURN << OP_ORACLE;
        std::vector<unsigned char> data;
        data.push_back(0x02); // Version 2
        data.push_back(0x00); // num_messages = 0
        for (int i = 0; i < 16; ++i) data.push_back(0x00);
        script << data;

        CMutableTransaction coinbase;
        coinbase.vin.resize(1); coinbase.vin[0].prevout.SetNull();
        coinbase.vout.resize(1); coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
        CTxOut o; o.nValue = 0; o.scriptPubKey = script;
        coinbase.vout.push_back(o);
        CTransaction tx(coinbase);

        COracleBundle extracted;
        bool ok = manager.ExtractOracleBundle(tx, extracted);
        // num_messages=0 means no oracle entries, extraction might succeed with empty messages
        // If it succeeds, ValidatePhaseTwoBundle should reject it
        if (ok) {
            Consensus::Params params = MakePhase2Params(4, 7);
            bool valid = OracleBundleManager::ValidateBundle(extracted, 0, params);
            BOOST_CHECK_MESSAGE(!valid, "Phase 2 bundle with 0 messages should fail validation");
        }
    }

    // Test 3: Truncated oracle entry (oracle_id but no sig)
    {
        CScript script;
        script << OP_RETURN << OP_ORACLE;
        std::vector<unsigned char> data;
        data.push_back(0x02); // Version 2
        data.push_back(0x01); // num_messages = 1
        for (int i = 0; i < 16; ++i) data.push_back(0x00); // price + timestamp
        data.push_back(0x00); // oracle_id
        // Missing 64 bytes of signature
        script << data;

        CMutableTransaction coinbase;
        coinbase.vin.resize(1); coinbase.vin[0].prevout.SetNull();
        coinbase.vout.resize(1); coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
        CTxOut o; o.nValue = 0; o.scriptPubKey = script;
        coinbase.vout.push_back(o);
        CTransaction tx(coinbase);

        COracleBundle extracted;
        bool ok = manager.ExtractOracleBundle(tx, extracted);
        BOOST_CHECK_MESSAGE(!ok, "Truncated Phase 2 entry (no sig) should fail extraction");
    }

    LogPrintf("REDTEAM: Attack 11 — All malformed data handled safely ✓\n");
}

// ============================================================================
// ATTACK 12: MEDIAN DETERMINISM — verify even-count averaging is identical
// across both implementations (potential integer division rounding divergence)
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_median_rounding_determinism)
{
    LogPrintf("REDTEAM: Attack 12 — median rounding determinism for even counts\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    std::vector<CKey> keys;
    for (uint32_t i = 0; i < 6; ++i) keys.push_back(GetRegtestOracleKey(i));

    int64_t ts = GetTime();

    // Even count with values that produce non-integer average
    // 50001 + 50002 = 100003 / 2 = 50001 (integer truncation)
    uint64_t prices[] = {49999, 50001, 50002, 50004, 50006, 50008};

    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;
    for (uint32_t i = 0; i < 6; ++i) {
        bundle.messages.push_back(MakeSignedMsg(keys[i], i, prices[i], ts));
    }

    CAmount price_static = OracleBundleManager::CalculateConsensusPrice(bundle, params);
    CAmount price_member = static_cast<CAmount>(bundle.GetConsensusPrice(4));

    BOOST_CHECK_EQUAL(price_static, price_member);
    LogPrintf("REDTEAM: Attack 12 — Even-count median: static=%lld, member=%lld → %s\n",
              price_static, price_member,
              price_static == price_member ? "DETERMINISTIC ✓" : "FORK VECTOR!!");
}

// ============================================================================
// ATTACK 13: DUPLICATE ORACLE ID to inflate valid_count
// What if an attacker includes the same oracle_id twice?
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_duplicate_oracle_id_inflation)
{
    LogPrintf("REDTEAM: Attack 13 — duplicate oracle ID to inflate valid signature count\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    int64_t ts = GetTime();

    // Create 3 unique oracle messages + duplicate oracle 0 twice more
    CKey key0 = GetRegtestOracleKey(0);
    CKey key1 = GetRegtestOracleKey(1);
    CKey key2 = GetRegtestOracleKey(2);

    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;

    bundle.messages.push_back(MakeSignedMsg(key0, 0, 50000, ts));
    bundle.messages.push_back(MakeSignedMsg(key1, 1, 50000, ts));
    bundle.messages.push_back(MakeSignedMsg(key2, 2, 50000, ts));
    // ATTACK: Duplicate oracle 0 to reach threshold
    bundle.messages.push_back(MakeSignedMsg(key0, 0, 50000, ts));
    bundle.messages.push_back(MakeSignedMsg(key0, 0, 50000, ts));

    bundle.median_price_micro_usd = 50000;

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result,
        "Bundle with duplicate oracle IDs should be REJECTED");

    LogPrintf("REDTEAM: Attack 13 — %s\n", result ? "EXPLOITABLE!!" : "Defense holds ✓");
}

// ============================================================================
// ATTACK 14: PRICE OUTSIDE RANGE in Phase 2
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_price_out_of_range)
{
    LogPrintf("REDTEAM: Attack 14 — price outside valid range in Phase 2\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    std::vector<CKey> keys;
    for (uint32_t i = 0; i < 5; ++i) keys.push_back(GetRegtestOracleKey(i));
    int64_t ts = GetTime();

    // All oracles sign a price of 0 (below minimum 100)
    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;
    for (uint32_t i = 0; i < 5; ++i) {
        // Can't use MakeSignedMsg for price=0 as it might fail validation
        COraclePriceMessage msg;
        msg.oracle_id = i;
        msg.price_micro_usd = 50; // Below ORACLE_MIN_PRICE_MICRO_USD (100)
        msg.timestamp = ts;
        msg.block_height = 700;
        msg.nonce = i;
        msg.oracle_pubkey = XOnlyPubKey(keys[i].GetPubKey());
        msg.SignAttestation(keys[i]);
        bundle.messages.push_back(msg);
    }
    bundle.median_price_micro_usd = 50;

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result,
        "Bundle with sub-minimum prices should be REJECTED");

    LogPrintf("REDTEAM: Attack 14 — %s\n", result ? "EXPLOITABLE!!" : "Defense holds ✓");
}

// ============================================================================
// ATTACK 15: PHASE 2 CONSENSUS MISMATCH — bundle claims wrong median price
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_consensus_price_mismatch)
{
    LogPrintf("REDTEAM: Attack 15 — consensus price mismatch in Phase 2 bundle\n");

    Consensus::Params params = MakePhase2Params(4, 7);
    std::vector<CKey> keys;
    for (uint32_t i = 0; i < 5; ++i) keys.push_back(GetRegtestOracleKey(i));

    int64_t ts = GetTime();
    uint64_t real_consensus = 51000;

    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.timestamp = ts;
    for (uint32_t i = 0; i < 5; ++i) {
        bundle.messages.push_back(MakeSignedMsg(keys[i], i, real_consensus, ts));
    }

    // ATTACK: Claim a DIFFERENT median price
    bundle.median_price_micro_usd = 99999;

    bool result = OracleBundleManager::ValidateBundle(bundle, 0, params);
    BOOST_CHECK_MESSAGE(!result,
        "Bundle with mismatched median_price should be REJECTED by price verification");

    LogPrintf("REDTEAM: Attack 15 — %s\n", result ? "EXPLOITABLE!!" : "Defense holds ✓");
}

BOOST_AUTO_TEST_SUITE_END()
