// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * RH-53: OP_CHECKPRICE is reserved/disabled and cannot read mock or live price
 *        state (post-fix regression test; pre-fix PoC for mock weaponization)
 *
 * Target:
 *   src/script/interpreter.cpp::EvalScript (OP_CHECKPRICE handler).
 *
 * POST-FIX invariant (this test):
 *   In active DigiDollar Tapscript, OP_CHECKPRICE consumes one stack operand
 *   and pushes vchFalse regardless of the wallet/node oracle cache, the test
 *   hook, or the witness value.
 *
 * PRE-FIX behavior (documented for the historical record):
 *   - A hardcoded static `GetMockOraclePrice()` returned 100000 µUSD ($0.10)
 *     unconditionally. Any DD-script using OP_CHECKPRICE compared against
 *     the mock, ignoring the real oracle entirely.
 *   - Root cause files: src/script/interpreter.cpp:433-438, :700.
 *
 * Why the fix matters (attacker model):
 *   - Malicious script author ships a contract advertising "unlocks at $X"
 *     that actually gates on the hardcoded $0.10 — user cannot detect the
 *     divergence from the opaque leaf script.
 *   - Any good-faith author using OP_CHECKPRICE ships a silently broken
 *     feature: either constant-false (real price != $0.10) or trivially
 *     spendable by anyone who pushes 100000 on the stack.
 *   - Oracle infrastructure is entirely bypassed for in-script price checks.
 *
 * This test was originally filed with BOOST_WARN_MESSAGE to allow the
 * suite to stay green while the fix direction was being decided. After the
 * decision to reserve/disable OP_CHECKPRICE, the assertions are flipped back
 * to BOOST_CHECK_MESSAGE so the test acts as a regression fixture.
 */

#include <boost/test/unit_test.hpp>

#include <consensus/amount.h>
#include <oracle/bundle_manager.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <test/util/setup_common.h>

namespace {

// Legacy mock value — kept as a literal here because the interpreter no
// longer exposes it. Pre-fix this was the hardcoded return of the static
// GetMockOraclePrice() function.
constexpr CAmount LEGACY_MOCK_ORACLE_PRICE = 100000;   // $0.10 µUSD
// Distinct "real" oracle price installed via UpdatePriceCache.
constexpr CAmount REAL_ORACLE_PRICE = 500000;          // $0.50 µUSD

// No-op signature checker; OP_CHECKPRICE never consults it.
class NullSigChecker : public BaseSignatureChecker {};

struct EvalOutcome {
    bool ok;
    ScriptError err;
    bool top_is_true;
    size_t stack_size;
};

EvalOutcome RunCheckPrice(CAmount witness_price, SigVersion sigversion)
{
    CScript script;
    script << CScriptNum(witness_price) << OP_CHECKPRICE;

    std::vector<std::vector<unsigned char>> stack;
    NullSigChecker checker;
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    ScriptExecutionData execdata;

    bool ok = EvalScript(stack, script, SCRIPT_VERIFY_DIGIDOLLAR,
                         checker, sigversion, execdata, &err);

    EvalOutcome out{ok, err, false, stack.size()};
    if (ok && !stack.empty()) {
        const auto& top = stack.back();
        for (size_t i = 0; i < top.size(); ++i) {
            if (top[i] != 0) {
                if (i == top.size() - 1 && top[i] == 0x80) {
                    out.top_is_true = false;
                    break;
                }
                out.top_is_true = true;
                break;
            }
        }
    }
    return out;
}

// RAII helper: install a price hook as a regression canary and restore it on
// scope exit. OP_CHECKPRICE must ignore this hook and still push false.
class ScopedOraclePrice {
public:
    explicit ScopedOraclePrice(CAmount price_micro_usd)
    {
        m_previous_hook = g_get_oracle_consensus_price;
        auto& mgr = OracleBundleManager::GetInstance();
        mgr.Clear();
        mgr.UpdatePriceCache(/*height=*/1, static_cast<uint64_t>(price_micro_usd));
        // Install the hook; OP_CHECKPRICE must not call it for a true result.
        g_get_oracle_consensus_price = []() -> CAmount {
            return OracleBundleManager::GetInstance().GetLatestPrice();
        };
    }
    ~ScopedOraclePrice()
    {
        OracleBundleManager::GetInstance().Clear();
        g_get_oracle_consensus_price = m_previous_hook;
    }
private:
    GetOracleConsensusPriceFn m_previous_hook{nullptr};
};

// RAII helper that clears the hook entirely — simulates the standalone
// libdigibyteconsensus.so build (hook never registered).
class ScopedNoOraclePrice {
public:
    ScopedNoOraclePrice()
    {
        m_previous_hook = g_get_oracle_consensus_price;
        OracleBundleManager::GetInstance().Clear();
        g_get_oracle_consensus_price = nullptr;
    }
    ~ScopedNoOraclePrice()
    {
        g_get_oracle_consensus_price = m_previous_hook;
    }
private:
    GetOracleConsensusPriceFn m_previous_hook{nullptr};
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(rh53_op_checkprice_mock_weaponization_tests, BasicTestingSetup)

// ---------------------------------------------------------------------------
// Scenario A (TAPSCRIPT sigversion):
//   Real oracle price is $0.50. Witness puts $0.50 on stack. Post-fix:
//   OP_CHECKPRICE still pushes FALSE because the opcode is disabled.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rh53_checkprice_must_consult_real_oracle_match_tapscript)
{
    ScopedOraclePrice seed(REAL_ORACLE_PRICE);
    auto& mgr = OracleBundleManager::GetInstance();
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), REAL_ORACLE_PRICE);

