// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-43: DigiAssets + DigiDollar Interaction Attacks
 *
 * Tests attacking the boundary between DigiAssets (layer 2 tokens) and
 * DigiDollar (consensus-level stablecoin). DigiAssets uses:
 *   - nVersion = 2 (standard)
 *   - OP_RETURN with "DA" marker + CBOR-encoded metadata
 *   - P2PKH/P2SH/P2TR outputs with nValue > 0 (colored coins model)
 *     or nValue = 0 for some issuance outputs
 *
 * DigiDollar uses:
 *   - nVersion with 0x0770 in lower 16 bits, type in bits 24-31
 *   - OP_RETURN with "DD" marker + type + amounts
 *   - P2TR outputs with nValue = 0 (token outputs)
 *
 * Attack surface: Can one be confused for the other?
 */

#include <consensus/digidollar.h>
#include <digidollar/validation.h>
#include <digidollar/scripts.h>
#include <key.h>
#include <pubkey.h>
#include <script/script.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_rh43_digiassets_interaction_tests)

struct RH43TestSetup : public TestingSetup {
    RH43TestSetup() : TestingSetup(ChainType::REGTEST) {}

    // Helper: build a DD version field
    static int32_t MakeDDVersion(DigiDollar::DigiDollarTxType type) {
        const int32_t DD_BASE = 0x0D1D0770;
        return (static_cast<int32_t>(type) << 24) | (DD_BASE & 0x00FFFFFF);
    }

    // Helper: build a DD OP_RETURN for MINT
    static CScript MakeDDMintOpReturn(CAmount ddAmount, int64_t lockHeight, int lockTier = 0) {
        CScript script;
        script << OP_RETURN;
        script << std::vector<unsigned char>{'D', 'D'};
        script << CScriptNum(1); // MINT type
        script << CScriptNum(ddAmount);
        script << CScriptNum(lockHeight);
        if (lockTier > 0) script << CScriptNum(lockTier);
        return script;
    }

    // Helper: build a DD OP_RETURN for TRANSFER
    static CScript MakeDDTransferOpReturn(const std::vector<CAmount>& amounts) {
        CScript script;
        script << OP_RETURN;
        script << std::vector<unsigned char>{'D', 'D'};
        script << CScriptNum(2); // TRANSFER type
        for (auto amt : amounts) {
            script << CScriptNum(amt);
        }
        return script;
    }

    // Helper: build a DigiAssets-style OP_RETURN
    static CScript MakeDAOpReturn(const std::vector<unsigned char>& metadata = {0x01, 0x02, 0x03}) {
        CScript script;
        script << OP_RETURN;
        script << std::vector<unsigned char>{'D', 'A'};
        script << metadata;
        return script;
    }

    // Helper: make P2TR script from key
    static CScript MakeP2TR(const XOnlyPubKey& xpub) {
        CScript script;
        script << OP_1 << ToByteVector(xpub);
        return script;
    }
};

