// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-19: Serialization & Deserialization Attack Tests
 *
 * Red-team fourth pass focusing on novel serialization bugs:
 *
 * BUG-1: V01/V02 trailing data malleability — extra bytes after valid data
 *        are silently ignored by ExtractOracleBundle, enabling coinbase
 *        transaction malleability (txid changes with same oracle semantics).
 *        V03 correctly enforces exact size. V01/V02 do not.
 *
 * BUG-2: V02 num_messages=0 creates a "ghost bundle" — zero messages but
 *        a consensus price and timestamp are deserialized. Downstream code
 *        that checks median_price_micro_usd > 0 will treat this as valid
 *        oracle data despite having zero attestations.
 *
 * BUG-3: V02 num_messages has no upper bound — a crafted coinbase with
 *        num_messages=255 and matching data causes 255*65 = 16575 bytes
 *        of oracle messages to be parsed. While bounded by script size,
 *        there's no explicit cap vs ORACLE_ACTIVE_COUNT.
 *
 * BUG-4: COracleBundle SERIALIZE_METHODS omits version, aggregate_sig,
 *        and participation_bitmap. P2P serialization of v0x03 bundles
 *        loses MuSig2 data — round-trip through CDataStream drops v0x03
 *        fields, silently downgrading to v0x02 semantics.
 *
 * BUG-5: Phase2 signature hash type confusion — GetAttestationSignatureHash()
 *        hashes (oracle_id, price, timestamp) using CHashWriter with
 *        uint32_t oracle_id + uint64_t price + int64_t timestamp. The
 *        on-chain V02 format stores oracle_id as uint8_t. An oracle_id
 *        of 1 serializes differently in the hash (4 bytes LE) vs on-chain
 *        (1 byte). This is correct behavior but a potential confusion
 *        vector if anyone tries to compute the hash from on-chain data
 *        without knowing the original type widths.
 *
 * BUG-6: V02 deserialization doesn't validate price range — any uint64
 *        price is accepted during deserialization, only validated later
 *        (if at all). Price of 0 or UINT64_MAX passes deserialization.
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <primitives/oracle.h>
#include <script/script.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>

BOOST_FIXTURE_TEST_SUITE(digidollar_rh19_serialization_tests, TestingSetup)

// ============================================================================
// BUG-1: V01/V02 trailing data malleability
// ============================================================================

BOOST_AUTO_TEST_CASE(v01_trailing_data_accepted)
{
    // Construct a valid V01 script: OP_RETURN OP_ORACLE <01> <17-byte payload>
    // Then append extra garbage bytes. ExtractOracleBundle should reject but doesn't.

    std::vector<unsigned char> compact_data;
    compact_data.push_back(0x01); // version
    compact_data.push_back(0x01); // oracle_id = 1

    // price = 50000 micro-USD (little-endian uint64)
    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i) {
        compact_data.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));
    }

    // timestamp = 1700000000 (little-endian int64)
    int64_t timestamp = 1700000000;
    for (int i = 0; i < 8; ++i) {
        compact_data.push_back(static_cast<unsigned char>((timestamp >> (i * 8)) & 0xFF));
    }

    // This is a valid 18-byte V01 payload. Now add trailing garbage.
    std::vector<unsigned char> malleated_data = compact_data;
    malleated_data.push_back(0xDE);
    malleated_data.push_back(0xAD);
    malleated_data.push_back(0xBE);
    malleated_data.push_back(0xEF);

    // Build CScript with trailing data in the push
    CScript script_clean;
    script_clean << OP_RETURN << OP_ORACLE;
    script_clean << compact_data;

    CScript script_malleated;
    script_malleated << OP_RETURN << OP_ORACLE;
    script_malleated << malleated_data;

    // Both produce valid coinbase transactions with different txids
    CMutableTransaction tx_clean;
    tx_clean.vout.emplace_back(0, script_clean);

    CMutableTransaction tx_malleated;
    tx_malleated.vout.emplace_back(0, script_malleated);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    COracleBundle bundle_clean, bundle_malleated;
    bool ok_clean = manager.ExtractOracleBundle(CTransaction(tx_clean), bundle_clean);
    bool ok_malleated = manager.ExtractOracleBundle(CTransaction(tx_malleated), bundle_malleated);

    // BUG: Both succeed with identical oracle data, but different scripts/txids
    // This is a malleability vector — the trailing bytes change the txid
    // without changing the oracle semantics.
    if (ok_clean && ok_malleated) {
        BOOST_CHECK_EQUAL(bundle_clean.median_price_micro_usd, bundle_malleated.median_price_micro_usd);
        BOOST_CHECK_EQUAL(bundle_clean.timestamp, bundle_malleated.timestamp);

        // The scripts are different, proving malleability
        BOOST_CHECK(script_clean != script_malleated);

        // SECURITY: V01 should reject trailing data (like V03 does)
        // For now, document that this is a known malleability vector
        BOOST_TEST_MESSAGE("BUG-1 CONFIRMED: V01 accepts trailing data after valid payload");
    }
}

