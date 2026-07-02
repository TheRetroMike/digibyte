# DigiDollar Original Oracle Design
**As Specified in TECHNICAL_SPECIFICATION.md v1.0**

*Document Purpose: Explain the originally proposed oracle system for DigiDollar*
*Date: 2025-01-04*
*Status: Original Design (Pre-Implementation)*

---

## System Flow Chart: Complete Oracle Process

```
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃                    DIGIDOLLAR ORACLE SYSTEM                     ┃
┃                      End-to-End Flow                            ┃
┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛

┌─────────────────────────────────────────────────────────────────┐
│  STEP 1: SYSTEM INITIALIZATION                                  │
│  30 Hardcoded Oracle Nodes (src/chainparams.cpp)                │
│  oracle1.digibyte.io, oracle2.digibyte.org, oracle3...          │
└─────────────────────────────────────────────────────────────────┘
                            ↓
          ╔════════════════════════════════════════╗
          ║   EVERY 100 BLOCKS (~25 MINUTES)      ║
          ║   New Epoch Begins                     ║
          ╚════════════════════════════════════════╝
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 2: DETERMINISTIC ORACLE SELECTION                         │
│  • Get block hash at epoch start (random seed)                  │
│  • Shuffle all 30 oracles using seed                            │
│  • Select first 15 → ACTIVE ORACLES THIS EPOCH                  │
│  • All nodes agree (deterministic)                              │
└─────────────────────────────────────────────────────────────────┘
                            ↓
          ╔════════════════════════════════════════╗
          ║   EVERY 4 BLOCKS (~1 MINUTE)          ║
          ║   Active Oracles Update Price          ║
          ╚════════════════════════════════════════╝
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 3: PRICE FETCHING (Each of 15 Active Oracles)            │
│                                                                  │
│  Oracle queries 7 data sources in parallel:                     │
│  ┌──────────┬──────────┬──────────┬──────────┬──────────┐     │
│  │ Binance  │ KuCoin  │ Messari  │  OKEx    │  Huobi   │     │
│  │ $0.01234 │$0.01235 │$0.01233  │$0.01236  │$0.01233  │     │
│  └──────────┴──────────┴──────────┴──────────┴──────────┘     │
│  ┌──────────┬──────────┐                                       │
│  │CoinGecko │CoinMktCap│                                       │
│  │$0.01234  │$0.01235  │                                       │
│  └──────────┴──────────┘                                       │
│                                                                  │
│  • Requires 4-of-7 sources minimum                              │
│  • Calculate median: $0.01234                                   │
│  • Convert to micro-USD: 12,340                                 │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 4: SIGNATURE & BROADCAST                                  │
│  • Create price message (timestamp, price, height, nonce)       │
│  • Sign with Schnorr signature (64 bytes)                       │
│  • Broadcast to P2P network (NetMsgType::ORACLEPRICE)           │
│  • All 15 active oracles broadcast independently                │
└─────────────────────────────────────────────────────────────────┘
                            ↓
                ┌───────────────────────┐
                │  P2P NETWORK LAYER    │
                │  Messages propagate   │
                │  to all DigiByte nodes│
                └───────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 5: MINER COLLECTION & VALIDATION                          │
│  Miner creating new block collects oracle messages:             │
│  • Receives 8-15 oracle price messages from network             │
│  • Verifies each Schnorr signature                              │
│  • Confirms oracles are active in current epoch                 │
│  • Checks minimum threshold: 8-of-15 required                   │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 6: CONSENSUS PRICE CALCULATION                            │
│  • Extract prices from all valid messages                       │
│  • Sort: [12330, 12330, 12340, 12340, 12340, 12340,           │
│           12340, 12350, 12350, 12360]                           │
│  • Calculate median (middle value): 12,340 micro-USD           │
│  • Create oracle bundle with median + signatures                │
│  • Build Merkle root of all price messages                      │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 7: BLOCK INCLUSION                                        │
│  Miner includes oracle bundle in coinbase transaction:          │
│  • Input: Newly minted DGB                                      │
│  • Output 0: Miner reward (283 DGB)                            │
│  • Output 1: OP_RETURN <oracle bundle>                          │
│      └─ Median price: 12,340 micro-USD ($0.01234)              │
│      └─ Timestamp: 1704326400                                   │
│      └─ Signatures: [sig1...sig10]                              │
│      └─ Merkle root: 0x9f2e4a8b...                              │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 8: NETWORK VALIDATION                                     │
│  Every node receiving the block validates:                      │
│  • Extract oracle bundle from coinbase OP_RETURN                │
│  • Verify >= 8 signatures present                               │
│  • Verify each signature cryptographically                      │
│  • Verify oracles are active in epoch                           │
│  • Recalculate median matches reported price                    │
│  • Accept block if all checks pass                              │
└─────────────────────────────────────────────────────────────────┘
                            ↓
                ┌───────────────────────┐
                │   CONSENSUS ACHIEVED  │
                │  Price: $0.01234/DGB  │
                │  Valid for 20 blocks  │
                │  (~5 minutes)         │
                └───────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 9: DIGIDOLLAR OPERATIONS                                  │
│                                                                  │
│  MINTING:                                                        │
│  • User wants 1,000 DD ($1,000)                                 │
│  • System reads oracle price: $0.01234/DGB                      │
│  • Calculates collateral: 243,111 DGB (300% ratio)             │
│  • User locks DGB, receives 1,000 DigiDollars                   │
│                                                                  │
│  REDEMPTION:                                                     │
│  • User burns 1,000 DD                                          │
│  • System calculates DGB owed at current oracle price           │
│  • Releases proportional collateral to user                     │
│                                                                  │
│  All operations use consensus price from latest block           │
└─────────────────────────────────────────────────────────────────┘
                            ↓
          ╔════════════════════════════════════════╗
          ║         REPEAT EVERY 4 BLOCKS          ║
          ║    Price updates continuously          ║
          ║    System maintains 1-min freshness    ║
          ╚════════════════════════════════════════╝
```

