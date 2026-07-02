// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-29: Coinbase Oracle Data Manipulation Tests
 *
 * Red-team attack vectors targeting how oracle data is embedded in and
 * extracted from coinbase transactions.
 *
 * Attack surface:
 * 1. Coinbase OP_RETURN injection (extra data alongside oracle)
 * 2. Multiple oracle OP_RETURNs in coinbase
 * 3. Oracle data size manipulation (min/max for V01/V02/V03)
 * 4. Version confusion (V03 claiming V02, mixed versions in chain)
 * 5. Oracle signature replay from old blocks
 * 6. Oracle data activation timing (immediate vs coinbase maturity)
 * 7. Miner withholding oracle data
 * 8. Oracle data in non-coinbase transactions
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <primitives/block.h>
#include <primitives/oracle.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <validation.h>

namespace {

// Helper: Create a signed V01 oracle script
CScript MakeV01Script(uint8_t oracle_id, uint64_t price, int64_t timestamp)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE;
    std::vector<unsigned char> data;
    data.push_back(0x01); // version
    data.push_back(oracle_id);
    for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) data.push_back((static_cast<uint64_t>(timestamp) >> (i * 8)) & 0xFF);
    script << data;
    return script;
}

// Helper: Create a V02 oracle script with signed attestations
CScript MakeV02Script(uint8_t num_msgs, uint64_t price, int64_t timestamp,
                      const std::vector<std::pair<uint8_t, std::vector<unsigned char>>>& oracle_sigs)
{
    CScript script;
    script << OP_RETURN << OP_ORACLE;

    std::vector<unsigned char> data;
    data.push_back(0x02); // version
    data.push_back(num_msgs);
    for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) data.push_back((static_cast<uint64_t>(timestamp) >> (i * 8)) & 0xFF);

    for (const auto& [oid, sig] : oracle_sigs) {
        data.push_back(oid);
        data.insert(data.end(), sig.begin(), sig.end());
    }
    script << data;
    return script;
}

// Helper: Build a coinbase tx with a given oracle script at vout[1]
CMutableTransaction MakeCoinbaseTx(const CScript& oracle_script, int32_t height)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    // BIP34: encode height in scriptSig
    coinbase.vin[0].scriptSig = CScript() << height;
    // vout[0]: miner reward
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    // vout[1]: oracle data
    coinbase.vout.push_back(CTxOut(0, oracle_script));
    return coinbase;
}

// Helper: Build a CBlock with a coinbase containing oracle script
CBlock MakeBlock(const CScript& oracle_script, uint32_t nTime, int32_t height)
{
    CBlock block;
    block.nTime = nTime;
    block.nVersion = 0x20000000;
    CMutableTransaction coinbase = MakeCoinbaseTx(oracle_script, height);
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

// Helper: Create a real signed Phase2 oracle message
COraclePriceMessage MakeSignedMessage(const CKey& key, uint8_t oracle_id,
                                       uint64_t price, int64_t timestamp)
{
    COraclePriceMessage msg;
    msg.oracle_id = oracle_id;
    msg.price_micro_usd = price;
    msg.timestamp = timestamp;
    msg.block_height = 0;
    msg.nonce = 0;
    msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
    msg.SignAttestation(key);
    return msg;
}

// Helper: Build a V02 script with real Schnorr signatures
CScript MakeV02ScriptSigned(uint64_t price, int64_t timestamp,
                             const std::vector<std::pair<uint8_t, CKey>>& oracle_keys)
{
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> sigs;
    for (const auto& [oid, key] : oracle_keys) {
        COraclePriceMessage msg = MakeSignedMessage(key, oid, price, timestamp);
        sigs.push_back({oid, msg.schnorr_sig});
    }
    return MakeV02Script(static_cast<uint8_t>(oracle_keys.size()), price, timestamp, sigs);
}

CScript MakeV03Script(uint64_t price, int64_t timestamp, int32_t epoch = 1)
{
    COracleBundle bundle(epoch);
    bundle.version = 3;
    bundle.participation_bitmap = {0x0f};
    bundle.median_price_micro_usd = price;
    bundle.timestamp = timestamp;
    bundle.aggregate_sig.assign(64, 0x42);
    return OracleBundleManager::GetInstance().CreateOracleScript(bundle);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(rh29_coinbase_oracle_manipulation, RegTestingSetup)

// ============================================================================
// ATTACK 1: Coinbase OP_RETURN injection — extra data alongside oracle
// ============================================================================

BOOST_AUTO_TEST_CASE(attack1_extra_op_return_before_oracle)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 1a: Extra OP_RETURN output before oracle data ===");

    // A miner adds an extra OP_RETURN output (e.g., pool tag) before oracle output
    int64_t now = GetTime();
    CScript oracle_script = MakeV03Script(50000, now);

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 101;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    // Extra OP_RETURN (pool tag)
    CScript pool_tag;
    pool_tag << OP_RETURN;
    pool_tag << std::vector<unsigned char>{'P', 'O', 'O', 'L'};
    coinbase.vout.push_back(CTxOut(0, pool_tag));
    // Oracle OP_RETURN
    coinbase.vout.push_back(CTxOut(0, oracle_script));

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(coinbase), bundle);

    // ExtractOracleBundle scans all outputs for the single v0x03 OP_ORACLE
    // marker, so the non-oracle OP_RETURN should be ignored.
    BOOST_CHECK_MESSAGE(extracted, "MuSig2 oracle data should be extractable even with extra OP_RETURNs");
    if (extracted) {
        BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 50000ULL);
    }

    BOOST_TEST_MESSAGE("  Extra OP_RETURNs before oracle: extraction works (oracle scans all outputs)");
}