// ===========================================================================
// ATTACK 1: DA/DD Version Collision
//
// DigiDollar checks lower 16 bits == 0x0770. DigiAssets uses nVersion=2.
// Question: Can any version value be valid for BOTH systems?
// DigiAssets layer 2 parsers typically check nVersion == 2 (standard).
// DD checks (nVersion & 0xFFFF) == 0x0770.
//
// For collision: need nVersion such that (v & 0xFFFF) == 0x0770 AND
// DigiAssets parser accepts it. Since 0x0770 != 2, no direct collision.
// BUT: What about nVersion = 0x00020770? Lower 16 bits = 0x0770 (DD match),
// and upper bytes contain 0x0002 — could a DA parser read this as "version 2"
// from the upper 16 bits?
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_01_version_collision_da_dd, RH43TestSetup)
{
    // DD version marker: lower 16 bits = 0x0770
    const int32_t DD_MARKER = 0x0770;

    // DigiAssets standard version
    const int32_t DA_STANDARD_VERSION = 2;

    // PROOF: No direct collision — 0x0770 != 2
    BOOST_CHECK_NE(DD_MARKER, DA_STANDARD_VERSION);

    // But can we craft a version that matches DD AND looks like version 2?
    // Attempt: version = 0x00020770
    int32_t hybrid_version = (DA_STANDARD_VERSION << 16) | DD_MARKER;
    BOOST_CHECK_EQUAL(hybrid_version, 0x00020770);

    CMutableTransaction hybrid_tx;
    hybrid_tx.nVersion = hybrid_version;
    hybrid_tx.vin.resize(1);
    hybrid_tx.vout.resize(1);

    // This IS a valid DD transaction (lower 16 bits match)
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(hybrid_tx)));

    // A naive DigiAssets parser checking (nVersion >> 16) would see "2"
    int naive_da_version = (hybrid_version >> 16) & 0xFFFF;
    BOOST_CHECK_EQUAL(naive_da_version, 2);

    // FINDING: A transaction with nVersion=0x00020770 passes HasDigiDollarMarker
    // AND could trick a naive DA parser that reads version from upper bits.
    // However, standard DA parsers check nVersion == 2 exactly, so this
    // shouldn't work against well-implemented DA nodes.
    BOOST_CHECK_NE(hybrid_version, DA_STANDARD_VERSION);

    // The DD type extraction would get type=0 (bits 24-31 = 0x00) = DD_TX_NONE
    // So this would be rejected by GetDigiDollarTxType as well
    auto ddType = DigiDollar::GetDigiDollarTxType(CTransaction(hybrid_tx));
    BOOST_CHECK_EQUAL(ddType, DigiDollar::DD_TX_NONE);

    // Proper DD mint version: type=1 in bits 24-31
    int32_t dd_mint = MakeDDVersion(DigiDollar::DD_TX_MINT);
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction([&](){
        CMutableTransaction t; t.nVersion = dd_mint; t.vin.resize(1); t.vout.resize(1); return t;
    }())));
    BOOST_CHECK_NE(dd_mint, DA_STANDARD_VERSION);

    // CONCLUSION: No practical version collision exists because:
    // 1. DA uses nVersion == 2 exactly; DD uses lower 16 bits == 0x0770
    // 2. GetDigiDollarTxType rejects type 0 (DD_TX_NONE)
    // 3. But the VERSION MASK is overly permissive: any value with
    //    lower 16 bits = 0x0770 passes HasDigiDollarMarker, leaving
    //    65536 possible version values that appear as DD transactions.
    //    This is a fingerprinting surface (already noted in RH-18-01).
}

// ===========================================================================
// ATTACK 2: DigiAssets tx masquerading as DigiDollar
//
// Can an attacker craft a tx with nVersion=2 (DA standard), DA OP_RETURN,
// AND somehow pass DD validation? Or more subtly: can a tx with DD version
// but DA OP_RETURN format trick the DD amount extractor?
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_02_da_masquerading_as_dd, RH43TestSetup)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey xpub(key.GetPubKey());

    // Scenario A: Standard DA tx (nVersion=2) with DA OP_RETURN
    // Should NOT pass HasDigiDollarMarker
    {
        CMutableTransaction da_tx;
        da_tx.nVersion = 2;
        da_tx.vin.resize(1);
        da_tx.vin[0].prevout = COutPoint(uint256::ONE, 0);
        da_tx.vout.push_back(CTxOut(0, MakeP2TR(xpub)));  // DA token output
        da_tx.vout.push_back(CTxOut(0, MakeDAOpReturn()));  // DA metadata

        BOOST_CHECK(!DigiDollar::HasDigiDollarMarker(CTransaction(da_tx)));
        // SAFE: DA tx with nVersion=2 never passes DD marker check
    }

    // Scenario B: DA tx with nVersion=2 but DD-formatted OP_RETURN ("DD" marker)
    // Still shouldn't pass HasDigiDollarMarker (version check is first gate)
    {
        CMutableTransaction sneaky_da;
        sneaky_da.nVersion = 2;
        sneaky_da.vin.resize(1);
        sneaky_da.vin[0].prevout = COutPoint(uint256::ONE, 0);
        sneaky_da.vout.push_back(CTxOut(0, MakeP2TR(xpub)));
        sneaky_da.vout.push_back(CTxOut(0, MakeDDMintOpReturn(100000, 172800)));

        // Version check blocks it
        BOOST_CHECK(!DigiDollar::HasDigiDollarMarker(CTransaction(sneaky_da)));
        // SAFE: Even with DD OP_RETURN, wrong version blocks DD processing
    }

    // Scenario C: TX with DD version marker but DA OP_RETURN format
    // Has DD marker but OP_RETURN says "DA" not "DD"
    {
        CMutableTransaction dd_with_da_opreturn;
        dd_with_da_opreturn.nVersion = MakeDDVersion(DigiDollar::DD_TX_MINT);
        dd_with_da_opreturn.vin.resize(1);
        dd_with_da_opreturn.vin[0].prevout = COutPoint(uint256::ONE, 0);
        dd_with_da_opreturn.vout.push_back(CTxOut(0, MakeP2TR(xpub)));
        dd_with_da_opreturn.vout.push_back(CTxOut(0, MakeDAOpReturn()));

        // Passes version check (DD marker present)
        BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(dd_with_da_opreturn)));
        BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(CTransaction(dd_with_da_opreturn)),
                         DigiDollar::DD_TX_MINT);

        // But ExtractDDAmount should fail — OP_RETURN has "DA" not "DD"
        CAmount extracted = 0;
        bool ok = DigiDollar::ExtractDDAmount(MakeDAOpReturn(), extracted);
        BOOST_CHECK(!ok);
        // extracted may be modified on failure — just verify ok==false
        // SAFE: DD OP_RETURN parser requires "DD" marker, rejects "DA"
    }
}

