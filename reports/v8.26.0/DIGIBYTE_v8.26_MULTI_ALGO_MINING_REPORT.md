# DigiByte v8.26 Multi-Algorithm Mining System Report

## 1. Simple Explanation: How Multi-Algorithm Mining Works

### What is Multi-Algorithm Mining?

DigiByte's multi-algorithm mining system is like having five different types of locks on a door, where any one key can open it - but each lock requires a completely different type of key. Instead of relying on just one mining algorithm (like Bitcoin's SHA-256), DigiByte allows miners to use five different algorithms simultaneously to find blocks and secure the network.

### Why Multiple Algorithms?

1. **Decentralization**: Different algorithms favor different hardware (CPUs, GPUs, ASICs, FPGAs), preventing any single type of miner from dominating
2. **Security**: An attacker would need to control 93% of one algorithm plus 51% of the remaining hashrate to attack the network
3. **Accessibility**: More people can participate in mining using various hardware types
4. **Resilience**: If one algorithm becomes compromised or centralized, the network remains secure

### How It Works

- **Fair Competition**: All five algorithms compete to find the next block
- **Target Time**: Each algorithm aims to find one block every 75 seconds (5 algos × 15 seconds = 75 seconds per algo)
- **Independent Difficulty**: Each algorithm has its own difficulty that adjusts in real-time
- **Equal Opportunity**: Any algorithm can find the next block - it's a race!

## 2. The Evolution of DigiByte Mining

DigiByte's mining system has evolved through five major eras:

### Era 1: DigiShield V1 (Blocks 0 - 144,999)
- **Launch**: January 10, 2014 (Block 0)
- **DigiShield Activation**: Block 67,200 (February 28, 2014)
- **Algorithm**: Scrypt only
- **Innovation**: Real-time difficulty adjustment (DigiShield) - adjusts every block instead of every 2016 blocks like Bitcoin
- **Block Time**: 60 seconds initially, then 30 seconds after block 67,200

### Era 2: MultiAlgo V2 (Blocks 145,000 - 399,999) 
- **Activation**: September 1, 2014
- **Algorithms**: 5 activated (SHA256D, Scrypt, Groestl, Skein, Qubit)
- **Innovation**: First blockchain with 5 simultaneous mining algorithms
- **Block Time**: 30 seconds (150 seconds per algorithm / 5 algorithms)
- **Difficulty**: Each algorithm adjusts independently every 10 blocks

### Era 3: MultiShield V3 (Blocks 400,000 - 1,429,999)
- **Activation**: December 10, 2014
- **Algorithms**: Same 5 algorithms
- **Innovation**: Global difficulty balancing with per-algorithm fine-tuning
- **Block Time**: Still 30 seconds
- **Improvement**: Better resistance to multipool mining and hashrate fluctuations

### Era 4: DigiSpeed V4 (Blocks 1,430,000 - 9,112,319)
- **Activation**: December 4, 2015
- **Algorithms**: Same 5 algorithms
- **Innovation**: 15-second blocks (2x faster) with more responsive difficulty
- **Block Time**: 15 seconds (75 seconds per algorithm / 5 algorithms)
- **Benefits**: Faster confirmations, improved scalability

### Era 5: Odocrypt Era (Blocks 9,112,320+)
- **Activation**: July 21, 2019
- **Algorithms**: 5 algorithms (Groestl swapped for Odocrypt)
- **Innovation**: Odocrypt changes its internal structure every 10 days (FPGA-friendly, ASIC-resistant)
- **Current Algorithms**: SHA256D, Scrypt, Skein, Qubit, Odocrypt
- **Block Time**: 15 seconds (maintained)

## 3. How Real-Time Difficulty Adjustment Works

### DigiShield & MultiShield Technology

Unlike Bitcoin which adjusts difficulty every 2016 blocks (approximately 2 weeks), DigiByte adjusts difficulty on **every single block**. This real-time adjustment prevents mining attacks and ensures consistent block times.

#### DigiShield (Single Algorithm Era)
- **Purpose**: Protect against multipool mining attacks
- **Method**: Adjusts difficulty within limits (+25%/-50%) every block  
- **Result**: Stable block times even with massive hashrate fluctuations