BOOST_AUTO_TEST_CASE(attack1_extra_data_appended_to_oracle_script)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 1b: Extra data appended to oracle OP_RETURN script ===");

    // Attacker appends extra bytes after the oracle data within the same OP_RETURN
    int64_t now = GetTime();
    CScript script;
    script << OP_RETURN << OP_ORACLE;

    std::vector<unsigned char> data;
    data.push_back(0x01); // version
    data.push_back(0);    // oracle_id
    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) data.push_back((static_cast<uint64_t>(now) >> (i * 8)) & 0xFF);
    script << data;

    // Append extra push of arbitrary data
    std::vector<unsigned char> extra_payload(32, 0xDE);
    script << extra_payload;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    BOOST_TEST_MESSAGE("  Extracted: " << extracted);
    BOOST_CHECK_MESSAGE(!extracted,
        "V01 extraction must reject trailing data so the same oracle payload has one canonical serialization");
}

BOOST_AUTO_TEST_CASE(attack1_extra_data_appended_v02)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 1c: Extra data appended to V02 oracle script ===");

    int64_t now = GetTime();
    // Build V02 with 1 oracle (even though consensus may need more, testing parsing)
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> sigs;
    sigs.push_back({0, std::vector<unsigned char>(64, 0xAA)});

    // Build script manually to append extra data
    CScript script;
    script << OP_RETURN << OP_ORACLE;

    std::vector<unsigned char> data;
    data.push_back(0x02); // version
    data.push_back(1);    // num_msgs
    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) data.push_back((static_cast<uint64_t>(now) >> (i * 8)) & 0xFF);
    data.push_back(0);   // oracle_id
    data.insert(data.end(), 64, 0xAA); // fake sig
    // Append 32 bytes of garbage
    data.insert(data.end(), 32, 0xFF);

    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    BOOST_TEST_MESSAGE("  V02 extraction with trailing data: " << extracted);
    BOOST_CHECK_MESSAGE(!extracted,
        "V02 extraction must reject trailing data beyond expected_size");
}

BOOST_AUTO_TEST_CASE(attack1_extra_data_appended_v03)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 1d: Extra data appended to V03 oracle script ===");

    // V03 uses DeserializeV03Data which enforces exact size
    // Build a valid V03 payload and append extra bytes
    COracleBundle fake_bundle;
    fake_bundle.version = 3;
    fake_bundle.participation_bitmap = {0xFF}; // 8 oracles
    fake_bundle.median_price_micro_usd = 50000;
    fake_bundle.timestamp = GetTime();
    fake_bundle.aggregate_sig.resize(64, 0xCC);

    std::vector<unsigned char> v03_data = fake_bundle.SerializeV03Data();
    BOOST_REQUIRE(!v03_data.empty());

    // Prepend version byte and append garbage
    std::vector<unsigned char> data;
    data.push_back(0x03);
    data.insert(data.end(), v03_data.begin(), v03_data.end());
    data.insert(data.end(), 16, 0xFF); // extra garbage

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    // V03 DeserializeV03Data enforces exact size match — should reject
    BOOST_TEST_MESSAGE("  V03 extraction with trailing data: " << extracted);
    if (!extracted) {
        BOOST_TEST_MESSAGE("  V03 correctly rejects trailing data (exact size enforcement)");
    } else {
        BOOST_TEST_MESSAGE("  >>> BUG: V03 accepts trailing data — malleability!");
    }
}

// ============================================================================
// ATTACK 2: Multiple oracle OP_RETURNs in coinbase
// ============================================================================

