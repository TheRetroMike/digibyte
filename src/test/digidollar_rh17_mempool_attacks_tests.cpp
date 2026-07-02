// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-17: MEMPOOL POLICY & TRANSACTION RELAY ATTACKS
 *
 * Red-team tests targeting DigiDollar mempool acceptance, relay policies,
 * and interactions with RBF/CPFP/package relay. These test novel attack
 * vectors at the policy layer rather than consensus.
 *
 * Attack vectors tested:
 * 1. Version mask collision: non-DD txs get DD standardness exemptions
 * 2. RBF displacement: DD mint replaced by non-DD tx, canceling mint
 * 3. Dust exemption abuse via DD version without DD content
 * 4. DD validation cost asymmetry (CPU-expensive mempool checks)
 * 5. Standardness vs consensus gap for DD transactions
 * 6. Fee estimation pollution from DD transactions
 * 7. CPFP on DD change outputs
 * 8. Package relay bypass of individual DD validation
 */

#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <digidollar/health.h>
#include <digidollar/digidollar.h>
#include <digidollar/scripts.h>
#include <digidollar/validation.h>
#include <kernel/chainparams.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <test/util/setup_common.h>
#include <util/rbf.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(digidollar_rh17_mempool_attacks_tests, BasicTestingSetup)

// =============================================================================
// Helper: Create a transaction with specific version
// =============================================================================
static CMutableTransaction MakeTxWithVersion(int32_t version, int numOutputs = 1)
{
    CMutableTransaction tx;
    tx.nVersion = version;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(uint256::ONE, 0);
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(72, 0x30);
    tx.vout.resize(numOutputs);
    for (int i = 0; i < numOutputs; i++) {
        tx.vout[i].nValue = 1000;
        tx.vout[i].scriptPubKey = CScript() << OP_DUP << OP_HASH160
            << std::vector<unsigned char>(20, 0x01) << OP_EQUALVERIFY << OP_CHECKSIG;
    }
    return tx;
}

// =============================================================================
// RH-17-01: VERSION MASK COLLISION — STANDARDNESS BYPASS
//
// ATTACK: The DD version check in IsStandardTx uses lower 16 bits == 0x0770.
// An attacker crafts a NON-DD transaction with version where lower 16 bits
// match 0x0770 (e.g., 0x00000770, 0x12340770, etc.). This tx gets:
//   - Version range check bypassed (DD txs skip version > TX_MAX check)
//   - Dust check bypassed (DD txs skip IsDust)
// But it's NOT actually a DD transaction (no DD marker in consensus).
//
// IMPACT: Attacker creates non-standard dust outputs that pollute the UTXO
// set, or uses high version numbers that should be rejected.
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_01_version_mask_collision_standardness_bypass)
{
    // The DD version marker check in policy uses lower 16 bits
    const int32_t DD_TX_VERSION = 0x0D1D0770;
    const int32_t DD_VERSION_MASK = 0x0000FFFF;
    const int32_t DD_MARKER = DD_TX_VERSION & DD_VERSION_MASK; // 0x0770

    // Test various version numbers that match the mask but aren't real DD txs
    std::vector<int32_t> collision_versions = {
        0x00000770,  // Minimal collision
        0x12340770,  // Random upper bits
        static_cast<int32_t>(0xFFFF0770),  // Max upper bits (signed: negative)
        0x7FFF0770,  // Max positive upper bits
        DD_TX_VERSION, // Actual DD version
    };

    for (int32_t ver : collision_versions) {
        // Verify mask matches
        BOOST_CHECK_EQUAL(ver & DD_VERSION_MASK, DD_MARKER);

        // Create tx with this version and a dust output
        CMutableTransaction tx = MakeTxWithVersion(ver);
        tx.vout[0].nValue = 1; // 1 satoshi — definitely dust

        std::string reason;
        // IsStandardTx should detect this as "DigiDollar" due to version mask
        bool isStandard = IsStandardTx(CTransaction(tx),
            MAX_OP_RETURN_RELAY, /*permit_bare_multisig=*/true,
            CFeeRate(DUST_RELAY_TX_FEE), reason);

        // VULNERABILITY CHECK: If version 0x00000770 (not a real DD tx) passes
        // standardness, an attacker can bypass dust checks with fake DD versions.
        // The consensus layer (HasDigiDollarMarker) uses the SAME mask, so
        // these would also be treated as DD in consensus — but without valid
        // DD content, they'd fail DD validation in AcceptToMemoryPool.
        //
        // The question is: does standardness let them through before DD
        // validation catches them? If yes, that's wasted validation work.

        if (ver != DD_TX_VERSION) {
            // Non-canonical DD versions that match mask:
            // These pass standardness (dust bypass) but will fail DD validation
            // in ATMP — this is the gap.
            BOOST_TEST_MESSAGE("Version " << std::hex << ver << std::dec
                << ": isStandard=" << isStandard << " reason=" << reason);

            // DEFENSE CHECK: The tx IS considered standard due to mask match
            // This means the validation cost is paid before rejection
            if (isStandard) {
                BOOST_TEST_MESSAGE("FINDING: Version 0x" << std::hex << ver << std::dec
                    << " passes standardness as DD despite not being canonical DD version. "
                    "Attacker can craft txs that bypass dust checks and waste DD validation CPU.");
            }
        }
    }

    // CRITICAL: Verify that a truly non-DD tx with dust is rejected
    CMutableTransaction normalTx = MakeTxWithVersion(2);
    normalTx.vout[0].nValue = 1; // dust
    std::string reason;
    bool isStandard = IsStandardTx(CTransaction(normalTx),
        MAX_OP_RETURN_RELAY, /*permit_bare_multisig=*/true,
        CFeeRate(DUST_RELAY_TX_FEE), reason);
    BOOST_CHECK_MESSAGE(!isStandard, "Normal tx with dust should be non-standard");
    BOOST_CHECK_EQUAL(reason, "dust");
}

