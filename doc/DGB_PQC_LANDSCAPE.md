# Post-Quantum Cryptography: Full Industry Landscape
**Companion document to `DGB_PQC_PLAN.md`**  
**Version:** 1.0 — March 31, 2026

---

## Table of Contents

1. [Bitcoin Proposals (7 Total)](#1-bitcoin-proposals)
2. [Ethereum's Approach](#2-ethereums-approach)
3. [Other Chain Implementations](#3-other-chain-implementations)
4. [The Burn vs. Steal Debate](#4-the-burn-vs-steal-debate)
5. [Migration Pathway Analysis](#5-migration-pathway-analysis)
6. [Algorithm Deep Dive](#6-algorithm-deep-dive)
7. [Industry Timeline Comparison](#7-industry-timeline-comparison)
8. [Implications for DigiByte](#8-implications-for-digibyte)

---

## 1. Bitcoin Proposals

There are **seven** distinct proposals for Bitcoin's quantum resistance, ranging from minimal changes to radical protocol overhauls. Source: [Chaincode Labs report](https://chaincode.com/bitcoin-post-quantum.pdf) (May 2025), [Project Eleven analysis](https://blog.projecteleven.com/posts/a-look-at-post-quantum-proposals-for-bitcoin), BIP-360 spec.

### 1.1 BIP-360: P2MR — Pay-to-Merkle-Root

**Authors:** Hunter Beast, Ethan Heilman, Isabel Foxen Duke  
**Status:** Draft BIP, merged into BIPs repo Feb 2026 (v0.11.0)  
**Type:** Soft fork, SegWit version 2, `bc1z` addresses

**What it does:** Removes the quantum-vulnerable keypath spend from Taproot. P2MR commits only to a Merkle root of a script tree — no internal public key is exposed on-chain.

```
P2TR output:  OP_1 <tweaked_pubkey>     ← pubkey exposed, quantum vulnerable
P2MR output:  OP_2 <merkle_root_hash>   ← just a hash, safe at rest
```

**Key design decisions:**
- Does NOT include any PQC signature algorithms (yet)
- Still uses Schnorr/ECDSA signatures in leaf scripts
- PQC signatures will come later via OP_SUCCESSx upgrade path in new tapscript leaf versions
- All existing tapscript programs work in P2MR without modification
- Witness size: 103 bytes (single leaf) vs 66 bytes (P2TR keypath) — only 37 bytes larger

**Strategy:**
1. Phase 1 (NOW): Remove the attack surface — hide pubkeys behind Merkle roots
2. Phase 2 (FUTURE): Add PQC signature opcodes via separate BIP

**Why this is smart:** Zero new crypto libraries. Minimal code change. Ships fast. Buys time for PQC algorithm maturity.

**Library:** `libbitcoinpqc` (MIT licensed) — https://github.com/cryptoquick/libbitcoinpqc

### 1.2 QBIP / QRAMP: Mandatory Migration & Legacy Sunset

**Authors:** Jameson Lopp, Christian Papathanasiou, others  
**Status:** Discussion stage  
**Type:** Hard fork (controversial)

**What it does:** The "nuclear option" — freeze ALL quantum-vulnerable UTXOs by a deadline.

**Three phases:**
- **Phase A:** Disallow sending TO legacy script types (only P2QRH as destinations)
- **Phase B:** Flag-day ~5 years after activation — ECDSA/Schnorr spends become invalid. All unmigrated UTXOs permanently unspendable. Satoshi's ~1.1M BTC get burned.
- **Phase C (optional):** Hard fork to allow recovery via ZK proof of BIP-39 seed possession

**Lopp's argument:** "Quantum recovered coins only make everyone else's coins worth less. Think of it as a theft from everyone." Pieter Wuille (Bitcoin Core) agrees: coins MUST be frozen.

**Controversy:** Violates "your keys, your coins." Most divisive proposal in Bitcoin's history.

### 1.3 Hourglass: Rate-Limited Quantum Theft

**Authors:** Hunter Beast, Michael Casey (same BIP-360 team)  
**Status:** Draft proposal  
**Type:** Soft fork

**What it does:** Instead of freezing vulnerable UTXOs, throttle quantum theft to a crawl.

**Mechanics:**
- Limits P2PK spends to **1 per block** (instead of ~6,000)
- No new P2PK outputs can be created
- A quantum attacker could drain ~7,200 BTC/day instead of 1.7M BTC in hours
- Stretches a 3-hour attack window to **~8 months**
- Creates a fee market — quantum attackers bid against each other for the single P2PK slot
- Hourglass v2 extends to reused addresses and Taproot outputs

**Game theory:** First-mover incentive to reveal quantum capability, turns attackers into miner revenue, gives market time to price in supply shock gradually.

### 1.4 Lamport Signatures via OP_CAT

**Authors:** Ethan Heilman, Armin Sabouri (BIP-347)  
**Status:** BIP-347 proposed, not activated  
**Type:** Soft fork (OP_CAT reintroduction)

**What it does:** Reintroduce OP_CAT (disabled by Satoshi in 2010), which enables Lamport signatures in tapscript.

**How:**
- Lamport signatures only need hashing + concatenation on the stack
- OP_CAT provides the concatenation; hashing already exists
- Users add a Lamport signature spending condition as a leaf in their Taproot script tree
- When quantum hits → separate soft fork disables Taproot keypath spends globally
- Only the Lamport script-path remains valid

**Pros:**
- Hash-based signatures = maximum confidence in quantum safety
- Jeremy Rubin: "Fun Fact: OP_CAT existed in Bitcoin until 2010, when Satoshi forked out a bunch of opcodes. So in theory the original Bitcoin implementation supported Post Quantum cryptography out of the box!"

**Cons:**
- Lamport signatures are ~8,192 bytes (117× Schnorr) — huge
- One-time use only (signing twice reveals the private key)
- Requires TWO soft forks (OP_CAT + disable keypath)
- OP_CAT has other use cases that are controversial ("impossible to predict all consequences" — Robin Linus)

**Variant — Matt Corallo's OP_SPHINCS:** Instead of building Lamport from OP_CAT, add a dedicated PQC signature verification opcode. Same concept, cleaner implementation. Luke Dashjr noted wallets could start implementing fallback paths immediately, even before activation.

### 1.5 Quantum-Safe Taproot (SLH-DSA Leaf)

**Status:** Conceptual  
**Type:** User-side preparation now, soft fork later

**What it does:** Users add a PQC spending leaf to their Taproot script tree TODAY. No protocol changes needed yet.

**How:**
- When creating Taproot output, add extra leaf: `OP_SLHDSA <hash_of_PQ_pubkey>`
- Normal spending continues via Schnorr keypath — zero cost, zero change
- When quantum threat materializes → soft fork disables ALL Schnorr keypath spends
- Only the SLH-DSA script-path branch remains valid
- Funds already protected because the PQ leaf was there from creation

**The genius:** Zero cost until needed. Prepare today, activate later.

**The problem:** Any Taproot UTXO created WITHOUT the SLH-DSA leaf gets permanently frozen when the soft fork activates. This is defacto confiscation of non-adopters. Also SLH-DSA signatures are 8-50KB.

### 1.6 Pre-Signed Recovery Merkle Tree

**Status:** Conceptual  
**Type:** NO consensus changes required

**What it does:** You can protect your coins RIGHT NOW with zero protocol changes.

**How:**
1. Build all "recovery" transactions offline (moving coins to PQC addresses)
2. Hash them into a Merkle tree
3. Publish ONLY the Merkle root in an OP_RETURN today
4. When quantum hits → soft fork adds a delay to all vulnerable spends
5. You reveal your pre-signed recovery tx + Merkle proof → overrides any attacker's spend

**Key properties:**
- Privacy-preserving: the Merkle root reveals nothing about which UTXOs you own
- You're planting a time-locked "poison pill" today against a future quantum attacker
- Works with existing Bitcoin — no fork needed for the preparation step
- Fork only needed later to enforce the delay + recovery mechanism

### 1.7 Fawkescoin / Lifeboat (Tadge Dryja)

**Author:** Tadge Dryja (Lightning Network co-inventor)  
**Status:** Conceptual  
**Type:** Soft fork (triggered by quantum event)

**What it does:** No changes until "Proof of Quantum Computer" (PoQC) is demonstrated on-chain.

**How:**
1. "Quantum canaries" on-chain — bounties locked behind progressively harder quantum challenges
2. When a canary is claimed → signals quantum capability has reached a certain level
3. At that moment, soft fork requires 3-hash commitments before any spend:
   - AID (address ID = hash of pubkey)
   - SDP (sequence-dependent proof = hash of pubkey + txid)
   - CTXID (commitment transaction ID)
4. Commit first, then spend. Attacker can't front-run because they'd need to make a valid commitment first.

**Until quantum arrives:** Bitcoin works exactly as today. Zero overhead.

**Critical caveat (applies to ALL commit-reveal schemes):** Every scheme requires your ECC key was NEVER previously exposed. If your pubkey was ever revealed (address reuse, P2PK, Taproot keypath), an attacker can produce identical commitments.

### 1.8 STARK Signature Compression (Ethan Heilman)

**Status:** BDML proposal, early stage  
**Type:** Future enhancement to any PQC deployment

**What it does:** Compresses ALL PQC signatures in a block into a single STARK proof.

**How:**
- Instead of carrying individual 2-8KB PQC signatures per transaction
- A block producer creates a single STARK proving "every tx in this block has valid PQC sigs"
- Per-transaction overhead drops to ~76 bytes
- Throughput goes from ~7 tx/s → ~87 tx/s

**Properties:**
- Non-financial data (inscriptions/JPEGs) DON'T get compressed — creates natural fee differential
- Could make PQC transactions CHEAPER than current ECDSA transactions
- Requires STARK security assumptions in Bitcoin's consensus (controversial)
- Huge engineering effort

---

## 2. Ethereum's Approach

### The Five Quantum Vulnerabilities

Ethereum has **five** distinct quantum attack surfaces (Bitcoin has one):

| Surface | What's Exposed | Impact |
|---------|---------------|--------|
| **Account signatures** | Every account's pubkey exposed on first tx | All active accounts vulnerable |
| **Smart contract admin keys** | Exposed upgrade/mint/freeze keys | USDT, USDC, all DeFi at risk |
| **Proof-of-Stake consensus** | ~1M BLS12-381 validator keys | Could compromise consensus itself |
| **Data Availability (KZG)** | Polynomial commitments over ECC | Reusable backdoor ("on-setup" attack) |
| **ZK proofs** | Application-layer proofs | Many L2s compromised |

### Vitalik's Emergency Plan (2024)

If quantum hits before they're ready:
1. Hard fork the chain
2. Revert blocks after quantum attack
3. Use STARKs + BIP-32 seeds to prove ownership
4. Rebuild from last known good state

Widely criticized as "untenable" — requires centralized coordination during an active attack.

### Vitalik's Phased Roadmap (Feb 2026)

Four areas:
1. **Validator signatures**: Replace BLS with hash-based signatures
2. **KZG commitments**: Replace with quantum-safe alternative (major engineering)
3. **Wallet signatures**: EIP-8141 makes accounts flexible enough to swap signature schemes
4. **ZK proofs**: "Validation frames" to bundle/compress PQC proofs

### Key Ethereum EIPs

| EIP | What | Status |
|-----|------|--------|
| **EIP-8141** | First-class flexible accounts — swap signature algorithms | Discussion |
| **EIP-7702** | Account abstraction (Pectra) — delegate to smart contract | Shipped, but delegation uses ECDSA |
| **Hash-Committed Accounts** | New account type with hash-derived addresses | Brand new (Mar 2026) |
| **LeanVM** | More adaptable VM for new crypto primitives | Research |

### Ethereum Foundation PQ Team

- Established January 2026
- **2029 target** for L1 quantum upgrade
- No concrete EIP with activation timeline
- Years behind Bitcoin's BIP-360 effort
- Budget: dedicated salaries + research grants

### The Ethereum Account Model Problem

This is Ethereum's structural disadvantage that DigiByte doesn't share:

```
Ethereum: account → pubkey PERMANENTLY exposed after first tx
Bitcoin/DGB: UTXO → pubkey hidden behind hash, only exposed at spend time
```

Every active Ethereum wallet is a permanent quantum target. There is no hash-hiding protection. This affects ~150M accounts.

---

## 3. Other Chain Implementations

### QRL (Quantum Resistant Ledger) — The Pioneer

- **Launched:** 2018 — first PQC blockchain
- **Current:** XMSS (eXtended Merkle-tree Signature Scheme) — hash-based, stateful
- **Migrating to:** SPHINCS+ (FIPS 205) — hash-based, STATELESS
- **Why migrate:** XMSS is stateful (must track one-time signature usage). Reusing a leaf = key compromise. Operational complexity.
- **Project Zond:** Adding Proof-of-Stake + EVM compatibility with Dilithium-backed signatures
- **Track record:** 7 years uninterrupted PQC security
- **Lesson for DGB:** PQC from day one eliminates the entire migration nightmare. QRL proves it's operationally viable.

### XRP Ledger — ML-DSA on AlphaNet

- **Date:** December 24, 2025
- **What:** ML-DSA (CRYSTALS-Dilithium) as optional key type on AlphaNet testnet
- **Approach:** New `sfPostQuantum` account flag enables PQC for that account
- **Signature size:** 2,420 bytes (ML-DSA-44)
- **Status:** Testnet only, no mainnet activation date
- **Key detail:** XRPL is account-based (like Ethereum), so pubkey exposure is permanent — makes PQC more urgent for them

### Algorand — Falcon-512 for State Proofs

- **Date:** 2025, production
- **What:** Falcon-512 for state proofs (cross-chain verification)
- **Why Falcon:** Smallest signatures (666 bytes) — state proofs are generated frequently
- **Scope:** State proofs only, not all transaction signatures
- **Lesson for DGB:** Falcon works in production at scale, but Algorand chose it for a specific use case, not general transactions

### Solana — Winternitz Vault

- **Date:** December 2025
- **What:** Smart contract-based vault using Winternitz One-Time Signatures (WOTS)
- **Approach:** Opt-in vault for users concerned about quantum risk
- **NOT a protocol change** — just a smart contract layer
- **Lead:** Project Eleven (also authored the blog post Jared shared)
- **Limitation:** WOTS is one-time use — must generate new keys for each transaction
- **Lesson for DGB:** Optional opt-in vaults are a valid interim measure

### IOTA — WOTS from Day One

- Uses Winternitz One-Time Signature Scheme
- Quantum-resistant by design
- Limitation: one-time use creates UX friction

### Mochimo / Abelian

- Mentioned in Google's paper as PQC-native chains
- Small projects with limited adoption
- Prove the concept works but lack ecosystem

---

## 4. The Burn vs. Steal Debate

### The Question

> Should quantum-vulnerable funds be made unspendable ("burned"), or should they be left available for recovery by quantum computers ("stolen")?

### Arguments for BURN (freeze vulnerable UTXOs)

**Jameson Lopp** (Casa CTO, [blog post](https://blog.lopp.net/against-quantum-recovery-of-bitcoin/)):
- "Quantum recovered coins only make everyone else's coins worth less"
- "Think of it as a theft from everyone"
- Quantum theft = wealth redistribution to whoever builds CRQCs first (nation-states, Google)
- Creates sustained market volatility as millions of BTC are gradually dumped

**Pieter Wuille** (Bitcoin Core legend):
- Coins MUST be frozen or Bitcoin can't maintain value
- Property rights argument cuts both ways — allowing theft is also a property rights violation against everyone else

**Economic argument:**
- ~6.26M BTC at risk (~30% of supply)
- If gradually stolen and sold, could crash Bitcoin's price
- A coordinated burn provides certainty — market can price it in once
- Remaining supply becomes more scarce → price may increase

### Arguments for STEAL (do nothing / let quantum computers claim them)

**Core principle:** "Not freezing user funds is one of Bitcoin's inviolable properties"
- Bitcoin was designed so no third party can seize your funds
- Burning UTXOs = the network seizing control from rightful owners
- Some "dormant" wallets may belong to people who simply haven't moved coins yet
- Kleiman v. Wright case (2020): keys from 2009 addresses were used to sign messages, proving "dormant" ≠ "lost"

**Practical argument:**
- Impossible to distinguish "lost forever" from "choosing not to move"
- Legal liability if community actively freezes someone's funds
- "Do nothing" is the safest default in a system designed to resist intervention

### The Hourglass Compromise

Neither burn nor steal — **throttle:**
- Rate-limit vulnerable spends to 1 per block
- Stretches potential theft from hours to months
- Market can absorb gradual supply increase
- Miners earn fees from quantum attackers competing for the limited slot
- No confiscation — funds remain theoretically spendable

### What This Means for DigiByte

DGB has far fewer vulnerable UTXOs than Bitcoin:
- No Taproot deployed → no P2TR keypath exposure
- Standard P2PKH → pubkeys hidden behind hashes
- Much smaller total supply at risk
- Community is smaller → consensus is faster

**For DigiDollar:** Not applicable — launching from scratch with PQC. No legacy UTXOs to burn or protect.

---

## 5. Migration Pathway Analysis

### Scale of the Problem (Bitcoin)

| Metric | Bitcoin | DigiByte |
|--------|---------|----------|
| Total UTXOs | ~190 million | ~TBD million |
| Immediately vulnerable | ~6.26M BTC (P2PK, reused addresses) | Much smaller |
| Satoshi-era P2PK | ~1.72M BTC (1,720,747 BTC) | Genesis coins only |
| P2TR (exposed pubkey) | ~146,715 BTC | N/A (no Taproot) |
| Migration time (100% blocks) | 76-142 days | Much faster (smaller UTXO set) |
| Migration time (25% blocks) | 305-568 days | ~75-140 days (est.) |

### Migration Mechanisms

| Mechanism | Description | Fork Required | User Action | DGB Applicability |
|-----------|-------------|---------------|-------------|-------------------|
| **Commit-Delay-Reveal** | 3-stage migration: commit hash → wait → reveal + sign | Soft fork | Opt-in | Good for high-value |
| **QRAMP (flag-day)** | Hard deadline, unmigrated UTXOs frozen | Hard fork | Mandatory | Too aggressive for DGB |
| **Hourglass** | Rate-limit vulnerable spends | Soft fork | None (passive) | Excellent fit |
| **Private tx services** | Submit to trusted miners, skip mempool | None | Opt-in | DGB has no equivalent yet |
| **Quantum canaries** | On-chain bounties as early warning system | None | None | Excellent, easy to implement |

### Chaincode Labs Timeline Estimates

**Short-term contingency (~2 years):**
- Research + BIP: 3-6 months
- Implementation: 3-12 months
- Migration: 6-12 months

**Long-term comprehensive (~7 years):**
- Research + BIP: ~2.5 years
- Implementation: ~1.5 years
- Migration: ~3 years

**Historical precedent:**
- SegWit: conception (2015) → activation (2017) → 90% adoption (2023) = **8.5 years**
- Taproot: conception (2014) → activation (2021) → currently 30-40% adoption = **12+ years and counting**

**DGB advantage:** Smaller community = faster consensus. We don't need 8 years.

---

## 6. Algorithm Deep Dive

### Full Comparison Table (from Chaincode Labs, adapted)

| Algorithm | Year | Family | Public Key | Signature | vs Schnorr Sig | Sign Cost | Verify Cost |
|-----------|------|--------|-----------|-----------|---------------|-----------|-------------|
| **Schnorr** | 1989 | ECC | 32 B | 64 B | 1.0× | 1.0× | 1.0× |
| **ECDSA** | 1992 | ECC | 33 B | 72 B | 1.1× | 1.05× | 1.05× |
| **Lamport** | 1977 | Hash | 16,384 B | 8,192 B | **117×** | ~0.3× | ~5× |
| **XMSS** | 2011 | Hash (stateful) | 68 B | 2,440 B | 38× | ~30× | ~50× |
| **SLH-DSA-128s** | 2015 | Hash (stateless) | 32 B | 7,856 B | **122×** | ~111,000× | ~37× |
| **SLH-DSA-128f** | 2015 | Hash (stateless) | 32 B | 17,088 B | **267×** | ~5,700× | ~99× |
| **ML-DSA-44** | 2017 | Lattice (MLWE) | 1,312 B | 2,420 B | 38× | ~8× | **~0.9×** ⚡ |
| **FN-DSA-512** | 2017 | Lattice (NTRU) | 897 B | 666 B | **10×** | ~24× | **~0.6×** ⚡ |
| **SQIsign I** | 2023 | Isogeny | 64 B | 177 B | **2.8×** | ~135,000× | ~830× |

### SQIsign — The Dark Horse

- **Smallest PQC signatures:** 177 bytes (only 2.8× Schnorr!)
- **Smallest PQC pubkeys:** 64 bytes (2× Schnorr)
- But: **Sign takes 135,000× ECDSA, verify takes 830×** — unusably slow for blockchain
- NIST evaluating it for standardization (submission 2023)
- From the isogeny family — same family as SIKE, which was **broken in 1 hour** in 2022
- **Status: DO NOT USE. Too new, too slow, family has a broken sibling.**
- Watch for future improvements — if verification speeds improve, SQIsign would be ideal for blockchains

### Algorithm Recommendations by Use Case

| Use Case | Recommended | Why |
|----------|-------------|-----|
| **DGB transactions (general)** | ML-DSA-44 | Best studied, NIST primary, verify faster than ECDSA |
| **DGB transactions (compact)** | FN-DSA-512 | Smallest PQC sigs (666B), good verify speed |
| **DigiDollar collateral** | ML-DSA-44 (hybrid + ECDSA) | Maximum security for locked funds |
| **DigiDollar oracles** | Diverse (ML-DSA + Falcon across oracles) | Defense in depth |
| **Cold storage (paranoid)** | SLH-DSA-128s | Hash-based, no lattice assumptions, NIST backup |
| **DigiAssets issuance** | ML-DSA-44 | Long-lived keys need quantum resistance |
| **Digi-ID authentication** | FN-DSA-512 | Compact for challenge-response |
| **Future (if it matures)** | SQIsign | Near-ECDSA sizes, but needs years more research |

### Why Two Algorithms?

Ethan Heilman's argument (echoed by Chaincode Labs):
- Supporting ONE algorithm is a single point of failure
- If lattice math (MLWE/NTRU) is somehow broken, hash-based (SLH-DSA) survives
- If hash-based has practical issues, lattice-based works
- **Minimum: one lattice (ML-DSA-44) + one hash-based (SLH-DSA-128s)**
- FN-DSA-512 as optional third for compact use cases

---

## 7. Industry Timeline Comparison

### Government Mandates

| Entity | Deprecate ECC | Disallow ECC | Notes |
|--------|--------------|-------------|-------|
| **NIST (USA)** | 2030 | 2035 | Exception for hybrid ECC+PQC |
| **NSA (CNSA 2.0)** | Software by 2030 | Browsers/OS by 2033 | Military/intelligence |
| **UK (NCSC/GCHQ)** | Plan by 2028 | Complete by 2035 | Three-phase migration |
| **EU (ETSI)** | No date set | No date set | Own standards alongside NIST |
| **China (NGCC)** | No date set | No date set | Independent algorithms (not NIST) |

### Blockchain Timelines

| Chain | PQC Status | Estimated Mainnet | Approach |
|-------|-----------|-------------------|----------|
| **QRL** | Live since 2018 | Already done ✅ | XMSS → SPHINCS+ migration |
| **Algorand** | State proofs in production | Done (limited scope) ✅ | Falcon-512 |
| **XRP Ledger** | AlphaNet testnet (Dec 2025) | 2027-2028? | ML-DSA-44 optional accounts |
| **Solana** | Winternitz Vault (opt-in) | N/A (app layer) | WOTS vault contract |
| **Bitcoin** | BIP-360 draft | 2028-2030 (optimistic) | P2MR → PQC sigs later |
| **Ethereum** | PQ team formed Jan 2026 | 2029+ | EIP-8141, LeanVM |
| **DigiByte** | Planning (this document) | **2027-2028 target** | P2MR + ML-DSA hybrid |

### Expert CRQC Timeline Estimates

| Source | When CRQCs Break ECC | Confidence |
|--------|---------------------|-----------|
| Google Quantum AI (Mar 2026) | **2029 internal migration deadline** | High |
| NIST | Deprecate by 2030, disallow by 2035 | High |
| Chaincode Labs survey (2024) | 50%+ chance by 2030-2035 (1/3 of experts) | Medium |
| Scott Aaronson (Sep 2024) | "Worry about this NOW" | High (he's historically a pessimist) |
| Hartmut Neven (Google QAI head) | Commercial quantum in 5 years | Medium |
| Jensen Huang (NVIDIA) | 20 years away | Low confidence (classical computing bias) |
| Google resource estimate (2026) | 500K physical qubits, minutes to crack | Very high |

---

## 8. Implications for DigiByte

### DigiByte's Structural Advantages (Expanded)

| Feature | Why It Helps | vs Bitcoin | vs Ethereum |
|---------|-------------|-----------|-------------|
| **15-second blocks** | 40× smaller on-spend attack window | Better | Comparable (12s) |
| **UTXO model** | Pubkeys hidden behind hashes | Same | Far better |
| **P2PKH standard** | Hash-protected by default | Same | Far better |
| **Taproot deployed** | P2TR outputs ARE vulnerable (same as BTC) — P2MR fixes this | Same risk | Far better |
| **Multi-algo PoW** | Mining unaffected + ASIC diversity | Same | N/A (PoS) |
| **No smart contracts** | No admin key risk | Same | Far better |
| **No PoS** | No validator key exposure | Same | Far better |
| **Smaller UTXO set** | Faster migration | Better | N/A |
| **Smaller community** | Faster consensus on upgrades | Much better | Better |
| **DigiDollar not launched** | PQC from genesis, zero legacy | Unique advantage | Unique advantage |
| **SegWit active** | Witness discount infrastructure ready | Same | N/A |

### The DigiByte Opportunity

1. **Ship P2MR before Bitcoin does.** BIP-360 is stuck in Bitcoin's governance. DGB can implement the same idea and activate it in months.

2. **Launch DigiDollar with PQC from day one.** QRL proved this works. Zero migration problem. Marketing gold: "The world's first quantum-resistant stablecoin."

3. **Be the first major UTXO chain to activate PQC signatures.** XRP Ledger is on testnet. Bitcoin is years away. DGB can leapfrog both.

4. **Implement quantum canaries.** Simple, zero-cost early warning system. Just create on-chain bounties behind progressively harder quantum challenges.

5. **Use the Hourglass model for legacy UTXOs.** Rate-limit vulnerable spends rather than freeze them. Less controversial than QBIP, still protective.

---

## Sources

### Primary Research
- [Google Quantum AI Paper](https://quantumai.google/static/site-assets/downloads/cryptocurrency-whitepaper.pdf) — March 30, 2026
- [Chaincode Labs: Bitcoin and Quantum Computing](https://chaincode.com/bitcoin-post-quantum.pdf) — May 2025
- [Project Eleven: A Look at Post-Quantum Proposals for Bitcoin](https://blog.projecteleven.com/posts/a-look-at-post-quantum-proposals-for-bitcoin)
- [BIP-360 Specification](https://bip360.org/bip360.html)

### Bitcoin Proposals
- [BIP-360 GitHub](https://github.com/bitcoin/bips/pull/1670)
- [Jameson Lopp: Against Quantum Recovery](https://blog.lopp.net/against-quantum-recovery-of-bitcoin/)
- [Hourglass Proposal](https://github.com/cryptoquick/bips/blob/hourglass/bip-hourglass.mediawiki)
- [OP_CAT / BIP-347](https://github.com/bitcoin/bips/blob/master/bip-0347.mediawiki)

### Ethereum
- [Vitalik: How to hard-fork in a quantum emergency](https://ethresear.ch/t/how-to-hard-fork-to-save-most-users-funds-in-a-quantum-emergency/18901)
- [Coinbase PQ Advisory Board](https://www.coinbase.com/blog/coinbase-establishes-independent-advisory-board-on-quantum-computing-and-blockchain)
- [CoinDesk: Crypto's quantum threat](https://www.coindesk.com/tech/2026/03/28/here-s-how-bitcoin-ethereum-and-other-networks-are-preparing-for-the-looming-quantum-threat)

### Other Chains
- [QRL Definitive Guide](https://www.theqrl.org/the-definitive-guide-to-post-quantum-blockchain-security/)
- [XRPL AlphaNet PQC](https://cryptoslate.com/xrpl-flips-to-quantum-safe-signatures-2420-byte-proofs-replace-elliptic-curves/)
- [Solana Winternitz Vault](https://github.com/blueshift-gg/solana-winternitz-vault)

### NIST Standards
- [FIPS 204 (ML-DSA)](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.204.pdf)
- [FIPS 205 (SLH-DSA)](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.205.pdf)
- [FIPS 206 (FN-DSA/Falcon)](https://csrc.nist.gov/pubs/fips/206/final)