BOOST_AUTO_TEST_CASE(attack2_two_oracle_op_returns)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 2a: Two OP_ORACLE outputs in coinbase ===");

    int64_t now = GetTime();
    CScript oracle1 = MakeV01Script(0, 50000, now);
    CScript oracle2 = MakeV01Script(1, 99999, now); // different price!

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 101;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    coinbase.vout.push_back(CTxOut(0, oracle1));
    coinbase.vout.push_back(CTxOut(0, oracle2));

    CBlock block;
    block.nTime = static_cast<uint32_t>(now);
    block.nVersion = 0x20000000;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.hashMerkleRoot = BlockMerkleRoot(block);

    // ValidateBlockOracleData should detect and reject multiple oracle outputs
    BlockValidationState state;
    const Consensus::Params& params = Params().GetConsensus();
    bool valid = OracleDataValidator::ValidateBlockOracleData(block, nullptr, params, state);

    BOOST_TEST_MESSAGE("  Validation result: " << valid);
    BOOST_TEST_MESSAGE("  State: " << state.ToString());

    // ExtractOracleBundle should also reject ambiguous oracle outputs rather than
    // returning the first match and silently ignoring the second.
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(*block.vtx[0], bundle);
    BOOST_CHECK_MESSAGE(!extracted,
        "ExtractOracleBundle must reject transactions with multiple OP_ORACLE outputs");

    // The block validation should reject this
    if (!valid) {
        BOOST_TEST_MESSAGE("  Correctly rejected block with multiple oracle outputs");
    } else {
        BOOST_TEST_MESSAGE("  >>> BUG: Block with multiple oracle outputs accepted");
    }
}

BOOST_AUTO_TEST_CASE(attack2_oracle_plus_non_oracle_op_return)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 2b: One OP_ORACLE + one plain OP_RETURN ===");

    // This is a legitimate scenario (witness commitment + oracle)
    int64_t now = GetTime();
    CScript oracle = MakeV03Script(50000, now);

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 101;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // Witness commitment (plain OP_RETURN without OP_ORACLE)
    CScript witness_commitment;
    witness_commitment << OP_RETURN;
    witness_commitment << std::vector<unsigned char>(36, 0xAA);
    coinbase.vout.push_back(CTxOut(0, witness_commitment));

    // Oracle output
    coinbase.vout.push_back(CTxOut(0, oracle));

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(coinbase), bundle);

    BOOST_CHECK_MESSAGE(extracted, "Should extract MuSig2 oracle data from coinbase with witness commitment");
    if (extracted) {
        BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 50000ULL);
        BOOST_TEST_MESSAGE("  Correctly found oracle output alongside witness commitment");
    }
}

// ============================================================================
// ATTACK 3: Oracle data size manipulation
// ============================================================================

BOOST_AUTO_TEST_CASE(attack3_v01_undersized)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 3a: V01 undersized data ===");

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    // V01 needs 18 bytes (1 version + 1 oracle_id + 8 price + 8 timestamp)
    // Send only 10 bytes
    std::vector<unsigned char> data(10, 0x01);
    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);
    BOOST_CHECK_MESSAGE(!extracted, "V01 undersized data should be rejected");
}

BOOST_AUTO_TEST_CASE(attack3_v02_num_messages_overflow)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 3b: V02 num_messages=255 (overflow attempt) ===");

    CScript script;
    script << OP_RETURN << OP_ORACLE;

    std::vector<unsigned char> data;
    data.push_back(0x02); // version
    data.push_back(255);  // num_messages = 255 (way more than ORACLE_ACTIVE_COUNT=17, RC30)
    uint64_t price = 50000;
    int64_t ts = GetTime();
    for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) data.push_back((static_cast<uint64_t>(ts) >> (i * 8)) & 0xFF);
    // Only provide 1 oracle entry (not 255)
    data.push_back(0);
    data.insert(data.end(), 64, 0xBB);

    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    if (!extracted) {
        BOOST_TEST_MESSAGE("  V02 correctly rejects num_messages=255 (exceeds ORACLE_ACTIVE_COUNT or size check)");
    } else {
        BOOST_TEST_MESSAGE("  >>> BUG: V02 extracted with num_messages=255");
    }
}

BOOST_AUTO_TEST_CASE(attack3_v02_zero_messages)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 3c: V02 num_messages=0 (ghost bundle) ===");

    CScript script;
    script << OP_RETURN << OP_ORACLE;

    std::vector<unsigned char> data;
    data.push_back(0x02); // version
    data.push_back(0);    // num_messages = 0
    uint64_t price = 50000;
    int64_t ts = GetTime();
    for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) data.push_back((static_cast<uint64_t>(ts) >> (i * 8)) & 0xFF);
    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    BOOST_CHECK_MESSAGE(!extracted, "V02 with num_messages=0 should be rejected (ghost bundle)");
}