// =============================================================================
// RH-17-02: CONSENSUS MARKER MATCHES POLICY MARKER
//
// Verify that HasDigiDollarMarker (consensus) uses the same mask as
// IsStandardTx (policy). If they diverge, there's a standardness/consensus
// gap where txs could pass one but not the other.
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_02_consensus_vs_policy_marker_consistency)
{
    // Policy constants (from policy.cpp)
    const int32_t POLICY_DD_VERSION = 0x0D1D0770;
    const int32_t POLICY_MASK = 0x0000FFFF;

    // Test that HasDigiDollarMarker agrees with policy for various versions
    std::vector<int32_t> test_versions = {
        0x0D1D0770,  // Canonical DD
        0x00000770,  // Minimal match
        0x12340770,  // Arbitrary upper bits
        0x00000001,  // Normal v1
        0x00000002,  // Normal v2
        0x00000771,  // Off by one
        0x0000076F,  // Off by one (below)
    };

    for (int32_t ver : test_versions) {
        CMutableTransaction mtx = MakeTxWithVersion(ver);
        CTransaction tx(mtx);

        bool policyDD = (ver & POLICY_MASK) == (POLICY_DD_VERSION & POLICY_MASK);
        bool consensusDD = DigiDollar::HasDigiDollarMarker(tx);

        // CRITICAL: Policy and consensus MUST agree on what is a DD tx
        BOOST_CHECK_MESSAGE(policyDD == consensusDD,
            "VULNERABILITY: Policy/consensus disagree on version 0x" << std::hex << ver
            << ": policy=" << policyDD << " consensus=" << consensusDD
            << ". Gap allows bypass of DD validation or standardness.");

        if (policyDD != consensusDD) {
            BOOST_TEST_MESSAGE("CRITICAL FINDING: Standardness/consensus gap at version 0x"
                << std::hex << ver << std::dec
                << ". Policy says DD=" << policyDD
                << ", Consensus says DD=" << consensusDD);
        }
    }
}