#### MultiShield (Multi-Algorithm Era)
- **Global Adjustment**: Overall network difficulty based on median block times
- **Local Adjustment**: Per-algorithm fine-tuning (±2-4% per block gap)
- **Algorithm Fairness**: If one algorithm gets too many blocks, its difficulty increases
- **Result**: All algorithms maintain equal opportunity to find blocks

## 4. Mainnet Mining Process Flowchart

```
┌─────────────────┐
│  NEW BLOCK      │
│  REQUEST        │
└────────┬────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────┐
│                   CHECK BLOCK HEIGHT                        │
├─────────────────────────────────────────────────────────────┤
│  H < 145,000:        V1 DigiShield (Scrypt only)                   │
│  145,000 ≤ H < 400,000:  V2 MultiAlgo (5 algorithms)               │
│  400,000 ≤ H < 1,430,000: V3 MultiShield (improved adjustment)     │
│  1,430,000 ≤ H < 9,112,320: V4 DigiSpeed (15-sec blocks)          │
│  H ≥ 9,112,320:      Odocrypt Era (Groestl replaced by Odocrypt)   │
└─────────────────────────────────────────────────────────────┘
                               │
         ┌─────────────────────┴─────────────────────┐
         ▼                                           ▼
┌──────────────────┐                      ┌───────────────────┐
│ H < 145,000      │                      │ H ≥ 145,000       │
│ ─────────────    │                      │ ──────────────    │
│ Only SCRYPT      │                      │ MULTI-ALGO MODE   │
│ Active           │                      │                   │
└────────┬─────────┘                      └─────────┬─────────┘
         │                                          │
         ▼                                          ▼
┌──────────────────┐              ┌────────────────────────────┐
│ V1 DIFFICULTY    │              │   SELECT ALGORITHM         │
│ ──────────────   │              ├────────────────────────────┤
│ • Retarget every │              │ Blocks 145k-9.1M:          │
│   67 blocks      │              │ • SHA256D (ASIC)           │
│ • Limits: +25%   │              │ • Scrypt (ASIC)            │
│   -50%           │              │ • Groestl (GPU)            │
└────────┬─────────┘              │ • Skein (CPU)              │
         │                        │ • Qubit (Mixed)            │
         │                        ├────────────────────────────┤
         │                        │ Blocks 9,112,320+:         │
         │                        │ • SHA256D (ASIC)           │
         │                        │ • Scrypt (ASIC)            │
         │                        │ • Skein (CPU)              │
         │                        │ • Qubit (Mixed)            │
         │                        │ • Odocrypt (FPGA)          │
         │                        │   [Groestl removed]        │
         │                        └─────────┬──────────────────┘
         │                                   │
         │                                   ▼
         │              ┌────────────────────────────────────┐
         │              │    GET DIFFICULTY VERSION         │
         │              ├────────────────────────────────────┤
         │              │ 145k ≤ H < 400k: V2 GetNextWork   │
         │              │ • 10-block average per algo       │
         │              │ • Independent algo difficulties   │
         │              ├────────────────────────────────────┤
         │              │ 400k ≤ H < 1.43M: V3 GetNextWork  │
         │              │ • Global median time adjustment   │
         │              │ • Per-algo local ±2% per block    │
         │              ├────────────────────────────────────┤
         │              │ H ≥ 1,430,000: V4 GetNextWork     │
         │              │ • Faster 15-second blocks         │
         │              │ • Per-algo local ±4% per block    │
         │              └─────────┬──────────────────────────┘
         │                        │
         ▼                        ▼
┌──────────────────┐    ┌────────────────────────────────────┐
│ SET VERSION = 2  │    │    SET BLOCK VERSION WITH ALGO     │
│                  │    ├────────────────────────────────────┤
│ (Pre-MultiAlgo   │    │ Version = 0x20000000 | base | algo │
│  uses 0x20000002)│    │                                    │
│                  │    │                                    │
│                  │    │ Examples (with BIP9):              │
│                  │    │ • Scrypt:  0x20000002              │
│                  │    │ • SHA256D: 0x20000202              │
│                  │    │ • Groestl: 0x20000402              │
│                  │    │ • Skein:   0x20000602              │
│                  │    │ • Qubit:   0x20000802              │
│                  │    │ • Odo:     0x20000E02              │
└────────┬─────────┘    └─────────┬──────────────────────────┘
         │                        │
         │                        ▼
         │              ┌────────────────────────────────────┐
         │              │   APPLY ALGORITHM HASH FUNCTION    │
         │              ├────────────────────────────────────┤
         │              │ SHA256D:  Double SHA-256           │
         │              │ Scrypt:   Scrypt(N=1024,r=1,p=1)  │
         │              │ Groestl:  Groestl-512              │
         │              │ Skein:    Skein-512                │
         │              │ Qubit:    5-round chain            │
         │              │ Odocrypt: Odo(key=time/10days)    │
         │              └─────────┬──────────────────────────┘
         │                        │
         └────────────┬───────────┘
                      │
                      ▼
         ┌────────────────────────┐
         │   MINE BLOCK           │
         │   ──────────           │
         │   Find nonce where:    │
         │   hash < difficulty    │
         └──────────┬─────────────┘
                    │
                    ▼
         ┌────────────────────────┐
         │   VALIDATE POW         │
         │   ─────────────        │
         │   • Check algo active  │
         │   • Verify hash < diff │
         │   • Update chain       │
         └──────────┬─────────────┘
                    │
                    ▼
         ┌────────────────────────┐
         │   UPDATE DIFFICULTY    │
         │   ──────────────────   │
         │   • Per-algo tracking  │
         │   • Real-time adjust   │
         └──────────┬─────────────┘
                    │
                    ▼
         ┌────────────────────────┐
         │   BLOCK ACCEPTED       │
         └────────────────────────┘
```

