// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-60: Unhandled CScriptNum escape through the MEMPOOL path
 *        (Wave-8 adversarial PoC — MempoolAccept::PreChecks)
 *
 * Target:
 *   src/digidollar/validation.cpp:1199  — CScriptNum(data, true)
 *   src/digidollar/validation.cpp:1206  — CScriptNum(data, true, 8)
 *
 * Reached from MEMPOOL via:
 *   src/validation.cpp:817
 *     MemPoolAccept::PreChecks() calls
 *       DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state)
 *   without any surrounding try/catch.
 *
 * Reached from ValidateDigiDollarTransaction via:
 *   src/digidollar/validation.cpp:2025
 *     case DD_TX_TRANSFER: return ValidateTransferTransaction(tx, ctx, state);
 *   — the only try/catch in ValidateDigiDollarTransaction (lines 1972..1976)
 *   wraps ONLY the GetDigiDollarTxType() lookup; the type-specific dispatch
 *   is unwrapped.
 *
 * Difference from priors:
 *   - Prior C4 (DIGIDOLLAR_BUG_HUNT_REPORT.md) cites lines 1199/1206 but
 *     characterises the caller as ConnectBlock via src/validation.cpp:2933.
 *     That is the BLOCK path: fix direction is "wrap in ConnectBlock".
 *   - Prior W1-H-02 (rh52) is about CheckBlock → BIP34 coinbase push, a
 *     DIFFERENT CScriptNum site in the oracle validators.
 *   - This PoC is about the MEMPOOL path. Even after ConnectBlock is
 *     wrapped, the mempool entry (src/validation.cpp:817) is still
 *     exception-unsafe. A single unauthenticated peer TX drops straight
 *     into AcceptToMemoryPool → PreChecks → ValidateDigiDollarTransaction
 *     → ValidateTransferTransaction → throw.
 *   - That throw escapes PreChecks, AcceptSingleTransaction, and
 *     AcceptToMemoryPool. The call sites in net_processing.cpp
 *     (lines 1612, 1699, 3382, 4671, 5402) are all INSIDE the outer
 *     ProcessMessage try/catch at src/net_processing.cpp:6388 — so the
 *     exception is caught, BUT the misbehavior path that would have
 *     banned the peer is NEVER reached: there is no state.Invalid() →
 *     no MaybePunishNodeForTx() scoring, no peer ban. The attacker gets
 *     unlimited retries at zero P2P cost, and can cycle the attack tx
 *     through many peers to burn CPU on tx deserialization and coin
 *     caching (PreChecks lines 884..916 fetch coins before the DD
 *     validation runs, so the attacker also gets free disk I/O).
 *
 * Exploit (DoS):
 *   Craft a DD TRANSFER tx (nVersion set with DD marker + type 2)
 *   whose OP_RETURN is:
 *     OP_RETURN <"DD"> <non-minimal txType encoding> ...
 *   ValidateTransferTransaction reaches validation.cpp:1199 with
 *   data = {0x02, 0x00} (type 2 re-encoded non-minimally). CScriptNum
 *   with fRequireMinimal=true throws scriptnum_error. No catch in the
 *   call stack until net_processing's generic catch at :6388. Result:
 *     - peer NOT banned
 *     - peer CAN re-send the same tx forever
 *     - each round burns tx-deserialize + FetchCoin + lock acquisition
 *     - mempool sequence advances (line 965) before the throw? No —
 *       throw is AFTER PreChecks returns to AcceptSingleTransaction,
 *       but BEFORE the entry is committed.
 *     - logging at net_processing.cpp:6388 does not rate-limit, so a
 *       flood of distinct malformed txs also floods the node log.
 *
 * Blast radius:
 *   Pre-activation: gated at validation.cpp:783 → returns invalid cleanly,
 *   no throw. Activation gate is BIP9 — mainnet Phase 3 is ALWAYS_ACTIVE
 *   from genesis on regtest/testnet, ordinary BIP9 on mainnet. After
 *   activation (regtest today, mainnet post Phase-3) every DD-marked
 *   TRANSFER tx with a malformed OP_RETURN reaches the throw.
 *
 * This test proves the exploit WITHOUT touching consensus code. It
 * demonstrates:
 *   (1) CScriptNum(data, true) throws on the crafted push.
 *   (2) ValidateDigiDollarTransaction propagates the throw (no catch).
 *   (3) The expected fix (try/catch around ValidateDigiDollarTransaction
 *       at src/validation.cpp:817 OR at the dispatch switch at
 *       validation.cpp:2020) converts the throw to state.Invalid().
 *
 * Severity: HIGH (DoS, peer-ban bypass, log flood, CPU amplification).
 */

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/validation.h>
#include <digidollar/validation.h>
#include <digidollar/digidollar.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>

