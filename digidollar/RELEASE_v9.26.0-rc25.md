# DigiByte v9.26.0-rc25 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ SAME TESTNET — NO RESET

**RC25 uses the same testnet19 chain as RC19–RC24.** No data migration needed.

- **No chain reset** — your blockchain data, wallets, and oracle keys all carry over
- **Same ports:** P2P **12033**, RPC **14025**
- **Same oracle consensus:** **5-of-9** Schnorr threshold
- **Just update the binary** and restart your node

### 🔑 Oracle Operators — NEW: Auto-Start!

**RC25 eliminates the #1 operator complaint: you no longer need to manually restart your oracle after every node restart.**

- **Unencrypted wallets:** Oracle starts automatically when the wallet loads — zero manual steps.
- **Encrypted wallets:** Oracle starts automatically after you unlock with `walletpassphrase`. A log message on startup tells you exactly what to do.

If auto-start doesn't activate (e.g., changed wallet files), you can still manually start:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

Qt users: **File → Open Wallet → oracle**, then **Help → Debug Window → Console** → `startoracle <your_oracle_id>`

---

## What's New in RC25

RC25 is a **stability and reliability release** targeting seven bugs reported by testnet operators, plus two community-contributed fixes for Dandelion++ and UTXO management. The headline fix is Bug #16 — a critical block production halt that could stop the entire chain.

**If you experienced block production stalls, oracle restart issues, stale price broadcasts, wallet rescan errors, or UTXO fragmentation preventing mints, upgrade to RC25.**

### Bug Fixes