## 5. Files & Functions Index

### Core Mining Files

#### `/src/primitives/block.h` & `/src/primitives/block.cpp`
- **Algorithm Definitions**:
  - `enum` defining `ALGO_SHA256D`, `ALGO_SCRYPT`, `ALGO_GROESTL`, `ALGO_SKEIN`, `ALGO_QUBIT`, `ALGO_ODO`
  - Block version constants: `BLOCK_VERSION_SHA256D`, `BLOCK_VERSION_SCRYPT`, etc.
- **Key Functions**:
  - `CBlockHeader::GetAlgo()` - Extracts algorithm from block version (line 25-47)
  - `CBlockHeader::GetPoWAlgoHash()` - Computes algorithm-specific hash (line 56-106)
  - `CBlockHeader::SetAlgo()` - Sets algorithm in block version
  - `GetAlgoName()` - Converts algorithm ID to string (line 126-148)
  - `GetAlgoByName()` - Converts string to algorithm ID (line 150-171)
  - `OdoKey()` - Calculates Odocrypt shapechange key (line 49-54)

#### `/src/pow.cpp` & `/src/pow.h`
- **Difficulty Adjustment Functions**:
  - `GetNextWorkRequired()` - Main entry point, routes to version (line 256-285)
  - `GetNextWorkRequiredV1()` - DigiShield (single algo) (line 27-93)
  - `GetNextWorkRequiredV2()` - MultiAlgo (per-algo averaging) (line 95-137)
  - `GetNextWorkRequiredV3()` - MultiShield (global + local) (line 139-190)
  - `GetNextWorkRequiredV4()` - DigiSpeed (faster adjustment) (line 192-254)
- **Algorithm Search**:
  - `GetLastBlockIndexForAlgo()` - Finds previous block of same algo (line 395-411)
  - `GetLastBlockIndexForAlgoFast()` - Optimized version using cache (line 413-430)
- **Validation**:
  - `CheckProofOfWork()` - Validates block hash meets difficulty (line 376-393)
  - `PermittedDifficultyTransition()` - Disabled for DigiByte (line 314-374)

#### `/src/validation.cpp`
- **Algorithm Activation**:
  - `IsAlgoActive()` - Determines which algorithms are active at height (line 1831-1858)
    - Before block 100: Scrypt only
    - Blocks 100-599: SHA256D, Scrypt, Groestl, Skein, Qubit
    - Block 600+: SHA256D, Scrypt, Skein, Qubit, Odocrypt (Groestl swapped out)

#### `/src/consensus/params.h`
- **Consensus Parameters Structure**:
  - Fork height definitions (lines 108-113)
  - Difficulty adjustment parameters (lines 136-167)
  - Multi-algo specific parameters (lines 147-148)
  - `DeploymentHeight()` - Returns activation height for features (line 188-211)

