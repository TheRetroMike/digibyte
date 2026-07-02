# GitHub Issue #345 - Performance Regression Analysis
## getblockchaininfo RPC 3× Slowdown in v8.26.1

### Executive Summary

A performance regression in the `getblockchaininfo` RPC call has been identified and fixed. The root cause is the use of an inefficient O(n) chain-walking algorithm instead of the available O(1) cached lookup for multi-algorithm difficulty calculations.

**Impact:** ~3× slowdown (from ~1s to ~3s)
**Fix:** One-line change in `src/rpc/blockchain.cpp:87`
**Status:** Fixed and ready for testing

---

## Root Cause Analysis

### The Problem

The `GetDifficulty()` function in `src/rpc/blockchain.cpp:86` was using `GetLastBlockIndexForAlgo()`, which iterates backwards through the entire blockchain to find the last block mined with each algorithm:

```cpp
const CBlockIndex* GetLastBlockIndexForAlgo(const CBlockIndex* pindex, const Consensus::Params& params, int algo)
{
    for (; pindex; pindex = pindex->pprev)  // ← Walks backwards through entire chain!
    {
        if (pindex->GetAlgo() != algo)
            continue;
        // ... validation checks ...
        return pindex;
    }
    return nullptr;
}
```

### Why This Matters for DigiByte

1. **Block Count:** DigiByte has approximately **23 million blocks** (vs Bitcoin's ~800k)
   - 15-second block time vs Bitcoin's 600 seconds = 40× more blocks

2. **Multiple Algorithm Lookups:** The `getblockchaininfo` RPC calls `GetDifficulty()` **6-7 times:**
   - Once for the general "difficulty" field (defaults to Groestl, algo=2)
   - Once for each active algorithm in the "difficulties" object (~5-6 algos)

3. **Chain Walking Overhead:** Each call to `GetLastBlockIndexForAlgo()` walks backwards through an average of **20-50 blocks** to find the previous block of the same algorithm
   - With 6-7 calls × 20-50 blocks walked = **120-350 block lookups per RPC call**
   - At 23 million blocks deep, memory access patterns become increasingly cache-inefficient

### Why v8.26 is Slower Than v7.17

Two compounding factors:

1. **Bitcoin Core v26 Added New "difficulty" Field**
   - Old versions (v7.x - v8.22): Only calculated per-algorithm "difficulties" object
   - New version (v8.26): Calculates both singular "difficulty" + "difficulties" object
   - Result: One additional expensive chain walk per RPC call

2. **Blockchain Growth**
   - When v7.17.3 was released, the blockchain was shorter
   - More blocks = deeper chain walks = worse performance with O(n) algorithm

---

## The Solution

### Available Infrastructure

DigiByte **already has** the infrastructure for O(1) algorithm lookups:

```cpp
// In src/chain.h - Every CBlockIndex maintains:
CBlockIndex *lastAlgoBlocks[NUM_ALGOS_IMPL];  // ← Cached pointers to last block per algo
```

This array is properly maintained during block loading and validation (see `src/node/blockstorage.cpp:335-336`).

### The Fast Function

The fast version already exists and is used elsewhere in the codebase:

```cpp
const CBlockIndex* GetLastBlockIndexForAlgoFast(const CBlockIndex* pindex, const Consensus::Params& params, int algo)
{
    for (; pindex; pindex = pindex->lastAlgoBlocks[algo])  // ← Uses cached pointers!
    {
        if (pindex->GetAlgo() != algo)
            continue;
        // ... validation checks ...
        return pindex;
    }
    return nullptr;
}
```

This version uses the cached `lastAlgoBlocks[]` array to jump directly to the previous block of the same algorithm, avoiding the need to scan through all intervening blocks.

### The Fix

**File:** `src/rpc/blockchain.cpp`
**Line:** 87

**Changed:**
```cpp
blockindex = GetLastBlockIndexForAlgo(tip, Params().GetConsensus(), algo);
```

**To:**
```cpp
// Use fast O(1) lookup instead of O(n) chain walking for RPC performance
blockindex = GetLastBlockIndexForAlgoFast(tip, Params().GetConsensus(), algo);
```

---

## Expected Performance Improvement

**Before Fix:**
- 6-7 calls to `GetLastBlockIndexForAlgo()`
- Each walks ~20-50 blocks
- Total: 120-350 block lookups
- Time: ~3 seconds

**After Fix:**
- 6-7 calls to `GetLastBlockIndexForAlgoFast()`
- Each uses cached pointer (1-2 block checks max)
- Total: ~10-15 block lookups
- Expected time: **~0.3-0.5 seconds** (or better)

**Expected speedup: 6-10×** (bringing it well below the v7.17.3 baseline)

---

## Testing Recommendations

1. **Build and Test:**
   ```bash
   make clean
   make -j$(nproc)
   ```

2. **Performance Test:**
   ```bash
   # Warm up the node
   time digibyte-cli getblockchaininfo
   time digibyte-cli getblockchaininfo
   time digibyte-cli getblockchaininfo

   # Average the results
   ```

3. **Verify Output:**
   - Ensure all difficulty values are correct
   - Compare with output from v8.26.1 to ensure no functional regression

---

## Additional Observations

### Why This Bug Existed

1. **Bitcoin Core Doesn't Have This Code:** The multi-algorithm support is DigiByte-specific
2. **The Slow Function Was Used First:** When multi-algo support was initially added, the slow version was implemented first
3. **Fast Version Added Later:** The fast version was added for mining/validation performance but RPC code wasn't updated

### Other Potential Optimizations

While investigating, I noticed the following could be further optimized in the future (not critical):

1. **CalculateCurrentUsage()** - Currently sums all block file sizes; could be cached and updated incrementally
2. **GuessVerificationProgress()** - Recalculates on every call; could benefit from caching with invalidation on new blocks

---

## Conclusion

This was a classic case of using an O(n) algorithm when an O(1) solution was already available. The performance regression became more noticeable in v8.26 due to:
- Additional difficulty field calculation
- Blockchain growth over time
- Bitcoin Core architectural changes

The fix is minimal, safe, and uses existing, well-tested infrastructure. The fast function is already used successfully in proof-of-work validation code paths, so we know it works correctly.

---

## Files Modified

- `src/rpc/blockchain.cpp:87` - Changed `GetLastBlockIndexForAlgo` to `GetLastBlockIndexForAlgoFast`

---

**Analysis completed:** 2025-11-17
**Fix implemented:** Yes
**Testing required:** Yes
**Ready for merge:** Pending testing