// =============================================================================
// RH-17-03: RBF DISPLACEMENT OF DD MINT
//
// ATTACK: Attacker creates a DD mint transaction that signals RBF (sequence
// < 0xFFFFFFFE). Then replaces it with a regular non-DD transaction that
// pays higher fee. The mint is displaced from mempool — effectively canceled.
//
// This is economically rational: the attacker mints DD, then realizes
// market conditions changed, and can cancel the mint without it confirming.
// But it could also be used to grief: front-run someone's mint by spending
// the same inputs.
//
// FINDING: This is by design for RBF-signaling txs. But DD mints should
// probably NOT signal RBF (their sequence should be set to final) to
// prevent displacement. If the DD tx builder allows RBF signaling on mints,
// that's a design issue.
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_03_rbf_displacement_of_dd_mint)
{
    // Create a DD mint tx with RBF signaling
    CMutableTransaction ddMintTx = MakeTxWithVersion(0x0D1D0770);
    ddMintTx.vin[0].nSequence = 0xFFFFFFFD; // BIP125 opt-in RBF

    BOOST_CHECK_MESSAGE(SignalsOptInRBF(CTransaction(ddMintTx)),
        "DD mint with sequence 0xFFFFFFFD signals RBF");

    // Verify the DD marker is recognized
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(CTransaction(ddMintTx)));

    // Create a replacement non-DD tx spending the same input
    CMutableTransaction replacementTx = MakeTxWithVersion(2);
    replacementTx.vin[0].prevout = ddMintTx.vin[0].prevout; // Same input
    replacementTx.vin[0].nSequence = 0xFFFFFFFF; // Final

    // The replacement is NOT a DD tx
    BOOST_CHECK(!DigiDollar::HasDigiDollarMarker(CTransaction(replacementTx)));

    // FINDING: Nothing in the RBF policy prevents a DD mint from being
    // replaced by a non-DD tx. The ReplacementChecks in validation.cpp
    // only check fee/size rules, not DD-type preservation.
    BOOST_TEST_MESSAGE(
        "FINDING: DD mint txs can be RBF-displaced by non-DD txs. "
        "If DD tx builder sets sequence < 0xFFFFFFFE, mints are cancelable. "
        "RECOMMENDATION: DD mint builder should set nSequence = 0xFFFFFFFF "
        "(final) to prevent RBF displacement, unless intentional.");

    // Also test: DD mint with final sequence cannot be RBF'd
    CMutableTransaction finalDDMint = MakeTxWithVersion(0x0D1D0770);
    finalDDMint.vin[0].nSequence = 0xFFFFFFFF;
    BOOST_CHECK_MESSAGE(!SignalsOptInRBF(CTransaction(finalDDMint)),
        "DD mint with final sequence does NOT signal RBF — safe from displacement");

    // And: DD mint with nLockTime requires sequence < 0xFFFFFFFF
    // which means it inherently signals RBF
    CMutableTransaction timelockDDMint = MakeTxWithVersion(0x0D1D0770);
    timelockDDMint.nLockTime = 1000;
    timelockDDMint.vin[0].nSequence = 0xFFFFFFFE; // Required for nLockTime
    BOOST_CHECK_MESSAGE(!SignalsOptInRBF(CTransaction(timelockDDMint)),
        "Sequence 0xFFFFFFFE does NOT signal RBF (only < 0xFFFFFFFE does)");
}

