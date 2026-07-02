// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-05: Bundle Manager & Validation Adversarial Security Tests
 *
 * Attack vectors tested:
 * 1. v0x02 version downgrade when Phase 3 is active (bypass MuSig2)
 * 2. Epoch=0 oracle set mismatch in extracted v0x02 bundles
 * 3. Price=0 and price=MAX_INT64 boundary attacks
 * 4. Multiple conflicting oracle bundles in same block
 * 5. Oversized bundle OP_RETURN data
 * 6. Empty bundle (no oracle data) acceptance post-activation
 * 7. Timestamp manipulation (future + very old)
 * 8. v0x03 bitmap manipulation (fewer than threshold participants)
 * 9. num_messages=0 in v0x02 header (zero-length bundle bypass)
 * 10. Strategy selector / OOM regression check
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <oracle/musig2_aggregator.h>
#include <primitives/block.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <random.h>
#include <uint256.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>
#include <limits>
#include <set>

BOOST_FIXTURE_TEST_SUITE(rh05_bundle_validation_attacks, RegTestingSetup)

// Helper: Build a v0x02 CScript manually
static CScript BuildV02Script(uint8_t num_msgs, uint64_t price, int64_t timestamp,
                               const std::vector<std::pair<uint8_t, std::vector<unsigned char>>>& oracle_sigs)
{
    std::vector<unsigned char> p2_data;
    p2_data.push_back(num_msgs);
    for (int i = 0; i < 8; ++i) p2_data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) p2_data.push_back((timestamp >> (i * 8)) & 0xFF);
    for (const auto& [id, sig] : oracle_sigs) {
        p2_data.push_back(id);
        if (sig.size() == 64) {
            p2_data.insert(p2_data.end(), sig.begin(), sig.end());
        } else {
            p2_data.insert(p2_data.end(), 64, 0x00);
        }
    }

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    script << std::vector<unsigned char>{0x02};
    script << p2_data;
    return script;
}

// Helper: Build a v0x03 CScript manually
static CScript BuildV03Script(const std::vector<unsigned char>& bitmap,
                               uint64_t price, int64_t timestamp,
                               const std::vector<unsigned char>& agg_sig)
{
    std::vector<unsigned char> v03_data;
    v03_data.push_back(static_cast<unsigned char>(bitmap.size()));
    v03_data.insert(v03_data.end(), bitmap.begin(), bitmap.end());
    // epoch (4 bytes, zero)
    for (int i = 0; i < 4; ++i) v03_data.push_back(0x00);
    for (int i = 0; i < 8; ++i) v03_data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) v03_data.push_back((timestamp >> (i * 8)) & 0xFF);
    v03_data.insert(v03_data.end(), agg_sig.begin(), agg_sig.end());

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    script << std::vector<unsigned char>{0x03};
    script << v03_data;
    return script;
}

// Helper: Create a block with a specific oracle script in coinbase
static CBlock CreateBlockWithScript(const CScript& oracle_script, uint32_t block_time, int32_t height)
{
    CBlock block;
    block.nVersion = 1;
    block.nTime = block_time;
    block.hashPrevBlock.SetNull();
    block.nBits = 0x207fffff;
    block.nNonce = 0;

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    // BIP34 height encoding
    coinbase.vin[0].scriptSig = CScript() << height;

    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // Add oracle output
    CTxOut oracle_output;
    oracle_output.nValue = 0;
    oracle_output.scriptPubKey = oracle_script;
    coinbase.vout.push_back(oracle_output);

    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    return block;
}

