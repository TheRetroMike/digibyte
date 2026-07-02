# DigiDollar Oracle Implementation Plan
**Two-Phase Strategy for Secure, Decentralized Price Feeds**

*Version 1.0*
*Created: 2025-01-04*
*Status: Implementation Roadmap*

---

## Executive Summary

DigiDollar requires a robust oracle system to provide real-time DGB/USD pricing for minting, redemption, and protection mechanisms (DCA, ERR, volatility detection). This document outlines a **two-phase implementation strategy** that prioritizes rapid deployment while building toward maximum decentralization.

**Phase 1 (MVP)**: Hardcoded trusted oracle nodes - Enables DigiDollar launch in 4-6 weeks
**Phase 2 (Final)**: Economic staking with miner validation - Achieves full decentralization

### Current Status
- DigiDollar implementation: **82% complete**
- Oracle framework: **100% ready** (mock implementation in place)
- Missing: **Real exchange API integration** and **P2P broadcasting**
- Timeline to production: **4-6 weeks (Phase 1)** → **10-14 additional weeks (Phase 2)**

### Key Innovation: DigiDollar Staking for Oracles
Unlike traditional oracle staking (e.g., Chainlink stakes LINK), DigiDollar oracles will stake **DigiDollars themselves**, creating a powerful economic flywheel:
- Oracles must mint DD to participate → locks DGB supply
- Oracles earn triple rewards: transaction fees + DGB appreciation + reputation bonuses
- Creates organic demand for DigiDollar while securing the oracle network

---

## Table of Contents