BOOST_AUTO_TEST_CASE(attack3_v03_bitmap_len_zero)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 3d: V03 bitmap_len=0 ===");

    CScript script;
    script << OP_RETURN << OP_ORACLE;

    std::vector<unsigned char> data;
    data.push_back(0x03); // version
    data.push_back(0);    // bitmap_len = 0
    // price + timestamp + sig
    for (int i = 0; i < 8 + 8 + 64; ++i) data.push_back(0);
    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    BOOST_CHECK_MESSAGE(!extracted, "V03 with bitmap_len=0 should be rejected");
}

BOOST_AUTO_TEST_CASE(attack3_v03_bitmap_len_mismatch)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 3e: V03 bitmap_len claims 5 but only 1 byte follows ===");

    CScript script;
    script << OP_RETURN << OP_ORACLE;

    std::vector<unsigned char> data;
    data.push_back(0x03); // version
    data.push_back(5);    // bitmap_len = 5
    data.push_back(0xFF); // only 1 bitmap byte
    // price + timestamp + sig = 80 bytes
    for (int i = 0; i < 80; ++i) data.push_back(0);
    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    // DeserializeV03Data checks exact size: 1 + bitmap_len + 4 + 8 + 8 + 64
    // Expected: 1 + 5 + 4 + 8 + 8 + 64 = 90; actual data after version: 1 + 1 + 80 = 82
    BOOST_CHECK_MESSAGE(!extracted, "V03 with bitmap_len mismatch should be rejected");
}

// ============================================================================
// ATTACK 4: Version confusion attacks
// ============================================================================

BOOST_AUTO_TEST_CASE(attack4_version_byte_0x00)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 4a: Unknown version byte 0x00 ===");

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    std::vector<unsigned char> data(20, 0x00); // version=0 + garbage
    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);
    BOOST_CHECK_MESSAGE(!extracted, "Version 0x00 should be rejected");
}

BOOST_AUTO_TEST_CASE(attack4_version_byte_0x04)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 4b: Future version byte 0x04 ===");

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    std::vector<unsigned char> data(100, 0x04); // version=4 + garbage
    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);
    BOOST_CHECK_MESSAGE(!extracted, "Future version 0x04 should be rejected");
}

BOOST_AUTO_TEST_CASE(attack4_v03_data_with_v02_version_byte)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 4c: V03-sized data but version byte says 0x02 ===");

    // Build V03-style payload but set version to 0x02
    // This could confuse parsers that branch on version byte
    CScript script;
    script << OP_RETURN << OP_ORACLE;

    std::vector<unsigned char> data;
    data.push_back(0x02); // LIES — actually V03 format data
    // Construct something that looks like V02 header but has V03 semantics
    data.push_back(2);    // V02 interprets this as num_messages=2
    uint64_t price = 50000;
    int64_t ts = GetTime();
    for (int i = 0; i < 8; ++i) data.push_back((price >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) data.push_back((static_cast<uint64_t>(ts) >> (i * 8)) & 0xFF);
    // V02 expects 2 * 65 = 130 more bytes (oracle_id + sig per message)
    // If we provide exactly that, the V02 parser will accept it with garbage sigs
    for (int m = 0; m < 2; ++m) {
        data.push_back(static_cast<uint8_t>(m)); // oracle_id
        data.insert(data.end(), 64, 0xCC);       // fake sig
    }
    script << data;

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    if (extracted) {
        BOOST_CHECK_EQUAL(bundle.version, 2);
        BOOST_TEST_MESSAGE("  V02 parser accepted data (version=2). Sigs are fake but parse succeeded.");
        BOOST_TEST_MESSAGE("  Signature validation happens in ValidatePhaseTwoBundle, not extraction.");
        BOOST_TEST_MESSAGE("  This is OK — extraction is separate from validation.");
    }
}

BOOST_AUTO_TEST_CASE(attack4_v01_in_phase2_block)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 4d: V01 bundle in block that should be Phase 2 ===");

    // After Phase 2 activation, V01 bundles should be rejected
    // Check if CheckPhase3OracleBundleVersion in validation.cpp catches this
    int64_t now = GetTime();
    CScript v01_script = MakeV01Script(0, 50000, now);
    CBlock block = MakeBlock(v01_script, static_cast<uint32_t>(now), 101);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(*block.vtx[0], bundle);

    if (extracted) {
        BOOST_CHECK_EQUAL(bundle.version, 1); // V01 extraction sets version = 1 (Phase One)
        BOOST_TEST_MESSAGE("  V01 bundle extracted. Version field: " << bundle.version);

        // In ValidateBlockOracleData, Phase 1 requires exactly 1 message
        // Phase 2 requires ValidatePhaseTwoBundle which needs signatures
        // But the version-gating check in CheckPhase3OracleBundleVersion
        // only rejects version != 2 && != 3
        BlockValidationState state;
        bool valid = OracleDataValidator::ValidateBlockOracleData(block, nullptr, Params().GetConsensus(), state);
        BOOST_TEST_MESSAGE("  Block validation: " << valid << " state: " << state.ToString());
    }
}