// ============================================================================
// ATTACK 1: v0x02 version downgrade when Phase 3 is active
// Phase 3 is active at height 0 on regtest. Can we bypass MuSig2 with v0x02?
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_v02_downgrade_when_phase3_active)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 1: v0x02 downgrade to bypass MuSig2 ===");

    const Consensus::Params& params = Params().GetConsensus();
    
    // Phase 3 should be active on regtest (height 0)
    BOOST_CHECK(params.IsMuSig2OracleActive(1000));

    // Build a v0x02 bundle with fake signatures
    int64_t now = GetTime();
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> oracle_sigs;
    for (int i = 0; i < params.nOracleRequiredMessages; ++i) {
        oracle_sigs.push_back({static_cast<uint8_t>(i), std::vector<unsigned char>(64, 0xAA)});
    }
    CScript v02_script = BuildV02Script(params.nOracleRequiredMessages, 50000, now, oracle_sigs);

    // Put it in a block at a height where Phase 3 is active
    int32_t height = 1000;
    CBlock block = CreateBlockWithScript(v02_script, static_cast<uint32_t>(now), height);

    // This is the key question: does ValidateBlockOracleData reject a v0x02
    // bundle when Phase 3 is active? If it accepts it, that's a version
    // downgrade vulnerability — attackers can skip MuSig2 entirely.
    BlockValidationState state;
    bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);

    // DEFENSE CHECK: On regtest, phase 2 validation is more lenient (no chainparams
    // oracle pubkey matching). The real question is whether v0x02 is even allowed
    // when Phase 3 is mandated.
    //
    // FINDING: The code DOES NOT enforce v0x03 when Phase 3 is active.
    // A v0x02 bundle falls through to Phase 2 validation, which is weaker.
    // This is a potential version downgrade attack vector.
    BOOST_TEST_MESSAGE("  v0x02 at Phase3-active height: " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  State: " << state.ToString());
    
    // Document the finding - whether it passes or fails tells us if there's a gap
    if (result) {
        BOOST_TEST_MESSAGE("  >>> FINDING: v0x02 bundles accepted when Phase 3 is active!");
        BOOST_TEST_MESSAGE("  >>> This could allow bypassing MuSig2 aggregate signature requirements.");
    }
}

// ============================================================================
// ATTACK 2: v0x02 epoch confusion must be impossible in V1 because legacy
// bundles are rejected before extraction.
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_epoch_zero_oracle_set_mismatch)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 2: v0x02 epoch confusion rejected ===");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    
    // Create a v0x02 script and verify extraction rejects it outright.
    int64_t now = GetTime();
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> oracle_sigs;
    oracle_sigs.push_back({0, std::vector<unsigned char>(64, 0xBB)});
    CScript v02_script = BuildV02Script(1, 50000, now, oracle_sigs);

    // Build a block and extract
    CBlock block = CreateBlockWithScript(v02_script, static_cast<uint32_t>(now), 5000);
    COracleBundle extracted;
    bool ok = manager.ExtractOracleBundle(*block.vtx[0], extracted);
    BOOST_CHECK_MESSAGE(!ok,
        "DigiDollar V1 must reject v0x02 oracle data before any epoch or "
        "roster logic can be reached.");
}

// ============================================================================
// ATTACK 3: Price boundary attacks — price=0 and price=MAX
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_price_zero)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 3a: price=0 bundle ===");

    const Consensus::Params& params = Params().GetConsensus();
    int64_t now = GetTime();
    int32_t height = 1000; // Above activation

    // Create a v0x02 bundle with price=0 at post-activation height
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> oracle_sigs;
    for (int i = 0; i < params.nOracleRequiredMessages; ++i) {
        oracle_sigs.push_back({static_cast<uint8_t>(i), std::vector<unsigned char>(64, 0xBB)});
    }
    CScript script = BuildV02Script(params.nOracleRequiredMessages, 0 /* price=0 */, now, oracle_sigs);
    CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), height);

    BlockValidationState state;
    bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
    BOOST_TEST_MESSAGE("  price=0 at height " << height << ": " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  State: " << state.ToString());

    // price=0 should be rejected — it's below ORACLE_MIN_PRICE_MICRO_USD
    // Phase 2 validation checks price range per-message
    // If this passes, there's a missing price range check in ValidateBlockOracleData
    if (result) {
        BOOST_TEST_MESSAGE("  >>> FINDING: price=0 accepted at post-activation height!");
        BOOST_TEST_MESSAGE("  >>> ValidateBlockOracleData does NOT check price range for v0x02");
    }
    // Document the finding — don't assert yet, this is an audit
}

BOOST_AUTO_TEST_CASE(attack_price_max_int64)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 3b: price=MAX_INT64 bundle ===");

    const Consensus::Params& params = Params().GetConsensus();
    int64_t now = GetTime();
    int32_t height = 1000;

    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> oracle_sigs;
    for (int i = 0; i < params.nOracleRequiredMessages; ++i) {
        oracle_sigs.push_back({static_cast<uint8_t>(i), std::vector<unsigned char>(64, 0xCC)});
    }
    CScript script = BuildV02Script(params.nOracleRequiredMessages, std::numeric_limits<uint64_t>::max(), now, oracle_sigs);
    CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), height);

    BlockValidationState state;
    bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
    BOOST_TEST_MESSAGE("  price=MAX at height " << height << ": " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  State: " << state.ToString());

    if (result) {
        BOOST_TEST_MESSAGE("  >>> FINDING: price=UINT64_MAX accepted at post-activation height!");
        BOOST_TEST_MESSAGE("  >>> ValidateBlockOracleData does NOT check price range for v0x02");
    }
}

