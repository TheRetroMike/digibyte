# DigiDollar: Technical Deep-Dive Presentation
## Architecture, Implementation, and Security Analysis

**Duration**: 30-45 minutes (adaptable)
**Target Audience**: Developers, researchers, technical reviewers, blockchain architects
**Goal**: Complete technical understanding of DigiDollar's architecture, security model, and implementation

---

# SECTION 1: ARCHITECTURE OVERVIEW

## 1.1 System Architecture Diagram

```
┌────────────────────────────────────────────────────────────────┐
│                 DIGIDOLLAR SYSTEM ARCHITECTURE                  │
│                    (Multi-Layer Design)                         │
└────────────────────────────────────────────────────────────────┘

LAYER 7: USER INTERFACE
┌──────────────────────────────────────────────────────────────┐
│  DigiByte Core Wallet (Qt GUI)                               │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐      │
│  │Over- ││ Mint ││ Send ││Receive││Redeem││Vaults│          │
│  │view  ││      ││      ││      ││      ││      │          │
│  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘      │
└──────────────────────────────────────────────────────────────┘
                           │ RPC/Signals
                           ▼
LAYER 6: BUSINESS LOGIC
┌──────────────────────────────────────────────────────────────┐
│  DigiDollar Manager (C++ Core)                               │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐             │
│  │ Mint Logic  │ │ Transfer    │ │ Redemption  │             │
│  │ • Collateral│ │ • UTXO Sel. │ │ • Timelock  │             │
│  │ • DCA Check │ │ • Signing   │ │ • ERR Logic │             │
│  └─────────────┘ └─────────────┘ └─────────────┘             │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 5: PROTECTION SYSTEMS
┌──────────────────────────────────────────────────────────────┐
│  Four-Layer Protection Framework                             │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐          │
│  │ DCA (1.0-2x) │ │ ERR (80-95%) │ │ Volatility   │          │
│  │ Multipliers  │ │ Haircuts     │ │ Freeze 20%   │          │
│  └──────────────┘ └──────────────┘ └──────────────┘          │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 4: ORACLE SYSTEM
┌──────────────────────────────────────────────────────────────┐
│  Decentralized Price Feed Network                            │
│  7 Exchange APIs → MAD Filtering → Median → Block Storage    │
│  Phase 1: 1-of-1 | Phase 2: 8-of-15 Schnorr threshold        │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 3: CONSENSUS & VALIDATION
┌──────────────────────────────────────────────────────────────┐
│  DigiByte Consensus Rules                                    │
│  • Transaction version validation (0x0D1D0770)               │
│  • Oracle data validation                                    │
│  • Collateral ratio enforcement                              │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 2: BLOCKCHAIN STORAGE (UTXO Model)
┌──────────────────────────────────────────────────────────────┐
│  DigiByte Blockchain                                         │
│  • P2TR outputs (Taproot)                                    │
│  • OP_RETURN metadata                                        │
│  • MAST redemption paths                                     │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 1: NETWORK & CRYPTOGRAPHY
┌──────────────────────────────────────────────────────────────┐
│  P2P Network + Cryptographic Primitives                      │
│  • Schnorr Signatures (BIP-340)                              │
│  • Taproot P2TR (BIP-341)                                    │
│  • OP_CHECKLOCKTIMEVERIFY (BIP-65)                           │
└──────────────────────────────────────────────────────────────┘
```

### Speaker Notes
> "DigiDollar is built as a native extension to DigiByte—not a side chain, not a smart contract layer, but integrated directly into the core protocol. This diagram shows the seven architectural layers, from cryptographic primitives at the bottom to the user interface at the top."

---

## 1.2 Why UTXO-Native Matters

### UTXO vs Account Model Comparison

| Feature | Account Model (Ethereum) | UTXO Model (DigiByte) |
|---------|--------------------------|----------------------|
| State | Global contract state | Independent coin states |
| Parallelism | Sequential nonces | Fully parallel spending |
| Auditability | Contract logic hidden | All on-chain transparent |
| Freezability | Contract owner can freeze | Impossible by design |
| Attack surface | Smart contract bugs | Bitcoin Script only |

### Technical Advantages

```cpp
// Account Model: Balance is abstract
mapping(address => uint256) balances;
// Alice has 1000 USDT (you trust the contract)

// UTXO Model: Each coin is independent
std::map<COutPoint, CAmount> dd_utxos;
// Alice has:
//   UTXO 1: 600 DD (txid:abc, vout:1) ← Specific, verifiable
//   UTXO 2: 400 DD (txid:def, vout:1) ← Independently secured
```

### Key Innovation: DD UTXO Tracking

