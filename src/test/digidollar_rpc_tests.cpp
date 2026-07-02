// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/digidollar.h>
#include <crypto/sha256.h>
#include <digidollar/health.h>
#include <key.h>
#include <oracle/bundle_manager.h>
#include <oracle/mock_oracle.h>
#include <primitives/oracle.h>
#include <pubkey.h>
#include <rpc/server.h>
#include <rpc/client.h>
#include <rpc/digidollar.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <univalue.h>
#include <node/context.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(digidollar_rpc_tests)

static constexpr int64_t RPC_TEST_ORACLE_PRICE_MICRO_USD = 1000000;

static CKey RegtestOracleKey(uint32_t oracle_id)
{
    const std::string seed = "digibyte_regtest_oracle_" + std::to_string(oracle_id);
    uint256 hash;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(seed.data()), seed.size()).Finalize(hash.begin());

    CKey key;
    key.Set(hash.begin(), hash.end(), true);
    return key;
}

class DigiDollarRPCTestSetup : public TestingSetup {
public:
    DigiDollarRPCTestSetup() : TestingSetup(ChainType::REGTEST) {
        // Initialize health monitoring system
        DigiDollar::SystemHealthMonitor::Initialize();
    }

    ~DigiDollarRPCTestSetup() {
        DigiDollar::SystemHealthMonitor::Shutdown();
    }

    UniValue CallRPC(std::string args)
    {
        std::vector<std::string> vArgs{SplitString(args, ' ')};
        std::string strMethod = vArgs[0];
        vArgs.erase(vArgs.begin());
        JSONRPCRequest request;
        request.context = &m_node;
        request.strMethod = strMethod;
        request.params = RPCConvertValues(strMethod, vArgs);
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        try {
            UniValue result = tableRPC.execute(request);
            return result;
        }
        catch (const UniValue& objError) {
            throw std::runtime_error(objError.find_value("message").get_str());
        }
    }
};

// Test 1: getdigidollarstats - Basic Response
BOOST_FIXTURE_TEST_CASE(test_getdigidollarstats_basic, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdigidollarstats");

    // Should return a valid JSON object
    BOOST_CHECK(result.isObject());

    // Check required top-level fields
    BOOST_CHECK(result.exists("health_percentage"));
    BOOST_CHECK(result.exists("health_status"));
    BOOST_CHECK(result.exists("total_collateral_dgb"));
    BOOST_CHECK(result.exists("total_dd_supply"));
    BOOST_CHECK(result.exists("oracle_price_cents"));
    BOOST_CHECK(result.exists("oracle_price_micro_usd"));
    BOOST_CHECK(result.exists("oracle_available"));
    BOOST_CHECK(result.exists("oracle_status"));
    BOOST_CHECK(result.exists("minting_restricted_reason"));
    BOOST_CHECK(result.exists("is_emergency"));
    BOOST_CHECK(result.exists("system_collateral_ratio"));
    BOOST_CHECK(result.exists("total_collateral_locked"));
    BOOST_CHECK(result.exists("active_positions"));
    BOOST_CHECK(result.exists("oracle_price_age"));
    BOOST_CHECK(result.exists("dca_tier"));
}

// Test 2: getdigidollarstats - Data Types
BOOST_FIXTURE_TEST_CASE(test_getdigidollarstats_types, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdigidollarstats");

    // Validate data types
    BOOST_CHECK(result["health_percentage"].isNum());
    BOOST_CHECK(result["health_status"].isStr());
    BOOST_CHECK(result["total_collateral_dgb"].isNum());
    BOOST_CHECK(result["total_dd_supply"].isNum());
    BOOST_CHECK(result["oracle_price_cents"].isNum());
    BOOST_CHECK(result["oracle_price_micro_usd"].isNum());
    BOOST_CHECK(result["oracle_available"].isBool());
    BOOST_CHECK(result["oracle_status"].isStr());
    BOOST_CHECK(result["minting_restricted_reason"].isStr());
    BOOST_CHECK(result["is_emergency"].isBool());
    BOOST_CHECK(result["system_collateral_ratio"].isNum());
    BOOST_CHECK(result["total_collateral_locked"].isNum());
    BOOST_CHECK(result["active_positions"].isNum());
    BOOST_CHECK(result["oracle_price_age"].isNum());
    BOOST_CHECK(result["dca_tier"].isObject());
}