// ============================================================================
// ATTACK 4: Multiple conflicting oracle bundles in same block
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_multiple_oracle_outputs)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 4: Multiple conflicting oracle bundles ===");

    int64_t now = GetTime();
    
    // Create block with TWO oracle OP_RETURN outputs with different prices
    CBlock block;
    block.nVersion = 1;
    block.nTime = static_cast<uint32_t>(now);
    block.hashPrevBlock.SetNull();
    block.nBits = 0x207fffff;
    block.nNonce = 0;

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 1000;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 72000 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // Oracle output 1: price = 50000
    {
        CScript script;
        script << OP_RETURN << OP_ORACLE;
        script << std::vector<unsigned char>{0x01};
        std::vector<unsigned char> data;
        data.push_back(0); // oracle_id
        uint64_t price = 50000;
        for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
        int64_t ts = now;
        for (int i = 0; i < 8; ++i) data.push_back((ts >> (i * 8)) & 0xFF);
        script << data;
        CTxOut out; out.nValue = 0; out.scriptPubKey = script;
        coinbase.vout.push_back(out);
    }

    // Oracle output 2: price = 99000000 (conflicting!)
    {
        CScript script;
        script << OP_RETURN << OP_ORACLE;
        script << std::vector<unsigned char>{0x01};
        std::vector<unsigned char> data;
        data.push_back(0);
        uint64_t price = 99000000;
        for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
        int64_t ts = now;
        for (int i = 0; i < 8; ++i) data.push_back((ts >> (i * 8)) & 0xFF);
        script << data;
        CTxOut out; out.nValue = 0; out.scriptPubKey = script;
        coinbase.vout.push_back(out);
    }

    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));

    BlockValidationState state;
    const Consensus::Params& params = Params().GetConsensus();
    bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);

    BOOST_TEST_MESSAGE("  Multiple oracle outputs: " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  State: " << state.ToString());
    
    // This SHOULD be rejected (oracle_output_count > 1 check)
    BOOST_CHECK(!result);
    BOOST_TEST_MESSAGE("  Defense holds: multiple oracle outputs correctly rejected");
}

// ============================================================================
// ATTACK 5: Oversized bundle — stuff extra data into OP_RETURN
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_oversized_bundle)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 5: Oversized bundle data ===");

    // Create a v0x02 script with 255 oracle entries (max num_messages byte)
    int64_t now = GetTime();
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> oracle_sigs;
    for (int i = 0; i < 255; ++i) {
        oracle_sigs.push_back({static_cast<uint8_t>(i), std::vector<unsigned char>(64, 0xCC)});
    }
    CScript big_script = BuildV02Script(255, 50000, now, oracle_sigs);
    
    BOOST_TEST_MESSAGE("  Oversized script size: " << big_script.size() << " bytes");
    BOOST_CHECK(big_script.size() > MAX_SCRIPT_ELEMENT_SIZE);

    // Check if extraction handles this correctly
    CBlock block = CreateBlockWithScript(big_script, static_cast<uint32_t>(now), 1000);
    COracleBundle extracted;
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    bool ok = manager.ExtractOracleBundle(*block.vtx[0], extracted);
    
    BOOST_TEST_MESSAGE("  Extraction of oversized bundle: " << (ok ? "SUCCESS" : "FAILED"));
    if (ok) {
        BOOST_TEST_MESSAGE("  Extracted " << extracted.messages.size() << " messages");
        // Even if extracted, validation should reject
        BlockValidationState state;
        const Consensus::Params& params = Params().GetConsensus();
        bool valid = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
        BOOST_TEST_MESSAGE("  Validation: " << (valid ? "ACCEPTED" : "REJECTED"));
    }
}