Traditional UTXOs store value in the output itself. DD tokens have 0 DGB value—the DD amount is stored in metadata. DigiDollar solves this with explicit mapping:

```cpp
// (txid, vout) → DD amount in cents
std::map<COutPoint, CAmount> dd_utxos;

// Example flow:
// Transaction A creates DD:
//   vout[1]: 0 DGB, P2TR script, metadata says "500 DD"
//   → Map: (TxA, 1) → 50000 cents
//
// Transaction B splits:
//   vin[0]: (TxA, 1) ← Spending 500 DD
//   vout[0]: 300 DD to Alice
//   vout[1]: 200 DD change
//   → Remove: (TxA, 1)
//   → Add: (TxB, 0) → 30000 cents
//   → Add: (TxB, 1) → 20000 cents
```

---

## 1.3 Transaction Version Encoding

### The Magic Number: 0x0D1D0770

```cpp
// From primitives/transaction.h:46-56
static const int32_t DD_TX_VERSION = 0x0D1D0770;  // "DigiDollar" marker
static const int32_t DD_VERSION_MASK = 0x0000FFFF; // Lower 16 bits
static const int32_t DD_TYPE_MASK = 0xFF000000;    // Upper 8 bits
static const int32_t DD_FLAGS_MASK = 0x00FF0000;   // Middle 8 bits

// Version construction:
inline int32_t MakeDigiDollarVersion(DigiDollarTxType type, uint8_t flags = 0) {
    return (static_cast<int32_t>(type) << 24) |      // Transaction type
           (static_cast<int32_t>(flags) << 16) |      // Optional flags
           (DD_TX_VERSION & DD_VERSION_MASK);         // 0x0770 marker
}
```

### Bit Layout

```
┌────────────────────────────────────────────────────────────┐
│ Bits 31-24 │ Bits 23-16 │ Bits 15-0                       │
│ TX Type    │ Flags      │ DD Marker (0x0770)              │
└────────────────────────────────────────────────────────────┘

Example Version Values:
  Mint:     0x01000770 (type=1, flags=0, marker=0x0770)
  Transfer: 0x02000770 (type=2, flags=0, marker=0x0770)
  Redeem:   0x03000770 (type=3, flags=0, marker=0x0770)
  ERR:      0x05000770 (type=5, flags=0, marker=0x0770)
```

### Why This Matters

1. **Dust Bypass**: Bitcoin Core's dust checks skip DD transactions
2. **Type Detection**: Nodes instantly identify transaction purpose
3. **Validation Rules**: Type-specific consensus rules applied
4. **Future-Proof**: 256 possible types, 256 flag combinations

---

# SECTION 2: COLLATERALIZATION MECHANICS

## 2.1 The 9-Tier Sliding Scale

### Complete Specification

| Lock Period | Collateral Ratio | Survives Price Drop | Example ($100 DD) |
|-------------|------------------|---------------------|-------------------|
| 1 hour*     | 1000%           | -90%                | 1000 DGB          |
| 30 days     | 500%            | -80%                | 500 DGB           |
| 3 months    | 400%            | -75%                | 400 DGB           |
| 6 months    | 350%            | -71.4%              | 350 DGB           |
| 1 year      | 300%            | -66.7%              | 300 DGB           |
| 3 years     | 250%            | -60%                | 250 DGB           |
| 5 years     | 225%            | -55.6%              | 225 DGB           |
| 7 years     | 212%            | -52.8%              | 212 DGB           |
| 10 years    | 200%            | -50%                | 200 DGB           |

*Testing only (regtest/testnet)

### Design Rationale

```
Economic Model: Similar to US Treasury Bonds
├── Shorter lock = Higher risk = More collateral
├── Longer lock = Lower risk = Less collateral
├── Attack resistance: 1000% prevents flash loan attacks
└── Incentive: Capital efficiency rewards long-term commitment
```

### Calculation Formula

```cpp
int GetCollateralRatio(int lockTier) {
    const int ratios[] = {1000, 500, 400, 350, 300, 250, 225, 212, 200};
    return (lockTier >= 0 && lockTier < 9) ? ratios[lockTier] : 200;
}

CAmount CalculateCollateral(CAmount ddAmount, int lockTier, CAmount oraclePrice) {
    int ratio = GetCollateralRatio(lockTier);
    double dcaMultiplier = GetDCAMultiplier(); // 1.0x - 2.0x

    // Formula: (DD_cents × ratio% × DCA) / (oracle_price_microUSD / 1,000,000)
    return (ddAmount * ratio * dcaMultiplier * COIN) / (oraclePrice / 1000000);
}
```

---

## 2.2 CCollateralPosition Structure

### Complete Field Breakdown