namespace {

// Build a DD TRANSFER transaction whose OP_RETURN embeds a
// non-minimally-encoded txType push. The push {0x02, 0x00} decodes
// to integer 2 (the TRANSFER type), but the trailing 0x00 makes it
// non-minimal. CScriptNum(..., fRequireMinimal=true) throws
// scriptnum_error.
CMutableTransaction BuildPoisonTransferTx(const std::vector<unsigned char>& poison_type_push)
{
    CMutableTransaction mtx;
    // Set DD version marker + TRANSFER type (so the dispatcher routes
    // to ValidateTransferTransaction which contains the unwrapped
    // CScriptNum call at validation.cpp:1199).
    mtx.nVersion = MakeDigiDollarVersion(DD_TX_TRANSFER);

    CTxIn in;
    in.prevout = COutPoint(uint256::ONE, 0);
    mtx.vin.push_back(in);

    // Attacker-controlled OP_RETURN:
    //   OP_RETURN <"DD"> <poison_type_push>
    // The poison push is placed where ValidateTransferTransaction
    // reads it at validation.cpp:1198 and feeds into
    // CScriptNum(data, true) at :1199.
    CScript opret;
    opret << OP_RETURN;
    std::vector<unsigned char> dd_marker = {'D', 'D'};
    opret << dd_marker;
    opret << poison_type_push;      // <— this is the poison
    mtx.vout.push_back(CTxOut(0, opret));

    // The transfer validator also reads P2TR outputs; include a plain
    // OP_1 placeholder so the function reaches OP_RETURN scanning
    // before any P2TR-specific exit. It does not matter here because
    // the throw at :1199 fires before any P2TR output is inspected.
    CScript p2tr;
    p2tr << OP_1 << std::vector<unsigned char>(32, 0x00);
    mtx.vout.push_back(CTxOut(0, p2tr));

    return mtx;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(rh60_mempool_dd_scriptnum_escape_tests, RegTestingSetup)

// rh60-01: prove CScriptNum(data, true) at validation.cpp:1199 throws
// on a non-minimally-encoded type push. This is the raw primitive the
// PoC depends on.
BOOST_AUTO_TEST_CASE(rh60_01_scriptnum_nonminimal_raises)
{
    // Non-minimal encoding of integer 2: {0x02, 0x00}
    std::vector<unsigned char> poison = {0x02, 0x00};

    bool threw = false;
    std::string what;
    try {
        CScriptNum txType(poison, /*fRequireMinimal=*/true);
        (void)txType;
    } catch (const scriptnum_error& e) {
        threw = true;
        what = e.what();
    }

    BOOST_CHECK_MESSAGE(threw,
        "CScriptNum with fRequireMinimal=true must reject non-minimal "
        "encoding {0x02,0x00}. This is the primitive exploited by the "
        "ValidateTransferTransaction CScriptNum at validation.cpp:1199.");
    BOOST_TEST_MESSAGE("RH-60-01 scriptnum_error what='" << what << "'");
}

// rh60-02: PRIMARY PoC. Drive ValidateDigiDollarTransaction directly
// (the exact function MemPoolAccept::PreChecks calls at
// src/validation.cpp:817) with a poison TRANSFER tx. Confirm the
// scriptnum_error escapes the DD validator: there is NO catch around
// the dispatch at src/digidollar/validation.cpp:2025.
//
// Expected today (pre-patch): exception propagates out.
// Expected post-patch (wrap at validation.cpp:817 OR at
// digidollar/validation.cpp:2020 switch): clean state.Invalid(...)
// with reject reason like "transfer-bad-op-return-encoding".
BOOST_AUTO_TEST_CASE(rh60_02_atmp_path_dd_validator_must_not_throw)
{
    std::vector<unsigned char> poison = {0x02, 0x00}; // non-minimal "2"
    CMutableTransaction mtx = BuildPoisonTransferTx(poison);
    CTransaction tx(mtx);

    // Sanity: the tx must look like a DD TRANSFER to the dispatcher.
    BOOST_REQUIRE(DigiDollar::HasDigiDollarMarker(tx));
    BOOST_REQUIRE_EQUAL(static_cast<int>(DigiDollar::GetDigiDollarTxType(tx)),
                       static_cast<int>(DD_TX_TRANSFER));

    // Build the same ValidationContext shape that
    // MemPoolAccept::PreChecks builds at src/validation.cpp:806-815.
    // Values other than the chainparams ref are irrelevant for this PoC
    // since the throw fires before any context-driven branch.
    DigiDollar::ValidationContext ctx(
        /*height=*/1,
        /*price_micro_usd=*/100000,
        /*collateral=*/150,
        /*chainParams=*/*CChainParams::RegTest({}),
        /*coins_view=*/nullptr,
        /*skip_oracle=*/true,
        /*tx_lookup=*/nullptr,
        /*pool=*/nullptr);

    TxValidationState state;
    bool returned = false;
    bool threw_scriptnum = false;
    bool threw_other = false;
    std::string caught_what;

    try {
        returned = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
    } catch (const scriptnum_error& e) {
        threw_scriptnum = true;
        caught_what = e.what();
    } catch (const std::exception& e) {
        threw_other = true;
        caught_what = e.what();
    } catch (...) {
        threw_other = true;
        caught_what = "unknown";
    }

    BOOST_TEST_MESSAGE("  ValidateDigiDollarTransaction returned=" << returned
                       << " threw_scriptnum=" << threw_scriptnum
                       << " threw_other=" << threw_other
                       << " what='" << caught_what << "'"
                       << " reject='" << state.GetRejectReason() << "'");

    BOOST_CHECK_MESSAGE(!threw_scriptnum && !threw_other,
        "RH-60: ValidateDigiDollarTransaction propagated a CScriptNum "
        "exception out of the DD validator. The MemPoolAccept::PreChecks "
        "caller at src/validation.cpp:817 has no try/catch. Fix direction: "
        "wrap the DD dispatch at digidollar/validation.cpp:2020 in "
        "try/catch → state.Invalid(TxValidationResult::TX_CONSENSUS, "
        "\"bad-dd-op-return-encoding\") OR wrap the single callsite at "
        "validation.cpp:817.");

    if (!threw_scriptnum && !threw_other) {
        // Post-patch expectation anchor.
        BOOST_CHECK_MESSAGE(!returned,
            "Post-patch: malformed DD TRANSFER tx must be rejected with "
            "a clean state.Invalid(), not silently accepted.");
        BOOST_CHECK_MESSAGE(!state.GetRejectReason().empty(),
            "Post-patch: state.GetRejectReason() must be set.");
    }
}

// rh60-03: confirm the same throw reaches us via the 8-byte CScriptNum
// at validation.cpp:1206 (amount push). Same primitive, different
// site — ensures the defender's fix covers BOTH calls, not just :1199.
//
// Construction: OP_RETURN <"DD"> <minimal 0x02> <non-minimal 8-byte
// amount> ... . The first CScriptNum at :1199 succeeds (txType=2),
// then the while-loop body at :1206 hits the poisoned amount push.
BOOST_AUTO_TEST_CASE(rh60_03_amount_push_also_throws)
{
    CMutableTransaction mtx;
    mtx.nVersion = MakeDigiDollarVersion(DD_TX_TRANSFER);
    CTxIn in;
    in.prevout = COutPoint(uint256::ONE, 0);
    mtx.vin.push_back(in);

    CScript opret;
    opret << OP_RETURN;
    opret << std::vector<unsigned char>{'D', 'D'};
    opret << std::vector<unsigned char>{0x02};            // txType=2, minimal
    // Poison amount push: non-minimal 8-byte encoding of value 1
    // (trailing 0x00 forbidden with fRequireMinimal=true).
    opret << std::vector<unsigned char>{0x01, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00};
    mtx.vout.push_back(CTxOut(0, opret));

    CScript p2tr;
    p2tr << OP_1 << std::vector<unsigned char>(32, 0x00);
    mtx.vout.push_back(CTxOut(0, p2tr));

    CTransaction tx(mtx);
    BOOST_REQUIRE(DigiDollar::HasDigiDollarMarker(tx));

    DigiDollar::ValidationContext ctx(
        /*height=*/1,
        /*price_micro_usd=*/100000,
        /*collateral=*/150,
        /*chainParams=*/*CChainParams::RegTest({}),
        /*coins_view=*/nullptr,
        /*skip_oracle=*/true,
        /*tx_lookup=*/nullptr,
        /*pool=*/nullptr);

    TxValidationState state;
    bool threw_scriptnum = false;
    std::string caught_what;
    bool returned = false;
    try {
        returned = DigiDollar::ValidateDigiDollarTransaction(tx, ctx, state);
    } catch (const scriptnum_error& e) {
        threw_scriptnum = true;
        caught_what = e.what();
    } catch (const std::exception& e) {
        caught_what = std::string("other:") + e.what();
    } catch (...) {
        caught_what = "unknown";
    }

    BOOST_TEST_MESSAGE("  [amount-push site]"
                       << " returned=" << returned
                       << " threw_scriptnum=" << threw_scriptnum
                       << " what='" << caught_what << "'"
                       << " reject='" << state.GetRejectReason() << "'");

    BOOST_CHECK_MESSAGE(!threw_scriptnum,
        "RH-60-03: CScriptNum at src/digidollar/validation.cpp:1206 "
        "(amount push) also escapes as an uncaught exception. The "
        "defender fix must cover BOTH :1199 and :1206.");
}

BOOST_AUTO_TEST_SUITE_END()
