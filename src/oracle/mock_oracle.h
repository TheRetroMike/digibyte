// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_ORACLE_MOCK_ORACLE_H
#define DIGIBYTE_ORACLE_MOCK_ORACLE_H

#include <consensus/amount.h>
#include <key.h>
#include <primitives/oracle.h>
#include <sync.h>
#include <uint256.h>

#include <cstdint>
#include <map>

/**
 * Mock Oracle Manager for RegTest
 * Provides a singleton mock oracle for testing DigiDollar functionality in RegTest mode.
 * This allows testing of price-dependent features without requiring real oracle nodes.
 */
class MockOracleManager
{
private:
    static MockOracleManager* instance;

    CAmount mockPriceMicroUSD;      // Price in micro-USD (e.g., 6500 = $0.0065/DGB, 1000000 = $1.00/DGB)
    int64_t lastUpdateHeight;       // Height of last price update
    bool enabled;                   // Whether mock oracle is enabled

    //! Test oracle private keys (derived from SHA256("digibyte_regtest_oracle_N"))
    std::map<uint32_t, CKey> testOracleKeys;

    mutable RecursiveMutex cs_price;

    // Private constructor for singleton
    MockOracleManager();

    //! Initialize deterministic test oracle keys
    void InitTestKeys();

public:
    // Singleton access
    static MockOracleManager& GetInstance();

    // Delete copy constructor and assignment operator
    MockOracleManager(const MockOracleManager&) = delete;
    MockOracleManager& operator=(const MockOracleManager&) = delete;

    /**
     * Get current mock oracle price
     * @return Price in micro-USD (1,000,000 = $1.00)
     */
    CAmount GetCurrentPrice() const;

    /**
     * Set mock oracle price
     * @param price_micro_usd Price in micro-USD (1,000,000 = $1.00)
     */
    void SetMockPrice(CAmount price_micro_usd);
    void SetMockPrice(CAmount price_micro_usd, int64_t update_height);

    /**
     * Check if mock oracle is enabled
     * @return true if enabled
     */
    bool IsEnabled() const;

    /**
     * Enable or disable mock oracle
     * @param enable true to enable, false to disable
     */
    void SetEnabled(bool enable);

    /**
     * Get last update height
     * @return Block height of last price update
     */
    int64_t GetLastUpdateHeight() const;

    /**
     * Create a real MuSig2 v0x03 bundle for regtest.
     * Uses deterministic regtest oracle keys so tests exercise the same bundle
     * format that V1 accepts on chain.
     * @param height Block height the bundle will be mined at
     * @param block_time Timestamp to sign, or current time when 0
     * @return Signed MuSig2 bundle, or an empty bundle if signing fails
     */
    COracleBundle CreateMockMuSig2Bundle(int height, int64_t block_time = 0);

    /**
     * Simulate price volatility for testing
     * @param percentChange Percentage change (positive or negative)
     */
    void SimulateVolatility(int percentChange);
    void SimulateVolatility(int percentChange, int64_t update_height);

    /**
     * Get test private key for a specific oracle ID
     * @param oracle_id Oracle ID (0-4 for regtest)
     * @return CKey for the oracle, or invalid key if not found
     */
    CKey GetTestKey(uint32_t oracle_id) const;

    /**
     * Reset mock oracle to default state
     */
    void Reset();
};

#endif // DIGIBYTE_ORACLE_MOCK_ORACLE_H
