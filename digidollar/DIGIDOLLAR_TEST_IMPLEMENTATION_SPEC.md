# DigiDollar Test Implementation Specification

**Version**: 1.0
**Date**: 2025-12-22
**Status**: SPEC ONLY - DO NOT IMPLEMENT WITHOUT APPROVAL

---

## Executive Summary

This specification outlines the complete task list for finishing DigiDollar test implementation. All items are based on deep code verification across 5 analysis domains.

### Verified Test Counts

| Category | Current Tests | Gaps Identified | Target |
|----------|---------------|-----------------|--------|
| DigiDollar Unit Tests | 752 | 150+ functions untested | ~900 |
| Oracle Unit Tests | 123 | 135+ scenarios missing | ~260 |
| Protection Systems | 107 | 46 RED phase stubs | ~160 |
| Qt GUI Tests | 11 | 500+ UI paths untested | ~100 |
| Functional Tests | 113 | 60+ scenarios missing | ~175 |
| **TOTAL** | **1,106** | **~900 gaps** | **~1,600** |

---

## PHASE 0: CRITICAL CLEANUP - DELETE INCORRECT TESTS

**MANDATORY FIRST STEP**: Remove all tests related to features that DO NOT EXIST.

### DigiDollar Redemption Rules (TRUTH):
1. **Normal Redemption** - FULL position only, after timelock expires
2. **ERR Redemption** - FULL position only, requires MORE DD burned, returns FULL collateral

### Features That DO NOT EXIST (DELETE ALL REFERENCES):
- ~~Partial redemption~~ - DOES NOT EXIST
- ~~Emergency oracle override~~ - DOES NOT EXIST
- ~~Oracle approval for early redemption~~ - DOES NOT EXIST
- ~~OverrideFreeze with oracle signatures~~ - DOES NOT EXIST

### Tests to DELETE:

#### File: src/test/digidollar_validation_tests.cpp
| Line | Test Name | Action |
|------|-----------|--------|
| 262 | path_validation_partial_redemption | DELETE |
| 1493 | test_validate_partial_redemption_rules | DELETE |
| All | CreatePartialRedemptionPath calls | DELETE |
| All | ValidatePartialRedemption calls | DELETE |

#### File: src/test/digidollar_volatility_tests.cpp
| Line | Test Name | Action |
|------|-----------|--------|
| 388 | OverrideFreeze test (insufficient) | DELETE |
| 425 | OverrideFreeze test (sufficient) | DELETE |
| 458 | OverrideFreeze test (invalid) | DELETE |
| 909 | maliciousOverrides test | DELETE |
| 932 | overrideMessages loop | DELETE |
| 968 | conflictingMessages test | DELETE |
| 1002 | futureOverride test | DELETE |
| 2251 | OverrideFreeze approval test | DELETE |

#### File: src/test/digidollar_transaction_tests.cpp
| Line | Test Name | Action |
|------|-----------|--------|
| 734-736 | Partial redemption test cases | DELETE |

#### File: src/test/digidollar_scripts_tests.cpp
| Line | Test Name | Action |
|------|-----------|--------|
| 110 | test_partial_redemption_path_creation | DELETE |
| 250 | CreatePartialRedemptionPath call | DELETE |
| 308 | CreatePartialRedemptionPath call | DELETE |

#### File: src/test/digidollar_timelock_tests.cpp
| Line | Test Name | Action |
|------|-----------|--------|
| 1916 | Path C: Partial redemption comment | DELETE |
| 2607-2618 | Oracle emergency override test | DELETE |
| 2623-2702 | partial_redemption_timelock test | DELETE ENTIRE TEST |

#### File: src/test/digidollar_wallet_tests.cpp
| Line | Test Name | Action |
|------|-----------|--------|
| 1469 | partial redemption scenario setup | DELETE |
| 2899-2920 | test_track_partial_redemption | DELETE ENTIRE TEST |
| 2929-2960 | TrackPartialRedemption full test | DELETE ENTIRE TEST |
| 2962-2980 | test_track_partial_redemption_exceeds_minted | DELETE ENTIRE TEST |
| 3119-3120 | TrackPartialRedemption call | DELETE |
| 3531 | Partial redemption comment/call | DELETE |

