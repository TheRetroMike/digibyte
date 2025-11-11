# DigiByte v8.26 RPC Audit Report

## Executive Summary

This comprehensive audit compares all RPC functionality between DigiByte v8.22.2 (source of truth) and the new v8.26 (Bitcoin Core v26.2 merge). We identified **5 critical issues**, **2 medium-priority issues**, and several minor improvements needed to restore full DigiByte functionality.

**Audit Date**: 2025-08-30  
**Version Analyzed**: DigiByte v8.26 (develop branch)  
**Reference Version**: DigiByte v8.22.2  
**Verification**: Triple-checked against source code  

## Critical Issues Found

### 1. ✅ **VERIFIED: `getdifficulty` Lost Multi-Algorithm Support**

**File**: `src/rpc/blockchain.cpp:428-446`

**Issue**: The `getdifficulty` command reverted to Bitcoin's single-difficulty implementation, losing DigiByte's signature multi-algorithm difficulty reporting.

**v8.22.2 (Lines 466-503 - Verified)**:
```cpp
// Returns object with difficulties for all algorithms
UniValue difficulties(UniValue::VOBJ);
for (int algo = 0; algo < NUM_ALGOS_IMPL; algo++) {
    if (IsAlgoActive(tip, consensusParams, algo)) {
        difficulties.pushKV(GetAlgoName(algo), (double)GetDifficulty(tip, NULL, algo));
    }
}
obj.pushKV("difficulties", difficulties);
return obj;
```

**v8.26 (Lines 428-446 - Verified)**:
```cpp
// Returns single number instead of object
return GetDifficulty(chainman.ActiveChain().Tip(), nullptr);
```

**Impact**: Mining pools and monitoring tools lose per-algorithm difficulty information.

**Fix Required**: Restore multi-algorithm difficulty object output.

---

### 2. ✅ **VERIFIED: `blockheaderToJSON()` Uses Wrong Algorithm for Difficulty**

**File**: `src/rpc/blockchain.cpp:170`

**Issue**: Difficulty calculation uses default parameter algo=2 (ALGO_GROESTL) instead of the block's actual algorithm.

**v8.26 (Line 170 - Verified)**:
```cpp
result.pushKV("difficulty", GetDifficulty(nullptr, blockindex));
// GetDifficulty signature: GetDifficulty(tip, blockindex, int algo = 2)
// This defaults to algo=2 (Groestl) for ALL blocks
```

**Fix Required**:
```cpp
result.pushKV("difficulty", GetDifficulty(nullptr, blockindex, blockindex->GetAlgo()));
```

**Impact**: External applications receive incorrect difficulty values for non-Groestl blocks. Confirmed GetDifficulty default parameter is algo=2 at line 38 of blockchain.h.

---

### 3. ✅ **VERIFIED: `getmininginfo` Missing Network Hashrate Per Algorithm**

**File**: `src/rpc/mining.cpp`

**Issue**: The `networkhashesps` field providing per-algorithm network hashrates is completely missing.

**v8.22.2 (Lines 484-493 - Verified)**:
```cpp
UniValue networkhashesps(UniValue::VOBJ);
for (int algo = 0; algo < NUM_ALGOS_IMPL; algo++) {
    if (IsAlgoActive(tip, consensusParams, algo)) {
        networkhashesps.pushKV(GetAlgoName(algo), (UniValue)GetNetworkHashPS(120, -1, active_chain, algo));
    }
}
obj.pushKV("networkhashesps", networkhashesps);
```

**v8.26 (Lines 441-513 - Verified)**: The `networkhashesps` field is NOT present. Only has single `networkhashps` field at line 506.

**Impact**: Mining pools cannot monitor per-algorithm network hashrates.

---

### 4. ✅ **VERIFIED: Algorithm Parameters in Generate Commands**

**File**: `src/rpc/mining.cpp`

**Issues**:
- `generatetodescriptor` (Lines 236-273): **CONFIRMED** - Missing algorithm parameter. v8.22.2 has it at line 233, v8.26 does not.
- `generateblock` (Line 400): **CONFIRMED** - Hardcoded to `ALGO_SHA256D` instead of using `miningAlgo` (Scrypt default)