#### `/src/kernel/chainparams.cpp`
- **Network-Specific Parameters**:
  - **Mainnet** (lines 108-149):
    - `multiAlgoDiffChangeTarget = 145000`
    - `alwaysUpdateDiffChangeTarget = 400000`
    - `workComputationChangeTarget = 1430000`
    - `algoSwapChangeTarget = 9100000`
    - `OdoHeight = 9112320`
  - **Testnet** (lines 339-344):
    - `multiAlgoDiffChangeTarget = 100`
    - `alwaysUpdateDiffChangeTarget = 400`
    - `workComputationChangeTarget = 1430`
    - `algoSwapChangeTarget = 20000`
    - `OdoHeight = 600`
  - **Regtest** (lines 642-646):
    - `multiAlgoDiffChangeTarget = 100`
    - `alwaysUpdateDiffChangeTarget = 200`
    - `workComputationChangeTarget = 400`
    - `algoSwapChangeTarget = 600`

#### `/src/rpc/mining.cpp`
- **RPC Commands**:
  - `getmininginfo()` - Returns per-algorithm difficulties (line 441-513)
  - `getblocktemplate()` - Includes `odokey` for Odocrypt (line 617-1024)
  - Mining algorithm selection via `miningAlgo` global (line 53)

#### `/src/rpc/blockchain.cpp`
- **RPC Commands**:
  - `getdifficulty()` - Returns current difficulty (line 428-446)
  - `GetDifficulty()` - Helper supporting per-algorithm queries

#### `/src/chain.cpp` & `/src/chain.h`
- **Chain Index**:
  - `CBlockIndex::GetAlgo()` - Gets algorithm from stored block
  - `lastAlgoBlocks[]` - Array caching last block per algorithm

## 6. Technical Implementation Details (v8.26)

### 6.1 Block Version Structure

**CRITICAL**: Block version encoding is different across DigiByte's evolution and MUST be correct for each era.

#### Version Bit Layout
```
Bits 31-28: VERSIONBITS signaling (0x2 when active via VERSIONBITS_TOP_BITS)
Bits 11-8:  Algorithm identifier (masked by BLOCK_VERSION_ALGO = 0x0F00)
Bits 7-0:   Base version (BLOCK_VERSION_DEFAULT = 2)
```

#### Constants (from versionbits.h and primitives/block.h)
```cpp
VERSIONBITS_TOP_BITS = 0x20000000  // Set in bits 31-28
VERSIONBITS_TOP_MASK = 0xF0000000  // Mask for top 4 bits
BLOCK_VERSION_DEFAULT = 2           // Base version

// Algorithm bits (in bits 11-8):
BLOCK_VERSION_SCRYPT  = (0 << 8)  = 0x0000
BLOCK_VERSION_SHA256D = (2 << 8)  = 0x0200
BLOCK_VERSION_GROESTL = (4 << 8)  = 0x0400
BLOCK_VERSION_SKEIN   = (6 << 8)  = 0x0600
BLOCK_VERSION_QUBIT   = (8 << 8)  = 0x0800
BLOCK_VERSION_ODO     = (14 << 8) = 0x0E00
```

#### Complete Block Version Table for Mainnet (v8.26)

| Era | Height Range | Active Algorithms | Version Calculation | Hex Values |
|-----|--------------|------------------|---------------------|------------|
| **Pre-MultiAlgo** | 0-144,999 | Scrypt only | VERSIONBITS + base | `0x20000002` (Scrypt with VERSIONBITS) |
| **MultiAlgo V2** | 145,000-399,999 | 5 algorithms | VERSIONBITS + algo | `0x20000002` (Scrypt)<br/>`0x20000202` (SHA256D)<br/>`0x20000402` (Groestl)<br/>`0x20000602` (Skein)<br/>`0x20000802` (Qubit) |
| **MultiShield V3** | 400,000-1,429,999 | 5 algorithms | VERSIONBITS + algo | Same as MultiAlgo era |
| **DigiSpeed V4** | 1,430,000-9,112,319 | 5 algorithms | VERSIONBITS + algo | Same as MultiAlgo era |
| **Odocrypt Era** | 9,112,320+ | 5 algorithms<br/>(Groestl removed,<br/>Odocrypt added) | VERSIONBITS + algo | `0x20000002` (Scrypt)<br/>`0x20000202` (SHA256D)<br/>`0x20000602` (Skein)<br/>`0x20000802` (Qubit)<br/>`0x20000E02` (Odocrypt)<br/>**NO** `0x20000402` (Groestl removed) |