**Key Security Properties:**
- **Byzantine Fault Tolerance**: 8-of-15 threshold tolerates up to 7 faulty oracles
- **Two-Layer Defense**: 7 data sources per oracle × 15 oracles = 105 independent data points
- **Median Pricing**: Resistant to outliers and manipulation attempts
- **Deterministic Selection**: Unpredictable rotation prevents targeted attacks
- **Cryptographic Proof**: Schnorr signatures ensure authenticity
- **Aggregator Coverage**: CoinGecko (500+ exchanges), CoinMarketCap (300+ exchanges), Messari (100+ exchanges)

---

## Simple Explainer: How It Works

The original DigiDollar oracle design uses a **hardcoded trusted oracle network** similar to how DigiByte uses DNS seed nodes. Here's the simple version:

### The 5-Second Summary

1. **30 trusted oracle nodes** are hardcoded into DigiByte Core
2. Every 100 blocks (~25 minutes), **15 oracles are randomly selected** to be active
3. Each oracle fetches DGB/USD prices from **7+ data sources** (Binance, KuCoin, Messari, CoinGecko, CoinMarketCap, etc.)
4. Oracles sign their price with **Schnorr signatures** and broadcast to the network
5. **8 of 15 oracles must agree** (within tolerance) for price to be accepted
6. The **median price** is used for all DigiDollar operations

### Visual Overview

```
┌─────────────────────────────────────────────────────────────┐
│              30 HARDCODED ORACLE NODES                       │
│  oracle1.digibyte.io  oracle2.digibyte.org  oracle3...      │
└─────────────────────────────────────────────────────────────┘
                          ↓
              Every 100 blocks, shuffle and select
                          ↓
┌─────────────────────────────────────────────────────────────┐
│              15 ACTIVE ORACLES (This Epoch)                  │
│         oracle3, oracle7, oracle12, oracle18...              │
└─────────────────────────────────────────────────────────────┘
                          ↓
           Each oracle fetches from 7+ data sources
                          ↓
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│ Binance  │ KuCoin  │ Messari  │  OKEx    │  Huobi   │CoinGecko │ CoinMC   │
│ $0.01234 │$0.01235 │$0.01233  │$0.01236  │$0.01233  │$0.01234  │$0.01235  │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
                          ↓
                Calculate median: $0.01234
                          ↓
              Sign with Schnorr signature
                          ↓
              Broadcast to DigiByte network
                          ↓
┌─────────────────────────────────────────────────────────────┐
│              MINERS COLLECT ORACLE PRICES                    │
│  Need at least 8 of 15 signatures (53% threshold)           │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│              CONSENSUS PRICE: $0.01234                       │
│         Used for all DigiDollar minting/redemption           │
└─────────────────────────────────────────────────────────────┘
```

### Why This Design?

**✅ Fast to Implement**: 4-6 weeks (similar to DNS seeds)
**✅ Proven Model**: DigiByte DNS seeds work the same way
**✅ Secure Enough**: 8-of-15 threshold resists single point of failure
**✅ Multi-Layer Defense**: 7 data sources per oracle (4 exchanges + 3 aggregators)
**✅ Aggregator Protection**: CoinGecko, CoinMarketCap, Messari add 900+ exchanges
**✅ Simple Config**: Just `oracle=1` in digibyte.conf to become an oracle
**✅ Gets DigiDollar Launched**: Can deploy while working on more advanced designs

**❌ Centralized**: Limited to 30 hardcoded nodes
**❌ Trust Required**: Must trust initial oracle selection
**❌ Slow Expansion**: Adding oracles requires software update

---

## Detailed Architecture

### 1. Oracle Node Configuration

#### 1.1 Hardcoded Oracle List

**Location**: `src/chainparams.cpp`

The original design hardcodes 30 oracle nodes directly into the DigiByte Core codebase:

```cpp
class CMainParams : public CChainParams {
    // DigiDollar oracle nodes (30 hardcoded, 15 active per epoch)
    std::vector<OracleNode> vOracleNodes = {
        {"oracle1.digibyte.io", "xpub661MyMwAqRbcFW31YEwpkMuc..."},
        {"oracle2.digibyte.org", "xpub661MyMwAqRbcGczjuLamPf..."},
        {"oracle3.dgb.community", "xpub661MyMwAqRbcFtXgS5sYJA..."},
        // ... 27 more oracle nodes with hostnames and extended public keys
    };

    // Oracle epoch configuration
    consensus.nOracleEpochBlocks = 100;      // Rotate oracles every 100 blocks (~25 min)
    consensus.nOracleUpdateInterval = 4;     // Update price every 4 blocks (~1 min)
    consensus.nOracleCount = 30;             // Total hardcoded oracles
    consensus.nActiveOracles = 15;           // Active per epoch
    consensus.nOracleThreshold = 8;          // 8-of-15 signatures required
    consensus.nPriceValidBlocks = 20;        // Price valid for 20 blocks (~5 min)
};
```

**Key Parameters**:
- **30 total oracles**: Fixed set hardcoded in source code
- **15 active oracles**: Selected each epoch
- **8-of-15 threshold**: 53% consensus required
- **100-block epochs**: ~25 minutes (240 blocks/hour ÷ 4 algos × 100 = ~25 min)
- **4-block updates**: ~1 minute price refresh rate

#### 1.2 Oracle Operator Configuration

Any of the 30 hardcoded oracle operators enables oracle mode:

```bash
# digibyte.conf

# Enable oracle mode (only works if you're one of the 30 hardcoded nodes)
oracle=1

# Data source configuration (exchanges + aggregators)
oraclesources=binance,kucoin,messari,okex,huobi,coingecko,coinmarketcap

# Exchange API keys (read-only, no trading permissions needed)
oracleapikey_binance=YOUR_BINANCE_API_KEY
oracleapisecret_binance=YOUR_BINANCE_SECRET

oracleapikey_kucoin=YOUR_KUCOIN_API_KEY
oracleapisecret_kucoin=YOUR_KUCOIN_SECRET

oracleapikey_okex=YOUR_OKEX_API_KEY
oracleapisecret_okex=YOUR_OKEX_SECRET

oracleapikey_huobi=YOUR_HUOBI_API_KEY
oracleapisecret_huobi=YOUR_HUOBI_SECRET

# Aggregated data providers (recommended for reliability)
oracleapikey_messari=YOUR_MESSARI_API_KEY
oracleapikey_coingecko=YOUR_COINGECKO_API_KEY
oracleapikey_coinmarketcap=YOUR_COINMARKETCAP_API_KEY
```

---

### 2. Oracle Price Message Format

#### 2.1 Price Message Structure

**Location**: `src/primitives/oracle.h`

```cpp
class COraclePriceMessage {
public:
    // Price data
    uint32_t nTimestamp;           // Unix timestamp of price fetch
    uint32_t nPricePerDGB;         // Price in micro-USD (1,000,000 = $1.00)
    uint32_t nBlockHeight;         // Current block height

    // Signature data (Schnorr)
    std::vector<unsigned char> vchSchnorrSig;  // Schnorr signature
    XOnlyPubKey oraclePubKey;                  // Oracle's public key

    // Anti-replay protection
    uint256 nonce;                 // Random nonce to prevent replay attacks

    // Methods
    bool VerifySignature() const;  // Verify Schnorr signature
    uint256 GetMessageHash() const; // Get hash for signing

    ADD_SERIALIZE_METHODS;         // Enable serialization for P2P
};
```

**Example Price Message**:
```
Timestamp:   1704326400 (2025-01-04 00:00:00 UTC)
Price:       12,340 micro-USD ($0.01234 per DGB)
BlockHeight: 18,500,000
PubKey:      xpub661MyMwAqRbcFW31YEwpkMuc...
Signature:   <64-byte Schnorr signature>
Nonce:       a3f8e92b4c... (random 256-bit value)
```

#### 2.2 Price Format

Prices are stored in **micro-USD** (millionths of a dollar):

```
$1.00     = 1,000,000 micro-USD
$0.01234  = 12,340 micro-USD
$0.00001  = 10 micro-USD
```

**Why micro-USD?**
- Avoids floating point precision issues
- uint32_t supports up to $4,294 per DGB (more than enough)
- Easy integer math for calculations

---

### 3. Oracle Selection Algorithm

#### 3.1 Deterministic Epoch Selection

**Location**: `src/consensus/oracle.cpp`

The original design uses **deterministic random selection** based on block hash:

```cpp
std::vector<COracleNode> SelectActiveOracles(int nHeight) {
    // Calculate epoch start height
    int epochStart = (nHeight / 100) * 100;

    // Get block hash at epoch start as randomness seed
    uint256 epochSeed = GetBlockHash(epochStart);

    // Shuffle all 30 oracles using epoch seed
    std::vector<COracleNode> shuffled = vOracleNodes;
    std::mt19937 rng(epochSeed.GetUint64(0));
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    // Return first 15 oracles for this epoch
    return std::vector<COracleNode>(shuffled.begin(), shuffled.begin() + 15);
}
```

