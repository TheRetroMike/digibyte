// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Regression tests for the RC30 "254 GB debug.log" incident.
//
// Two stuck DigiDollar TRANSFER transactions in the mempool caused four
// LogPrintf() call sites to fire thousands of times per second on the same
// revalidation path, filling the disk and crashing the host. This suite
// codifies the invariant that those specific hot-path messages must be
// gated behind BCLog::DIGIDOLLAR so they stay silent unless the operator
// opts in with -debug=digidollar.
//

#include <logging.h>
#include <oracle/bundle_manager.h>
#include <oracle/mock_oracle.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs_test = std::filesystem;

namespace {

// Mirrors the LogSetup helper in logging_tests.cpp: redirects the process
// log to a per-test temp file so we can read back what the code wrote.
struct HotPathLogSetup : public BasicTestingSetup {
    fs::path prev_log_path;
    fs::path tmp_log_path;
    bool prev_reopen_file;
    bool prev_print_to_file;
    bool prev_log_timestamps;
    bool prev_log_threadnames;
    bool prev_log_sourcelocations;
    std::unordered_map<BCLog::LogFlags, BCLog::Level> prev_category_levels;
    BCLog::Level prev_log_level;
    uint32_t prev_categories;

    HotPathLogSetup()
        : prev_log_path{LogInstance().m_file_path},
          tmp_log_path{m_args.GetDataDirBase() / "tmp_hot_path_debug.log"},
          prev_reopen_file{LogInstance().m_reopen_file},
          prev_print_to_file{LogInstance().m_print_to_file},
          prev_log_timestamps{LogInstance().m_log_timestamps},
          prev_log_threadnames{LogInstance().m_log_threadnames},
          prev_log_sourcelocations{LogInstance().m_log_sourcelocations},
          prev_category_levels{LogInstance().CategoryLevels()},
          prev_log_level{LogInstance().LogLevel()},
          prev_categories{LogInstance().GetCategoryMask()}
    {
        LogInstance().m_file_path = tmp_log_path;
        LogInstance().m_reopen_file = true;
        LogInstance().m_print_to_file = true;
        LogInstance().m_log_timestamps = false;
        LogInstance().m_log_threadnames = false;
        LogInstance().m_log_sourcelocations = false;
        LogInstance().SetLogLevel(BCLog::Level::Debug);
        LogInstance().SetCategoryLogLevel({});
        // Start with no categories enabled — the default production state.
        LogInstance().DisableCategory(BCLog::LogFlags::ALL);
    }

    ~HotPathLogSetup()
    {
        LogInstance().m_file_path = prev_log_path;
        LogPrintf("Sentinel log to reopen log file\n");
        LogInstance().m_print_to_file = prev_print_to_file;
        LogInstance().m_reopen_file = prev_reopen_file;
        LogInstance().m_log_timestamps = prev_log_timestamps;
        LogInstance().m_log_threadnames = prev_log_threadnames;
        LogInstance().m_log_sourcelocations = prev_log_sourcelocations;
        LogInstance().SetLogLevel(prev_log_level);
        LogInstance().SetCategoryLogLevel(prev_category_levels);
        LogInstance().DisableCategory(BCLog::LogFlags::ALL);
        // Restore previously-enabled categories by flag-by-flag re-enable.
        for (uint32_t bit = 0; bit < 32; ++bit) {
            const auto flag = static_cast<BCLog::LogFlags>(1U << bit);
            if (prev_categories & (1U << bit)) {
                LogInstance().EnableCategory(flag);
            }
        }
    }