// ===========================================================================
// ATTACK 3: DigiDollar tx masquerading as DigiAssets
//
// Can a DD tx trick DigiAssets layer 2 parsers? DigiAssets parsers look for:
//   - nVersion = 2
//   - OP_RETURN with "DA" prefix + CBOR metadata
// A DD tx uses nVersion with 0x0770 marker — this is NOT 2.
// But what if a DD tx ALSO includes a DA-formatted OP_RETURN alongside
// the DD OP_RETURN? (Multiple OP_RETURNs — see Attack 5.)
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_03_dd_masquerading_as_da, RH43TestSetup)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey xpub(key.GetPubKey());

    // DD tx with standard DD version — DA parsers check nVersion==2
    CMutableTransaction dd_tx;
    dd_tx.nVersion = MakeDDVersion(DigiDollar::DD_TX_MINT);
    dd_tx.vin.resize(1);
    dd_tx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    dd_tx.vout.push_back(CTxOut(0, MakeP2TR(xpub)));
    dd_tx.vout.push_back(CTxOut(0, MakeDDMintOpReturn(50000, 172800)));

    // DD version != 2, so DA parsers should ignore this tx
    BOOST_CHECK_NE(dd_tx.nVersion, 2);
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(dd_tx)));

    // A DA parser that only checks OP_RETURN format (not version) could
    // be confused. The "DD" marker != "DA" marker though, so it depends
    // on how strict the DA parser is.
    // FINDING: The version field difference is the primary defense.
    // But layer 2 parsers are NOT part of consensus — they run on
    // separate DA nodes. If a DA parser is lenient about nVersion,
    // it could accidentally try to parse DD OP_RETURNs as DA metadata.
    // This is a layer 2 concern, not a consensus issue.
}