#### File: src/test/digidollar_redeem_tests.cpp
| Line | Test Name | Action |
|------|-----------|--------|
| 162 | test_partial_redemption_keep_position_open | DELETE ENTIRE TEST |
| 293 | partial redemption comment | DELETE |
| 455 | PARTIAL redemption script creation | DELETE |

### Implementation Files to Clean:

#### File: src/digidollar/validation.cpp
- DELETE: ValidatePartialRedemptionConditions function
- DELETE: ValidateEmergencyRedemptionConditions function
- DELETE: Any CreatePartialRedemptionPath calls

#### File: src/digidollar/scripts.cpp
- DELETE: CreatePartialRedemptionPath function

#### File: src/wallet/digidollarwallet.cpp
- DELETE: TrackPartialRedemption function

#### File: src/consensus/volatility.cpp
- DELETE: OverrideFreeze function

---

## PHASE 1: CRITICAL BLOCKERS (Must Fix Before Release)

### 1.1 RED Phase Tests Requiring GREEN Implementation

These tests exist but EXPECT FAILURE. Implementation must be completed.

#### DCA System (12 RED phase functions)

| Priority | Task ID | Function | File Location |
|----------|---------|----------|---------------|
| P0 | DCA-001 | Implement HandleRapidTransition | dca.cpp:297-303 |
| P0 | DCA-002 | Implement ValidateExtremeValues | dca.cpp:305-311 |
| P0 | DCA-003 | Implement ValidateMultiplierPrecision | dca.cpp:313-319 |
| P0 | DCA-004 | Implement PreventIntegerOverflow | dca.cpp:321-327 |
| P1 | DCA-005 | Implement HandleConcurrentUpdates | dca.cpp:329-335 |
| P1 | DCA-006 | Implement VerifyMemoryStability | dca.cpp:337-343 |
| P1 | DCA-007 | Implement ValidateErrorHandling | dca.cpp:345-351 |
| P1 | DCA-008 | Implement IsStateTransitionTracked | dca.cpp:353-359 |
| P2 | DCA-009 | Implement HasHysteresis | dca.cpp:361-367 |
| P2 | DCA-010 | Implement TrackSystemRecovery | dca.cpp:369-375 |
| P2 | DCA-011 | Implement ValidateConcurrentCalculations | dca.cpp:377-383 |
| P2 | DCA-012 | Implement SimulateResourceExhaustion | dca.cpp:385-391 |

#### Volatility System (18 TODO functions)

| Priority | Task ID | Function | File Location |
|----------|---------|----------|---------------|
| P0 | VOL-001 | Fix SerializeHash calls (3 locations) | volatility_tests.cpp:73,896,959 |
| P0 | VOL-002 | Fix signature generation | volatility_tests.cpp:992 |
| P0 | VOL-003 | Implement ValidateOscillationDamping | volatility_tests.cpp:643-646 |
| P0 | VOL-004 | Implement ValidateCorruptedDataHandling | volatility_tests.cpp:669-671 |
| P1 | VOL-005 | Implement ValidateMemoryStability | volatility_tests.cpp:697-699 |
| P1 | VOL-006 | Implement ValidateThreadSafety | volatility_tests.cpp:719-721 |
| P1 | VOL-007 | Implement ValidatePrecisionThresholds | volatility_tests.cpp:755-757 |
| P1 | VOL-008 | Implement ValidateStatePersistence | volatility_tests.cpp:771-773 |
| P1 | VOL-009 | Implement ValidateRapidBlockProgression | volatility_tests.cpp:802-804 |
| P1 | VOL-010 | Implement ValidateReorganizationHandling | volatility_tests.cpp:822-824 |
| P2 | VOL-011 | Implement ValidateNestedCooldowns | volatility_tests.cpp:841-843 |
| P2 | VOL-012 | Implement ValidateTimeAnomalyHandling | volatility_tests.cpp:863-865 |
| P2 | VOL-013 | Implement ValidateMaliciousSignatureDetection | volatility_tests.cpp:913-915 |
| P2 | VOL-014 | Implement ValidateFloodProtection | volatility_tests.cpp:937-939 |
| P2 | VOL-015 | Implement ValidateConflictResolution | volatility_tests.cpp:971-973 |
| P2 | VOL-016 | Implement ValidateDCAIntegration | volatility_tests.cpp:1034-1036 |
| P2 | VOL-017 | Implement ValidateProtectionCoordination | volatility_tests.cpp:1048-1050 |
| P2 | VOL-018 | Implement ValidateResourceManagement | volatility_tests.cpp:1080-1082 |