1. [Strategic Overview](#strategic-overview)
2. [Phase 1: Hardcoded Oracle Nodes (MVP)](#phase-1-hardcoded-oracle-nodes-mvp)
3. [Phase 2: Economic Staking & Decentralization](#phase-2-economic-staking--decentralization)
4. [Security Analysis](#security-analysis)
5. [Economic Incentive Design](#economic-incentive-design)
6. [Implementation Timeline](#implementation-timeline)
7. [Risk Mitigation](#risk-mitigation)
8. [Success Metrics](#success-metrics)

---

## Strategic Overview

### Design Principles

1. **Speed to Market**: Phase 1 must launch DigiDollar within 4-6 weeks
2. **Progressive Decentralization**: Start simple, evolve to full decentralization
3. **Economic Security**: Staking creates skin-in-the-game for oracle operators
4. **Dual Security Layers**: Economic staking + DigiByte's 5-algo PoW validation
5. **Sustainable Economics**: Fee pool scales with DigiDollar adoption

### The Two-Phase Vision

```
Phase 1: HARDCODED ORACLES (Weeks 1-6)
├─ 30 trusted oracle nodes (hardcoded in chainparams.cpp)
├─ 8-of-15 Schnorr signature threshold consensus
├─ Real exchange API integration (Binance, Coinbase, Kraken, etc.)
├─ P2P oracle message broadcasting
├─ Basic fee pool (0.05% of mints → oracle rewards)
└─ Goal: DigiDollar operational with functional pricing

         ↓ MIGRATION (Weeks 7-14)

Phase 2: ECONOMIC STAKING (Weeks 7-20)
├─ Stake 10,000 DigiDollars to become oracle
├─ Permissionless participation (anyone can join)
├─ Slashing for misbehavior (lose staked DD)
├─ Miner validation (DigiByte PoW validates bundles)
├─ Dispute resolution (DGB holder voting)
├─ Enhanced rewards (reputation bonuses)
└─ Goal: Fully decentralized, attack-resistant oracle network
```

### Why This Approach Works

✅ **Fast Launch**: Phase 1 gets DigiDollar to market quickly
✅ **Proven Security**: Hardcoded oracles work (similar to DNS seeds)
✅ **Smooth Migration**: Can transition from Phase 1 → Phase 2 gradually
✅ **Economic Alignment**: Phase 2 creates sustainable long-term incentives
✅ **Defense in Depth**: Phase 2 adds multiple security layers

---

## Phase 1: Hardcoded Oracle Nodes (MVP)

### Overview

Phase 1 deploys a curated set of 30 hardcoded oracle nodes to provide immediate price feed functionality. This is the **minimum viable implementation** needed to launch DigiDollar on mainnet.

### Architecture

```cpp
// File: src/chainparams.cpp

class CMainParams : public CChainParams {
    // Hardcoded oracle nodes (30 total, 15 active per epoch)
    std::vector<OracleNode> vOracleNodes = {
        // Tier 1: Foundation & Core Team (High Trust)
        {"oracle1.digibyte.io", "xpub661MyMwAqRbcFW31..."},
        {"oracle2.digibyte.io", "xpub661MyMwAqRbcGczj..."},

        // Tier 2: Major Exchanges (Economic Alignment)
        {"oracle-binance.digidollar.org", "xpub661MyMwAqRbcFtXg..."},
        {"oracle-coinbase.digidollar.org", "xpub661MyMwAqRbcGhAp..."},
        {"oracle-kraken.digidollar.org", "xpub661MyMwAqRbcEoKm..."},

        // Tier 3: Mining Pools (Security Alignment)
        {"oracle-oceanmining.digidollar.org", "xpub661MyMwAqRbcDoVp..."},
        {"oracle-antpool.digidollar.org", "xpub661MyMwAqRbcCpTn..."},

        // Tier 4: Community Leaders (Geographic Diversity)
        {"oracle-asia1.digidollar.org", "xpub661MyMwAqRbcBqMf..."},
        {"oracle-europe1.digidollar.org", "xpub661MyMwAqRbcArLj..."},
        {"oracle-americas1.digidollar.org", "xpub661MyMwAqRbcDsKp..."},

        // ... 20 more oracles for redundancy
    };

    // Epoch configuration
    consensus.nOracleEpochBlocks = 1440;  // ~6 hours per epoch (240 blocks/algo × 6)
    consensus.nOracleUpdateInterval = 4;   // Update every ~1 minute
    consensus.nOracleThreshold = 8;        // 8-of-15 signatures required
    consensus.nMaxPriceDeviation = 5;      // 5% max variance allowed
};
```

### Core Components

#### 1. Exchange API Integration

**File: `/src/oracle/exchange.cpp`**

```cpp
class ExchangeAPIManager {
public:
    /**
     * Fetch DGB/USD price from multiple exchanges
     * Returns median price after outlier filtering
     */
    CAmount FetchMedianPrice() {
        std::vector<CAmount> prices;

        // Fetch from each exchange with error handling
        if (auto price = FetchBinance()) prices.push_back(*price);
        if (auto price = FetchCoinbase()) prices.push_back(*price);
        if (auto price = FetchKraken()) prices.push_back(*price);
        if (auto price = FetchKuCoin()) prices.push_back(*price);
        if (auto price = FetchBittrex()) prices.push_back(*price);

        // Must have at least 3 sources
        if (prices.size() < 3) {
            throw std::runtime_error("Insufficient price sources");
        }

        // Filter outliers using MAD (Median Absolute Deviation)
        auto filtered = FilterOutliers(prices);

        // Return median of filtered prices
        std::sort(filtered.begin(), filtered.end());
        return filtered[filtered.size() / 2];
    }

private:
    std::optional<CAmount> FetchBinance();
    std::optional<CAmount> FetchCoinbase();
    std::optional<CAmount> FetchKraken();
    std::optional<CAmount> FetchKuCoin();
    std::optional<CAmount> FetchBittrex();

    std::vector<CAmount> FilterOutliers(const std::vector<CAmount>& prices);
};
```

**Supported Exchanges:**
1. **Binance** (DGB/USDT) - Highest volume, primary source
2. **Coinbase Pro** (DGB/USD) - Direct USD pair, regulated exchange
3. **Kraken** (DGB/USD) - Established exchange, good liquidity
4. **KuCoin** (DGB/USDT) - Asian market coverage
5. **Bittrex** (DGB/USD) - US market coverage

#### 2. P2P Oracle Broadcasting

**File: `/src/oracle/broadcast.cpp`**

```cpp
struct COraclePriceMessage {
    CAmount price;              // DGB/USD in cents (100 = $1.00)
    uint64_t timestamp;         // Unix timestamp
    CPubKey oraclePubKey;       // Oracle identity
    uint256 exchangeHash;       // Merkle root of exchange prices (proof)
    std::vector<unsigned char> schnorrSig;  // Schnorr signature

    SERIALIZE_METHODS(COraclePriceMessage, obj) {
        READWRITE(obj.price, obj.timestamp, obj.oraclePubKey,
                  obj.exchangeHash, obj.schnorrSig);
    }
};

class OracleBroadcaster {
public:
    /**
     * Create and broadcast oracle price message
     * Called every 60 seconds by oracle operators
     */
    void BroadcastPrice() {
        // Fetch median price from exchanges
        CAmount price = m_exchangeAPI.FetchMedianPrice();

        // Create price message
        COraclePriceMessage msg;
        msg.price = price;
        msg.timestamp = GetTime();
        msg.oraclePubKey = m_oracleKey.GetPubKey();
        msg.exchangeHash = ComputeExchangeProof();

        // Sign with Schnorr signature
        uint256 msgHash = msg.GetMessageHash();
        m_oracleKey.SignSchnorr(msgHash, msg.schnorrSig);

        // Broadcast to P2P network
        g_connman->ForEachNode([&](CNode* node) {
            node->PushMessage(NetMsgType::ORACLEPRICE, msg);
        });

        LogPrintf("Oracle price broadcast: %s DGB/USD\n", FormatMoney(price));
    }
};
```

#### 3. Oracle Selection & Validation

**File: `/src/consensus/oracle.cpp`**

```cpp
class OracleConsensusManager {
public:
    /**
     * Select 15 active oracles for current epoch
     * Deterministic selection based on block hash
     */
    std::vector<CPubKey> SelectActiveOracles(int nHeight) {
        // Get epoch seed from block hash
        int epochStart = (nHeight / EPOCH_LENGTH) * EPOCH_LENGTH;
        uint256 epochSeed = chainActive[epochStart]->GetBlockHash();

        // Shuffle all 30 oracles using epoch seed
        std::vector<OracleNode> shuffled = chainparams.vOracleNodes;
        std::mt19937 rng(epochSeed.GetUint64(0));
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        // Return first 15 oracles
        std::vector<CPubKey> selected;
        for (int i = 0; i < 15; i++) {
            selected.push_back(shuffled[i].pubkey);
        }

        return selected;
    }

    /**
     * Validate and aggregate oracle prices
     * Requires 8-of-15 valid signatures
     */
    CAmount GetConsensusPrice(
        const std::vector<COraclePriceMessage>& prices,
        int nHeight
    ) {
        auto activeOracles = SelectActiveOracles(nHeight);

        // Verify signatures and filter to active oracles only
        std::vector<CAmount> validPrices;
        for (const auto& msg : prices) {
            // Check if oracle is active for this epoch
            if (!IsActiveOracle(msg.oraclePubKey, activeOracles)) continue;

            // Verify Schnorr signature
            if (!msg.VerifySignature()) continue;

            // Check timestamp freshness (within 5 minutes)
            if (GetTime() - msg.timestamp > 300) continue;

            validPrices.push_back(msg.price);
        }

        // Require at least 8 valid signatures
        if (validPrices.size() < 8) {
            throw std::runtime_error("Insufficient oracle consensus");
        }

        // Return median price (resistant to outliers)
        std::sort(validPrices.begin(), validPrices.end());
        return validPrices[validPrices.size() / 2];
    }
};
```

### Configuration

**For Oracle Operators:**

```bash
# digibyte.conf

# Enable oracle mode
oracle=1

# Exchange API keys (read-only, no trading permissions)
oracleexchanges=binance,coinbase,kraken,kucoin,bittrex

# Binance
oracleapikey_binance=YOUR_BINANCE_API_KEY
oracleapisecret_binance=YOUR_BINANCE_SECRET

# Coinbase Pro
oracleapikey_coinbase=YOUR_COINBASE_KEY
oracleapisecret_coinbase=YOUR_COINBASE_SECRET

# Kraken
oracleapikey_kraken=YOUR_KRAKEN_KEY
oracleapisecret_kraken=YOUR_KRAKEN_SECRET

# KuCoin
oracleapikey_kucoin=YOUR_KUCOIN_KEY
oracleapisecret_kucoin=YOUR_KUCOIN_SECRET

# Bittrex
oracleapikey_bittrex=YOUR_BITTREX_KEY
oracleapisecret_bittrex=YOUR_BITTREX_SECRET

# Broadcasting interval (seconds)
oraclebroadcastinterval=60

# Minimum exchanges for valid median
oracleminexchanges=3
```

### Security in Phase 1

#### Attack Resistance

| Attack Vector | Mitigation | Effectiveness |
|---------------|------------|---------------|
| **Single Oracle Compromise** | 8-of-15 threshold + median pricing | High - would need 8 oracles |
| **Exchange Manipulation** | Median of 5 exchanges + outlier filtering | High - would need 3+ exchanges |
| **Sybil Attack** | Hardcoded set prevents fake oracles | Perfect - only 30 known oracles |
| **Network DoS** | Multiple oracles provide redundancy | High - can lose 22 oracles |
| **Stale Prices** | 5-minute freshness check | Medium - relies on oracle uptime |

#### Limitations of Phase 1

❌ **Centralization**: 30 hardcoded nodes = limited decentralization
❌ **Trust Required**: Must trust initial oracle selection
❌ **Slow Expansion**: Adding oracles requires software update
❌ **No Economic Penalty**: Bad oracles can't be slashed
❌ **Social Coordination**: Community must agree on oracle operators

**Phase 2 addresses all these limitations.**

### Phase 1 Deliverables

✅ **Functional Oracle System**
- Real-time DGB/USD pricing from 5+ exchanges
- 30 operational oracle nodes
- 8-of-15 Schnorr threshold consensus
- P2P price message broadcasting
- Median aggregation with outlier filtering

✅ **DigiDollar Integration**
- Minting uses real oracle prices
- DCA/ERR/Volatility protection systems operational
- GUI displays current oracle price
- RPC command: `getoracleprice`

✅ **Basic Economic Incentives**
- 0.05% of mint transactions → oracle fee pool
- Weekly distribution to active oracles
- Simple pro-rata allocation

✅ **Production Ready**
- Testnet deployment and testing
- Mainnet launch ready
- Documentation and operator guides

### Timeline: 4-6 Weeks

**Week 1-2: Exchange API Integration**
- Implement HTTP/CURL clients for 5 exchanges
- JSON parsing and error handling
- Rate limiting and retry logic
- Outlier filtering algorithms (MAD, IQR, Z-score)

**Week 3: P2P Oracle Protocol**
- Oracle price message format
- Schnorr signature signing/verification
- P2P message propagation
- Replay attack prevention

**Week 4: Consensus Integration**
- Epoch-based oracle selection
- 8-of-15 threshold validation
- Median price calculation
- Historical price tracking (for TWAP/volatility)

**Week 5: DigiDollar Integration**
- Replace mock oracle with real implementation
- Update minting/redemption to use real prices
- GUI price display
- RPC command implementation

**Week 6: Testing & Deployment**
- Testnet deployment
- Functional testing with 30 oracles
- Attack simulation testing
- Documentation and operator onboarding
- **Mainnet launch ready**

---

## Phase 2: Economic Staking & Decentralization

### Overview

Phase 2 transforms the oracle system from a trusted set into a **permissionless, economically secured network**. Anyone can become an oracle by staking DigiDollars, creating strong economic incentives for honest behavior.

### The Revolutionary Approach: Stake DigiDollars, Not DGB

**Key Innovation**: Unlike Chainlink (stake LINK) or other oracles, DigiDollar oracles stake **DigiDollars themselves**. This creates a powerful economic flywheel:

```
User Wants to Become Oracle
    ↓
Mint 2,500 DigiDollars
    ↓
Lock 75,000 DGB as collateral (3-year term, 300% ratio at $0.01/DGB)
    ↓
Stake 2,500 DD to register as oracle
    ↓
Earn Triple Rewards:
    1. Oracle transaction fees (0.05% of mints)
    2. DGB collateral appreciation (10x+ potential)
    3. Reputation bonuses (2-5x multipliers for top performers)
    ↓
More oracles = More DD minted = More DGB locked
    ↓
Reduced DGB supply = Price support = Better collateralization
    ↓
Stronger DigiDollar = More adoption = Higher oracle rewards
    ↓
POSITIVE FEEDBACK LOOP
```

### Architecture

#### 1. Oracle Registration via DigiDollar Staking

**File: `/src/oracle/registration.cpp`**

```cpp
struct OracleRegistration {
    CPubKey oraclePubKey;          // Oracle identity
    COutPoint ddStakeTxOut;        // Points to staked DD output
    CAmount ddAmountStaked;         // 10,000 DD minimum
    int64_t lockHeight;             // When stake was locked
    int64_t unlockHeight;           // When stake can be withdrawn (~3 months)
    std::vector<std::string> exchanges;  // Exchange API endpoints
    std::string hostname;           // Optional hostname for identification
    uint64_t reputationScore;       // 0-100 performance score
    bool isSlashed;                 // Penalty flag
};

class OracleRegistrationManager {
public:
    /**
     * Register new oracle by staking DigiDollars
     * @param ddAmount Amount to stake (minimum 10,000 DD)
     * @param lockBlocks Lock period in blocks (minimum ~3 months)
     * @param exchanges List of exchange API endpoints
     * @return Registration transaction hash
     */
    uint256 RegisterOracle(
        CAmount ddAmount,
        int64_t lockBlocks,
        const std::vector<std::string>& exchanges
    ) {
        // Validate minimum stake
        if (ddAmount < MIN_ORACLE_STAKE) {
            throw std::runtime_error("Insufficient stake amount");
        }

        if (lockBlocks < MIN_LOCK_BLOCKS) {
            throw std::runtime_error("Lock period too short");
        }

        // Create oracle registration transaction
        CMutableTransaction tx;
        tx.nVersion = DD_TX_VERSION | (DD_TX_ORACLE_REGISTER << 16);

        // Input: DigiDollar UTXOs to stake
        SelectDDCoinsForStake(ddAmount, tx);

        // Output 0: Staked DD (P2TR with timelock)
        CScript stakeScript = CreateOracleStakeScript(
            m_oracleKey.GetPubKey(),
            chainActive.Height() + lockBlocks
        );
        tx.vout.push_back(CTxOut(0, stakeScript));  // 0 DGB value

        // Output 1: OP_RETURN with oracle metadata
        CScript metadata;
        metadata << OP_RETURN << OP_ORACLE_REGISTER;
        metadata << SerializeOracleMetadata(exchanges, hostname);
        tx.vout.push_back(CTxOut(0, metadata));

        // Sign and broadcast
        SignTransaction(tx);
        return BroadcastTransaction(tx);
    }

    /**
     * Check if oracle is active and eligible
     */
    bool IsOracleActive(const CPubKey& oraclePubKey) {
        auto reg = GetOracleRegistration(oraclePubKey);
        if (!reg) return false;

        // Check stake is still locked
        if (chainActive.Height() >= reg->unlockHeight) return false;

        // Check not slashed
        if (reg->isSlashed) return false;

        // Check minimum stake maintained
        if (GetOracleStakeAmount(reg->ddStakeTxOut) < MIN_ORACLE_STAKE) {
            return false;
        }

        return true;
    }

private:
    static constexpr CAmount MIN_ORACLE_STAKE = 10000 * CENT;  // 10,000 DD
    static constexpr int MIN_LOCK_BLOCKS = 26280;  // ~3 months (~6hrs × 4/day × 90)
};
```

#### 2. Slashing Mechanism

**File: `/src/oracle/slashing.cpp`**

```cpp
enum SlashingReason {
    SLASH_OFFLINE_7_DAYS = 1,      // 5% penalty (500 DD)
    SLASH_PRICE_DEVIATION = 2,      // 25% penalty (2,500 DD)
    SLASH_REPEATED_BAD_DATA = 3,    // 100% penalty (full stake)
    SLASH_COLLUSION = 4             // 100% penalty + permanent blacklist
};

struct SlashingProposal {
    uint256 proposalId;
    CPubKey targetOracle;
    SlashingReason reason;
    CAmount proposedPenalty;
    std::vector<uint256> evidence;  // Block hashes, transaction IDs, price data
    CPubKey proposerPubKey;
    int64_t challengePeriodEnd;     // 48-hour challenge period
    std::map<CPubKey, bool> votes;  // DGB holder votes (weighted by stake)
};

class OracleSlashingManager {
public:
    /**
     * Create slashing proposal for oracle misconduct
     */
    uint256 CreateSlashingProposal(
        const CPubKey& targetOracle,
        SlashingReason reason,
        const std::vector<uint256>& evidence
    ) {
        SlashingProposal proposal;
        proposal.proposalId = GetRandHash();
        proposal.targetOracle = targetOracle;
        proposal.reason = reason;
        proposal.proposedPenalty = CalculatePenalty(reason);
        proposal.evidence = evidence;
        proposal.proposerPubKey = m_proposerKey.GetPubKey();
        proposal.challengePeriodEnd = chainActive.Height() + 11520;  // 48 hours

        // Broadcast to network
        BroadcastSlashingProposal(proposal);

        return proposal.proposalId;
    }

    /**
     * Execute slashing if vote passes
     * Requires 60% approval weighted by DGB stake
     */
    bool ExecuteSlashing(const uint256& proposalId) {
        auto proposal = GetProposal(proposalId);

        // Check challenge period expired
        if (chainActive.Height() < proposal.challengePeriodEnd) {
            throw std::runtime_error("Challenge period not expired");
        }

        // Calculate vote outcome
        CAmount votesFor = 0;
        CAmount votesAgainst = 0;

        for (const auto& [voterPubKey, voteFor] : proposal.votes) {
            CAmount voterStake = GetDGBStake(voterPubKey);
            if (voteFor) votesFor += voterStake;
            else votesAgainst += voterStake;
        }

        // Require 60% to execute
        CAmount totalVotes = votesFor + votesAgainst;
        if (votesFor * 100 / totalVotes < 60) {
            // Slashing rejected - penalize false proposer
            PenalizeFalseProposer(proposal.proposerPubKey, 1000 * CENT);
            return false;
        }

        // Execute slashing
        SlashOracle(proposal.targetOracle, proposal.proposedPenalty);

        // Reward proposer (10% of slashed amount)
        RewardProposer(proposal.proposerPubKey, proposal.proposedPenalty / 10);

        return true;
    }

private:
    void SlashOracle(const CPubKey& oracle, CAmount penalty) {
        auto reg = GetOracleRegistration(oracle);
        CAmount currentStake = GetOracleStakeAmount(reg.ddStakeTxOut);
        CAmount newStake = currentStake - penalty;

        if (newStake <= 0) {
            // Full stake slashed - permanent blacklist
            BlacklistOracle(oracle);
            DistributeSlashedFunds(currentStake, g_oracleInsuranceFund);
        } else {
            // Partial slash - update stake
            UpdateOracleStake(reg.ddStakeTxOut, newStake);
            DistributeSlashedFunds(penalty, g_oracleInsuranceFund);
        }

        LogPrintf("Oracle %s slashed: %s DD penalty\n",
                  oracle.GetID().ToString(), FormatMoney(penalty));
    }
};
```

#### 3. Miner Validation Layer

**File: `/src/oracle/bundle_manager.cpp`**

```cpp
struct COracleBundle {
    std::vector<COraclePriceMessage> prices;  // 8-15 oracle submissions
    CAmount medianPrice;                       // Pre-computed median
    uint256 merkleRoot;                        // Merkle root of prices
    uint64_t timestamp;                        // Bundle creation time

    SERIALIZE_METHODS(COracleBundle, obj) {
        READWRITE(obj.prices, obj.medianPrice, obj.merkleRoot, obj.timestamp);
    }
};

class OracleBundleManager {
public:
    /**
     * Miners select best oracle bundle for block
     * Validates bundle meets consensus rules
     */
    std::optional<COracleBundle> SelectBestBundle() {
        // Get all recent oracle price messages from mempool
        auto availablePrices = GetOraclePricesFromMempool();

        // Get active oracles for current epoch
        auto activeOracles = SelectActiveOraclesForEpoch(chainActive.Height());

        // Filter to active oracles only
        std::map<CPubKey, COraclePriceMessage> validPrices;
        for (const auto& msg : availablePrices) {
            if (IsActiveOracle(msg.oraclePubKey, activeOracles)) {
                validPrices[msg.oraclePubKey] = msg;
            }
        }

        // Need at least 8 oracles
        if (validPrices.size() < 8) return std::nullopt;

        // Build candidate bundles and select best
        return CreateBestBundle(validPrices);
    }

    /**
     * Validate oracle bundle meets consensus rules
     * Called during block validation
     */
    bool ValidateOracleBundle(const COracleBundle& bundle, int nHeight) {
        // Must have 8-15 oracle submissions
        if (bundle.prices.size() < 8 || bundle.prices.size() > 15) {
            return error("Invalid oracle count");
        }

        // All oracles must be active for this epoch
        auto activeOracles = SelectActiveOraclesForEpoch(nHeight);
        for (const auto& msg : bundle.prices) {
            if (!IsActiveOracle(msg.oraclePubKey, activeOracles)) {
                return error("Inactive oracle in bundle");
            }
        }

        // All signatures must be valid
        for (const auto& msg : bundle.prices) {
            if (!msg.VerifySignature()) {
                return error("Invalid oracle signature");
            }
        }

        // Price variance must be within tolerance (5%)
        auto [minPrice, maxPrice] = GetPriceRange(bundle.prices);
        if ((maxPrice - minPrice) * 100 / minPrice > 5) {
            return error("Price variance too high");
        }

        // Median must be within 10% of previous 10-block median
        CAmount historicalMedian = GetHistoricalMedian(10);
        if (abs(bundle.medianPrice - historicalMedian) * 100 / historicalMedian > 10) {
            return error("Price deviation from historical median");
        }

        // Merkle root must match
        if (bundle.merkleRoot != ComputeMerkleRoot(bundle.prices)) {
            return error("Invalid merkle root");
        }

        return true;
    }
};

// Integration with mining
void BlockAssembler::AddOracleBundleToBlock(CBlock& block) {
    auto bundle = g_oracleBundleManager.SelectBestBundle();
    if (!bundle) {
        LogPrintf("WARNING: No valid oracle bundle available\n");
        return;
    }

    // Add oracle bundle to coinbase OP_RETURN
    CTxOut oracleOutput;
    oracleOutput.nValue = 0;
    oracleOutput.scriptPubKey << OP_RETURN << OP_ORACLE_BUNDLE;
    oracleOutput.scriptPubKey << SerializeOracleBundle(*bundle);

    block.vtx[0]->vout.push_back(oracleOutput);
}
```

### Economic Incentive Design

#### Oracle Reward Structure

```cpp
/**
 * Calculate weekly oracle rewards
 * Triple reward structure creates powerful incentives
 */
CAmount CalculateOracleReward(const CPubKey& oraclePubKey) {
    auto reg = GetOracleRegistration(oraclePubKey);

    // 1. BASE REWARD: Pro-rata share of fee pool
    CAmount weeklyFeePool = GetOracleFeePoolBalance();
    int activeOracleCount = GetActiveOracleCount();
    CAmount baseReward = weeklyFeePool / activeOracleCount;

    // 2. REPUTATION MULTIPLIER (0.5x - 5.0x)
    double reputationMultiplier = CalculateReputationMultiplier(reg.reputationScore);
    //   Elite (95-100):   5.0x
    //   Excellent (90-94): 3.0x
    //   Good (80-89):      2.0x
    //   Average (70-79):   1.5x
    //   Basic (50-69):     1.0x
    //   Poor (0-49):       0.5x

    // 3. UPTIME BONUS (0-20%)
    double uptimeBonus = CalculateUptimeBonus(oraclePubKey);
    //   99%+ uptime:  1.20x
    //   95-99%:       1.10x
    //   <95%:         1.00x

    // 4. ACCURACY BONUS (0-30%)
    double accuracyBonus = CalculateAccuracyBonus(oraclePubKey);
    //   99%+ accurate:  1.30x
    //   95-99%:         1.15x
    //   <95%:           1.00x

    // 5. SENIORITY BONUS (1% per month, max 20%)
    int monthsActive = GetMonthsActive(oraclePubKey);
    double seniorityBonus = 1.0 + std::min(0.20, monthsActive * 0.01);

    // TOTAL REWARD FORMULA
    CAmount totalReward = baseReward
                        * reputationMultiplier
                        * uptimeBonus
                        * accuracyBonus
                        * seniorityBonus;

    return totalReward;
}
```

#### Fee Pool Funding

```cpp
// DigiDollar transaction fees → oracle rewards
const int64_t ORACLE_FEE_BASIS_POINTS_MINT = 5;      // 0.05% of mints
const int64_t ORACLE_FEE_BASIS_POINTS_REDEEM = 2;    // 0.02% of redemptions
const int64_t ORACLE_FEE_BASIS_POINTS_TRANSFER = 1;  // 0.01% of transfers

/**
 * Accumulate oracle fees from DigiDollar transactions
 */
void AccumulateOracleFee(const CTransaction& tx) {
    if (!IsDigiDollarTx(tx)) return;

    CAmount ddAmount = GetDDAmount(tx);
    CAmount fee = 0;

    switch (GetDDTxType(tx)) {
        case DD_TX_MINT:
            fee = (ddAmount * ORACLE_FEE_BASIS_POINTS_MINT) / 10000;
            break;
        case DD_TX_REDEEM:
        case DD_TX_PARTIAL:
        case DD_TX_ERR:
            fee = (ddAmount * ORACLE_FEE_BASIS_POINTS_REDEEM) / 10000;
            break;
        case DD_TX_TRANSFER:
            fee = (ddAmount * ORACLE_FEE_BASIS_POINTS_TRANSFER) / 10000;
            break;
    }

    g_oracleFeePool += fee;

    LogPrint(BCLog::ORACLE, "Oracle fee collected: %s DD (pool: %s DD)\n",
             FormatMoney(fee), FormatMoney(g_oracleFeePool));
}
```

#### Example Oracle Economics

**Elite Oracle Operator (Year 1):**
```
Initial Investment:
- Mint 10,000 DigiDollars
- Lock 300,000 DGB collateral (3-year, 300% ratio at $0.01/DGB)
- Cost: $3,000

Weekly Rewards (assuming $5,500/week pool, 75 oracles):
- Base reward: $73.33
- Reputation multiplier: 5.0x (elite tier)
- Uptime bonus: 1.20x (99.5% uptime)
- Accuracy bonus: 1.30x (99.8% accurate)
- Seniority bonus: 1.10x (6 months active)
- Weekly total: $73.33 × 5.0 × 1.20 × 1.30 × 1.10 = $628/week

Annual Oracle Fees: $32,656

DGB Collateral Appreciation (3-year term):
- Locked: 300,000 DGB at $0.01 = $3,000
- Exit: 300,000 DGB at $0.10 (10x gain) = $30,000
- Gain: $27,000

Total 3-Year Return:
- Oracle fees: $32,656/year × 3 = $97,968
- DGB appreciation: $27,000
- Total: $124,968
- ROI: 4,066% on $3,000 investment
```

### Phase 2 Deliverables

✅ **Permissionless Oracle Network**
- Anyone can stake 10,000 DD and become oracle
- Economic security via slashing
- No central authority controls oracle set

✅ **Dual Security Layers**
- Layer 1: Economic staking (oracle skin-in-game)
- Layer 2: Miner validation (DigiByte PoW security)

✅ **Sophisticated Incentives**
- Triple reward structure (fees + appreciation + bonuses)
- Reputation-based multipliers (0.5x - 5.0x)
- Performance tracking and leaderboards

✅ **Dispute Resolution**
- UMA-style optimistic oracle (48-hour challenge)
- DGB holder voting (weighted by stake)
- Slashing execution with proposer rewards

✅ **Governance Framework**
- Community can propose parameter changes
- DGB holder voting on oracle parameters
- Transparent on-chain governance

### Timeline: 10-14 Additional Weeks

**Week 7-8: Oracle Registration**
- DD staking transaction type
- Registration validation
- Stake tracking database
- Activation after 100 confirmations

**Week 9-10: Slashing Mechanism**
- Proposal creation and voting
- 48-hour challenge period
- Penalty execution
- Insurance fund management

**Week 11-12: Reputation System**
- Performance tracking
- Reputation scoring algorithms
- Reward multiplier calculations
- Public leaderboard

**Week 13-14: Miner Validation**
- Oracle bundle selection
- Coinbase commitment structure
- Block validation consensus rules
- Mining pool integration

**Week 15-18: Testing & Security**
- Testnet deployment
- Attack simulation
- Economic security audit
- Bug bounty program

**Week 19-20: Production Deployment**
- Gradual migration from Phase 1
- Mainnet soft fork activation (BIP9)
- Community oracle onboarding
- Public monitoring dashboard

---

## Security Analysis

### Attack Cost Comparison

| Attack Vector | Phase 1 Cost | Phase 2 Cost | Winner |
|---------------|--------------|--------------|--------|
| **Control 8 Oracles** | Social engineering + infrastructure | 80,000 DD stake ($80k-$800k) | Phase 2 (economic barrier) |
| **Exchange Manipulation** | $1M-$3.5M (3+ exchanges) | $1M-$3.5M (3+ exchanges) | Tie (median filtering) |
| **51% Mining Attack** | N/A | $225k/day (5 algos) | Phase 2 (mining validation) |
| **Sybil Attack** | Impossible (hardcoded set) | Impossible (stake required) | Tie (both protected) |
| **Long-term Reputation** | N/A | 6 months + stake | Phase 2 (reputation decay) |

**Combined Attack (Phase 2)**: $249k-$1.425M minimum
- 8 oracle stakes: $80k-$800k
- 51% mining: $225k/day
- Detection risk: 95% probability of slashing

### Game Theory: Honest Behavior as Dominant Strategy

```
Oracle Strategy Payoff Matrix:

                    Other Oracles Honest    Other Oracles Malicious
Oracle Honest       (+) Steady rewards       (+++) High rewards + reputation
Oracle Malicious    (---) Slashed + banned   (--) Slashed + system damaged

Dominant Strategy: BE HONEST

Expected Value:
- Honest: +$30k-$100k/year (oracle fees + DGB appreciation)
- Malicious: -$10k (slashed stake) + blacklist

Incentive Compatibility: ✅ Truthful reporting maximizes expected value
```

### Comparison to Industry Leaders

| Feature | Chainlink | UMA | DigiDollar Phase 2 |
|---------|-----------|-----|---------------------|
| **Attack Cost** | ~$1M-$10M | ~$100k-$1M | **$249k-$1.4M** |
| **Decentralization** | Good (centralized nodes) | Excellent (token voting) | **Excellent (permissionless + mining)** |
| **Capital Efficiency** | 100% locked | Bond-based | **~50% locked (can spend remaining DD)** |
| **Reward Structure** | Single (LINK APY) | Dispute-only | **Triple (fees + appreciation + bonuses)** |
| **Security Layers** | 1 (staking) | 1 (voting) | **2 (staking + mining)** |

---

## Implementation Timeline

### Complete Roadmap: Phase 1 → Phase 2

```
Week 1-6: PHASE 1 (Critical Path)
├─ Exchange API integration
├─ P2P oracle broadcasting
├─ 30 hardcoded oracles operational
├─ Basic fee pool distribution
└─ DigiDollar mainnet launch ready

Week 7-14: PHASE 2 PART 1 (Economic Security)
├─ Oracle registration via DD staking
├─ Slashing mechanism
├─ Performance tracking
├─ Reputation system
└─ Community oracles onboarded

Week 15-20: PHASE 2 PART 2 (Miner Validation)
├─ Oracle bundle validation
├─ Coinbase commitment
├─ Mining pool integration
├─ Soft fork activation (BIP9)
└─ Dual-layer security operational

Week 21-30: PHASE 2 PART 3 (Polish & Production)
├─ Dispute resolution system
├─ Public oracle dashboard
├─ Advanced incentives
├─ Security audits
└─ Bug bounty program

Total: 30 weeks (7.5 months) to fully mature oracle system
But DigiDollar launches in Week 6!
```

### Migration Strategy

**Gradual Transition from Phase 1 → Phase 2:**

1. **Week 6**: Phase 1 live, 30 hardcoded oracles
2. **Week 10**: First community oracle stakes DD and joins (Phase 2 begins)
3. **Week 14**: 20 community oracles active (40% of oracle set)
4. **Week 18**: 50 community oracles active (63% of oracle set)
5. **Week 24**: 75 community oracles active (71% of oracle set)
6. **Week 30**: 100+ community oracles (77%+ of oracle set)

**No Disruption**: DigiDollar continues operating throughout migration

---

## Risk Mitigation

### Risk 1: Insufficient Oracle Participation (Bootstrap)

**Problem**: Early phase may not attract enough community oracles.

**Mitigation:**
1. **Foundation Oracle Guarantee**: DigiByte Foundation operates 10-15 oracles during bootstrap
2. **Early Adopter Bonuses**: First 50 community oracles get 5x rewards for 6 months
3. **Marketing Campaign**: "Turn your DGB into a business" - showcase oracle economics
4. **Oracle Fund**: $50k-$100k foundation commitment to oracle reward pool

### Risk 2: Exchange API Failures

**Problem**: Exchange APIs may fail, rate limit, or provide stale data.

**Mitigation:**
1. **5-Exchange Redundancy**: Can function with 3/5 exchanges operational
2. **Fallback Sources**: DEX aggregators, CoinGecko, CoinMarketCap as backups
3. **Staleness Detection**: Reject prices >5 minutes old
4. **Automatic Retry**: Exponential backoff with 3 retries per exchange

### Risk 3: Oracle Collusion

**Problem**: Oracles may collude to manipulate prices.

**Mitigation:**
1. **Rotating Epochs**: Deterministic selection changes every ~6 hours (unpredictable)
2. **Slashing Risk**: Collusion requires 80,000 DD stake risked
3. **Detection**: Outlier alerts flag suspicious coordination
4. **Miner Validation**: Miners reject obviously manipulated bundles

### Risk 4: Mining Centralization

**Problem**: If mining centralizes, miner validation layer weakens.

**Mitigation:**
1. **Multi-Algo Advantage**: 5 algorithms make centralization extremely difficult
2. **Social Coordination**: Community monitors and can switch pools
3. **Fallback to Staking**: Economic security layer still functions
4. **Can Soft Fork**: Disable miner validation if necessary

### Risk 5: DGB Price Volatility

**Problem**: Extreme volatility could destabilize oracle stake values.

**Mitigation:**
1. **Dynamic Stakes** (future): Adjust stake requirement based on DGB price
2. **Volatility Freeze**: Oracle registration disabled during high volatility
3. **Insurance Fund**: Slashed DD funds provide stability buffer
4. **Gradual Unlocking**: Allow partial stake withdrawal after 1 year

---

## Success Metrics

### Phase 1 Success Criteria

✅ **Functionality**
- 30 oracle nodes operational
- 99.9% uptime average
- <1% price deviation from CEX spot prices
- 100% of DigiDollar operations using real oracle prices

✅ **Reliability**
- Zero successful price manipulation attacks
- <1 minute median oracle price update latency
- All 5 exchanges integrated and functional
- P2P oracle message propagation <5 seconds

✅ **Adoption**
- DigiDollar successfully launched on mainnet
- $1M+ in DigiDollars minted (proves oracle functionality)
- Community confidence in oracle system

### Phase 2 Success Criteria

✅ **Decentralization**
- 50+ community oracles staked and active
- <30% oracles controlled by any single entity
- Geographic diversity across 10+ countries

✅ **Economic Security**
- $100k+ total value staked by community oracles
- Zero successful economic attacks
- Dispute resolution system tested (at least 1 challenge resolved)

✅ **Performance**
- Elite oracles earning $10k-$50k/year (proves economic viability)
- 95%+ oracle uptime average
- Reputation system creating competitive incentives

✅ **Long-term Viability**
- Oracle operator becomes viable profession
- Sustainable fee pool growth with DD adoption
- Public monitoring dashboard (oracle.digibyte.org) with 1k+ daily visitors

---

## Conclusion

This two-phase oracle plan provides a clear path from rapid deployment to full decentralization:

**Phase 1** delivers a functional oracle system in 4-6 weeks, enabling DigiDollar to launch with real-world pricing. The hardcoded oracle approach is proven, simple to implement, and provides immediate functionality.

**Phase 2** transforms the oracle network into a fully decentralized, economically secured system over 10-14 additional weeks. The innovative DigiDollar staking model (2,500 DD = $2,500 minimum) creates powerful incentives while maintaining accessibility for community operators.

By combining economic staking with DigiByte's existing 5-algorithm PoW security, Phase 2 creates a multi-layered defense-in-depth system that surpasses industry leaders like Chainlink and UMA in both decentralization and attack resistance.

**The result**: A world-class oracle network securing the world's first truly decentralized UTXO-native stablecoin.

---

**Next Steps:**
1. Community review and feedback
2. Finalize 30 hardcoded oracle operators for Phase 1
3. Begin exchange API integration (Week 1)
4. Launch DigiDollar with Phase 1 oracle (Week 6)
5. Roll out Phase 2 enhancements (Weeks 7-20)

**Status**: Ready for implementation