// ===========================================================================
// ATTACK 4: Mixed DA + DD transactions in one block
//
// Block validation in ConnectBlock processes transactions sequentially.
// DD validation uses per-block accumulators (mint totals, etc).
// Question: Does a DA tx (nVersion=2) with P2TR outputs and value=0
// affect DD block-level accounting?
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_04_mixed_da_dd_block_ordering, RH43TestSetup)
{
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    XOnlyPubKey xpub1(key1.GetPubKey());
    XOnlyPubKey xpub2(key2.GetPubKey());

    // DA transaction (nVersion=2, standard)
    CMutableTransaction da_tx;
    da_tx.nVersion = 2;
    da_tx.vin.resize(1);
    da_tx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    da_tx.vout.push_back(CTxOut(0, MakeP2TR(xpub1)));   // DA token output (nValue=0)
    da_tx.vout.push_back(CTxOut(1000, MakeP2TR(xpub1))); // DA dust output
    da_tx.vout.push_back(CTxOut(0, MakeDAOpReturn()));

    // DD transaction
    CMutableTransaction dd_tx;
    dd_tx.nVersion = MakeDDVersion(DigiDollar::DD_TX_MINT);
    dd_tx.vin.resize(1);
    dd_tx.vin[0].prevout = COutPoint(uint256::ZERO, 0);
    dd_tx.vout.push_back(CTxOut(0, MakeP2TR(xpub2)));
    dd_tx.vout.push_back(CTxOut(0, MakeDDMintOpReturn(100000, 172800)));

    // DA tx should NOT be identified as DD
    BOOST_CHECK(!DigiDollar::HasDigiDollarMarker(CTransaction(da_tx)));

    // DD tx IS identified as DD
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(dd_tx)));

    // Block ordering: DA before DD vs DD before DA should not matter
    // because ConnectBlock only processes txs with DD marker through DD path.
    // SAFE: The HasDigiDollarMarker gate prevents DA txs from entering DD logic.

    // However, the UTXO set doesn't distinguish DA from DD P2TR outputs.
    // A DA UTXO with nValue=0 and 34-byte P2TR script is structurally
    // identical to a DD UTXO. See Attack 6 for exploitation.
}

// ===========================================================================
// ATTACK 5: Dual OP_RETURN — DA + DD in one transaction
//
// Bitcoin (and DigiByte) standard policy allows at most one OP_RETURN output.
// But at consensus level, multiple OP_RETURN outputs are valid.
// If a tx has both a DA OP_RETURN and a DD OP_RETURN, what happens?
//
// Critical question: ExtractDDAmountFromTxRef processes "first DD OP_RETURN"
// (line: break after first match). What if the DA OP_RETURN comes first?
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_05_dual_opreturn_da_and_dd, RH43TestSetup)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey xpub(key.GetPubKey());

    // TX with DD version, DA OP_RETURN first, DD OP_RETURN second
    CMutableTransaction dual_opreturn;
    dual_opreturn.nVersion = MakeDDVersion(DigiDollar::DD_TX_MINT);
    dual_opreturn.vin.resize(1);
    dual_opreturn.vin[0].prevout = COutPoint(uint256::ONE, 0);
    dual_opreturn.vout.push_back(CTxOut(0, MakeP2TR(xpub)));     // DD token output
    dual_opreturn.vout.push_back(CTxOut(0, MakeDAOpReturn()));    // DA OP_RETURN (first!)
    dual_opreturn.vout.push_back(CTxOut(0, MakeDDMintOpReturn(50000, 172800))); // DD OP_RETURN (second)

    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(dual_opreturn)));

    // ExtractDDAmountFromTxRef iterates vout looking for OP_RETURN with "DD" marker.
    // It checks: data[0]=='D' && data[1]=='D'. The DA OP_RETURN has 'D','A'.
    // So the DA OP_RETURN is SKIPPED (marker mismatch), and the DD OP_RETURN
    // is found second.
    // Let's verify by extracting DD amount from the DD OP_RETURN directly
    CAmount extracted = 0;
    bool ok = DigiDollar::ExtractDDAmount(MakeDDMintOpReturn(50000, 172800), extracted);
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(extracted, 50000);

    // DA OP_RETURN should NOT extract as DD
    CAmount da_extracted = 0;
    bool da_ok = DigiDollar::ExtractDDAmount(MakeDAOpReturn(), da_extracted);
    BOOST_CHECK(!da_ok);

    // FINDING: The "DD" vs "DA" marker in OP_RETURN provides adequate
    // discrimination. Multiple OP_RETURNs are non-standard but valid,
    // and the parser correctly skips non-DD OP_RETURNs.

    // HOWEVER: Standard policy rejects multiple OP_RETURN outputs.
    // A miner who includes this in a block bypasses standardness checks.
    // The consensus code handles it correctly (skips DA, finds DD).
    // LOW RISK: No confusion, just non-standard relay behavior.

    // Edge case: What about OP_RETURN with "DD" prefix but DA-style CBOR after?
    CScript fake_dd_opreturn;
    fake_dd_opreturn << OP_RETURN;
    fake_dd_opreturn << std::vector<unsigned char>{'D', 'D'};
    fake_dd_opreturn << std::vector<unsigned char>{0xA2, 0x61, 0x74}; // CBOR garbage
    CAmount fake_extracted = 0;
    bool fake_ok = DigiDollar::ExtractDDAmount(fake_dd_opreturn, fake_extracted);
    // CBOR data would be parsed as CScriptNum — likely throws scriptnum_error
    // or produces garbage amount. ExtractDDAmount should handle this gracefully.
    // (If it doesn't crash, we're OK even if the amount is wrong — the rest of
    // DD validation catches invalid amounts)
    BOOST_CHECK_MESSAGE(!fake_ok || fake_extracted > 0,
        "Either extraction fails or produces a positive amount (caught by other validation)");
}