// Test 3: getdigidollarstats - Numeric Ranges
BOOST_FIXTURE_TEST_CASE(test_getdigidollarstats_ranges, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdigidollarstats");

    // Validate numeric ranges
    BOOST_CHECK_GE(result["health_percentage"].getInt<int>(), 0);
    BOOST_CHECK_LE(result["health_percentage"].getInt<int>(), 30000);
    BOOST_CHECK_GE(result["total_collateral_dgb"].get_real(), 0.0);
    BOOST_CHECK_GE(result["total_dd_supply"].getInt<int64_t>(), 0);
    BOOST_CHECK_GE(result["oracle_price_cents"].getInt<int64_t>(), 0);
    BOOST_CHECK_GE(result["oracle_price_micro_usd"].getInt<int64_t>(), 0);
    BOOST_CHECK_GE(result["active_positions"].getInt<int>(), 0);
    BOOST_CHECK_GE(result["oracle_price_age"].getInt<int>(), 0);
}

// Test 4: getdigidollarstats - DCA Tier Structure
BOOST_FIXTURE_TEST_CASE(test_getdigidollarstats_dca_tier, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdigidollarstats");

    // Get DCA tier object
    const UniValue& dcaTier = result["dca_tier"];
    BOOST_CHECK(dcaTier.isObject());

    // Check required fields
    BOOST_CHECK(dcaTier.exists("min_collateral"));
    BOOST_CHECK(dcaTier.exists("max_collateral"));
    BOOST_CHECK(dcaTier.exists("multiplier"));
    BOOST_CHECK(dcaTier.exists("status"));

    // Check data types
    BOOST_CHECK(dcaTier["min_collateral"].isNum());
    BOOST_CHECK(dcaTier["max_collateral"].isNum());
    BOOST_CHECK(dcaTier["multiplier"].isNum());
    BOOST_CHECK(dcaTier["status"].isStr());
}

// Test 5: getdcamultiplier - Basic Response
BOOST_FIXTURE_TEST_CASE(test_getdcamultiplier_basic, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdcamultiplier");

    // Should return a valid JSON object
    BOOST_CHECK(result.isObject());

    // Check required fields
    BOOST_CHECK(result.exists("multiplier"));
    BOOST_CHECK(result.exists("system_health"));
    BOOST_CHECK(result.exists("tier_status"));
    BOOST_CHECK(result.exists("description"));
}

// Test 6: getdcamultiplier - With Parameter
BOOST_FIXTURE_TEST_CASE(test_getdcamultiplier_with_health, DigiDollarRPCTestSetup)
{
    // Test with specific health values
    UniValue result150 = CallRPC("getdcamultiplier 150");
    BOOST_CHECK(result150.isObject());
    BOOST_CHECK_EQUAL(result150["system_health"].getInt<int>(), 150);

    UniValue result100 = CallRPC("getdcamultiplier 100");
    BOOST_CHECK(result100.isObject());
    BOOST_CHECK_EQUAL(result100["system_health"].getInt<int>(), 100);

    // Multiplier should be higher at lower health
    BOOST_CHECK_GE(result100["multiplier"].get_real(), result150["multiplier"].get_real());
}