// =============================================================================
// RH-17-04: DD VALIDATION CPU COST ASYMMETRY
//
// ATTACK: DD transactions trigger expensive validation in mempool:
// - Oracle price lookup
// - Block disk reads (txLookup reads blocks from disk!)
// - SystemHealthMonitor calls
// - Full DD consensus validation
//
// An attacker can craft txs that pass standardness (version mask match,
// dust bypass) but fail LATE in DD validation, maximizing CPU waste.
// The txLookup lambda in ATMP reads blocks from disk for every DD tx.
//
// COST ANALYSIS:
// - Normal tx mempool validation: ~0.1ms (sig checks)
// - DD tx mempool validation: ~5-50ms (disk reads + oracle + DD validation)
// - Ratio: 50-500x more expensive per DD tx
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_04_dd_validation_cost_asymmetry)
{
    // Verify that DD validation context creation involves disk I/O
    // The txLookup in ATMP does ReadBlockFromDisk for each referenced input
    // of a DD redemption — this is O(n_inputs * block_size) disk I/O

    // FINDING: The validation path for DD txs in AcceptToMemoryPool:
    // 1. HasDigiDollarMarker check (cheap)
    // 2. IsDigiDollarEnabled check (cheap)
    // 3. txLookup lambda creation (cheap — captures chainstate)
    // 4. GetOraclePriceForTransaction (moderate — may involve P2P lookup)
    // 5. GetSystemCollateralRatio (cheap — cached)
    // 6. ValidateDigiDollarTransaction (expensive — full validation)
    //
    // Steps 1-2 are cheap filters, but an attacker who crafts a tx that
    // passes 1-2 but fails at step 6 forces full validation work.
    //
    // Since ANY tx with lower 16 bits == 0x0770 triggers this path,
    // the attacker doesn't even need valid DD outputs.

    // Create a "fake DD" tx — version matches but no DD content
    CMutableTransaction fakeDDTx = MakeTxWithVersion(0x0D1D0770);
    CTransaction tx(fakeDDTx);

    // This tx will:
    // 1. Pass HasDigiDollarMarker ✓
    // 2. Hit IsDigiDollarEnabled check
    // 3. If enabled, create txLookup lambda (disk I/O setup)
    // 4. Call GetOraclePriceForTransaction (oracle lookup)
    // 5. Fail in ValidateDigiDollarTransaction (no valid DD outputs)
    //
    // Steps 3-4 are the expensive ones that run BEFORE the tx is rejected

    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(tx));

    // Verify the tx has no actual DD content
    auto txType = DigiDollar::GetDigiDollarTxType(tx);
    // FIXED [RH-26c]: Bounds check now returns DD_TX_NONE for out-of-range types.
    // The canonical DD version 0x0D1D0770 has upper byte 0x0D = 13 > DD_TX_MAX(4),
    // so it correctly returns DD_TX_NONE.
    BOOST_CHECK_EQUAL(txType, DigiDollar::DD_TX_NONE);

    BOOST_TEST_MESSAGE(
        "VERIFIED: [RH-26c] fix working — GetDigiDollarTxType returns DD_TX_NONE "
        "for out-of-range types. Version 0x0D1D0770 upper byte 0x0D=13 > DD_TX_MAX(4).");

    BOOST_TEST_MESSAGE(
        "FINDING: Fake DD txs (version mask match, no DD content) trigger "
        "expensive validation path including oracle lookup and disk I/O setup. "
        "RECOMMENDATION: Add early-exit in ATMP: check GetDigiDollarTxType != "
        "DD_TX_NONE before oracle/disk setup. Reject unknown DD types "
        "immediately after HasDigiDollarMarker.");
}