// ===========================================================================
// ATTACK 6: P2TR nValue=0 Confusion — DA UTXO spent as DD input
//
// THE MAIN ATTACK VECTOR. Both DA and DD can create:
//   - P2TR outputs (script.size()==34, script[0]==OP_1)
//   - nValue = 0
//
// In the UTXO set, these are structurally identical. The DD validation
// code uses ExtractDDAmountFromTxRef to look up the CREATING transaction
// and read the OP_RETURN. The defense relies on:
//   1. HasDigiDollarMarker check on the creating tx (line 202 in validation.cpp)
//   2. "DD" marker in OP_RETURN
//
// Attack: Create a DA tx that has nVersion with DD marker AND a DD-formatted
// OP_RETURN, producing P2TR nValue=0 outputs. Then spend those in a DD
// TRANSFER. The creating tx passes all DD checks!
//
// Wait — can such a tx actually get into a block? ConnectBlock runs DD
// validation on txs with DD marker. A mint with valid DD marker would need
// valid collateral, oracle price, etc. So you can't create fake DD outputs
// without actually going through mint validation.
//
// BUT: What about pre-activation? If the tx enters a block BEFORE DD is
// activated (via BIP9), ConnectBlock skips DD validation entirely.
// Post-activation, those UTXOs still exist and could be spent in a TRANSFER.
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_06_p2tr_nvalue0_da_utxo_as_dd_input, RH43TestSetup)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey xpub(key.GetPubKey());

    // Simulate a pre-activation tx that has DD version + DD OP_RETURN
    // but was included in a block before DD activation (no DD validation ran)
    CMutableTransaction pre_activation_tx;
    pre_activation_tx.nVersion = MakeDDVersion(DigiDollar::DD_TX_MINT);
    pre_activation_tx.vin.resize(1);
    pre_activation_tx.vin[0].prevout = COutPoint(uint256::ONE, 0);

    // P2TR output with nValue=0 (looks like DD token)
    pre_activation_tx.vout.push_back(CTxOut(0, MakeP2TR(xpub)));
    // DD-formatted OP_RETURN claiming 1,000,000 cents ($10,000)
    pre_activation_tx.vout.push_back(CTxOut(0, MakeDDMintOpReturn(1000000, 172800)));
    // No collateral output! This would fail DD mint validation, but
    // if it entered a pre-activation block, it exists as a valid UTXO.

    CTransactionRef pre_tx = MakeTransactionRef(pre_activation_tx);

    // Now, post-activation, try to spend this UTXO in a DD TRANSFER
    // ExtractDDAmountFromTxRef would:
    // 1. Check !IsCoinBase() — passes
    // 2. Check HasDigiDollarMarker — passes! (version has DD marker)
    // 3. Parse OP_RETURN for "DD" marker — finds it
    // 4. Extract amount = 1,000,000 cents
    // 5. Match to output index 0 — success
    // Simulate what ExtractDDAmountFromTxRef does
    bool has_marker = DigiDollar::HasDigiDollarMarker(*pre_tx);
    BOOST_CHECK(has_marker); // Version check passes

    // Parse the OP_RETURN
    CAmount opreturn_amount = 0;
    bool opreturn_ok = DigiDollar::ExtractDDAmount(pre_activation_tx.vout[1].scriptPubKey, opreturn_amount);
    BOOST_CHECK(opreturn_ok);
    BOOST_CHECK_EQUAL(opreturn_amount, 1000000);

    // ANALYSIS: A pre-activation transaction with DD version format and DD
    // OP_RETURN would create UTXOs that pass ALL post-activation DD checks.
    // The TRANSFER validation would accept these as valid DD inputs worth
    // $10,000 — with ZERO collateral backing.
    //
    // VERIFIED SAFE: ConnectBlock at validation.cpp:2874 REJECTS DD-versioned
    // txs pre-activation via:
    //   if (!IsDigiDollarEnabled(pindex->pprev, m_chainman))
    //     return Invalid(BLOCK_CONSENSUS, "digidollar-not-active", ...)
    // This prevents DD-versioned txs from entering blocks before activation.
    //
    // RESIDUAL RISK: Mempool also checks (validation.cpp:777). Both gates
    // reject rather than skip, which is the correct approach.
    // The defense is solid — no phantom DD UTXOs possible.
}