```cpp
// From digidollar.h:65-99
class CCollateralPosition {
public:
    COutPoint outpoint;         // The locked DGB UTXO (txid + vout)
    CAmount dgbLocked;          // DGB in satoshis (8 decimals)
    CAmount ddMinted;           // DD in cents (2 decimals)
    int collateralRatio;        // Initial ratio (200-1000%)
    int64_t unlockHeight;       // Block height when timelock expires
    std::vector<RedemptionPath> availablePaths;  // Active paths

    SERIALIZE_METHODS(CCollateralPosition, obj) {
        READWRITE(obj.outpoint);
        READWRITE(obj.dgbLocked);
        READWRITE(obj.ddMinted);
        READWRITE(obj.unlockHeight);
        READWRITE(obj.collateralRatio);
        READWRITE(obj.availablePaths);
    }
};
```

### Field Sizes

| Field | Type | Size | Example |
|-------|------|------|---------|
| outpoint | COutPoint | 36 bytes | (txid: 0xabc..., vout: 0) |
| dgbLocked | CAmount | 8 bytes | 50000000000 (500 DGB) |
| ddMinted | CAmount | 8 bytes | 10000 ($100.00) |
| unlockHeight | int64_t | 8 bytes | 1050000 |
| collateralRatio | int | 4 bytes | 500 |
| availablePaths | vector | variable | [PATH_NORMAL, PATH_ERR] |

---

## 2.3 Time-Lock Mechanisms (OP_CHECKLOCKTIMEVERIFY)

### Script Implementation

```cpp
CScript CreateCollateralScript(const TxBuilderMintParams& params) {
    CScript script;

    // Timelock enforcement
    script << params.unlockHeight;          // Push unlock height
    script << OP_CHECKLOCKTIMEVERIFY;       // Verify nLockTime >= height
    script << OP_DROP;                       // Clean stack

    // Key verification (Taproot)
    script << OP_1;                          // Witness version 1
    script << ToByteVector(params.taprootOutput);  // 32-byte key

    return script;
}
```

### Why Block Height Instead of Unix Time?

1. **Deterministic**: Block height unambiguous across all nodes
2. **No Clock Drift**: Immune to system clock manipulation
3. **Consensus-Safe**: Block timestamps can vary, heights cannot
4. **DigiByte-Specific**: 15-second blocks enable precise timing

### Lock Period Calculations

```
30-day lock:  Current + (30 × 24 × 60 × 60) / 15 = +172,800 blocks
1-year lock:  Current + 2,102,400 blocks
10-year lock: Current + 21,024,000 blocks
```

---

## 2.4 The Two Redemption Paths

### Path Architecture (Post RC5 Simplification)

```cpp
enum RedemptionPath {
    PATH_NORMAL = 0,     // 100% collateral return, burn exact DD owed
    PATH_ERR = 1         // 100% collateral return, burn MORE DD (105-125%)
};
```

### Normal Redemption (PATH_NORMAL)

**Conditions**:
```cpp
bool CanUseNormalPath(const CCollateralPosition& pos, int currentHeight, int systemHealth) {
    // REQUIREMENT 1: Timelock expired
    if (currentHeight < pos.unlockHeight) return false;

    // REQUIREMENT 2: System healthy (≥100% collateralized)
    if (systemHealth < 100) return false;

    return true;  // Full collateral return
}
```

### Emergency Redemption Ratio (PATH_ERR)

**Tiered Haircuts**:

| System Health | Return | Haircut | Example (500 DGB) |
|---------------|--------|---------|-------------------|
| 95-100%       | 95%    | 5%      | 475 DGB           |
| 90-95%        | 90%    | 10%     | 450 DGB           |
| 85-90%        | 85%    | 15%     | 425 DGB           |
| <85%          | 80%    | 20%     | 400 DGB           |

**Implementation**:
```cpp
CAmount GetAdjustedRedemption(CAmount normalRedemption, int systemHealth) {
    if (normalRedemption <= 0 || systemHealth >= 100)
        return normalRedemption;  // Healthy = full amount

    double adjustmentRatio;
    if (systemHealth >= 95)      adjustmentRatio = 0.95;
    else if (systemHealth >= 90) adjustmentRatio = 0.90;
    else if (systemHealth >= 85) adjustmentRatio = 0.85;
    else                         adjustmentRatio = 0.80;

    return static_cast<CAmount>(normalRedemption * adjustmentRatio);
}
```

**Critical Constraint**: ERR **STILL REQUIRES** timelock expiry. No early redemption exists.

---

# SECTION 3: MINTING FLOW - COMPLETE TECHNICAL DETAILS

## 3.1 Step-by-Step Transaction Construction

### Complete Minting Pipeline