// ============================================================================
// ATTACK 5: Oracle signature replay from old blocks
// ============================================================================

BOOST_AUTO_TEST_CASE(attack5_v02_sig_replay_same_price_timestamp)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 5a: Replay V02 sig with same price/timestamp ===");

    // Phase 2 signature = hash(oracle_id, price, timestamp)
    // If price and timestamp haven't changed, the SAME signature is valid!
    // A miner could copy oracle data from block N and use it in block N+K
    // as long as the oracle data is within ORACLE_MAX_AGE_SECONDS

    CKey oracle_key;
    oracle_key.MakeNewKey(true);

    uint64_t price = 50000;
    int64_t timestamp = GetTime() - 1800; // 30 minutes ago

    COraclePriceMessage msg = MakeSignedMessage(oracle_key, 0, price, timestamp);

    // Verify original
    BOOST_CHECK(msg.VerifyAttestation());

    // "Replay" — create new message with same fields
    COraclePriceMessage replayed;
    replayed.oracle_id = msg.oracle_id;
    replayed.price_micro_usd = msg.price_micro_usd;
    replayed.timestamp = msg.timestamp;
    replayed.block_height = 0;
    replayed.nonce = 0;
    replayed.oracle_pubkey = msg.oracle_pubkey;
    replayed.schnorr_sig = msg.schnorr_sig;

    // Replayed sig verifies because hash(oracle_id, price, timestamp) is the same!
    BOOST_CHECK_MESSAGE(replayed.VerifyAttestation(),
        "Replayed sig should verify — Phase2 hash doesn't include block height");

    BOOST_TEST_MESSAGE("  >>> CRITICAL FINDING: Phase 2 signatures are replayable!");
    BOOST_TEST_MESSAGE("  >>> hash = H(oracle_id || price || timestamp)");
    BOOST_TEST_MESSAGE("  >>> No block height, no epoch, no nonce in the signed hash");
    BOOST_TEST_MESSAGE("  >>> If oracle price stays constant, sig from block N works in block N+K");
    BOOST_TEST_MESSAGE("  >>> Only protection: ORACLE_MAX_AGE_SECONDS (3600s) timestamp staleness check");
    BOOST_TEST_MESSAGE("  >>> Within that 1-hour window, arbitrary replay is possible");
}

BOOST_AUTO_TEST_CASE(attack5_v02_sig_replay_stale_protection)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 5b: Replay sig beyond staleness window ===");

    CKey oracle_key;
    oracle_key.MakeNewKey(true);

    // Sign with timestamp 2 hours ago
    int64_t old_timestamp = GetTime() - 7200;
    COraclePriceMessage msg = MakeSignedMessage(oracle_key, 0, 50000, old_timestamp);
    BOOST_CHECK(msg.VerifyAttestation());

    // The sig is technically valid, but block validation should reject it
    // because oracle_age > ORACLE_MAX_AGE_SECONDS
    CScript script = MakeV02Script(1, 50000, old_timestamp,
        {{0, msg.schnorr_sig}});
    CBlock block = MakeBlock(script, static_cast<uint32_t>(GetTime()), 101);

    BlockValidationState state;
    bool valid = OracleDataValidator::ValidateBlockOracleData(block, nullptr, Params().GetConsensus(), state);

    BOOST_TEST_MESSAGE("  Stale replay validation: " << valid << " state: " << state.ToString());
    // Note: This depends on whether the block height is past DD activation
    // In regtest, DD activates at height 0 by default
}

