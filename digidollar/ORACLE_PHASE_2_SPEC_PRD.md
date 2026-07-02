# Oracle Phase 2 Specification PRD

**Product Requirements Document for DigiDollar Multi-Oracle Consensus**

---

## Document Control

| Field | Value |
|-------|-------|
| Version | 1.1.0 |
| Created | 2026-01-02 |
| Status | **IMPLEMENTED** |
| Authors | DigiByte Core Team |
| Branch | feature/digidollar-v1 |

---

## 1. Executive Summary

Oracle Phase 2 introduces multi-oracle consensus for DigiDollar stablecoin price feeds. This upgrade transitions from a single-oracle model (Phase 1) to a Byzantine fault-tolerant multi-oracle system that requires agreement from a supermajority of independent oracle operators.

### Key Design Decisions

| Network | Consensus Model | Total Oracles | Active per Epoch | Threshold |
|---------|-----------------|---------------|------------------|-----------|
| **Mainnet** | 8-of-15 | 30 | 15 | 53.3% |
| **Testnet** | 3-of-10 | 10 | 10 | 30% |
| **Regtest** | 1-of-1 | 5 | 1 | 100% |

---

## 2. Architecture Overview

### 2.1 Oracle Network Topology

```
┌─────────────────────────────────────────────────────────────────┐
│                    ORACLE POOL (30 nodes)                       │
│  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ... ┌─────┐ ┌─────┐  │
│  │  0  │ │  1  │ │  2  │ │  3  │ │  4  │     │ 28  │ │ 29  │  │
│  └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘     └──┬──┘ └──┬──┘  │
│     │       │       │       │       │           │       │      │
│     └───────┴───────┴───────┴───────┴───────────┴───────┘      │
│                             │                                   │
│                   Deterministic Selection                       │
│                             ▼                                   │
│            ┌────────────────────────────────┐                  │
│            │     ACTIVE SET (15 nodes)      │                  │
│            │  Epoch N: {0,2,5,7,9,...}      │                  │
│            │  Epoch N+1: {1,3,4,6,8,...}    │                  │
│            └─────────────┬──────────────────┘                  │
│                          │                                      │
│               Price Messages (Schnorr signed)                   │
│                          ▼                                      │
│            ┌────────────────────────────────┐                  │
│            │     CONSENSUS ENGINE           │                  │
│            │  Require: 8+ valid signatures  │                  │
│            │  Filter: IQR outlier removal   │                  │
│            │  Calculate: Median price       │                  │
│            └─────────────┬──────────────────┘                  │
│                          │                                      │
│                   Consensus Price                               │
│                          ▼                                      │
│            ┌────────────────────────────────┐                  │
│            │     DIGIDOLLAR SYSTEM          │                  │
│            │  Mint/Burn at consensus price  │                  │
│            └────────────────────────────────┘                  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Phase Comparison

| Aspect | Phase 1 | Phase 2 |
|--------|---------|---------|
| Consensus Model | 1-of-1 | 8-of-15 (mainnet) |
| Fault Tolerance | None | Up to 7 failures |
| Byzantine Resistance | None | Up to 7 malicious |
| Oracle Selection | Static | Epoch rotation |
| Price Calculation | Single source | IQR-filtered median |
| Signature Verification | Optional | Mandatory Schnorr |

---

## 3. Implementation Status

### 3.1 What's Already Implemented

| Component | File | Lines | Status |
|-----------|------|-------|--------|
| `ValidateBundle()` | `src/oracle/bundle_manager.cpp` | 1117-1124 | COMPLETE |
| `ValidatePhaseOneBundle()` | `src/oracle/bundle_manager.cpp` | 1134-1164 | COMPLETE |
| `ValidatePhaseTwoBundle()` | `src/oracle/bundle_manager.cpp` | 1166-1239 | COMPLETE |
| `GetRequiredConsensus()` | `src/oracle/bundle_manager.cpp` | 1126-1132 | COMPLETE |
| `CalculateConsensusPrice()` | `src/oracle/bundle_manager.cpp` | 1241-1300 | COMPLETE |
| Oracle constants | `src/primitives/oracle.h` | 19-23 | COMPLETE |
| Consensus params | `src/consensus/params.h` | 186-192 | COMPLETE |
| Testnet oracle keys (10) | `src/kernel/chainparams.cpp` | 554-564 | COMPLETE |
| Testnet oracle nodes | `src/kernel/chainparams.cpp` | 581-596 | COMPLETE |

### 3.2 What's Missing

| Component | Priority | Status | Notes |
|-----------|----------|--------|-------|
| Mainnet oracle keys (30) | HIGH | NOT STARTED | Need secure key generation ceremony |
| Phase 2 unit tests | HIGH | **COMPLETE** | `src/test/oracle_phase2_tests.cpp` (21 tests) |
| Phase 2 functional tests | HIGH | **COMPLETE** | `test/functional/digidollar_oracle_phase2.py` (10 tests) |
| Phase 2 integration tests | HIGH | **COMPLETE** | `test/functional/digidollar_phase2_integration.py` (6 tests) |
| Testnet activation | HIGH | **COMPLETE** | Activates at block 650 (3-of-10 consensus) |
| Economic incentives | LOW | NOT STARTED | Staking/slashing system |
| Reputation system | LOW | NOT STARTED | Oracle quality scoring |
| Setup documentation | MEDIUM | PARTIAL | See Section 8 (Operator Guide) |

---

## 4. Consensus Parameters

### 4.1 Hardcoded Constants (`src/primitives/oracle.h`)

```cpp
static constexpr int ORACLE_CONSENSUS_REQUIRED = 8;   // 8-of-15 for mainnet
static constexpr int ORACLE_ACTIVE_COUNT = 15;        // 15 active per epoch
static constexpr int ORACLE_TOTAL_COUNT = 30;         // 30 total oracle pool
static constexpr int ORACLE_MAX_AGE_SECONDS = 3600;   // 1 hour max price age
static constexpr int ORACLE_OUTLIER_THRESHOLD_PCT = 10; // 10% outlier threshold
```

### 4.2 Chain Parameters (`src/consensus/params.h`)

```cpp
int nOracleActivationHeight{std::numeric_limits<int>::max()};  // Oracle system activation
int nOracleEpochLength{1440};                                   // Blocks per epoch (24h)
int nOracleRequiredMessages{1};                                 // Required signatures (Phase 1: 1)
int nOracleTotalOracles{1};                                     // Active oracles (Phase 1: 1)
std::vector<std::string> vOraclePublicKeys;                     // Oracle public keys
int nDigiDollarPhase2Height{std::numeric_limits<int>::max()};   // Phase 2 activation
```

### 4.3 Network-Specific Configuration

#### Mainnet (Future)
```cpp
consensus.nOracleActivationHeight = TBD;           // Phase 1 activation
consensus.nOracleEpochLength = 5760;               // 1 day (5760 blocks)
consensus.nOracleRequiredMessages = 8;             // 8-of-15 for Phase 2
consensus.nOracleTotalOracles = 15;                // 15 active per epoch
consensus.nDigiDollarPhase2Height = TBD;           // Phase 2 activation
consensus.vOraclePublicKeys = { /* 30 keys */ };   // Full oracle pool
```

#### Testnet (IMPLEMENTED: Phase 2 Active)
```cpp
// Phase 2 configuration (IMPLEMENTED)
consensus.nOracleActivationHeight = 1;             // Active from genesis
consensus.nOracleEpochLength = 1440;               // 24 hours
consensus.nOracleRequiredMessages = 3;             // 3-of-10 consensus
consensus.nOracleTotalOracles = 10;                // All 10 oracles active
consensus.nDigiDollarPhase2Height = 650;           // Phase 2 activates at block 650