BOOST_AUTO_TEST_CASE(v02_trailing_data_accepted)
{
    // V02 format: version(1) + num_msgs(1) + price(8) + timestamp(8) + N*(id+sig)
    // With 0 messages, expected_size = 18. Add trailing bytes.

    std::vector<unsigned char> v02_data;
    v02_data.push_back(0x02); // version
    v02_data.push_back(0x00); // num_messages = 0

    // price = 50000 (8 bytes LE)
    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i)
        v02_data.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));

    // timestamp (8 bytes LE)
    int64_t ts = 1700000000;
    for (int i = 0; i < 8; ++i)
        v02_data.push_back(static_cast<unsigned char>((ts >> (i * 8)) & 0xFF));

    // Valid 18-byte V02 with 0 messages. Now add trailing garbage.
    std::vector<unsigned char> malleated = v02_data;
    malleated.push_back(0xFF);
    malleated.push_back(0xFF);

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    script << malleated;

    CMutableTransaction tx;
    tx.vout.emplace_back(0, script);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool ok = manager.ExtractOracleBundle(CTransaction(tx), bundle);

    // V02 with num_messages=0 and trailing data
    // The check is `data.size() < expected_size` where expected_size = 18 + 0*65 = 18
    // data.size() = 20, so 20 >= 18 passes. Trailing 2 bytes ignored.
    if (ok) {
        BOOST_TEST_MESSAGE("BUG-1+BUG-2 CONFIRMED: V02 accepts 0-message bundle with trailing data");
        BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, price);
    }
}

BOOST_AUTO_TEST_CASE(v03_correctly_rejects_trailing_data)
{
    // V03 DeserializeV03Data enforces exact size — good!
    // Verify this is actually enforced.

    std::vector<unsigned char> v03_data;
    v03_data.push_back(0x01); // bitmap_len = 1
    v03_data.push_back(0xFF); // bitmap byte

    // epoch (4 bytes, zero)
    for (int i = 0; i < 4; ++i)
        v03_data.push_back(0x00);

    // price (8 bytes)
    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i)
        v03_data.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));

    // timestamp (8 bytes)
    int64_t ts = 1700000000;
    for (int i = 0; i < 8; ++i)
        v03_data.push_back(static_cast<unsigned char>((ts >> (i * 8)) & 0xFF));

    // sig (64 bytes of zeros — won't verify but tests deserialization)
    v03_data.insert(v03_data.end(), 64, 0x00);

    // Should be exactly 86 bytes. Verify exact-size enforcement.
    BOOST_CHECK_EQUAL(v03_data.size(), 86u);

    COracleBundle bundle;
    BOOST_CHECK(COracleBundle::DeserializeV03Data(v03_data, bundle));

    // Now add trailing byte — should be rejected
    std::vector<unsigned char> malleated = v03_data;
    malleated.push_back(0xFF);

    COracleBundle bundle2;
    BOOST_CHECK(!COracleBundle::DeserializeV03Data(malleated, bundle2));

    BOOST_TEST_MESSAGE("V03 correctly rejects trailing data — V01/V02 should do the same");
}

