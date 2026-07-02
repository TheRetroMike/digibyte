# DigiDollar: Complete Technical Deep Dive
## The World's First UTXO-Native Decentralized Stablecoin

**Version**: 2.0 - Comprehensive Multi-Part Presentation
**Last Updated**: December 2025
**Target Audience**: Developers, Researchers, Investors, Technical Reviewers
**Total Duration**: 4-6 hours (modular - can be delivered in parts)

---

## EXECUTIVE SUMMARY

DigiDollar is the world's first truly decentralized stablecoin built natively on a UTXO blockchain. Unlike traditional stablecoins controlled by corporations (USDT, USDC) or complex smart contract systems (DAI), DigiDollar operates without any central authority. Every DigiDollar is backed by locked DigiByte (DGB) coins held in secure, time-locked digital vaults that users control with their own private keys.

### Key Value Propositions

| Feature | DigiDollar | USDT/USDC | DAI |
|---------|------------|-----------|-----|
| **Control** | User's own keys | Corporate custody | Smart contract |
| **Freezable** | No (impossible) | Yes (routinely) | Partially |
| **Collateral Visibility** | 100% on-chain | Opaque reserves | On-chain |
| **Transaction Cost** | $0.001 | $2-15 (Ethereum) | $2-50 |
| **Confirmation Time** | 90 seconds | 2-15 minutes | 2-15 minutes |
| **Death Spiral Risk** | Impossible | N/A (fiat-backed) | Possible |

### Implementation Status

- **85% Complete** - ~50,000+ lines of production-quality code
- **427 Tests** - 286 DigiDollar unit tests + 123 Oracle tests + 18 functional tests
- **27+ RPC Commands** - Full programmatic interface
- **7-Tab Qt Wallet** - Complete GUI implementation
- **Target Release**: DigiByte v8.26

---

## HIGH-LEVEL OUTLINE

This presentation is structured into three major parts plus supplementary materials:

### PART 1: WHY DIGIDOLLAR ON DIGIBYTE (60-90 minutes)
*The complete historical and architectural case for why DigiDollar must exist on DigiByte*

- 1.1 DigiByte's Origin Story (2014) - Fair launch, no ICO, no pre-mine
- 1.2 Twelve Years of Uninterrupted Uptime - Zero downtime, zero attacks
- 1.3 DigiShield to MultiShield Evolution - Security innovation history
- 1.4 Speed Advantage - 40x faster than Bitcoin (15-second blocks)
- 1.5 Ultra-Low Fees - 2,000x-5,000x cheaper than Ethereum
- 1.6 Five Mining Algorithms - Unparalleled decentralization
- 1.7 High Throughput - 1,066 TPS demonstrated
- 1.8 True Decentralization - No company, no CEO, no control
- 1.9 Competitor Comparison - Why no other blockchain qualifies
- 1.10 DigiDollar's Role in the Ecosystem

**Full Content**: See `DIGIDOLLAR_DEEPDIVE_PART1.md` (2,500+ lines)

---

### PART 2: DIGIDOLLAR TECHNICAL ARCHITECTURE (90-120 minutes)
*Complete technical specifications for developers and auditors*

- 2.1 System Architecture Overview - 7-layer design
- 2.2 UTXO-Native Design Philosophy - Why not smart contracts
- 2.3 Transaction Version Encoding - Magic number 0x0D1D0770
- 2.4 Custom Opcodes - 5 new opcodes (OP_DIGIDOLLAR, OP_DDVERIFY, etc.)
- 2.5 9-Tier Collateral Scale - 200%-1000% based on lock period
- 2.6 CCollateralPosition Structure - Vault data format
- 2.7 Time-Lock Mechanisms - OP_CHECKLOCKTIMEVERIFY
- 2.8 MINT Transaction Flow - Complete minting process
- 2.9 TRANSFER Transaction Flow - DD token transfers
- 2.10 REDEEM Transaction Flow - Two redemption paths
- 2.11 DD UTXO Tracking System - std::map<COutPoint, CAmount>
- 2.12 Protection Systems - DCA, ERR, Volatility Freeze
- 2.13 System Health Calculation - Network-wide UTXO scanning
- 2.14 RPC Interface Reference - All 27+ commands
- 2.15 Qt Wallet Implementation - 7 functional tabs

---

### PART 3: ORACLE SYSTEM DEEP DIVE (45-60 minutes)
*Comprehensive oracle architecture and security analysis*