#### Version Creation Code (from versionbits.cpp:231-248)
```cpp
int32_t ComputeBlockVersion(pindexPrev, params, algo) {
    int32_t nVersion = VERSIONBITS_TOP_BITS | BLOCK_VERSION_DEFAULT;
    // ... add any BIP9 deployment bits ...
    nVersion |= GetVersionForAlgo(algo);  // Add algorithm bits
    return nVersion;
}
```

### 6.2 Difficulty Adjustment Eras (Mainnet)

#### Era V1: DigiShield (Blocks 0-144,999)
- **Initial Phase** (Blocks 0-67,199): Standard difficulty adjustment
- **DigiShield Activated** (Block 67,200+): Real-time difficulty adjustment
- Single algorithm (Scrypt only)
- Retargets every block after DigiShield activation
- Adjustment limits: 25% up, 50% down (protects against multipool attacks)
- Real-time response to hash rate changes
- Code: `GetNextWorkRequiredV1()` in pow.cpp:27-93

#### Era V2: MultiAlgo (Blocks 145,000-399,999)
- 5 algorithms activated simultaneously (SHA256D, Scrypt, Groestl, Skein, Qubit)
- Each algorithm maintains independent difficulty
- 10-block averaging window per algorithm (`nAveragingInterval = 10`)
- Target: 150 seconds between blocks of same algorithm (30s × 5 algos)
- Adjustment limits: 100% up, 40% down
- Code: `GetNextWorkRequiredV2()` in pow.cpp:95-137

#### Era V3: MultiShield (Blocks 400,000-1,429,999)
- Global difficulty adjustment using median time past
- Per-algorithm local adjustment: `nLocalDifficultyAdjustment = 2%`
- 50-block lookback window (10 blocks × 5 algos)
- Dampened adjustment: `targetTimespan + (actual - target) / 6`
- Adjustment limits: 16% up, 8% down
- Code: `GetNextWorkRequiredV3()` in pow.cpp:139-190

#### Era V4: DigiSpeed (Blocks 1,430,000-9,112,319)
- Faster 15-second target blocks (2x speed increase)
- 75-second target per algorithm (15s × 5 algos)
- More responsive adjustment: `targetTimespan + (actual - target) / 4`
- Adjustment limits: 16% up, 8% down
- Per-algorithm adjustment: `nLocalTargetAdjustment = 4%` per block gap
- Code: `GetNextWorkRequiredV4()` in pow.cpp:192-254

#### Era V5: Odocrypt (Blocks 9,112,320+)
- Uses V4 difficulty adjustment algorithm
- Groestl algorithm removed, Odocrypt added
- Odocrypt changes internal structure every 10 days (864,000 seconds)
- Same 15-second block times and adjustment parameters as V4

### 6.3 Algorithm Hash Functions

Each algorithm applies a different proof-of-work hash function, targeting different hardware types:

#### 1. **SHA256D** (`ALGO_SHA256D = 0`)
- **Function**: Double SHA-256 (SHA256(SHA256(data)))
- **Hardware**: ASIC-optimized
- **Characteristics**: Same as Bitcoin, highly efficient on specialized hardware
- **Market Share**: Typically 20-25% of blocks

#### 2. **Scrypt** (`ALGO_SCRYPT = 1`)
- **Function**: Scrypt with parameters N=1024, r=1, p=1
- **Hardware**: ASIC-optimized (originally CPU/GPU)
- **Characteristics**: Memory-hard function, 128KB memory requirement
- **Market Share**: Typically 20-25% of blocks
- **Note**: Same parameters as Litecoin

#### 3. **Groestl** (`ALGO_GROESTL = 2`)
- **Function**: Groestl-512 hash
- **Hardware**: GPU-optimized
- **Active Period**: Blocks 145,000 - 9,112,319 (mainnet)
- **Status**: **REMOVED** at block 9,112,320, replaced by Odocrypt
- **Characteristics**: AES-based, efficient on GPUs

#### 4. **Skein** (`ALGO_SKEIN = 3`)
- **Function**: Skein-512-256
- **Hardware**: CPU/GPU balanced
- **Characteristics**: Threefish block cipher based, SHA-3 finalist
- **Market Share**: Typically 20-25% of blocks

