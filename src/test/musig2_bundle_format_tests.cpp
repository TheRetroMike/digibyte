// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * MuSig2 Bundle Format (v0x03) Unit Tests
 *
 * Tests for COracleBundle v0x03 data structures:
 * - New fields: version, aggregate_sig, participation_bitmap
 * - Helper methods: IsMuSig2(), GetV03PayloadSize(), SerializeV03Data(), DeserializeV03Data()
 * - Serialization round-trip
 * - Payload size calculations for various oracle counts
 * - Legacy v0x01/v0x02 payload fields remain storage-only, not V1 bundles
 */

#include <boost/test/unit_test.hpp>

#include <consensus/params.h>
#include <primitives/oracle.h>
#include <test/util/setup_common.h>

#include <cstring>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(musig2_bundle_format_tests, BasicTestingSetup)

// ============================================================================
// test_v03_bundle_struct_default — new fields default to empty/zero
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_bundle_struct_default)
{
    COracleBundle bundle;

    // V1 launches with MuSig2 v0x03 as the default and only bundle format.
    BOOST_CHECK_EQUAL(bundle.version, 3);
    BOOST_CHECK(bundle.IsMuSig2());
    BOOST_CHECK(bundle.aggregate_sig.empty());
    BOOST_CHECK(bundle.participation_bitmap.empty());

    // Existing fields should still default correctly
    BOOST_CHECK_EQUAL(bundle.epoch, 0);
    BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 0);
    BOOST_CHECK_EQUAL(bundle.timestamp, 0);
    BOOST_CHECK(bundle.messages.empty());
}

// ============================================================================
// test_v03_bundle_has_aggregate_sig — can set and get aggregate_sig (64 bytes)
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_bundle_has_aggregate_sig)
{
    COracleBundle bundle;
    bundle.version = 3;

    // Set a 64-byte aggregate signature (BIP-340 Schnorr)
    std::vector<unsigned char> sig(64, 0xAB);
    bundle.aggregate_sig = sig;

    BOOST_CHECK_EQUAL(bundle.aggregate_sig.size(), 64);
    BOOST_CHECK(bundle.aggregate_sig == sig);

    // Verify each byte
    for (size_t i = 0; i < 64; ++i) {
        BOOST_CHECK_EQUAL(bundle.aggregate_sig[i], 0xAB);
    }
}

// ============================================================================
// test_v03_bundle_has_bitmap — can set and get participation bitmap
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_bundle_has_bitmap)
{
    COracleBundle bundle;
    bundle.version = 3;

    // RC41: 9-of-35 reserved roster → bitmap needs 5 bytes (ceil(35/8) = 5)
    // Oracles 0,1,2,3,4,5,6,7,8 participating = bits 0-8 set
    // Byte 0: 0xFF (bits 0-7), Byte 1: 0x01 (bit 8), remaining bytes unset
    std::vector<unsigned char> bitmap = {0xFF, 0x01, 0x00, 0x00, 0x00};
    bundle.participation_bitmap = bitmap;

    BOOST_CHECK_EQUAL(bundle.participation_bitmap.size(), 5);
    BOOST_CHECK_EQUAL(bundle.participation_bitmap[0], 0xFF);
    BOOST_CHECK_EQUAL(bundle.participation_bitmap[1], 0x01);
    BOOST_CHECK_EQUAL(bundle.participation_bitmap[2], 0x00);
    BOOST_CHECK_EQUAL(bundle.participation_bitmap[3], 0x00);
    BOOST_CHECK_EQUAL(bundle.participation_bitmap[4], 0x00);
}

// ============================================================================
// test_v03_bundle_version — version field correctly set to 3
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_bundle_version)
{
    COracleBundle bundle;

    // Default version is v0x03 because V1 has no legacy oracle-bundle phase.
    BOOST_CHECK_EQUAL(bundle.version, 3);
    BOOST_CHECK(bundle.IsMuSig2());

    // Explicit v0x03 is MuSig2.
    bundle.version = 3;
    BOOST_CHECK_EQUAL(bundle.version, 3);
    BOOST_CHECK(bundle.IsMuSig2());

    // Version 1 is not MuSig2
    bundle.version = 1;
    BOOST_CHECK(!bundle.IsMuSig2());
}