#### Redemption Tests (RED phase tests)

**IMPORTANT**: DigiDollar only supports TWO redemption paths:
1. **Normal Redemption** - Full position redemption after timelock expiry
2. **ERR Redemption** - Full position redemption requiring MORE DD burned (when system health < 100%)

**NO partial redemptions. NO emergency oracle override.**

| Priority | Task ID | Test Category | File Location |
|----------|---------|---------------|---------------|
| P0 | RDM-001 | Normal FULL redemption after timelock expiry | digidollar_redeem_tests.cpp:107-121 |
| P0 | RDM-002 | Normal redemption before timelock MUST FAIL | digidollar_redeem_tests.cpp:127-141 |
| P0 | RDM-003 | ERR redemption with increased DD burn | digidollar_redeem_tests.cpp |
| P0 | RDM-004 | ERR redemption returns FULL collateral | digidollar_redeem_tests.cpp |
| P0 | RDM-005 | Verify NO partial redemption allowed | DELETE existing partial tests |
| P0 | RDM-006 | Verify NO emergency override exists | DELETE existing override tests |

---

### 1.2 Known Application Bugs Requiring Test Coverage

These are bugs identified in code comments or skipped tests.

| Priority | Task ID | Bug Description | Location |
|----------|---------|-----------------|----------|
| P0 | BUG-001 | listdigidollartxs missing required fields | digidollar_basic.py:211-215 |
| P0 | BUG-002 | validateddaddress has validation bugs | digidollar_wallet.py:462-468 |
| P0 | BUG-003 | senddigidollar/redeemdigidollar TX building issues | digidollar_rpc.py:201-204 |
| P0 | BUG-004 | RPC parameter validation incomplete | digidollar_rpc.py:49-54 |
| P1 | BUG-005 | DD change bug in redemption (fixed 48cd411944) | Verify all variants covered |

---

### 1.3 Skipped Functional Tests Requiring Fix

| Priority | Task ID | Test | File | Reason Skipped |
|----------|---------|------|------|----------------|
| P0 | SKIP-001 | test_utility_commands | digidollar_rpc.py | RPC implementation issues |
| P0 | SKIP-002 | test_parameter_validation | digidollar_rpc.py | RPC implementation issues |
| P0 | SKIP-003 | test_error_handling | digidollar_rpc.py | RPC implementation issues |
| P0 | SKIP-004 | test_response_formats | digidollar_rpc.py | RPC implementation issues |
| P0 | SKIP-005 | test_command_integration | digidollar_rpc.py | RPC implementation issues |
| P1 | SKIP-006 | test_concurrent_transfers | digidollar_transfer.py | RPC not thread-safe |

---

## PHASE 2: ORACLE SYSTEM GAPS

### 2.1 Phase Two Consensus (8-of-15) - COMPLETELY UNTESTED