#### 5. **Qubit** (`ALGO_QUBIT = 4`)
- **Function**: Chain of 5 hash functions (Luffa → CubeHash → SHAvite → SIMD → Echo)
- **Hardware**: CPU/GPU balanced, ASIC-resistant
- **Characteristics**: Multiple rounds prevent ASIC optimization
- **Market Share**: Typically 20-25% of blocks

#### 6. **Odocrypt** (`ALGO_ODO = 7`)
- **Function**: Memory-hard algorithm with periodic shapechange
- **Hardware**: FPGA-optimized, ASIC-resistant
- **Shapechange Interval**: 10 days (864,000 seconds on mainnet)
- **Key Calculation**: `blockTime - (blockTime % 864000)`
- **Active Period**: Block 9,112,320+ (mainnet)
- **Characteristics**: 
  - Algorithm structure changes every 10 days
  - Prevents ASIC development (would become obsolete before manufacture)
  - FPGA miners can adapt to structure changes
- **Market Share**: Typically 20-25% of blocks
- **Code**: `OdoKey()` in primitives/block.cpp:49-54

### 6.4 Real-Time Difficulty Adjustment

The difficulty adjustment happens on **every single block** (not every 2016 blocks like Bitcoin):

```cpp
// V4 adjustment (current era on mainnet) - from pow.cpp:210-232
nActualTimespan = pindexLast->GetMedianTimePast() - pindexFirst->GetMedianTimePast();
nActualTimespan = nAveragingTargetTimespanV4 + (nActualTimespan - nAveragingTargetTimespanV4)/4;

// Apply limits
if (nActualTimespan < nMinActualTimespanV4) 
    nActualTimespan = nMinActualTimespanV4;
if (nActualTimespan > nMaxActualTimespanV4) 
    nActualTimespan = nMaxActualTimespanV4;

// Global retarget
bnNew = oldDifficulty;
bnNew *= nActualTimespan;
bnNew /= nAveragingTargetTimespanV4;

// Per-algorithm adjustment (pow.cpp:226-245)
nAdjustments = pindexPrevAlgo->nHeight + NUM_ALGOS - 1 - pindexLast->nHeight;
if (nAdjustments > 0) {
    for (int i = 0; i < nAdjustments; i++) {
        bnNew *= 100;
        bnNew /= (100 + nLocalTargetAdjustment);  // Make easier by 4% per block
    }
} else if (nAdjustments < 0) {
    for (int i = 0; i < -nAdjustments; i++) {
        bnNew *= (100 + nLocalTargetAdjustment);  // Make harder by 4% per block
        bnNew /= 100;
    }
}
```

### 6.5 Mining Process Flow

1. **Algorithm Selection**:
   - Miner chooses algorithm (via RPC parameter or config)
   - Checks if algorithm is active at current height

2. **Block Template Creation**:
   - Sets block version with algorithm bits
   - Gets current difficulty for chosen algorithm
   - For Odocrypt: includes `odokey` in template

3. **Proof-of-Work**:
   - Applies algorithm-specific hash function
   - Searches for nonce where `hash < target`

4. **Block Validation**:
   - Verifies algorithm is active
   - Checks version bits are correct
   - Validates PoW using algorithm-specific hash

5. **Difficulty Update**:
   - Updates global difficulty metrics
   - Adjusts per-algorithm difficulty
   - Maintains `lastAlgoBlocks` chain pointers

### 6.6 Fork Heights and Parameters

| Parameter | Mainnet | Testnet | Regtest |
|-----------|---------|---------|---------|  
| DigiShield Activation | 67,200 | N/A | N/A |
| MultiAlgo Activation | 145,000 | 100 | 100 |
| MultiShield (V3) | 400,000 | 400 | 200 |
| DigiSpeed (V4) | 1,430,000 | 1,430 | 400 |
| Odocrypt Activation | 9,112,320 | 600 | 600 |
| Block Time | 15 seconds | 15 seconds | 15 seconds |
| Algorithms (current) | 5 active | 5 active | 5/6 active |

### 6.7 Critical Version Validation Notes