// ============================================================================
// test_v03_bundle_serialization_roundtrip — serialize, deserialize, compare
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_bundle_serialization_roundtrip)
{
    COracleBundle bundle;
    bundle.version = 3;
    bundle.median_price_micro_usd = 1234567;  // ~$1.23
    bundle.timestamp = 1700000000;

    // RC41: 9-of-35 bitmap (5 bytes): oracles 0-8 participating
    bundle.participation_bitmap = {0xFF, 0x01, 0x00, 0x00, 0x00};

    // 64-byte aggregate signature
    bundle.aggregate_sig.resize(64);
    for (size_t i = 0; i < 64; ++i) {
        bundle.aggregate_sig[i] = static_cast<unsigned char>(i);
    }

    // Serialize
    std::vector<unsigned char> serialized = bundle.SerializeV03Data();
    BOOST_CHECK(!serialized.empty());

    // Deserialize into a new bundle
    COracleBundle deserialized;
    bool ok = COracleBundle::DeserializeV03Data(serialized, deserialized);
    BOOST_CHECK(ok);

    // Compare all v0x03 fields
    BOOST_CHECK_EQUAL(deserialized.version, 3);
    BOOST_CHECK(deserialized.IsMuSig2());
    BOOST_CHECK_EQUAL(deserialized.median_price_micro_usd, bundle.median_price_micro_usd);
    BOOST_CHECK_EQUAL(deserialized.timestamp, bundle.timestamp);
    BOOST_CHECK(deserialized.participation_bitmap == bundle.participation_bitmap);
    BOOST_CHECK(deserialized.aggregate_sig == bundle.aggregate_sig);
}

// ============================================================================
// test_v03_data_payload_size_9_of_35 — verify exactly 90 bytes for 9-of-35
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_data_payload_size_9_of_35)
{
    // v0x03 on-chain: bitmap_len(1) + bitmap(variable) + epoch(4) + price(8) + timestamp(8) + aggregate_sig(64)
    // RC41: For 35 reserved oracle slots: bitmap = ceil(35/8) = 5 bytes
    // Total: 1 + 5 + 4 + 8 + 8 + 64 = 90 bytes

    COracleBundle bundle;
    bundle.version = 3;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = 1700000000;
    bundle.participation_bitmap = {0xFF, 0x01, 0x00, 0x00, 0x00};  // 5 bytes for 35 slots
    bundle.aggregate_sig.resize(64, 0xAA);

    size_t payload_size = bundle.GetV03PayloadSize();
    BOOST_CHECK_EQUAL(payload_size, 90);

    // Also verify the serialized data is exactly this size
    std::vector<unsigned char> serialized = bundle.SerializeV03Data();
    BOOST_CHECK_EQUAL(serialized.size(), 90);
}

// ============================================================================
// test_v03_data_payload_size_17_of_35 — verify size for all active launch slots
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_data_payload_size_17_of_35)
{
    // Full current active participation: all 17 launch keys in a 35-slot bitmap.
    // Total: 1 + 5 + 4 + 8 + 8 + 64 = 90 bytes

    COracleBundle bundle;
    bundle.version = 3;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = 1700000000;
    bundle.participation_bitmap = {0xFF, 0xFF, 0x01, 0x00, 0x00};  // First 17 bits set
    bundle.aggregate_sig.resize(64, 0xBB);

    size_t payload_size = bundle.GetV03PayloadSize();
    BOOST_CHECK_EQUAL(payload_size, 90);
}

// ============================================================================
// test_v03_data_payload_size_9_of_35 — verify with 35 reserved slots
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_data_payload_size_9_of_35_reserved_slots)
{
    // For 35 oracle slots: bitmap = ceil(35/8) = 5 bytes
    // Total: 1 + 5 + 4 + 8 + 8 + 64 = 90 bytes

    COracleBundle bundle;
    bundle.version = 3;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = 1700000000;
    bundle.participation_bitmap = {0xFF, 0x01, 0x00, 0x00, 0x00};  // 5 bytes for 35 slots
    bundle.aggregate_sig.resize(64, 0xCC);

    size_t payload_size = bundle.GetV03PayloadSize();
    BOOST_CHECK_EQUAL(payload_size, 90);

    std::vector<unsigned char> serialized = bundle.SerializeV03Data();
    BOOST_CHECK_EQUAL(serialized.size(), 90);
}