// ============================================================================
// BUG-2: V02 num_messages=0 ghost bundle
// ============================================================================

BOOST_AUTO_TEST_CASE(v02_zero_messages_ghost_bundle)
{
    // V02 with num_messages=0 is accepted by ExtractOracleBundle
    // Produces a bundle with price and timestamp but zero messages/signatures.

    std::vector<unsigned char> v02_data;
    v02_data.push_back(0x02); // version
    v02_data.push_back(0x00); // num_messages = 0

    // Attacker-chosen price: $100 (max)
    uint64_t price = ORACLE_MAX_PRICE_MICRO_USD;
    for (int i = 0; i < 8; ++i)
        v02_data.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));

    int64_t ts = 1700000000;
    for (int i = 0; i < 8; ++i)
        v02_data.push_back(static_cast<unsigned char>((ts >> (i * 8)) & 0xFF));

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    script << v02_data;

    CMutableTransaction tx;
    tx.vout.emplace_back(0, script);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool ok = manager.ExtractOracleBundle(CTransaction(tx), bundle);

    if (ok) {
        // Ghost bundle: has a price but no attestations
        BOOST_CHECK_EQUAL(bundle.messages.size(), 0u);
        BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, ORACLE_MAX_PRICE_MICRO_USD);
        BOOST_CHECK(!bundle.HasConsensus(1)); // No messages = no consensus

        // The price field is still set, which could confuse downstream code
        // that checks median_price_micro_usd > 0 without also checking HasConsensus
        BOOST_CHECK(bundle.median_price_micro_usd > 0);

        BOOST_TEST_MESSAGE("BUG-2 CONFIRMED: V02 num_messages=0 creates ghost bundle with attacker-controlled price");
    }
}

// ============================================================================
// BUG-3: V02 no upper bound on num_messages
// ============================================================================

BOOST_AUTO_TEST_CASE(v02_excessive_message_count)
{
    // Craft V02 with num_messages=255 — 255 * 65 = 16575 bytes of oracle data
    // plus 18 byte header = 16593 bytes total.
    // This exceeds ORACLE_ACTIVE_COUNT (RC30: 17) but is accepted by deserialization.

    uint8_t num_messages = 255;
    std::vector<unsigned char> v02_data;
    v02_data.push_back(0x02);
    v02_data.push_back(num_messages);

    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i)
        v02_data.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));

    int64_t ts = 1700000000;
    for (int i = 0; i < 8; ++i)
        v02_data.push_back(static_cast<unsigned char>((ts >> (i * 8)) & 0xFF));

    // Add 255 entries: oracle_id (1 byte) + sig (64 bytes) each
    for (int m = 0; m < num_messages; ++m) {
        v02_data.push_back(static_cast<unsigned char>(m % 30)); // cycle oracle IDs
        v02_data.insert(v02_data.end(), 64, 0x00); // zero sigs
    }

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    script << v02_data;

    CMutableTransaction tx;
    tx.vout.emplace_back(0, script);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool ok = manager.ExtractOracleBundle(CTransaction(tx), bundle);

    if (ok) {
        BOOST_CHECK_EQUAL(bundle.messages.size(), 255u);
        BOOST_CHECK(bundle.messages.size() > ORACLE_ACTIVE_COUNT);
        BOOST_TEST_MESSAGE("BUG-3 CONFIRMED: V02 deserialization accepts 255 messages (max active is 17, RC30)");
    }
}

// ============================================================================
// BUG-4: COracleBundle P2P serialization drops v0x03 fields
// ============================================================================