1. **Block production halt from stale DD transactions (Bug #16 — Critical)** — When a DigiDollar mint sat in the mempool while the oracle price changed, `TestBlockValidity()` threw a fatal error and halted block production entirely. DD transactions are now pre-validated against the current oracle price in `addPackageTxs()`, invalid ones are skipped, and `TestBlockValidity()` retries without offending DD transactions on failure. Mints also include a 1% collateral safety margin to prevent knife-edge invalidation.

2. **Rescan false positive on matured mints (Bug #22)** — Wallet rescans triggered `bad-mint-lock-height-mismatch` for historical mints whose lock period had already expired. The lock-height comparison was checking against current chain tip instead of original confirmation height. Now skips the tier consistency check entirely for matured mints, since it's only relevant at acceptance time.

3. **txindex not enforced for DD-enabled nodes (Bug #21)** — DigiDollar requires `txindex=1` but nodes could start without it and fail unpredictably. Startup now checks for txindex when DigiDollar is enabled and exits with a clear error message if missing. Also documents correct config section placement (`[test]` for testnet, `[main]` for mainnet).

4. **Oracle keys not loading after restart (Bug #2)** — Oracle operators had to manually `loadwallet` + `startoracle` after every restart. Unencrypted wallets now auto-start oracles on load. Encrypted wallets auto-start after `walletpassphrase` unlock, with a startup log message guiding operators. Encrypted wallets are never auto-unlocked.

5. **Attestation quorum timing failures (Bug #4)** — Block templates could be built before the oracle quorum formed, producing zero-price blocks. A bounded wait window (default 2 seconds, configurable via `-oraclequorumwaitms`) now pauses template creation when quorum is one oracle away. Falls back to previous behavior on timeout.

6. **Oracle price staleness tracking (Bug #3)** — `HasValidPrice()` returned true indefinitely as long as any price had been broadcast, even hours-old stale data. Now checks broadcast timestamp against `ORACLE_MAX_AGE_SECONDS`. Added consecutive fetch failure tracking with escalating warnings at 5, 15, and every 30 failures.

7. **Partial redemption code removed (Bug #19)** — Removed dead partial redemption code paths from `CloseCollateralPosition()`. Partial redemptions are architecturally impossible in the UTXO model — the entire collateral UTXO is consumed as `vin[0]`, and consensus enforces `ddBurned >= originalDDMinted`. Eliminates a potential confusion vector.

### Community Contributions

- **Dandelion stempool ancestor leak (PR #392 — JohnnyLawDGB)** — Confirmed transactions weren't being purged from the Dandelion stempool on block connect, inflating ancestor counts until new transactions hit the 25-ancestor limit. `removeForBlock()` now cleans both mempool and stempool.
- **UTXO fragmentation blocks minting (PR #391 — JohnnyLawDGB)** — Wallets with many small UTXOs couldn't mint because no single transaction could cover collateral. `mintdigidollar` now auto-consolidates fragmented UTXOs before retrying the mint.

### Other Changes

- **Functional test suite updated for txindex enforcement** — All 30 DigiDollar functional test files updated with `-txindex=1`, plus fixes for collateral tracking values and Dandelion stempool timing in network relay tests. Full suite: 311/311 pass.

---

## 🧪 Testing

### Test Results
- **C++ unit tests** — all passing (55+ new tests for this release)
- **Python functional tests** — 311/311 passing
- **DigiDollar functional tests** — all passing
- **Live testnet verification** — all DigiDollar/Oracle RPCs verified on testnet19

---

## Commits Since RC24

```
8b972f3cdd fix: add -txindex=1 to all DigiDollar functional tests
f549227883 fix: add oracle price staleness tracking (Bug #3)
6c2b64789c fix: remove partial redemption code, enforce full-only (Bug #19)
e829517773 version: bump to v9.26.0-rc25, update wallet image and release notes
fd9135af23 fix: add bounded wait window for near-quorum oracle bundles (Bug #4)
a927e2e556 fix: auto-start oracles from wallet keys on load/unlock (Bug #2)
77a4c584ae fix: enforce txindex=1 for DigiDollar-enabled chains at startup (Bug #21)
ff2d8d5b17 fix: prevent block production halt from stale DD transactions (Bug #16)
641e0f8698 fix: skip tier consistency check for matured mints during rescan (Bug #22)
2aed101e68 Merge pull request #392 from JohnnyLawDGB/fix/dandelion-stempool-ancestor-leak
eb07fb7244 Merge pull request #391 from JohnnyLawDGB/fix/utxo-consolidation-mint
13f98b4a63 fix: purge confirmed txs from Dandelion stempool on block connect
41e3f66969 fix: auto-consolidate fragmented UTXOs when minting DigiDollar
```

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up custody of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Oracle Operator Setup

### Upgrading from RC24

```bash
digibyte-cli -testnet stop
# Replace binary
digibyted -testnet -daemon
# Oracle auto-starts from wallet! No manual steps needed for unencrypted wallets.
# Encrypted wallets: run walletpassphrase to trigger auto-start.
```

### Upgrading from RC23 or Earlier

```bash
digibyte-cli -testnet stop
# Replace binary
digibyted -testnet -daemon
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
# After this first manual start, future restarts will auto-start.
```

### New Oracle Setup

```bash
digibyted -testnet -daemon
digibyte-cli -testnet createwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle createoraclekey <your_oracle_id>
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
# Future restarts will auto-start your oracle.
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
txindex=1
addnode=oracle1.digibyte.io
```

> **⚠️ New in RC25:** `txindex=1` is now enforced at startup for DD-enabled nodes. Make sure it's in the correct section (`[test]` for testnet, `[main]` for mainnet). Global placement (above all sections) also works.

---

## Known Issues

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
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc25-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc25-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc25-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc25-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc25-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc25-aarch64-linux-gnu.tar.gz` |

---

## Troubleshooting

### "Block production halted with insufficient-collateral" (FIXED in RC25)
Stale DD transactions in the mempool could crash `CreateNewBlock()`. RC25 pre-validates DD transactions and retries without them on failure.

### "bad-mint-lock-height-mismatch during rescan" (FIXED in RC25)
Historical mints with expired lock heights triggered false validation errors. RC25 skips tier checks for matured mints.

### "Node fails silently without txindex" (FIXED in RC25)
Startup now requires `txindex=1` for DD-enabled nodes with a clear error message.

### "startoracle fails after restart" (FIXED in RC25)
Oracle keys now auto-start from the wallet. Unencrypted wallets need zero manual steps; encrypted wallets auto-start after `walletpassphrase`.

### "Zero-price blocks during rapid block production" (FIXED in RC25)
Near-quorum attestation bundles now wait up to 2 seconds for the final oracle before giving up.

### "Oracle keeps broadcasting stale prices" (FIXED in RC25)
`HasValidPrice()` now checks broadcast age. Stale prices are rejected and consecutive fetch failures trigger escalating warnings.

### "Insufficient fee inputs for calculated fee" on redemption (FIXED in RC24)
### "listdigidollaraddresses returns mock data" (FIXED in RC24)
### "getredemptioninfo shows same data for every position" (FIXED in RC24)
### "startoracle fails with 'Oracle not configured' after restart" (FIXED in RC23)
### "Oracle consensus stalled and never recovered" (FIXED in RC23)
### "Oracle prices stale or missing from blocks" (FIXED in RC22)
### "Sync stuck at block 7586" (FIXED in RC21)

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues

When reporting bugs include: what happened, steps to reproduce, platform, and error messages from debug.log.