```
STEP 1: Parameter Validation
    ├── DD amount: $100 minimum, $100,000 maximum
    └── Lock tier: 0-8 (9 tiers)

STEP 2: Oracle Price Query
    └── GetCurrentOraclePrice() → 6500 micro-USD ($0.0065)

STEP 3: System Health Check (DCA)
    ├── GetTotalSystemCollateral()
    ├── GetTotalDDSupply()
    └── DCA Multiplier: 1.0x - 2.0x

STEP 4: Collateral Calculation
    └── Required DGB = (DD × Ratio × DCA) / Price

STEP 5: UTXO Selection
    └── Greedy algorithm selects DGB UTXOs

STEP 6: Transaction Construction
    ├── Output 0: Collateral vault (P2TR + MAST + CLTV)
    ├── Output 1: DD token (Simple P2TR, key-path only)
    ├── Output 2: OP_RETURN metadata
    └── Output 3+: Change outputs

STEP 7: Signing & Broadcasting
    └── Schnorr for P2TR, ECDSA for fee inputs

STEP 8: Database Update
    └── Create WalletCollateralPosition
```

---

## 3.2 Output Structures

### Output 0: Collateral Vault (Complex P2TR)

```cpp
CScript CreateCollateralScript(const TxBuilderMintParams& params) {
    // PART 1: Timelock enforcement
    CScript timelockScript;
    timelockScript << params.unlockHeight;
    timelockScript << OP_CHECKLOCKTIMEVERIFY;
    timelockScript << OP_DROP;

    // PART 2: Normal redemption path
    CScript normalPath;
    normalPath << timelockScript;
    normalPath << OP_1;
    normalPath << ToByteVector(params.ownerKey.GetPubKey().GetHash());

    // PART 3: ERR redemption path
    CScript errPath;
    errPath << timelockScript;
    errPath << OP_1;
    errPath << ToByteVector(params.errKey.GetPubKey().GetHash());

    // PART 4: Create MAST tree
    uint256 normalLeaf = TaprootLeafHash(normalPath);
    uint256 errLeaf = TaprootLeafHash(errPath);
    uint256 merkleRoot = TaprootBranch(normalLeaf, errLeaf);

    // PART 5: Final P2TR output
    CScript finalScript;
    finalScript << OP_1;
    finalScript << ToByteVector(merkleRoot);

    return finalScript;
}
```

**Byte Structure**:
```
Byte 0:      0x51 (OP_1 = Taproot version)
Bytes 1-32:  [32-byte merkle root]
Total:       33 bytes
```

### Output 1: DD Token (Simple P2TR)

**Critical Difference**: DD tokens use simple P2TR WITHOUT MAST or timelocks.

```cpp
CScript CreateDDOutputScript(const CKey& owner, CAmount amount) {
    CPubKey pubkey = owner.GetPubKey();
    XOnlyPubKey xonly(pubkey);  // x-coordinate only

    CScript script;
    script << OP_1;              // Taproot version
    script << ToByteVector(xonly);  // 32-byte x-only pubkey

    // NOTE: NO merkle root, NO scripts, NO timelock
    return script;
}
```

**Witness Comparison**:

| Type | Witness Structure | Size |
|------|-------------------|------|
| DD Token (Key-Path) | [64-byte signature] | ~66 bytes |
| Collateral (Script-Path) | [signature][script][control block] | ~150 bytes |

### Output 2: OP_RETURN Metadata

```cpp
CScript CreateDDMetadata(const TxBuilderMintParams& params) {
    CScript script;
    script << OP_RETURN;
    script << std::vector<unsigned char>{'D', 'D'};  // Marker
    script << static_cast<unsigned char>(DD_TX_MINT);

    std::vector<unsigned char> amountBytes(8);
    WriteLE64(&amountBytes[0], params.ddAmount);
    script << amountBytes;

    std::vector<unsigned char> collateralBytes(8);
    WriteLE64(&collateralBytes[0], params.collateralRequired);
    script << collateralBytes;

    script << static_cast<unsigned char>(params.lockTier);

    return script;  // 21 bytes total
}
```

**Byte-by-Byte Format**:
```
Byte 0:      0x6a (OP_RETURN)
Bytes 1-2:   'DD' (0x4444)
Byte 3:      0x01 (MINT type)
Bytes 4-11:  DD amount in cents (little-endian uint64)
Bytes 12-19: Collateral in satoshis (little-endian uint64)
Byte 20:     Lock tier (0-8)
```

---

# SECTION 4: ORACLE SYSTEM ARCHITECTURE

## 4.1 Phase One vs Phase Two

### Phase One (Current - Testnet/Regtest)