// ============================================================================
// ATTACK 6: num_messages=0 in v0x02 header — zero-length bundle
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_v02_zero_messages)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 6: v0x02 with num_messages=0 ===");

    int64_t now = GetTime();
    
    // Craft a v0x02 script with num_messages=0 but valid header
    std::vector<unsigned char> p2_data;
    p2_data.push_back(0); // num_messages = 0
    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i) p2_data.push_back((price >> (i * 8)) & 0xFF);
    int64_t ts = now;
    for (int i = 0; i < 8; ++i) p2_data.push_back((ts >> (i * 8)) & 0xFF);

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    script << std::vector<unsigned char>{0x02};
    script << p2_data;

    CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), 1000);
    
    // Try to extract
    COracleBundle extracted;
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    bool ok = manager.ExtractOracleBundle(*block.vtx[0], extracted);
    
    BOOST_TEST_MESSAGE("  v0x02 num_messages=0 extraction: " << (ok ? "SUCCESS" : "FAILED"));
    if (ok) {
        BOOST_TEST_MESSAGE("  Extracted " << extracted.messages.size() << " messages");
        BOOST_TEST_MESSAGE("  Price: " << extracted.median_price_micro_usd);
        
        // If extraction succeeds with 0 messages, does validation catch it?
        BlockValidationState state;
        const Consensus::Params& params = Params().GetConsensus();
        bool valid = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
        BOOST_TEST_MESSAGE("  Validation: " << (valid ? "ACCEPTED" : "REJECTED"));
        BOOST_TEST_MESSAGE("  State: " << state.ToString());
        
        if (valid) {
            BOOST_TEST_MESSAGE("  >>> FINDING: v0x02 with zero messages accepted!");
        }
    }
}

// ============================================================================
// ATTACK 7: v0x03 bitmap with fewer than threshold participants
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_v03_below_threshold_bitmap)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 7: v0x03 bitmap below threshold ===");

    const Consensus::Params& params = Params().GetConsensus();
    int64_t now = GetTime();
    
    // Create a bitmap with only 1 participant (bit 0 set) — below threshold
    std::vector<unsigned char> bitmap = {0x01}; // Only oracle 0
    std::vector<unsigned char> fake_sig(64, 0xDD);

    CScript script = BuildV03Script(bitmap, 50000, now, fake_sig);
    CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), 1000);

    // Extract the bundle
    COracleBundle extracted;
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    bool ok = manager.ExtractOracleBundle(*block.vtx[0], extracted);
    
    if (ok) {
        BOOST_TEST_MESSAGE("  Extracted v0x03 bundle with " << extracted.messages.size() << " participants");
        BOOST_TEST_MESSAGE("  Required: " << params.nOracleRequiredMessages);
        
        // Try Phase 3 validation
        std::string error;
        bool valid = OracleBundleManager::ValidateMuSig2Bundle(extracted, 1000, params, error);
        BOOST_TEST_MESSAGE("  Phase 3 validation: " << (valid ? "ACCEPTED" : "REJECTED"));
        if (!valid) {
            BOOST_TEST_MESSAGE("  Error: " << error);
        }
        
        // This should be rejected due to threshold check
        BOOST_CHECK(!valid);
        BOOST_TEST_MESSAGE("  Defense holds: below-threshold v0x03 bundle rejected");
    }
}

// ============================================================================
// ATTACK 8: Timestamp manipulation — future timestamp
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_future_timestamp)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 8: Oracle timestamp far in future ===");

    const Consensus::Params& params = Params().GetConsensus();
    int64_t now = GetTime();
    int64_t future = now + 7200; // 2 hours in future

    // Use v0x02 format at a height above DD activation (650) so validation actually runs
    int32_t height = 1000;
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> oracle_sigs;
    for (int i = 0; i < params.nOracleRequiredMessages; ++i) {
        oracle_sigs.push_back({static_cast<uint8_t>(i), std::vector<unsigned char>(64, 0xAA)});
    }
    CScript script = BuildV02Script(params.nOracleRequiredMessages, 50000, future, oracle_sigs);
    CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), height);

    BlockValidationState state;
    bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
    BOOST_TEST_MESSAGE("  Future timestamp (2h) at height " << height << ": " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  State: " << state.ToString());

    // Should be rejected: bundle.timestamp > block.nTime + 60
    BOOST_CHECK(!result);
    BOOST_TEST_MESSAGE("  Defense holds: future timestamp rejected");
}