BOOST_AUTO_TEST_CASE(bundle_p2p_serialization_drops_musig2_fields)
{
    // COracleBundle::SERIALIZE_METHODS only serializes:
    //   messages, epoch, median_price_micro_usd, timestamp
    // It does NOT serialize: version, aggregate_sig, participation_bitmap
    // This means v0x03 bundles lose their MuSig2 data during P2P relay.

    COracleBundle original;
    original.version = 3;
    original.epoch = 42;
    original.median_price_micro_usd = 50000;
    original.timestamp = 1700000000;
    original.aggregate_sig.resize(64, 0xAB);
    original.participation_bitmap = {0xFF, 0x03};

    // Serialize via CDataStream (P2P path)
    DataStream ss{};
    ss << original;

    // Deserialize
    COracleBundle deserialized;
    ss >> deserialized;

    // Check what survived
    BOOST_CHECK_EQUAL(deserialized.epoch, 42);
    BOOST_CHECK_EQUAL(deserialized.median_price_micro_usd, 50000u);
    BOOST_CHECK_EQUAL(deserialized.timestamp, 1700000000);

    // FIX [RH-25a]: version-conditional serialization now preserves v0x03 fields
    BOOST_CHECK_EQUAL(deserialized.version, 3u);
    BOOST_CHECK_EQUAL(deserialized.aggregate_sig.size(), 64u);
    BOOST_CHECK_EQUAL(deserialized.aggregate_sig[0], 0xAB);
    BOOST_CHECK_EQUAL(deserialized.participation_bitmap.size(), 2u);
    BOOST_CHECK_EQUAL(deserialized.participation_bitmap[0], 0xFF);
    BOOST_CHECK_EQUAL(deserialized.participation_bitmap[1], 0x03);

    BOOST_TEST_MESSAGE("BUG-4 FIXED [RH-25a]: COracleBundle SERIALIZE_METHODS now preserves version, aggregate_sig, participation_bitmap");
}

// ============================================================================
// BUG-5: Hash type width mismatch documentation
// ============================================================================

BOOST_AUTO_TEST_CASE(phase2_hash_type_widths)
{
    // GetAttestationSignatureHash hashes oracle_id as uint32_t (4 bytes)
    // but on-chain V02 format stores oracle_id as uint8_t (1 byte).
    // If someone naively reconstructs the hash from on-chain bytes, they get wrong hash.

    COraclePriceMessage msg;
    msg.oracle_id = 1;
    msg.price_micro_usd = 50000;
    msg.timestamp = 1700000000;

    uint256 hash_from_message = msg.GetAttestationSignatureHash();

    // Manually compute what a naive on-chain reconstruction would produce
    // (using 1-byte oracle_id instead of 4-byte)
    CHashWriter ss_naive(0);
    uint8_t oracle_id_u8 = 1;
    ss_naive << oracle_id_u8;
    ss_naive << msg.price_micro_usd;
    ss_naive << msg.timestamp;
    uint256 hash_naive = ss_naive.GetHash();

    // These MUST be different — proves the type width matters for hash reconstruction
    BOOST_CHECK(hash_from_message != hash_naive);
    BOOST_TEST_MESSAGE("BUG-5 DOCUMENTED: Phase2 hash uses uint32_t oracle_id (4 bytes) but on-chain stores uint8_t (1 byte)");
}

// ============================================================================
// BUG-6: V02 deserialization accepts extreme prices
// ============================================================================