    std::string ReadLog() const
    {
        std::ifstream file{tmp_log_path};
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
};

// Resolve the src/ root from __FILE__ so the invariant test can read
// the hot-path .cpp files regardless of the cwd chosen by `make check`.
fs_test::path ResolveSrcRoot()
{
    fs_test::path p = fs_test::absolute(fs_test::path(__FILE__)).parent_path().parent_path();
    if (fs_test::exists(p / "digidollar" / "validation.cpp")) return p;
    fs_test::path cwd = fs_test::current_path();
    for (fs_test::path d = cwd; !d.empty(); d = d.parent_path()) {
        if (fs_test::exists(d / "digidollar" / "validation.cpp")) return d;
        if (fs_test::exists(d / "src" / "digidollar" / "validation.cpp")) return d / "src";
        if (d == d.parent_path()) break;
    }
    return {};
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(digidollar_hot_path_logging_tests, HotPathLogSetup)

// RED→GREEN behaviour test: exercises bundle_manager.cpp's
// GetCurrentOraclePriceMicroUSD() no-price fallback (the line that fired
// in the 254 GB incident). Pre-fix this wrote a line every call via
// LogPrintf; post-fix it must be silent unless DIGIDOLLAR is enabled.
BOOST_AUTO_TEST_CASE(get_current_oracle_price_no_price_log_is_gated_by_default)
{
    // Force the mock oracle to report "no price" so we fall through to the
    // real OracleBundleManager path; clear the manager so its cache is empty
    // and GetLatestPrice() returns 0, which triggers the fallback log.
    MockOracleManager::GetInstance().SetMockPrice(0);
    OracleBundleManager::GetInstance().Clear();

    // Simulate a hot-path revalidation loop. Pre-fix this writes ~50 lines;
    // post-fix the category gate keeps the log empty.
    for (int i = 0; i < 50; ++i) {
        (void)OracleIntegration::GetCurrentOraclePriceMicroUSD();
    }

    // Flush by forcing a reopen and emitting a sentinel line.
    LogInstance().m_reopen_file = true;
    LogPrintf("hot-path-log-flush-sentinel\n");

    const std::string log = ReadLog();
    BOOST_CHECK_MESSAGE(
        log.find("No oracle price available in GetCurrentOraclePriceMicroUSD") == std::string::npos,
        "RC30 incident regression: 'No oracle price available' must be gated behind -debug=digidollar. "
        "Found " << std::count(log.begin(), log.end(), '\n') << " log lines; first 200 chars: "
        << log.substr(0, 200));
}

// Complement: when the operator opts into -debug=digidollar, the message
// must still appear so debugging visibility is not lost.
BOOST_AUTO_TEST_CASE(get_current_oracle_price_no_price_log_visible_when_digidollar_enabled)
{
    LogInstance().EnableCategory(BCLog::DIGIDOLLAR);

    MockOracleManager::GetInstance().SetMockPrice(0);
    OracleBundleManager::GetInstance().Clear();

    (void)OracleIntegration::GetCurrentOraclePriceMicroUSD();

    LogInstance().m_reopen_file = true;
    LogPrintf("hot-path-log-flush-sentinel\n");

    const std::string log = ReadLog();
    BOOST_CHECK_MESSAGE(
        log.find("No oracle price available in GetCurrentOraclePriceMicroUSD") != std::string::npos,
        "Oracle no-price message must still be emitted when DIGIDOLLAR category is enabled. "
        "Log first 200 chars: " << log.substr(0, 200));
}

// Source-invariant test: the four messages that filled the 254 GB
// debug.log must be emitted through LogPrint(BCLog::DIGIDOLLAR, ...)
// rather than a raw LogPrintf(). This catches regressions that the two
// behaviour tests above can't reach (e.g. the stale-cache branch inside
// OracleBundleManager::GetLatestPrice, whose internal state requires
// friend access to force from a unit test).
BOOST_AUTO_TEST_CASE(rc30_hot_path_logs_are_gated_in_source)
{
    const fs_test::path src_root = ResolveSrcRoot();
    BOOST_REQUIRE_MESSAGE(!src_root.empty(),
                          "could not locate src/ from __FILE__ or cwd");

    struct Site {
        fs_test::path file;
        std::string marker;
    };
    const std::vector<Site> sites = {
        {src_root / "digidollar" / "validation.cpp",
         "DigiDollar: Validating %s transaction (txid: %s)"},
        {src_root / "digidollar" / "validation.cpp",
         "Could not determine input DD amounts for conservation check"},
        {src_root / "oracle" / "bundle_manager.cpp",
         "Rejecting stale cached price %lld micro-USD"},
        {src_root / "oracle" / "bundle_manager.cpp",
         "No oracle price available in GetCurrentOraclePriceMicroUSD"},
        // ATMP wrapper site missed by the original gating pass. Fires from
        // AcceptToMemoryPool → BroadcastTransaction → Dandelion stempool on
        // every rejected DD tx; matches shenger's repro log line exactly.
        {src_root / "validation.cpp",
         "DigiDollar: Transaction validation failed (txid: %s): %s"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: TickEpochSession h=%d epoch=%d state=%d is_oracle=%d"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: Step 1 - local_ids.size()=%zu for epoch %d"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: Step 1 - all_oracle_ids.size()=%zu"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: Step 1 - key aggregation succeeded for %zu oracle IDs"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: Skipping oracle %d epoch %d - already broadcast"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: Skipping oracle %d - GetOracleNode returned null"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: Skipping oracle %d - invalid private key"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: oracle %d pubkey size=%d hex=%s"},
        {src_root / "oracle" / "signing_orchestrator.cpp",
         "Oracle: Skipping oracle %d - secp256k1_ec_pubkey_parse failed"},
    };

    for (const auto& site : sites) {
        std::ifstream f(site.file);
        BOOST_REQUIRE_MESSAGE(f.is_open(), "cannot open " << site.file.string());
        std::stringstream buf;
        buf << f.rdbuf();
        const std::string content = buf.str();

        const size_t marker_pos = content.find(site.marker);
        BOOST_REQUIRE_MESSAGE(marker_pos != std::string::npos,
                              "marker not found in " << site.file.filename().string()
                              << ": '" << site.marker << "'");

        // Inspect the ~200 chars preceding the marker. The enclosing
        // logging macro must be LogPrint(BCLog::DIGIDOLLAR, ...), never a
        // raw LogPrintf(.
        const size_t scan_start = marker_pos > 200 ? marker_pos - 200 : 0;
        const std::string before = content.substr(scan_start, marker_pos - scan_start);

        const size_t logprint_pos = before.rfind("LogPrint(BCLog::DIGIDOLLAR");
        const size_t logprintf_pos = before.rfind("LogPrintf(");

        const bool wrapped_in_logprint = logprint_pos != std::string::npos;
        const bool wrapped_in_logprintf =
            logprintf_pos != std::string::npos &&
            (!wrapped_in_logprint || logprintf_pos > logprint_pos);

        BOOST_CHECK_MESSAGE(
            !wrapped_in_logprintf,
            "RC30 incident regression: '" << site.marker << "' in "
            << site.file.filename().string()
            << " is emitted through LogPrintf — must use LogPrint(BCLog::DIGIDOLLAR, ...)");
        BOOST_CHECK_MESSAGE(
            wrapped_in_logprint,
            "'" << site.marker << "' in " << site.file.filename().string()
            << " must be wrapped in LogPrint(BCLog::DIGIDOLLAR, ...)");
    }
}

BOOST_AUTO_TEST_SUITE_END()