    EvalOutcome out = RunCheckPrice(REAL_ORACLE_PRICE, SigVersion::TAPSCRIPT);
    BOOST_TEST_MESSAGE("  real=" << REAL_ORACLE_PRICE
                       << " witness=" << REAL_ORACLE_PRICE
                       << " script_ok=" << out.ok
                       << " top_is_true=" << out.top_is_true
                       << " err=" << ScriptErrorString(out.err));

    BOOST_CHECK_MESSAGE(out.ok,
        "OP_CHECKPRICE must evaluate without error.");
    // DD-FINAL-005 / AR-0: OP_CHECKPRICE is now deterministically DISABLED (reserved). It no
    // longer consults any oracle price, so it can NEVER push TRUE — which makes the mock /
    // wall-clock weaponization this suite guards against structurally impossible.
    BOOST_CHECK_MESSAGE(!out.top_is_true,
        "OP_CHECKPRICE must push FALSE (disabled) regardless of the oracle price.");
}

// ---------------------------------------------------------------------------
// Scenario B (TAPSCRIPT sigversion, inverse):
//   Real oracle price is $0.50. Witness puts $0.10 (the legacy mock value).
//   Post-fix: OP_CHECKPRICE pushes FALSE for all witness values.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rh53_checkprice_must_consult_real_oracle_mismatch_tapscript)
{
    ScopedOraclePrice seed(REAL_ORACLE_PRICE);
    auto& mgr = OracleBundleManager::GetInstance();
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), REAL_ORACLE_PRICE);

    EvalOutcome out = RunCheckPrice(LEGACY_MOCK_ORACLE_PRICE, SigVersion::TAPSCRIPT);
    BOOST_TEST_MESSAGE("  real=" << REAL_ORACLE_PRICE
                       << " witness=" << LEGACY_MOCK_ORACLE_PRICE
                       << " script_ok=" << out.ok
                       << " top_is_true=" << out.top_is_true
                       << " err=" << ScriptErrorString(out.err));

    BOOST_CHECK_MESSAGE(out.ok,
        "OP_CHECKPRICE must evaluate without error even on mismatch.");
    BOOST_CHECK_MESSAGE(!out.top_is_true,
        "OP_CHECKPRICE must push FALSE regardless of witness or oracle price. "
        "Any script that matches against either the legacy $0.10 mock or a "
        "node-local live price would be an oracle-bypass/fork trap.");
}