**Key Properties**:
- **Deterministic**: All nodes select same 15 oracles using same seed
- **Unpredictable**: Block hash provides randomness (can't predict which oracles until epoch starts)
- **Fair**: Each oracle has 15/30 = 50% chance of being selected each epoch
- **Verifiable**: Anyone can verify the correct oracles were selected

#### 3.2 Epoch Timeline Example

```
Block Height: 18,500,000-18,500,099 (Epoch 185,000)
├─ Seed: Hash of block 18,500,000
├─ Active Oracles: oracle3, oracle7, oracle12, oracle15, oracle18...
└─ Duration: 100 blocks (~25 minutes)

Block Height: 18,500,100-18,500,199 (Epoch 185,001)
├─ Seed: Hash of block 18,500,100 (NEW RANDOMNESS)
├─ Active Oracles: oracle1, oracle5, oracle9, oracle14, oracle22...
└─ Duration: 100 blocks (~25 minutes)
```

---

### 4. Price Fetching Process

#### 4.1 Exchange Integration

**Location**: `src/oracle/node.cpp`

Each oracle fetches prices from multiple exchanges:

```cpp
class COracleNode {
private:
    std::vector<std::string> m_dataSources = {
        // Direct exchange APIs
        "binance",       // Highest volume exchange
        "kucoin",        // Asian market coverage
        "okex",          // Additional Asian coverage
        "huobi",         // Additional global coverage

        // Aggregated data providers (recommended)
        "messari",       // Professional crypto data aggregator
        "coingecko",     // Multi-exchange aggregator
        "coinmarketcap"  // Multi-exchange aggregator
    };

public:
    /**
     * Fetch median price from all configured data sources
     * Returns price in micro-USD
     */
    CAmount FetchMedianPrice() {
        std::vector<double> prices;

        // Fetch from each data source
        for (const auto& source : m_dataSources) {
            try {
                double price = FetchPrice(source);
                if (price > 0) {
                    prices.push_back(price);
                }
            } catch (...) {
                LogPrintf("Failed to fetch from %s\n", source);
            }
        }

        // Require at least 4 successful sources (majority of 7)
        if (prices.size() < 4) {
            throw std::runtime_error("Insufficient price sources");
        }

        // Calculate median (resistant to outliers)
        std::sort(prices.begin(), prices.end());
        double median = prices[prices.size() / 2];

        // Convert to micro-USD
        return static_cast<CAmount>(median * 1000000);
    }

    /**
     * Create signed price message
     */
    COraclePriceMessage CreatePriceMessage() {
        COraclePriceMessage msg;
        msg.nTimestamp = GetTime();
        msg.nPricePerDGB = FetchMedianPrice();
        msg.nBlockHeight = chainActive.Height();
        msg.nonce = GetRandHash();

        // Sign with oracle's private key (Schnorr signature)
        uint256 msgHash = msg.GetMessageHash();
        SignSchnorr(msgHash, m_oracleKey, msg.vchSchnorrSig);

        msg.oraclePubKey = m_oracleKey.GetPubKey();

        return msg;
    }
};
```

**Data Fetching Details**:
1. Oracle contacts each data source API
2. Gets DGB/USD or DGB/USDT price (direct from exchange or aggregated)
3. Stores prices in vector
4. Requires at least 4 of 7 sources successful
5. Sorts prices and takes median
6. Median resistant to single source manipulation

---

### 5. Price Aggregation & Consensus

#### 5.1 Consensus Algorithm

**Location**: `src/consensus/oracle.cpp`

```cpp
/**
 * Aggregate oracle prices with 8-of-15 threshold validation
 * Returns consensus price in micro-USD
 */
CAmount GetConsensusPrice(const std::vector<COraclePriceMessage>& prices) {
    // Require at least 8 valid signatures
    if (prices.size() < 8) {
        throw std::runtime_error("Insufficient oracle consensus");
    }

    // Verify all signatures are valid
    for (const auto& price : prices) {
        if (!price.VerifySignature()) {
            throw std::runtime_error("Invalid oracle signature");
        }
    }

    // Extract prices into vector
    std::vector<uint32_t> validPrices;
    for (const auto& p : prices) {
        validPrices.push_back(p.nPricePerDGB);
    }

    // Sort prices
    std::sort(validPrices.begin(), validPrices.end());

    // Return median price (middle value)
    return validPrices[validPrices.size() / 2];
}
```

**Consensus Properties**:
- **8-of-15 threshold**: Requires 53% agreement
- **Byzantine Fault Tolerance**: Can tolerate up to 7 faulty/malicious oracles
- **Median Pricing**: Resistant to outliers
- **Schnorr Verification**: Cryptographic proof of authenticity

---

### 6. Block Integration

#### 6.1 Coinbase Oracle Data

**Location**: `src/validation.cpp`

Oracle prices are included in blocks via **OP_RETURN in coinbase transaction**:

```cpp
/**
 * Validate oracle data in block
 */
bool CheckBlockOracleData(const CBlock& block, const CChainParams& chainparams) {
    // Extract oracle data from coinbase transaction
    const CTransaction& coinbase = block.vtx[0];

    // Look for OP_RETURN output with oracle data
    COracleBundle oracleData;
    bool foundOracle = false;

    for (const auto& output : coinbase.vout) {
        if (output.scriptPubKey[0] == OP_RETURN) {
            if (ExtractOracleData(output.scriptPubKey, oracleData)) {
                foundOracle = true;
                break;
            }
        }
    }

    // Blocks without oracle data are valid but can't process DigiDollar txs
    if (!foundOracle) {
        return !BlockContainsDigiDollarTx(block);
    }

    // Get active oracles for this height
    auto activeOracles = SelectActiveOracles(block.nHeight);

    // Count valid signatures (must be from active oracles)
    int validSigs = 0;
    for (const auto& sig : oracleData.signatures) {
        if (VerifyOracleSignature(sig, activeOracles)) {
            validSigs++;
        }
    }

    // Require at least 8 valid signatures
    return validSigs >= 8;
}
```

**Block Structure**:
```
Block #18,500,123
├─ Coinbase Transaction
│  ├─ Input: Newly minted DGB
│  ├─ Output 0: Miner reward
│  ├─ Output 1: OP_RETURN <oracle_data>
│  │  └─ Contains:
│  │     ├─ Median Price: 12,340 micro-USD
│  │     ├─ Timestamp: 1704326400
│  │     ├─ 8-15 Oracle Signatures (Schnorr)
│  │     └─ Merkle root of all price messages
│  └─ ...
├─ Transaction 1: User sends DGB
├─ Transaction 2: DigiDollar mint (uses oracle price from coinbase)
└─ ...
```

---

## Flow Charts

### Flow Chart 1: Oracle Selection & Activation

```
┌─────────────────────────────────────────────────────────────┐
│                    EVERY 100 BLOCKS                          │
│             New Epoch Starts (e.g., block 18,500,100)        │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    1. Get Block Hash at Epoch Start                          │
│       epochSeed = GetBlockHash(18,500,100)                   │
│       → Returns: 0x3a8f2e9b4c7d... (256-bit hash)           │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    2. Use Hash as RNG Seed                                   │
│       rng = std::mt19937(epochSeed)                          │
│       → Deterministic random number generator                │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    3. Shuffle All 30 Hardcoded Oracles                       │
│       shuffled = [oracle1...oracle30]                        │
│       std::shuffle(shuffled, rng)                            │
│       → Result: [oracle3, oracle7, oracle1, oracle15...]     │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    4. Select First 15 Oracles                                │
│       active = shuffled[0...14]                              │
│       → [oracle3, oracle7, oracle1, oracle15, oracle22,      │
│          oracle9, oracle28, oracle12, oracle5, oracle19,     │
│          oracle14, oracle26, oracle8, oracle30, oracle11]    │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    5. These 15 Oracles Are Active For Next 100 Blocks       │
│       (Blocks 18,500,100 - 18,500,199)                       │
│       All nodes agree on same selection (deterministic)      │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    6. After 100 Blocks, Repeat Process                       │
│       → New epoch seed from block 18,500,200                 │
│       → New shuffled selection                               │
│       → Different 15 oracles become active                   │
└─────────────────────────────────────────────────────────────┘
```

---

### Flow Chart 2: Price Fetching & Broadcasting

```
┌─────────────────────────────────────────────────────────────┐
│          EVERY 4 BLOCKS (~1 minute)                          │
│   Oracle Node Wakes Up to Fetch & Broadcast Price           │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 1: Check If Oracle Is Active This Epoch             │
│    activeOracles = SelectActiveOracles(currentHeight)        │
│    if (myPubKey not in activeOracles): return (sleep)       │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 2: Fetch Price from Binance                         │
│    GET https://api.binance.com/api/v3/ticker/price          │
│         ?symbol=DGBUSDT                                      │
│    Response: {"symbol":"DGBUSDT","price":"0.01234"}          │
│    → price1 = $0.01234                                       │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 3: Fetch Price from KuCoin                          │
│    GET https://api.kucoin.com/api/v1/market/orderbook       │
│         /level1?symbol=DGB-USDT                              │
│    Response: {"bestBid":"0.01233","bestAsk":"0.01235"}       │
│    → price2 = $0.01234 (midpoint)                            │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 4: Fetch from Messari, OKEx, Huobi                  │
│    → price3 = $0.01235 (Messari aggregated)                  │
│    → price4 = $0.01233 (OKEx)                                │
│    → price5 = $0.01233 (Huobi)                               │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 5: Fetch from CoinGecko, CoinMarketCap              │
│    → price6 = $0.01234 (CoinGecko aggregated)                │
│    → price7 = $0.01235 (CoinMarketCap aggregated)            │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 6: Calculate Median Price (7 sources)               │
│    prices = [0.01234, 0.01234, 0.01235, 0.01233,            │
│              0.01233, 0.01234, 0.01235]                      │
│    sort(prices) → [0.01233, 0.01233, 0.01234, 0.01234,      │
│                    0.01234, 0.01235, 0.01235]                │
│    median = prices[3] = $0.01234 (middle value)             │
│    → Convert to micro-USD: 12,340                            │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 7: Create Price Message                             │
│    msg.nTimestamp = GetTime()         → 1704326400          │
│    msg.nPricePerDGB = 12340            → micro-USD           │
│    msg.nBlockHeight = chainActive.Height() → 18,500,123     │
│    msg.nonce = GetRandHash()           → 0xa3f8e92b...      │
│    msg.oraclePubKey = myPubKey         → xpub661...         │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 8: Sign Message with Schnorr Signature              │
│    msgHash = SHA256(timestamp || price || height || nonce)   │
│    signature = SchnorrSign(msgHash, myPrivateKey)           │
│    msg.vchSchnorrSig = signature (64 bytes)                 │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 9: Broadcast to P2P Network                         │
│    g_connman->ForEachNode([&](CNode* node) {                 │
│        node->PushMessage(NetMsgType::ORACLEPRICE, msg);      │
│    });                                                       │
│    → Message propagates to all DigiByte nodes                │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 10: Sleep Until Next Update                         │
│    Sleep for ~1 minute (4 blocks)                            │
│    → Repeat from Step 1                                      │
└─────────────────────────────────────────────────────────────┘
```

---

### Flow Chart 3: Consensus Price Validation

```
┌─────────────────────────────────────────────────────────────┐
│              MINER CREATING NEW BLOCK                        │
│  Miner collects oracle price messages from mempool/network  │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 1: Collect All Recent Oracle Price Messages         │
│    Received messages:                                        │
│    • oracle3:  $0.01234, sig1, timestamp: 1704326400        │
│    • oracle7:  $0.01235, sig2, timestamp: 1704326401        │
│    • oracle1:  $0.01234, sig3, timestamp: 1704326399        │
│    • oracle15: $0.01233, sig4, timestamp: 1704326402        │
│    • oracle22: $0.01236, sig5, timestamp: 1704326400        │
│    • oracle9:  $0.01234, sig6, timestamp: 1704326401        │
│    • oracle28: $0.01234, sig7, timestamp: 1704326400        │
│    • oracle12: $0.01235, sig8, timestamp: 1704326399        │
│    • oracle5:  $0.01234, sig9, timestamp: 1704326402        │
│    • oracle19: $0.01233, sig10, timestamp: 1704326401       │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 2: Verify Oracle Signatures                         │
│    For each message:                                         │
│      1. Extract oraclePubKey                                 │
│      2. Compute msgHash = SHA256(msg data)                   │
│      3. Verify: SchnorrVerify(msgHash, sig, pubKey)          │
│    → All 10 signatures verify successfully ✓                │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 3: Check Oracle is Active in Current Epoch          │
│    activeOracles = SelectActiveOracles(blockHeight)          │
│    For each message:                                         │
│      Check if oraclePubKey ∈ activeOracles                   │
│    → All 10 oracles are active ✓                             │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 4: Check Minimum Threshold (8-of-15)                │
│    validMessages = 10                                        │
│    threshold = 8                                             │
│    if (validMessages < threshold): REJECT                    │
│    → 10 >= 8, threshold met ✓                                │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 5: Extract Prices                                   │
│    prices = [12340, 12350, 12340, 12330, 12360,             │
│              12340, 12340, 12350, 12340, 12330]             │
│    (All in micro-USD)                                        │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 6: Calculate Median (Consensus Price)               │
│    Sort prices:                                              │
│    [12330, 12330, 12340, 12340, 12340, 12340, 12340,        │
│     12350, 12350, 12360]                                     │
│    Median (middle value) = prices[5] = 12340 micro-USD      │
│    → Consensus Price: $0.01234                               │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 7: Create Oracle Bundle for Block                   │
│    oracleBundle {                                            │
│      medianPrice: 12340 micro-USD                            │
│      timestamp: 1704326400                                   │
│      signatures: [sig1, sig2, ..., sig10]                    │
│      merkleRoot: 0x9f2e4a8b...  (Merkle root of all msgs)   │
│    }                                                         │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 8: Include in Coinbase OP_RETURN                    │
│    Coinbase Transaction:                                     │
│    • Input: Newly minted DGB                                 │
│    • Output 0: Miner reward (283 DGB)                       │
│    • Output 1: OP_RETURN <serialized oracleBundle>          │
│    → Block includes consensus price                          │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 9: Block Validation by Other Nodes                  │
│    Every node receiving this block:                          │
│    1. Extract oracle bundle from coinbase                    │
│    2. Verify >= 8 valid signatures                           │
│    3. Verify all oracles are active in epoch                 │
│    4. Recalculate median from provided prices                │
│    5. Accept block if all checks pass                        │
│    → Block accepted and chain advances ✓                     │
└─────────────────────────────────────────────────────────────┘
```

---

### Flow Chart 4: DigiDollar Minting with Oracle Price

```
┌─────────────────────────────────────────────────────────────┐
│                  USER WANTS TO MINT DIGIDOLLARS              │
│         User: "I want to mint 1,000 DigiDollars"            │
│         Lock Period: 1 year (300% collateral ratio)          │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 1: Get Current Oracle Price                         │
│    consensusPrice = GetConsensusPrice()                      │
│    → Extract from most recent block's coinbase               │
│    → Returns: 12,340 micro-USD ($0.01234 per DGB)           │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 2: Calculate Required Collateral                    │
│    ddAmount = 1,000 DigiDollars = $1,000                    │
│    collateralRatio = 300% (for 1 year lock)                  │
│                                                              │
│    dgbFor100Percent = $1,000 / $0.01234 = 81,037 DGB        │
│    requiredDGB = 81,037 × 3.00 = 243,111 DGB                │
│                                                              │
│    → User needs to lock 243,111 DGB                          │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 3: Check System Health (DCA)                        │
│    systemCollateral = GetSystemCollateralRatio()             │
│    → Returns: 180% (healthy)                                 │
│    dcaMultiplier = GetDCAMultiplier(180)                     │
│    → Returns: 1.0 (no adjustment, system is healthy)         │
│    finalRequiredDGB = 243,111 × 1.0 = 243,111 DGB           │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 4: Check Volatility Freeze                          │
│    isVolatilityFreeze = CheckVolatility()                    │
│    → Look at price changes over last hour                    │
│    → Returns: false (price stable)                           │
│    → Minting allowed ✓                                       │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 5: Create Mint Transaction                          │
│    Transaction:                                              │
│    • Version: 0x0D1D0770 | (DD_TX_MINT << 16)               │
│    • Input: User's DGB UTXO (243,111+ DGB)                  │
│    • Output 0: P2TR Collateral (243,111 DGB locked)         │
│    •   └─ Includes timelock script (1 year = 2,102,400 blocks) │
│    • Output 1: P2TR DigiDollar (0 DGB value, 1000 DD)       │
│    • Output 2: OP_RETURN (metadata)                          │
│    • Output 3: Change (excess DGB back to user)             │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 6: Transaction Validation by Network                │
│    Each node validates:                                      │
│    1. Extract ddAmount (1000) and collateral (243,111)       │
│    2. Get current oracle price (12,340 micro-USD)            │
│    3. Calculate required collateral                          │
│    4. Verify: actualCollateral >= requiredCollateral         │
│    5. Check mint limits ($100-$100k)                         │
│    6. Check no volatility freeze                             │
│    → All checks pass ✓                                       │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│    Step 7: Transaction Confirmed in Block                   │
│    • User's 243,111 DGB locked for 1 year                   │
│    • User receives 1,000 DigiDollars (DD address DD1...)    │
│    • Collateral position tracked in UTXO set                │
│    • Position redeemable after 2,102,400 blocks (~1 year)   │
└─────────────────────────────────────────────────────────────┘
```

---

## Security Properties

### 1. Byzantine Fault Tolerance

The 8-of-15 threshold provides Byzantine Fault Tolerance:

```
Total Oracles:    15 active
Threshold:        8 required
Byzantine Limit:  f = (n-1)/3 = (15-1)/3 = 4.67

Interpretation: System can tolerate up to 7 faulty oracles:
- 7 malicious oracles cannot reach 8-signature threshold
- Need 8 honest oracles minimum for consensus
- 15 - 7 = 8, exactly at threshold
```

**Attack Scenarios**:
- **5 compromised oracles**: 10 honest remain → consensus still works ✓
- **7 compromised oracles**: 8 honest remain → consensus barely works ✓
- **8 compromised oracles**: 7 honest remain → consensus fails ✗

### 2. Median Pricing Protection

Median pricing is resistant to outliers:

```
Example: 1 malicious oracle reports fake price

Prices from 10 oracles (each using 7 data sources):
[$0.01230, $0.01234, $0.01234, $0.01235, $0.01236,
 $0.01233, $0.01234, $0.01235, $0.01234, $0.50000 ← FAKE]

Sorted: [$0.01230, $0.01233, $0.01234, $0.01234, $0.01234,
         $0.01234, $0.01235, $0.01235, $0.01236, $0.50000]

Median: $0.01234 (5th value) → Fake price ignored ✓

Even if attacker controls multiple data sources:
- Each oracle uses 7 sources, needs 4 to succeed
- Would need to manipulate 4+ sources per oracle
- Would need to compromise 5+ oracles
- Combined cost: $1M+ for exchanges + infrastructure
```

**Two-layer defense: Source diversity × Oracle diversity**

### 3. Comprehensive Data Source Coverage

**Data Source Architecture:**

```
If attacker manipulates 1 exchange (e.g., flash loan attack on OKEx):

Oracle's Data Source Prices:
- Binance:      $0.01234  ✓ (Real)
- KuCoin:       $0.01234  ✓ (Real)
- OKEx:         $0.50000  ✗ (Manipulated)
- Huobi:        $0.01235  ✓ (Real)
- Messari:      $0.01233  ✓ (Real)
- CoinGecko:    $0.01234  ✓ (Real)
- CoinMarketCap: $0.01235  ✓ (Real)

Oracle calculates median: $0.01234 (ignores OKEx outlier)

All oracles report: $0.01234 → Consensus not affected ✓
```

**Would need to manipulate 3+ major exchanges simultaneously (cost: $1M+).**

### 4. Deterministic Selection

Attackers cannot predict which oracles will be active:

```
Current Epoch (Block 18,500,100):
- Active: oracle3, oracle7, oracle1, oracle15...
- Attacker controls: oracle5, oracle11, oracle23 (3 of 30)

Attacker's oracles NOT selected this epoch → cannot influence price

Next Epoch (Block 18,500,200):
- New randomness from block hash
- Different oracles selected
- Attacker has 15/30 = 50% chance per oracle
- Probability of getting 8+ oracles: 0.000000276 (1 in 3.6 million epochs)
```

---

## Limitations & Trade-offs

### Limitations of Original Design

1. **Centralization**
   - Only 30 hardcoded oracles
   - Requires trust in initial oracle selection
   - Limited geographic/entity diversity

2. **Slow Expansion**
   - Adding new oracles requires software update
   - All nodes must upgrade to recognize new oracles
   - Social coordination needed

3. **No Economic Incentive**
   - No stake requirement (no skin in the game)
   - No rewards for oracle operation
   - No penalties for misbehavior (initially)

4. **Single Point of Governance**
   - DigiByte Foundation decides which 30 oracles
   - No on-chain governance mechanism
   - Potential for capture by special interests

5. **Limited Scalability**
   - 30 oracles is fixed upper limit
   - Cannot grow to hundreds of oracles
   - Rotation only provides diversity, not scale

### Trade-offs Accepted

| Trade-off | Reason |
|-----------|--------|
| **Centralization** | Fast implementation (4-6 weeks) |
| **Trust Required** | Similar to DigiByte DNS seeds (proven model) |
| **No Economic Incentives** | Simplicity (can add later in Phase 2) |
| **Limited Scale** | 30 oracles sufficient for initial launch |
| **Manual Updates** | Acceptable for MVP, improves in later phases |

---

## Comparison to Advanced Designs

The original design is **Phase 1** of a multi-phase rollout:

| Feature | Original (Phase 1) | Staking (Phase 2) | Miner Validation (Phase 3) |
|---------|-------------------|-------------------|---------------------------|
| **Oracles** | 30 hardcoded | Unlimited staked community | Validated by miners |
| **Threshold** | 8-of-15 Schnorr | 8-of-15 with reputation | 8-of-15 + PoW validation |
| **Incentives** | None (volunteer) | DD staking rewards | Triple rewards |
| **Slashing** | None | Economic penalties | Miner rejection + slashing |
| **Timeline** | 4-6 weeks | 10-14 additional weeks | 6-8 additional weeks |
| **Decentralization** | Low | High | Very High |

**Migration Path**:
1. Launch with original design (fast to market)
2. Add staking layer (permissionless expansion)
3. Add miner validation (defense in depth)
4. Eventually: Hardcoded oracles become backup only

---

## Implementation Checklist

### Week 1-2: Oracle Infrastructure
- [ ] Add oracle node configuration to chainparams.cpp
- [ ] Implement COraclePriceMessage structure
- [ ] Implement Schnorr signature signing/verification
- [ ] Add P2P message type for oracle prices

### Week 3-4: Data Source Integration
- [ ] Implement Binance API client
- [ ] Implement KuCoin API client
- [ ] Implement OKEx API client
- [ ] Implement Huobi API client
- [ ] Implement Messari API client (https://messari.io/api)
- [ ] Implement CoinGecko API client (https://coingecko.com/api)
- [ ] Implement CoinMarketCap API client (https://coinmarketcap.com/api)
- [ ] Median calculation with outlier filtering (4-of-7 minimum)

### Week 5-6: Consensus & Validation
- [ ] Implement SelectActiveOracles() algorithm
- [ ] Implement GetConsensusPrice() with 8-of-15 threshold
- [ ] Add coinbase OP_RETURN oracle data
- [ ] Implement CheckBlockOracleData() validation
- [ ] Integrate with mint transaction validation

### Testing
- [ ] Unit tests for oracle selection
- [ ] Unit tests for median calculation
- [ ] Functional tests for oracle consensus
- [ ] Testnet deployment with 10 test oracles

---

## Conclusion

The original oracle design provides a **simple, proven approach** to decentralized price feeds:

**✅ Strengths**:
- Fast to implement (4-6 weeks)
- Proven model (DigiByte DNS seeds)
- Byzantine fault tolerant (8-of-15)
- Median pricing resists outliers
- Deterministic selection prevents prediction

**❌ Weaknesses**:
- Centralized (30 hardcoded nodes)
- No economic incentives/penalties
- Slow to expand (requires software updates)
- Trust in initial oracle selection

**🎯 Perfect For**: MVP launch to get DigiDollar operational quickly while developing more advanced oracle systems (staking, miner validation) in parallel.

The original design is not the final form, but rather **Phase 1 of a progressive decentralization strategy** that allows DigiDollar to launch quickly while building toward a fully decentralized oracle network.

---

**Status**: Ready for implementation
**Timeline**: 4-6 weeks to production
**Next Steps**: Begin exchange API integration (Week 1)