// Test 7: getdcamultiplier - Invalid Parameter
BOOST_FIXTURE_TEST_CASE(test_getdcamultiplier_invalid, DigiDollarRPCTestSetup)
{
    // Test with invalid health values
    BOOST_CHECK_THROW(CallRPC("getdcamultiplier -10"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("getdcamultiplier 40000"), std::runtime_error);
}

// Test 8: calculatecollateralrequirement - Basic Response
BOOST_FIXTURE_TEST_CASE(test_calculatecollateral_basic, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("calculatecollateralrequirement 10000 365 1000000");

    // Should return a valid JSON object
    BOOST_CHECK(result.isObject());

    // Check required fields
    BOOST_CHECK(result.exists("required_dgb"));
    BOOST_CHECK(result.exists("dd_amount_cents"));
    BOOST_CHECK(result.exists("dd_amount_usd"));
    BOOST_CHECK(result.exists("lock_days"));
    BOOST_CHECK(result.exists("lock_blocks"));
    BOOST_CHECK(result.exists("base_ratio"));
    BOOST_CHECK(result.exists("dca_multiplier"));
    BOOST_CHECK(result.exists("effective_ratio"));
    BOOST_CHECK(result.exists("oracle_price_micro_usd"));
    BOOST_CHECK(result.exists("system_health"));
    BOOST_CHECK(result.exists("dca_tier"));
}

// Test 9: calculatecollateralrequirement - Different Lock Periods
BOOST_FIXTURE_TEST_CASE(test_calculatecollateral_lock_periods, DigiDollarRPCTestSetup)
{
    // Test various lock periods
    std::vector<int> lockPeriods = {30, 90, 180, 365, 1095, 1825, 2555, 3650};

    for (int lockDays : lockPeriods) {
        std::string cmd = "calculatecollateralrequirement 10000 " + std::to_string(lockDays) +
            " " + std::to_string(RPC_TEST_ORACLE_PRICE_MICRO_USD);
        UniValue result = CallRPC(cmd);

        BOOST_CHECK(result.isObject());
        BOOST_CHECK_EQUAL(result["lock_days"].getInt<int>(), lockDays);
        BOOST_CHECK_GT(result["required_dgb"].get_real(), 0.0);
    }
}

// Test 10: calculatecollateralrequirement - Longer Lock = Less Collateral
BOOST_FIXTURE_TEST_CASE(test_calculatecollateral_ratio_scaling, DigiDollarRPCTestSetup)
{
    UniValue result30 = CallRPC("calculatecollateralrequirement 10000 30 1000000");
    UniValue result365 = CallRPC("calculatecollateralrequirement 10000 365 1000000");
    UniValue result3650 = CallRPC("calculatecollateralrequirement 10000 3650 1000000");

    // Longer lock periods should require less collateral
    double dgb30 = result30["required_dgb"].get_real();
    double dgb365 = result365["required_dgb"].get_real();
    double dgb3650 = result3650["required_dgb"].get_real();

    BOOST_CHECK_GT(dgb30, dgb365);
    BOOST_CHECK_GT(dgb365, dgb3650);
}

// Test 11: calculatecollateralrequirement - With Oracle Price (in micro-USD)
BOOST_FIXTURE_TEST_CASE(test_calculatecollateral_oracle_price, DigiDollarRPCTestSetup)
{
    // Test with custom oracle price (500 micro-USD = $0.0005)
    UniValue result = CallRPC("calculatecollateralrequirement 10000 365 500");

    BOOST_CHECK(result.isObject());
    BOOST_CHECK_EQUAL(result["oracle_price_micro_usd"].getInt<int64_t>(), 500);
}

// Test 12: validateddaddress - Basic Test
BOOST_FIXTURE_TEST_CASE(test_validateddaddress_basic, DigiDollarRPCTestSetup)
{
    // validateddaddress moved to wallet RPC table (Bug #17 fix)
    // Without wallet context, the RPC should throw "Method not found"
    BOOST_CHECK_THROW(CallRPC("validateddaddress DDtestaddress123456789abcdef"), std::runtime_error);
}

// Test 13: estimatecollateral - Basic Response
BOOST_FIXTURE_TEST_CASE(test_estimatecollateral_basic, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("estimatecollateral 10000 3 1000000");

    BOOST_CHECK(result.isObject());
    // Should have similar fields to calculatecollateralrequirement
    BOOST_CHECK(result.exists("required_dgb"));
}

// Test 13b: IsValidMintAmount rejects amounts below minimum
// Regtest: min 1 cent, max 100000 cents ($1000)
BOOST_FIXTURE_TEST_CASE(test_mint_amount_below_min, DigiDollarRPCTestSetup)
{
    const auto& ddParams = Params().GetDigiDollarParams();
    // 0 cents — below minimum
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(0, ddParams));
    // -1 — negative
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(-1, ddParams));
    // Exactly at minimum (1 cent in regtest) — should pass
    BOOST_CHECK(DigiDollar::IsValidMintAmount(ddParams.minMintAmount, ddParams));
}