// ============================================================================
// legacy_v02_message_fields_are_storage_only — v0x02 is not a V1 bundle
// ============================================================================
BOOST_AUTO_TEST_CASE(legacy_v02_message_fields_are_storage_only)
{
    COracleBundle bundle;
    bundle.version = 2;

    // The old message fields can still be populated for tests/tools, but V1
    // consensus does not treat them as an oracle bundle.
    bundle.epoch = 42;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = 1700000000;

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 50000;
    msg.timestamp = 1700000000;
    bundle.messages.push_back(msg);

    // Verify the storage fields are intact.
    BOOST_CHECK_EQUAL(bundle.epoch, 42);
    BOOST_CHECK_EQUAL(bundle.median_price_micro_usd, 50000);
    BOOST_CHECK_EQUAL(bundle.timestamp, 1700000000);
    BOOST_CHECK_EQUAL(bundle.messages.size(), 1);
    BOOST_CHECK_EQUAL(bundle.messages[0].oracle_id, 0);

    BOOST_CHECK_EQUAL(bundle.version, 2);
    BOOST_CHECK(!bundle.IsMuSig2());
    BOOST_CHECK(!bundle.IsValid(1));

    // Legacy message-threshold helpers may still summarize the message vector,
    // but the bundle itself is not valid for V1 block validation.
    BOOST_CHECK(bundle.HasConsensus(1));
    BOOST_CHECK(!bundle.HasConsensus(2));
}

// ============================================================================
// test_v01_bundle_unchanged — existing v0x01 bundle fields still work
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v01_bundle_unchanged)
{
    COracleBundle bundle;

    // v0x01 is Phase 1: single oracle, 1-of-1 consensus
    bundle.epoch = 10;
    bundle.median_price_micro_usd = 100000;
    bundle.timestamp = 1600000000;

    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 100000;
    msg.timestamp = 1600000000;
    bundle.messages.push_back(msg);

    // v0x01 functionality: single-oracle consensus
    BOOST_CHECK(bundle.HasConsensus(1));
    BOOST_CHECK_EQUAL(bundle.GetConsensusPrice(1), 100000);

    // The epoch constructor still works
    COracleBundle epoch_bundle(5);
    BOOST_CHECK_EQUAL(epoch_bundle.epoch, 5);

    // Messages vector still works correctly
    BOOST_CHECK(bundle.AddMessage(COraclePriceMessage(1, 50000, 1600000000)));
    BOOST_CHECK_EQUAL(bundle.messages.size(), 2);
}

// ============================================================================
// Additional edge case: deserialize with invalid data
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_deserialize_invalid_data)
{
    COracleBundle bundle;

    // Empty data
    std::vector<unsigned char> empty;
    BOOST_CHECK(!COracleBundle::DeserializeV03Data(empty, bundle));

    // Too short (only bitmap_len byte)
    std::vector<unsigned char> too_short = {0x02};
    BOOST_CHECK(!COracleBundle::DeserializeV03Data(too_short, bundle));

    // bitmap_len says 2 but only 1 byte of bitmap follows
    std::vector<unsigned char> truncated = {0x02, 0xFF};
    BOOST_CHECK(!COracleBundle::DeserializeV03Data(truncated, bundle));

    // bitmap_len=0 is invalid (must have at least 1 byte)
    std::vector<unsigned char> zero_bitmap(1 + 0 + 4 + 8 + 8 + 64, 0x00);
    zero_bitmap[0] = 0;  // bitmap_len = 0
    BOOST_CHECK(!COracleBundle::DeserializeV03Data(zero_bitmap, bundle));

    // Trailing bytes are invalid (must be exact size for declared bitmap_len)
    std::vector<unsigned char> with_trailing(1 + 2 + 4 + 8 + 8 + 64 + 1, 0x00);
    with_trailing[0] = 2;  // bitmap_len
    with_trailing[1] = 0x01;
    with_trailing[2] = 0x00;
    BOOST_CHECK(!COracleBundle::DeserializeV03Data(with_trailing, bundle));
}

// ============================================================================
// Additional: aggregate_sig must be exactly 64 bytes for serialization
// ============================================================================
BOOST_AUTO_TEST_CASE(test_v03_aggregate_sig_size_enforcement)
{
    COracleBundle bundle;
    bundle.version = 3;
    bundle.median_price_micro_usd = 50000;
    bundle.timestamp = 1700000000;
    bundle.participation_bitmap = {0xFF, 0x01};

    // Wrong size aggregate_sig (32 bytes instead of 64)
    bundle.aggregate_sig.resize(32, 0xAA);
    std::vector<unsigned char> serialized = bundle.SerializeV03Data();
    BOOST_CHECK(serialized.empty());  // Should fail: wrong sig size

    // Correct size
    bundle.aggregate_sig.resize(64, 0xAA);
    serialized = bundle.SerializeV03Data();
    BOOST_CHECK(!serialized.empty());
}

// ============================================================================
// nDigiDollarMuSig2Height defaults to max int
// ============================================================================
BOOST_AUTO_TEST_CASE(test_phase3_height_default)
{
    Consensus::Params params;
    BOOST_CHECK_EQUAL(params.nDigiDollarMuSig2Height, std::numeric_limits<int>::max());
}

BOOST_AUTO_TEST_SUITE_END()