| Priority | Task ID | Test Requirement |
|----------|---------|------------------|
| P0 | ORC-001 | Implement ValidatePhaseTwoBundle tests |
| P0 | ORC-002 | Test 8-of-15 signature threshold validation |
| P0 | ORC-003 | Test median price calculation with 8+ oracles |
| P0 | ORC-004 | Test IQR (interquartile range) outlier filtering |
| P0 | ORC-005 | Test bundle message count validation (max 15) |
| P1 | ORC-006 | Test Phase One to Phase Two transition |
| P1 | ORC-007 | Test CalculateConsensusPrice with Phase Two settings |

### 2.2 Exchange API Gaps (5 of 7 untested)

| Priority | Task ID | Exchange | Missing Tests |
|----------|---------|----------|---------------|
| P1 | EXC-001 | CoinMarketCap | Fetcher, JSON parser, error handling |
| P1 | EXC-002 | CoinGecko | Fetcher, JSON parser, error handling |
| P1 | EXC-003 | Coinbase | Fetcher, JSON parser, error handling |
| P1 | EXC-004 | Kraken | Fetcher, JSON parser, error handling |
| P1 | EXC-005 | Messari | Fetcher, JSON parser, error handling |

### 2.3 Oracle Security (0% coverage)

| Priority | Task ID | Attack Vector | Test Requirement |
|----------|---------|---------------|------------------|
| P0 | SEC-001 | Price manipulation | Test extreme price outliers (10x) |
| P0 | SEC-002 | Coordinated attack | Test N-of-15 colluding oracles |
| P0 | SEC-003 | Stale price injection | Test prices > 1 hour old |
| P0 | SEC-004 | Future timestamp | Test timestamp > current time |
| P1 | SEC-005 | Signature reuse | Test cross-epoch replay attacks |
| P1 | SEC-006 | Invalid signature recovery | Test Schnorr tampering |
| P2 | SEC-007 | Bundle overflow | Test > 15 messages in bundle |

### 2.4 Oracle Key Rotation (NOT IMPLEMENTED)

| Priority | Task ID | Feature | Requirement |
|----------|---------|---------|-------------|
| P2 | KEY-001 | Define rotation schedule | Design spec needed |
| P2 | KEY-002 | New key activation period | Design spec needed |
| P2 | KEY-003 | Old key deprecation logic | Design spec needed |
| P2 | KEY-004 | Key version tracking | Design spec needed |

---

## PHASE 3: VALIDATION FUNCTION GAPS

### 3.1 Untested Validation Functions

**NOTE**: Only TWO redemption paths exist - Normal (after timelock) and ERR (more DD burned).
No partial redemption. No emergency override.

| Priority | Task ID | Function | File |
|----------|---------|----------|------|
| P0 | VAL-001 | ValidateNormalRedemptionConditions (FULL only) | validation.cpp |
| P0 | VAL-002 | ValidateERRRedemptionConditions | validation.cpp |
| P0 | VAL-003 | ValidateScriptPathSpending | validation.cpp |
| P0 | VAL-004 | ValidateERRAdjustmentAmount (increased DD burn) | validation.cpp |
| P0 | VAL-005 | CalculateExpectedERRAdjustment | validation.cpp |
| P0 | VAL-006 | ValidateCollateralReleaseAmount (TODO at line 1690) | validation.cpp |
| P0 | VAL-007 | DELETE ValidatePartialRedemptionConditions | REMOVE FROM CODE |
| P0 | VAL-008 | DELETE ValidateEmergencyRedemptionConditions | REMOVE FROM CODE |

### 3.2 Untested Wallet Functions

| Priority | Task ID | Function | File |
|----------|---------|----------|------|
| P0 | WAL-001 | RedeemDigiDollar | digidollarwallet.cpp |
| P0 | WAL-002 | TransferDigiDollar broadcast mechanics | digidollarwallet.cpp |
| P1 | WAL-003 | ProcessIncomingDDTransaction | digidollarwallet.cpp |
| P1 | WAL-004 | UpdateDDBalance edge cases | digidollarwallet.cpp |
| P1 | WAL-005 | ValidatePositionLock | digidollarwallet.cpp |
| P1 | WAL-006 | StoreOwnerKey | digidollarwallet.cpp |
| P1 | WAL-007 | RetrieveOwnerKey error paths | digidollarwallet.cpp |
| P2 | WAL-008 | RestoreWalletState with corruption | digidollarwallet.cpp |
| P2 | WAL-009 | VerifyDDUTXOConsistency | digidollarwallet.cpp |
| P2 | WAL-010 | RecoverFromDatabaseError | digidollarwallet.cpp |