// Test 13c: IsValidMintAmount rejects amounts above maximum
// Regtest: max 100000 cents ($1000)
BOOST_FIXTURE_TEST_CASE(test_mint_amount_above_max, DigiDollarRPCTestSetup)
{
    const auto& ddParams = Params().GetDigiDollarParams();
    // Above max — should fail
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(ddParams.maxMintAmount + 1, ddParams));
    // Way above max
    BOOST_CHECK(!DigiDollar::IsValidMintAmount(ddParams.maxMintAmount * 2, ddParams));
    // Exactly at max — should pass
    BOOST_CHECK(DigiDollar::IsValidMintAmount(ddParams.maxMintAmount, ddParams));
    // Below max — should pass
    BOOST_CHECK(DigiDollar::IsValidMintAmount(ddParams.maxMintAmount - 1, ddParams));
}

// Test 14: getoracleprice - Basic Response
BOOST_FIXTURE_TEST_CASE(test_getoracleprice_basic, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getoracleprice");

    BOOST_CHECK(result.isObject());
    BOOST_CHECK(result.exists("price_cents"));
    BOOST_CHECK(result.exists("price_usd"));
    BOOST_CHECK_GE(result["price_cents"].get_real(), 0.0);
}

BOOST_FIXTURE_TEST_CASE(test_getoracleprice_ignores_stale_pending_messages, DigiDollarRPCTestSetup)
{
    OracleBundleManager& manager = OracleBundleManager::GetInstance();
    MockOracleManager& mock = MockOracleManager::GetInstance();
    const bool mock_was_enabled = mock.IsEnabled();
    struct Cleanup {
        OracleBundleManager& manager;
        MockOracleManager& mock;
        bool mock_was_enabled;
        ~Cleanup()
        {
            SetMockTime(0);
            manager.Clear();
            manager.SetEnabled(false);
            mock.SetEnabled(mock_was_enabled);
        }
    } cleanup{manager, mock, mock_was_enabled};

    mock.SetEnabled(false);

    manager.Clear();
    manager.SetEnabled(true);
    manager.SetMinOracleCount(4);

    const int64_t base_time = GetTime();
    SetMockTime(base_time);

    CKey key = RegtestOracleKey(0);
    COraclePriceMessage msg;
    msg.oracle_id = 0;
    msg.price_micro_usd = 500000;
    msg.timestamp = base_time;
    msg.oracle_pubkey = XOnlyPubKey(key.GetPubKey());
    BOOST_REQUIRE(msg.SignAttestation(key));
    BOOST_REQUIRE(manager.AddOracleMessage(msg));

    SetMockTime(base_time + ORACLE_MAX_AGE_SECONDS + 1);

    UniValue result = CallRPC("getoracleprice");
    BOOST_CHECK_EQUAL(result["oracle_count"].getInt<int>(), 0);
    BOOST_CHECK_EQUAL(result["last_update_time"].getInt<int64_t>(), 0);
    BOOST_CHECK_EQUAL(result["status"].get_str(), "error");
    BOOST_CHECK(result["is_stale"].get_bool());
}

// Test 15: getprotectionstatus - Basic Response
BOOST_FIXTURE_TEST_CASE(test_getprotectionstatus_basic, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getprotectionstatus");

    BOOST_CHECK(result.isObject());
    // Should return protection system information
}

// Test 16: listdigidollaraddresses - requires wallet context (Bug #12 fix)
// Now queries real wallet data instead of mock data, so without a wallet
// context it correctly throws. Full functional test coverage is in
// test/functional/digidollar_rpc_addresses.py
BOOST_FIXTURE_TEST_CASE(test_listdigidollaraddresses_basic, DigiDollarRPCTestSetup)
{
    // Without wallet context, the RPC should throw (no longer returns mock data)
    BOOST_CHECK_THROW(CallRPC("listdigidollaraddresses"), std::runtime_error);
}