```cpp
// Consensus: Exactly 1 oracle message required
if (bundle.messages.size() != 1) {
    return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS,
                        "bad-oracle-consensus",
                        "Phase One requires exactly 1 oracle message");
}
```

**Characteristics**:
- Single oracle (oracle_id = 0)
- Signature verification off-chain
- Compact 20-byte blockchain format
- Active on testnet/regtest only

### Phase Two (Planned - Mainnet)

```cpp
// Consensus: Minimum 8 of 15 signatures
if (bundle.messages.size() < 8) {
    return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS,
                        "bad-oracle-consensus",
                        "Phase Two requires minimum 8-of-15 signatures");
}
```

**Epoch System**:
```cpp
// 30 total oracles, 15 active per epoch
std::vector<uint32_t> SelectActiveOracles(int32_t epoch) {
    uint256 seed = Hash(BEGIN(epoch), END(epoch));
    std::vector<uint32_t> candidates;
    for (uint32_t i = 0; i < 30; ++i) candidates.push_back(i);
    Shuffle(candidates.begin(), candidates.end(), seed);
    return std::vector<uint32_t>(candidates.begin(), candidates.begin() + 15);
}
```

| Network | Epoch Size | Duration |
|---------|------------|----------|
| Mainnet | 100 blocks | ~25 min  |
| Testnet | 50 blocks  | ~12.5 min |
| Regtest | 10 blocks  | ~2.5 min |

---

## 4.2 Micro-USD Price Format

### Specification

```cpp
uint64_t price_micro_usd;  // 1,000,000 = $1.00 USD

static constexpr uint64_t MIN_PRICE_MICRO_USD = 100;        // $0.0001
static constexpr uint64_t MAX_PRICE_MICRO_USD = 100000000;  // $100.00
```

### Conversion Examples

| User Format | Micro-USD | Hex (Little-Endian) |
|-------------|-----------|---------------------|
| $0.0001     | 100       | 0x6400000000000000  |
| $0.0065     | 6,500     | 0x6419000000000000  |
| $1.00       | 1,000,000 | 0x40420F0000000000  |

### Why Micro-USD?
1. **Precision**: 6 decimal places
2. **Integer Math**: No floating-point errors
3. **Range**: $0.0001 to $100 per DGB
4. **Standard**: Common in financial APIs

---

## 4.3 Exchange API Aggregation

### Active Exchanges (7)

1. **CoinGecko** - Aggregator, most reliable
2. **CryptoCompare** - Professional API
3. **Binance** - Largest volume
4. **KuCoin** - Major altcoin exchange
5. **Gate.io** - Asian market leader
6. **HTX (Huobi)** - Established exchange
7. **Crypto.com** - Growing exchange

### Parallel Fetching

```cpp
std::vector<uint64_t> FetchAllPrices() {
    std::vector<std::future<uint64_t>> futures;

    // Launch 7 parallel HTTP requests
    futures.push_back(std::async(std::launch::async, FetchCoinGeckoPrice));
    futures.push_back(std::async(std::launch::async, FetchBinancePrice));
    // ... etc

    std::vector<uint64_t> prices;
    for (auto& future : futures) {
        uint64_t price = future.get();
        if (price > 0) prices.push_back(price);
    }
    return prices;
}
```

### MAD Outlier Filtering

```cpp
std::vector<uint64_t> FilterOutliers(const std::vector<uint64_t>& prices) {
    // Calculate median
    std::vector<uint64_t> sorted = prices;
    std::sort(sorted.begin(), sorted.end());
    uint64_t median = sorted[sorted.size() / 2];

    // Calculate MAD (Median Absolute Deviation)
    std::vector<uint64_t> deviations;
    for (uint64_t price : prices) {
        deviations.push_back(std::abs(static_cast<int64_t>(price - median)));
    }
    std::sort(deviations.begin(), deviations.end());
    uint64_t mad = deviations[deviations.size() / 2];

    // Filter: Keep prices within 3×MAD of median
    std::vector<uint64_t> filtered;
    for (uint64_t price : prices) {
        if (std::abs(static_cast<int64_t>(price - median)) <= 3 * mad) {
            filtered.push_back(price);
        }
    }
    return filtered;
}
```

**Example**:
```
Raw:     [6500, 6520, 6480, 9999, 6510, 6490, 6505]
                              ↑ Outlier (53% above median)
Median:  6505
MAD:     15
3×MAD:   45
Filtered: [6500, 6520, 6480, 6510, 6490, 6505]
Final:   6502 micro-USD
```

---

## 4.4 Compact Blockchain Storage Format

### 22-Byte Structure

