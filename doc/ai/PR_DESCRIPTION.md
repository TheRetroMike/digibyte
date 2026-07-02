# Fix: getmininginfo difficulty field always returns Groestl difficulty (Issue #346)

## Summary
Fixes #346 - The `difficulty` field in `getmininginfo` RPC was returning a frozen/incorrect value because it was always querying Groestl's difficulty instead of the current mining algorithm's difficulty.

## Problem Description
In DigiByte v8.26.1, users reported that the `difficulty` field in the `getmininginfo` RPC method displays a frozen value that doesn't update correctly. The `difficulties` field (plural) works as expected, but the singular `difficulty` field was broken.

## Root Cause
During the Bitcoin Core v26.2 merge, the `GetDifficulty()` call in `getmininginfo` lost the algorithm parameter.

**In v8.22.2 (working):**
```cpp
obj.pushKV("difficulty", (double)GetDifficulty(tip, NULL, miningAlgo));
```

**In v8.26.1 (broken):**
```cpp
obj.pushKV("difficulty", (double)GetDifficulty(tip));
```

Since DigiByte's `GetDifficulty()` function signature is:
```cpp
double GetDifficulty(const CBlockIndex* tip = NULL, const CBlockIndex* blockindex = nullptr, int algo = 2);
```

When called with only one parameter, `algo` defaults to `2` (ALGO_GROESTL), causing it to always return Groestl's difficulty regardless of the user's configured mining algorithm.

## Changes Made
- **File:** `src/rpc/mining.cpp`
- **Line 526:** Restored the `miningAlgo` parameter to the `GetDifficulty()` call
- **Change:** `GetDifficulty(tip)` → `GetDifficulty(tip, nullptr, miningAlgo)`

## Testing
This fix ensures that:
1. The `difficulty` field returns the difficulty for the **current mining algorithm** (as set by `-miningalgo`)
2. Behavior matches DigiByte v8.22.2
3. The field updates correctly as blocks arrive for different algorithms

**Test commands:**
```bash
# Test with different algorithms
digibyted -miningalgo=sha256d
digibyte-cli getmininginfo | jq '.difficulty'

digibyted -miningalgo=scrypt
digibyte-cli getmininginfo | jq '.difficulty'

# Verify difficulty matches the corresponding value in difficulties object
digibyte-cli getmininginfo | jq '{difficulty, difficulties}'
```

## Impact
- **Severity:** Medium - Affects mining operations and monitoring
- **Affected versions:** DigiByte v8.26.0, v8.26.1
- **Scope:** Users running any algorithm other than Groestl see incorrect difficulty values

## Additional Context
This is a regression introduced during the Bitcoin Core v26.2 merge. It highlights the importance of preserving DigiByte-specific multi-algorithm functionality when integrating upstream Bitcoin changes. The `difficulties` field was unaffected because it explicitly iterates through all algorithms with the correct parameters.

---

**Repository:** `DigiByte-Core/digibyte`
**Branch:** `claude/investigate-digibyte-issue-018eRW2M8brZzweTZpj1e89B`
**Target:** `develop`
**Commit:** c6f3663

**Create PR Link:**
```
https://github.com/DigiByte-Core/digibyte/compare/develop...claude/investigate-digibyte-issue-018eRW2M8brZzweTZpj1e89B?expand=1
```