---

## PHASE 4: EDGE CASE COVERAGE

### 4.1 Overflow/Underflow Scenarios

| Priority | Task ID | Scenario | Component |
|----------|---------|----------|-----------|
| P0 | OVF-001 | DD amount overflow at MAX_MONEY | Minting |
| P0 | OVF-002 | Collateral underflow at health = 0 | DCA |
| P0 | OVF-003 | Fee calculation overflow | Transaction building |
| P1 | OVF-004 | DCA multiplier floating point precision | DCA calculations |
| P1 | OVF-005 | ERR ratio calculation boundaries | ERR redemption |
| P1 | OVF-006 | Cumulative rounding error detection | All financial calcs |

### 4.2 Boundary Conditions

| Priority | Task ID | Boundary | Component |
|----------|---------|----------|-----------|
| P0 | BND-001 | Lock tier 0 at block 239 vs 240 | Timelock |
| P0 | BND-002 | Redemption at exact unlock height | Timelock |
| P0 | BND-003 | Minimum DD amount (mainnet $100) | Minting |
| P0 | BND-004 | Maximum DD amount (no upper bound?) | Minting |
| P0 | BND-005 | Dust threshold edge cases | Transfers |
| P1 | BND-006 | Collateral ratio boundaries (200%-1000%) | Lock tiers |
| P1 | BND-007 | DCA tier transitions at 100%, 120%, 150% | DCA |
| P1 | BND-008 | ERR tier transitions at 85%, 90%, 95% | ERR |

### 4.3 Error Path Coverage

| Priority | Task ID | Error Scenario | Component |
|----------|---------|----------------|-----------|
| P0 | ERR-001 | UTXO selection with no suitable inputs | TX building |
| P0 | ERR-002 | Fee estimation failure | TX building |
| P0 | ERR-003 | Change output below dust | TX building |
| P1 | ERR-004 | Key derivation error | Wallet |
| P1 | ERR-005 | Signature generation failure | TX signing |
| P1 | ERR-006 | Public key validation failure | Address validation |
| P1 | ERR-007 | Transaction version mismatch | Consensus |
| P1 | ERR-008 | OP_RETURN marker missing | Validation |
| P2 | ERR-009 | Wallet database locked during write | Persistence |
| P2 | ERR-010 | Disk full during append | Persistence |
| P2 | ERR-011 | Mempool acceptance rejection | Network |
| P2 | ERR-012 | Peer broadcast timeout | Network |

### 4.4 Concurrent Access Patterns

| Priority | Task ID | Pattern | Component |
|----------|---------|---------|-----------|
| P0 | CON-001 | Simultaneous transfer + redemption | Wallet |
| P0 | CON-002 | DCA multiplier change mid-transaction | DCA + TX |
| P1 | CON-003 | Wallet lock/unlock during DD operation | Wallet |
| P1 | CON-004 | UTXO spent between selection and assembly | TX building |
| P1 | CON-005 | Multiple parallel mints with same key | Minting |
| P2 | CON-006 | Rapid system health changes | DCA |

---

## PHASE 5: FUNCTIONAL TEST GAPS

### 5.1 Untested RPC Commands (9 of 27)