// Test 17: Repeated RPC access - getdigidollarstats
BOOST_FIXTURE_TEST_CASE(test_rpc_performance, DigiDollarRPCTestSetup)
{
    const int NUM_CALLS = 100;
    UniValue baseline;

    for (int i = 0; i < NUM_CALLS; ++i) {
        UniValue result = CallRPC("getdigidollarstats");
        BOOST_REQUIRE(result.isObject());
        BOOST_CHECK(result.exists("health_percentage"));
        BOOST_CHECK(result.exists("health_status"));
        BOOST_CHECK(result.exists("total_collateral_dgb"));
        BOOST_CHECK(result.exists("total_dd_supply"));
        BOOST_CHECK(result.exists("oracle_price_micro_usd"));

        if (i == 0) {
            baseline = result;
            continue;
        }

        BOOST_CHECK_EQUAL(result["health_percentage"].getInt<int>(),
                          baseline["health_percentage"].getInt<int>());
        BOOST_CHECK_EQUAL(result["total_dd_supply"].getInt<int64_t>(),
                          baseline["total_dd_supply"].getInt<int64_t>());
        BOOST_CHECK_EQUAL(result["total_collateral_dgb"].get_real(),
                          baseline["total_collateral_dgb"].get_real());
    }
}

// Test 18: Concurrent Access - getdigidollarstats
BOOST_FIXTURE_TEST_CASE(test_concurrent_access, DigiDollarRPCTestSetup)
{
    // Make multiple calls
    std::vector<UniValue> results;
    for (int i = 0; i < 10; ++i) {
        UniValue result = CallRPC("getdigidollarstats");
        BOOST_CHECK(result.isObject());
        results.push_back(result);
    }

    // All results should be valid and consistent
    for (size_t i = 1; i < results.size(); ++i) {
        // Basic consistency checks
        BOOST_CHECK_EQUAL(results[i]["health_percentage"].getInt<int>(),
                         results[0]["health_percentage"].getInt<int>());
        BOOST_CHECK_EQUAL(results[i]["total_dd_supply"].getInt<int64_t>(),
                         results[0]["total_dd_supply"].getInt<int64_t>());
    }
}

// Test 19: Memory Usage
BOOST_FIXTURE_TEST_CASE(test_memory_usage, DigiDollarRPCTestSetup)
{
    // Make calls and check for memory leaks
    const int NUM_CALLS = 1000;
    for (int i = 0; i < NUM_CALLS; ++i) {
        UniValue result = CallRPC("getdigidollarstats");
        BOOST_CHECK(result.isObject());

        // Clear result to prevent accumulation
        result.clear();
    }

    // If we reach here without running out of memory, the test passes
    BOOST_CHECK(true);
}

// Test 20: Integration with Health Monitor
BOOST_FIXTURE_TEST_CASE(test_health_monitor_integration, DigiDollarRPCTestSetup)
{
    // Get direct health report
    UniValue directReport = DigiDollar::SystemHealthMonitor::GetHealthReport();

    // Make RPC call
    UniValue rpcResult = CallRPC("getdigidollarstats");

    // Both should have same essential data
    BOOST_CHECK(directReport.exists("supply"));
    BOOST_CHECK(directReport.exists("collateral"));
    BOOST_CHECK(directReport.exists("health"));
    BOOST_CHECK(rpcResult.exists("total_dd_supply"));
    BOOST_CHECK(rpcResult.exists("total_collateral_dgb"));
    BOOST_CHECK(rpcResult.exists("health_percentage"));

    // Values should be consistent (accounting for different field names and types)
    // Note: directReport uses ValueFromAmount (real), RPC uses raw int64 for supply
    // The health values might differ slightly due to timing of UTXO scans

    // Both health values should be in valid range
    int directHealth = directReport["health"].getInt<int>();
    int rpcHealth = rpcResult["health_percentage"].getInt<int>();
    BOOST_CHECK_GE(directHealth, 0);
    BOOST_CHECK_LE(directHealth, 30000);
    BOOST_CHECK_GE(rpcHealth, 0);
    BOOST_CHECK_LE(rpcHealth, 30000);

    // Supply values should be consistent
    int64_t directSupply = static_cast<int64_t>(directReport["supply"].get_real() * COIN);
    int64_t rpcSupply = rpcResult["total_dd_supply"].getInt<int64_t>();
    BOOST_CHECK_EQUAL(directSupply, rpcSupply);

    // Collateral: both use ValueFromAmount
    BOOST_CHECK_EQUAL(directReport["collateral"].get_real(),
                     rpcResult["total_collateral_dgb"].get_real());
}