// All 10 oracle public keys defined and ACTIVE
consensus.vOraclePublicKeys = {
    "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",  // oracle 0
    "d4735e3a265e16eee03f59718b9b5d03019c07d8b6c51f90da3a666eec13ab35",  // oracle 1
    "4e07408562bedb8b60ce05c1decfe3ad16b72230967de01f640b7e4729b49fce",  // oracle 2
    "4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a",  // oracle 3
    "ef2d127de37b942baad06145e54b0c619a1f22327b2ebbcfbec78f5564afe39d",  // oracle 4
    "e7f6c011776e8db7cd330b54174fd76f7d0216b612387a5ffcfb81e6f0919683",  // oracle 5
    "7902699be42c8a8e46fbbb4501726517e86b22c56a189f7625a6da49081b2451",  // oracle 6
    "2c624232cdd221771294dfbb310aca000a0df6ac8b66b696d90ef06fdefb64a3",  // oracle 7
    "19581e27de7ced00ff1ce50b2047e7a567c76b1cbaebabe5ef03f7c3017bb5b7",  // oracle 8
    "4a44dc15364204a80fe80e9039455cc1608281820fe2b24f1e5233ade6af1dd5",  // oracle 9
};
```

#### Regtest
```cpp
consensus.nOracleActivationHeight = 1;             // Immediate activation
consensus.nOracleEpochLength = 144;                // 2.4 hours
consensus.nOracleRequiredMessages = 1;             // 1-of-1 for testing
consensus.nOracleTotalOracles = 1;                 // Single oracle
consensus.nDigiDollarPhase2Height = INT_MAX;       // Phase 2 disabled