```
Byte 0:      0x6a (OP_RETURN)
Byte 1:      0xbf (OP_ORACLE - custom opcode)
Byte 2:      0x01 (PUSH 1 byte)
Byte 3:      0x01 (Version = Phase One)
Byte 4:      0x11 (PUSH 17 bytes)
Byte 5:      0x00 (Oracle ID)
Bytes 6-13:  [Price in micro-USD, little-endian uint64]
Bytes 14-21: [Timestamp, little-endian int64]
```

### Space Efficiency

```
Full P2P Format:        128 bytes (includes signature)
Compact Block Format:    22 bytes
Savings:                82.8% reduction

Annual Growth Comparison:
  Full:    269 MB/year
  Compact:  46 MB/year
  Savings: 223 MB/year
```

### OP_ORACLE (0xbf)

```cpp
// Custom opcode for fast detection
OP_ORACLE = 0xbf,  // OP_NOP15 repurposed

bool HasOracleData(const CBlock& block) {
    const CTransaction& coinbase = *block.vtx[0];
    for (const auto& output : coinbase.vout) {
        if (script[0] == OP_RETURN && script[1] == OP_ORACLE) {
            return true;  // O(1) detection
        }
    }
    return false;
}
```

---

## 4.5 Block Validation Flow

### Complete Validation Chain

```cpp
bool ValidateBlockOracleData(const CBlock& block, ...) {
    // CHECK 1: Network filter (testnet/regtest only)
    if (chainType != ChainType::TESTNET && chainType != ChainType::REGTEST)
        return true;

    // CHECK 2: Activation height
    if (blockHeight < params.nDDActivationHeight)
        return true;

    // CHECK 3: Extract oracle bundle
    COracleBundle bundle;
    if (!ExtractOracleBundle(*block.vtx[0], bundle))
        return true;  // Transition period

    // CHECK 4: Bundle structure
    if (!bundle.IsValid())
        return state.Invalid(..., "bad-oracle-bundle");

    // CHECK 5: Message count (Phase One: exactly 1)
    if (bundle.messages.size() != 1)
        return state.Invalid(..., "bad-oracle-consensus");

    // CHECK 6: Median verification
    if (bundle.median_price_micro_usd != msg.price_micro_usd)
        return state.Invalid(..., "bad-oracle-median");

    // CHECK 7: Timestamp age (max 1 hour old)
    if (block.nTime - msg.timestamp > 3600)
        return state.Invalid(..., "bad-oracle-timestamp");

    // CHECK 8: Future timestamp (max 60 seconds)
    if (msg.timestamp > block.nTime + 60)
        return state.Invalid(..., "bad-oracle-timestamp");

    // CHECK 9: Oracle authorization
    if (!IsAuthorizedOracle(msg.oracle_id))
        return state.Invalid(..., "bad-oracle-unauthorized");

    return true;
}
```

---

# SECTION 5: PROTECTION SYSTEMS

## 5.1 Dynamic Collateral Adjustment (DCA)

### System Health Calculation

```cpp
int CalculateSystemHealth() {
    CAmount totalCollateral = GetTotalSystemCollateral();
    CAmount totalDD = GetTotalDDSupply();
    CAmount oraclePrice = GetCurrentOraclePrice();

    // Health = (Collateral Value) / (DD Value) × 100%
    int64_t collateralValueUSD = (totalCollateral * oraclePrice) / (COIN * 1000000);
    int64_t ddValueUSD = totalDD / 100;

    if (ddValueUSD == 0) return 100;
    return (collateralValueUSD * 100) / ddValueUSD;
}
```

### DCA Multiplier Table

| Health | Tier | Multiplier | Effect |
|--------|------|------------|--------|
| ≥150%  | Healthy | 1.0x | Normal |
| 120-149% | Warning | 1.2x | +20% collateral |
| 100-119% | Critical | 1.5x | +50% collateral |
| <100% | Emergency | 2.0x | +100% collateral |

### Implementation

```cpp
double GetDCAMultiplier() {
    int health = CalculateSystemHealth();
    if (health >= 150) return 1.0;
    if (health >= 120) return 1.2;
    if (health >= 100) return 1.5;
    return 2.0;
}
```

---

## 5.2 Emergency Redemption Ratio (ERR)

### Activation

```cpp
bool IsERRActive() {
    return (CalculateSystemHealth() < 100);
}
```

### Tiered Haircut Implementation

```cpp
CAmount GetAdjustedRedemption(CAmount normalRedemption, int systemHealth) {
    if (systemHealth >= 100) return normalRedemption;

    double ratio;
    if (systemHealth >= 95) ratio = 0.95;      // 5% haircut
    else if (systemHealth >= 90) ratio = 0.90; // 10% haircut
    else if (systemHealth >= 85) ratio = 0.85; // 15% haircut
    else ratio = 0.80;                          // 20% max

    return static_cast<CAmount>(normalRedemption * ratio);
}
```

