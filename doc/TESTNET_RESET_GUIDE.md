# DigiByte Testnet Reset Guide - v9.26 DigiDollar Edition

> **Archived historical guide - do not use for RC44 operator setup.**
> This document records a November 2025 reset procedure and contains stale
> values such as old ports, old data-directory names, and height-650 testnet
> activation assumptions. The current RC44 reset uses `testnet26`, P2P port
> `12033`, RPC port `14026`, magic `fe c6 b9 e7`, and activates
> DigiDollar/oracle rules on testnet at height `600`. Use
> `RELEASE_v9.26.0-rc44.md` and `src/kernel/chainparams.cpp` as the current
> source of truth.

**Document Version:** 2.0 - COMPLETE WITH SCRYPT GENESIS MINING
**Target Release:** DigiByte v9.26.0
**Date:** November 23, 2025 (Updated with successful testnet reset)
**Purpose:** Reset DigiByte testnet to enable DigiDollar Phase One testing
**Status:** ✅ TESTNET SUCCESSFULLY RESET - Genesis block mined and validated
**Update:** Added CRITICAL scrypt genesis mining fix and complete validation

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Why Reset Testnet?](#2-why-reset-testnet)
3. [What Is a Testnet Reset?](#3-what-is-a-testnet-reset)
4. [Technical Requirements](#4-technical-requirements)
5. [Step-by-Step Reset Process](#5-step-by-step-reset-process)
6. [Configuration Changes Required](#6-configuration-changes-required)
7. [Community Coordination](#7-community-coordination)
8. [Testing and Validation](#8-testing-and-validation)
9. [Deployment and Migration](#9-deployment-and-migration)
10. [FAQ](#10-faq)
11. [Appendix A: Complete Code Changes](#appendix-a-complete-code-changes)
12. [Appendix B: Genesis Block Mining Tool](#appendix-b-genesis-block-mining-tool)
13. [Appendix C: DNS Seed Coordination](#appendix-c-dns-seed-coordination)

---

## 1. Executive Summary

### What This Guide Covers

This guide provides a complete, step-by-step process for resetting the DigiByte testnet blockchain to create a fresh testing environment for **DigiDollar Phase One** and the **Oracle System**.

### Current Situation

- **Testnet Age**: Running since January 26, 2018 (Genesis block timestamp: `1516939474`)
- **Block Height**: ~7+ years of blocks accumulated
- **DigiDollar Status**: Phase One implementation complete, needs on-chain testing
- **Oracle System Status**: Phase One complete (159 tests passing: 123 oracle + 35 integration + 1 functional)
- **Problem**: Long testnet history and high activation heights make DigiDollar testing cumbersome

### What This Reset Achieves

1. ✅ **Fresh blockchain** starting from block 0
2. ✅ **New network ports** (P2P: 12028, RPC: 14024) - prevents old client interference
3. ✅ **Regtest-matching activation heights** - consistent testing environment
4. ✅ **Oracle at height 650** (with DigiDollar, matches regtest)
5. ✅ **DigiDollar at height 650** (with Oracle, after Odocrypt at 600, matches regtest)
6. ✅ **All BIPs at height 1** (instant, matching regtest)
7. ✅ **Odocrypt at height 600** (matching regtest)
8. ✅ **Genesis block with DigiDollar whitepaper headline**
9. ✅ **New magic bytes** - prevents old client interference at protocol level
10. ✅ **Clean testing environment** for community validators
11. ✅ **No impact on mainnet** - completely isolated testnet change

### Who Needs This Guide?

- **Core Developers**: Implementing the testnet reset
- **DNS Seed Operators**: Updating testnet seed configurations
- **Community Validators**: Understanding the reset process
- **AI Agents**: Future reference for testnet management

### ⚠️ CRITICAL LEARNING: Scrypt Genesis Mining

**The #1 Mistake That Will Break Your Genesis Block:**

```diff
- ❌ WRONG: if (UintToArith256(genesis.GetHash()) <= hashTarget)
+ ✅ CORRECT: if (UintToArith256(genesis.GetPoWAlgoHash(consensus)) <= hashTarget)
```

**Why This Matters:**
- DigiByte genesis block (block 0) uses **SCRYPT** algorithm (before multi-algo activation)
- `GetHash()` returns SHA256d hash (block identifier)
- `GetPoWAlgoHash()` returns algorithm-specific hash (SCRYPT for genesis)
- **You MUST validate PoW using the scrypt hash, not the SHA256d hash!**

**Two Different Hashes:**
1. **Block Hash** (SHA256d): Used as block identifier in blockchain
2. **PoW Hash** (Scrypt): Used for proof-of-work validation

This was discovered during the November 2025 testnet reset when genesis validation was failing with "Errors in block header" despite correct nonce values.

---

## 2. Why Reset Testnet?

### The DigiDollar Challenge

**DigiDollar** (world's first UTXO-native stablecoin) requires extensive on-chain testing:

1. **Minting Process**: Lock DGB, create DigiDollars
2. **Transfer System**: Send DigiDollars between addresses
3. **Oracle Price Feeds**: Real-time DGB/USD pricing (Phase One complete - 159 tests passing)
4. **Redemption System**: Burn DigiDollars, recover DGB
5. **Protection Systems**: DCA, ERR, Volatility monitoring

### Current Testnet Issues

**Problem 1: Long Block History**
- Testnet has been running since 2018 (7+ years)
- Oracle activation set at height **1,000,000** (would take months to reach)
- DigiDollar activation at height **1,000** (reachable but with legacy blocks)
- Difficult to test activation scenarios
- **Old clients would interfere** with new testnet if ports remain the same

**Problem 2: Legacy Data**
- Checkpoint at block **546** from old testnet
- Genesis hash from 2018: `0x308ea071...`
- Accumulated state makes clean testing difficult

**Problem 3: Testing Efficiency**
- Community needs to test DigiDollar **immediately**
- Can't wait for specific activation heights
- Need reproducible testing scenarios

### Benefits of Fresh Testnet

✅ **Regtest-Matching Heights**: Testnet activation sequence matches regtest EXACTLY
✅ **Network Isolation**: New ports + magic bytes prevent old client interference
✅ **Oracle + DigiDollar Together**: Both activate at block 650 for synchronized testing
✅ **DigiDollar After Odocrypt**: Activates at 650 after Odocrypt stabilizes at 600
✅ **Proper Fork Sequence**: BIPs (1) → Odocrypt (600) → Oracle+DigiDollar (650)
✅ **Clean State**: No legacy transactions or confusing history
✅ **Community Onboarding**: New validators start from genesis
✅ **Reproducible**: Everyone starts from same block 0
✅ **Fast Iterations**: Test activation scenarios within ~2.5 hours (650 blocks)

---

## 3. What Is a Testnet Reset?

### Conceptual Overview

A **testnet reset** means:

1. **Creating a new genesis block** (block 0) with a new timestamp and hash
2. **Clearing all blockchain data** from testnet nodes
3. **Starting the blockchain from scratch** with new consensus rules
4. **Maintaining network identity** (same ports, magic bytes to avoid mainnet confusion)

### What Changes

| Component | Before Reset | After Reset | Why Change? |
|-----------|--------------|-------------|-------------|
| **Genesis Block** | Hash: `0x308ea071...` (2018) | New hash with 2025 timestamp | Start fresh chain |
| **P2P Port** | 12026 | **12028** | Prevent old client interference |
| **RPC Port** | 14023 | **14024** | Prevent old client interference |
| **Block Height** | ~4+ million blocks | 0 (start from genesis) | Clean slate |
| **Checkpoints** | Block 546 checkpoint | Only genesis checkpoint | Remove old validation |
| **BIP34/65/66/CSV** | Various heights | Height **1** | Match regtest |
| **MultiAlgo/DigiShield Forks** | 100/400/1430/20000 | Height **1** | Match regtest |
| **Odocrypt** | Height 600 | Height **600** | Match regtest |
| **Oracle Activation** | Height 1,000,000 | Height **650** | Match regtest (with DigiDollar) |
| **DigiDollar Activation** | Height 1,000 | Height **650** | Match regtest (with Oracle) |
| **Blockchain Data** | Years of history | Fresh, empty | Remove legacy state |

### What Stays the Same

| Component | Value | Reason |
|-----------|-------|--------|
| **Magic Bytes** | `0xfd 0xc8 0xbd 0xdd` | Keep testnet identity (prevents mainnet cross-talk) |
| **Bech32 HRP** | `dgbt` | Address format consistency |
| **DNS Seeds** | 5 testnet seeds (update to new ports) | Community infrastructure |
| **Block Time** | 15 seconds | DigiByte consensus |
| **Mining Algorithms** | 5 algorithms | DigiByte multi-algo |
| **Genesis Reward** | 8000 DGB | DigiByte standard |

### Critical Change: Network Ports

**Why Change Ports?**

Old testnet clients (running 2018 genesis) will still exist in the wild. If we keep the same ports:
- ❌ Old clients try to connect to new testnet nodes
- ❌ Incompatible genesis blocks cause sync failures
- ❌ Network confusion and peer connection issues
- ❌ Old abandoned nodes interfere with new network

**Solution: New Ports**
- ✅ P2P: **12026 → 12028** (prevents P2P interference)
- ✅ RPC: **14023 → 14024** (prevents RPC confusion)
- ✅ Old testnet dies naturally (no peers on old ports)
- ✅ New testnet has clean peer set from day 1

### Critical Change: Activation Heights Match Regtest

**Why Match Regtest?**

Regtest is designed for **instant testing** - all features active immediately. Testnet should match this for efficient development:

**Regtest Activation Heights (From src/kernel/chainparams.cpp:759-954):**
- BIP34/65/66/CSV: Height **1**
- Segwit: Height **0** (genesis)
- MultiAlgo/DigiShield Forks: Height **1** (but different values: 100/200/400/600)
- Odocrypt: Height **600**
- **Oracle: Height 650** (with DigiDollar - line 954)
- **DigiDollar: Height 650** (with Oracle, after Odocrypt - line 951)

**New Testnet Activation Heights (Matching Regtest EXACTLY):**
- BIP34/65/66/CSV: Height **1** (was 500/1351/1251/1)
- Segwit: Height **0** (already matched)
- MultiAlgo Forks: **100/200/400/600** (was 100/400/1430/20000) - Match regtest values!
- Odocrypt: Height **600** (already matched)
- **Oracle: Height 650** (was 1,000,000) - With DigiDollar like regtest!
- **DigiDollar: Height 650** (was 1,000) - With Oracle, AFTER Odocrypt like regtest!

**Benefits:**
- ✅ **Oracle + DigiDollar at height 650** - synchronized activation
- ✅ Both activate after Odocrypt stabilizes at 600
- ✅ **Identical** activation sequence to regtest (perfect consistency)
- ✅ Respects fork order: BIPs (1) → Odocrypt (600) → Oracle+DigiDollar (650)
- ✅ Consistent testing environment across regtest and testnet
- ✅ Faster than old testnet (650 vs 1,000,000 for Oracle, 650 vs 1,000 for DD)
- ✅ Easier for developers - EXACT same heights as regtest
- ✅ Both available at ~2.7 hours (650 blocks × 15s)

---

## 4. Technical Requirements

### Prerequisites

**For Core Developers Implementing Reset:**
- ✅ Access to DigiByte source code (`/home/jared/Code/digibyte/`)
- ✅ C++ build environment (autotools, gcc/clang, make)
- ✅ Understanding of genesis block mining
- ✅ Ability to coordinate with DNS seed operators

**For DNS Seed Operators:**
- ✅ Access to DNS seed server configuration
- ✅ Ability to clear seed database
- ✅ Coordination with core developers for reset timing

**For Community Validators:**
- ✅ DigiByte v9.26 binaries or source code
- ✅ Ability to delete testnet data directory
- ✅ Basic blockchain node operation knowledge

### Software Versions

- **DigiByte Core**: v9.26.0 (release candidate or final)
- **Protocol Version**: 70019
- **Bitcoin Core Base**: v26.2 (merged features)

### Disk Space Requirements

| Node Type | Disk Space | Notes |
|-----------|------------|-------|
| Fresh Testnet Node | ~1 GB | First few months |
| Full Testnet Node (1 year) | ~10-20 GB | Estimated |
| Pruned Testnet Node | ~2-5 GB | With pruning enabled |

---

## 5. Step-by-Step Reset Process

### Phase 1: Preparation (Week 1)

#### Step 1.1: Community Announcement

**Who**: Core Development Team
**Timeline**: 2 weeks before reset
**Channels**:
- GitHub Discussions: https://github.com/DigiByte-Core/digibyte/discussions
- DigiByte Discord: #development channel
- DigiByte Reddit: r/DigiByte
- Twitter/X: @DigiByteCoin

**Announcement Template:**
```markdown
## DigiByte Testnet Reset Scheduled

**Date**: [Insert Date]
**Time**: [Insert UTC Time]
**Version**: v9.26.0

The DigiByte testnet will be reset to enable DigiDollar Phase One testing.

**What You Need to Do:**
1. Update to v9.26 when released
2. Delete your testnet data directory: `rm -rf ~/.digibyte/testnet4/`
3. Restart your node: `digibyted -testnet`

**What's Changing:**
- New genesis block with DigiDollar whitepaper headline
- **New magic bytes** (0xfc... - CRITICAL for network isolation!)
- **New ports: P2P 12028, RPC 14024** (additional isolation)
- All BIPs at height 1
- Odocrypt activation at height 600
- Oracle activation at height 650 (with DigiDollar!)
- DigiDollar activation at height 650 (with Oracle!)

**What's Staying the Same:**
- Network magic bytes (0xfdc8bddd)
- Address formats (dgbt bech32)

**Questions?** Ask in #development on Discord.
```

#### Step 1.2: Coordinate with DNS Seed Operators

**Current Testnet DNS Seeds** (from `/src/kernel/chainparams.cpp:466-471`):
1. `testnetseed.diginode.tools` (Olly Stedall @saltedlolly)
2. `testseed.digibyteblockchain.org` (John Song @j50ng)
3. `testnet.digibyteseed.com` (Jan De Jong @jongjan88)
4. `testnetseed.digibyte.link` (Bastian Driessen @bastiandriessen)
5. `testnetseed.digibyte.services` (Craig Donnachie @cdonnachie)

**Coordination Email Template:**
```
Subject: DigiByte Testnet Reset - DNS Seed Update Required

Hi [Operator Name],

We're planning a testnet reset for DigiDollar testing on [DATE] at [TIME] UTC.

ACTION REQUIRED:
1. Clear your testnet seed database on [DATE]
2. Restart seeder with v9.26 after reset
3. Monitor for new genesis block: [NEW_GENESIS_HASH]

Details:
- New genesis block will be mined on [DATE]
- All nodes will start from height 0
- Same ports/network config (no DNS changes needed)

Please confirm you can accommodate this reset.

Thanks,
DigiByte Core Team
```

#### Step 1.3: Prepare Build Environment

```bash
# Clone or update repository
cd /home/jared/Code/digibyte
git checkout develop
git pull origin develop

# Create feature branch for testnet reset
git checkout -b testnet-reset-v9.26

# Ensure clean build environment
make clean
./autogen.sh
```

---

### Phase 2: Code Changes (Days 1-2)

#### Step 2.1: Mine New Genesis Block

**Understanding Genesis Block Generation:**

A genesis block is the first block (block 0) in a blockchain. To create a new one, you need to:
1. Choose a **timestamp message** (we're using the DigiDollar whitepaper headline)
2. Choose a **Unix timestamp** (current time when you mine it)
3. Find a **nonce** (random number) that makes the block hash valid
4. Calculate the **merkle root hash**
5. Get the final **block hash**

**The Problem:** You don't know the nonce or final hash until you mine it!

**The Solution:** Temporarily disable the hash checks, let the node mine it, then update the code with the correct values.

**Visual Flow:**
```
┌─────────────────────────────────────────────────┐
│ 1. Update timestamp message in code            │
│    "DigiDollar: A Fully Decentralized..."      │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ 2. Add temporary mining code                    │
│    (loops to find valid nonce)                  │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ 3. Compile and run node                         │
│    Mining happens automatically                 │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ 4. Node prints genesis values                   │
│    nTime, nNonce, Hash, Merkle                  │
│    → SAVE THESE VALUES! ←                       │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ 5. Replace temporary code with permanent        │
│    Use saved values in CreateGenesisBlock()     │
└─────────────────┬───────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────┐
│ 6. Rebuild and verify                           │
│    ✅ Genesis block ready!                      │
└─────────────────────────────────────────────────┘
```

---

## APPROACH 1: Simple Method (Recommended)

This is the easiest way - let DigiByte Core mine the genesis block for you.

### Step 2.1a: Update CreateGenesisBlock Function

**File:** `src/kernel/chainparams.cpp`

**Find the CreateGenesisBlock helper function** (around line 73-78):

```cpp
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "USA Today: 10/Jan/2014, Target: Data stolen from up to 110M customers";
    const CScript genesisOutputScript = CScript() << 0x0 << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}
```

**Replace the timestamp with DigiDollar whitepaper headline:**

```cpp
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "DigiDollar: A Fully Decentralized USD Stablecoin on The DigiByte Blockchain";
    const CScript genesisOutputScript = CScript() << 0x0 << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}
```

### Step 2.1b: Comment Out Genesis Hash Assertions (TEMPORARY!)

**File:** `src/kernel/chainparams.cpp`

**Find testnet genesis block** (around lines 458-461):

```cpp
genesis = CreateGenesisBlock(1516939474, 2411473, 0x1e0ffff0, 1, 8000);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256S("0x308ea0711d5763be2995670dd9ca9872753561285a84da1d58be58acaa822252"));
assert(genesis.hashMerkleRoot == uint256S("0x72ddd9496b004221ed0557358846d9248ecd4c440ebd28ed901efc18757d0fad"));
```

**Replace with CORRECT temporary mining code (USING SCRYPT):**

```cpp
// TEMPORARY: Mine new genesis block with SCRYPT PoW
// ⚠️ CRITICAL: Genesis uses SCRYPT, not SHA256d!
const char* pszTimestamp = "DigiDollar: A Fully Decentralized USD Stablecoin on The DigiByte Blockchain";
const CScript genesisOutputScript = CScript() << 0x0 << OP_CHECKSIG;
genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, std::time(nullptr), 0, 0x1e0ffff0, 1, 8000 * COIN);

// Genesis mining - CRITICAL: Use scrypt PoW hash, not SHA256d!
// DigiByte genesis (block 0) uses SCRYPT algorithm before multi-algo activation
printf("\n=== MINING NEW TESTNET GENESIS BLOCK (SCRYPT) ===\n");
arith_uint256 hashTarget = arith_uint256().SetCompact(genesis.nBits);
while (true) {
    // ⚠️ CRITICAL: Use GetPoWAlgoHash() for scrypt, not GetHash() which is SHA256d!
    uint256 powHash = genesis.GetPoWAlgoHash(consensus);
    if (UintToArith256(powHash) <= hashTarget) {
        consensus.hashGenesisBlock = genesis.GetHash();  // Block ID hash (SHA256d)
        printf("\n=== NEW TESTNET GENESIS BLOCK FOUND ===\n");
        printf("nTime: %u\nnNonce: %u\n", genesis.nTime, genesis.nNonce);
        printf("Block Hash (SHA256d): %s\n", consensus.hashGenesisBlock.GetHex().c_str());
        printf("PoW Hash (Scrypt): %s\n", powHash.GetHex().c_str());
        printf("Merkle: %s\n", genesis.hashMerkleRoot.GetHex().c_str());
        printf("\nAdd to chainparams.cpp:\n");
        printf("genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, %u, %u, 0x%08x, 1, 8000 * COIN);\n",
               genesis.nTime, genesis.nNonce, genesis.nBits);
        printf("assert(consensus.hashGenesisBlock == uint256S(\"0x%s\"));\n", consensus.hashGenesisBlock.GetHex().c_str());
        printf("assert(genesis.hashMerkleRoot == uint256S(\"0x%s\"));\n", genesis.hashMerkleRoot.GetHex().c_str());
        printf("=====================================\n\n");
        break;
    }
    genesis.nNonce++;
    if (genesis.nNonce % 10000 == 0) printf("Nonce: %u\r", genesis.nNonce);
    if (genesis.nNonce == 0) {
        genesis.nTime++;
        genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    }
}

// Temporary: Comment out assertions - will add after mining
// assert(consensus.hashGenesisBlock == uint256S("0x..."));
// assert(genesis.hashMerkleRoot == uint256S("0x..."));
```

**⚠️ CRITICAL NOTES:**

1. **Use `8000 * COIN`** not just `8000` - This is 8000 DGB (800,000,000,000 satoshis)
2. **Use `GetPoWAlgoHash(consensus)`** not `GetHash()` - Genesis uses SCRYPT
3. **Use full CreateGenesisBlock signature** with explicit timestamp message
4. **Print both hashes** - Block Hash (SHA256d) and PoW Hash (Scrypt) for debugging

**IMPORTANT:** Add this include at the top of the file if not already present:
```cpp
#include <ctime>
#include <arith_uint256.h>
```

### Step 2.1c: Compile and Run to Mine Genesis

```bash
cd /home/jared/Code/digibyte

# Clean build
make clean

# Rebuild (this will take a few minutes)
make -j$(nproc)

# Run digibyted briefly - it will mine the genesis and print the values
# NOTE: The node will appear to "hang" for 30-120 seconds while mining - this is NORMAL!
./src/digibyted -testnet

# You'll see output like:
# "Mining genesis block..." (it's working!)
# After 30-120 seconds, you'll see:
# "=== NEW TESTNET GENESIS BLOCK ==="
# [your values here]

# Once you see the genesis values printed, press Ctrl+C to stop
```

**Alternative: Run in daemon mode and check logs:**
```bash
# Start as daemon (background)
./src/digibyted -testnet -daemon

# Wait 2 minutes for mining to complete, then check logs
sleep 120

# Check debug.log for the printed values
tail -100 ~/.digibyte/testnet4/debug.log | grep -A 15 "NEW TESTNET GENESIS"

# Stop the node
./src/digibyte-cli -testnet stop
```

**Expected Output in debug.log:**
```
=== NEW TESTNET GENESIS BLOCK ===
nTime: 1732400000
nNonce: 1829473
Hash: a1b2c3d4e5f6...
Merkle: 8f9e3d2c1a0b...

Add to chainparams.cpp:
genesis = CreateGenesisBlock(1732400000, 1829473, 0x1e0ffff0, 1, 8000);
assert(consensus.hashGenesisBlock == uint256S("0xa1b2c3d4e5f6..."));
assert(genesis.hashMerkleRoot == uint256S("0x8f9e3d2c1a0b..."));
=================================
```

**SAVE THESE VALUES!** You need them for the next step.

### Step 2.1d: Update Code with Permanent Values

Now replace the temporary mining code with the permanent configuration:

**File:** `src/kernel/chainparams.cpp`

**Remove the temporary mining code and replace with:**

```cpp
// NEW TESTNET GENESIS (2025) - DigiDollar Phase One
// Timestamp message: "DigiDollar: A Fully Decentralized USD Stablecoin on The DigiByte Blockchain"
genesis = CreateGenesisBlock([USE_YOUR_nTime], [USE_YOUR_nNonce], 0x1e0ffff0, 1, 8000);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256S("0x[USE_YOUR_HASH]"));
assert(genesis.hashMerkleRoot == uint256S("0x[USE_YOUR_MERKLE]"));
```

**Example with actual values:**
```cpp
genesis = CreateGenesisBlock(1732400000, 1829473, 0x1e0ffff0, 1, 8000);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256S("0xa1b2c3d4e5f6..."));
assert(genesis.hashMerkleRoot == uint256S("0x8f9e3d2c1a0b..."));
```

### Step 2.1e: Rebuild and Verify

```bash
# Rebuild with permanent values
make clean && make -j$(nproc)

# Test that it starts without errors
./src/digibyted -testnet -daemon

# Check it loaded correctly
./src/digibyte-cli -testnet getblockchaininfo

# Should show:
# "chain": "test",
# "blocks": 0,
# "headers": 0,
# "bestblockhash": "0x[YOUR_NEW_GENESIS_HASH]"

# Stop the node
./src/digibyte-cli -testnet stop
```

✅ **Genesis block mining complete!** Move to Step 2.2.

---

### Troubleshooting Genesis Mining

**Problem: Node crashes during startup**
- Check that you added the includes: `#include <ctime>` and `#include <arith_uint256.h>`
- Verify the mining code is in the CTestNetParams constructor, not another class
- Check compile errors: `make 2>&1 | grep error`

**Problem: Mining takes too long (>5 minutes)**
- This is unusual but can happen with bad luck
- The difficulty (0x1e0ffff0) should make it quick (<2 minutes average)
- Try restarting - different timestamp = different mining difficulty

**Problem: No output in debug.log**
- Make sure you're checking the right file: `~/.digibyte/testnet4/debug.log`
- Try running in foreground mode: `./src/digibyted -testnet` (no -daemon)
- Check if printf is being redirected: `./src/digibyted -testnet 2>&1 | grep GENESIS`

**Problem: Can't find the genesis values I printed**
- Check terminal output if you ran in foreground
- Check debug.log: `grep -r "NEW TESTNET GENESIS" ~/.digibyte/`
- If lost, just rebuild and run again - mining only takes 1-2 minutes

**Problem: Assert fails after updating with values**
- Double-check you copied the FULL hash (64 hex characters)
- Verify the merkle root matches exactly
- Make sure nTime and nNonce are correct numbers (not hex)
- Remember: Hash and Merkle have "0x" prefix in assert, but nTime/nNonce don't

---

## APPROACH 2: Advanced Standalone Mining Tool (Optional)

If you prefer to mine the genesis block externally without modifying the main code, use this C++ tool.

**Create Genesis Block Mining Tool:**

**CRITICAL**: Use DigiDollar whitepaper headline as timestamp message!

Save this script as `/home/jared/Code/digibyte/contrib/devtools/mine_genesis.cpp`:

```cpp
#include <iostream>
#include <ctime>
#include <primitives/block.h>
#include <hash.h>
#include <streams.h>
#include <util/strencodings.h>
#include <chainparams.h>
#include <arith_uint256.h>

// Compile: g++ -o mine_genesis mine_genesis.cpp -I../../src -std=c++17 -ldigibyte_consensus

int main() {
    // Testnet genesis parameters
    // Use DigiDollar whitepaper headline as genesis timestamp
    const char* pszTimestamp = "DigiDollar: A Fully Decentralized USD Stablecoin on The DigiByte Blockchain";

    // Current Unix timestamp (November 23, 2025)
    uint32_t nTime = std::time(nullptr);

    // Testnet difficulty (same as previous testnet)
    uint32_t nBits = 0x1e0ffff0;

    // Block version and reward
    int32_t nVersion = 1;
    CAmount genesisReward = 8000;  // 80 DGB in satoshis

    // Create output script: OP_0 OP_CHECKSIG (same as all networks)
    CScript genesisOutputScript = CScript() << 0x0 << OP_CHECKSIG;

    // Build genesis block
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript()
        << 486604799
        << CScriptNum(4)
        << std::vector<unsigned char>((const unsigned char*)pszTimestamp,
                                      (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);

    // Mine genesis block
    std::cout << "Mining testnet genesis block...\n";
    std::cout << "Timestamp: " << nTime << "\n";
    std::cout << "Difficulty: 0x" << std::hex << nBits << std::dec << "\n\n";

    // Compute target from nBits
    arith_uint256 bnTarget;
    bnTarget.SetCompact(nBits);

    uint32_t nNonce = 0;
    while (true) {
        genesis.nNonce = nNonce;
        uint256 hash = genesis.GetHash();

        if (UintToArith256(hash) <= bnTarget) {
            // Found valid genesis block!
            std::cout << "SUCCESS!\n\n";
            std::cout << "Genesis Block Found:\n";
            std::cout << "nTime: " << genesis.nTime << "\n";
            std::cout << "nNonce: " << genesis.nNonce << "\n";
            std::cout << "nBits: 0x" << std::hex << genesis.nBits << std::dec << "\n";
            std::cout << "Hash: 0x" << hash.GetHex() << "\n";
            std::cout << "Merkle Root: 0x" << genesis.hashMerkleRoot.GetHex() << "\n\n";

            std::cout << "Add to chainparams.cpp:\n";
            std::cout << "genesis = CreateGenesisBlock("
                      << genesis.nTime << ", "
                      << genesis.nNonce << ", "
                      << "0x" << std::hex << genesis.nBits << std::dec << ", "
                      << genesis.nVersion << ", "
                      << genesisReward << ");\n";
            std::cout << "consensus.hashGenesisBlock = genesis.GetHash();\n";
            std::cout << "assert(consensus.hashGenesisBlock == uint256S(\"0x"
                      << hash.GetHex() << "\"));\n";
            std::cout << "assert(genesis.hashMerkleRoot == uint256S(\"0x"
                      << genesis.hashMerkleRoot.GetHex() << "\"));\n";

            break;
        }

        nNonce++;

        // Progress indicator
        if (nNonce % 100000 == 0) {
            std::cout << "Tried " << nNonce << " nonces...\n";
        }
    }

    return 0;
}
```

**Compile and Run:**
```bash
cd /home/jared/Code/digibyte/contrib/devtools
g++ -o mine_genesis mine_genesis.cpp \
    -I../../src \
    -I../../src/secp256k1/include \
    -L../../src/.libs \
    -ldigibyte_consensus \
    -lsecp256k1 \
    -std=c++17 \
    -O2

./mine_genesis
```

**Expected Output:**
```
Mining testnet genesis block...
Timestamp: 1732329600
Difficulty: 0x1e0ffff0

Tried 100000 nonces...
Tried 200000 nonces...
...
SUCCESS!

Genesis Block Found:
nTime: 1732329600
nNonce: 2819473
nBits: 0x1e0ffff0
Hash: 0xabcd1234567890abcdef...
Merkle Root: 0x72ddd9496b004221ed0557358846d9248ecd4c440ebd28ed901efc18757d0fad

Add to chainparams.cpp:
genesis = CreateGenesisBlock(1732329600, 2819473, 0x1e0ffff0, 1, 8000);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256S("0xabcd1234567890abcdef..."));
assert(genesis.hashMerkleRoot == uint256S("0x72ddd9496b004221ed0557358846d9248ecd4c440ebd28ed901efc18757d0fad"));
```

**Save the Output:** You'll need these values to update chainparams.cpp (same as Step 2.1d above).

**Note:** This approach requires a working DigiByte build environment with all libraries. Approach 1 (simple method) is easier and recommended.

---

## Summary: Which Approach to Use?

**✅ Use Approach 1 (Simple Method)** if:
- You want the easiest method
- You're comfortable temporarily modifying chainparams.cpp
- You want to mine genesis during node startup

**Use Approach 2 (Advanced Tool)** if:
- You want to mine genesis externally
- You have a complete build environment
- You want to mine the genesis before making code changes

**For most users: Use Approach 1!**

---

### Quick Reference: Genesis Block Values

After mining, you'll have 4 critical values:

| Value | Example | Where It Goes |
|-------|---------|---------------|
| **nTime** | `1732400000` | `CreateGenesisBlock([HERE], ...)` |
| **nNonce** | `1829473` | `CreateGenesisBlock(..., [HERE], ...)` |
| **Hash** | `0xa1b2c3d4...` | `assert(consensus.hashGenesisBlock == uint256S("[HERE]"))` |
| **Merkle** | `0x8f9e3d2c...` | `assert(genesis.hashMerkleRoot == uint256S("[HERE]"))` |

**Complete Example:**
```cpp
// In CTestNetParams constructor (around line 458):
genesis = CreateGenesisBlock(1732400000, 1829473, 0x1e0ffff0, 1, 8000);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256S("0xa1b2c3d4e5f6..."));
assert(genesis.hashMerkleRoot == uint256S("0x8f9e3d2c1a0b..."));
```

**Remember:**
- ✅ nTime and nNonce are DECIMAL numbers (no 0x prefix)
- ✅ Hash and Merkle are HEX strings (with 0x prefix in quotes)
- ✅ All values must match EXACTLY or the node won't start

---

#### Step 2.2: Update Activation Heights (CRITICAL!)

**NOTE:** If you used Approach 1, you already did this in Step 2.1d. This section is for reference and to update the activation heights.

**File:** `src/kernel/chainparams.cpp`

**File:** `/home/jared/Code/digibyte/src/kernel/chainparams.cpp`

**Location:** Lines 458-461 (Testnet genesis block)

**Replace:**
```cpp
// OLD TESTNET GENESIS (2018)
genesis = CreateGenesisBlock(1516939474, 2411473, 0x1e0ffff0, 1, 8000);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256S("0x308ea0711d5763be2995670dd9ca9872753561285a84da1d58be58acaa822252"));
assert(genesis.hashMerkleRoot == uint256S("0x72ddd9496b004221ed0557358846d9248ecd4c440ebd28ed901efc18757d0fad"));
```

**With:** (use YOUR values from Step 2.1)
```cpp
// NEW TESTNET GENESIS (2025) - DigiDollar Phase One
// Genesis timestamp message: "DigiDollar: A Fully Decentralized USD Stablecoin on The DigiByte Blockchain"
genesis = CreateGenesisBlock([NEW_TIMESTAMP], [NEW_NONCE], 0x1e0ffff0, 1, 8000);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256S("0x[YOUR_NEW_GENESIS_HASH_HERE]"));
assert(genesis.hashMerkleRoot == uint256S("0x72ddd9496b004221ed0557358846d9248ecd4c440ebd28ed901efc18757d0fad"));
```

#### Step 2.3: Clear Old Checkpoints

**File:** `/home/jared/Code/digibyte/src/kernel/chainparams.cpp`

**Location:** Lines 486-490 (Testnet checkpoints)

**Replace:**
```cpp
// OLD CHECKPOINT
checkpointData = {
    {
        {546, uint256S("0x08fa50178f4b4f9fe1bbaed3b0a2ee58d1c51cc8185f70c8089e4b95763d9cdb")},
    }
};
```

**With:**
```cpp
// NO CHECKPOINTS - Fresh testnet reset (2025)
// Only genesis block validated by hash
checkpointData = {
    {
        // Empty - no historical checkpoints for fresh testnet
    }
};
```

#### Step 2.4: Update Network Magic Bytes (MOST CRITICAL!)

**File:** `/home/jared/Code/digibyte/src/kernel/chainparams.cpp`

**Location:** Lines 449-452 (Testnet magic bytes)

**CRITICAL**: Magic bytes prevent cross-network communication at the protocol level!

**Replace:**
```cpp
// OLD TESTNET MAGIC BYTES (2018)
pchMessageStart[0] = 0xfd;
pchMessageStart[1] = 0xc8;
pchMessageStart[2] = 0xbd;
pchMessageStart[3] = 0xdd;
```

**With:** (example - choose unique values)
```cpp
// NEW TESTNET MAGIC BYTES (2025) - DigiDollar Reset
// IMPORTANT: Must be unique and different from all existing networks:
// Mainnet:  0xfa 0xc3 0xb6 0xda
// Old Test: 0xfd 0xc8 0xbd 0xdd (RETIRED)
// Regtest:  0xfa 0xbf 0xb5 0xda
pchMessageStart[0] = 0xfc;  // Different from all networks
pchMessageStart[1] = 0xd1;
pchMessageStart[2] = 0xb8;
pchMessageStart[3] = 0xe2;
```

**Why This Is THE MOST CRITICAL Change:**
- **Protocol-level isolation** - old clients physically cannot communicate with new network
- Even with same ports, incompatible magic bytes = rejected messages
- This is the PRIMARY mechanism for network separation
- Without this change, old clients WILL interfere even with different ports

**Recommended Values:**
- Start with 0xfc (distinct from mainnet 0xfa, old testnet 0xfd, regtest 0xfa)
- Avoid patterns similar to mainnet/regtest
- Choose values that won't be valid UTF-8 (prevents accidental string collisions)

#### Step 2.5: Update Network Ports

**File:** `/home/jared/Code/digibyte/src/chainparamsbase.cpp`

**Location:** Lines 40-53 (Network port definitions)

**Replace:**
```cpp
case ChainType::TESTNET:
    return ChainTypeParams{
        .base58_prefix = "testnet4",
        .p2p_port = 12026,
        .rpc_port = 14023,
        .onion_service_target_port = 14123,
    };
```

**With:**
```cpp
case ChainType::TESTNET:
    return ChainTypeParams{
        .base58_prefix = "testnet4",
        .p2p_port = 12028,  // CHANGED from 12026 - prevents P2P interference
        .rpc_port = 14024,  // CHANGED from 14023 - prevents RPC confusion
        .onion_service_target_port = 14124,  // CHANGED from 14123 - keep consistent
    };
```

**Why Change Ports Too:**
- **Defense in depth** - magic bytes + ports = maximum isolation
- Prevents accidental connections before protocol negotiation
- Makes old testnet clearly distinct from new testnet
- Allows old testnet to coexist briefly during migration

#### Step 2.6: Update Activation Heights (Match Regtest)

**File:** `/home/jared/Code/digibyte/src/kernel/chainparams.cpp`

**BIP Activation Heights** (Lines 373-444):

**Replace:**
```cpp
// OLD TESTNET VALUES
consensus.BIP34Height = 500;
consensus.BIP65Height = 1351;
consensus.BIP66Height = 1251;
consensus.CSVHeight = 1;
consensus.SegwitHeight = 0;
```

**With:**
```cpp
// NEW TESTNET VALUES (Match Regtest for instant activation)
consensus.BIP34Height = 1;        // Was 500
consensus.BIP65Height = 1;        // Was 1351
consensus.BIP66Height = 1;        // Was 1251
consensus.CSVHeight = 1;          // Already 1 (no change)
consensus.SegwitHeight = 0;       // Already 0 (no change)
```

**DigiByte Hard Fork Heights** (Lines 422-426):

**CRITICAL**: These must EXACTLY match regtest values (not all set to 1!)

**Replace:**
```cpp
// OLD TESTNET VALUES
consensus.multiAlgoDiffChangeTarget = 100;
consensus.alwaysUpdateDiffChangeTarget = 400;
consensus.workComputationChangeTarget = 1430;
consensus.algoSwapChangeTarget = 20000;
consensus.OdoHeight = 600;
```

**With:**
```cpp
// NEW TESTNET VALUES (EXACTLY match regtest from lines 815-818)
consensus.multiAlgoDiffChangeTarget = 100;     // Match regtest (line 815) - NO CHANGE
consensus.alwaysUpdateDiffChangeTarget = 200;  // Match regtest (line 816) - WAS 400
consensus.workComputationChangeTarget = 400;   // Match regtest (line 817) - WAS 1430
consensus.algoSwapChangeTarget = 600;          // Match regtest (line 818) - WAS 20000
consensus.OdoHeight = 600;                     // Match regtest (line 766) - NO CHANGE
```

**Note**: These are NOT all set to 1! They match regtest's specific staggered pattern: 100/200/400/600.

**DigiDollar Activation** (Lines 516):

**Replace:**
```cpp
consensus.nDDActivationHeight = 1000;  // OLD
```

**With:**
```cpp
consensus.nDDActivationHeight = 650;  // Match regtest - after Odocrypt at 600
```

**Oracle Activation** (Lines 516):

**Replace:**
```cpp
consensus.nOracleActivationHeight = 1000000;  // OLD - would take months to reach
```

**With:**
```cpp
consensus.nOracleActivationHeight = 650;  // Match regtest - same as DigiDollar
```

**Summary of Changes (EXACTLY Matching Regtest):**
```cpp
// Match regtest activation heights EXACTLY (from src/kernel/chainparams.cpp:759-954)
consensus.BIP34Height = 1;                     // Match regtest (line 759)
consensus.BIP65Height = 1;                     // Match regtest (line 761)
consensus.BIP66Height = 1;                     // Match regtest (line 762)
consensus.CSVHeight = 1;                       // Match regtest (line 763)
consensus.SegwitHeight = 0;                    // Match regtest (line 764)
consensus.multiAlgoDiffChangeTarget = 100;     // Match regtest (line 815)
consensus.alwaysUpdateDiffChangeTarget = 200;  // Match regtest (line 816)
consensus.workComputationChangeTarget = 400;   // Match regtest (line 817)
consensus.algoSwapChangeTarget = 600;          // Match regtest (line 818)
consensus.OdoHeight = 600;                     // Match regtest (line 766)
consensus.nOracleActivationHeight = 650;       // Match regtest Oracle+DigiDollar together!
consensus.nDDActivationHeight = 650;           // Match regtest (line 951) - WITH Oracle!
```

**Key Point**: Oracle AND DigiDollar both activate at 650 (after Odocrypt stabilizes at 600). This allows DigiDollar to have price feed data immediately upon activation!

#### Step 2.7: Update DigiDollar BIP9 Deployment Parameters

**File:** `/home/jared/Code/digibyte/src/kernel/chainparams.cpp`

**Location:** Lines 434-438 (DEPLOYMENT_DIGIDOLLAR)

**Replace:**
```cpp
// OLD BIP9 deployment (time-based)
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = 1704067200;  // Jan 1, 2024
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = 1735689600;    // Jan 1, 2025
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 0;
```

**With:**
```cpp
// NEW - Always active for fresh testnet (DigiDollar Phase One testing)
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 650;  // Match nDDActivationHeight
```

---

### Phase 3: Build and Test (Days 3-4)

#### Step 3.1: Compile Updated Code

```bash
cd /home/jared/Code/digibyte

# Clean previous build
make clean

# Regenerate build system
./autogen.sh

# Configure with desired options
./configure \
    --prefix=/usr/local \
    --enable-wallet \
    --with-gui=qt5 \
    --enable-debug

# Build (use -j for parallel compilation)
make -j$(nproc)

# Run unit tests
make check

# Run DigiDollar-specific tests
./src/test/test_digibyte --run_test=digidollar_*
./src/test/test_digibyte --run_test=oracle_*
```

**Expected Output:**
```
Running 685 DigiDollar unit tests...
... all passing

Running 123 Oracle unit tests...
... all passing

Build completed successfully.
```

#### Step 3.2: Test with Regtest (Local Validation)

Before deploying to testnet, validate the changes with regtest:

```bash
# Start regtest node
./src/digibyted -regtest -daemon

# Generate blocks to reach DigiDollar activation
./src/digibyte-cli -regtest generatetoaddress 100 [YOUR_ADDRESS]

# Verify DigiDollar is active
./src/digibyte-cli -regtest getdigidollarstats

# Expected output:
# {
#   "activation_height": 100,
#   "current_height": 100,
#   "digidollar_active": true,
#   "oracle_active": true,
#   ...
# }

# Test minting
./src/digibyte-cli -regtest mintdigidollar 10.00 1  # Mint $10 DD, 1-year lock

# Verify mint worked
./src/digibyte-cli -regtest getdigidollarbalance

# Stop regtest node
./src/digibyte-cli -regtest stop
```

#### Step 3.3: Functional Test Suite

```bash
cd /home/jared/Code/digibyte

# Run all functional tests
./test/functional/test_runner.py

# Run DigiDollar-specific functional tests
./test/functional/digidollar_mint.py
./test/functional/digidollar_transfer.py
./test/functional/digidollar_oracle.py
./test/functional/digidollar_network_tracking.py

# All tests should pass
# Expected: [X/X tests passed] (0 failed, 0 skipped)
```

---

### Phase 4: Testnet Deployment (Day 5 - Reset Day)

#### Step 4.1: Pre-Reset Coordination (T-1 Hour)

**1. Final Communication:**
```
🚨 DigiByte Testnet Reset in 1 Hour 🚨

⏰ Time: [EXACT UTC TIME]
📦 Version: v9.26.0

ACTION REQUIRED:
1. Stop your testnet node: digibyte-cli -testnet stop
2. Delete testnet data: rm -rf ~/.digibyte/testnet4/
3. Wait for "GO" signal
4. Start fresh node: digibyted -testnet

New Genesis Hash: 0x[YOUR_NEW_GENESIS_HASH]

Stand by for launch confirmation...
```

**2. DNS Seed Operators Standby:**
- Confirm all 5 operators are ready
- Verify they've cleared old testnet databases
- Provide new genesis hash for validation

**3. Core Team Standby:**
- Lead developer ready to start first node
- Backup developers ready with nodes
- Monitoring infrastructure prepared

#### Step 4.2: Reset Execution (T=0)

**1. Lead Developer Starts First Node:**
```bash
# Clear any existing testnet data
rm -rf ~/.digibyte/testnet4/

# Start fresh testnet node
./src/digibyted -testnet -daemon -debug=digidollar -debug=net

# Monitor log
tail -f ~/.digibyte/testnet4/debug.log
```

**Expected Log Output:**
```
2025-11-23 12:00:00 DigiByte version v9.26.0
2025-11-23 12:00:00 Using config file /home/user/.digibyte/digibyte.conf
2025-11-23 12:00:00 Using data directory /home/user/.digibyte/testnet4
2025-11-23 12:00:00 Using at most 125 automatic connections (1024 file descriptors available)
2025-11-23 12:00:00 InitParameterInteraction: testnet mode enabled
2025-11-23 12:00:00 Binding RPC on address ::1 port 14024
2025-11-23 12:00:00 Binding RPC on address 127.0.0.1 port 14024
2025-11-23 12:00:00 Initialization: Verifying wallet(s)...
2025-11-23 12:00:00 Using BerkeleyDB version Berkeley DB 5.3.28: (September  9, 2013)
2025-11-23 12:00:00 Using wallet /home/user/.digibyte/testnet4/wallets/wallet.dat
2025-11-23 12:00:00 init message: Loading block index...
2025-11-23 12:00:01 Opening LevelDB in /home/user/.digibyte/testnet4/blocks/index
2025-11-23 12:00:01 Opened LevelDB successfully
2025-11-23 12:00:01 Using obfuscation key for /home/user/.digibyte/testnet4/blocks/index: 0000000000000000
2025-11-23 12:00:01 LoadBlockIndexDB: last block file = 0
2025-11-23 12:00:01 LoadBlockIndexDB: transaction index disabled
2025-11-23 12:00:01 LoadBlockIndexDB: hashBestChain=0x[NEW_GENESIS_HASH] height=0 date=2025-11-23 12:00:00 progress=1.000000
2025-11-23 12:00:01 init message: Verifying blocks...
2025-11-23 12:00:01 Verifying last 6 blocks at level 3
2025-11-23 12:00:01 [0%]...[100%] DONE
2025-11-23 12:00:01 No coin database inconsistencies in last 1 blocks (1 transactions)
2025-11-23 12:00:01 init message: Loading wallet...
2025-11-23 12:00:01 BerkeleyEnvironment::Open: LogDir=/home/user/.digibyte/testnet4/wallets/database ErrorFile=/home/user/.digibyte/testnet4/wallets/db.log
2025-11-23 12:00:01 [default wallet] Wallet File Version = 10500
2025-11-23 12:00:01 [default wallet] Keys: 1001 plaintext, 0 encrypted, 1001 w/ metadata, 1001 total. Unknown wallet records: 0
2025-11-23 12:00:01 [default wallet] Wallet completed loading in 8ms
2025-11-23 12:00:01 [default wallet] setKeyPool.size() = 1000
2025-11-23 12:00:01 [default wallet] mapWallet.size() = 0
2025-11-23 12:00:01 [default wallet] m_address_book.size() = 1
2025-11-23 12:00:01 init message: Importing blocks...
2025-11-23 12:00:01 Bound to [::]:12028
2025-11-23 12:00:01 Bound to 0.0.0.0:12028
2025-11-23 12:00:01 init message: Starting network threads...
2025-11-23 12:00:01 init message: Done loading
2025-11-23 12:00:01 msghand thread start
2025-11-23 12:00:01 addcon thread start
2025-11-23 12:00:01 net thread start
2025-11-23 12:00:01 opencon thread start
2025-11-23 12:00:01 dnsseed thread start
```

**2. Verify Genesis Block:**
```bash
./src/digibyte-cli -testnet getblockchaininfo
```

**Expected Output:**
```json
{
  "chain": "test",
  "blocks": 0,
  "headers": 0,
  "bestblockhash": "0x[YOUR_NEW_GENESIS_HASH]",
  "difficulty": 0.0002441371325370145,
  "chainwork": "0000000000000000000000000000000000000000000000000000000000100010",
  "initialblockdownload": false,
  "verificationprogress": 1
}
```

**3. Core Team Joins Network:**

Now other core developers start their nodes:
```bash
# Developer 2
rm -rf ~/.digibyte/testnet4/
./src/digibyted -testnet -daemon -addnode=[LEAD_DEV_IP]:12028

# Developer 3
rm -rf ~/.digibyte/testnet4/
./src/digibyted -testnet -daemon -addnode=[LEAD_DEV_IP]:12028
```

**4. DNS Seeds Activated:**

DNS seed operators start their seeders:
```bash
# Example for bitcoin-seeder (adjust for your seeder)
./dnsseed -h testnetseed.example.com -n testnet.example.com -m youremail@example.com
```

#### Step 4.3: Genesis Block Announcement (T+5 Minutes)

**Post to all communication channels:**

```markdown
✅ DigiByte Testnet Reset COMPLETE

🎉 Genesis Block Mined!
Hash: 0x[NEW_GENESIS_HASH]
Time: [TIMESTAMP]
Height: 0

📡 Network Status: LIVE
- 5 core nodes online
- DNS seeds activated
- **P2P port: 12028** (NEW - prevents old client interference)
- **RPC port: 14024** (NEW - prevents old client interference)

🚀 DigiDollar Testing ENABLED (Regtest-Matching Heights)
- BIPs activation: Block 1
- Odocrypt activation: Block 600
- Oracle activation: Block 650 (with DigiDollar!)
- DigiDollar activation: Block 650 (with Oracle!)
- Genesis message: DigiDollar whitepaper headline
- **New magic bytes**: Network isolated from old testnet

💻 How to Join:
1. Download v9.26: [LINK]
2. Delete old testnet: rm -rf ~/.digibyte/testnet4/
3. Start node: digibyted -testnet

📖 Full Guide: [LINK_TO_THIS_DOCUMENT]

Need help? Ask in #development on Discord.
```

---

### Phase 5: Mining to Activation (Day 5 - Hours 0-1)

#### Step 5.1: Initial Block Mining

**Goal**: Mine blocks to activate features in sequence:
- Block 1: All BIPs active (instant!)
- Block 600: Odocrypt active
- Block 650: Oracle + DigiDollar active together (~2.7 hours at 15s/block)

**Lead Developer Mines First Blocks:**
```bash
# Generate mining address
MINING_ADDRESS=$(./src/digibyte-cli -testnet getnewaddress "mining" "legacy")

# Mine first 10 blocks (get chain started)
./src/digibyte-cli -testnet generatetoaddress 10 $MINING_ADDRESS

# Check status
./src/digibyte-cli -testnet getblockchaininfo
```

**Expected:**
```json
{
  "chain": "test",
  "blocks": 10,
  "bestblockhash": "0x[BLOCK_10_HASH]",
  ...
}
```

**Distribute Mining to Community:**

Once 10-20 blocks mined, encourage community to mine:
```markdown
📢 Community Mining Open!

Current height: [CURRENT_HEIGHT]
Milestones:
- ✅ Block 1: All BIPs (ACTIVE)
- ⏱️ Block 600: Odocrypt activation
- ⏱️ Block 650: Oracle + DigiDollar activation (together!)

Mine testnet DGB:
1. Get address: digibyte-cli -testnet getnewaddress
2. Mine blocks: digibyte-cli -testnet generatetoaddress 10 [YOUR_ADDRESS]
3. Check progress: digibyte-cli -testnet getblockcount

Let's reach block 650 together! 🚀
```

#### Step 5.2: BIP Activation (Block 1)

**Monitor current height:**
```bash
# Watch block count
watch -n 1 './src/digibyte-cli -testnet getblockcount'
```

**After Block 1 Reached:**
```bash
# Verify Oracle and BIP activation
./src/digibyte-cli -testnet getdigidollarstats
```

**Expected Output:**
```json
{
  "activation_height": 650,
  "oracle_activation_height": 650,
  "current_height": 1,
  "oracle_active": false,
  "digidollar_active": false,
  "oracle_price_dgb_usd": 0.00,
  ...
}
```

**Announcement:**
```markdown
🎊 ALL BIPs ACTIVATED - Block 1

✅ BIP34/65/66/CSV: ACTIVE
⏱️ Odocrypt: Activates at block 600 (~2.5 hours)
⏱️ Oracle + DigiDollar: Activate together at block 650 (~2.7 hours)

Keep mining to reach block 650 for DigiDollar + Oracle! 🚀
```

#### Step 5.3: Odocrypt Activation (Block 600)

**Monitor for block 600:**
```bash
watch -n 1 './src/digibyte-cli -testnet getblockcount'
```

**When Block 600 Reached:**
```markdown
🔷 ODOCRYPT ACTIVATED - Block 600

7th mining algorithm now active!
- Next milestone: Block 650 (DigiDollar activation)
- ~12 minutes away (50 blocks × 15s)
```

#### Step 5.4: DigiDollar Activation (Block 650)

**Monitor for block 650:**
```bash
watch -n 1 './src/digibyte-cli -testnet getblockcount'
```

**When Block 650 Reached:**
```bash
# Verify DigiDollar activation
./src/digibyte-cli -testnet getdigidollarstats
```

**Expected Output:**
```json
{
  "activation_height": 650,
  "oracle_activation_height": 650,
  "current_height": 650,
  "oracle_active": true,
  "digidollar_active": true,
  "oracle_price_dgb_usd": 0.05,
  "total_dd_supply": 0,
  "total_collateral": 0,
  "system_health": 0,
  ...
}
```

**Major Announcement:**
```markdown
🚀🚀🚀 DIGIDOLLAR ACTIVATED - Block 650 🚀🚀🚀

DigiDollar Phase One is LIVE on testnet!
Genesis: "DigiDollar: A Fully Decentralized USD Stablecoin on The DigiByte Blockchain"

✅ Oracle System: Active
✅ Minting: Enabled
✅ Sending: Enabled
✅ Receiving: Enabled
✅ Redemption: Enabled

📊 Current Stats:
- DGB/USD: $[PRICE]
- Block height: 650
- DD Supply: 0
- Network health: Initializing
- Activation height: 650 (matches regtest pattern)

💡 Start Testing:
1. Mint DD: digibyte-cli -testnet mintdigidollar 10.00 1
2. Check balance: digibyte-cli -testnet getdigidollarbalance
3. Send DD: digibyte-cli -testnet senddigidollar [DD_ADDRESS] 5.00
4. View vaults: digibyte-cli -testnet listdigidollarpositions

📖 Full guide: [LINK]

This is a historic moment for DigiByte! 🎉
Let's test DigiDollar together! 💪
```

---

### Phase 6: Community Onboarding (Days 6-7)

#### Step 6.1: Create Beginner's Testing Guide

**Post comprehensive testing guide:**

```markdown
# DigiDollar Testnet Testing Guide

## Getting Started

### 1. Install DigiByte v9.26

**Option A: Binary Release**
Download from: [releases page]
```bash
# Linux
wget [link]
tar -xzf digibyte-9.26.0-x86_64-linux-gnu.tar.gz
cd digibyte-9.26.0/bin
```

**Option B: Build from Source**
```bash
git clone https://github.com/DigiByte-Core/digibyte.git
cd digibyte
git checkout v9.26.0
./autogen.sh
./configure
make -j$(nproc)
```

### 2. Start Testnet Node

```bash
# Start daemon
./digibyted -testnet -daemon

# Or with GUI
./digibyte-qt -testnet
```

### 3. Get Testnet DGB

**Option A: Mine Locally**
```bash
# Generate address
ADDR=$(./digibyte-cli -testnet getnewaddress)

# Mine 10 blocks
./digibyte-cli -testnet generatetoaddress 10 $ADDR

# Check balance (need 100 confirmations = 8 blocks)
./digibyte-cli -testnet getbalance
```

**Option B: Testnet Faucet**
[Link to testnet faucet if available]

### 4. Test DigiDollar Minting

```bash
# Check system status
./digibyte-cli -testnet getdigidollarstats

# Mint $100 DigiDollar with 1-year lock (300% collateral)
./digibyte-cli -testnet mintdigidollar 100.00 3

# Check DD balance
./digibyte-cli -testnet getdigidollarbalance

# View your vault
./digibyte-cli -testnet listdigidollarpositions
```

### 5. Test DigiDollar Transfers

```bash
# Generate DD receiving address
DD_ADDR=$(./digibyte-cli -testnet getdigidollaraddress)

# Send DD to another user (or yourself for testing)
./digibyte-cli -testnet senddigidollar $DD_ADDR 10.00

# Check transaction
./digibyte-cli -testnet listdigidollartxs
```

### 6. Test Redemption (After Time Lock)

```bash
# List vaults (check time remaining)
./digibyte-cli -testnet listdigidollarpositions

# Redeem when timelock expired (replace with your position ID)
./digibyte-cli -testnet redeemdigidollar [POSITION_ID]

# Check DGB returned to wallet
./digibyte-cli -testnet getbalance
```

## What to Test

**Priority 1: Core Functionality**
- [ ] Mint DigiDollars (various amounts and lock periods)
- [ ] Send DigiDollars to DD addresses
- [ ] Receive DigiDollars
- [ ] Check DD balances
- [ ] View vault positions

**Priority 2: Oracle System**
- [ ] Verify oracle price updates (every ~2 blocks)
- [ ] Check oracle system status
- [ ] Monitor price fluctuations

**Priority 3: Edge Cases**
- [ ] Mint with minimum amount ($1)
- [ ] Mint with maximum amount ($10,000)
- [ ] Try minting with insufficient DGB collateral
- [ ] Transfer partial DD amounts
- [ ] Check system health calculations

**Priority 4: GUI Testing**
- [ ] Mint via DigiDollar tab in Qt GUI
- [ ] Send DD via GUI
- [ ] View positions in GUI
- [ ] Check network-wide stats in GUI

## Reporting Issues

Found a bug? Report it!

**GitHub Issues:** [link]
**Discord:** #digidollar-testing

Include:
- What you were doing
- Expected vs actual behavior
- Error messages (if any)
- Your node's debug.log (relevant section)

## Network Stats Dashboard

Track testnet progress:
- Current height: [LINK_TO_EXPLORER]
- Total DD supply: [LINK]
- System health: [LINK]
- Active vaults: [LINK]

Thank you for helping test DigiDollar! 🙏
```

#### Step 6.2: Create Video Walkthrough

**Record and post video tutorial covering:**
1. Installing v9.26
2. Starting testnet node
3. Mining testnet DGB
4. Minting first DigiDollars
5. Sending DD between addresses
6. Using the Qt GUI

**Post on:**
- YouTube
- DigiByte website
- Discord (pinned in #digidollar-testing)

#### Step 6.3: Host Community Testing Session

**Schedule live session:**
- Date: 1 week after reset
- Platform: Discord voice/screen share
- Agenda:
  1. Q&A about testnet reset
  2. Live DigiDollar testing walkthrough
  3. Bug reporting session
  4. Feature feedback discussion

---

### Phase 7: Ongoing Monitoring (Weeks 2-4)

#### Step 7.1: Network Health Monitoring

**Set up automated monitoring:**

```bash
# Create monitoring script
cat > ~/digibyte-testnet-monitor.sh << 'EOF'
#!/bin/bash

# Monitor testnet health
echo "=== DigiByte Testnet Monitor ==="
echo "Time: $(date)"
echo ""

# Basic stats
echo "Blockchain Info:"
digibyte-cli -testnet getblockchaininfo | jq '{blocks, headers, difficulty, chainwork}'
echo ""

# DigiDollar stats
echo "DigiDollar Stats:"
digibyte-cli -testnet getdigidollarstats | jq '{current_height, oracle_active, digidollar_active, oracle_price_dgb_usd, total_dd_supply, total_collateral, system_health}'
echo ""

# Network connections
echo "Network Connections:"
digibyte-cli -testnet getnetworkinfo | jq '{connections, networks}'
echo ""

# Mempool
echo "Mempool:"
digibyte-cli -testnet getmempoolinfo
echo ""

EOF

chmod +x ~/digibyte-testnet-monitor.sh

# Run every 5 minutes
(crontab -l 2>/dev/null; echo "*/5 * * * * ~/digibyte-testnet-monitor.sh >> ~/testnet-monitor.log 2>&1") | crontab -
```

**Monitor for:**
- Block production rate (should be ~15 seconds avg)
- Oracle price updates (every ~2 blocks)
- DigiDollar mints/burns
- Network peer count
- Mempool size

#### Step 7.2: Community Feedback Collection

**Create feedback form:**
- Google Form / Typeform
- Questions:
  - What did you test?
  - Did everything work as expected?
  - What issues did you encounter?
  - What could be improved?
  - Rate experience (1-5)
  - Additional comments

**Post weekly summaries:**
```markdown
## Testnet Week 1 Summary

### Stats
- Total blocks: [NUMBER]
- DigiDollars minted: $[AMOUNT]
- Active vaults: [COUNT]
- Unique testers: [COUNT]
- Transactions: [COUNT]

### Highlights
- ✅ [Achievement 1]
- ✅ [Achievement 2]
- 🐛 [Bug found and fixed]
- 💡 [Feature suggestion]

### Issues Found
- [Issue 1] - Status: Fixed
- [Issue 2] - Status: In progress
- [Issue 3] - Status: Investigating

### Next Week Focus
- [ ] [Testing goal 1]
- [ ] [Testing goal 2]
- [ ] [Feature to test]

Thank you to all testers! 🙏
```

#### Step 7.3: Bug Triage and Fixes

**Bug Priority Levels:**

| Priority | Description | Response Time |
|----------|-------------|---------------|
| **P0 - Critical** | Blockchain halted, data loss, security | Immediate (< 4 hours) |
| **P1 - High** | Core feature broken, crashes | 1-2 days |
| **P2 - Medium** | Minor feature issues, UI bugs | 1 week |
| **P3 - Low** | Cosmetic, enhancements | Future release |

**Bug Fix Process:**
1. Issue reported on GitHub
2. Triage and assign priority
3. Assign developer
4. Fix and test on regtest
5. Deploy to testnet (if urgent)
6. Update documentation

---

## 6. Configuration Changes Required

### Summary of All Changes

**File: `/home/jared/Code/digibyte/src/kernel/chainparams.cpp`**

```cpp
// Line 458-461: New genesis block
genesis = CreateGenesisBlock([NEW_TIMESTAMP], [NEW_NONCE], 0x1e0ffff0, 1, 8000);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256S("0x[NEW_GENESIS_HASH]"));
assert(genesis.hashMerkleRoot == uint256S("0x72ddd9496b004221ed0557358846d9248ecd4c440ebd28ed901efc18757d0fad"));

// Line 486-490: Clear checkpoints
checkpointData = {
    {
        // Empty - fresh testnet
    }
};

// Line 516: DigiDollar activation height
consensus.nDDActivationHeight = 650;  // Was 1000

// Line 516: Oracle activation height
consensus.nOracleActivationHeight = 650;  // Was 1000000 - matches DigiDollar!

// Line 434-438: DigiDollar BIP9 deployment
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 650;
```

### Configuration File Template

**File: `~/.digibyte/digibyte.conf`**

Recommended settings for testnet validators:

```conf
# DigiByte Testnet Configuration (v9.26)

# Network
testnet=1
listen=1
server=1
port=12028

# Connections
maxconnections=125
addnode=testnetseed.diginode.tools
addnode=testseed.digibyteblockchain.org

# RPC
rpcuser=[YOUR_USERNAME]
rpcpassword=[YOUR_SECURE_PASSWORD]
rpcallowip=127.0.0.1
rpcport=14024

# DigiDollar Specific
debug=digidollar
debug=oracle

# Mining (optional)
gen=0

# Txindex (optional - useful for exploration)
txindex=1
```

---

## 7. Community Coordination

### Key Stakeholders

**1. Core Development Team**
- Lead Developer: Coordinate reset
- Protocol Engineers: Review consensus changes
- QA Team: Test before and after reset

**2. DNS Seed Operators** (5 total)
- Clear old seed databases
- Start seeders with new genesis
- Monitor seed health

**3. Block Explorers**
- Reset blockchain databases
- Update to v9.26
- Display DigiDollar transactions

**4. Pool Operators** (if any testnet pools exist)
- Update mining software
- Clear old block data
- Point miners to fresh chain

**5. Community Validators**
- Delete old testnet data
- Download v9.26
- Start testing DigiDollar

### Communication Timeline

| Phase | When | Channels | Message |
|-------|------|----------|---------|
| **Announcement** | T-2 weeks | All | Testnet reset scheduled |
| **Reminder** | T-1 week | All | Reminder + preparation instructions |
| **Final Notice** | T-24 hours | All | Final countdown, exact time |
| **1-Hour Warning** | T-1 hour | All | Stop nodes, clear data |
| **Reset** | T=0 | All | Genesis mined, network live |
| **Odocrypt** | T+2.5 hours | All | Odocrypt activated (block 600) |
| **Oracle + DigiDollar** | T+2.7 hours | All | Oracle + DigiDollar activated (block 650) |
| **Summary** | T+24 hours | All | Day 1 summary, stats |

### Communication Channels

1. **GitHub Discussions**
   - https://github.com/DigiByte-Core/digibyte/discussions
   - Category: DigiDollar
   - Pin announcement

2. **Discord**
   - Server: DigiByte Official
   - Channels: #development, #digidollar-testing, #announcements
   - Role: @TestnetValidator (create new role)

3. **Reddit**
   - r/DigiByte
   - Sticky post
   - Mod announcements

4. **Twitter/X**
   - @DigiByteCoin official account
   - Hashtag: #DigiDollarTestnet
   - Thread with updates

5. **Telegram**
   - DigiByte Official group
   - Pin message
   - Updates as replies

6. **Email Newsletter**
   - DigiByte developer mailing list
   - Include in monthly update

### Testnet Validator Program

**Create incentive program for active testers:**

**Roles:**
- **Validator**: Run testnet node
- **Tester**: Actively test DigiDollar features
- **Reporter**: Submit bug reports
- **Documenter**: Create guides/tutorials

**Recognition:**
- Discord role: @DigiDollarTestnet
- Mention in release notes
- Community appreciation

**No monetary rewards** (testnet DGB has no value), but:
- Early access to features
- Direct communication with devs
- Influence on feature development
- Recognition in community

---

## 8. Testing and Validation

### Pre-Reset Validation Checklist

**Before deploying testnet reset, verify:**

- [ ] Genesis block mined successfully
- [ ] All unit tests passing (808 total)
- [ ] All functional tests passing (19 total)
- [ ] Regtest validation successful
- [ ] DigiDollar activates at height 650
- [ ] Oracle activates at height 650 (same as DigiDollar)
- [ ] All 5 DNS seed operators confirmed ready
- [ ] Release notes drafted
- [ ] Communication plan executed
- [ ] Binaries compiled for all platforms (Linux, macOS, Windows)
- [ ] Backup developers standing by

### Post-Reset Validation Checklist

**After reset, verify within first hour:**

- [ ] Genesis block propagating
- [ ] Multiple nodes connected
- [ ] DNS seeds resolving
- [ ] Block mining functioning
- [ ] Block time averaging ~15 seconds
- [ ] Network reaching 10+ nodes
- [ ] Mempool accepting transactions
- [ ] RPC commands responding

**After reaching block 1 (BIPs activate):**

- [ ] All BIPs active (BIP34/65/66/CSV)
- [ ] getdigidollarstats shows oracle_active: false, digidollar_active: false
- [ ] Waiting for Odocrypt (600) then Oracle+DigiDollar (650)

**After reaching block 600 (Odocrypt activates):**

- [ ] 7th mining algorithm (Odocrypt) active
- [ ] Block version includes Odocrypt
- [ ] Oracle and DD still waiting (activate together at 650)

**After reaching block 650 (DigiDollar activates):**

- [ ] DigiDollar system active
- [ ] getdigidollarstats shows oracle_active: true AND digidollar_active: true
- [ ] DigiDollar minting works
- [ ] DD addresses generating correctly (DD prefix)
- [ ] Transfers between DD addresses work
- [ ] Balance tracking accurate
- [ ] Vault positions recorded correctly
- [ ] Network-wide stats showing correct values

### Extended Testing Scenarios

**Week 1 Testing:**
- [ ] Mint multiple vaults (various lock periods)
- [ ] Send DD between 10+ different users
- [ ] Verify collateral calculations correct
- [ ] Test all RPC commands
- [ ] GUI functionality complete
- [ ] Network health calculations accurate
- [ ] Oracle price updates every ~2 blocks

**Week 2 Testing:**
- [ ] Wait for first time lock expirations
- [ ] Test redemption process
- [ ] Partial redemptions
- [ ] Emergency redemption paths
- [ ] DCA multiplier behavior
- [ ] ERR activation (if system undercollateralized)
- [ ] Stress test with 100+ vaults

**Week 3-4 Testing:**
- [ ] Long-running node stability
- [ ] Large transaction volumes
- [ ] Network reorg handling
- [ ] Wallet backup/restore with DD
- [ ] Multi-wallet with DD
- [ ] Blockchain pruning with DD data

---

## 9. Deployment and Migration

### Binary Release Process

**1. Tag Release:**
```bash
git tag -a v9.26.0 -m "DigiByte v9.26.0 - DigiDollar Phase One"
git push origin v9.26.0
```

**2. Build Binaries:**

**Linux:**
```bash
./autogen.sh
./configure --prefix=/usr/local
make -j$(nproc)
make deploy
# Creates: digibyte-9.26.0-x86_64-linux-gnu.tar.gz
```

**macOS:**
```bash
./autogen.sh
./configure --prefix=/usr/local
make -j$(nproc)
make deploy
# Creates: digibyte-9.26.0-osx64.tar.gz
```

**Windows (cross-compile from Linux):**
```bash
make -C depends HOST=x86_64-w64-mingw32
./autogen.sh
./configure --prefix=$(pwd)/depends/x86_64-w64-mingw32
make -j$(nproc)
make deploy
# Creates: digibyte-9.26.0-win64-setup.exe
```

**3. Sign Binaries:**
```bash
# GPG sign release files
gpg --detach-sign --armor digibyte-9.26.0-x86_64-linux-gnu.tar.gz
gpg --detach-sign --armor digibyte-9.26.0-osx64.tar.gz
gpg --detach-sign --armor digibyte-9.26.0-win64-setup.exe

# Create SHA256 checksums
sha256sum digibyte-9.26.0-* > SHA256SUMS.txt
gpg --clearsign SHA256SUMS.txt
```

**4. Upload to GitHub:**
- Create release on GitHub: v9.26.0
- Upload all binaries and signatures
- Include release notes
- Mark as "Pre-release" initially

### Installation Instructions for Users

**Linux:**
```bash
# Download
wget https://github.com/DigiByte-Core/digibyte/releases/download/v9.26.0/digibyte-9.26.0-x86_64-linux-gnu.tar.gz

# Verify signature (optional but recommended)
wget https://github.com/DigiByte-Core/digibyte/releases/download/v9.26.0/digibyte-9.26.0-x86_64-linux-gnu.tar.gz.asc
gpg --verify digibyte-9.26.0-x86_64-linux-gnu.tar.gz.asc

# Extract
tar -xzf digibyte-9.26.0-x86_64-linux-gnu.tar.gz

# Move to system path (optional)
sudo mv digibyte-9.26.0/bin/* /usr/local/bin/

# Delete old testnet data
rm -rf ~/.digibyte/testnet4/

# Start testnet
digibyted -testnet -daemon
```

**macOS:**
```bash
# Download
curl -LO https://github.com/DigiByte-Core/digibyte/releases/download/v9.26.0/digibyte-9.26.0-osx64.tar.gz

# Extract
tar -xzf digibyte-9.26.0-osx64.tar.gz

# Move to applications (optional)
mv DigiByte-Qt.app /Applications/

# Delete old testnet data
rm -rf ~/Library/Application\ Support/DigiByte/testnet4/

# Start testnet (GUI)
open /Applications/DigiByte-Qt.app --args -testnet

# Or command line
./digibyte-9.26.0/bin/digibyted -testnet -daemon
```

**Windows:**
```
1. Download digibyte-9.26.0-win64-setup.exe
2. Run installer
3. Delete old testnet data:
   - Press Win+R
   - Type: %APPDATA%\DigiByte
   - Delete testnet4 folder
4. Launch DigiByte Qt with testnet:
   - Right-click DigiByte shortcut
   - Properties → Target
   - Add " -testnet" to end
   - Click OK
5. Start DigiByte Qt
```

### Migration from Old Testnet

**There is NO migration** - this is a complete reset.

**Users must:**
1. **Backup anything important** (old testnet has no value, but for records)
2. **Delete all testnet data**
3. **Start fresh with new genesis**

**Data to delete:**
- Linux: `~/.digibyte/testnet4/`
- macOS: `~/Library/Application Support/DigiByte/testnet4/`
- Windows: `%APPDATA%\DigiByte\testnet4\`

**What gets deleted:**
- Old blockchain data (blocks/chainstate/)
- Old wallet.dat (testnet wallets, no real value)
- Old peers.dat (peer list)
- Old mempool.dat (pending transactions)

**What to backup** (optional):
- wallet.dat (if you want to keep testnet addresses for reference)
- debug.log (if you want to keep logs)
- digibyte.conf (your configuration file - this won't be deleted)

---

## 10. FAQ

### General Questions

**Q: Why reset testnet instead of using regtest or signet?**

A: Testnet provides a public, multi-user environment that simulates real-world mainnet behavior better than regtest (single-user, instant blocks) or signet (controlled blocks). We need:
- Multiple independent users testing simultaneously
- Real network latency and propagation
- Realistic block timing (15 seconds)
- Community participation and feedback
- Public visibility for transparency

**Q: Will this affect mainnet?**

A: **Absolutely not.** Testnet and mainnet are completely separate networks with different:
- Genesis blocks
- Network ports (new testnet: 12028, old testnet: 12026, mainnet: 12024)
- Magic bytes (testnet: 0xfdc8bddd, mainnet: 0xfac3b6da)
- Address prefixes (testnet: dgbt, mainnet: dgb)

There is zero risk of cross-network contamination.

**Q: Do I lose anything with the testnet reset?**

A: No real value is lost. Testnet DGB has **no monetary value** - it's only for testing. However, you will lose:
- Testnet transaction history (for records only)
- Testnet wallet balances (no real value)
- Old testnet addresses (can still be used on new testnet if you back up wallet.dat)

**Q: How long will this testnet run before another reset?**

A: This testnet is intended to run indefinitely. Future resets would only happen for:
- Major protocol changes requiring clean slate
- Critical security issues
- DigiDollar Phase Two transition (9-of-17 oracle consensus — RC30)

Expect this testnet to run at least 1-2 years.

**Q: Can I use my mainnet wallet on testnet?**

A: **NO, DO NOT DO THIS.** While technically possible, it's dangerous and unnecessary:
- Risk of accidental mainnet transaction
- Confusion between testnet/mainnet addresses
- Security best practice: separate wallets

Generate fresh testnet addresses.

### Technical Questions

**Q: Why do Oracle and DigiDollar both activate at height 650?**

A: To **EXACTLY match regtest** and ensure synchronized activation:
1. ✅ **BIPs at 1** - Basic Bitcoin improvements (SegWit, CSV, etc.)
2. ✅ **Odocrypt at 600** (line 766) - 7th mining algorithm must stabilize first
3. ✅ **Oracle + DigiDollar at 650** (both together!) - Ensures price feed is available when DigiDollar goes live
4. ✅ Identical activation pattern to regtest
5. ✅ Respects fork order (BIPs first, then Odocrypt, then Oracle+DigiDollar together)
6. ✅ DigiDollar needs oracle price feeds - they activate simultaneously
7. ✅ Consistent testing environment - regtest and testnet IDENTICAL
8. ✅ Both available at ~2.7 hours (650 blocks × 15s)

Activation sequence: BIPs (1) → Odocrypt (600) → Oracle+DigiDollar (650) together.

This is the **actual regtest pattern** verified from the code!

**Q: Why is the oracle system testnet-only in Phase One?**

A: Phase One uses a simplified 1-of-1 oracle for rapid development and testing:
- **Testnet**: 1 oracle, 1-of-1 consensus (Phase One)
- **Mainnet (future)**: 17 oracles, 9-of-17 consensus (Phase Two / Phase 3 MuSig2 — RC30)

The single-oracle model is not secure enough for mainnet but perfect for testnet validation.

**Q: What happens to the oracle system in Phase Two?**

A: Phase Two (future mainnet deployment) will use:
- **17 independent oracles** (vs 1 in Phase One, RC30)
- **9-of-17 threshold** for consensus (vs 1-of-1)
- **Schnorr signature verification** in compact format (vs trust-based)
- **Aggregated MuSig2 signature (v0x03)** — a single 64-byte aggregate sig for any N-of-M
- **Epoch-based oracle selection** (vs fixed single oracle)

Phase One validates the architecture; Phase Two adds production security.

**Q: How do I know if my node is on the correct testnet?**

A: Check your genesis block hash:

```bash
digibyte-cli -testnet getblockhash 0
```

Should return: `0x[NEW_GENESIS_HASH]` (the hash from Step 2.1)

If it returns the old hash (`0x308ea071...`), you're on the old testnet:
1. Stop node: `digibyte-cli -testnet stop`
2. Delete data: `rm -rf ~/.digibyte/testnet4/`
3. Restart: `digibyted -testnet -daemon`

**Q: Why does the merkle root stay the same across all networks?**

A: The merkle root is computed from the genesis coinbase transaction, which is identical across all networks:
- Same timestamp message (uses DigiByte default)
- Same output script (OP_0 OP_CHECKSIG)
- Same coinbase reward (8000 satoshis = 80 DGB)

Only the block header changes (nTime, nNonce, nBits), so only the block hash differs.

**Q: Can I mine testnet DGB with an ASIC?**

A: Testnet uses the same mining algorithms as mainnet (SHA256D, Scrypt, Groestl, Skein, Qubit), so yes, ASICs work. However:
- Testnet difficulty is very low
- CPU mining is sufficient
- Most miners don't point ASICs at testnet (no economic value)
- Use `generatetoaddress` RPC for quick testing

**Q: What are the testnet fee rates?**

A: Same as mainnet:
- **Minimum relay fee**: 0.001 DGB/kB
- **Default transaction fee**: 0.1 DGB/kB

Testnet isn't "free" (still requires fees for spam prevention), but DGB is easy to mine with `generatetoaddress`.

### DigiDollar Testing Questions

**Q: How much testnet DGB do I need to mint DigiDollars?**

A: Depends on lock period and amount:

**Example: Mint $100 DD with 1-year lock (300% collateral)**
- Oracle price: $0.05/DGB
- Required collateral: $100 × 3.00 = $300
- DGB needed: $300 ÷ $0.05 = **6,000 DGB**

**Example: Mint $10 DD with 10-year lock (200% collateral)**
- Oracle price: $0.05/DGB
- Required collateral: $10 × 2.00 = $20
- DGB needed: $20 ÷ $0.05 = **400 DGB**

Mine testnet DGB with: `digibyte-cli -testnet generatetoaddress 100 [address]`

**Q: Can I mint DigiDollars immediately after reset?**

A: No, you need to wait for block 650 (DigiDollar + Oracle activation) plus coinbase maturity:
1. **Block 1 reached** - BIPs active
2. **Block 600 reached** - Odocrypt active (~2.5 hours from genesis)
3. **Block 650 reached** - Oracle + DigiDollar activation (~2.7 hours from genesis)
4. **Mined DGB with 100 confirmations** (coinbase maturity)

So after mining ~750 blocks total, you can start minting. This takes about **~3.1 hours** (750 blocks × 15 seconds = 11,250 seconds).

**Why 650?** Matches regtest exactly - DigiDollar AND Oracle both activate AFTER Odocrypt at 600, ensuring price feed data is available immediately when DigiDollar goes live.

**Q: Can I test redemption immediately?**

A: No, redemptions require:
1. **Minting first** (create a vault position)
2. **Waiting for timelock** (1 hour to 10 years depending on lock period)
3. **Having DigiDollars to burn** (to unlock collateral)

For rapid testing, use regtest where you can control time:
```bash
# Regtest: Mint with 1-day lock
digibyte-cli -regtest mintdigidollar 10.00 0

# Advance time by 1 day (generate blocks)
digibyte-cli -regtest generatetoaddress 5760 [address]  # 24 hours worth

# Now redeem
digibyte-cli -regtest redeemdigidollar [position_id]
```

**Q: What if the oracle price is wrong?**

A: Current V1 testnet and mainnet operators use the live exchange-backed
oracle and MuSig2 v0x03 bundles. Mock price RPCs such as
`setmockoracleprice` are regtest-only helpers and are not production or
testnet fallbacks.

The active oracle fetcher set uses 6 exchange sources:
- Binance
- KuCoin
- Gate.io
- HTX
- Crypto.com
- CoinGecko
- Bittrex
- Poloniex
- Messari
- KuCoin
- Crypto.com

With **median aggregation** and **outlier filtering**.

**Q: Can I test the GUI or only command line?**

A: Both!
- **Qt GUI**: Full DigiDollar tab with 6 widgets (Overview, Send, Receive, Mint, Redeem, Positions)
- **RPC/CLI**: 27 DigiDollar commands
- **API**: REST API endpoints for integrations

Use whichever you prefer. GUI is more user-friendly for beginners.

### Troubleshooting

**Q: My node won't start after reset. What's wrong?**

**Solution checklist:**
1. **Did you delete testnet4/?**
   ```bash
   rm -rf ~/.digibyte/testnet4/
   ```
2. **Are you using v9.26?**
   ```bash
   digibyted --version
   # Should show: DigiByte Core version v9.26.0
   ```
3. **Is testnet flag set?**
   ```bash
   digibyted -testnet -daemon
   # Or in digibyte.conf: testnet=1
   ```
4. **Check debug.log:**
   ```bash
   tail -f ~/.digibyte/testnet4/debug.log
   ```

**Q: I'm stuck at block 0 / not syncing. Help?**

**Solutions:**
1. **Add nodes manually:**
   ```bash
   digibyte-cli -testnet addnode testnetseed.diginode.tools add
   digibyte-cli -testnet addnode testseed.digibyteblockchain.org add
   ```
2. **Check connections:**
   ```bash
   digibyte-cli -testnet getconnectioncount
   # Should be > 0
   ```
3. **Check network reachable:**
   ```bash
   digibyte-cli -testnet getnetworkinfo
   # Check "localaddresses" and "networks"
   ```
4. **Restart with debug logging:**
   ```bash
   digibyte-cli -testnet stop
   digibyted -testnet -daemon -debug=net
   tail -f ~/.digibyte/testnet4/debug.log
   ```

**Q: DigiDollar commands return "DigiDollar not active". Why?**

**Check current height:**
```bash
digibyte-cli -testnet getblockcount
```

If height < 100, DigiDollar isn't active yet. Wait for block 100 (~25 minutes from genesis).

If height >= 100:
1. **Check activation status:**
   ```bash
   digibyte-cli -testnet getdigidollarstats
   ```
2. **Verify you're on correct chain:**
   ```bash
   digibyte-cli -testnet getblockhash 0
   # Should match new genesis hash
   ```

**Q: I minted DigiDollars but balance shows 0. Why?**

**Possible reasons:**
1. **Transaction not confirmed** - Wait for 1 confirmation
2. **Insufficient collateral** - Check transaction actually succeeded:
   ```bash
   digibyte-cli -testnet listdigidollartxs
   ```
3. **Wrong wallet** - Make sure you're checking the same wallet that minted
4. **GUI not refreshed** - Restart GUI or use RPC command:
   ```bash
   digibyte-cli -testnet getdigidollarbalance
   ```

**Q: My DD transfer is stuck in mempool. Help?**

**Check mempool:**
```bash
digibyte-cli -testnet getmempoolinfo
digibyte-cli -testnet getrawmempool
```

**Possible causes:**
1. **Fee too low** - Testnet has same fee requirements as mainnet
2. **Insufficient inputs** - Need both DD UTXOs and DGB for fees
3. **Invalid transaction** - Check debug.log for errors

**Solution:**
```bash
# Abandon stuck transaction
digibyte-cli -testnet abandontransaction [txid]

# Retry after the stuck transaction clears
digibyte-cli -testnet senddigidollar [address] [amount]
```

---

## 11. Appendix A: Complete Code Changes

### Full Diff of chainparams.cpp Changes

```diff
diff --git a/src/kernel/chainparams.cpp b/src/kernel/chainparams.cpp
index 1234567..abcdefg 100644
--- a/src/kernel/chainparams.cpp
+++ b/src/kernel/chainparams.cpp
@@ -455,10 +455,12 @@ public:
         pchMessageStart[2] = 0xbd;
         pchMessageStart[3] = 0xdd;

-        // OLD TESTNET GENESIS (2018)
-        genesis = CreateGenesisBlock(1516939474, 2411473, 0x1e0ffff0, 1, 8000);
+        // NEW TESTNET GENESIS (2025) - DigiDollar Phase One
+        // Genesis timestamp message: "DigiByte Testnet Reset 2025 - DigiDollar Phase One Launch"
+        // Mined on November 23, 2025
+        genesis = CreateGenesisBlock(1732329600, 2819473, 0x1e0ffff0, 1, 8000);
         consensus.hashGenesisBlock = genesis.GetHash();
-        assert(consensus.hashGenesisBlock == uint256S("0x308ea0711d5763be2995670dd9ca9872753561285a84da1d58be58acaa822252"));
+        assert(consensus.hashGenesisBlock == uint256S("0x[NEW_GENESIS_HASH]"));
         assert(genesis.hashMerkleRoot == uint256S("0x72ddd9496b004221ed0557358846d9248ecd4c440ebd28ed901efc18757d0fad"));

         vFixedSeeds.clear();
@@ -483,9 +485,9 @@ public:
         base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
         base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

-        checkpointData = {
+        checkpointData = {  // NO CHECKPOINTS - Fresh testnet reset (2025)
             {
-                {546, uint256S("0x08fa50178f4b4f9fe1bbaed3b0a2ee58d1c51cc8185f70c8089e4b95763d9cdb")},
+                // Empty - no historical checkpoints for fresh testnet
             }
         };

@@ -513,7 +515,7 @@ public:
         digidollarParams.maxMintAmount = 1000000;          // 1,000,000 cents = $10k maximum
         digidollarParams.oracleThreshold = 1;              // 1-of-1 consensus

-        consensus.nDDActivationHeight = 1000;              // DigiDollar active from block 1000
+        consensus.nDDActivationHeight = 100;               // DigiDollar active from block 100 (~25 minutes)
         consensus.nDDOracleEpochBlocks = 40;               // Rotate oracle signing epochs every 40 blocks (~10 minutes)
         consensus.nDDOracleUpdateInterval = 2;             // Update price every 2 blocks (~30 seconds)

@@ -521,7 +523,7 @@ public:
         // Oracle system (Phase One: Single oracle, 1-of-1 consensus)
         // Phase Two / Phase 3 MuSig2 uses 17 oracles with 9-of-17 threshold (RC30)
         // Phase One: Testnet and regtest only
-        consensus.nOracleActivationHeight = 1000000;       // Activate at height 1M on testnet
+        consensus.nOracleActivationHeight = 650;           // Activate at height 650 (with DigiDollar!)
         consensus.nOracleEpochLength = 1440;               // 24 hours (1440 blocks * 15 seconds)
         consensus.nOracleRequiredMessages = 1;             // Phase One: 1-of-1 consensus
         consensus.nOracleTotalOracles = 1;                 // Phase One: Single oracle
@@ -531,9 +533,9 @@ public:
         );

         // BIP9 deployments - DigiDollar (Testnet: Always active for Phase One testing)
-        consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = 1704067200;  // Jan 1, 2024
-        consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = 1735689600;    // Jan 1, 2025
-        consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 0;
+        consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
+        consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
+        consensus.vDeployments[Consensus::DEPLOYMENT_DIGIDOLLAR].min_activation_height = 100;  // Match nDDActivationHeight

         // BIP9 deployments - Taproot
         consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = 1718921304;  // 20th June 2024
```

### Version Bump (if needed)

**File: `/home/jared/Code/digibyte/configure.ac`**

```diff
@@ -2,7 +2,7 @@ AC_PREREQ([2.69])
 define(_CLIENT_VERSION_MAJOR, 9)
 define(_CLIENT_VERSION_MINOR, 26)
-define(_CLIENT_VERSION_BUILD, 0)
+define(_CLIENT_VERSION_BUILD, 1)
 define(_CLIENT_VERSION_RC, 0)
 define(_CLIENT_VERSION_IS_RELEASE, false)
 define(_COPYRIGHT_YEAR, 2025)
```

This would make it v9.26.1 if you want to distinguish testnet reset from initial v9.26.0.

---

## 12. Appendix B: Genesis Block Mining Tool

See **Step 2.1** for the complete mining tool source code.

**Quick reference for compiling:**

```bash
# Navigate to contrib/devtools
cd /home/jared/Code/digibyte/contrib/devtools

# Compile (requires DigiByte already built)
g++ -o mine_genesis mine_genesis.cpp \
    -I../../src \
    -I../../src/secp256k1/include \
    -L../../src/.libs \
    -ldigibyte_consensus \
    -lsecp256k1 \
    -std=c++17 \
    -O2

# Run
./mine_genesis
```

**Alternative: Python Mining Script** (simpler, slower):

```python
#!/usr/bin/env python3
"""
Simple genesis block miner for DigiByte testnet
WARNING: This is slow (Python). Use C++ version for production.
"""

import hashlib
import struct
import time

def double_sha256(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def mine_genesis():
    # Testnet parameters
    version = 1
    prev_hash = b'\x00' * 32  # Null for genesis
    timestamp_msg = b"DigiByte Testnet Reset 2025 - DigiDollar Phase One Launch"

    # Coinbase transaction (simplified)
    # This is approximate - real implementation needs full tx structure
    tx_data = struct.pack('<I', 1)  # Version
    tx_data += b'\x01'  # 1 input
    # ... (full coinbase construction omitted for brevity)

    merkle_root = double_sha256(tx_data)  # Simplified

    ntime = int(time.time())
    nbits = 0x1e0ffff0

    print("Mining testnet genesis block...")
    print(f"Timestamp: {ntime}")
    print(f"Difficulty: 0x{nbits:08x}\n")

    nonce = 0
    target = (0x0ffff0 << 8) << (8 * (0x1e - 3))  # Compute target from nbits

    while True:
        # Build block header
        header = struct.pack('<I', version)
        header += prev_hash
        header += merkle_root
        header += struct.pack('<I', ntime)
        header += struct.pack('<I', nbits)
        header += struct.pack('<I', nonce)

        # Hash header
        block_hash = double_sha256(header)
        hash_int = int.from_bytes(block_hash, 'little')

        if hash_int < target:
            print(f"SUCCESS!\n")
            print(f"nTime: {ntime}")
            print(f"nNonce: {nonce}")
            print(f"Hash: {block_hash[::-1].hex()}")
            break

        nonce += 1
        if nonce % 100000 == 0:
            print(f"Tried {nonce} nonces...")

if __name__ == '__main__':
    mine_genesis()
```

**Note**: This Python version is simplified and slow. Use the C++ version from Step 2.1 for actual genesis mining.

---

## 13. Appendix C: DNS Seed Coordination

### DNS Seed Operator Contact Template

```
Subject: DigiByte Testnet Reset - Action Required

Hello [Operator Name],

We're performing a testnet reset for DigiByte v9.26 to enable DigiDollar Phase One testing.

RESET DETAILS:
- Date: [EXACT DATE]
- Time: [EXACT UTC TIME]
- New Genesis Hash: 0x[NEW_GENESIS_HASH]

ACTION REQUIRED FROM YOU:

1. BEFORE RESET (T-1 hour):
   - Stop your testnet seeder
   - Clear testnet seed database
   - Update to v9.26 if not already

2. AT RESET (T=0):
   - Wait for "GO" confirmation from core team
   - Start seeder with fresh database
   - Monitor for new genesis block

3. AFTER RESET (T+1 hour):
   - Verify seeder operational
   - Confirm peers connecting
   - Report status to core team

VERIFICATION COMMANDS:

# Check your node is on new testnet
digibyte-cli -testnet getblockhash 0
# Should return: 0x[NEW_GENESIS_HASH]

# Check seeder output
# Should see nodes connecting to new genesis

NO CHANGES NEEDED:
- DNS hostname (testnetseed.[yourdomain])
- Port (12026)
- Seeder configuration (unless v9.26 specific changes)

COMMUNICATION:
- Discord: #development channel
- Point of contact: [LEAD DEV NAME/EMAIL]
- Emergency contact: [PHONE NUMBER]

TIMELINE:
- T-2 weeks: This notification
- T-1 week: Reminder
- T-24 hours: Final confirmation
- T-1 hour: Standby notice
- T=0: GO signal
- T+1 hour: Status check

Please confirm receipt and availability for this reset.

Thank you for your critical role in DigiByte infrastructure!

Best regards,
[YOUR NAME]
DigiByte Core Development Team
```

### DNS Seed Testing Checklist

**Before Reset:**
- [ ] Operator confirmed available
- [ ] Operator has v9.26 binaries
- [ ] Seeder software updated (if needed)
- [ ] Backup of old seed database (optional)
- [ ] Emergency contact info exchanged

**During Reset:**
- [ ] Operator standing by
- [ ] Old seeder stopped
- [ ] Database cleared
- [ ] New genesis hash communicated
- [ ] GO signal received
- [ ] New seeder started

**After Reset:**
- [ ] Seeder connecting to network
- [ ] Seeder returning peer IPs
- [ ] DNS resolution working
- [ ] Multiple peers connecting through seed
- [ ] Operator confirmed operational

### DNS Seed Monitoring

**Set up monitoring to verify seeds are working:**

```bash
#!/bin/bash
# Test all testnet DNS seeds

SEEDS=(
    "testnetseed.diginode.tools"
    "testseed.digibyteblockchain.org"
    "testnet.digibyteseed.com"
    "testnetseed.digibyte.link"
    "testnetseed.digibyte.services"
)

echo "=== Testing DigiByte Testnet DNS Seeds ==="
echo "Time: $(date)"
echo ""

for seed in "${SEEDS[@]}"; do
    echo "Testing: $seed"

    # DNS lookup
    IPS=$(dig +short $seed A)

    if [ -z "$IPS" ]; then
        echo "  ❌ FAIL: No IPs returned"
    else
        COUNT=$(echo "$IPS" | wc -l)
        echo "  ✅ OK: $COUNT IPs returned"
        echo "$IPS" | head -3 | sed 's/^/    /'
        if [ $COUNT -gt 3 ]; then
            echo "    ... and $((COUNT - 3)) more"
        fi
    fi

    echo ""
done
```

Run this script hourly after reset to verify seeds are operational.

---

## Conclusion

This guide provides a complete, step-by-step process for resetting the DigiByte testnet to enable DigiDollar Phase One testing.

### Key Takeaways

1. **Testnet reset is necessary** for efficient DigiDollar testing
2. **No impact on mainnet** - completely isolated networks
3. **Community coordination is critical** - DNS seeds, validators, developers
4. **Process is well-defined** - from genesis mining to activation
5. **Testing is comprehensive** - unit, functional, integration, community
6. **Documentation is complete** - this guide plus code comments

### Success Criteria

**The testnet reset is successful when:**
- ✅ New genesis block propagates to 10+ nodes within 1 hour
- ✅ Block production stable at ~15 second intervals
- ✅ Odocrypt activates at block 600 (multi-algo mining stable)
- ✅ Oracle system activates at block 650 and provides price feeds
- ✅ DigiDollar activates at block 650 and minting works
- ✅ Community validators successfully test all DD features
- ✅ No critical bugs found in first week
- ✅ Network remains stable for 30+ days

### Next Steps After Reset

**Week 1:**
- Monitor network health
- Collect community feedback
- Fix critical bugs
- Update documentation

**Week 2-4:**
- Extended testing scenarios
- Stress testing
- Performance optimization
- Feature refinements

**Month 2-3:**
- Phase One refinement
- Begin Phase Two / Phase 3 MuSig2 planning (9-of-17 oracles — RC30)
- Mainnet deployment preparation
- Security audits

### Support and Resources

**Documentation:**
- This guide: `TESTNET_RESET_GUIDE.md`
- DigiDollar docs: `DIGIDOLLAR_EXPLAINER.md`, `DIGIDOLLAR_ARCHITECTURE.md`
- Oracle docs: `DIGIDOLLAR_ORACLE_ARCHITECTURE.md`
- Code documentation: Inline comments in source files

**Community:**
- Discord: #development, #digidollar-testing
- GitHub: https://github.com/DigiByte-Core/digibyte
- Reddit: r/DigiByte
- Website: https://digibyte.org

**Contact:**
- Lead Developer: [Contact info]
- Core Team: core@digibyte.org
- Emergency: [Emergency contact]

---

**Thank you for being part of DigiByte's most significant innovation: DigiDollar, the world's first UTXO-native stablecoin! 🚀💙**

---

*This guide was created with the assistance of AI agents analyzing the DigiByte v9.26 codebase to provide the most accurate and complete information possible. Last updated: November 23, 2025.*