BOOST_AUTO_TEST_CASE(attack5_v03_epoch_not_on_chain)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 5c: V03 epoch not stored on-chain ===");

    // V03 bundles don't store epoch on-chain (it's set to 0 on extraction)
    // But ComputeOracleBundleHash includes epoch
    // The signing uses the real epoch, but verification uses epoch=0
    // This is the epoch mismatch bug (also noted in RH-05)

    // Demonstrate that ComputeOracleBundleHash with epoch=0 produces different
    // hash than with epoch=10
    COracleBundle bundle;
    bundle.epoch = 0;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = GetTime();
    uint256 hash_epoch0 = ComputeOracleBundleHash(bundle);

    bundle.epoch = 10;
    uint256 hash_epoch10 = ComputeOracleBundleHash(bundle);

    BOOST_CHECK_MESSAGE(hash_epoch0 != hash_epoch10,
        "Different epochs should produce different hashes");

    BOOST_TEST_MESSAGE("  >>> CRITICAL FINDING: V03 epoch mismatch vulnerability");
    BOOST_TEST_MESSAGE("  >>> Signing uses real epoch (e.g., 10)");
    BOOST_TEST_MESSAGE("  >>> Extraction sets epoch=0");
    BOOST_TEST_MESSAGE("  >>> ComputeOracleBundleHash(epoch=0) != hash signed over epoch=10");
    BOOST_TEST_MESSAGE("  >>> V03 signature verification should FAIL for any non-zero epoch");
    BOOST_TEST_MESSAGE("  >>> Currently 'works' because regtest tests happen in epoch 0");
    BOOST_TEST_MESSAGE("  hash(epoch=0): " << hash_epoch0.ToString());
    BOOST_TEST_MESSAGE("  hash(epoch=10): " << hash_epoch10.ToString());
}

// ============================================================================
// ATTACK 6: Oracle data activation timing
// ============================================================================

BOOST_AUTO_TEST_CASE(attack6_oracle_price_immediate_use)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 6: Oracle data used immediately (no maturity) ===");

    // Coinbase outputs require 100 block maturity to spend
    // But oracle DATA (price) is used for DD validation in the SAME block
    // This means a miner controls the oracle price for their own block

    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    int64_t now = GetTime();
    uint64_t price1 = 40000; // $0.04
    uint64_t price2 = 80000; // $0.08 — 2x the price!

    CScript script1 = MakeV03Script(price1, now);
    CScript script2 = MakeV03Script(price2, now);

    CBlock block1 = MakeBlock(script1, static_cast<uint32_t>(now), 101);
    CBlock block2 = MakeBlock(script2, static_cast<uint32_t>(now), 101);

    COracleBundle bundle1, bundle2;
    BOOST_CHECK(manager.ExtractOracleBundle(*block1.vtx[0], bundle1));
    BOOST_CHECK(manager.ExtractOracleBundle(*block2.vtx[0], bundle2));

    BOOST_TEST_MESSAGE("  Block 1 oracle price: " << bundle1.median_price_micro_usd);
    BOOST_TEST_MESSAGE("  Block 2 oracle price: " << bundle2.median_price_micro_usd);
    BOOST_TEST_MESSAGE("  >>> Oracle prices take effect immediately in the containing block");
    BOOST_TEST_MESSAGE("  >>> No maturity period for oracle data — only coinbase UTXO maturity");
    BOOST_TEST_MESSAGE("  >>> This is by design: DD transactions in block N use block N's oracle");
    BOOST_TEST_MESSAGE("  >>> Risk: A miner who controls oracle signing can manipulate the price");
    BOOST_TEST_MESSAGE("  >>> for DD transactions they include in their own block");
    BOOST_TEST_MESSAGE("  >>> Mitigation: MuSig2 quorum makes this hard");
}

// ============================================================================
// ATTACK 7: Miner withholding oracle data
// ============================================================================

BOOST_AUTO_TEST_CASE(attack7_block_without_oracle_data)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 7a: Block with no oracle OP_RETURN ===");

    // What happens when a miner produces a block without oracle data?
    CBlock block;
    block.nTime = static_cast<uint32_t>(GetTime());
    block.nVersion = 0x20000000;

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 101;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    // NO oracle output
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.hashMerkleRoot = BlockMerkleRoot(block);

    BlockValidationState state;
    bool valid = OracleDataValidator::ValidateBlockOracleData(block, nullptr, Params().GetConsensus(), state);

    BOOST_TEST_MESSAGE("  Block without oracle data: valid=" << valid);
    BOOST_TEST_MESSAGE("  State: " << state.ToString());

    // From the code: "Allow blocks without oracle data during transition"
    // oracle_output_count == 0 → return true
    if (valid) {
        BOOST_TEST_MESSAGE("  >>> FINDING: Blocks without oracle data are ALLOWED (transition period)");
        BOOST_TEST_MESSAGE("  >>> A miner can permanently withhold oracle data");
        BOOST_TEST_MESSAGE("  >>> DD transactions in such blocks have no price reference");
        BOOST_TEST_MESSAGE("  >>> If DD minting/redeeming REQUIRES a price, this blocks DD activity");
        BOOST_TEST_MESSAGE("  >>> But the block itself is valid — no forced oracle inclusion");
    }
}