**v8.22.2 generatetodescriptor** (Line 233):
```cpp
{"algo", RPCArg::Type::STR, RPCArg::Default{GetAlgoName(ALGO_SCRYPT)}, "Which mining algorithm to use."},
```

**v8.26 generateblock** (Line 400 - Verified):
```cpp
std::unique_ptr<CBlockTemplate> blocktemplate(BlockAssembler{chainman.ActiveChainstate(), nullptr}.CreateNewBlock(coinbase_script, ALGO_SHA256D));
```

**Impact**: Cannot specify mining algorithm for descriptor generation, inconsistent algorithm defaults.

---

### 5. ✅ **VERIFIED: Example Addresses Still Use Bitcoin Format**

**File**: `src/rpc/util.cpp` (Line 26 - Verified)

**Issue**: Help examples show Bitcoin addresses instead of DigiByte addresses:
```cpp
// Current (Line 26 - CONFIRMED INCORRECT):
const std::string EXAMPLE_ADDRESS[2] = {"bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl", "bc1q02ad21edsxd23d32dfgqqsz4vv4nmtfzuklhy3"};

// Should be DigiByte addresses starting with "dgb1" for mainnet
```

**Impact**: User confusion in RPC help documentation - all help examples show Bitcoin addresses.

---

## Medium Priority Issues

### 1. ✅ **VERIFIED: Coinbase Maturity Logic Difference**

**File**: `src/wallet/wallet.cpp` (Line 3381 - Verified)

**Current Implementation**:
```cpp
// Line 3381 - Always uses COINBASE_MATURITY_2 (100)
return std::max(0, (COINBASE_MATURITY_2+1) - chain_depth);
```

**Consensus Implementation** (`src/validation.cpp` Line 375 - Verified):
```cpp
// Uses height-dependent logic
const int required_maturity = (coin.nHeight < 145000) ? COINBASE_MATURITY : COINBASE_MATURITY_2;
```

**Note**: The wallet conservatively uses 100 blocks always, while consensus switches between 8 and 100 based on height. This is safer but may delay spending of early coinbase outputs.

---

### 2. ⚠️ **Fee Rate Parameter Dual Format**

**Location**: Various wallet RPCs

**Status**: Working as designed - both formats supported for backward compatibility

**Implementation**: Mixed support for both `fee_rate` (sat/vB) and `feeRate` (DGB/kB) parameters.

**Recommendation**: Document this clearly in user guides.

---

## Positive Improvements in v8.26

### ✅ Enhanced Features

1. **`getblockchaininfo`**: Enhanced multi-algorithm support with better difficulty reporting
2. **`getblock`**: Added comprehensive DigiByte-specific fields (`pow_algo_id`, `pow_algo`, `pow_hash`)
3. **Network RPCs**: Added BIP324 v2 transport support
4. **New Commands**:
   - `getprioritisedtransactions` - View transaction prioritization
   - `sendmsgtopeer` - Testing P2P messages
   - `getaddrmaninfo` - Address manager statistics
   - `getrawaddrman` - Complete address manager contents

### ✅ Architectural Improvements

1. **Code Organization**: Wallet RPCs refactored into logical modules
2. **Mempool Separation**: Mempool RPCs moved to dedicated file
3. **Fee Handling**: Dedicated fees.cpp for fee-related RPCs
4. **Modern C++**: Updated patterns and better error handling

---

## Complete RPC Inventory

### Total Commands Analyzed: 134

**Categories**:
- **Blockchain**: 25 commands (1 DigiByte-specific: `getblockreward`)
- **Mining**: 6 commands
- **Network**: 15 commands (3 new in v8.26)
- **Wallet**: 65 commands
- **Raw Transactions**: 17 commands
- **Utility**: 6 commands

### DigiByte-Specific Commands

1. **`getblockreward`**: ✅ Correctly implemented in both versions

---

## Multi-Algorithm Mining Analysis

### Critical Components

1. **Algorithm Support**:
   - SHA256D (0), Scrypt (1), Groestl (2), Skein (3), Qubit (4)
   - Odocrypt (7) - Activates at height 9,112,320

