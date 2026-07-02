# DigiByte & DigiDollar Post-Quantum Cryptography Plan
**Version:** 2.0 — March 31, 2026  
**Author:** Irene (DigiByte PQC Research)  
**Catalyst:** [Google Quantum AI Paper](https://quantumai.google/static/site-assets/downloads/cryptocurrency-whitepaper.pdf) — March 30, 2026  
**Status:** DRAFT — Requires Jared's review before any implementation begins

---

## TL;DR

Google demonstrated that **500,000 physical qubits** can break secp256k1 ECDSA in **minutes** — a 20× reduction from previous estimates. Their internal migration deadline is **2029**. DigiByte uses secp256k1 for all transaction signatures. We need to add post-quantum cryptography (PQC) before quantum computers can crack our keys.

**The good news:** PoW mining is NOT threatened. Hash functions are safe. UTXO-model chains are structurally better positioned than account-model chains. DigiByte's 15-second blocks make on-spend attacks 40× harder than Bitcoin.

**The critical issue:** DigiDollar's collateral lockup mechanism MUST NOT expose raw public keys in locking scripts. Long-duration lockups (>2 years) are unsafe without PQC.

**The opportunity:** DigiDollar hasn't launched yet — we can build PQC in from day one. DigiByte can ship PQC before Bitcoin (BIP-360 is still just a proposal).

---

**Companion doc:** [`DGB_PQC_LANDSCAPE.md`](DGB_PQC_LANDSCAPE.md) — Full industry landscape (7 Bitcoin proposals, Ethereum/Solana/QRL/XRP analysis, burn-vs-steal debate, algorithm deep dive, migration pathways)

---

## Table of Contents

1. [The Threat](#1-the-threat)
2. [What's Safe, What's Not](#2-whats-safe-whats-not)
3. [Algorithm Selection](#3-algorithm-selection)
4. [DigiDollar: The Lockup Problem](#4-digidollar-the-lockup-problem)
5. [DigiDollar: PQC from Day One](#5-digidollar-pqc-from-day-one)
6. [DigiByte Core: Revised Two-Phase Soft Fork Plan](#6-digibyte-core-soft-fork-plan)
7. [Transaction Size Impact](#7-transaction-size-impact)
8. [Implementation Roadmap](#8-implementation-roadmap)
9. [Mining Impact](#9-mining-impact)
10. [Open Questions](#10-open-questions)

---

## 1. The Threat

### Google's Key Findings (March 30, 2026)

| Metric | Value |
|--------|-------|
| Physical qubits to break secp256k1 | **<500,000** |
| Logical qubits needed | **1,200–1,450** |
| Time to crack one key | **9–23 minutes** (superconducting) |
| Improvement over prior estimates | **20× fewer qubits** |
| Google's internal migration deadline | **2029** |

### Three Attack Types

1. **On-Spend Attack:** Intercept a transaction in the mempool, crack the pubkey before it confirms. Requires speed.
   - Bitcoin window: ~10 minutes → **feasible** with a fast-clock CRQC
   - **DigiByte window: ~15 seconds → extremely difficult** even with future CRQCs
   
2. **At-Rest Attack:** Crack a public key that's been sitting exposed on-chain. Unlimited time.
   - Affects: P2PK outputs, reused addresses, exposed pubkeys in scripts
   - **This is the main threat to timelocked DigiDollar collateral**

3. **On-Setup Attack:** Break protocol-level cryptographic parameters for reusable exploits.
   - **Does NOT affect DigiByte** (no trusted setups, no BLS, no zk-SNARKs)

### Timeline Estimate

| When | Who Has CRQCs | Risk Level |
|------|--------------|------------|
| Now–2028 | Nobody | Low |
| 2028–2030 | Nation-states (Google, NSA, China) | Medium |
| 2030–2033 | Well-funded private actors | High |
| 2033+ | Commercially available | Critical |

**Conservative planning horizon: assume 2029 for nation-state capability, 2032 for broad availability.**

---

## 2. What's Safe, What's Not

### ✅ SAFE — Not Threatened by Quantum

| Component | Why It's Safe |
|-----------|--------------|
| **PoW Mining (all 5 algorithms)** | Grover's gives only √N speedup, consumed by error correction. A quantum miner achieves ~0.25 TH/s vs an ASIC's 110+ TH/s. Safe for decades. |
| **SHA-256d (block hashing)** | 128-bit quantum security (Grover halves effective bits). Still extremely strong. |
| **Scrypt, Qubit, Skein, Odocrypt** | Same — hash functions resist quantum. |
| **Merkle trees** | SHA-256d based. Quantum-safe. |
| **Block headers** | No signatures, only PoW hash. Safe. |
| **P2PKH addresses (unspent, never reused)** | Pubkey hidden behind RIPEMD160(SHA256(pubkey)). ~80-bit quantum security on the hash. Adequate. |

### ❌ VULNERABLE — Needs PQC Upgrade

| Component | Why It's Vulnerable | Urgency |
|-----------|-------------------|---------|
| **Transaction signatures (ECDSA secp256k1)** | Shor's algorithm directly solves ECDLP | HIGH |
| **P2PK outputs (raw pubkey in script)** | Pubkey permanently exposed | CRITICAL |
| **Reused P2PKH addresses** | Pubkey exposed from first spend | HIGH |
| **Timelocked UTXOs with raw pubkeys** | Pubkey exposed for lockup duration | CRITICAL |
| **Taproot keypath spends** | Pubkey exposed | HIGH |
| **Digi-ID authentication** | Uses ECDSA challenge-response | MEDIUM |
| **DigiAssets issuance keys** | Long-lived ECDSA keys | MEDIUM |

### ⚠️ The Dormant UTXO Problem

Any UTXO that never moves to a PQC address is permanently at risk once CRQCs exist. This includes lost coins, dormant wallets, and Satoshi-era equivalent outputs. The community will eventually need a sunset policy for ECDSA-only UTXOs.

---

## 3. Algorithm Selection

### NIST-Standardized PQC Signature Algorithms

| Algorithm | FIPS | Type | Public Key | Signature | vs ECDSA Sig | Verify Speed | Status |
|-----------|------|------|-----------|-----------|-------------|-------------|--------|
| **secp256k1 ECDSA** | — | ECC | 33 B | 72 B | baseline | ~100 µs | CURRENT |
| **ML-DSA-44** | 204 | Lattice (MLWE) | 1,312 B | 2,420 B | **33×** | ~40 µs ⚡ | NIST Final |
| **ML-DSA-65** | 204 | Lattice (MLWE) | 1,952 B | 3,309 B | 46× | ~50 µs | NIST Final |
| **FN-DSA-512 (Falcon)** | 206 | Lattice (NTRU) | 897 B | ~666 B | **9×** | ~40 µs ⚡ | NIST Final |
| **FN-DSA-1024** | 206 | Lattice (NTRU) | 1,793 B | ~1,313 B | 18× | ~60 µs | NIST Final |
| **SLH-DSA-128s (SPHINCS+)** | 205 | Hash-based | 32 B | 7,856 B | 109× | ~4 ms | NIST Final |
| XMSS | SP800-208 | Hash (stateful) | 64 B | ~2,500 B | 35× | ~1 ms | **DO NOT USE** |

### Recommendation

**Primary algorithm: ML-DSA-44 (CRYSTALS-Dilithium Level 2)**
- Most studied lattice scheme, NIST's primary standard
- Verification is **2.5× FASTER than ECDSA** (~40 µs vs ~100 µs)
- 2,420-byte signatures are manageable with witness discount
- Used by: XRP Ledger testnet, QRL (migrating to), Bitcoin BIP-360

**Secondary/future: FN-DSA-512 (Falcon-512)**
- Smallest PQC signatures (666 bytes) — best for throughput
- Used by: Algorand (production)
- Risk: Gaussian sampling in keygen creates side-channel complexity
- Recommendation: Add support but don't make primary until implementation matures

**DO NOT USE for regular transactions:**
- SLH-DSA (SPHINCS+): 7,856-byte signatures kill throughput. Only for ultra-paranoid cold storage.
- XMSS/LMS (stateful): State management is catastrophic for UTXO chains. Reusing a leaf = key compromise. Non-starter for decentralized wallets.

### Why Not Falcon as Primary?

Despite Falcon's size advantage (666B vs 2,420B), ML-DSA-44 is safer to implement:
1. Falcon requires floating-point arithmetic (or constant-time emulation) for key generation
2. Gaussian sampling creates timing side-channels if not implemented carefully
3. ML-DSA's math is simpler and better understood
4. Bitcoin's BIP-360 leads with ML-DSA for the same reasons
5. We can add Falcon support later as a second algorithm option

---

## 4. DigiDollar: The Lockup Problem

### The Vulnerability

When DigiDollar locks DGB as collateral, a typical timelock script:

```
<locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP
OP_DUP OP_HASH160 <pubKeyHash> OP_EQUALVERIFY OP_CHECKSIG
```

**If implemented correctly (P2PKH-style with hash commitment):**
- The public key is **hidden behind HASH160** in the locking script ✅
- The pubkey only gets exposed when the UTXO is spent (unlocked)
- Attack window at spend time = 15 seconds → **quantum infeasible** for the foreseeable future

**If implemented WRONG (raw pubkey in script):**
```
<locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP
<raw_pubkey> OP_CHECKSIG
```
- The public key is **exposed from day one** ❌
- Sits on-chain for the entire lockup duration
- Attacker derives private key offline, waits for timelock to expire, steals everything
- **This is a death sentence for any lockup >2 years**

### RULE: NEVER Put Raw Public Keys in DigiDollar Locking Scripts

This is non-negotiable. All collateral scripts MUST use hash commitments (HASH160 or HASH256 of the pubkey), never the raw pubkey itself. The pubkey should only be revealed at redemption time.

### Maximum Safe Lockup Duration (Pre-PQC)

| Duration | Risk Assessment | Recommendation |
|----------|----------------|----------------|
| 0–18 months | **LOW** — No CRQC exists | ✅ Safe |
| 18–36 months | **MEDIUM** — Approaching Google's 2029 window | ⚠️ Use only with hash-committed scripts |
| 3–5 years | **HIGH** — CRQCs likely available to nation-states | ❌ Requires PQC signatures |
| 5–10 years | **CRITICAL** — Any exposed pubkey is compromised | ❌ PQC mandatory |

**Recommendation: Hard cap initial DigiDollar lockups at 18 months. Extend to unlimited once PQC is deployed.**

### Even With Hash Commits, There's a Subtle Risk

When the collateral is unlocked (spent), the pubkey is revealed in the spending transaction. If an attacker can front-run this in the mempool:
1. See the spending tx → extract pubkey
2. Run Shor's algorithm
3. Create competing tx → steal funds

**But**: DigiByte's 15-second block time makes this ~40× harder than Bitcoin. Even if Shor's runs in 9 minutes (Google's estimate), the DGB block is confirmed in 15 seconds. The attacker can't win this race.

**Additional mitigation**: DigiDollar could implement a **commit-reveal** scheme for redemptions:
1. **Commit**: User broadcasts a hash commitment to redeem (no pubkey exposed)
2. **Reveal** (next block): User reveals pubkey + signature
3. Attacker sees pubkey only AFTER the commitment is on-chain → can't front-run

---

## 5. DigiDollar: PQC from Day One

### Why This Is a Massive Opportunity

DigiDollar has **zero legacy UTXOs**. Unlike Bitcoin (1.7M BTC in P2PK outputs) or Ethereum (every account has exposed pubkeys), DigiDollar starts clean. We can mandate PQC from genesis block. This eliminates the entire migration problem.

### Recommended Architecture

```
DigiDollar Transaction Structure (PQC-native):

Collateral Locking:
├── Locking Script: OP_CLTV <locktime> OP_DROP
│                   OP_DUP OP_HASH256 <hash_of_PQC_pubkey>
│                   OP_EQUALVERIFY OP_PQC_CHECKSIG
├── PQC Pubkey: ML-DSA-44 (1,312 bytes) — stored off-chain, hash on-chain
└── Collateral: DGB amount

Collateral Unlocking:
├── Witness: ML-DSA-44 signature (2,420 bytes)
├── Witness: ML-DSA-44 pubkey (1,312 bytes)
├── Witness: secp256k1 signature (72 bytes)     ← hybrid, during transition
├── Witness: secp256k1 pubkey (33 bytes)         ← hybrid, during transition
└── Verified against hash commitment in locking script

Oracle Price Feeds:
├── Oracle identity: ML-DSA-44 keypair (rotated every 30 days)
├── Price attestation: Signed JSON with timestamp
├── On-chain: HASH256 of attestation in OP_RETURN
└── Multi-oracle: Require 3-of-5 oracle PQC signatures
```

### Can DigiDollar Launch with PQC Before DigiByte Core Has It?

**Yes.** Two approaches:

**Option A: Application-Layer PQC (faster, recommended for initial launch)**
- DigiDollar protocol validates PQC signatures at the application layer
- DGB base layer still uses ECDSA for the underlying transactions
- DigiDollar wraps DGB UTXOs with PQC-committed collateral proofs
- Works immediately without any DigiByte Core fork

**Option B: Coordinated Launch with DGB PQC Soft Fork (ideal long-term)**
- Ship DigiDollar alongside the DigiByte PQC soft fork
- Both use native P2QRH addresses
- Architecturally cleanest, strongest security narrative
- Takes longer (18-30 months)

**Recommendation: Start with Option A, migrate to Option B when the DGB soft fork activates.**

### DigiDollar PQC Design Decisions

1. **Lockup duration**: 18-month max pre-PQC, unlimited post-PQC
2. **Signature algorithm**: ML-DSA-44 (primary) + secp256k1 (hybrid during transition)
3. **Script design**: Always HASH256(pqc_pubkey) in locking scripts, never raw pubkey
4. **Oracle diversity**: Use multiple PQC algorithms across oracles (ML-DSA + Falcon)
5. **Commit-reveal for redemptions**: Optional but recommended for high-value vaults

---

## 6. DigiByte Core: Revised Two-Phase Soft Fork Plan

### REVISED STRATEGY (v2.0) — BIP-360 Two-Phase Approach

After reviewing BIP-360's latest design, the Chaincode Labs report, and 7 distinct Bitcoin proposals, the optimal strategy is a **two-phase approach** rather than jumping straight to PQC signatures.

### Phase 1: P2MR — Remove the Attack Surface (FAST)

**Concept:** Adapted from BIP-360 (P2MR). Remove the quantum-vulnerable public key from outputs. No new cryptography needed.

**SegWit witness version 2:**
```
scriptPubKey: OP_2 <32-byte merkle_root_hash>
```

The 32-byte value is the Merkle root of a script tree. No internal public key. No exposed pubkey.

**Address format:**
```
dgb1z<bech32m payload>    (witness v2, 'z' prefix)
```

**Spending (script-path only):**
```
Witness: <script> <merkle_proof> <signature> <pubkey>
```

Public key only revealed at spend time (15-second window). At-rest attack eliminated.

**Size overhead:** 103 bytes witness (vs 66 bytes P2TR keypath) — only 37 bytes more. **Negligible.**

**Code change:** Minimal — reuse existing tapscript infrastructure, remove keypath spend logic.

**Why Phase 1 first:**
- No new crypto libraries
- No signature size bloat  
- No block capacity crisis
- Ships in months, not years
- Buys time for PQC algorithm maturity
- DigiDollar can use P2MR immediately for collateral scripts

### Phase 2: PQC Signatures — Add Quantum-Proof Verification (LATER)

**Concept:** Add PQC signature opcodes via new tapscript leaf versions (OP_SUCCESSx upgrade path built into Phase 1).

**New leaf version in P2MR script tree:**
```
Leaf script: <pqc_pubkey_hash> OP_PQC_CHECKSIG_ML_DSA_44
```

**Supported algorithms (initially):**
1. **ML-DSA-44** (CRYSTALS-Dilithium, FIPS 204) — primary
2. **FN-DSA-512** (Falcon, FIPS 206) — compact option
3. **SLH-DSA-128s** (SPHINCS+, FIPS 205) — hash-based backup

**Library:** Vendor `libbitcoinpqc` (MIT) or `liboqs` into `src/crypto/pqc/`

**Hybrid mode (transition period):**
```
Witness: <ecdsa_sig> <ecdsa_pubkey> <ml_dsa_sig> <ml_dsa_pubkey>
```
Both must verify. If either breaks, the other protects.

**Key code changes:**
```cpp
// src/script/interpreter.cpp — P2MR witness handling (Phase 1)
case 2: // SegWit v2 = P2MR
    return VerifyP2MRWitness(program, witness, flags, serror);

// src/crypto/pqc/ml_dsa44.h — PQC verification (Phase 2)
bool ML_DSA44_Verify(const std::vector<uint8_t>& pubkey,
                     const uint256& hash,
                     const std::vector<uint8_t>& signature);

// src/wallet/wallet.cpp — new address types
case OutputType::P2MR:
    return GetNewP2MRAddress(label);
case OutputType::P2MR_PQC:
    return GetNewPQCAddress(label);
```

### Additional Protective Measures (from Bitcoin proposals)

These can be implemented alongside or between the two phases:

| Measure | Description | Effort | Priority |
|---------|-------------|--------|----------|
| **Quantum canaries** | On-chain bounties behind progressively harder quantum challenges. Early warning system. | Trivial | HIGH |
| **Hourglass throttle** | Rate-limit P2PK spends to 1 per block. Slow quantum theft to a crawl. | Soft fork | MEDIUM |
| **Commit-reveal for high-value** | Two-step spending for high-value UTXOs. Prevents mempool front-running. | Soft fork | MEDIUM |
| **Pre-signed recovery trees** | Users commit Merkle roots of recovery txs in OP_RETURN today. Zero protocol changes. | None | LOW (user-side) |

### Transition Phases (Revised)

| Phase | Period | What Happens |
|-------|--------|-------------|
| **Phase 1a: P2MR** | Months 1-6 | SegWit v2 with Merkle root commitment. No pubkey exposure. `dgb1z` addresses. |
| **Phase 1b: Quantum canaries** | Months 3-6 | On-chain bounties as early warning. Simple OP_RETURN outputs. |
| **Phase 1c: Hourglass** | Months 6-12 | Rate-limit vulnerable output types. Soft fork alongside or after P2MR. |
| **Phase 2: PQC signatures** | Months 12-24 | ML-DSA-44 + FN-DSA-512 via new tapscript leaf version. |
| **Phase 3: Deprecation** | Year 3-5 | Wallets warn when sending to ECDSA-only addresses. |
| **Phase 4: Sunset** | Year 5-7+ | Community decision on legacy UTXOs (burn/throttle/extend). |

---

## 7. Transaction Size Impact

### Size Comparison (2-input, 2-output transaction)

| Scheme | Raw Size | vBytes (w/ witness discount) | vs Current |
|--------|----------|------------------------------|-----------|
| P2WPKH (current) | 371 B | 211 vB | baseline |
| **Hybrid ML-DSA-44** | 8,086 B | 2,495 vB | **11.8×** heavier |
| **Hybrid Falcon-512** | 3,700 B | 1,160 vB | **5.5×** heavier |
| ML-DSA-44 only | 5,800 B | 1,680 vB | 8× heavier |
| SLH-DSA-128s hybrid | 16,100 B | 4,322 vB | 20× heavier |

### Block Capacity Impact

With current ~1MB blocks (8M weight units):

| Scheme | Txs per Block | TPS (15-sec blocks) | Capacity Reduction |
|--------|--------------|--------------------|--------------------|
| P2WPKH (current) | ~4,700 | ~313 | baseline |
| Hybrid ML-DSA-44 | ~400 | ~27 | **~8.5× fewer** |
| Hybrid Falcon-512 | ~860 | ~57 | **~5.5× fewer** |

### Mitigations

1. **Enhanced witness discount**: Give PQC witness data a 10:1 discount instead of SegWit's 4:1. This brings ML-DSA-44 hybrid down to ~650 vB (~3× current, manageable).

2. **Block size increase**: DigiByte has increased block size before. A 4× increase to 4MB base / 32M weight would restore current throughput levels.

3. **Gradual adoption**: PQC adoption will be slow. Most transactions will remain ECDSA for years. Block capacity isn't an immediate crisis.

4. **Signature aggregation**: Future research may enable PQC signature aggregation (like Schnorr's `MuSig` for ECDSA). This would dramatically reduce per-transaction overhead.

### The Key Insight

**PQC verification is actually FASTER than ECDSA.** ML-DSA-44 verify: ~40 µs. secp256k1 ECDSA verify: ~100 µs. The bottleneck is SIZE (bandwidth/storage), not computation. DigiByte nodes won't struggle to validate PQC blocks — they'll just be bigger.

---

## 8. Implementation Roadmap

### Phase 1: Library Integration (Months 1–4)
- [ ] Vendor `libbitcoinpqc` into `src/crypto/pqc/`
- [ ] CMake integration, static linking
- [ ] C++ wrapper classes: `PQCSigningKey`, `PQCVerifyKey`, `PQCSignature`
- [ ] Unit tests for ML-DSA-44 and FN-DSA-512 keygen/sign/verify
- [ ] Benchmarks on reference hardware
- [ ] **DigiDollar**: Begin PQC-native collateral protocol design

### Phase 2: Wallet & Address Support (Months 3–8)
- [ ] New key type in wallet (PQC keypairs in wallet.dat)
- [ ] P2QRH address generation (bech32m, witness v2)
- [ ] `getnewaddress "label" "p2qrh"` RPC
- [ ] Hash-based HD key derivation for PQC (BIP-32 doesn't work for ML-DSA)
- [ ] Key backup/export (2.5KB private keys need care)
- [ ] **DigiDollar**: Ship application-layer PQC collateral (Option A)

### Phase 3: Consensus Rules (Months 6–14)
- [ ] P2QRH scriptPubKey template
- [ ] `VerifyP2QRHWitness()` in `script/interpreter.cpp`
- [ ] Hybrid mode enforcement (both ECDSA + ML-DSA must verify)
- [ ] Transaction relay policy (`IsStandard()` updates)
- [ ] Witness weight/discount rules for PQC data
- [ ] Signing serialization specification (sighash for PQC)
- [ ] **Draft DGIP** (DigiByte Improvement Proposal)

### Phase 4: Testing (Months 10–18)
- [ ] Functional tests (`test/functional/p2qrh_*.py`, minimum 50 tests)
- [ ] Fuzz targets for PQC witness parsing
- [ ] Test vectors (cross-implementation compatibility)
- [ ] Regtest and testnet deployment
- [ ] External security audit of PQC integration
- [ ] **DigiDollar**: Migrate to native P2QRH (Option B)

### Phase 5: Activation (Months 18–30)
- [ ] DGIP finalized and published
- [ ] Miner signaling (BIP 9 versionbits, 90% threshold)
- [ ] Lock-in and activation height
- [ ] Ecosystem coordination (wallets, exchanges, explorers)

### Phase 6: Ecosystem Migration (Months 24–60)
- [ ] Hardware wallet support (Ledger, Trezor)
- [ ] Mobile wallet updates
- [ ] Exchange deposit address upgrades
- [ ] DigiAssets, Digi-ID PQC support
- [ ] Community migration campaign

**Total: 18–30 months to mainnet activation, 3–5 years to broad ecosystem support.**

---

## 9. Mining Impact

### PoW Mining: NO CHANGE NEEDED

Google's paper is explicit: **quantum computers cannot meaningfully accelerate PoW mining.**

The math:
- Grover's algorithm: √N speedup on unstructured search
- A quantum miner's theoretical hashrate: ~0.25 TH/s
- A single Antminer S19 Pro: ~110 TH/s
- **Classical ASICs are 440× faster than quantum miners**

DigiByte's five algorithms (SHA256d, Scrypt, Qubit, Skein, Odocrypt) are all hash-based. Grover's quadratic speedup is completely irrelevant given the overhead of quantum error correction. Mining is safe for decades.

### What DOES Affect Miners

1. **Larger transactions** → more data per block → slightly higher bandwidth requirements for propagation
2. **PQC signature validation** is actually faster than ECDSA → verification load DECREASES
3. **Block size increase** (if implemented) → more storage requirements for full nodes
4. No changes to mining algorithms, difficulty adjustment, or block rewards

### Odocrypt Note

Odocrypt's FPGA-friendly, rotating algorithm design adds defense against ASIC centralization but is irrelevant to quantum resistance. Multi-algo mining is a classical security feature, not a quantum one.

---

## 10. Open Questions

### For Jared to Decide

1. **Algorithm priority**: ML-DSA-44 (safer implementation) vs Falcon-512 (smaller signatures)? Both? BIP-360 leads with ML-DSA but supports both.

2. **DigiDollar launch timing**: Ship with application-layer PQC now (Option A) or wait for DGB soft fork coordination (Option B)?

3. **Maximum lockup duration**: 18 months hard cap pre-PQC? Or 24 months?

4. **Block size policy**: Increase block size alongside PQC fork? How much?

5. **Witness discount**: Standard SegWit 4:1 for PQC witness data, or enhanced 10:1?

6. **Dormant UTXO sunset**: When (if ever) should ECDSA-only UTXOs be frozen/burned?

7. **DigiDollar commit-reveal**: Mandatory for all redemptions, or optional?

### Technical Questions Requiring Further Research

- BIP-32 HD derivation equivalent for ML-DSA keypairs (follow Bitcoin's BIP-360 work)
- Signature aggregation for PQC (active research area, not yet standardized)
- Hardware wallet vendor timelines for PQC support
- Cross-chain implications (DigiAssets, atomic swaps with PQC)
- Digi-ID migration to PQC challenge-response

---

## References

### Google Paper
- Blog: https://research.google/blog/safeguarding-cryptocurrency-by-disclosing-quantum-vulnerabilities-responsibly/
- Whitepaper: https://quantumai.google/static/site-assets/downloads/cryptocurrency-whitepaper.pdf

### NIST Standards
- FIPS 204 (ML-DSA): https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.204.pdf
- FIPS 205 (SLH-DSA): https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.205.pdf
- FIPS 206 (FN-DSA/Falcon): https://csrc.nist.gov/pubs/fips/206/final

### Implementation References
- Bitcoin BIP-360 (P2QRH): https://github.com/bitcoin/bips/blob/master/bip-0360.mediawiki
- libbitcoinpqc: https://github.com/cryptoquick/libbitcoinpqc
- liboqs: https://openquantumsafe.org/liboqs/
- Algorand PQC (Falcon): https://www.algorand.foundation/news/algorand-state-proofs

### Blockchain PQC Deployments
- QRL (XMSS → ML-DSA): https://www.theqrl.org/
- Algorand (Falcon-512): First PQC transaction 2025
- Solana (Winternitz Vault): Testnet 2025
- XRP Ledger (ML-DSA): AlphaNet testnet 2025

---

## Appendix A: Quick Reference — Signature Sizes

```
Algorithm           Public Key    Signature    Total per Input
─────────────────────────────────────────────────────────────
secp256k1 ECDSA     33 bytes      72 bytes     ~148 bytes
ML-DSA-44           1,312 bytes   2,420 bytes  ~3,800 bytes
FN-DSA-512          897 bytes     666 bytes    ~1,600 bytes
SLH-DSA-128s        32 bytes      7,856 bytes  ~8,000 bytes

Hybrid (ECDSA + ML-DSA-44):      ~3,857 bytes witness per input
Hybrid (ECDSA + Falcon-512):     ~1,688 bytes witness per input
```

## Appendix B: DigiByte's Structural Advantages

| Feature | Advantage for PQC Migration |
|---------|---------------------------|
| 15-second blocks | 40× smaller on-spend attack window vs Bitcoin |
| UTXO model | Pubkeys hidden behind hashes (unlike Ethereum's account model) |
| Multi-algo PoW | Mining unaffected by quantum (and already resistant to ASIC centralization) |
| Smaller ecosystem | Faster coordination for soft fork activation |
| DigiDollar not yet launched | Can build PQC in from genesis — no migration needed |
| SegWit already active | Witness discount infrastructure exists for PQC signatures |
| No smart contracts | No "admin key" vulnerability (unlike Ethereum ERC-20 proxies) |
| No PoS | No validator key exposure risk |

## Appendix C: Detailed Research Reports

Full technical reports from deep research:
- `/tmp/pqc_algorithms_research.md` — Algorithm comparison, sizes, C++ libraries
- `/tmp/dgb_pqc_implications.md` — DGB/DigiDollar specific threat analysis
- `/tmp/dgb_pqc_implementation.md` — C++ implementation path, BIP-360, timeline

---

*This document will be updated as research progresses and decisions are made.*