BOOST_AUTO_TEST_CASE(v02_extreme_price_deserialization)
{
    // V02 deserializes any uint64 price without range checking.
    // Price = 0 and price = UINT64_MAX both pass.

    auto make_v02_script = [](uint64_t price, int64_t ts) -> CScript {
        std::vector<unsigned char> data;
        data.push_back(0x02);
        data.push_back(0x01); // 1 message

        for (int i = 0; i < 8; ++i)
            data.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));
        for (int i = 0; i < 8; ++i)
            data.push_back(static_cast<unsigned char>((ts >> (i * 8)) & 0xFF));

        // oracle_id + 64-byte zero sig
        data.push_back(0x01);
        data.insert(data.end(), 64, 0x00);

        CScript script;
        script << OP_RETURN << OP_ORACLE;
        script << data;
        return script;
    };

    OracleBundleManager& manager = OracleBundleManager::GetInstance();

    // Price = 0
    {
        CScript script = make_v02_script(0, 1700000000);
        CMutableTransaction tx;
        tx.vout.emplace_back(0, script);
        COracleBundle bundle;
        bool ok = manager.ExtractOracleBundle(CTransaction(tx), bundle);
        if (ok) {
            BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 0u);
            BOOST_TEST_MESSAGE("BUG-6a: V02 accepts price=0 during deserialization");
        }
    }

    // Price = UINT64_MAX
    {
        CScript script = make_v02_script(UINT64_MAX, 1700000000);
        CMutableTransaction tx;
        tx.vout.emplace_back(0, script);
        COracleBundle bundle;
        bool ok = manager.ExtractOracleBundle(CTransaction(tx), bundle);
        if (ok) {
            BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, UINT64_MAX);
            BOOST_CHECK(bundle.median_price_micro_usd > ORACLE_MAX_PRICE_MICRO_USD);
            BOOST_TEST_MESSAGE("BUG-6b: V02 accepts price=UINT64_MAX during deserialization");
        }
    }
}

// ============================================================================
// BUG-7: V01 truncated payload crash test
// ============================================================================

BOOST_AUTO_TEST_CASE(v01_truncated_payload)
{
    // V01 requires 18 bytes (version + 17 data). What about partial payloads?
    // The check is `data.size() < 18` — payloads of 1-17 bytes are rejected.
    // But what about empty data (just version byte)?

    for (size_t len = 1; len < 18; ++len) {
        std::vector<unsigned char> data(len, 0x01); // version=0x01 + garbage
        data[0] = 0x01;

        CScript script;
        script << OP_RETURN << OP_ORACLE;
        script << data;

        CMutableTransaction tx;
        tx.vout.emplace_back(0, script);

        OracleBundleManager& manager = OracleBundleManager::GetInstance();
        COracleBundle bundle;
        bool ok = manager.ExtractOracleBundle(CTransaction(tx), bundle);
        BOOST_CHECK_MESSAGE(!ok, "V01 truncated payload of " + std::to_string(len) + " bytes should be rejected");
    }

    BOOST_TEST_MESSAGE("V01 truncated payloads correctly rejected (no crash)");
}

// ============================================================================
// BUG-8: V03 bitmap_len=255 amplification
// ============================================================================

BOOST_AUTO_TEST_CASE(v03_large_bitmap_amplification)
{
    // bitmap_len is uint8, max 255. DeserializeV03Data accepts any bitmap_len.
    // With bitmap_len=255, DecodeBitmap can produce up to 255*8 = 2040 oracle IDs.
    // Each gets a synthetic COraclePriceMessage in ExtractOracleBundle.

    std::vector<unsigned char> v03_data;
    v03_data.push_back(255); // bitmap_len = 255

    // 255 bitmap bytes, all 0xFF (all oracles "participating")
    v03_data.insert(v03_data.end(), 255, 0xFF);

    // epoch (4 bytes, zero)
    for (int i = 0; i < 4; ++i)
        v03_data.push_back(0x00);

    // price (8 bytes)
    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i)
        v03_data.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));

    // timestamp (8 bytes)
    int64_t ts = 1700000000;
    for (int i = 0; i < 8; ++i)
        v03_data.push_back(static_cast<unsigned char>((ts >> (i * 8)) & 0xFF));

    // sig (64 bytes zeros)
    v03_data.insert(v03_data.end(), 64, 0x00);

    // Expected size: 1 + 255 + 4 + 8 + 8 + 64 = 340
    BOOST_CHECK_EQUAL(v03_data.size(), 340u);

    COracleBundle bundle;
    bool ok = COracleBundle::DeserializeV03Data(v03_data, bundle);
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(bundle.participation_bitmap.size(), 255u);

    // This bitmap claims 255*8 = 2040 oracle participants, far exceeding
    // ORACLE_TOTAL_COUNT (30). The DecodeBitmap in ExtractOracleBundle uses
    // params.nOracleTotalOracles as a cap, but the raw deserialization doesn't.
    BOOST_TEST_MESSAGE("BUG-8: V03 bitmap_len=255 accepted — potential amplification if DecodeBitmap cap is wrong");
}