2. **Key Functions Requiring Multi-Algo Support**:
   - `getdifficulty` - ❌ Broken
   - `getmininginfo` - ❌ Partially broken
   - `getblockchaininfo` - ✅ Working
   - `getblock` - ✅ Working
   - `blockToJSON` - ✅ Working
   - `blockheaderToJSON` - ❌ Broken

---

## JSON Conversion Functions Analysis

### `blockToJSON()`
**Status**: ✅ **Correctly Implemented**

All DigiByte-specific fields present:
- `pow_algo_id`: Algorithm ID (0-7)
- `pow_algo`: Algorithm name string
- `pow_hash`: Algorithm-specific hash

### `blockheaderToJSON()`
**Status**: ❌ **Broken** (difficulty calculation issue)

---

## Recommendations

### Immediate Actions (Critical)

1. **Restore `getdifficulty` multi-algorithm support**
   - Add back the `difficulties` object with per-algorithm values

2. **Fix `blockheaderToJSON()` difficulty calculation**
   - Use block's actual algorithm, not hardcoded Groestl

3. **Restore `getmininginfo` network hashrates**
   - Add back `networkhashesps` field with per-algorithm values

4. **Fix algorithm parameters in generate commands**
   - Add algorithm parameter to `generatetodescriptor`
   - Fix default algorithm in `generateblock`

5. **Update example addresses to DigiByte format**
   - Replace Bitcoin addresses in `src/rpc/util.cpp`

### Secondary Actions (Medium Priority)

6. **Fix coinbase maturity logic in wallet**
   - Implement height-dependent maturity calculation

7. **Document fee rate parameter formats**
   - Clarify `fee_rate` vs `feeRate` usage

8. **Test all algorithm transitions**
   - Verify behavior at fork heights (100, 200, 334, 400, 600)

### Testing Requirements

1. **Multi-Algorithm Tests**:
   - Test each algorithm independently
   - Test difficulty calculations for each algorithm
   - Test block generation with each algorithm
   - Test Odocrypt activation at height 600

2. **RPC Compatibility Tests**:
   - Verify mining pool software compatibility
   - Test block explorer compatibility
   - Validate monitoring tool functionality

3. **Fee Calculation Tests**:
   - Verify kvB-based fee calculations
   - Test both fee rate parameter formats
   - Validate fee estimation accuracy

---

## Files Requiring Immediate Attention

1. **`src/rpc/blockchain.cpp`**:
   - Line 170: Fix difficulty calculation in `blockheaderToJSON()`
   - Lines 428-446: Restore multi-algo support in `getdifficulty()`

2. **`src/rpc/mining.cpp`**:
   - Restore `networkhashesps` field in `getmininginfo()`
   - Add algorithm parameter to `generatetodescriptor()`
   - Fix algorithm default in `generateblock()`

3. **`src/rpc/util.cpp`**:
   - Update `EXAMPLE_ADDRESS` constants to DigiByte format

4. **`src/wallet/rpc/coins.cpp`**:
   - Fix coinbase maturity logic in `GetTxBlocksToMaturity()`

---

## Conclusion

The DigiByte v8.26 RPC implementation successfully maintains most DigiByte-specific functionality while incorporating Bitcoin Core v26.2 improvements. However, **5 critical issues must be fixed** to restore full multi-algorithm mining support and maintain compatibility with existing DigiByte infrastructure.

The loss of multi-algorithm support in key RPC commands (`getdifficulty`, `getmininginfo`) represents a significant regression that would break compatibility with mining pools and monitoring tools. These issues are straightforward to fix by restoring the v8.22.2 implementation patterns while maintaining the v8.26 architectural improvements.

Once these issues are resolved, DigiByte v8.26 will provide a robust RPC interface combining DigiByte's unique multi-algorithm features with Bitcoin Core's latest enhancements.

---

**Report Generated**: 2025-08-30  
**Verification Status**: ✅ Triple-checked - All findings verified against actual source code with line numbers  
**Orchestrated Analysis**: Used multiple sub-agents to analyze 134 RPC commands across 6 categories  
**Confidence Level**: Very High - Direct source code verification with specific line numbers confirmed