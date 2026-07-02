# DigiByte v9.26.0-rc19 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ TESTNET RESET — READ THIS FIRST

**RC19 resets the testnet to a new chain: testnet19.** This means:

- **Your old testnet blockchain data must be deleted** (see Upgrade Notes below)
- **The chain starts from block 0** — everyone syncs fresh
- **New ports:** P2P port is now **12033**, RPC port is **14025**
- **BIP9 activation is LIVE** — DigiDollar activates via real miner signaling, not auto-activation
- **9th oracle added** — JohnnyLawDGB joins as oracle 8, consensus is now **5-of-9**

### 🔑 Oracle Operators — IMPORTANT

**Your oracle wallet and keys are NOT lost.** They are stored in your old testnet data directory. To migrate:

1. **Before deleting old data**, copy your oracle wallet:
   - **Linux:** Copy `~/.digibyte/testnet18/wallets/oracle/` to a safe location
   - **Windows:** Copy `%APPDATA%\DigiByte\testnet18\wallets\oracle\` to a safe location
   - **macOS:** Copy `~/Library/Application Support/DigiByte/testnet18/wallets/oracle/` to a safe location
2. **Install RC19** and start the node (it creates the new `testnet19` directory)
3. **Copy your saved oracle wallet** into the new testnet19 wallets directory:
   - **Linux:** `~/.digibyte/testnet19/wallets/oracle/`
   - **Windows:** `%APPDATA%\DigiByte\testnet19\wallets\oracle\`
   - **macOS:** `~/Library/Application Support/DigiByte/testnet19/wallets/oracle/`
4. **Load and start your oracle:**
   ```bash
   digibyte-cli -testnet loadwallet "oracle"
   digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
   ```

Your existing Schnorr keypair carries over — you do **NOT** need to run `createoraclekey` again. The private key lives in your wallet file.

### 📡 BIP9 Activation Process

RC19 uses **real BIP9 miner signaling** for DigiDollar activation. This means:

- DigiDollar features are **disabled at launch** — the DigiDollar tab will show "Not yet activated"
- Miners signal support by setting bit 23 in block headers
- Once **70% of blocks in a 200-block window** signal support, activation locks in
- DigiDollar fully activates after the lock-in period completes (min block 600)
- **We are testing the full activation lifecycle** — this mirrors how mainnet activation will work

You can monitor activation progress with:
```bash
digibyte-cli -testnet getdigidollardeploymentinfo
```

---

## What's New in RC19

RC19 is a **major security hardening release** with 132 commits since RC18. The entire DigiDollar codebase has been through two rounds of automated adversarial security testing (**Red Hornet Audit**), resulting in **29 security fixes**, **41 verified defenses**, and a new **Phase 2 multi-oracle Schnorr signature** protocol.

---

## 🔴 Red Hornet Security Audit — Highlights

The **Red Hornet** automated security audit systematically attacked every DigiDollar subsystem with adversarial test cases. Two full rounds were completed covering consensus, oracle, wallet, P2P networking, and mining attack vectors.

### What Was Tested
- **300+ attack scenarios** across 10 threat categories
- **Consensus attacks:** overflow, inflation, conservation bypass, timelock bypass
- **Oracle attacks:** price forgery, staleness, partition, eclipse, Sybil flooding, replay, relay amplification
- **Wallet attacks:** encrypted key exposure, watch-only contamination, data races
- **Mining attacks:** reordering, censorship, timestamp manipulation, selfish mining
- **Network attacks:** double-spend, reorg theft, mempool conflicts, ancestor limits

### 29 Security Fixes Found & Fixed

Every fix below was found by Red Hornet adversarial testing, verified with a proof-of-concept exploit, and patched:

| ID | Fix | One-Line Summary |
|----|-----|-----------------|
| T1-02 | 🔴 CVE-grade | Attacker could inflate DD amounts in OP_RETURN to mint money from nothing |
| T1-03 | 🔒 Consensus | Lock period in transaction didn't have to match the collateral tier — free low-collateral long locks |
| T1-04f | 🔒 Consensus | Multiple DD outputs in one mint tx could bypass amount validation |
| T1-05a | 🔒 Consensus | Oracle validation was skipped during all block connections, not just initial sync |
| T1-06 | 🔧 Crash | Division by zero in system health calculation when no collateral exists |
| T1-07 | 🔒 Consensus | Oracle IDs > 255 silently truncated — could impersonate other oracles |
| T1-08 | 🔒 Consensus | Collateral release allowed without checking DD marker — free DGB unlock |
| T2-01 | 🔒 Consensus | Lock height used as absolute instead of relative — wrong collateral ratios |
| T2-02 | 🔒 Consensus | Transfer conservation check bypassed when source tx wasn't a DD transaction |
| T2-03 | 🔒 Consensus | Partial burn via miner fees let attacker steal collateral |
| T2-04 | 🔒 Oracle | Stale oracle price never expired — old price used forever if oracles went offline |
| T2-05a | 🔒 Health | System collateral ratio was hardcoded, not calculated from real UTXO data |
| T2-05b | 🔒 Health | Unit mismatch (cents vs millicents) gave wrong health score |
| T2-05c | 🔒 Health | Minting allowed when health metrics unavailable — should fail-closed |
| T2-06b | 🔒 Consensus | Collateral outputs disguised as fee inputs in redemption transactions |
| T3-02 | 🔒 P2P | Oracle bundle relay amplification — pubkey rebinding allowed infinite relay |
| T3-03 | 🔒 Oracle | Consensus price depended on wall-clock time — different nodes got different prices |
| T3-04 | 🔒 P2P | Oracle message replay by mutating nonce/block_height fields |
| T3-05a | 🔒 Oracle | Consensus threshold was hardcoded in some paths instead of using chainparams |
| T4-02d | 🔒 Wallet | Missing mutex in DigiDollar wallet — data races under concurrent access |
| T4-03 | 🔒 Wallet | DD private keys stored unencrypted even when wallet was locked |
| T4-04 | 🔒 Wallet | Watch-only wallet showed spendable DD balance from other wallets |
| T5-02 | 🔴 CVE-grade | Coinbase transactions could carry DD markers — mint DigiDollars from block rewards |
| T5-04 | 🔒 Consensus | Collateral release didn't check for DD marker — unlock collateral without burning DD |
| T5-06 | 🔒 Consensus | Supply/collateral tracking used stale UTXO scan instead of live block data |
| T8-03 | 🔒 Consensus | Each node independently queried oracles — partition attack gives different prices |
| T9-01 | 🔒 Oracle | Three different outlier algorithms used inconsistently — unified to IQR |
| __int128 | 🔒 Consensus | Integer overflow in collateral math at extreme values wrapped to near-zero (4 locations) |
| float | 🔒 Consensus | Floating-point division in partial redemption — different results on different CPUs |

### 41 Verified Defenses (No Fix Needed)

These attacks were tested and confirmed to already be properly defended:

- Eclipse/Sybil oracle flooding (fail-closed)
- Miner DD transaction reordering and censorship
- Block timestamp manipulation for DD advantage
- Selfish mining to delay BIP9 activation
- Double-spend via conflicting DD mempool transactions
- Reorg-based collateral theft
- Same-block mint/transfer/redeem ordering attacks
- MAX_MONEY DD mint overflow
- Zero-amount DD operations
- BIP9 activation boundary edge cases (block 599/600/601)
- Rapid mint/redeem wallet consistency
- Unconfirmed DD chain ancestor limit attacks
- Oracle key rotation during consensus
- Stale oracle subset (5 active + 2 stale out of 9)
- And 27 more...

---

## 🔐 NEW: Phase 2 Multi-Oracle Schnorr Signatures

RC19 introduces a new **two-round consensus attestation protocol** for oracle price verification:

- **Round 1:** Each oracle independently signs `H(oracle_id, consensus_price, consensus_timestamp)`
- **Round 2:** Signatures are aggregated — 5-of-9 threshold required for consensus
- **On-chain:** Full individual signatures included in blocks for decentralized verification
- **Red team tested:** 15 exploit scenarios verified — all defenses held
- **Activates at same height as Phase 1** — no separate deployment, mainnet goes straight to multi-oracle

This replaces the Phase 1 single-oracle signature model and ensures no single oracle can dictate prices.

---

## 🐛 Bug Fixes

- **Wallet recovery redeem button** — Fixed redeemed vaults showing as active after wallet restore; added post-rescan UTXO validation
- **Oracle log spam** — Fixed duplicate oracle price messages flooding debug.log from P2P hash mismatch; added deduplication + converted to debug-only logging
- **Bad auto-merge** — Fixed 4 instances where git merge silently dropped DD amount consistency checks
- **Array bounds check** — Fixed out-of-bounds access in multi-algo block index tracking
- **ABI mismatch segfault** — Fixed null m_wallet pointer causing crash on wallet operations
- **Cross-test deduplication** — Fixed seen_message_hashes leaking between test runs
- **Oracle benchmark** — Fixed missing min_required parameter after threshold refactor

---

## 📊 All Changes: RC18 → RC19 (One-Line Summary)

| Category | Count | Summary |
|----------|-------|---------|
| 🔒 Security fixes | 29 | CVE-grade inflation, consensus bypasses, oracle exploits, wallet encryption, P2P relay attacks |
| ✅ Verified defenses | 41 | Attack tests confirming existing protections hold |
| 🔐 Consensus upgrades | 4 | Phase 2 Schnorr sigs, block-extracted oracle price, IQR unification, incremental ERR tracking |
| 🐛 Bug fixes | 11 | Wallet recovery, log spam, segfaults, merge conflicts, array bounds |
| 📚 Documentation | 6 | Repo maps, integration guides, security docs |
| 🔧 Build/CI | 7 | macOS ARM64, OpenSSL flags, copyright, BIP9 dates |
| 🧪 Test infrastructure | 2 | Testnet validation V10 (5-of-9), Phase 2 red team (15 exploit tests) |
| 🌐 Network | 1 | 9th oracle (JohnnyLawDGB), 5-of-9 consensus, testnet19 |

**Total: 132 commits (excluding merges)**

---

## 🧪 Testing

### Test Results
- **1,948 C++ unit tests** — all passing (19 new from audit)
- **311 functional tests** — all passing (293 passed, 16 skipped)
- **172 testnet integration tests** — all passing (V10 script, 5 Qt wallets, 5-of-9 oracles)
- **70+ Red Hornet adversarial tests** — comprehensive attack coverage

### Testnet Validation Script (V10)
The full integration test runs 5 Qt wallet instances with 5 live oracles and validates:
- BIP9 activation lifecycle
- Minting at all 9 collateral tiers
- Transfer chains across all participants
- Redemption after lock expiry + early redemption rejection
- Oracle consensus, below-threshold rejection, outlier filtering
- Wallet restart, backup/restore, and reindex persistence
- DD supply conservation at every step

---

## Technical Changes

| File | Change |
|------|--------|
| `src/kernel/chainparams.cpp` | Testnet19 chain params, P2P port 12033, 9th oracle, 5-of-9 consensus |
| `src/chainparamsbase.cpp` | Testnet19 data dir and ports |
| `src/consensus/digidollar.cpp` | Block-extracted oracle price, IQR outlier filtering, incremental ERR |
| `src/oracle/schnorr.cpp` | Phase 2 two-round consensus attestation |
| `src/oracle/bundle_manager.cpp` | Deterministic consensus pricing, relay deduplication |
| `src/oracle/exchange.cpp` | Oracle price expiry, staleness detection |
| `src/validation.cpp` | ConnectBlock DD validation with block-extracted price |
| `src/wallet/digidollar_wallet.cpp` | Encrypted DD keys, mutex, watch-only isolation, position state validation |
| `src/net_processing.cpp` | P2P oracle replay/relay fixes, seen hash alignment |
| `src/test/digidollar_redteam_tests.cpp` | 15 Phase 2 Schnorr exploit tests |

---

## Commits Since RC18

### Consensus Upgrades
```
1f0767daed consensus: Phase 2 multi-oracle Schnorr signatures (T5-03)
d920c5d781 consensus: use block-extracted oracle price for DD validation (T8-03)
806aec150c consensus: unify oracle median/outlier formula to IQR (T9-01)
e128f3bf5f consensus: incremental DD supply/collateral tracking for ERR (T5-06)
ea8b7881c7 consensus: add JohnnyLawDGB as oracle 8, bump to testnet19 (5-of-9 consensus)
a0217afada consensus: fix integer overflow in ValidateCollateralRatio()
b0e5e6d41d consensus: set DigiDollar mainnet BIP9 window to May 2026 - May 2028
```

### Security Fixes (29)
```
9c8abf17b9 security: [T1-02] fix OP_RETURN DD amount inflation attack (CVE-grade)
fb207f531c security: [T1-03-FIX] verify lockHeight matches lockTier in mint OP_RETURN
fcca13e25a security: [T1-04f] reject mint transactions with multiple DD OP_RETURN outputs
f995f92ad6 security: [T1-05a] fix skipOracleValidation — only skip during IBD
6237953bec security: [T1-08] fix collateral release bypass via missing DD amount lookup
f91cd93039 security: [T1-06] fix division by zero in CalculateSystemHealth()
193e56d2fd security: [T1-07] reject oracle_id > 255 to prevent truncation
c089d99fce security: [T1-08] add regtest-only runtime guards to MockOracleManager
96535df61e security: [T2-01] fix absolute lockHeight used as relative lock period
01aab48617 security: [T2-02] fix transfer conservation bypass via non-DD source tx
44b7af6886 security: [T2-03] fix partial burn collateral theft via miner fees
9bff35f1cb security: [T2-04] fix oracle price staleness exploit — add expiry
dbd6d9443d security: [T2-05a] implement real GetSystemCollateralRatio from UTXO data
a8d21b3da4 security: [T2-05b] fix unit mismatch in GetCurrentSystemHealth
d2300f47ad security: [T2-05c] fix ShouldBlockMinting — fail-closed when metrics unavailable
38b78aabe1 security: [T2-06b] fix collateral masquerade as fee input in redemptions
eb7ee4f5cd security: [T3-02] fix ORACLEBUNDLE P2P relay amplification
b7688a97ff security: [T3-03] fix time-dependent consensus price
0e3b0dd079 security: [T3-04] fix P2P oracle message replay via nonce mutation
6038242e45 security: [T3-05a] fix oracle consensus threshold — use chainparams everywhere
656a3bac0b security: [T4-02d] add mutex to DigiDollar wallet — prevent data races
2965bcb3df security: [T4-03a] encrypt DD private keys when wallet is encrypted
c087f7f4e3 security: [T4-04] fix watch-only DD balance contamination
24b4bc6301 security: [T5-02] fix coinbase DD marker bypass — DD from nothing
ddfc3b1a14 security: [T5-04] fix missing HasDigiDollarMarker check in collateral release
95d6151144 audit: [T2-05] DCA/ERR health calculation gaming — 4 bugs found
```

### Bug Fixes
```
9153e07692 fix: eliminate oracle log spam from P2P duplicate hash mismatch
ea3137ed3d fix: mark redeemed vaults inactive during wallet recovery
e57a353cc8 fix: array bounds check in multi-algo block index tracking
e42e73601c fix: add in-class nullptr initializer for m_wallet to prevent segfault
f3b96c4118 fix: null-safe locking in DigiDollar wallet to prevent segfault
4faf2bcc6e fix: clear seen_message_hashes to prevent cross-test dedup contamination
7587f9060d fix: pass min_required to GetConsensusPrice() in oracle benchmark
ec38053c0b fix: pass oracle price from ValidationContext to ShouldBlockMinting
ee5a9b5f57 fix: resolve bad auto-merge that dropped DD amount consistency check
10a99f6aa0 fix: update JohnnyLawDGB oracle pubkey (regenerated fresh wallet)
```

### Tests & Audit (42)
```
7b58bb41ff audit: RED HORNET Phase 2 Schnorr signature security tests (15 exploit tests)
faa90612fa test: upgrade testnet validation to 5-of-9 oracle consensus (V10)
... (40 additional audit verification commits)
```

### Documentation
```
6bc5a27c86 docs: add REPO_MAP.md (1,781 lines) and REPO_MAP_DIGIDOLLAR.md (957 lines)
385bcf3bf8 docs: REPO_MAP_GUIDE.md — AI agent codebase navigation guide
c9c1574f55 docs: fix accuracy issues in integration guides
14bf99ea68 docs: add DigiDollar wallet and exchange integration guides
96c72a0ab8 docs: document deterministic IV design in wallet crypter
6b6acf35d1 docs: improve accuracy of deterministic IV security documentation
```

### Build & Release
```
73c54c9c2d fix: OpenSSL build — move -D_GNU_SOURCE to CFLAGS
a72c9b95b1 fix: OpenSSL Configure error by routing flags through make variables
67155fdc18 build: fix CI depends failures for macOS ARM64 and Linux
cdc6c1ac18 update: copyright year from 2025 to 2026
36221d144c fix: restore mainnet Taproot BIP9 dates to match v8.26.2
```

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up control of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Wallet GUI Guide

### How to Use DigiDollar in the Wallet

1. **Open the DigiDollar Tab** — Click "DigiDollar" in the top navigation bar (will show activation status until BIP9 activates)
2. **Mint DigiDollars** — Lock DGB as collateral to create DUSD. Enter the amount and confirm.
3. **Send DigiDollars** — Use the DigiDollar tab's send function or "Send DGB" with a DD address
4. **Receive DigiDollars** — Click "Receive DGB" to get your DD-capable address
5. **View History** — DD Transactions tab shows complete transaction history (auto-refreshes)
6. **Redeem DigiDollars** — Burn DUSD to unlock your DGB collateral after the lock period expires
7. **Coin Control** — Use manual DD input selection for advanced redemptions
8. **Address Book** — Save frequently used DD addresses for quick sending
9. **Export History** — Export DD transactions to CSV for record keeping
10. **Mask Values** — Enable "Mask Values" to hide all balances and amounts across DGB and DigiDollar tabs for privacy

---

## Oracle Operator Setup

### Prerequisites
- DigiByte Core RC19 built from source with curl support (or download the binary)
- An assigned oracle ID (0–8 for testnet, contact the maintainer)

### New Oracle Setup (First Time)

```bash
# Step 1: Start your node
digibyted -testnet -daemon