| Priority | Task ID | Command | Required Tests |
|----------|---------|---------|----------------|
| P0 | RPC-001 | importdigidollaraddress | Import validation, key recovery, duplicate handling |
| P0 | RPC-002 | estimatecollateral | All lock tiers, DCA impact |
| P1 | RPC-003 | enablemockoracle | Mock oracle activation/deactivation |
| P1 | RPC-004 | getoraclepubkey | Oracle pubkey format validation |
| P1 | RPC-005 | listoracles | Active oracle enumeration |
| P1 | RPC-006 | sendoracleprice | Price submission, signature verification |
| P1 | RPC-007 | startoracle | Oracle lifecycle start |
| P1 | RPC-008 | stoporacle | Oracle lifecycle stop |
| P2 | RPC-009 | simulatepricevolatility | Volatility simulation accuracy |

### 5.2 Missing End-to-End Scenarios

| Priority | Task ID | Scenario | Description |
|----------|---------|----------|-------------|
| P0 | E2E-001 | Complete wallet lifecycle | Mint -> Transfer -> Redeem across 2 wallets |
| P0 | E2E-002 | Multi-tier position management | 3 positions with different tiers, sequential redemption |
| P0 | E2E-003 | DCA cascade protection | Health drop triggering all DCA tiers |
| P0 | E2E-004 | Network reorg with positions | Fork resolution with active DD positions |
| P1 | E2E-005 | Cross-chain position migration | Wallet backup -> restore on different node |
| P1 | E2E-006 | ERR activation under load | 100+ simultaneous ERR redemptions |
| P2 | E2E-007 | All three protection systems | Volatility + DCA + ERR simultaneously |

### 5.3 Multi-Node Scenarios

| Priority | Task ID | Scenario | Description |
|----------|---------|----------|-------------|
| P0 | NET-001 | Partition during mint | Mint on isolated node, then rejoin |
| P0 | NET-002 | Partition during redemption | Redemption conflict resolution |
| P1 | NET-003 | Chain reorganization | Reorg after mint, verify position integrity |
| P1 | NET-004 | Multi-partition recovery | 2-2 split then all reconnect |
| P1 | NET-005 | Long network disconnect | >100 blocks divergence |
| P2 | NET-006 | Node restart with pending redemptions | Recovery verification |

### 5.4 Stress Test Scenarios

| Priority | Task ID | Scenario | Target |
|----------|---------|----------|--------|
| P1 | STR-001 | 1000+ concurrent mint operations | TX throughput |
| P1 | STR-002 | 10000+ mints in single wallet | Wallet performance |
| P1 | STR-003 | Rapid create/destroy positions | Memory leak detection |
| P1 | STR-004 | Network congestion with fee estimation | Fee accuracy |
| P2 | STR-005 | 1000+ simultaneous redemptions | Queue handling |
| P2 | STR-006 | Random node crashes during equilibrium | Fault tolerance |

---

## PHASE 6: QT GUI TEST EXPANSION

### 6.1 Per-Widget Test Requirements

#### DigiDollarOverviewWidget (942 lines, 1 test)

| Priority | Task ID | Functionality | Test Count |
|----------|---------|---------------|------------|
| P1 | GUI-OV-001 | updateBalance RPC call and display | 3 |
| P1 | GUI-OV-002 | updateOraclePrice with mock/RPC fallback | 3 |
| P1 | GUI-OV-003 | updateSystemHealth all 4 tiers | 4 |
| P1 | GUI-OV-004 | updateRecentTransactions (20 items) | 3 |
| P2 | GUI-OV-005 | Timer-driven refresh mechanism | 2 |
| P2 | GUI-OV-006 | Signal/slot connections | 2 |

#### DigiDollarSendWidget (1246 lines, 1 test)

| Priority | Task ID | Functionality | Test Count |
|----------|---------|---------------|------------|
| P0 | GUI-SN-001 | Address validation real-time feedback | 4 |
| P0 | GUI-SN-002 | Amount validation with USD display | 4 |
| P0 | GUI-SN-003 | Send button validation and execution | 5 |
| P0 | GUI-SN-004 | 3-second confirmation dialog countdown | 3 |
| P1 | GUI-SN-005 | Error mapping for 6 error types | 6 |
| P1 | GUI-SN-006 | Coin control dialog integration | 3 |
| P2 | GUI-SN-007 | Clear, paste, use available buttons | 3 |

