# DigiByte v9.26.0-rc23 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ SAME TESTNET — NO RESET

**RC23 uses the same testnet19 chain as RC19–RC22.** No data migration needed.

- **No chain reset** — your blockchain data, wallets, and oracle keys all carry over
- **Same ports:** P2P **12033**, RPC **14025**
- **Same oracle consensus:** **5-of-9** Schnorr threshold
- **Just update the binary** and restart your node

### 🔑 Oracle Operators — IMPORTANT

After upgrading to RC23, **you must reload your oracle wallet and restart your oracle:**

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

Qt users: **File → Open Wallet → oracle**, then **Help → Debug Window → Console** → `startoracle <your_oracle_id>`

> **⚠️ After every node restart, you must re-load your wallet and re-start the oracle.** Your key persists in the wallet — you never need to run `createoraclekey` again.

---

## What's New in RC23

RC23 is a **stability and correctness release** targeting four issues identified during RC22 testnet operations:

1. **Oracle consensus deadlock from stale attestation timestamps** — oracles could permanently stall when all nodes generated identical Phase 2 hashes, blocking quorum indefinitely
2. **Oracle keys failing to load after node restart** — `startoracle` returned "Oracle not configured" on restart due to a key compression flag not being preserved on database read
3. **Oracle consensus proposals not broadcasting after quorum** — proposals were only emitted during block creation with a rate limit, causing permanent Phase 2 quorum failure if remote attestations arrived late
4. **Four DigiDollar RPC display bugs** — `getdigidollarstats`, `getoracleprice`, `getalloracleprices`, and `getdigidollarbalance` all had reporting errors (display-only; no consensus or math affected)

**If you experienced oracle stalls, "Oracle not configured" errors after restart, or incorrect RPC output, upgrade to RC23.**

---

## 🐛 Bug Fix #1: Oracle Consensus Deadlock (Stale Attestation Timestamps)

**PR #383**

**The problem:** When rapid block production or a network partition caused oracle consensus to stall, all oracle nodes computed attestations with the same frozen `(price, timestamp)` tuple. Because the Phase 2 hash is derived from `(oracle_id, price, timestamp)`, every node produced an identical hash each cycle. The deduplication filter then silently discarded all incoming Phase 2 messages as duplicates — permanently blocking quorum for the remainder of that epoch (1,440 blocks).

**Fix:** Two changes:
1. Phase 2 message hashes now incorporate a monotonically increasing nonce derived from the current block height, guaranteeing each consensus cycle produces a unique hash regardless of whether price and timestamp are frozen.
2. Stale attestation state is cleaned up every 300 seconds, preventing the seen-message hash set from blocking legitimate recovery attempts.

**Effect:** Oracles now self-recover from consensus stalls without manual intervention.

---

## 🐛 Bug Fix #2: Oracle Keys Not Loading After Restart

**The problem:** `startoracle` failed with "Oracle not configured" every time the node was restarted. Users had to re-run `createoraclekey` repeatedly.

**Root cause:** `ReadOracleKey()` used `CKey::Load()` which does not preserve the `fCompressed` flag. After the database read, derived pubkeys had wrong compression — Schnorr key derivation produced a different pubkey than the one registered on-chain.

**Fix:** After loading via `CKey::Load()`, the compressed flag is explicitly restored by calling `CKey::Set()` with correct compression. The pubkey now matches on restart.

---

## 🐛 Bug Fix #3: Oracle Consensus Proposals Not Broadcasting After Quorum

**The problem:** If remote attestations arrived after the first block of an epoch, `BroadcastConsensusProposal()` was never called again for the remaining 1,439 blocks, causing permanent Phase 2 quorum failure.

**Root cause:** `BroadcastConsensusProposal()` was gated behind a once-per-epoch rate limiter inside `CreateNewBlock()`.

**Fix:** Consensus proposals are now also broadcast immediately when quorum is first detected, independently of block creation.

---

## 🐛 Bug Fix #4: Four DigiDollar RPC Display Bugs

All four are display/API layer bugs — consensus, minting, and redemption math are unaffected.

- **`getdigidollarstats` `active_positions` always 0** — hardcoded placeholder; now reads live vault count from `DigiDollarStatsIndex`
- **`getoracleprice` sub-cent prices rounded to 1** — floor clause removed; sub-cent prices now reported accurately
- **`getalloracleprices` stale per-oracle prices after restart** — cache entries now invalidated on oracle restart
- **`getdigidollarbalance` unconfirmed DD in confirmed field** — `confirmed` now only counts outputs with ≥1 confirmation; `unconfirmed` shows 0-conf outputs

---

## 🧪 Testing

### New Tests Added in RC23

