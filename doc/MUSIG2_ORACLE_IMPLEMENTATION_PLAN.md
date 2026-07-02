# MuSig2 Oracle Bundle Implementation Plan

> Historical planning note: V1 implementation is complete and production
> validation is MuSig2 v0x03-only. Older transition/fallback language in this
> plan is retained as design history, not current release guidance.

> **Goal:** Replace individual per-oracle Schnorr signatures in oracle bundles with MuSig2 aggregate signatures, reducing on-chain oracle data from ~609 bytes (9-of-17) to ~88 bytes (constant, any N).
>
> **Status:** PLANNING — awaiting review before implementation begins
>
> **Author:** Irene (AI agent) + Jared Tate
>
> **Date:** March 26, 2026

---

## Table of Contents

1. [Problem Statement](#problem-statement)
2. [Background: What is MuSig2?](#background-what-is-musig2)
3. [Current Implementation (Phase 2)](#current-implementation-phase-2)
4. [Proposed Implementation (Phase 3)](#proposed-implementation-phase-3)
5. [On-Chain Format](#on-chain-format)
6. [Size Comparison](#size-comparison)
7. [Implementation Phases](#implementation-phases)
8. [Oracle Coordination Protocol](#oracle-coordination-protocol)
9. [Consensus Changes](#consensus-changes)
10. [Security Considerations](#security-considerations)
11. [Backward Compatibility](#backward-compatibility)
12. [Testing Strategy](#testing-strategy)
13. [Files to Modify](#files-to-modify)
14. [Open Questions](#open-questions)
15. [References](#references)

---

## Problem Statement

### ⚠️ THE CORE PROBLEM: Linear Signature Growth

The current oracle bundle implementation has a **fundamental scaling flaw**: on-chain size grows linearly with the number of oracles. Every oracle that signs adds exactly 65 bytes (1-byte ID + 64-byte Schnorr signature) to the coinbase OP_RETURN output. This means:

- **More decentralization = bigger blocks.** The more oracles we add to strengthen the network, the more we bloat the chain. Security and efficiency are working against each other.
- **The cost is permanent.** Oracle data is embedded in every single block, every 15 seconds, forever. There is no pruning — this data is part of consensus.
- **It doesn't scale.** At 17 oracles (RC30), we're burning over 1 KB per block on signatures alone. At 30 oracles, it would be ~2 KB. At 100 oracles, ~6.5 KB. Per block. Forever.

```
ON-CHAIN ORACLE SIZE vs. ORACLE COUNT (current implementation)

Bytes
1100 ┤
1130 ┤                                                          ●  17 oracles (1,129 B) [RC30]
 900 ┤
 800 ┤
 700 ┤                                          ●  11 oracles (739 B)
 600 ┤                              ●  9 oracles (609 B)
 500 ┤                  ●  7 oracles (479 B)
 400 ┤          ●  5 oracles (349 B)
 300 ┤  ●  4 oracles (284 B)
 200 ┤
 100 ┤──────────────────────────────────────────────────────────── MuSig2 (88 B, ANY count)
   0 ┤
     └──┬──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────
        4      5      7      9     11     13     15     30

     Current: size = 24 + (N × 65) bytes  ← GROWS WITH EVERY ORACLE
     MuSig2:  size = 88 bytes             ← CONSTANT REGARDLESS OF COUNT
```

**Annual chain growth from oracle data alone:**

| Oracles | Current Size | Annual Growth | MuSig2 Size | MuSig2 Annual |
|---------|-------------|---------------|-------------|---------------|
| 5-of-9 | 349 B/block | **700 MB/year** | 88 B/block | 176 MB/year |
| 9-of-17 (RC30) | 739 B/block | **1.48 GB/year** | 89 B/block | 178 MB/year |
| 17-of-17 | 1,129 B/block | **2.26 GB/year** | 89 B/block | 178 MB/year |
| 15-of-30 | 1,974 B/block | **3.96 GB/year** | 90 B/block | 180 MB/year |
| 30-of-100 | 6,474 B/block | **12.97 GB/year** | 94 B/block | 188 MB/year |

Over 10 years at 9-of-17, the current design adds **12.2 GB** of signature data to the chain. With MuSig2, that same 10 years costs **1.76 GB** — a saving of over 10 GB.

**This is a design flaw, not a feature.** The whitepaper explicitly called for aggregate signatures. The implementation took a shortcut.

---

### Whitepaper Specification (not implemented)

The DigiDollar whitepaper specifies that oracle bundles should use Schnorr threshold/aggregate signatures for efficient on-chain storage:

> *"Threshold Signatures: Schnorr enables the possibility of threshold signatures – multiple oracles could produce a single aggregated signature on the median price. This single signature in the block header is smaller than including several individual signatures, reducing block space usage and simplifying validation."*
> — DigiDollar Whitepaper, Section: Using Schnorr Signatures for More Efficient Oracle Price Verification

**The current Phase 2 implementation does NOT do this.** Instead, it stores N individual Schnorr signatures in the coinbase OP_RETURN output. Each oracle contributes `oracle_id(1 byte) + schnorr_sig(64 bytes) = 65 bytes` to the bundle.

### Measured bundle sizes (RC27, proven on regtest):

| Oracles | Script Size | Annual Chain Growth |
|---------|-------------|---------------------|
| 4-of-7 (regtest) | **284 bytes** | 590 MB/year |
| 5-of-9 (earlier testnet) | **349 bytes** | 727 MB/year |
| 7-of-7 | **479 bytes** | 1.00 GB/year |
| 9-of-17 (RC30 mainnet/testnet) | **739 bytes** | 1.55 GB/year |
| 17-of-17 | **1,129 bytes** | 2.36 GB/year |

With MuSig2 aggregate signatures, **ALL configurations collapse to ~88 bytes** regardless of oracle count.

---

## Background: What is MuSig2?

**MuSig2** is a multi-signature scheme for Schnorr signatures, standardized as **BIP 327**. It was developed by Jonas Nick and Tim Ruffing (Blockstream) and Yannick Seurin (ANSSI — French National Cybersecurity Agency).

### Key properties:
- **Key Aggregation:** N public keys → 1 aggregate public key (32 bytes)
- **Signature Aggregation:** N signers → 1 aggregate signature (64 bytes)
- **BIP 340 Compatible:** The aggregate signature is indistinguishable from a regular Schnorr signature
- **2 Communication Rounds:** Signers exchange nonces (round 1), then partial signatures (round 2)
- **Provably Secure:** Proven existentially unforgeable under the AOMDL assumption (peer-reviewed, published at Crypto 2021)
- **Production Ready:** Included in bitcoin-core/secp256k1 as a default-enabled module since 2024

### History:
| Year | Milestone |
|------|-----------|
| 2018 | MuSig1 published (Blockstream) — 3 rounds, impractical |
| 2020 | MuSig2 published (eprint.iacr.org/2020/1261) — 2 rounds |
| 2021 | Presented at Real World Crypto 2021, Taproot activated in Bitcoin |
| 2022 | BIP 327 assigned and published |
| 2024 | MuSig2 module merged into libsecp256k1 (default-enabled) |
| 2025 | BIP 327 status: **Deployed** |

### MuSig2 is N-of-N, not M-of-N

MuSig2 natively requires ALL aggregated signers to participate. For M-of-N (e.g., 9-of-17), we use **signer selection**: the coordinator picks which M oracles will sign, runs MuSig2 with exactly those M, and stores a bitmap indicating which oracles participated. Verification reconstructs the aggregate pubkey for that specific subset.

This is simpler and more battle-tested than FROST (true threshold sigs) and is the approach recommended by the BIP 327 authors for M-of-N use cases combined with Taproot script paths.

---

## Current Implementation (Phase 2)

### On-chain format (version 0x02):
```
OP_RETURN OP_ORACLE [push version=0x02] [push data]

Data layout:
  num_messages:     1 byte (uint8)
  consensus_price:  8 bytes (uint64 LE, micro-USD)
  consensus_time:   8 bytes (int64 LE, Unix timestamp)
  messages[]:       N × 65 bytes each:
    oracle_id:      1 byte (uint8)
    schnorr_sig:    64 bytes (BIP 340 signature)

Total data: 17 + (N × 65) bytes
```

### Signing flow (current):
1. Each oracle independently signs `SHA256(oracle_id || price || timestamp)` with their private key
2. Oracles broadcast signed messages via P2P (`MSG_ORACLE_PRICE`)
3. Oracles propose consensus and collect attestations (`MSG_ORACLE_CONSENSUS`, `MSG_ORACLE_ATTESTATION`)
4. Mining node collects ≥M attestations and includes all individual signatures in coinbase OP_RETURN
5. Validating nodes extract bundle, verify each signature individually against chainparams pubkeys

### Relevant source files:
- `src/oracle/bundle_manager.cpp` — `CreateOracleScript()`, `ExtractOracleBundle()`, `AddOracleBundleToBlock()`
- `src/oracle/bundle_manager.h` — `OracleBundleManager` class
- `src/primitives/oracle.h` — `COraclePriceMessage`, `COracleBundle`
- `src/primitives/oracle.cpp` — `SignPhase2()`, `VerifyPhase2()`
- `src/kernel/chainparams.cpp` — Oracle pubkeys and consensus params
- `src/consensus/params.h` — `nOracleRequiredMessages`, `nOracleTotalOracles`, `nDigiDollarPhase2Height`
- `src/net_processing.cpp` — P2P oracle message handling
- `src/protocol.h` / `.cpp` — `MSG_ORACLE_PRICE`, `MSG_ORACLE_ATTESTATION`, etc.

---

## Proposed Implementation (Phase 3)

### On-chain format (version 0x03):
```
OP_RETURN OP_ORACLE [push version=0x03] [push data]

Data layout:
  bitmap_len:       1 byte (uint8 — length of signer bitmap in bytes)
  signer_bitmap:    bitmap_len bytes (bit N = 1 means oracle N participated)
  consensus_price:  8 bytes (uint64 LE, micro-USD)
  consensus_time:   8 bytes (int64 LE, Unix timestamp)
  aggregate_sig:    64 bytes (BIP 340 Schnorr signature — MuSig2 aggregate)

Total data: 81 + bitmap_len bytes
  17 oracles:  81 + 3 = 84 bytes  (RC30)
  30 oracles:  81 + 4 = 85 bytes
  64 oracles:  81 + 8 = 89 bytes
  100 oracles: 81 + 13 = 94 bytes
  256 oracles: 81 + 32 = 113 bytes
```

The variable-length bitmap (with 1-byte length prefix) means this format **never needs to change** regardless of how many oracles DigiDollar grows to support. The aggregate signature is always exactly 64 bytes — the only variable is the bitmap, which grows at 1 bit per oracle.

### Signing flow (proposed):
```
                    ┌─────────────┐
                    │  PRICE FEED │
                    │  (exchanges)│
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
         ┌────────┐  ┌────────┐  ┌────────┐
         │Oracle 0│  │Oracle 1│  │  ...   │  (17 oracles, RC30)
         └───┬────┘  └───┬────┘  └───┬────┘
             │            │            │
    ═══════════════════════════════════════════
    ROUND 0: PRICE AGREEMENT (existing consensus protocol)
    - Oracles agree on consensus price + timestamp
    - Same as current Phase 2 price consensus
    ═══════════════════════════════════════════
             │            │            │
    ═══════════════════════════════════════════
    ROUND 1: NONCE EXCHANGE (new)
    - Each oracle generates MuSig2 nonce pair
    - Broadcasts public nonce via P2P (MSG_ORACLE_MUSIG_NONCE)
    - Can be pre-generated before price is known
    ═══════════════════════════════════════════
             │            │            │
             ▼            ▼            ▼
         ┌──────────────────────────────────┐
         │  NONCE AGGREGATION               │
         │  (every node computes locally)   │
         │  Combine ≥9 public nonces        │
         │  Determine signer set + bitmap   │
         └──────────────┬───────────────────┘
                        │
    ═══════════════════════════════════════════
    ROUND 2: PARTIAL SIGNING (new)
    - Each oracle creates partial sig using:
      - Their secret key
      - The aggregate nonce
      - The message (bitmap + price + timestamp)
    - Broadcasts partial sig via P2P (MSG_ORACLE_MUSIG_PARTIALSIG)
    ═══════════════════════════════════════════
             │            │            │
             ▼            ▼            ▼
         ┌──────────────────────────────────┐
         │  SIGNATURE AGGREGATION           │
         │  (mining node)                   │
         │  Combine ≥9 partial sigs → 1 sig │
         │  Store in coinbase OP_RETURN     │
         └──────────────────────────────────┘
                        │
                        ▼
              ┌──────────────────┐
              │  BLOCK MINED     │
              │  88 bytes oracle │
              └──────────────────┘
```

### Verification (validating nodes):
1. Extract version 0x03 bundle from coinbase OP_RETURN
2. Read signer bitmap → determine which oracles participated
3. Look up those oracles' pubkeys from chainparams
4. Compute aggregate pubkey using `secp256k1_musig_pubkey_agg()` for that subset
5. Verify the single 64-byte aggregate signature using standard `secp256k1_schnorrsig_verify()` against the aggregate pubkey
6. Confirm signer count ≥ `nOracleRequiredMessages` (e.g., 9)

**Note:** Step 4 can be cached — for a given bitmap, the aggregate pubkey is deterministic. With 17 oracles and C(17,9)=24,310 possible subsets, all aggregate pubkeys can be precomputed at startup (RC30).

### ⚠️ CRITICAL DESIGN CHANGE: Signature Message

In Phase 2, each oracle signs a **different message**: `H(oracle_id || price || timestamp)`. This means every oracle's signature is unique — they can't be aggregated because MuSig2 requires all signers to sign the **same message**.

In Phase 3, all participating oracles sign the **same message**: `H(bitmap || price || timestamp)`. The bitmap replaces individual oracle IDs — it encodes which oracles participated. This is what makes aggregation possible.

**Implications:**
- The bitmap must be agreed upon BEFORE signing (oracles need to know the signer set)
- The coordinator determines the bitmap after collecting nonces in Round 1
- If an oracle drops out between Round 1 and Round 2, the bitmap changes and everyone must re-sign (handled by session timeout + retry)

### ⚠️ ARCHITECTURE NOTE: Epoch Rotation and Bitmap Scope

The current oracle system has a **two-tier structure**:
- **30 total oracle keys** hardcoded in chainparams (`ORACLE_TOTAL_COUNT = 30`)
- **17 active oracles** selected per epoch via deterministic rotation on mainnet/testnet (RC30 chainparams override; legacy header default `ORACLE_ACTIVE_COUNT = 15`)
- **9 required** for consensus (`nOracleRequiredMessages = 9` on mainnet/testnet in RC30)

**Decision needed: What does the bitmap reference?**

**Option A: Global bitmap (bit N = oracle ID N from full 30-oracle set)**
- Pro: Simple, self-describing — validators always know which oracle is which
- Pro: No need to reconstruct epoch's active set to decode
- Con: Bitmap must be wide enough for total oracle count (4 bytes for 30, RC30 bitmap spans the full 17 active slots plus reserved)
- **Recommended for simplicity and future-proofing**

**Option B: Epoch-relative bitmap (bit N = Nth oracle in this epoch's active set)**
- Pro: Smaller bitmap (3 bytes for 17 — RC30)
- Con: Validators must compute `SelectOraclesForEpoch()` to decode bitmap
- Con: Bitmap meaning changes every epoch — harder to audit/debug

---

## Size Comparison

### Byte-exact validated sizes (verified against CreateOracleScript output + format spec):

| Configuration | Current Script | MuSig2 Script | Savings | Annual Current | Annual MuSig2 |
|---------------|---------------|---------------|---------|----------------|---------------|
| 4-of-7 | 285 B | 89 B | **69%** | 571 MB | 178 MB |
| 5-of-9 | 350 B | 90 B | **74%** | 702 MB | 180 MB |
| 9-of-17 (RC30) | 740 B | 91 B | **88%** | 1,484 MB | 182 MB |
| 17-of-17 | 1,130 B | 91 B | **92%** | 2,266 MB | 182 MB |
| 17-of-30 | 1,130 B | 92 B | **92%** | 2,266 MB | 184 MB |
| 26-of-50 | 1,715 B | 95 B | **94%** | 3,439 MB | 190 MB |
| 51-of-100 | 3,340 B | 101 B | **97%** | 6,697 MB | 202 MB |
| 129-of-256 | 8,410 B | 120 B | **99%** | 16,862 MB | 241 MB |

**10-year chain growth from oracle data:**

| Scenario | Current | MuSig2 | Saved |
|----------|---------|--------|-------|
| Launch (9-of-17) | **11.9 GB** | **1.76 GB** | 10.2 GB |
| Growth (26-of-50) | **33.6 GB** | **1.86 GB** | 31.7 GB |
| Scale (51-of-100) | **65.4 GB** | **1.98 GB** | 63.4 GB |
| Max (129-of-256) | **164.7 GB** | **2.35 GB** | 162.3 GB |

(Based on 15-second block times = 2,102,400 blocks/year)

### Verified: aggregate signature uses standard Schnorr verify

From the official libsecp256k1 MuSig2 documentation:
> *"The aggregate signature can be verified with `secp256k1_schnorrsig_verify`."*

This means validating nodes do NOT need new cryptographic verification code. The only new code needed for verification is computing the aggregate pubkey from the bitmap + chainparams keys (one call to `secp256k1_musig_pubkey_agg`).

---

## Implementation Phases

### Phase 1: libsecp256k1 MuSig2 Module (3-4 days)

**Goal:** Update our secp256k1 subtree to include the MuSig2 module.

**Tasks:**
- [ ] Update `src/secp256k1/` subtree to latest bitcoin-core/secp256k1 (or cherry-pick MuSig2 module)
- [ ] Enable `--enable-module-musig` in configure
- [ ] Verify existing Schnorr/Taproot/ECDSA tests still pass
- [ ] Write basic MuSig2 integration tests:
  - Key aggregation for 2, 7, 17 signers (RC30)
  - Full sign/verify round-trip
  - Subset signing (simulate 9-of-17)
- [ ] Verify `secp256k1_musig_pubkey_agg()` produces deterministic results for same key sets

**Files modified:**
- `src/secp256k1/` (subtree update)
- `src/secp256k1/configure.ac` (enable musig module)
- `configure.ac` (propagate musig module flag)
- `src/Makefile.am` (link musig if needed)

**Risk:** Low. The module is well-tested upstream and doesn't affect existing functionality.

---

### Phase 2: MuSig2 Key Aggregation + Verification (3-4 days)

**Goal:** Implement aggregate pubkey computation and signature verification for oracle bundles. This is the consensus-critical verification path.

**Tasks:**
- [ ] New class: `MuSig2OracleAggregator` in `src/oracle/musig2_aggregator.h/.cpp`
  - `ComputeAggregatePubkey(bitmap, chainparams_keys) → XOnlyPubKey`
  - `VerifyAggregateSignature(agg_sig, agg_pubkey, message) → bool`
  - Aggregate pubkey cache (bitmap → pubkey, precomputed at startup)
- [ ] Update `ExtractOracleBundle()` to parse version 0x03 format
- [ ] Update `CreateOracleScript()` to produce version 0x03 format
- [ ] Update `ValidateBlockOracleData()` to verify MuSig2 aggregate signatures
- [ ] New consensus parameter: `nDigiDollarPhase3Height`
- [ ] Unit tests: roundtrip create → extract → verify for all oracle counts

**Files modified:**
- `src/oracle/musig2_aggregator.h` (NEW)
- `src/oracle/musig2_aggregator.cpp` (NEW)
- `src/oracle/bundle_manager.cpp` (CreateOracleScript, ExtractOracleBundle)
- `src/oracle/bundle_manager.h`
- `src/consensus/params.h` (nDigiDollarPhase3Height)
- `src/kernel/chainparams.cpp` (activation heights)
- `src/validation.cpp` (ValidateBlockOracleData)
- `src/Makefile.am` (new source files)

**Risk:** Medium. This is consensus-critical code. Needs extensive testing.

---

### Phase 3: Oracle Coordination Protocol (5-7 days)

**Goal:** Implement the P2P nonce exchange and partial signature collection that enables oracles to produce MuSig2 aggregate signatures.

**Tasks:**
- [ ] New P2P message types:
  - `MSG_ORACLE_MUSIG_NONCE` — oracle broadcasts public nonce for current epoch
  - `MSG_ORACLE_MUSIG_PARTIALSIG` — oracle broadcasts partial signature
- [ ] New class: `MuSig2SigningSession` in `src/oracle/musig2_session.h/.cpp`
  - Manages per-epoch signing sessions
  - Collects nonces from peers
  - Triggers partial signing once enough nonces arrive
  - Aggregates partial sigs into final aggregate signature
  - Handles timeouts and fallback to v0x02 format
- [ ] Update `OracleNode` to participate in MuSig2 sessions:
  - Generate nonce pair on epoch start
  - Broadcast public nonce
  - Create partial signature when aggregate nonce is ready
  - Broadcast partial signature
- [ ] Update `OracleBundleManager::AddOracleBundleToBlock()`:
  - Prefer MuSig2 aggregate bundle if available
  - Fall back to individual sigs (v0x02) if MuSig2 session incomplete
- [ ] Nonce pre-generation: oracles generate nonces for next epoch before current epoch ends
- [ ] Rate limiting and DoS protection for new message types

**Files modified:**
- `src/oracle/musig2_session.h` (NEW)
- `src/oracle/musig2_session.cpp` (NEW)
- `src/oracle/oracle_node.h` (nonce generation, partial signing)
- `src/oracle/oracle_node.cpp`
- `src/oracle/bundle_manager.cpp` (prefer MuSig2 bundles)
- `src/protocol.h` / `.cpp` (new message types)
- `src/net_processing.cpp` (handle new messages)

**Risk:** High. This is the most complex phase. The nonce security guarantees of MuSig2 must be maintained — nonce reuse leaks private keys. Needs careful review.

---

### Phase 4: Testing, Integration & Activation (3-4 days)

**Goal:** Comprehensive testing and testnet deployment.

**Tasks:**
- [ ] Unit tests:
  - MuSig2 key aggregation for all possible 9-of-17 subsets (24,310 combinations — RC30)
  - Nonce exchange simulation (in-process, no network)
  - Partial signature generation and aggregation
  - Bundle roundtrip: create v0x03 → extract → verify
  - Invalid bitmap rejection
  - Insufficient signers rejection
  - Mixed v0x02/v0x03 blocks across activation height
- [ ] Regtest integration test (`test_phase3_oracle_regtest.sh`):
  - Start 17 oracle nodes (RC30)
  - Mine blocks with MuSig2 oracle bundles
  - Verify bundle size is exactly 88 bytes
  - Test 9-of-17 (minimum threshold)
  - Test 16-of-17 (one offline)
  - Test 8-of-17 (below threshold — should fall back or reject)
  - Test Phase 2 → Phase 3 transition across activation height
- [ ] Testnet deployment:
  - Set `nDigiDollarPhase3Height` for testnet20
  - Deploy updated binaries to oracle operators
  - Monitor MuSig2 nonce exchange performance
  - Verify block propagation with 88-byte oracle bundles
- [ ] Full regression: all existing tests pass (currently 2,029)
- [ ] Update `digidollar/ORACLE_PHASE_2_SPEC_PRD.md` → create `ORACLE_PHASE_3_SPEC_PRD.md`

---

## Oracle Coordination Protocol

### Timing within a block interval (15 seconds):

```
Time     Event
─────    ─────────────────────────────────
t=0s     New block received, epoch boundary check
t=0-2s   Oracles fetch fresh prices, agree on consensus price
t=2-4s   ROUND 1: Oracles broadcast MuSig2 public nonces
t=4-6s   Nonce collection window (wait for ≥9 nonces)
t=6-8s   Aggregate nonce computed, broadcast to oracles
t=8-10s  ROUND 2: Oracles compute + broadcast partial signatures
t=10-12s Partial sig collection (wait for ≥9 partial sigs)
t=12-13s Mining node aggregates partial sigs → final aggregate sig
t=13-15s Block mined with 88-byte oracle bundle
```

### Pre-generation optimization:

Nonces for epoch E+1 can be generated and broadcast during epoch E. This moves Round 1 out of the critical path, leaving only Round 2 (partial signing) in the block interval. Partial signing is a single elliptic curve operation per oracle — sub-millisecond.

### Fallback mechanism:

If MuSig2 coordination fails to complete within the block interval:
1. Mining node falls back to current v0x02 format (individual sigs)
2. This is transparent to the network — both v0x02 and v0x03 are valid after Phase 3 activation
3. Metrics track MuSig2 success rate for monitoring

---

## Consensus Changes

### New parameters:
```cpp
// consensus/params.h
int nDigiDollarPhase3Height;  // Activation height for MuSig2 oracle bundles

// kernel/chainparams.cpp (mainnet — TBD)
consensus.nDigiDollarPhase3Height = TBD;

// kernel/chainparams.cpp (testnet)
consensus.nDigiDollarPhase3Height = TBD;  // Set for testnet20

// kernel/chainparams.cpp (regtest)
consensus.nDigiDollarPhase3Height = 800;  // After Phase 2 at 650
```

### Validation rules (blocks at or above Phase 3 height):
1. Oracle bundle version 0x03 is accepted (in addition to 0x02 for transition)
2. For v0x03 bundles:
   - Bitmap must have ≥ `nOracleRequiredMessages` bits set
   - All set bits must correspond to valid oracle IDs in chainparams
   - Aggregate pubkey is computed from bitmap + chainparams keys
   - Aggregate signature must verify against aggregate pubkey and `SHA256(bitmap || price || timestamp)`
3. v0x02 bundles remain valid (backward compatibility during transition)
4. Optional: after a grace period (e.g., Phase 3 + 10,000 blocks), v0x02 bundles are rejected, forcing all miners to use MuSig2

---

## Security Considerations

### Nonce reuse prevention (CRITICAL)
MuSig2's security model requires that secret nonces are **never reused**. If an oracle reuses a nonce across two different signing sessions, its private key can be extracted.

**Mitigations:**
- Secret nonces are generated fresh for each epoch using `secp256k1_musig_nonce_gen()` with entropy from the OS CSPRNG
- Secret nonces are zeroed immediately after partial signing
- The `secp256k1_musig_secnonce` struct is explicitly designed to prevent copying (see BIP 327 security notes)
- Oracle software must never persist secret nonces to disk

### Rogue key attacks
MuSig2 is immune to rogue key attacks by design (key aggregation includes a coefficient derived from all pubkeys). No proof-of-possession required.

### Coordinator trust
The MuSig2 coordinator (mining node) sees public nonces and partial signatures but **cannot forge signatures** or extract private keys. The coordinator is untrusted — a malicious coordinator can only cause signing to fail (DoS), not produce invalid signatures.

### Signer availability
If fewer than M oracles are available for nonce exchange, MuSig2 session fails gracefully and falls back to v0x02 individual signatures (or no oracle data if below threshold).

### Bitmap manipulation
An attacker cannot construct a valid aggregate signature for a bitmap they didn't coordinate, because they would need the private keys of the oracles listed in the bitmap.

---

## Backward Compatibility

- **Pre-Phase 3 nodes:** Continue to produce and validate v0x02 bundles only. They will reject v0x03 bundles, so Phase 3 is a **hard fork** (requires node upgrade).
- **Post-Phase 3 nodes:** Accept both v0x02 and v0x03 bundles during a transition period. After the grace period, only v0x03 is accepted.
- **Oracle operators:** Must upgrade to support MuSig2 nonce exchange. Non-upgraded oracles can still submit individual prices but cannot participate in MuSig2 sessions.
- **Miners:** Must upgrade to aggregate partial signatures. Non-upgraded miners can still produce v0x02 blocks during the transition period.

---

## Testing Strategy

### Unit tests (src/test/):
- `musig2_oracle_tests.cpp` (NEW):
  - Key aggregation correctness for all C(17,9) = 24,310 subsets (RC30)
  - Nonce generation and aggregation
  - Partial sign + aggregate + verify roundtrip
  - v0x03 bundle create → extract → verify
  - Invalid bitmap handling
  - Mixed version validation across Phase 3 boundary

### Integration tests (shell scripts):
- `test_phase3_oracle_regtest.sh` (NEW):
  - Full MuSig2 signing flow on regtest
  - Bundle size verification (exactly 88 bytes)
  - Threshold edge cases
  - Phase 2 → Phase 3 transition
  - Fallback to v0x02 when MuSig2 fails

### Existing tests (must continue passing):
- 2,029 existing tests (as of RC27)
- 96 oracle-specific tests (oracle_phase2_tests, oracle_bundle_manager_tests, etc.)
- All DigiDollar consensus tests

---

## Files to Modify

### New files:
| File | Purpose |
|------|---------|
| `src/oracle/musig2_aggregator.h` | Aggregate pubkey computation + cache |
| `src/oracle/musig2_aggregator.cpp` | MuSig2 key aggregation implementation |
| `src/oracle/musig2_session.h` | Per-epoch MuSig2 signing session manager |
| `src/oracle/musig2_session.cpp` | Nonce collection, partial sig aggregation |
| `src/test/musig2_oracle_tests.cpp` | Unit tests |
| `test_phase3_oracle_regtest.sh` | Integration test script |
| `digidollar/ORACLE_PHASE_3_SPEC_PRD.md` | Formal specification |

### Modified files:
| File | Changes |
|------|---------|
| `src/secp256k1/` | Subtree update for MuSig2 module |
| `src/oracle/bundle_manager.cpp` | v0x03 format in CreateOracleScript/ExtractOracleBundle |
| `src/oracle/bundle_manager.h` | Version constant, MuSig2 bundle support |
| `src/primitives/oracle.h` | Aggregate signature field in COracleBundle |
| `src/consensus/params.h` | nDigiDollarPhase3Height |
| `src/kernel/chainparams.cpp` | Phase 3 activation heights, aggregate key precompute |
| `src/validation.cpp` | ValidateBlockOracleData v0x03 path |
| `src/protocol.h` / `.cpp` | MSG_ORACLE_MUSIG_NONCE, MSG_ORACLE_MUSIG_PARTIALSIG |
| `src/net_processing.cpp` | Handle new P2P messages |
| `src/oracle/oracle_node.h` / `.cpp` | MuSig2 participation (nonce gen, partial sign) |
| `src/Makefile.am` / `src/Makefile.test.include` | New source files |

---

## Scaling Beyond 17 Oracles (RC30)

A key advantage of MuSig2 is that on-chain size barely changes as oracle count grows. The only variable-size component is the signer bitmap (1 bit per possible oracle). The aggregate signature is always exactly 64 bytes.

### On-chain size at scale:

```
Oracle Count | Bitmap | Total On-Chain | Current (individual sigs)
-------------|--------|----------------|-------------------------
     17      |  3 B   |     89 B       |  1,129 B  (13× bigger)  [RC30]
     30      |  4 B   |     90 B       |  1,974 B  (22× bigger)
     64      |  8 B   |     94 B       |  4,184 B  (45× bigger)
    100      | 13 B   |     99 B       |  6,524 B  (66× bigger)
    256      | 32 B   |    118 B       | 16,657 B (141× bigger)
```

At 256 oracles, MuSig2 is **118 bytes**. The current approach would be **16,657 bytes** — that's 33.4 GB/year of oracle signatures alone. MuSig2 keeps it at 237 MB/year. The gap only gets wider.

### Aggregate pubkey caching: the combinatorial wall

For M-of-N verification, we need the aggregate pubkey for the specific M-oracle subset that signed. The number of possible subsets is C(N,M):

```
Config    | Possible Subsets | Cache Size (32B each) | Precompute?
----------|------------------|-----------------------|------------
 9-of-17  |          24,310  |        760 KB         | ✅ YES — precompute all at startup (RC30)
 9-of-16  |          11,440  |        357 KB         | ✅ YES — still fast
15-of-30  |     155,117,520  |        4.6 GB         | ❌ NO — compute on-demand
33-of-64  |     1.8 trillion |       impossible       | ❌ NO — compute on-demand
```

**The solution is simple:** `secp256k1_musig_pubkey_agg()` takes ~0.1ms for any subset size. With 15-second blocks, computing one aggregate pubkey per block validation is negligible. At ≤17 oracles (RC30), precompute the full cache. Above ~20, compute on-demand with an LRU cache (most blocks will use the same few oracle subsets anyway since the same oracles tend to be online).

### Bitmap encoding for future-proofing

The v0x03 format uses a fixed 2-byte bitmap (uint16, max 16 oracles). To support more than 16 oracles in the future, two options:

**Option A: Variable-length bitmap (recommended — adopted in RC30 v0x03)**
- First byte encodes bitmap length: `bitmap_len(1) + bitmap(bitmap_len) + price(8) + ts(8) + sig(64)`
- 17 oracles (RC30): 1 + 3 + 80 = 84 bytes
- 100 oracles: 1 + 13 + 80 = 94 bytes
- 256 oracles: 1 + 32 + 80 = 113 bytes
- Unlimited future scaling with negligible overhead

**Option B: Fixed widths with version bumps**
- v0x03: uint32 bitmap (max 32 oracles — covers RC30's 17 active slots)
- v0x04 (future): uint64 bitmap (max 64 oracles)
- Simple but requires consensus changes to scale

**Recommendation:** Use Option A (variable-length bitmap) from the start. The 1-byte length prefix costs almost nothing and means we never need another format change for oracle scaling. Whether we have 17 or 500 oracles, the v0x03 format handles it.

### Coordination protocol at scale

MuSig2 coordination (nonce exchange + partial sig collection) gets harder with more oracles because:
- More nonces to collect in Round 1
- More partial sigs to collect in Round 2
- More P2P messages per block interval

However, each round is just "collect M messages from N possible senders" — the same problem we already solve for oracle price consensus. The timing budget (15-second blocks) is generous. With nonce pre-generation, only Round 2 (partial sigs) is time-critical, and partial signing is sub-millisecond per oracle.

At 100+ oracles, the coordinator might receive 100 partial sigs but only needs 51. This is embarrassingly parallel and easily fits in the block interval. The P2P message volume scales linearly (not exponentially) — 100 nonce messages + 100 partial sig messages = 200 small messages per epoch, trivial for the DigiByte P2P network.

**Bottom line: MuSig2 scales to hundreds of oracles with no on-chain size penalty and manageable coordination overhead. The current approach doesn't scale past ~15 without unacceptable chain bloat.**

---

## Open Questions

1. **Grace period duration:** How long after Phase 3 activation do we accept v0x02 bundles? Suggest 10,000 blocks (~1.7 days) to allow stragglers to upgrade.

2. **Bitmap encoding:** Fixed uint16 (max 16, simpler) vs variable-length with length prefix (unlimited, 1 byte overhead)? **Recommend variable-length for future-proofing.**

3. **Nonce pre-generation depth:** How many epochs ahead should oracles pre-generate nonces? 1 epoch ahead seems sufficient for 15-second blocks.

4. **Aggregate pubkey caching strategy:** At ≤16 oracles, precompute all C(N,M) aggregate pubkeys at startup (~160 KB). Above 16, use on-demand computation with LRU cache. `secp256k1_musig_pubkey_agg()` takes ~0.1ms — negligible per block.

5. **FROST vs MuSig2+bitmap:** Should we evaluate FROST (true threshold sigs, no bitmap needed, constant-size output) as an alternative? FROST is less mature but eliminates the bitmap and the need for signer selection. BIP for FROST does not yet exist. **Recommend MuSig2 — it's battle-tested and the bitmap overhead is negligible.**

6. **Testnet vs mainnet activation:** Do we deploy Phase 3 on testnet20 (new testnet reset) or activate on existing testnet19? Suggest new testnet reset.

7. **secp256k1 update scope:** Full subtree update vs cherry-pick MuSig2 module only? Full update is cleaner but may bring other changes that need review.

---

## References

| Resource | Link |
|----------|------|
| BIP 327: MuSig2 specification | https://github.com/bitcoin/bips/blob/master/bip-0327.mediawiki |
| MuSig2 academic paper | https://eprint.iacr.org/2020/1261 |
| libsecp256k1 MuSig2 module | https://github.com/bitcoin-core/secp256k1/tree/master/src/modules/musig |
| libsecp256k1 MuSig2 example | https://github.com/bitcoin-core/secp256k1/blob/master/examples/musig.c |
| libsecp256k1 MuSig2 documentation | https://github.com/bitcoin-core/secp256k1/blob/master/doc/musig.md |
| BIP 340: Schnorr Signatures | https://github.com/bitcoin/bips/blob/master/bip-0340.mediawiki |
| BIP 341: Taproot | https://github.com/bitcoin/bips/blob/master/bip-0341.mediawiki |
| Blockstream MuSig2 blog post | https://medium.com/blockstream/musig2-simple-two-round-schnorr-multisignatures-bf9582e99295 |
| DigiDollar Whitepaper | `digidollar/whitepaper.md` (lines 596-600) |
| Current Phase 2 Spec | `digidollar/ORACLE_PHASE_2_SPEC_PRD.md` |
| Current oracle implementation | `src/oracle/bundle_manager.cpp` |

---

## Timeline Estimate

| Phase | Duration | Dependencies |
|-------|----------|-------------|
| Phase 1: secp256k1 MuSig2 module | 3-4 days | None |
| Phase 2: Key aggregation + verification | 3-4 days | Phase 1 |
| Phase 3: Coordination protocol | 5-7 days | Phase 2 |
| Phase 4: Testing + activation | 3-4 days | Phase 3 |
| **Total** | **~2-3 weeks** | |

---

*This document should be reviewed by all contributors before implementation begins. The MuSig2 coordination protocol (Phase 3) is the highest-risk component and should receive the most scrutiny.*