#### DigiDollarMintWidget (942 lines, 1 test)

| Priority | Task ID | Functionality | Test Count |
|----------|---------|---------------|------------|
| P0 | GUI-MT-001 | Lock tier selection with double-warning | 4 |
| P0 | GUI-MT-002 | Collateral calculation with oracle price | 4 |
| P0 | GUI-MT-003 | Mint button with final confirmation | 5 |
| P1 | GUI-MT-004 | All 9 lock tier block calculations | 9 |
| P1 | GUI-MT-005 | Amount warning labels (min/max/available) | 4 |
| P2 | GUI-MT-006 | Oracle price update mechanism | 2 |

#### DigiDollarRedeemWidget (784 lines, 1 test)

| Priority | Task ID | Functionality | Test Count |
|----------|---------|---------------|------------|
| P0 | GUI-RD-001 | Position loading from RPC | 3 |
| P0 | GUI-RD-002 | Exact amount enforcement | 4 |
| P0 | GUI-RD-003 | Redemption process execution | 5 |
| P1 | GUI-RD-004 | Position info display formatting | 4 |
| P1 | GUI-RD-005 | Block time formatting | 3 |
| P2 | GUI-RD-006 | Validation label styling | 2 |

#### DigiDollarReceiveWidget (935 lines, 0 tests)

| Priority | Task ID | Functionality | Test Count |
|----------|---------|---------------|------------|
| P0 | GUI-RC-001 | Address generation via RPC | 3 |
| P0 | GUI-RC-002 | QR code generation with payment URI | 3 |
| P1 | GUI-RC-003 | Copy address/QR functionality | 4 |
| P1 | GUI-RC-004 | Recent requests table population | 4 |
| P2 | GUI-RC-005 | Context menu actions (5 actions) | 5 |
| P2 | GUI-RC-006 | Request removal and persistence | 2 |

#### DigiDollarPositionsWidget (961 lines, 1 test)

| Priority | Task ID | Functionality | Test Count |
|----------|---------|---------------|------------|
| P0 | GUI-PO-001 | Position loading and health calculation | 4 |
| P0 | GUI-PO-002 | Table population with 7 columns | 4 |
| P0 | GUI-PO-003 | Redeem button conditional styling | 4 |
| P1 | GUI-PO-004 | Health widget gradient coloring | 4 |
| P1 | GUI-PO-005 | Row color coding for redeemed | 3 |
| P2 | GUI-PO-006 | Context menu with 3 actions | 3 |
| P2 | GUI-PO-007 | Auto-refresh timer mechanism | 2 |

#### DigiDollarTransactionsWidget (458 lines, 0 tests)

| Priority | Task ID | Functionality | Test Count |
|----------|---------|---------------|------------|
| P0 | GUI-TX-001 | Table population via RPC | 3 |
| P0 | GUI-TX-002 | 6 column formatting | 6 |
| P1 | GUI-TX-003 | Type filter and search functionality | 4 |
| P1 | GUI-TX-004 | Amount formatting and coloring | 3 |
| P2 | GUI-TX-005 | Context menu with 3 actions | 3 |
| P2 | GUI-TX-006 | Confirmation status display | 4 |

---

## PHASE 7: PROTECTION SYSTEM INTEGRATION

### 7.1 Cross-System Integration Tests

| Priority | Task ID | Systems | Scenario |
|----------|---------|---------|----------|
| P0 | INT-001 | DCA + ERR | Both activating simultaneously |
| P0 | INT-002 | Volatility + DCA | Freeze + critical tier interaction |
| P0 | INT-003 | All three | Volatility + DCA + ERR cascade |
| P1 | INT-004 | DCA + Oracle | DCA calculation with oracle failure |
| P1 | INT-005 | ERR + Oracle | ERR activation with oracle consensus failure |
| P2 | INT-006 | Volatility + Oracle | Override freeze with oracle signatures |