// ============================================================================
// BUG-9: CHashWriter serialization order matters for cross-impl verification
// ============================================================================

BOOST_AUTO_TEST_CASE(oracle_bundle_hash_field_order)
{
    // ComputeOracleBundleHash hashes (epoch, price, timestamp)
    // GetAttestationSignatureHash hashes (oracle_id, price, timestamp)
    // These use CHashWriter << which serializes each field in its native width.
    // Verify the hash is sensitive to field order (no accidental collisions).

    COracleBundle bundle1;
    bundle1.epoch = 1;
    bundle1.median_price_micro_usd = 50000;
    bundle1.timestamp = 1700000000;

    COracleBundle bundle2;
    bundle2.epoch = 50000;          // swap epoch and price values
    bundle2.median_price_micro_usd = 1;
    bundle2.timestamp = 1700000000;

    uint256 hash1 = ComputeOracleBundleHash(bundle1);
    uint256 hash2 = ComputeOracleBundleHash(bundle2);

    // Even though fields are "swapped", different types (int32 vs uint64)
    // should produce different serializations
    BOOST_CHECK(hash1 != hash2);
}

// ============================================================================
// BUG-10: V02 duplicate oracle_id in deserialized messages
// ============================================================================

BOOST_AUTO_TEST_CASE(v02_duplicate_oracle_ids_in_script)
{
    // V02 deserialization doesn't check for duplicate oracle IDs.
    // A miner could include the same oracle_id twice with different signatures.
    // ValidatePhaseTwoBundle checks for dupes, but ExtractOracleBundle doesn't.

    std::vector<unsigned char> v02_data;
    v02_data.push_back(0x02);
    v02_data.push_back(0x02); // 2 messages

    uint64_t price = 50000;
    for (int i = 0; i < 8; ++i)
        v02_data.push_back(static_cast<unsigned char>((price >> (i * 8)) & 0xFF));

    int64_t ts = 1700000000;
    for (int i = 0; i < 8; ++i)
        v02_data.push_back(static_cast<unsigned char>((ts >> (i * 8)) & 0xFF));

    // Two messages with SAME oracle_id
    for (int m = 0; m < 2; ++m) {
        v02_data.push_back(0x01); // oracle_id = 1 (both times)
        v02_data.insert(v02_data.end(), 64, 0x00);
    }

    CScript script;
    script << OP_RETURN << OP_ORACLE;
    script << v02_data;

    CMutableTransaction tx;
    tx.vout.emplace_back(0, script);

    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    COracleBundle bundle;
    bool ok = manager.ExtractOracleBundle(CTransaction(tx), bundle);

    if (ok) {
        // Deserialization succeeds with duplicate oracle IDs
        BOOST_CHECK_EQUAL(bundle.messages.size(), 2u);
        BOOST_CHECK_EQUAL(bundle.messages[0].oracle_id, bundle.messages[1].oracle_id);
        BOOST_TEST_MESSAGE("BUG-10 CONFIRMED: V02 deserialization accepts duplicate oracle IDs");
    }
}

BOOST_AUTO_TEST_SUITE_END()