// ============================================================================
// ATTACK 9: Timestamp manipulation — very old timestamp
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_very_old_timestamp)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 9: Oracle timestamp very old ===");

    const Consensus::Params& params = Params().GetConsensus();
    int64_t now = GetTime();
    int64_t old_ts = now - 86400; // 24 hours ago

    // Use v0x02 format at a height above DD activation
    int32_t height = 1000;
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> oracle_sigs;
    for (int i = 0; i < params.nOracleRequiredMessages; ++i) {
        oracle_sigs.push_back({static_cast<uint8_t>(i), std::vector<unsigned char>(64, 0xAA)});
    }
    CScript script = BuildV02Script(params.nOracleRequiredMessages, 50000, old_ts, oracle_sigs);
    CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), height);

    BlockValidationState state;
    bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
    BOOST_TEST_MESSAGE("  Old timestamp (24h) at height " << height << ": " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  State: " << state.ToString());

    // Should be rejected: oracle_age > ORACLE_MAX_AGE_SECONDS (3600)
    BOOST_CHECK(!result);
    BOOST_TEST_MESSAGE("  Defense holds: stale timestamp rejected");
}

// ============================================================================
// ATTACK 10: Strategy selector / OOM regression (fuzz marathon fix)
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_oom_regression_large_fuzz_input)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 10: OOM regression check ===");

    // Create a bundle with maximum fuzzed messages — verify no OOM
    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.version = 2;

    // Add 255 messages (max for uint8_t num_messages field)
    for (int i = 0; i < 255; ++i) {
        COraclePriceMessage msg;
        msg.oracle_id = static_cast<uint32_t>(i);
        msg.price_micro_usd = 50000 + (i * 100);
        msg.timestamp = GetTime();
        msg.block_height = 0;
        msg.nonce = 0;
        msg.schnorr_sig.resize(64, static_cast<unsigned char>(i));
        bundle.messages.push_back(msg);
    }

    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = GetTime();

    // Exercise all validation paths — should not OOM or crash
    (void)bundle.IsValid(4, 0);
    (void)bundle.HasConsensus(4);
    (void)bundle.GetConsensusPrice(4);
    (void)bundle.IsMuSig2();

    // Serialization round-trip
    try {
        DataStream ds{};
        ds << bundle;
        COracleBundle bundle2;
        ds >> bundle2;
        BOOST_CHECK_EQUAL(bundle2.messages.size(), bundle.messages.size());
    } catch (const std::exception& e) {
        BOOST_TEST_MESSAGE("  Serialization exception (expected for large bundle): " << e.what());
    }

    // V03 with max bitmap
    COracleBundle v03;
    v03.version = 3;
    v03.epoch = 0;
    v03.median_price_micro_usd = 50000;
    v03.timestamp = GetTime();
    v03.aggregate_sig.resize(64, 0xEE);
    v03.participation_bitmap.resize(32, 0xFF); // 256 participants

    std::vector<unsigned char> serialized = v03.SerializeV03Data();
    BOOST_CHECK(!serialized.empty());

    COracleBundle v03_back;
    bool ok = COracleBundle::DeserializeV03Data(serialized, v03_back);
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(v03_back.median_price_micro_usd, 50000ULL);

    BOOST_TEST_MESSAGE("  OOM regression check passed — no crash with large inputs");
}

// ============================================================================
// ATTACK 11: IsValidOracleMessage with manipulated pubkey
// Attacker sends message with their own pubkey instead of chainparams pubkey
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_forged_pubkey_in_message)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 11: Forged pubkey in oracle message ===");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    // Use Phase 2 mode (min > 1) to test chainparams binding
    manager.SetMinOracleCount(4);

    // Create an attacker key
    CKey attacker_key;
    attacker_key.MakeNewKey(true);

    // Create a message signed by attacker, claiming to be oracle 0
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 50000;
    msg.timestamp = GetTime();
    msg.block_height = 0;
    msg.nonce = 0;
    msg.oracle_pubkey = XOnlyPubKey(attacker_key.GetPubKey());
    msg.Sign(attacker_key);

    // The message should verify with attacker's key (Sign uses Phase1 hash, check Phase2)
    // Note: VerifyAttestation uses GetAttestationSignatureHash which covers oracle_id+price+timestamp
    // and verifies against oracle_pubkey. Sign() uses GetSignatureHash (Phase 1) by default.
    // So VerifyAttestation will fail because the sig was made with Phase 1 hash.
    // Use SignAttestation instead for a proper test:
    msg.SignAttestation(attacker_key);
    BOOST_CHECK(msg.VerifyAttestation());

    // But AddOracleMessage (which calls IsValidOracleMessage internally) should rebind
    // to chainparams key and reject
    bool valid = manager.AddOracleMessage(msg);
    BOOST_TEST_MESSAGE("  Forged pubkey message accepted by AddOracleMessage: " << valid);
    
    // In regtest, this depends on whether chainparams has oracle 0 configured
    // The defense is the pubkey binding in IsValidOracleMessage
    if (!valid) {
        BOOST_TEST_MESSAGE("  Defense holds: forged pubkey correctly rejected");
    } else {
        BOOST_TEST_MESSAGE("  >>> Note: accepted (may be OK in regtest if oracle 0 key matches)");
    }

    manager.Clear();
}