### 7.2 State Persistence Tests

| Priority | Task ID | System | Scenario |
|----------|---------|--------|----------|
| P0 | PER-001 | DCA | System health cache recovery after restart |
| P0 | PER-002 | ERR | Queue persistence across crash |
| P0 | PER-003 | Volatility | Price history recovery |
| P1 | PER-004 | All | State recovery after block reorg |
| P1 | PER-005 | ERR | Activation height becomes invalid after reorg |

---

## IMPLEMENTATION ORDER

### Sprint 1: Critical Blockers (P0 items)
1. Fix all RED phase redemption tests (RDM-001 through RDM-006)
2. Fix known application bugs (BUG-001 through BUG-004)
3. Un-skip critical functional tests (SKIP-001 through SKIP-005)
4. Implement P0 validation functions (VAL-001 through VAL-008)
5. Add overflow/underflow tests (OVF-001 through OVF-003)

### Sprint 2: Oracle & Security
1. Implement Phase Two consensus tests (ORC-001 through ORC-007)
2. Add oracle security tests (SEC-001 through SEC-006)
3. Complete exchange API tests (EXC-001 through EXC-005)
4. Fix DCA RED phase functions (DCA-001 through DCA-004)

### Sprint 3: Volatility & Edge Cases
1. Fix volatility TODO functions (VOL-001 through VOL-010)
2. Add boundary condition tests (BND-001 through BND-008)
3. Add error path coverage (ERR-001 through ERR-008)
4. Implement concurrent access tests (CON-001 through CON-004)

### Sprint 4: Functional & E2E
1. Add untested RPC command coverage (RPC-001 through RPC-009)
2. Implement E2E scenarios (E2E-001 through E2E-004)
3. Add multi-node tests (NET-001 through NET-004)
4. Add stress test scenarios (STR-001 through STR-004)

### Sprint 5: GUI & Polish
1. Expand Qt GUI tests for Send/Mint/Redeem (GUI-SN, GUI-MT, GUI-RD)
2. Add Receive and Transactions widget tests (GUI-RC, GUI-TX)
3. Complete Positions widget tests (GUI-PO)
4. Add protection system integration tests (INT-001 through INT-006)

### Sprint 6: Hardening
1. Remaining P2 items across all phases
2. Performance benchmarking suite
3. Long-running stability tests
4. Final documentation update

---

## VERIFICATION CHECKLIST

Before marking any task complete:

- [ ] Test passes in isolation
- [ ] Test passes with full test suite
- [ ] No regression in existing tests
- [ ] Edge cases documented
- [ ] Error messages are clear
- [ ] No memory leaks (valgrind clean)
- [ ] Thread-safe if applicable
- [ ] Performance acceptable (<100ms for unit tests)

---

## APPENDIX A: File Reference

### Unit Test Files
```
src/test/digidollar_*.cpp (27 files)
src/test/oracle_*.cpp (8 files)
```

### Functional Test Files
```
test/functional/digidollar_*.py (18 files)
test/functional/wallet_digidollar_*.py (2 files)
test/functional/feature_oracle_p2p.py
```

### Implementation Files Requiring Test Coverage
```
src/digidollar/validation.cpp
src/digidollar/txbuilder.cpp
src/wallet/digidollarwallet.cpp
src/rpc/digidollar.cpp
src/consensus/dca.cpp
src/consensus/err.cpp
src/consensus/volatility.cpp
src/oracle/*.cpp (4 files)
src/qt/digidollar*.cpp (10 files)
```

---

## APPENDIX B: Priority Definitions

| Priority | Definition | Timeline |
|----------|------------|----------|
| P0 | Blocks release - must fix | Sprint 1 |
| P1 | High risk - should fix | Sprint 2-3 |
| P2 | Important - nice to have | Sprint 4-6 |

---

*SPEC generated from deep verification analysis of DigiDollar test infrastructure.*
*Total tasks identified: 250+*
*Estimated test count increase: 1,106 -> 1,600+*