// Uses MockOracleManager for testing
```

---

## 5. Switchability Mechanism

### 5.1 How to Activate Phase 2 on Testnet

To transition testnet from Phase 1 (1-of-1) to Phase 2 (3-of-10):

**Step 1: Modify `src/kernel/chainparams.cpp` (CTestNetParams)**

```cpp
// Change from:
consensus.nOracleRequiredMessages = 1;        // Phase One: 1-of-1 consensus
consensus.nOracleTotalOracles = 1;            // Phase One: Single oracle active
consensus.nDigiDollarPhase2Height = std::numeric_limits<int>::max();

// To:
consensus.nOracleRequiredMessages = 3;        // Phase Two: 3-of-10 consensus
consensus.nOracleTotalOracles = 10;           // Phase Two: All 10 oracles active
consensus.nDigiDollarPhase2Height = 10000;    // Activate at block 10000
```

**Step 2: Enable Oracle Nodes**

In `InitializeOracleNodes()`, set oracle 1-9 to active:

```cpp
// Change is_active from false to true for oracles 1-9
{1,  ParsePubKey("02d4735e..."), "oracle2.digibyte.io:12031", true},  // was: false
{2,  ParsePubKey("034e0740..."), "oracle3.digibyte.io:12031", true},  // was: false
// ... etc
```

**Step 3: Rebuild and Deploy**

```bash
make clean && make -j$(nproc)
# Deploy to testnet nodes
```

### 5.2 Rollback Procedure

To revert from Phase 2 to Phase 1:

1. Set `nDigiDollarPhase2Height` to a future height (or INT_MAX)
2. The system automatically uses Phase 1 validation for blocks below the threshold

```cpp
// In ValidateBundle():
if (block_height >= params.nDigiDollarPhase2Height) {
    return ValidatePhaseTwoBundle(bundle, params);  // 8-of-15
} else {
    return ValidatePhaseOneBundle(bundle, params);  // 1-of-1
}
```

### 5.3 Configuration Matrix

| Setting | Regtest | Testnet (IMPLEMENTED) | Mainnet (Future) |
|---------|---------|----------------------|------------------|
| `nOracleActivationHeight` | 1 | 1 | TBD |
| `nOracleRequiredMessages` | 1 | **3** | 8 |
| `nOracleTotalOracles` | 1 | **10** | 15 |
| `nDigiDollarPhase2Height` | INT_MAX | **650** | TBD |
| `nOracleEpochLength` | 144 | 1440 | 5760 |
| Oracle keys defined | 1 | **10 (all active)** | 30 |

---

## 6. Validation Flow

### 6.1 Phase 2 Bundle Validation (`ValidatePhaseTwoBundle`)

```
┌─────────────────────────────────────────────────────────────┐
│                  ValidatePhaseTwoBundle()                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. CHECK MESSAGE COUNT                                     │
│     └─ bundle.messages.size() >= params.nOracleRequired     │
│        └─ FAIL if insufficient messages                     │
│                                                             │
│  2. CHECK FOR DUPLICATE ORACLE IDs                          │
│     └─ std::set<uint32_t> seen_oracles                      │
│        └─ FAIL if same oracle_id appears twice              │
│                                                             │
│  3. GET ACTIVE ORACLE SET                                   │
│     └─ GetActiveOraclesForEpoch(bundle.epoch)               │
│        └─ Deterministic selection from 30-node pool         │
│                                                             │
│  4. VALIDATE EACH MESSAGE                                   │
│     for each message:                                       │
│     ├─ Check oracle_id in active set                        │
│     ├─ Verify message.IsValid()                             │
│     ├─ Verify Schnorr signature                             │
│     └─ Increment valid_count if all pass                    │
│                                                             │
│  5. CHECK CONSENSUS THRESHOLD                               │
│     └─ valid_count >= params.nOracleRequired                │
│        └─ FAIL if not enough valid signatures               │
│                                                             │
│  6. VERIFY CONSENSUS PRICE                                  │
│     └─ CalculateConsensusPrice(bundle, params)              │
│        └─ FAIL if calculated != bundle.median_price         │
│                                                             │
│  7. RETURN TRUE (bundle valid)                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 Consensus Price Calculation (`CalculateConsensusPrice`)