- 3.1 Why Oracles Are Necessary - The blockchain blind spot
- 3.2 Phase One Architecture - 1-of-1 consensus (testnet)
- 3.3 Phase Two Vision - 8-of-15 multi-oracle consensus
- 3.4 Exchange Price Aggregation - 7+ active exchange APIs
- 3.5 MAD Outlier Filtering - Statistical manipulation resistance
- 3.6 Micro-USD Price Format - 6 decimal precision
- 3.7 OP_ORACLE Coinbase Format - 22-byte compact storage
- 3.8 P2P Message Propagation - Network-wide price distribution
- 3.9 Block Validation Flow - CheckBlock oracle verification
- 3.10 Attack Resistance Analysis - Security properties

**Full Content**: See `DIGIDOLLAR_ORACLE_EXPLAINER.md` (1,500+ lines)

---

### PART 4: ECOSYSTEM & FUTURE POSSIBILITIES (45-60 minutes)
*Time-lock use cases, secondary markets, and cottage industries*

**Separate Document**: See `DigiDollar_Ecosystem_and_Timelock_UseCases.md`

---

## PART 2: DIGIDOLLAR TECHNICAL ARCHITECTURE

### 2.1 System Architecture Overview

```
┌────────────────────────────────────────────────────────────────┐
│                 DIGIDOLLAR SYSTEM ARCHITECTURE                  │
│                    (7-Layer Design)                             │
└────────────────────────────────────────────────────────────────┘

LAYER 7: APPLICATION INTERFACE
┌──────────────────────────────────────────────────────────────┐
│  Qt Wallet (7 tabs) + DigiDollar Manager                     │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌────┐│
│  │Over- ││Receive││ Send ││ Mint ││Redeem││Posit-││Trans││
│  │view  ││      ││      ││      ││      ││ions  ││     ││
│  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘ └────┘│
└──────────────────────────────────────────────────────────────┘
                           │ RPC Interface (27+ commands)
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
│  │ High Base    │ │ DCA (1.0-2x) │ │ ERR (80-95%) │          │
│  │ Collateral   │ │ Multipliers  │ │ Haircuts     │          │
│  │ (200-1000%)  │ │              │ │              │          │
│  └──────────────┘ └──────────────┘ └──────────────┘          │
│  ┌──────────────┐                                            │
│  │ Volatility   │                                            │
│  │ Freeze (20%) │                                            │
│  └──────────────┘                                            │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 4: ORACLE SYSTEM
┌──────────────────────────────────────────────────────────────┐
│  Decentralized Price Feed Network                            │
│  7 Exchange APIs → MAD Filtering → Median → Coinbase Storage │
│  Phase 1: 1-of-1 | Phase 2: 8-of-15 Schnorr threshold        │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 3: CONSENSUS & VALIDATION
┌──────────────────────────────────────────────────────────────┐
│  DigiByte Consensus Rules                                    │
│  • Transaction version validation (0x0D1D0770)               │
│  • Oracle data validation in CheckBlock()                    │
│  • Collateral ratio enforcement                              │
│  • Timelock verification (OP_CHECKLOCKTIMEVERIFY)            │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 2: SCRIPT & STORAGE (UTXO Model)
┌──────────────────────────────────────────────────────────────┐
│  DigiByte Blockchain                                         │
│  • P2TR outputs (Taproot/Schnorr)                            │
│  • OP_RETURN metadata (21-byte DD format)                    │
│  • MAST redemption paths for collateral                      │
│  • Simple key-path for DD tokens                             │
└──────────────────────────────────────────────────────────────┘
                           │
                           ▼
LAYER 1: CRYPTOGRAPHIC PRIMITIVES
┌──────────────────────────────────────────────────────────────┐
│  P2P Network + Cryptographic Layer                           │
│  • Schnorr Signatures (BIP-340)                              │
│  • Taproot P2TR (BIP-341)                                    │
│  • OP_CHECKLOCKTIMEVERIFY (BIP-65)                           │
│  • 5-algorithm PoW (SHA256D, Scrypt, Skein, Qubit, Odocrypt) │
└──────────────────────────────────────────────────────────────┘
```

---

### 2.2 UTXO-Native Design Philosophy

#### Why UTXO Instead of Smart Contracts?

| Feature | UTXO Model (DigiDollar) | Account Model (Ethereum) |
|---------|-------------------------|--------------------------|
| State | Independent coin states | Global contract state |
| Parallelism | Fully parallel spending | Sequential nonces |
| Auditability | All on-chain transparent | Contract logic hidden |
| Freezability | Impossible by design | Contract owner can freeze |
| Attack surface | Bitcoin Script only | Smart contract bugs |
| Upgradability | Protocol-level (hard fork) | Contract owner can upgrade |