// ---------------------------------------------------------------------------
// Scenario C (legacy sigversions):
//   DigiDollar opcodes are Tapscript-only. Legacy script versions must reject
//   OP_CHECKPRICE as a bad opcode even when the oracle hook is installed.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rh53_checkprice_rejected_outside_tapscript)
{
    ScopedOraclePrice seed(REAL_ORACLE_PRICE);
    auto& mgr = OracleBundleManager::GetInstance();
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), REAL_ORACLE_PRICE);

    for (SigVersion sigversion : {SigVersion::BASE, SigVersion::WITNESS_V0}) {
        EvalOutcome out = RunCheckPrice(REAL_ORACLE_PRICE, sigversion);
        BOOST_TEST_MESSAGE("  [legacy sigversion] real=" << REAL_ORACLE_PRICE
                           << " witness=" << REAL_ORACLE_PRICE
                           << " script_ok=" << out.ok
                           << " top_is_true=" << out.top_is_true
                           << " err=" << ScriptErrorString(out.err));

        BOOST_CHECK_MESSAGE(!out.ok,
            "OP_CHECKPRICE must be rejected outside Tapscript.");
        BOOST_CHECK_EQUAL(out.err, SCRIPT_ERR_BAD_OPCODE);
    }
}

// ---------------------------------------------------------------------------
// Scenario D (fail-closed when no oracle): hook unset (standalone consensus
// library build). OP_CHECKPRICE must push FALSE regardless of witness — no
// hardcoded fallback of any kind.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rh53_checkprice_fails_closed_with_no_oracle_hook)
{
    ScopedNoOraclePrice no_oracle;
    // Any witness value must produce FALSE because there is no oracle.
    for (CAmount witness : {CAmount{0}, CAmount{1}, LEGACY_MOCK_ORACLE_PRICE,
                            REAL_ORACLE_PRICE, CAmount{1'000'000},
                            CAmount{100'000'000}}) {
        EvalOutcome out = RunCheckPrice(witness, SigVersion::TAPSCRIPT);
        BOOST_TEST_MESSAGE("  no-oracle witness=" << witness
                           << " top_is_true=" << out.top_is_true);
        BOOST_CHECK(out.ok);
        BOOST_CHECK_MESSAGE(!out.top_is_true,
            "OP_CHECKPRICE must fail-closed (push FALSE) when no oracle "
            "consensus price is available. NO hardcoded fallback is allowed. "
            "witness=" << witness);
    }
}

// ---------------------------------------------------------------------------
// Scenario E (fail-closed when oracle returns 0): hook registered but
// cache empty → GetLatestPrice() returns 0 → opcode still fails closed.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rh53_checkprice_fails_closed_with_zero_oracle_price)
{
    auto prev_hook = g_get_oracle_consensus_price;
    OracleBundleManager::GetInstance().Clear();
    g_get_oracle_consensus_price = []() -> CAmount {
        return OracleBundleManager::GetInstance().GetLatestPrice();
    };
    // Cache is empty → GetLatestPrice() returns 0.
    BOOST_REQUIRE_EQUAL(OracleBundleManager::GetInstance().GetLatestPrice(), 0);

    for (CAmount witness : {CAmount{0}, CAmount{1}, LEGACY_MOCK_ORACLE_PRICE, REAL_ORACLE_PRICE}) {
        EvalOutcome out = RunCheckPrice(witness, SigVersion::TAPSCRIPT);
        BOOST_CHECK(out.ok);
        BOOST_CHECK_MESSAGE(!out.top_is_true,
            "OP_CHECKPRICE must fail-closed when oracle returns 0 even if "
            "witness also equals 0. Zero is not a valid oracle price. witness="
            << witness);
    }

    g_get_oracle_consensus_price = prev_hook;
}

// ---------------------------------------------------------------------------
// Scenario F (control): even when the installed hook price is exactly the
// legacy mock value and witness=100000, OP_CHECKPRICE must still push FALSE.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rh53_control_price_equals_legacy_mock_still_matches)
{
    ScopedOraclePrice seed(LEGACY_MOCK_ORACLE_PRICE);
    auto& mgr = OracleBundleManager::GetInstance();
    BOOST_REQUIRE_EQUAL(mgr.GetLatestPrice(), LEGACY_MOCK_ORACLE_PRICE);

    EvalOutcome out = RunCheckPrice(LEGACY_MOCK_ORACLE_PRICE, SigVersion::TAPSCRIPT);
    BOOST_CHECK(out.ok);
    // DD-FINAL-005 / AR-0: OP_CHECKPRICE disabled -> always FALSE even when the witness equals
    // the (legacy mock) price; the opcode is inert and cannot be made to match any price.
    BOOST_CHECK(!out.top_is_true);
}

BOOST_AUTO_TEST_SUITE_END()