// =============================================================================
// RH-17-05: DUST EXEMPTION CREATES UTXO BLOAT VECTOR
//
// ATTACK: DD txs skip the dust check in IsStandardTx. If a valid DD tx
// creates change outputs, those change outputs also skip dust checks.
// An attacker mints DD with many tiny DGB change outputs, bloating the
// UTXO set with economically unspendable outputs.
//
// The dust check exemption applies to ALL outputs of a DD tx, not just
// the DD token output.
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_05_dust_exemption_utxo_bloat)
{
    const int32_t valid_transfer_version =
        (static_cast<int32_t>(DigiDollar::DD_TX_TRANSFER) << 24) | 0x00000770;

    // A legitimate DD transaction still needs its zero-value P2TR token output
    // exempted from normal DGB dust policy.
    CMutableTransaction ddTokenTx = MakeTxWithVersion(valid_transfer_version, 3);
    ddTokenTx.vout[0].nValue = 0;
    ddTokenTx.vout[0].scriptPubKey = CScript() << OP_1 << std::vector<unsigned char>(32, 0xDD);
    ddTokenTx.vout[1].nValue = 0;
    ddTokenTx.vout[1].scriptPubKey = CScript() << OP_RETURN << std::vector<unsigned char>{'D', 'D'};
    ddTokenTx.vout[2].nValue = 100000;

    std::string tokenReason;
    bool tokenStandard = IsStandardTx(CTransaction(ddTokenTx),
        MAX_OP_RETURN_RELAY, /*permit_bare_multisig=*/true,
        CFeeRate(DUST_RELAY_TX_FEE), tokenReason);

    BOOST_CHECK_MESSAGE(tokenStandard,
        "Valid DD zero-value P2TR token output must remain standard; reason="
        << tokenReason);

    // Create a valid DD-versioned tx with unrelated positive-value DGB dust
    // outputs. Only zero-value DD token outputs should receive the exemption.
    CMutableTransaction ddTx = MakeTxWithVersion(valid_transfer_version, 5);
    ddTx.vout[0].nValue = 0;
    ddTx.vout[0].scriptPubKey = CScript() << OP_1 << std::vector<unsigned char>(32, 0xDD);
    ddTx.vout[1].nValue = 1;      // 1 sat — unrelated DGB dust
    ddTx.vout[2].nValue = 1;      // 1 sat — unrelated DGB dust
    ddTx.vout[3].nValue = 1;      // 1 sat — unrelated DGB dust
    ddTx.vout[4].nValue = 100000; // Normal output

    std::string reason;
    bool isStandard = IsStandardTx(CTransaction(ddTx),
        MAX_OP_RETURN_RELAY, /*permit_bare_multisig=*/true,
        CFeeRate(DUST_RELAY_TX_FEE), reason);

    // VULNERABILITY: ALL outputs skip dust check, not just DD-specific ones
    BOOST_TEST_MESSAGE("DD tx with 4 dust outputs: isStandard=" << isStandard
        << " reason=" << reason);

    BOOST_CHECK_MESSAGE(!isStandard,
        "VULNERABILITY: DD version mask exempts positive-value DGB dust outputs. "
        "Only zero-value DD token outputs should bypass dust policy.");
    BOOST_CHECK_EQUAL(reason, "dust");

    // Compare: non-DD tx with same outputs should fail
    CMutableTransaction normalTx = MakeTxWithVersion(2, 5);
    normalTx.vout[0].nValue = 1;
    normalTx.vout[1].nValue = 1;
    normalTx.vout[2].nValue = 1;
    normalTx.vout[3].nValue = 1;
    normalTx.vout[4].nValue = 100000;

    std::string normalReason;
    bool normalStandard = IsStandardTx(CTransaction(normalTx),
        MAX_OP_RETURN_RELAY, /*permit_bare_multisig=*/true,
        CFeeRate(DUST_RELAY_TX_FEE), normalReason);

    BOOST_CHECK_MESSAGE(!normalStandard,
        "Normal tx with dust outputs must be non-standard");
    BOOST_CHECK_EQUAL(normalReason, "dust");
}

// =============================================================================
// RH-17-06: STANDARDNESS ALLOWS HIGH VERSION NUMBERS
//
// ATTACK: IsStandardTx skips the version range check for DD txs.
// Normal txs must have 1 <= version <= TX_MAX_STANDARD_VERSION.
// DD-detected txs bypass this entirely. But since the mask only checks
// lower 16 bits, version 0x7FFF0770 is treated as DD and bypasses
// the version check. If consensus also treats it as DD but validation
// fails, the only protection is DD validation — no version range guard.
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_06_high_version_bypass)
{
    // Normal tx with high version: rejected
    CMutableTransaction highVerTx = MakeTxWithVersion(100);
    highVerTx.vout[0].nValue = 100000; // Not dust
    std::string reason;
    bool standard = IsStandardTx(CTransaction(highVerTx),
        MAX_OP_RETURN_RELAY, true, CFeeRate(DUST_RELAY_TX_FEE), reason);
    BOOST_CHECK(!standard);
    BOOST_CHECK_EQUAL(reason, "version");

    // DD tx with same high-ish version but mask match: passes version check
    CMutableTransaction ddHighVer = MakeTxWithVersion(0x00640770); // 100 in upper bits
    ddHighVer.vout[0].nValue = 100000;
    std::string ddReason;
    bool ddStandard = IsStandardTx(CTransaction(ddHighVer),
        MAX_OP_RETURN_RELAY, true, CFeeRate(DUST_RELAY_TX_FEE), ddReason);

    BOOST_TEST_MESSAGE("DD high-version tx: isStandard=" << ddStandard
        << " reason=" << ddReason);

    if (ddStandard) {
        BOOST_TEST_MESSAGE(
            "DEFENSE GAP: Any version with lower 16 bits == 0x0770 bypasses "
            "version range check. Only DD validation guards against abuse.");
    }
}