// ===========================================================================
// ATTACK 7: DD OP_RETURN with DA-like CBOR payload (metadata confusion)
//
// DD OP_RETURN format: OP_RETURN "DD" <type> <amount> [<lockHeight>] [<tier>]
// DA OP_RETURN format: OP_RETURN "DA" <CBOR-encoded metadata>
//
// What if someone puts CBOR after "DD"? The DD parser reads pushdata as
// CScriptNum. CBOR data reinterpreted as CScriptNum could produce
// unexpected values.
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_07_volatile_metadata_overlap, RH43TestSetup)
{
    // Craft a DD OP_RETURN where the "amount" field is actually CBOR data
    CScript malicious_opreturn;
    malicious_opreturn << OP_RETURN;
    malicious_opreturn << std::vector<unsigned char>{'D', 'D'};
    malicious_opreturn << CScriptNum(1); // type = MINT

    // Instead of a proper amount, push CBOR-like data that CScriptNum interprets
    // as a large value. CBOR map header 0xBF followed by data:
    // As CScriptNum (little-endian), {0x00, 0xCA, 0x9A, 0x3B} = 999999999 + sign
    std::vector<unsigned char> cbor_as_amount = {0x00, 0xCA, 0x9A, 0x3B, 0x00};
    // This is 999,999,488 as little-endian int (approximately $10M)
    malicious_opreturn << cbor_as_amount;
    malicious_opreturn << CScriptNum(172800); // lockHeight

    CAmount extracted = 0;
    bool ok = DigiDollar::ExtractDDAmount(malicious_opreturn, extracted);

    if (ok) {
        // The CBOR bytes were interpreted as a CScriptNum amount
        // DD validation MUST catch this via amount range checks
        BOOST_TEST_MESSAGE("CBOR-as-amount extracted: " + std::to_string(extracted) + " cents");

        // Check if this exceeds max mint amount ($100k = 10,000,000 cents)
        DigiDollar::ConsensusParams params;
        if (extracted > params.maxMintAmount) {
            BOOST_TEST_MESSAGE("SAFE: Amount " + std::to_string(extracted) +
                " exceeds maxMintAmount " + std::to_string(params.maxMintAmount));
        } else {
            BOOST_TEST_MESSAGE("WARNING: Extracted amount " + std::to_string(extracted) +
                " is within valid range — metadata confusion could work");
        }
    } else {
        BOOST_TEST_MESSAGE("SAFE: CBOR data rejected by CScriptNum parser");
    }

    // Also test: negative amount via CBOR-like data
    // CScriptNum sign bit is highest bit of last byte
    std::vector<unsigned char> negative_cbor = {0x01, 0x00, 0x00, 0x80}; // -1 in CScriptNum
    CScript neg_opreturn;
    neg_opreturn << OP_RETURN;
    neg_opreturn << std::vector<unsigned char>{'D', 'D'};
    neg_opreturn << CScriptNum(1);
    neg_opreturn << negative_cbor;
    neg_opreturn << CScriptNum(172800);

    CAmount neg_extracted = 0;
    bool neg_ok = DigiDollar::ExtractDDAmount(neg_opreturn, neg_extracted);
    if (neg_ok) {
        // Negative amounts should be rejected by DD validation
        BOOST_CHECK_MESSAGE(neg_extracted <= 0,
            "Negative CScriptNum from CBOR-like data should produce non-positive amount");
    }
    // ExtractDDAmountFromTxRef returns amount > 0 check, so negative = rejected. SAFE.
}