#### The Key Innovation: DD UTXO Tracking

DigiDollar tokens have 0 DGB value in the UTXO itself. The DD amount is stored in metadata:

```cpp
// DD tokens are tracked via explicit mapping
std::map<COutPoint, CAmount> dd_utxos;

// Example flow:
// Transaction A creates DD:
//   vout[1]: 0 DGB, P2TR script, metadata says "500 DD"
//   → Map: (TxA, 1) → 50000 cents

// Transaction B splits:
//   vin[0]: (TxA, 1) ← Spending 500 DD
//   vout[0]: 300 DD to Alice
//   vout[1]: 200 DD change
//   → Remove: (TxA, 1)
//   → Add: (TxB, 0) → 30000 cents
//   → Add: (TxB, 1) → 20000 cents
```

---

### 2.3 Transaction Version Encoding

#### The Magic Number: 0x0D1D0770

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

#### Bit Layout

```
┌────────────────────────────────────────────────────────────┐
│ Bits 31-24 │ Bits 23-16 │ Bits 15-0                       │
│ TX Type    │ Flags      │ DD Marker (0x0770)              │
└────────────────────────────────────────────────────────────┘

Transaction Version Values:
  0x01000770 = MINT     (Create DigiDollars)
  0x02000770 = TRANSFER (Send DigiDollars)
  0x03000770 = REDEEM   (Burn DigiDollars)
  0x04000770 = PARTIAL  (Partial redemption)
  0x05000770 = ERR      (Emergency Redemption)
```

---

### 2.4 Custom Opcodes

DigiDollar introduces 5 new opcodes by repurposing OP_NOP11-15:

| Opcode | Hex | Purpose | Location |
|--------|-----|---------|----------|
| OP_DIGIDOLLAR | 0xbb | DD output marker | src/script/script.h:209 |
| OP_DDVERIFY | 0xbc | DD verification | src/script/script.h:210 |
| OP_CHECKPRICE | 0xbd | Price checking | src/script/script.h:211 |
| OP_CHECKCOLLATERAL | 0xbe | Collateral validation | src/script/script.h:212 |
| OP_ORACLE | 0xbf | Oracle data marker | src/script/script.h:213 |

---

### 2.5 9-Tier Collateral Scale

| Lock Period | Collateral Ratio | Survives Price Drop | Example ($100 DD) |
|-------------|------------------|---------------------|-------------------|
| 1 hour* | 1000% | -90% | 1000 DGB @ $0.10 |
| 30 days | 500% | -80% | 500 DGB |
| 3 months | 400% | -75% | 400 DGB |
| 6 months | 350% | -71.4% | 350 DGB |
| 1 year | 300% | -66.7% | 300 DGB |
| 3 years | 250% | -60% | 250 DGB |
| 5 years | 225% | -55.6% | 225 DGB |
| 7 years | 212% | -52.8% | 212 DGB |
| 10 years | 200% | -50% | 200 DGB |

*Testing only (regtest/testnet)

#### Design Rationale

```
Economic Model: Similar to US Treasury Bonds
├── Shorter lock = Higher risk = More collateral
├── Longer lock = Lower risk = Less collateral
├── Attack resistance: 1000% prevents flash loan attacks
└── Incentive: Capital efficiency rewards long-term commitment
```

---

### 2.6 CCollateralPosition Structure

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
};
```

---

### 2.7 Time-Lock Mechanisms

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

#### Lock Period Calculations

```
30-day lock:  Current + (30 × 24 × 60 × 60) / 15 = +172,800 blocks
1-year lock:  Current + 2,102,400 blocks
10-year lock: Current + 21,024,000 blocks
```

---

### 2.8 MINT Transaction Flow

```
MINT TRANSACTION STRUCTURE
═══════════════════════════════════════════════════════════

INPUTS:
┌─────────────────────────────────────────────────────────┐
│ DGB UTXOs (for collateral + fees)                       │
│ Signed with: ECDSA (standard DGB inputs)                │
└─────────────────────────────────────────────────────────┘