BOOST_AUTO_TEST_CASE(attack7_block_with_empty_oracle_script)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 7b: Block with OP_RETURN OP_ORACLE but no data ===");

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    // No data push after OP_ORACLE

    CBlock block = MakeBlock(script, static_cast<uint32_t>(GetTime()), 101);

    BlockValidationState state;
    bool valid = OracleDataValidator::ValidateBlockOracleData(block, nullptr, Params().GetConsensus(), state);

    BOOST_TEST_MESSAGE("  Empty oracle script: valid=" << valid);

    // The oracle output is counted (oracle_output_count=1) but ExtractOracleBundle
    // fails because data is empty. Then: "transition period" allows it.
    if (valid) {
        BOOST_TEST_MESSAGE("  >>> FINDING: Empty oracle marker passes validation (transition leniency)");
    }
}

// ============================================================================
// ATTACK 8: Oracle data in non-coinbase transactions
// ============================================================================

BOOST_AUTO_TEST_CASE(attack8_oracle_in_regular_tx)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 8: Oracle OP_RETURN in non-coinbase tx ===");

    // OP_ORACLE is OP_NOP15 — treated as NOP in script execution
    // So a regular tx can have OP_RETURN OP_ORACLE in an output
    // This wouldn't be validated as oracle data, but could it confuse anything?

    int64_t now = GetTime();
    CScript oracle_script = MakeV01Script(0, 99999, now); // attacker's price

    // Create a non-coinbase tx with oracle-like output
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    tx.vin[0].scriptSig = CScript() << OP_TRUE;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0;
    tx.vout[0].scriptPubKey = oracle_script;

    // ExtractOracleBundle takes a CTransaction — it doesn't check if it's coinbase
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(tx), bundle);

    BOOST_TEST_MESSAGE("  Oracle extraction from non-coinbase tx: " << extracted);
    if (extracted) {
        BOOST_TEST_MESSAGE("  >>> FINDING: ExtractOracleBundle doesn't verify tx is coinbase!");
        BOOST_TEST_MESSAGE("  >>> It relies on callers (ValidateBlockOracleData) to only pass coinbase tx");
        BOOST_TEST_MESSAGE("  >>> If any code path calls ExtractOracleBundle on a regular tx,");
        BOOST_TEST_MESSAGE("  >>> it would extract attacker-controlled oracle data");
        BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 99999ULL);
    }

    // However, ValidateBlockOracleData always reads from block.vtx[0] (coinbase)
    // So block validation is safe. The risk is if other code calls ExtractOracleBundle
    // on non-coinbase transactions.
    BOOST_TEST_MESSAGE("  Block validation is safe (always reads vtx[0])");
    BOOST_TEST_MESSAGE("  Risk: RPC or other code paths calling ExtractOracleBundle directly");
}

BOOST_AUTO_TEST_CASE(attack8_oracle_in_second_tx)
{
    BOOST_TEST_MESSAGE("=== RH-29 Attack 8b: Oracle in block's 2nd tx (not coinbase) ===");

    int64_t now = GetTime();

    // Coinbase with NO oracle data
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 101;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // Second tx with oracle data
    CMutableTransaction tx2;
    tx2.vin.resize(1);
    tx2.vin[0].prevout = COutPoint(uint256::ONE, 0);
    tx2.vin[0].scriptSig = CScript() << OP_TRUE;
    tx2.vout.resize(1);
    tx2.vout[0].nValue = 0;
    tx2.vout[0].scriptPubKey = MakeV01Script(0, 99999, now);

    CBlock block;
    block.nTime = static_cast<uint32_t>(now);
    block.nVersion = 0x20000000;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.vtx.push_back(MakeTransactionRef(std::move(tx2)));
    block.hashMerkleRoot = BlockMerkleRoot(block);

    // ValidateBlockOracleData only looks at block.vtx[0] (coinbase)
    // The oracle data in vtx[1] is completely ignored
    BlockValidationState state;
    bool valid = OracleDataValidator::ValidateBlockOracleData(block, nullptr, Params().GetConsensus(), state);

    BOOST_TEST_MESSAGE("  Oracle in non-coinbase tx: block valid=" << valid);
    BOOST_TEST_MESSAGE("  ValidateBlockOracleData correctly only reads coinbase (vtx[0])");

    // Verify that extraction from vtx[1] would work (but isn't done by validation)
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool could_extract = manager.ExtractOracleBundle(*block.vtx[1], bundle);
    BOOST_TEST_MESSAGE("  Could extract from vtx[1]: " << could_extract);
    if (could_extract) {
        BOOST_TEST_MESSAGE("  >>> Attacker's price in vtx[1]: " << bundle.median_price_micro_usd);
        BOOST_TEST_MESSAGE("  >>> Block validation ignores it (safe)");
    }
}

// ============================================================================
// ADDITIONAL: Edge cases and combined attacks
// ============================================================================