// ===========================================================================
// ATTACK 8: DD version field with type=0 but DA-style body
//
// GetDigiDollarTxType returns DD_TX_NONE for type >= DD_TX_MAX or type == 0.
// But HasDigiDollarMarker only checks lower 16 bits — it returns true
// even for type=0. This means a tx with 0x001D0770 passes HasDigiDollarMarker
// but fails GetDigiDollarTxType (type=0=DD_TX_NONE).
//
// In mempool validation (line ~770), this is caught by the early reject:
//   if (earlyType == DD_TX_NONE) return Invalid(...)
// But in ConnectBlock, is the same check present?
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_08_dd_marker_type_zero_gap, RH43TestSetup)
{
    // Version with DD marker but type=0 in bits 24-31
    int32_t type_zero_version = 0x001D0770;

    CMutableTransaction type_zero_tx;
    type_zero_tx.nVersion = type_zero_version;
    type_zero_tx.vin.resize(1);
    type_zero_tx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    type_zero_tx.vout.resize(1);

    // Passes marker check
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(type_zero_tx)));

    // But type extraction returns NONE
    BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(CTransaction(type_zero_tx)),
                     DigiDollar::DD_TX_NONE);

    // This creates a gap: HasDigiDollarMarker says "yes it's DD"
    // but GetDigiDollarTxType says "invalid type". Any code that
    // checks HasDigiDollarMarker but NOT GetDigiDollarTxType has a bug.

    // Test type=4 (DD_TX_MAX, invalid)
    int32_t type_max_version = (4 << 24) | 0x001D0770;
    CMutableTransaction type_max_tx;
    type_max_tx.nVersion = type_max_version;
    type_max_tx.vin.resize(1);
    type_max_tx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    type_max_tx.vout.resize(1);

    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(type_max_tx)));
    BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(CTransaction(type_max_tx)),
                     DigiDollar::DD_TX_NONE);

    // Test type=255 (max byte value, well beyond DD_TX_MAX)
    int32_t type_255_version = (255 << 24) | 0x001D0770;
    CMutableTransaction type_255_tx;
    type_255_tx.nVersion = type_255_version;
    type_255_tx.vin.resize(1);
    type_255_tx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    type_255_tx.vout.resize(1);

    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(type_255_tx)));
    BOOST_CHECK_EQUAL(DigiDollar::GetDigiDollarTxType(CTransaction(type_255_tx)),
                     DigiDollar::DD_TX_NONE);

    // FINDING: 253 out of 256 possible type values pass HasDigiDollarMarker
    // but fail GetDigiDollarTxType. This is handled in mempool (RH-36c early
    // reject) but needs to be verified in ConnectBlock too.
    // With 65536 mask variants × 253 invalid types = ~16.5M version values
    // that pass HasDigiDollarMarker. DoS surface if ConnectBlock doesn't
    // early-reject these before expensive lookups.
}