# Step 2: Create a wallet
digibyte-cli -testnet createwallet "oracle"

# Step 3: Generate your oracle key (one-time)
digibyte-cli -testnet -rpcwallet=oracle createoraclekey <your_oracle_id>

# Step 4: Start the oracle
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### Existing Oracle — Upgrading from RC18 or Earlier

**Your oracle keys are safe.** See the **Testnet Reset** section at the top for migration steps. The key point: copy your oracle wallet from the old `testnet18` directory to the new `testnet19` directory.

After migration:
```bash
# Step 1: Start your node with RC19
digibyted -testnet -daemon

# Step 2: Load your existing wallet
digibyte-cli -testnet loadwallet "oracle"

# Step 3: Start the oracle
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

> **⚠️ After every node restart, you must re-load your wallet and re-start the oracle.** Your key persists in the wallet — you never need to run `createoraclekey` again.

### Qt Wallet Users

1. Start DigiByte Qt with `-testnet`
2. Go to **File → Open Wallet → oracle** to load your wallet
3. Go to **Help → Debug Window → Console**
4. Type: `startoracle <your_oracle_id>`

### Current Oracle Operators (Testnet)

| ID | Operator | Status |
|----|----------|--------|
| 0 | Jared | ✅ Active |
| 1 | Green Candle | ✅ Active |
| 2 | Bastian | ✅ Active |
| 3 | DanGB | ✅ Active |
| 4 | Shenger | ✅ Active |
| 5 | Ycagel | ✅ Active |
| 6 | Aussie Epic | ✅ Active |
| 7 | LookIntoMyEyes | ✅ Active |
| 8 | JohnnyLawDGB | 🆕 New in RC19 |

For the complete guide, see **`DIGIDOLLAR_ORACLE_SETUP.md`**.

---

## Complete RPC Command Reference

### DigiDollar Commands (Wallet)

| Command | Description |
|---------|-------------|
| `mintdigidollar` | Mint DigiDollars by locking DGB as collateral |
| `senddigidollar` | Send DigiDollars to another address |
| `redeemdigidollar` | Redeem DigiDollars to unlock DGB collateral |
| `getdigidollarbalance` | Show your DigiDollar balance |
| `listdigidollarpositions` | List your active collateral positions |
| `listdigidollartxs` | List your DigiDollar transaction history |
| `getdigidollaraddress` | Get or create a DigiDollar receive address |
| `validateddaddress` | Validate a DigiDollar address |
| `listdigidollaraddresses` | List all DigiDollar addresses in your wallet |
| `importdigidollaraddress` | Import a DigiDollar address for watch-only |
| `getdigidollarstats` | Get network-wide DigiDollar statistics |
| `getdigidollardeploymentinfo` | Get DigiDollar activation/deployment status |
| `calculatecollateralrequirement` | Calculate DGB collateral needed for a DD mint |
| `estimatecollateral` | Estimate collateral requirement by tier |
| `getdcamultiplier` | Get the current DCA multiplier for collateral |
| `getredemptioninfo` | Get info about redeeming a specific position |
| `getprotectionstatus` | Check if liquidation protection is active |

### Oracle Commands

| Command | Description |
|---------|-------------|
| `createoraclekey <id>` | Generate a new oracle Schnorr keypair (one-time) |
| `getoraclepubkey <id>` | Show the oracle public key stored in your wallet |
| `startoracle <id>` | Start running as an oracle operator |
| `stoporacle <id>` | Stop your oracle |
| `getoracleprice` | Get the consensus price |
| `getalloracleprices` | Per-oracle breakdown |
| `getoracles` | Network-wide oracle status |
| `listoracle` | Show your local oracle status |
| `sendoracleprice` | Manually submit a price (testing) |

---

## Upgrade Notes

### ⚠️ This is a Testnet Reset

**RC19 uses a new chain: testnet19 (port 12033).** Your old testnet data is incompatible.

**Steps to upgrade:**

1. **Save your oracle wallet** (if you run an oracle — see Oracle migration section above)
2. **Close your old wallet**
3. **Delete old testnet data:**
   - **Linux:** Delete `~/.digibyte/testnet18/` (and any older testnet directories)
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet18\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet18/`
4. **Download and install RC19**
5. **Update your `digibyte.conf`** — the `addnode` addresses remain the same:
   ```ini
   testnet=1
   
   [test]
   digidollar=1
   addnode=oracle1.digibyte.io
   ```