### Key Constraint

**ERR still requires timelock expiry**. No early redemption mechanism exists.

---

## 5.3 Volatility Protection

### Freeze Thresholds

| Timeframe | Threshold | Action | Cooldown |
|-----------|-----------|--------|----------|
| 1 hour | 10% | Warning | None |
| 1 hour | 20% | Freeze new mints | 144 blocks (~36h) |
| 24 hours | 30% | Freeze all DD ops | 288 blocks (~72h) |
| 7 days | 50% | Emergency mode | Manual override |

### Implementation

```cpp
bool IsVolatilityFreeze() const {
    int64_t now = GetTime();
    uint64_t price1HourAgo = GetPriceAt(now - 3600);
    uint64_t currentPrice = GetCurrentPrice();

    double change = std::abs(static_cast<double>(currentPrice - price1HourAgo)) /
                    price1HourAgo;

    if (change >= 0.20) {
        LogPrintf("Volatility freeze: %.1f%% change\n", change * 100);
        return true;
    }
    return false;
}
```

---

## 5.4 Network-Wide UTXO Scanning

### System Health Monitor

```cpp
void SystemHealthMonitor::ScanUTXOSet(CCoinsView* view, ...) {
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());

    while (pcursor->Valid()) {
        COutPoint key;
        Coin coin;
        pcursor->GetKey(key);
        pcursor->GetValue(coin);

        // Identify potential DD vault outputs
        // - Output index 0
        // - P2TR script
        // - Has DGB value

        if (key.n == 0 &&
            coin.out.scriptPubKey[0] == OP_1 &&
            coin.out.nValue > 0) {

            // Fetch full transaction
            CTransactionRef tx = GetTransaction(..., key.hash, ...);

            // Validate DD structure (3+ outputs, OP_RETURN metadata)
            if (tx->vout.size() >= 3 &&
                tx->vout[2].scriptPubKey[0] == OP_RETURN) {

                // Extract DD amount from metadata
                CAmount ddAmount;
                ExtractDDAmount(tx->vout[2].scriptPubKey, ddAmount);

                // Accumulate
                s_currentMetrics.totalDDSupply += ddAmount;
                s_currentMetrics.totalCollateral += coin.out.nValue;
                s_currentMetrics.vaultCount++;
            }
        }
        pcursor->Next();
    }

    // Calculate system health
    s_currentMetrics.systemHealth = CalculateHealth();
}
```

### Key Innovation

Every node can independently verify system health by scanning the UTXO set. No external indexer required.

---

# SECTION 6: WHY DIGIDOLLAR AVOIDS LUNA-STYLE FAILURES

## 6.1 No Algorithmic Minting

| Feature | LUNA/UST | DigiDollar |
|---------|----------|------------|
| Collateral | 0-100% (algorithmic) | 200-1000% (enforced) |
| Exit Speed | Instant | 30 days - 10 years |
| Price Anchor | Algorithmic arbitrage | Oracle + collateral |
| Death Spiral Protection | None | 4-layer system |

## 6.2 No Death Spiral Mechanism

**LUNA Death Spiral**:
```
UST loses peg → Burn UST, mint LUNA → LUNA dumps →
More panic → More minting → Total collapse
```

**Why Impossible in DigiDollar**:

1. **No mint-on-demand arbitrage**: Must lock real DGB collateral
2. **Time-locked collateral**: Cannot exit early regardless of price
3. **No infinite minting**: Limited by 21B DGB supply
4. **Four-layer protection**: Adapts without liquidating

## 6.3 No Forced Liquidations

```cpp
// This function DOES NOT EXIST in DigiDollar
bool AttemptLiquidation(const CCollateralPosition& pos) {
    // There is NO CODE PATH to liquidate
    // Collateral is locked until timelock expires
    return false;  // Cannot liquidate
}
```

Traditional DeFi destroys positions during crashes. DigiDollar positions simply wait.

---

# SECTION 7: CONSENSUS VS WALLET RULES

## 7.1 Consensus Rules (All nodes MUST agree)

| Rule | Enforcement |
|------|-------------|
| Transaction version (0x0D1D0770) | Block rejected if invalid |
| Oracle data in blocks | Block rejected if missing/invalid |
| Oracle message count | Block rejected if wrong |
| Oracle timestamp | Block rejected if stale/future |
| Oracle authorization | Block rejected if unauthorized |

## 7.2 Wallet Rules (Can vary)

| Rule | Type |
|------|------|
| Minimum mint ($100) | Wallet policy |
| Maximum mint ($100,000) | Wallet policy |
| Fee requirements | Policy (not consensus) |
| Address format validation | Wallet-level |