| Test | What It Verifies |
|------|-----------------|
| `oracle_consensus_deadlock_recovery` | Oracles self-recover from stale-hash stalls |
| `oracle_nonce_prevents_duplicate_filter` | Phase 2 nonce ensures unique hashes per cycle |
| `oracle_stale_cleanup_fires` | Stale attestation cleanup runs on schedule |
| `oracle_key_load_compressed_flag` | Oracle key compression preserved after DB read |
| `oracle_startoracle_after_restart` | `startoracle` succeeds after node restart |
| `oracle_proposal_broadcast_on_quorum` | Proposals broadcast when quorum detected |
| `rpc_active_positions_reads_stats_index` | `getdigidollarstats.active_positions` reads live data |
| `rpc_oracle_price_subcent_accurate` | Sub-cent DGB prices not rounded in `getoracleprice` |
| `rpc_alloracleprices_cache_invalidated` | Stale per-oracle prices cleared after restart |
| `rpc_balance_confirmed_vs_unconfirmed` | Confirmed/unconfirmed DD balance split accurate |

### Test Results
- **1,963 C++ unit tests** — all passing
- **DigiDollar functional tests** — all passing
- **Testnet live verification** — oracle self-recovery confirmed, RPC outputs verified

---

## 📊 All Changes: RC22 → RC23

| Category | Count | Summary |
|----------|-------|---------|
| 🐛 Bug fixes | 4 | Oracle deadlock, oracle key load, proposal broadcast, RPC display |
| 🧪 Tests | 10 | Oracle consensus + wallet key + RPC correctness |
| 📦 Version | 1 | Bump to v9.26.0-rc23, update wallet image |

---

## Technical Changes

| File | Change |
|------|--------|
| `src/oracle/consensus.cpp` | Phase 2 nonces; stale hash cleanup every 300s |
| `src/oracle/consensus.h` | Nonce tracking; cleanup timer declaration |
| `src/wallet/oracle_wallet.cpp` | Restore `fCompressed` flag after `CKey::Load()` |
| `src/oracle/bundle_manager.cpp` | Broadcast proposal on quorum detection |
| `src/rpc/digidollar.cpp` | Fix `active_positions`, sub-cent price, per-oracle cache, balance split |
| `src/test/digidollar_oracle_tests.cpp` | 6 new oracle consensus + key tests |
| `src/test/digidollar_rpc_tests.cpp` | 4 new RPC output correctness tests |
| `configure.ac` | Version bump RC22 → RC23 |
| `src/qt/res/icons/digibyte_wallet.png` | Updated wallet splash image |

---

## Commits Since RC22

```
272d44092b fix(rpc): resolve 4 DigiDollar RPC display bugs reported in RC22
94f8392835 oracle: fix consensus deadlock from stale attestation timestamps
eecd086791 fix(wallet): restore compressed flag when loading oracle keys from database
d00e683072 fix(oracle): broadcast consensus proposals proactively on quorum
6ff7e70800 version: bump to v9.26.0-rc23, update wallet image
```

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up custody of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Oracle Operator Setup

### Upgrading from RC22

```bash
digibyte-cli -testnet stop
# Replace binary
digibyted -testnet -daemon
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### New Oracle Setup

```bash
digibyted -testnet -daemon
digibyte-cli -testnet createwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle createoraclekey <your_oracle_id>
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

For the complete guide including RC18-and-earlier migration, see **`DIGIDOLLAR_ORACLE_SETUP.md`**.

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
| 8 | JohnnyLawDGB | ✅ Active |

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
| `createoraclekey <id>` | Generate oracle Schnorr keypair (one-time) |
| `getoraclepubkey <id>` | Show oracle public key from wallet |
| `startoracle <id>` | Start running as an oracle operator |
| `stoporacle <id>` | Stop your oracle |
| `getoracleprice` | Get the consensus price |
| `getalloracleprices` | Per-oracle price breakdown |
| `getoracles` | Network-wide oracle status |
| `listoracle` | Show local oracle status |
| `sendoracleprice` | Manually submit a price (testing) |

---

## Configuration

```ini
testnet=1

[test]
digidollar=1
addnode=oracle1.digibyte.io
```

---

## Known Issues

- `startoracle` must be re-run after every node restart
- DigiDollar features disabled until BIP9 activation (~block 600 with continuous mining)
- Oracle prices show as 0 until sufficient operators restart after upgrading
- Stale redeem buttons for already-redeemed positions when restoring via descriptor import (cosmetic)

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (testnet19) |
| Default P2P Port | **12033** |
| Default RPC Port | **14025** |
| Oracle Node | oracle1.digibyte.io |
| Oracle Consensus | **5-of-9** Schnorr threshold |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc23-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc23-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc23-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc23-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc23-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc23-aarch64-linux-gnu.tar.gz` |

---

## Troubleshooting

### "startoracle fails with 'Oracle not configured' after restart" (FIXED in RC23)
Upgrade to RC23 — oracle key compression is now correctly preserved on database read.

### "Oracle consensus stalled and never recovered" (FIXED in RC23)
RC23 adds Phase 2 nonces and automatic stale-state cleanup for self-recovery.

### "getdigidollarstats shows active_positions: 0" (FIXED in RC23)
Now reads live vault count from the stats index.

### "Oracle prices stale or missing from blocks" (FIXED in RC22)
### "Unconfirmed mint balance shown as spendable" (FIXED in RC22)
### "Sync stuck at block 7586" (FIXED in RC21)

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues

When reporting bugs include: what happened, steps to reproduce, platform, and error messages from debug.log.