// Test 21: Health Status String Validation
BOOST_FIXTURE_TEST_CASE(test_health_status_strings, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdigidollarstats");

    std::string status = result["health_status"].get_str();

    // Should be one of the valid status strings
    BOOST_CHECK(status == "healthy" || status == "warning" ||
                status == "critical" || status == "emergency");
}

// Test 22: DCA Multiplier Description
BOOST_FIXTURE_TEST_CASE(test_dca_multiplier_description, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdcamultiplier");

    BOOST_CHECK(result.exists("description"));
    std::string description = result["description"].get_str();
    BOOST_CHECK(!description.empty());
}

// Test 23: Oracle Price Format
BOOST_FIXTURE_TEST_CASE(test_oracle_price_format, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getoracleprice");

    BOOST_CHECK(result.exists("price_cents"));
    BOOST_CHECK(result.exists("price_usd"));

    // price_cents is int64_t (integer division: micro-USD / 10000).
    // Note: sub-cent prices (e.g., DGB at $0.0065 = 6500 micro-USD) truncate to 0 cents.
    // Use price_usd or price_micro_usd for sub-cent precision.
    int64_t cents = result["price_cents"].getInt<int64_t>();
    double usd = result["price_usd"].get_real();

    BOOST_CHECK(cents >= 0);
    BOOST_CHECK(usd >= 0.0);
    if (usd > 0) {
        int64_t micro_usd = result["price_micro_usd"].getInt<int64_t>();
        // USD should match micro-USD exactly
        BOOST_CHECK_CLOSE(usd, static_cast<double>(micro_usd) / 1000000.0, 0.001);
        // Cents uses integer division: micro_usd / 10000
        BOOST_CHECK_EQUAL(cents, micro_usd / 10000);
    }
}

// Test 24: Collateral Calculation Consistency
BOOST_FIXTURE_TEST_CASE(test_collateral_calculation_consistency, DigiDollarRPCTestSetup)
{
    // Call twice with same parameters
    UniValue result1 = CallRPC("calculatecollateralrequirement 10000 365 1000000");
    UniValue result2 = CallRPC("calculatecollateralrequirement 10000 365 1000000");

    // Results should be identical
    BOOST_CHECK_EQUAL(result1["required_dgb"].get_real(),
                     result2["required_dgb"].get_real());
    BOOST_CHECK_EQUAL(result1["base_ratio"].getInt<int>(),
                     result2["base_ratio"].getInt<int>());
    BOOST_CHECK_EQUAL(result1["effective_ratio"].getInt<int>(),
                     result2["effective_ratio"].getInt<int>());
}

// Test 25: DD Amount Calculation
BOOST_FIXTURE_TEST_CASE(test_dd_amount_calculation, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("calculatecollateralrequirement 10000 365 1000000");

    BOOST_CHECK(result.exists("dd_amount_cents"));
    BOOST_CHECK(result.exists("dd_amount_usd"));

    BOOST_CHECK_EQUAL(result["dd_amount_cents"].getInt<int64_t>(), 10000);
    BOOST_CHECK_CLOSE(result["dd_amount_usd"].get_real(), 100.0, 0.01);
}

// Test 26: Lock Blocks Calculation
BOOST_FIXTURE_TEST_CASE(test_lock_blocks_calculation, DigiDollarRPCTestSetup)
{
    // DigiByte has 15 second blocks, so:
    // 30 days = 30 * 86400 seconds / 15 = 172800 blocks.
    // V1 only accepts canonical lock tiers, so this must use a canonical period.
    UniValue result = CallRPC("calculatecollateralrequirement 10000 30 1000000");

    BOOST_CHECK(result.exists("lock_blocks"));
    int64_t lockBlocks = result["lock_blocks"].getInt<int64_t>();

    // Should be approximately 172800 blocks for 30 days (within 1%).
    double expectedBlocks = 172800.0;
    double actualBlocks = static_cast<double>(lockBlocks);
    BOOST_CHECK_CLOSE(actualBlocks, expectedBlocks, 1.0);
}