OUTPUTS:
┌─────────────────────────────────────────────────────────┐
│ vout[0]: COLLATERAL VAULT                               │
│          P2TR + MAST + CLTV timelock                    │
│          Cannot be spent until unlock height            │
├─────────────────────────────────────────────────────────┤
│ vout[1]: DD TOKEN                                       │
│          Simple P2TR (key-path only, no MAST, no CLTV)  │
│          Freely transferable immediately                │
├─────────────────────────────────────────────────────────┤
│ vout[2]: OP_RETURN METADATA (21 bytes)                  │
│          ┌────────────────────────────────────────────┐ │
│          │ Byte 0:     0x6a (OP_RETURN)               │ │
│          │ Bytes 1-2:  0x44 0x44 (DD marker)          │ │
│          │ Byte 3:     TX type (0x01 for MINT)        │ │
│          │ Bytes 4-11: DD amount (uint64_t LE)        │ │
│          │ Bytes 12-19: Collateral (uint64_t LE)      │ │
│          │ Byte 20:    Lock tier (0-8)                │ │
│          └────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────┤
│ vout[3+]: DGB CHANGE (if any)                           │
└─────────────────────────────────────────────────────────┘

VERSION: 0x01000770 (MINT type)
```

---

### 2.9 TRANSFER Transaction Flow

```
TRANSFER TRANSACTION STRUCTURE
═══════════════════════════════════════════════════════════

INPUTS:
┌─────────────────────────────────────────────────────────┐
│ DD Token UTXOs                                          │
│ Signed with: Schnorr KEY-PATH (BIP-340)                 │
│ Witness: 64-byte signature only                         │
├─────────────────────────────────────────────────────────┤
│ DGB UTXOs (for fees only)                               │
│ Signed with: ECDSA (standard)                           │
└─────────────────────────────────────────────────────────┘