```
┌─────────────────────────────────────────────────────────────┐
│                 CalculateConsensusPrice()                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. COLLECT VALID PRICES                                    │
│     └─ For each message with valid signature                │
│        └─ Add price_micro_usd to prices vector              │
│                                                             │
│  2. HANDLE EDGE CASES                                       │
│     └─ If empty: return 0                                   │
│     └─ If < 4 prices: return simple median                  │
│                                                             │
│  3. APPLY IQR OUTLIER FILTERING (1.5 * IQR rule)           │
│     ├─ Sort prices                                          │
│     ├─ Calculate Q1 (25th percentile)                       │
│     ├─ Calculate Q3 (75th percentile)                       │
│     ├─ IQR = Q3 - Q1                                        │
│     ├─ lower_bound = Q1 - 1.5 * IQR                         │
│     ├─ upper_bound = Q3 + 1.5 * IQR                         │
│     └─ Filter prices outside bounds                         │
│                                                             │
│  4. CALCULATE MEDIAN                                        │
│     └─ If filtered empty: use unfiltered median             │
│     └─ Otherwise: median of filtered prices                 │
│                                                             │
│  5. RETURN CONSENSUS PRICE (micro-USD)                      │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 7. Test Specifications

### 7.1 Unit Tests Required

| Test File | Test Name | Description | Status |
|-----------|-----------|-------------|--------|
| `oracle_phase2_tests.cpp` | `phase2_minimum_messages` | Verify fails with < 3 messages | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `phase2_duplicate_oracle_ids` | Verify fails with duplicate IDs | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `phase2_invalid_signatures` | Verify fails with bad signatures | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `phase2_exact_threshold` | Verify success with exact threshold | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `phase2_above_threshold` | Verify success with > threshold | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `phase2_missing_signatures` | Verify rejects unsigned messages | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `consensus_price_odd_count` | Median for odd count | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `consensus_price_even_count` | Median for even count | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `consensus_price_iqr_filtering` | IQR outlier filtering | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `consensus_price_empty_bundle` | Empty bundle returns 0 | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `consensus_price_single_message` | Single message handling | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `required_consensus_phase1` | Returns 1 below phase2 height | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `required_consensus_phase2_at_activation` | Returns params at height | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `required_consensus_phase2_above_activation` | Returns params above height | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `validate_bundle_routes_phase1` | Routes to Phase 1 validation | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `validate_bundle_routes_phase2` | Routes to Phase 2 validation | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `byzantine_tolerance_test` | 7 malicious + 8 honest | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `all_same_prices` | Identical prices handling | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `maximum_price_values` | Max price values | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `testnet_configuration` | 3-of-10 testnet config | IMPLEMENTED |
| `oracle_phase2_tests.cpp` | `mainnet_configuration` | 8-of-15 mainnet config | IMPLEMENTED |

### 7.2 Functional Tests (IMPLEMENTED)

| Test File | Test Name | Description | Status |
|-----------|-----------|-------------|--------|
| `digidollar_oracle_phase2.py` | `test_phase2_activation` | Verify Phase 2 activates at height | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_multi_oracle_consensus` | Multi-oracle consensus (3-of-10) | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_insufficient_signatures` | Verify rejection with < 3 sigs | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_oracle_epoch_rotation` | Deterministic oracle selection | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_outlier_filtering` | IQR outlier filtering | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_byzantine_oracle` | Byzantine fault tolerance | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_phase_transition` | Mint across phase boundary | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_price_staleness` | Stale price rejection | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_consensus_price_calculation` | Median calculation | IMPLEMENTED |
| `digidollar_oracle_phase2.py` | `test_duplicate_oracle_rejection` | Duplicate oracle ID rejection | IMPLEMENTED |

### 7.3 Integration Tests (IMPLEMENTED)

| Test File | Test Name | Description | Status |
|-----------|-----------|-------------|--------|
| `digidollar_phase2_integration.py` | `test_e2e_mint_phase2` | Full mint with multi-oracle | IMPLEMENTED |
| `digidollar_phase2_integration.py` | `test_e2e_burn_phase2` | Full burn with multi-oracle | IMPLEMENTED |
| `digidollar_phase2_integration.py` | `test_network_partition` | Consensus with network splits | IMPLEMENTED |
| `digidollar_phase2_integration.py` | `test_oracle_restart` | Recovery after restart | IMPLEMENTED |
| `digidollar_phase2_integration.py` | `test_price_update_propagation` | Price propagation across nodes | IMPLEMENTED |
| `digidollar_phase2_integration.py` | `test_consensus_recovery` | Recovery after failure | IMPLEMENTED |

---

## 8. Oracle Operator Guide

### 8.1 Requirements

| Requirement | Specification |
|-------------|---------------|
| Hardware | 4 CPU cores, 8GB RAM, 100GB SSD |
| Network | Static IP, 99.9% uptime |
| Software | DigiByte Core v8.26+, oracle daemon |
| Security | HSM recommended for key storage |
| Connectivity | Access to 7+ price feed APIs |

### 8.2 Key Generation

```bash
# Generate Schnorr keypair (BIP-340)
digibyte-cli createoraclekey