## 7.3 Activation Heights

| Network | DD Activation | Oracle Activation | Epoch Size |
|---------|---------------|-------------------|------------|
| Mainnet | 18,500,000 | 18,500,000 | 100 blocks |
| Testnet | 1,000,000 | 1,000,000 | 50 blocks |
| Regtest | 100 | 100 | 10 blocks |

---

# SECTION 8: TEST COVERAGE

## 8.1 Unit Tests: 409 Total

### DigiDollar Tests (286)

| Test File | Count |
|-----------|-------|
| digidollar_activation_tests.cpp | 5 |
| digidollar_address_tests.cpp | 11 |
| digidollar_consensus_tests.cpp | 11 |
| digidollar_dca_tests.cpp | 22 |
| digidollar_mint_tests.cpp | 29 |
| digidollar_opcodes_tests.cpp | 21 |
| digidollar_oracle_tests.cpp | 35 |
| digidollar_p2p_tests.cpp | 12 |
| digidollar_scripts_tests.cpp | 13 |
| digidollar_timelock_tests.cpp | 38 |
| digidollar_transaction_tests.cpp | 19 |
| digidollar_wallet_tests.cpp | 15 |
| (+ others) | ... |

### Oracle Tests (123)

| Test File | Count |
|-----------|-------|
| oracle_exchange_tests.cpp | 56 |
| oracle_message_tests.cpp | 15 |
| oracle_p2p_tests.cpp | 17 |
| oracle_config_tests.cpp | 13 |
| (+ others) | ... |

## 8.2 Functional Tests: 18

- `digidollar_basic.py` - Core functionality
- `digidollar_mint.py` - Minting process
- `digidollar_transfer.py` - Transfer operations
- `digidollar_redeem.py` - Redemption
- `digidollar_oracle.py` - Oracle integration
- `digidollar_protection.py` - DCA/ERR/Volatility
- `digidollar_network_tracking.py` - UTXO scanning
- (+ 11 others)

## 8.3 Implementation Status: 85%

| Component | Completion |
|-----------|------------|
| Core Data Structures | 95% |
| Address System | 100% |
| Minting Process | 95% |
| Transfer System | 98% |
| Redemption System | 75% |
| Network Tracking | 100% |
| Oracle (Phase 1) | 95% |
| Protection Systems | 95% |
| GUI | 92% |
| Test Coverage | 100% |

**Production Readiness**:
- **Testnet**: Ready NOW
- **Mainnet**: Requires Phase 2 oracle + security audit (8-12 weeks)

---

# SECTION 9: TECHNICAL Q&A REFERENCE

## Common Developer Questions

### "How are DD amounts tracked through transfers?"

DD tokens have 0 DGB value. The amount is stored in OP_RETURN metadata and tracked via `std::map<COutPoint, CAmount> dd_utxos` mapping (txid, vout) → cents.

### "What prevents oracle manipulation?"

Phase 1: Single authorized oracle from chainparams.
Phase 2: 8-of-15 threshold signatures with MAD outlier filtering.
All prices must be within 3×MAD of median to be accepted.

### "How does system health get calculated across nodes?"

Every node scans the UTXO set (like `gettxoutsetinfo`) to identify DD vault outputs, extract collateral amounts from vout[0] and DD amounts from OP_RETURN metadata, then calculates health percentage identically.

### "What's the minimum/maximum DD amount?"

Wallet policy: $100 minimum, $100,000 maximum.
Not consensus-enforced—could theoretically be bypassed with raw transactions.

### "Can I redeem early if there's an emergency?"

No. OP_CHECKLOCKTIMEVERIFY is cryptographically enforced. No code path exists for early redemption.

---

# APPENDIX: KEY CODE REFERENCES

| Component | File | Line |
|-----------|------|------|
| DD_TX_VERSION | primitives/transaction.h | 46-56 |
| CCollateralPosition | digidollar/digidollar.h | 65-99 |
| CreateCollateralScript | digidollar/txbuilder.cpp | 185-198 |
| CreateDDOutputScript | digidollar/txbuilder.cpp | 200-210 |
| GetDCAMultiplier | consensus/dca.cpp | 50-65 |
| GetAdjustedRedemption | consensus/err.cpp | 74-91 |
| ValidateBlockOracleData | validation.cpp | 4065-4135 |
| ScanUTXOSet | digidollar/health.cpp | 274-420 |
| OP_ORACLE | script/script.h | 214 |

---

*Document synthesized from DigiDollar reference architecture, oracle system documentation, and technical implementation analysis.*
*Implementation status: 85% complete | 409 unit tests | 18 functional tests*
*Last updated: December 2024*