6. **Launch with `-testnet`** — a new `testnet19` directory will be created automatically
7. **Restore your oracle wallet** if applicable (see migration steps above)

---

## Configuration

### Minimum digibyte.conf for DigiDollar Testing:
```ini
testnet=1

[test]
digidollar=1
addnode=oracle1.digibyte.io
```

### Optional Settings:
```ini
[test]
txindex=1
digidollarstatsindex=1
algo=sha256d
```

---

## Known Issues

- BIP9 activation requires sufficient mining hashrate to progress through signaling windows
- `startoracle` must be re-run after every node restart
- DigiDollar features are disabled until BIP9 activation completes (~block 600 with continuous mining)

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (testnet19) |
| Default P2P Port | **12033** |
| Default RPC Port | **14025** |
| Oracle Node | oracle1.digibyte.io |
| Address Prefix | dgbt1... (bech32) |
| Multi-Algo Activation | Block 100 |
| BIP9 Signaling Start | Block 200 |
| BIP9 Threshold | 70% (140 of 200 blocks) |
| DD Activation (earliest) | Block 600 |
| Oracle Consensus | **5-of-9** Schnorr threshold |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc19-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc19-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc19-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc19-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc19-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc19-aarch64-linux-gnu.tar.gz` |

---

## Quick Start

### New to DigiDollar?

1. Download the binary for your platform (see Downloads above)
2. Create your config file (see Configuration section)
3. Launch with `-testnet` flag: `./digibyte-qt -testnet`
4. Wait for the blockchain to sync
5. Monitor BIP9 activation: `digibyte-cli -testnet getdigidollardeploymentinfo`
6. Once activated, the DigiDollar tab becomes fully functional
7. You need DGB to mint — ask in Gitter and someone can send you testnet DGB

### Want to Run an Oracle?

See the **Oracle Operator Setup** section above, or read `DIGIDOLLAR_ORACLE_SETUP.md` for the complete guide.

---

## Troubleshooting

### "DigiDollar tab says Not Activated"
- This is expected! RC19 uses real BIP9 activation. DigiDollar features unlock after miners signal support and the activation threshold is reached.
- Monitor progress: `digibyte-cli -testnet getdigidollardeploymentinfo`

### "Can't connect to network"
- RC19 uses **port 12033** (not 12032 or earlier). Check your firewall.
- Make sure `addnode=oracle1.digibyte.io` is under `[test]` in your config

### "Old testnet data"
- Delete ALL old testnet directories (testnet10 through testnet18) — RC19 uses testnet19

### "No wallet is loaded" when running oracle commands
- Load your wallet first: `digibyte-cli -testnet loadwallet "oracle"`
- For Qt: **File → Open Wallet → oracle**

### "Mining not working"
- Blocks 0-99 are scrypt-only. After block 100, multi-algo activates.
- Set `algo=sha256d` in config for fastest CPU mining after block 100

### "Oracle price shows 0 or N/A"
- Oracle prices require BIP9 activation first
- Wait for activation and sufficient oracle operators to come online

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

When reporting bugs, start your message with **BUG:** and include: what happened, steps to reproduce, platform (Windows/Linux/Mac), and any error messages from your debug.log file.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