BOOST_AUTO_TEST_CASE(attack_v01_price_boundaries)
{
    BOOST_TEST_MESSAGE("=== RH-29 Extra: V01 price boundary values ===");

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    int64_t now = GetTime();

    // Price = 0
    {
        CScript script = MakeV01Script(0, 0, now);
        COracleBundle bundle;
        bool ok = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);
        BOOST_TEST_MESSAGE("  price=0: extracted=" << ok);
        if (ok) BOOST_TEST_MESSAGE("  >>> V01 extraction allows price=0 (validation should catch this)");
    }

    // Price = UINT64_MAX
    {
        CScript script = MakeV01Script(0, UINT64_MAX, now);
        COracleBundle bundle;
        bool ok = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);
        BOOST_TEST_MESSAGE("  price=UINT64_MAX: extracted=" << ok);
        if (ok) {
            BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, UINT64_MAX);
            BOOST_TEST_MESSAGE("  >>> V01 extraction allows UINT64_MAX price");
        }
    }

    // Price = ORACLE_MIN_PRICE_MICRO_USD - 1
    {
        CScript script = MakeV01Script(0, ORACLE_MIN_PRICE_MICRO_USD - 1, now);
        COracleBundle bundle;
        bool ok = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);
        BOOST_TEST_MESSAGE("  price=MIN-1: extracted=" << ok);
        if (ok) BOOST_TEST_MESSAGE("  >>> V01 extraction doesn't check price range (validation does)");
    }
}

BOOST_AUTO_TEST_CASE(attack_v02_duplicate_oracle_id)
{
    BOOST_TEST_MESSAGE("=== RH-29 Extra: V02 duplicate oracle IDs in extraction ===");

    int64_t now = GetTime();
    // Two messages from same oracle_id=0
    std::vector<std::pair<uint8_t, std::vector<unsigned char>>> sigs;
    sigs.push_back({0, std::vector<unsigned char>(64, 0xAA)});
    sigs.push_back({0, std::vector<unsigned char>(64, 0xBB)}); // same oracle_id!

    CScript script = MakeV02Script(2, 50000, now, sigs);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    BOOST_TEST_MESSAGE("  V02 duplicate oracle_id extraction: " << extracted);
    if (extracted) {
        BOOST_CHECK_EQUAL(bundle.messages.size(), 2u);
        BOOST_CHECK_EQUAL(bundle.messages[0].oracle_id, bundle.messages[1].oracle_id);
        BOOST_TEST_MESSAGE("  >>> FINDING: Extraction accepts duplicate oracle IDs");
        BOOST_TEST_MESSAGE("  >>> ValidatePhaseTwoBundle catches this with seen_oracles set");
        BOOST_TEST_MESSAGE("  >>> But ExtractOracleBundle alone doesn't — defense in depth gap");
    }
}

BOOST_AUTO_TEST_CASE(attack_oracle_id_out_of_range)
{
    BOOST_TEST_MESSAGE("=== RH-29 Extra: Oracle ID beyond ORACLE_ACTIVE_COUNT ===");

    int64_t now = GetTime();
    // oracle_id = 255 (way beyond ORACLE_ACTIVE_COUNT=17, RC30)
    CScript script = MakeV01Script(255, 50000, now);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    BOOST_TEST_MESSAGE("  Oracle ID=255: extracted=" << extracted);
    if (extracted) {
        BOOST_CHECK_EQUAL(bundle.messages[0].oracle_id, 255);
        BOOST_TEST_MESSAGE("  >>> FINDING: Extraction accepts oracle_id=255 (out of range)");
        BOOST_TEST_MESSAGE("  >>> GetOracleNode(255) will return nullptr");
        BOOST_TEST_MESSAGE("  >>> Validation catches this in authorized oracle check");
    }
}

BOOST_AUTO_TEST_CASE(attack_timestamp_negative)
{
    BOOST_TEST_MESSAGE("=== RH-29 Extra: Negative timestamp in oracle data ===");

    int64_t neg_timestamp = -1;
    CScript script = MakeV01Script(0, 50000, neg_timestamp);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool extracted = manager.ExtractOracleBundle(CTransaction(MakeCoinbaseTx(script, 101)), bundle);

    BOOST_TEST_MESSAGE("  Negative timestamp: extracted=" << extracted);
    if (extracted) {
        BOOST_TEST_MESSAGE("  Extracted timestamp: " << bundle.timestamp);
        BOOST_TEST_MESSAGE("  >>> Extraction accepts negative timestamps");
        BOOST_TEST_MESSAGE("  >>> oracle_age = block.nTime - (-1) = block.nTime + 1");
        BOOST_TEST_MESSAGE("  >>> This will be >3600 and rejected by staleness check");
    }
}

BOOST_AUTO_TEST_SUITE_END()