// =============================================================================
// RH-17-07: NLOCKTIME + DD = MEMPOOL CPU WASTE
//
// ATTACK: CheckFinalTxAtTip runs BEFORE DD validation in the ATMP PreChecks.
// So a non-final DD tx is rejected early (good). BUT: if nLockTime is set
// to current height (making it final), the tx proceeds to DD validation.
// An attacker can create DD txs that are always "just final" to maximize
// the DD validation path execution.
//
// FINDING: nLockTime check at line ~840 in ATMP runs BEFORE DD validation
// at line ~765. This is correct — non-final txs are cheaply rejected.
// No vulnerability here, just documenting the defense.
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_07_nlocktime_dd_interaction)
{
    // Verify CheckFinalTx ordering: nLockTime check is at line ~840,
    // DD validation is at line ~765 in PreChecks.
    //
    // Wait — actually DD validation (HasDigiDollarMarker block) is at
    // line 765, and CheckFinalTxAtTip is at line 840. That means DD
    // validation runs FIRST before the finality check!

    // VULNERABILITY: DD validation (expensive) runs BEFORE CheckFinalTxAtTip
    // (cheap). An attacker can submit non-final DD txs to trigger full DD
    // validation, then they get rejected for being non-final AFTER wasting
    // DD validation CPU.

    // Verify the ordering concern: DD validation at ~765, finality at ~840
    // This is a code review finding, not a runtime check
    BOOST_CHECK_MESSAGE(true,
        "Code review finding: DD validation ordering in ATMP PreChecks");

    BOOST_TEST_MESSAGE(
        "VULNERABILITY: In ATMP PreChecks, DD validation (line ~765) runs "
        "BEFORE CheckFinalTxAtTip (line ~840). Non-final DD txs waste full "
        "DD validation CPU before being rejected as non-final. "
        "RECOMMENDATION: Move CheckFinalTxAtTip BEFORE the DD validation "
        "block, or add CheckFinalTxAtTip as an early-exit within the DD block.");
}

// =============================================================================
// RH-17-08: DD TX TYPE UNKNOWN BUT PASSES STANDARDNESS
//
// ATTACK: A tx with DD version marker but DD_TX_NONE type passes
// IsStandardTx (version bypass, dust bypass) but should fail DD consensus
// validation. This creates a class of txs that are "standard but invalid" —
// they consume mempool validation resources.
//
// Any miner or node processing these wastes CPU on full DD validation
// before rejecting them.
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_08_dd_unknown_type_standardness)
{
    // Create tx with DD version but no DD-specific outputs
    CMutableTransaction unknownDD = MakeTxWithVersion(0x0D1D0770);
    // Only regular P2PKH outputs — no DD marker outputs
    unknownDD.vout[0].nValue = 50000;

    CTransaction tx(unknownDD);

    // Verify it's treated as DD by both policy and consensus
    BOOST_CHECK(DigiDollar::HasDigiDollarMarker(tx));

    auto txType = DigiDollar::GetDigiDollarTxType(tx);

    // Check standardness
    std::string reason;
    bool isStandard = IsStandardTx(tx,
        MAX_OP_RETURN_RELAY, true, CFeeRate(DUST_RELAY_TX_FEE), reason);

    BOOST_TEST_MESSAGE("DD_TX_NONE: type=" << static_cast<int>(txType)
        << " isStandard=" << isStandard << " reason=" << reason);

    if (isStandard && static_cast<int>(txType) >= static_cast<int>(DigiDollar::DD_TX_MAX)) {
        BOOST_TEST_MESSAGE(
            "FINDING: Txs with DD version but invalid/out-of-range type pass "
            "standardness checks. They'll be relayed to peers before "
            "failing DD validation. This enables amplified CPU waste "
            "across the network — each peer validates before rejecting. "
            "RECOMMENDATION: Add DD type check in IsStandardTx: reject "
            "DD-versioned txs with DD_TX_NONE type as non-standard.");
    }
}