#### ReserveAlgoBits Activation
- **Mainnet**: Block 8,547,840 - VERSIONBITS becomes mandatory
- **Testnet/Signet/Regtest**: Block 0 - Always active
- After activation, ALL blocks MUST have `VERSIONBITS_TOP_BITS` set (except pre-MultiAlgo)

#### Common Version Errors
1. **Using version 4** (Bitcoin style) instead of DigiByte versions
2. **Missing VERSIONBITS_TOP_BITS** after MultiAlgo activation
3. **Using wrong algorithm bits** for the current era
4. **Using Groestl (0x20000402)** after block 600 (it's removed!)
5. **Not using Odocrypt (0x20000E02)** when mining with Odo after block 600

#### Version Validation (validation.cpp:4128-4132)
```cpp
// DigiByte validates version requirements
if ((block.nVersion < 2 && DeploymentActiveAfter(pindexPrev, DEPLOYMENT_HEIGHTINCB)) ||
    (block.nVersion < 3 && DeploymentActiveAfter(pindexPrev, DEPLOYMENT_DERSIG)) ||
    (block.nVersion < 4 && DeploymentActiveAfter(pindexPrev, DEPLOYMENT_CLTV))) {
    return state.Invalid("bad-version");
}
```

### 6.8 RPC Interface

The system exposes mining information through enhanced RPCs:

- **`getmininginfo`**: Returns difficulties for all active algorithms
- **`getblocktemplate`**: Accepts `algo` parameter, includes `odokey` for Odocrypt
- **`getdifficulty`**: Can query specific algorithm difficulty
- **`generatetoaddress`**: Accepts `algo` parameter for testing

## 7. Validation & Recent Updates

### Recent Mining Fixes (August 2025)

Based on recent commits to v8.26, the following critical issues were fixed:

1. **Algorithm Tracking Fix** (Commit c7814f7e95):
   - Fixed block template not regenerating when algorithm changes
   - Added `lastAlgo` tracking to force template regeneration
   - Critical for multi-algo mining to work correctly

2. **RPC Algorithm Support** (Commit 005b948919):
   - Added missing `algo` parameter to `generateblock` RPC
   - Added algorithm information to `getblocktemplate` response
   - Returns `pow_algo_id` and `pow_algo` in template

3. **Multi-Algorithm Mining Restoration** (Commit 12244ba6ae):
   - Restored full multi-algorithm mining RPC functionality from v8.22
   - Fixed algorithm selection in mining RPCs
   - Ensured proper algorithm validation

### Source Code Verification

All findings were verified against the v8.26 source code with recent fixes:

1. **Mainnet Fork Heights** (chainparams.cpp lines 109-113):
   - DigiShield: Block 67,200
   - MultiAlgo: Block 145,000
   - MultiShield: Block 400,000
   - DigiSpeed: Block 1,430,000
   - Odocrypt: Block 9,112,320

2. **Algorithm Activation Logic** (validation.cpp lines 1834-1860):
   - Pre-145k: Scrypt only
   - 145k-9.1M: 5 algorithms (SHA256D, Scrypt, Groestl, Skein, Qubit)
   - 9.1M+: 5 algorithms (Groestl removed, Odocrypt added)

3. **Difficulty Selection** (pow.cpp lines 277-284):
   - V1: Blocks 0-144,999
   - V2: Blocks 145,000-399,999
   - V3: Blocks 400,000-1,429,999
   - V4: Blocks 1,430,000+

### Key Technical Insights

1. **Real-Time Adjustment**: Every block adjusts difficulty (not every 2016 blocks like Bitcoin)
2. **Algorithm Balance**: Each algorithm targets 20% of blocks (1 in 5)
3. **Security Model**: Need 93% of one algorithm + 51% of others to attack
4. **Odocrypt Innovation**: Only blockchain with time-morphing PoW algorithm
5. **Fair Mining**: No single hardware type can dominate the network

### Implementation Notes

- **Block Time Evolution**: 60s → 30s (block 67,200) → 15s (block 1,430,000)
- **Algorithm Count**: Always 5 active algorithms (never 6 simultaneously)
- **Groestl Replacement**: Not a 6th algorithm addition, but a swap at block 9,112,320
- **Version Bits**: VERSIONBITS mandatory after block 8,547,840 (mainnet)

---

*Report updated with v8.26 source code analysis and recent commit fixes*
*Date: 2025-09-05*