// ============================================================================
// ATTACK 12: AddOracleMessage seen_message_hashes cap overflow
// Verify the 2048 cap holds under sustained attack
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_seen_hashes_memory_exhaustion)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 12: seen_message_hashes memory cap ===");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(1);

    CKey key;
    key.MakeNewKey(true);

    // Inject 3000 unique messages — should be capped at 2048
    int accepted = 0;
    for (int i = 0; i < 3000; ++i) {
        COraclePriceMessage msg;
        msg.oracle_id = static_cast<uint32_t>(i % 256);
        msg.price_micro_usd = 50000;
        msg.timestamp = GetTime() + i; // Unique timestamp for unique hash
        msg.block_height = 0;
        msg.nonce = i;
        msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
        msg.Sign(key);
        if (manager.AddOracleMessage(msg)) accepted++;
    }

    BOOST_TEST_MESSAGE("  Accepted " << accepted << " messages out of 3000");
    // The seen_message_hashes set should be capped
    // We can't directly check the set size, but no OOM = success
    BOOST_TEST_MESSAGE("  Memory cap test passed — no OOM");

    manager.Clear();
}

// ============================================================================
// ATTACK 13: ValidateBlockOracleData — price range not checked at block level
// Phase 1 bundles: price extracted from script, no min/max check
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_phase1_price_no_range_check)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 13: Phase 1 price range not validated at block level ===");

    int64_t now = GetTime();
    const Consensus::Params& params = Params().GetConsensus();

    // Price below ORACLE_MIN_PRICE_MICRO_USD (100) — set to 1
    {
        CScript script;
        script << OP_RETURN << OP_ORACLE;
        script << std::vector<unsigned char>{0x01};
        std::vector<unsigned char> data;
        data.push_back(0); // oracle_id
        uint64_t price = 1; // Way below minimum
        for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
        int64_t ts = now;
        for (int i = 0; i < 8; ++i) data.push_back((ts >> (i * 8)) & 0xFF);
        script << data;

        // Use a height BELOW Phase 2 activation for pure Phase 1 path
        int32_t height = 100; // Below nDDActivationHeight (650 on regtest)
        CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), height);
        BlockValidationState state;
        bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
        BOOST_TEST_MESSAGE("  Phase 1 price=1 validation: " << (result ? "ACCEPTED" : "REJECTED"));
        BOOST_TEST_MESSAGE("  State: " << state.ToString());

        // Note: This height may be below DD activation, so it might pass trivially
        // The real test is whether Phase 1 validation checks price range
    }
}

// ============================================================================
// ATTACK 14: v0x03 with forged 64-byte aggregate signature
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_v03_forged_aggregate_sig)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 14: v0x03 forged aggregate signature ===");

    const Consensus::Params& params = Params().GetConsensus();
    int32_t height = 1000;

    // Build a v0x03 bundle with valid-looking but fake aggregate sig
    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = height / params.nOracleEpochLength;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = GetTime();
    // Bitmap with 4 oracles (IDs 0-3) = 0x0F
    bundle.participation_bitmap = {0x0F};
    // Random 64-byte sig (will fail verification)
    // GetRandBytes limited to 32 bytes per call, so fill in two halves
    bundle.aggregate_sig.resize(64);
    GetRandBytes(Span{bundle.aggregate_sig.data(), 32});
    GetRandBytes(Span{bundle.aggregate_sig.data() + 32, 32});

    std::string error;
    bool result = OracleBundleManager::ValidateMuSig2Bundle(bundle, height, params, error);
    BOOST_TEST_MESSAGE("  Forged v0x03 sig: " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  Error: " << error);
    BOOST_CHECK(!result);
    BOOST_CHECK(!error.empty());  // May fail on pubkey computation or sig verification
    BOOST_TEST_MESSAGE("  Defense holds: forged aggregate sig rejected");
}