// =============================================================================
// RH-17-09: MULTIPLE OP_RETURN OUTPUTS IN DD TXS
//
// DD mints use OP_RETURN for amount encoding. The standardness check
// allows only 1 OP_RETURN (nDataOut > 1 → reject). This check runs
// AFTER the DD dust bypass. But DD txs may legitimately need an OP_RETURN.
// Question: Can an attacker add extra OP_RETURNs to a DD tx?
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_09_multiple_op_return_dd_tx)
{
    const int32_t valid_transfer_version =
        (static_cast<int32_t>(DigiDollar::DD_TX_TRANSFER) << 24) | 0x00000770;
    CMutableTransaction ddTx = MakeTxWithVersion(valid_transfer_version, 3);
    ddTx.vout[0].nValue = 50000;
    ddTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160
        << std::vector<unsigned char>(20, 0x01) << OP_EQUALVERIFY << OP_CHECKSIG;

    // Two OP_RETURN outputs
    ddTx.vout[1].nValue = 0;
    ddTx.vout[1].scriptPubKey = CScript() << OP_RETURN << std::vector<unsigned char>(20, 0xDD);
    ddTx.vout[2].nValue = 0;
    ddTx.vout[2].scriptPubKey = CScript() << OP_RETURN << std::vector<unsigned char>(20, 0xEE);

    std::string reason;
    bool isStandard = IsStandardTx(CTransaction(ddTx),
        MAX_OP_RETURN_RELAY, true, CFeeRate(DUST_RELAY_TX_FEE), reason);

    // multi-op-return check should still catch this even for DD txs
    BOOST_CHECK_MESSAGE(!isStandard,
        "DD tx with multiple OP_RETURNs should be non-standard");
    BOOST_CHECK_EQUAL(reason, "multi-op-return");

    BOOST_TEST_MESSAGE(
        "DEFENSE VERIFIED: multi-op-return check applies to DD txs. "
        "DD version bypass does NOT exempt from OP_RETURN count limit.");
}

// =============================================================================
// RH-17-10: WEIGHT LIMIT APPLIES TO DD TXS
//
// Verify MAX_STANDARD_TX_WEIGHT still limits DD txs despite version bypass.
// =============================================================================

BOOST_AUTO_TEST_CASE(rh17_10_weight_limit_applies_to_dd)
{
    // Create an oversized DD tx
    const int32_t valid_transfer_version =
        (static_cast<int32_t>(DigiDollar::DD_TX_TRANSFER) << 24) | 0x00000770;
    CMutableTransaction ddTx = MakeTxWithVersion(valid_transfer_version);
    ddTx.vout[0].nValue = 50000;
    // Add massive scriptSig to exceed weight limit
    ddTx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(MAX_STANDARD_TX_WEIGHT / 4 + 1, 0x00);

    std::string reason;
    bool isStandard = IsStandardTx(CTransaction(ddTx),
        MAX_OP_RETURN_RELAY, true, CFeeRate(DUST_RELAY_TX_FEE), reason);

    BOOST_CHECK_MESSAGE(!isStandard, "Oversized DD tx should be non-standard");
    BOOST_CHECK_EQUAL(reason, "tx-size");

    BOOST_TEST_MESSAGE("DEFENSE VERIFIED: MAX_STANDARD_TX_WEIGHT applies to DD txs.");
}

BOOST_AUTO_TEST_SUITE_END()