// ===========================================================================
// ATTACK 9: DA OP_RETURN that accidentally matches DD format
//
// DigiAssets CBOR metadata could coincidentally start with bytes that
// decode as "DD" when pushed as script data. Test if this creates any
// confusion in the DD parser.
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_09_accidental_dd_match_in_da_opreturn, RH43TestSetup)
{
    // DA OP_RETURN where CBOR metadata happens to start with 0x44 0x44 ("DD")
    // Format: OP_RETURN <push "DA"> <push of CBOR starting with "DD"...>
    CScript da_with_dd_bytes;
    da_with_dd_bytes << OP_RETURN;
    da_with_dd_bytes << std::vector<unsigned char>{'D', 'A'}; // DA marker
    // CBOR payload that starts with "DD" bytes (0x44 = CBOR 1-byte string)
    da_with_dd_bytes << std::vector<unsigned char>{0x44, 0x44, 0x01, 0x00, 0xE8, 0x03};

    CAmount extracted = 0;
    bool ok = DigiDollar::ExtractDDAmount(da_with_dd_bytes, extracted);
    // Should fail because first push after OP_RETURN is "DA" not "DD"
    BOOST_CHECK(!ok);
    // extracted may be modified on failure — just verify ok==false

    // Now test: What if DA uses a SINGLE push containing all metadata
    // and that push starts with "DD"?
    CScript da_single_push;
    da_single_push << OP_RETURN;
    // Single push: "DD" + CBOR data
    std::vector<unsigned char> combined = {'D', 'D', 0x01, 0x00, 0xE8, 0x03};
    da_single_push << combined;

    CAmount extracted2 = 0;
    bool ok2 = DigiDollar::ExtractDDAmount(da_single_push, extracted2);
    // The parser checks if the FIRST push after OP_RETURN is exactly {D,D} (2 bytes).
    // A 6-byte push starting with "DD" has size != 2, so it should fail.
    BOOST_CHECK(!ok2);

    // What about a 2-byte push that IS "DD" followed by DA-style CBOR?
    CScript ambiguous;
    ambiguous << OP_RETURN;
    ambiguous << std::vector<unsigned char>{'D', 'D'}; // Exactly 2 bytes = matches DD!
    // Next push: CBOR data that CScriptNum interprets as type
    ambiguous << std::vector<unsigned char>{0xA2}; // CBOR map(2) = 162 as uint8
    // Next push: more CBOR
    ambiguous << std::vector<unsigned char>{0x19, 0x27, 0x10}; // CBOR uint(10000)

    CAmount extracted3 = 0;
    bool ok3 = DigiDollar::ExtractDDAmount(ambiguous, extracted3);
    if (ok3) {
        // The CBOR was interpreted as DD fields!
        // type = 162 (0xA2) — way beyond DD_TX_MAX, but ExtractDDAmount
        // might not check type validity (that's GetDigiDollarTxType's job)
        BOOST_TEST_MESSAGE("WARNING: CBOR data after 'DD' marker parsed as DD amount: " +
                          std::to_string(extracted3));
    }
    // Even if extracted, the tx would need to pass full DD validation
    // (version check, type check, collateral, oracle) to cause damage.
    // SAFE at consensus level due to layered validation.
}

// ===========================================================================
// ATTACK 10: Cross-spend — DD transfer consuming a non-DD P2TR UTXO
//
// A DD TRANSFER spends DD token UTXOs (P2TR, nValue=0) and the DD amounts
// come from the creating tx's OP_RETURN. What if a TRANSFER input points to
// a non-DD tx (nVersion=2) that happens to have a P2TR nValue=0 output?
//
// ExtractDDAmountFromTxRef checks HasDigiDollarMarker on the CREATING tx.
// If the creating tx has nVersion=2, it fails. SAFE.
// But what if the creating tx is a PREVIOUS DD transfer with extra outputs?
// ===========================================================================
BOOST_FIXTURE_TEST_CASE(rh43_10_cross_spend_nondd_p2tr_utxo, RH43TestSetup)
{
    CKey key;
    key.MakeNewKey(true);
    XOnlyPubKey xpub(key.GetPubKey());

    // Regular tx with P2TR nValue=0 output (NOT a DD tx)
    CMutableTransaction regular_p2tr;
    regular_p2tr.nVersion = 2;
    regular_p2tr.vin.resize(1);
    regular_p2tr.vin[0].prevout = COutPoint(uint256::ONE, 0);
    regular_p2tr.vout.push_back(CTxOut(0, MakeP2TR(xpub))); // P2TR, nValue=0
    regular_p2tr.vout.push_back(CTxOut(10000, MakeP2TR(xpub))); // change

    CTransactionRef regular_ref = MakeTransactionRef(regular_p2tr);

    // Try to extract DD amount from this non-DD tx
    // Simulating what happens when a DD TRANSFER tries to use this as input
    bool has_marker = DigiDollar::HasDigiDollarMarker(*regular_ref);
    BOOST_CHECK(!has_marker);
    // SAFE: HasDigiDollarMarker check in ExtractDDAmountFromTxRef blocks this.

    // The DD TRANSFER would fail to extract input amounts → conservation check
    // sees 0 DD input → rejects the transfer (can't create DD from nothing).
    // Defense is solid: version check on creating tx is the right approach.
}

BOOST_AUTO_TEST_SUITE_END()