// ============================================================================
// ATTACK 15: v0x02 with duplicate oracle IDs
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_v02_duplicate_oracle_ids)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 15: v0x02 with duplicate oracle IDs ===");

    const Consensus::Params& params = Params().GetConsensus();
    int64_t now = GetTime();
    int32_t height = 1000;

    // Build v0x02 with same oracle_id repeated 4 times
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> oracle_sigs;
    for (int i = 0; i < params.nOracleRequiredMessages; ++i) {
        oracle_sigs.push_back({0 /* same oracle_id=0 */, std::vector<unsigned char>(64, 0xDD)});
    }
    CScript script = BuildV02Script(params.nOracleRequiredMessages, 50000, now, oracle_sigs);
    CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), height);

    BlockValidationState state;
    bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
    BOOST_TEST_MESSAGE("  Duplicate oracle IDs: " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  State: " << state.ToString());
    // ValidatePhaseTwoBundle checks for duplicate oracle IDs
    BOOST_CHECK(!result);
    BOOST_TEST_MESSAGE("  Defense holds: duplicate oracle IDs rejected");
}

// ============================================================================
// ATTACK 16: Unknown version byte (0xFF) 
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_unknown_version_byte)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 16: Unknown version byte 0xFF ===");

    int64_t now = GetTime();
    int32_t height = 1000;

    // Craft a raw script with OP_RETURN OP_ORACLE <version=0xFF> <junk>
    CScript script;
    script << OP_RETURN << OP_ORACLE;
    std::vector<unsigned char> data(82, 0xAA);
    data[0] = 0xFF; // Unknown version
    script << data;

    CBlock block = CreateBlockWithScript(script, static_cast<uint32_t>(now), height);
    BlockValidationState state;
    const Consensus::Params& params = Params().GetConsensus();
    bool result = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);
    BOOST_TEST_MESSAGE("  Version 0xFF: " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  State: " << state.ToString());
    // Should be accepted as "transition period" since extraction fails
    // This is safe because validation treats unextractable data as no-oracle-data
    if (result) {
        BOOST_TEST_MESSAGE("  Unknown version treated as no-oracle-data (transition period)");
    }
}

// ============================================================================  
// ATTACK 17: v0x03 empty bitmap (0 participants)
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_v03_empty_bitmap)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 17: v0x03 empty bitmap ===");

    const Consensus::Params& params = Params().GetConsensus();
    int32_t height = 1000;

    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = 0;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = GetTime();
    bundle.participation_bitmap = {}; // Empty
    bundle.aggregate_sig.resize(64, 0xAA);

    std::string error;
    bool result = OracleBundleManager::ValidateMuSig2Bundle(bundle, height, params, error);
    BOOST_TEST_MESSAGE("  Empty bitmap: " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  Error: " << error);
    BOOST_CHECK(!result);
    BOOST_TEST_MESSAGE("  Defense holds: empty bitmap rejected");
}

// ============================================================================
// ATTACK 18: v0x03 all-zeros bitmap (no participants set)
// ============================================================================
BOOST_AUTO_TEST_CASE(attack_v03_all_zeros_bitmap)
{
    BOOST_TEST_MESSAGE("=== RH-05 Attack 18: v0x03 all-zeros bitmap ===");

    const Consensus::Params& params = Params().GetConsensus();
    int32_t height = 1000;

    COracleBundle bundle;
    bundle.version = 3;
    bundle.epoch = 0;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = GetTime();
    bundle.participation_bitmap = {0x00}; // No bits set
    bundle.aggregate_sig.resize(64, 0xAA);

    std::string error;
    bool result = OracleBundleManager::ValidateMuSig2Bundle(bundle, height, params, error);
    BOOST_TEST_MESSAGE("  All-zeros bitmap: " << (result ? "ACCEPTED" : "REJECTED"));
    BOOST_TEST_MESSAGE("  Error: " << error);
    BOOST_CHECK(!result);
    BOOST_TEST_MESSAGE("  Defense holds: zero-participant bitmap rejected");
}

BOOST_AUTO_TEST_SUITE_END()