# Output:
# {
#   "oracle_id": 0,
#   "private_key": "...",  # KEEP SECRET - store in HSM
#   "public_key": "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
#   "address": "dgb1..."
# }
```

### 8.3 Registration Process (Future)

1. Generate keypair using HSM
2. Submit public key to DigiByte Foundation
3. Pass security audit
4. Stake required DGB collateral
5. Public key added to chainparams in next release
6. Begin signing price messages

### 8.4 Operating an Oracle Node

```bash
# Start oracle daemon
./digibyted -daemon -oraclemode=1 -oracleid=0 -oraclekey=<privkey>

# Configuration options
-oraclemode=1             # Enable oracle mode
-oracleid=<id>            # Your assigned oracle ID (0-29)
-oraclekey=<key>          # Private key (or path to HSM)
-oracleinterval=60        # Price update interval (seconds)
-oracleexchanges=all      # Price feed sources
```

### 8.5 Monitoring

```bash
# Check oracle status
digibyte-cli getoraclestatus

# Output:
# {
#   "oracle_id": 0,
#   "status": "active",
#   "last_price": 100000,  # $1.00 in micro-USD
#   "last_update": 1704153600,
#   "messages_sent": 1440,
#   "consensus_participation": "98.5%"
# }
```

---

## 9. Security Considerations

### 9.1 CRITICAL: Do NOT Do

| Constraint | Reason |
|------------|--------|
| Do NOT activate Phase 2 on mainnet without security audit | Multi-oracle code is untested at scale |
| Do NOT use test keys on mainnet | Keys in chainparams.cpp are for testing only |
| Do NOT skip Schnorr signature verification | Allows price manipulation |
| Do NOT accept bundles with < 8 signatures (mainnet) | Violates Byzantine tolerance |
| Do NOT allow empty `schnorr_sig` in Phase 2 | Phase 1 allowed optional sigs, Phase 2 requires |
| Do NOT commit private keys to repository | Use HSM or secure key management |

### 9.2 Attack Vectors and Mitigations

| Attack | Mitigation |
|--------|------------|
| Price manipulation by oracle | 8-of-15 consensus + IQR outlier filtering |
| Sybil attack (fake oracles) | Hardcoded public keys in chainparams |
| Replay attack | Epoch + block height + nonce in signature hash |
| Eclipse attack on oracle | Multiple exchange APIs + peer diversity |
| Byzantine fault | 8-of-15 tolerates up to 7 malicious oracles |

### 9.3 Audit Checklist (Before Mainnet)

- [ ] External security audit of `ValidatePhaseTwoBundle()`
- [ ] Formal verification of consensus algorithm
- [ ] Fuzzing of price message parsing
- [ ] Penetration testing of oracle daemon
- [ ] Key ceremony with multiple witnesses
- [ ] Testnet soak period (minimum 30 days)
- [ ] All functional tests passing
- [ ] Documentation review

---

## 10. Implementation Checklist

### 10.1 Phase 2 Testnet Activation

- [x] Define 10 testnet oracle public keys
- [x] Implement `ValidatePhaseTwoBundle()`
- [x] Implement `CalculateConsensusPrice()` with IQR
- [x] Implement `GetRequiredConsensus()`
- [x] Implement `ValidateBundle()` routing
- [x] Write unit tests for Phase 2 validation
- [x] Write functional tests for multi-oracle
- [x] Set `nDigiDollarPhase2Height = 650`
- [x] Set `nOracleRequiredMessages = 3`
- [x] Set `nOracleTotalOracles = 10`
- [x] Enable oracle nodes 1-9 in `InitializeOracleNodes()`
- [ ] Deploy 10 oracle daemons
- [ ] Monitor for 7 days before mainnet consideration

### 10.2 Phase 2 Mainnet Activation (Future)

- [ ] Complete testnet validation
- [ ] External security audit
- [ ] Generate 30 mainnet oracle keypairs (key ceremony)
- [ ] Add 30 public keys to mainnet chainparams
- [ ] Set mainnet activation height
- [ ] Deploy 30 oracle nodes across geographic regions
- [ ] Community announcement and documentation
- [ ] Phased activation with monitoring

---

## 11. Appendix

### A. Oracle Public Keys (Testnet)

```cpp
// From src/kernel/chainparams.cpp lines 554-564
oracle 0: "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"  // ACTIVE
oracle 1: "d4735e3a265e16eee03f59718b9b5d03019c07d8b6c51f90da3a666eec13ab35"  // reserved
oracle 2: "4e07408562bedb8b60ce05c1decfe3ad16b72230967de01f640b7e4729b49fce"  // reserved
oracle 3: "4b227777d4dd1fc61c6f884f48641d02b4d121d3fd328cb08b5531fcacdabf8a"  // reserved
oracle 4: "ef2d127de37b942baad06145e54b0c619a1f22327b2ebbcfbec78f5564afe39d"  // reserved
oracle 5: "e7f6c011776e8db7cd330b54174fd76f7d0216b612387a5ffcfb81e6f0919683"  // reserved
oracle 6: "7902699be42c8a8e46fbbb4501726517e86b22c56a189f7625a6da49081b2451"  // reserved
oracle 7: "2c624232cdd221771294dfbb310aca000a0df6ac8b66b696d90ef06fdefb64a3"  // reserved
oracle 8: "19581e27de7ced00ff1ce50b2047e7a567c76b1cbaebabe5ef03f7c3017bb5b7"  // reserved
oracle 9: "4a44dc15364204a80fe80e9039455cc1608281820fe2b24f1e5233ade6af1dd5"  // reserved
```

### B. Key Code References

| Component | File | Function/Lines |
|-----------|------|----------------|
| Bundle validation | `src/oracle/bundle_manager.cpp` | `ValidateBundle()` L1117-1124 |
| Phase 1 validation | `src/oracle/bundle_manager.cpp` | `ValidatePhaseOneBundle()` L1134-1164 |
| Phase 2 validation | `src/oracle/bundle_manager.cpp` | `ValidatePhaseTwoBundle()` L1166-1239 |
| Consensus calculation | `src/oracle/bundle_manager.cpp` | `CalculateConsensusPrice()` L1241-1300 |
| Consensus params | `src/consensus/params.h` | L186-192 |
| Oracle constants | `src/primitives/oracle.h` | L19-23 |
| Testnet config | `src/kernel/chainparams.cpp` | `CTestNetParams` L400-598 |
| Regtest config | `src/kernel/chainparams.cpp` | `CRegTestParams` L767-996 |

### C. Related Documentation

- `digidollar/DIGIDOLLAR_ORACLE_PHASE_ONE_SPEC.md` - Phase 1 specification
- `DIGIDOLLAR_ORACLE_ARCHITECTURE.md` - Complete oracle technical docs
- `DIGIDOLLAR_ORACLE_EXPLAINER.md` - Oracle system overview
- `DIGIDOLLAR_ORACLE_SETUP_COMPLETE_GUIDE.md` - Setup instructions

---

## 12. Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0.0 | 2026-01-02 | DigiByte Core | Initial draft |
| 1.1.0 | 2026-01-02 | DigiByte Core | Phase 2 implemented on testnet (3-of-10 at block 650) |

---

**END OF DOCUMENT**