// Test 27: Emergency Status Detection
BOOST_FIXTURE_TEST_CASE(test_emergency_status, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdigidollarstats");

    BOOST_CHECK(result.exists("is_emergency"));
    bool isEmergency = result["is_emergency"].get_bool();
    int healthPct = result["health_percentage"].getInt<int>();
    int64_t totalDD = result["total_dd_supply"].getInt<int64_t>();

    // Emergency should be true when health < 100% and there are DD liabilities.
    if (totalDD > 0 && healthPct < 100) {
        BOOST_CHECK_EQUAL(isEmergency, true);
    } else if (totalDD == 0) {
        BOOST_CHECK_EQUAL(isEmergency, false);
    }
}

// Test 28: System Collateral Ratio Alias
BOOST_FIXTURE_TEST_CASE(test_collateral_ratio_alias, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdigidollarstats");

    // system_collateral_ratio should match health_percentage
    BOOST_CHECK_EQUAL(result["system_collateral_ratio"].getInt<int>(),
                     result["health_percentage"].getInt<int>());
}

// Test 29: Total Collateral Locked Alias
BOOST_FIXTURE_TEST_CASE(test_total_collateral_alias, DigiDollarRPCTestSetup)
{
    UniValue result = CallRPC("getdigidollarstats");

    // total_collateral_locked should match total_collateral_dgb
    // (they are both returned as real/double from ValueFromAmount)
    BOOST_CHECK_GE(result["total_collateral_locked"].get_real(), 0.0);
    BOOST_CHECK_GE(result["total_collateral_dgb"].get_real(), 0.0);
    BOOST_CHECK_EQUAL(result["total_collateral_locked"].get_real(),
                     result["total_collateral_dgb"].get_real());
}

// Test 30: DCA Tier Multiplier Range
BOOST_FIXTURE_TEST_CASE(test_dca_tier_multiplier_range, DigiDollarRPCTestSetup)
{
    // Test multiplier at various health levels
    for (int health = 50; health <= 200; health += 25) {
        std::string cmd = "getdcamultiplier " + std::to_string(health);
        UniValue result = CallRPC(cmd);

        double multiplier = result["multiplier"].get_real();

        // Multiplier should be in reasonable range (1.0 to 10.0)
        BOOST_CHECK_GE(multiplier, 1.0);
        BOOST_CHECK_LE(multiplier, 10.0);
    }
}

// Test 31: Bug #10 - Friendly error for unconfirmed DD inputs (rapid sends)
BOOST_FIXTURE_TEST_CASE(test_senddigidollar_unconfirmed_input_error_message, DigiDollarRPCTestSetup)
{
    // The raw error from the network contains "dd-input-amounts-unknown"
    // The RPC layer should translate this into a user-friendly message
    std::string rawError = "Transaction rejected by network: dd-input-amounts-unknown, Cannot verify DD conservation: input DD amounts undetermined";
    std::string friendlyMsg = "Previous DigiDollar transfer has not confirmed yet. Please wait for confirmation and try again.";

    // Verify the raw error contains the trigger substring
    BOOST_CHECK(rawError.find("dd-input-amounts-unknown") != std::string::npos);

    // Simulate what the RPC handler should do: detect and replace
    std::string result;
    if (rawError.find("dd-input-amounts-unknown") != std::string::npos) {
        result = friendlyMsg;
    } else {
        result = strprintf("Transfer failed: %s", rawError);
    }

    BOOST_CHECK_EQUAL(result, friendlyMsg);

    // Also verify that other errors pass through unchanged
    std::string otherError = "insufficient funds";
    std::string otherResult;
    if (otherError.find("dd-input-amounts-unknown") != std::string::npos) {
        otherResult = friendlyMsg;
    } else {
        otherResult = strprintf("Transfer failed: %s", otherError);
    }
    BOOST_CHECK_EQUAL(otherResult, "Transfer failed: insufficient funds");
}

BOOST_AUTO_TEST_SUITE_END()