OUTPUTS:
┌─────────────────────────────────────────────────────────┐
│ vout[0]: DD TO RECIPIENT                                │
│          Simple P2TR (recipient's key)                  │
├─────────────────────────────────────────────────────────┤
│ vout[1]: DD CHANGE (if any)                             │
│          Simple P2TR (sender's key)                     │
├─────────────────────────────────────────────────────────┤
│ vout[2]: DGB FEE CHANGE (if any)                        │
└─────────────────────────────────────────────────────────┘

VERSION: 0x02000770 (TRANSFER type)

CONSERVATION RULE: Total DD In = Total DD Out
```

---

### 2.10 REDEEM Transaction Flow

#### Two Redemption Paths (Both Require Timelock Expired)

**Path 1: Normal Redemption (100% return)**
- Conditions: Timelock expired AND System Health ≥ 100%
- Result: Full collateral returned

**Path 2: ERR Redemption (80-95% return)**
- Conditions: Timelock expired AND System Health < 100%
- Tiered haircuts based on health:

| System Health | Return | Haircut |
|---------------|--------|---------|
| 95-100% | 95% | 5% |
| 90-95% | 90% | 10% |
| 85-90% | 85% | 15% |
| <85% | 80% | 20% |

**CRITICAL**: No early redemption exists. Both paths require timelock expiry.

---

### 2.11 Protection Systems

#### Layer 1: High Base Collateral (200-1000%)

Minimum collateral ratios prevent instant undercollateralization.

#### Layer 2: Dynamic Collateral Adjustment (DCA)

```cpp
double GetDCAMultiplier(int systemHealth) {
    if (systemHealth >= 150) return 1.0;    // Healthy
    if (systemHealth >= 120) return 1.2;    // Warning (+20%)
    if (systemHealth >= 100) return 1.5;    // Critical (+50%)
    return 2.0;                              // Emergency (+100%)
}
```

#### Layer 3: Emergency Redemption Ratio (ERR)

When system health drops below 100%, redemptions receive haircuts (80-95%).

#### Layer 4: Volatility Freeze

- 20% price change in 36 hours triggers freeze
- 144-block cooldown period (36 minutes)
- Prevents manipulation during extreme volatility

---

### 2.12 System Health Calculation

```
System Health = (Total Collateral × Oracle Price) / Total DD Supply × 100

Example:
  Total DGB locked: 10,000,000 DGB
  Oracle price: $0.01/DGB
  Total DD supply: $50,000

  Health = (10,000,000 × 0.01) / 50,000 × 100 = 200%
```

**Implementation**: Network-wide UTXO scanning via `ScanUTXOSet()` ensures every node calculates identical health values.

---

### 2.13 RPC Interface Reference

#### System Health Commands (5)
- `getdigidollarstats` - Overall system statistics
- `getdcamultiplier` - Current DCA multiplier
- `getprotectionstatus` - Protection system status
- `calculatecollateralrequirement` - Collateral calculator
- `getdigidollardeploymentinfo` - Deployment information

#### Wallet Commands (7)
- `mintdigidollar` - Create new DigiDollars
- `senddigidollar` - Transfer DigiDollars
- `redeemdigidollar` - Redeem for collateral
- `getdigidollarbalance` - DD balance
- `listdigidollarpositions` - List all vaults
- `getdigidollarunspent` - Unspent DD UTXOs
- `estimatemintfee` - Fee estimation

#### Oracle Commands (10)
- `getoracleprice` - Current oracle price
- `listoracles` - List authorized oracles
- `sendoracleprice` - Submit oracle price
- `setmockoracleprice` - Set mock price (regtest)
- `getmockoracleprice` - Get mock price
- `getoraclepubkey` - Oracle public key
- `startoracle` / `stoporacle` - Oracle control
- `simulatepricevolatility` - Volatility simulation
- `enablemockoracle` - Enable mock mode

---

### 2.14 Qt Wallet Implementation

**7 Functional Tabs**:

1. **Overview** - DD balance, system health, oracle price
2. **Receive** - Generate DD addresses (DD/TD/RD prefix)
3. **Send** - Transfer DigiDollars
4. **Mint** - Create new DigiDollars with collateral
5. **Redeem** - Burn DD and recover DGB
6. **Positions** - Vault manager with unlock dates
7. **Transactions** - DD transaction history

**Source Files**: `src/qt/digidollar*.cpp` (7 widget files)

---

## APPENDIX A: CODE REFERENCE TABLE

| Component | File Location | Lines |
|-----------|---------------|-------|
| Core Data Structures | src/digidollar/digidollar.h | 1-200 |
| Transaction Builder | src/digidollar/txbuilder.cpp | 1-500 |
| Wallet Integration | src/wallet/digidollarwallet.cpp | 1-800 |
| DCA System | src/consensus/dca.cpp | 1-200 |
| ERR System | src/consensus/err.cpp | 1-150 |
| Oracle Manager | src/oracle/oracle.cpp | 1-600 |
| RPC Commands | src/rpc/digidollar.cpp | 1-1200 |
| Qt Widgets | src/qt/digidollar*.cpp | 7 files |
| Unit Tests | src/test/digidollar_*.cpp | 26 files |
| Functional Tests | test/functional/digidollar_*.py | 18 files |

---

## APPENDIX B: GLOSSARY

| Term | Definition |
|------|------------|
| **DD** | DigiDollar - the stablecoin token |
| **DGB** | DigiByte - the underlying cryptocurrency |
| **UTXO** | Unspent Transaction Output |
| **P2TR** | Pay-to-Taproot (BIP-341) |
| **CLTV** | CheckLockTimeVerify (BIP-65) |
| **MAST** | Merklized Alternative Script Tree |
| **DCA** | Dynamic Collateral Adjustment |
| **ERR** | Emergency Redemption Ratio |
| **MAD** | Median Absolute Deviation |
| **Micro-USD** | 1,000,000 = $1.00 |

---

## APPENDIX C: TECHNICAL Q&A

**Q: Can DigiDollar fail like Terra/LUNA?**
A: No. Terra's failure was caused by algorithmic minting without real collateral. DigiDollar requires 200-1000% DGB collateral locked in time-locked vaults. The collateral cannot be accessed early, preventing death spiral selling.

**Q: What if DGB price drops 80%?**
A: The 9-tier collateral system provides buffers. A 10-year lock (200% collateral) survives a 50% drop. Higher-ratio shorter locks survive larger drops. ERR haircuts distribute losses fairly rather than causing cascading failures.

**Q: Can my DigiDollars be frozen?**
A: No. Unlike USDT/USDC, DigiDollar has no freeze mechanism. Your keys control your tokens. No entity can interfere with your funds.

**Q: What happens during oracle failure?**
A: Phase Two (8-of-15 consensus) requires 8 oracles to agree. Single oracle failure has no impact. Complete oracle network failure triggers volatility freeze, protecting the system while prices stabilize.

---

## CONCLUSION

DigiDollar represents a fundamental advancement in stablecoin technology:

**UTXO-Native**: No smart contracts, no custody risk
**4-Layer Protection**: DCA, ERR, Volatility Freeze, High Base Collateral
**Key Sovereignty**: Your keys always, time-locks not custodians
**12-Year Foundation**: Built on DigiByte's proven infrastructure

> "The first stablecoin where cryptographic time-locks replace custodial trust, and UTXO transparency replaces smart contract complexity."

---

**Implementation Status**: 85% Complete
**Test Coverage**: 427 Tests
**Source Code**: github.com/digibyte-core/digibyte
**Branch**: feature/digidollar-v1
**Coming in**: DigiByte v8.26
