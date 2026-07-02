# DigiByte v9.26.0-rc26 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ SAME TESTNET — NO RESET

**RC26 uses the same testnet19 chain as RC19–RC25.** No data migration needed.

- **No chain reset** — your blockchain data, wallets, and oracle keys all carry over
- **Same ports:** P2P **12033**, RPC **14025**
- **Same oracle consensus:** **5-of-9** Schnorr threshold
- **Just update the binary** and restart your node

### 🔑 Oracle Operators

Oracles auto-start from wallet keys (introduced in RC25). No manual steps needed for unencrypted wallets. Encrypted wallets auto-start after `walletpassphrase`.

---

## What's New in RC26

RC26 is a **bug fix and UX polish release** resolving 12 issues reported by testnet operators. This release completes the entire DigiDollar bug tracker — **all 23 tracked bugs are now resolved.**

**If you experienced phantom fees on redemptions, wallet shutdown hangs, confusing error messages on rapid sends, or UTXO fragmentation blocking mints, upgrade to RC26.**

### Bug Fixes

1. **Qt wallet hangs indefinitely on shutdown (Bug #23)** — After running for days with a time-locked position, closing the wallet got stuck on "DigiByte is shutting down..." for 19+ hours. The 60-second refresh timer in the DigiDollar positions widget was never stopped during shutdown, causing a deadlock on `cs_dd_wallet`. Timer is now stored as a member variable and stopped in three places: destructor, wallet model teardown, and a `ShutdownRequested()` guard in the callback.

2. **gettransaction reports entire collateral as fee on redemption (Bug #17)** — After a successful redemption, `gettransaction` showed ~240K DGB as the fee instead of the actual ~0.15 DGB miner fee. The standard Bitcoin fee calculation doesn't understand DD collateral unlock semantics. Now detects `DD_TX_REDEEM` transactions and computes the true fee by summing all input values from the wallet. Fixed in both `gettransaction` RPC and `GetAmounts()` helper.

3. **Opaque error on rapid DigiDollar sends (Bug #10)** — Sending DigiDollar twice before the first transaction confirmed returned a raw consensus rejection: `"dd-input-amounts-unknown"`. Now intercepted in the RPC layer with a user-friendly message: *"Previous DigiDollar transfer has not confirmed yet. Please wait ~15 seconds and try again."*

4. **estimatecollateral accepts invalid mint amounts (Bug #11)** — `estimatecollateral` happily returned estimates for amounts outside the consensus limits ($100 min / $100K max), misleading users into thinking a mint would succeed. Now validates against `IsValidMintAmount()` with clear error messages. Help text updated in both `mintdigidollar` and `estimatecollateral` to document limits.

5. **Mint UTXO auto-consolidation fails when margin exceeds balance (PR #393 — JohnnyLawDGB)** — The 10% margin calculation in auto-consolidation could push the target above the wallet balance, rejecting mints even with sufficient DGB. Replaced with a sweep-to-self approach using `subtractfeefromamount`. Thanks to @JohnnyLawDGB for the fix and @AussieEpic for reporting.

6. **validateddaddress doesn't report ismine for wallet addresses (Bug #17)** — `validateddaddress` always returned `ismine: false` for addresses owned by the wallet. Now correctly queries wallet ownership.

7. **listdigidollartxs reports zero fees for send transactions (Bug #13)** — Transaction fees for DD sends were always shown as 0. Now computes actual DGB fees from wallet debit/credit data.

8. **senddigidollar response amounts and sub-dollar input handling (Bugs #11/25, #18)** — Fixed amount formatting in `senddigidollar` response and handling of sub-dollar DD amounts.

9. **getprotectionstatus returns hardcoded mock data (Bugs #7, #9)** — `getprotectionstatus` returned static mock values instead of real system health metrics. Now queries actual system state.

10. **getoracleprice sub-cent price and hardcoded volatility (Bugs #2, #8)** — Oracle price reporting had sub-cent rounding issues and hardcoded 24h/volatility values. Fixed to use real data.

11. **Mint UTXO consolidation sweep (pre-PR #393)** — Initial consolidation improvement using full-balance sweep with `subtractfee`, later refined by PR #393.

### Test Suite

- **2,014 C++ unit tests** — all passing (3 new tests added in RC26)
- **23 DigiDollar functional tests** — all passing
- **Full test suite verified** on combined RC26 branch

---

## Commits Since RC25

```
9e20435575 fix: mint UTXO consolidation sweep entire balance with subtractfee
b60b7d3eb1 fix: getoracleprice sub-cent price_cents and hardcoded 24h/volatility (Bugs #2, #8)
89e1b64a73 fix: getprotectionstatus returns real system health instead of hardcoded mocks (Bugs #7, #9)
ce9c9551ea fix: senddigidollar response amounts and sub-dollar input handling (Bugs #11/25, #18)
431dc89fa5 fix: listdigidollartxs now reports actual DGB fees for send transactions (Bug #13)
b590c49975 fix: validateddaddress now correctly reports ismine for wallet DD addresses (Bug #17)
a39f22a760 version: bump to v9.26.0-rc26, update wallet image and release notes
ae8b5b0e92 Merge pull request #393 from JohnnyLawDGB/fix/mint-utxo-consolidation
d99ce74291 fix: improve senddigidollar error message for unconfirmed DD inputs (Bug #10)
b600cd43ac fix: add mint limit validation to estimatecollateral and document limits in help text (Bug #11)
b573129311 fix: correct fee calculation for DD redemption transactions in gettransaction (Bug #17)
40460ab6da fix: stop DD positions refresh timer during shutdown to prevent hang (Bug #23)
```

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up custody of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Oracle Operator Setup

### Upgrading from RC25

```bash
digibyte-cli -testnet stop
# Replace binary
digibyted -testnet -daemon
# Oracle auto-starts from wallet — no manual steps needed.
```

### Upgrading from RC24 or Earlier

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

> **Note:** `txindex=1` is enforced at startup for DD-enabled nodes (since RC25). Make sure it's in the correct section (`[test]` for testnet, `[main]` for mainnet). Global placement (above all sections) also works.

---

## Known Issues

- DigiDollar features disabled until BIP9 activation (~block 600 with continuous mining)
- Oracle prices show as 0 until sufficient operators restart after upgrading

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
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc26-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc26-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc26-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc26-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc26-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc26-aarch64-linux-gnu.tar.gz` |

---

## Troubleshooting

### "Qt wallet stuck on shutting down" (FIXED in RC26)
The DD positions refresh timer deadlocked with shutdown. RC26 stops the timer in three places to prevent this.

### "gettransaction shows 240K DGB fee after redemption" (FIXED in RC26)
Standard fee calc misinterpreted collateral unlock. RC26 detects REDEEM transactions and computes the real miner fee.

### "dd-input-amounts-unknown on rapid sends" (FIXED in RC26)
Sending DD before previous transfer confirms now gives a clear "wait ~15 seconds" message.

### "estimatecollateral accepts out-of-range amounts" (FIXED in RC26)
Now validates $100 min / $100K max limits with clear error messages.

### "Block production halted with insufficient-collateral" (FIXED in RC25)
### "bad-mint-lock-height-mismatch during rescan" (FIXED in RC25)
### "Node fails silently without txindex" (FIXED in RC25)
### "startoracle fails after restart" (FIXED in RC25)
### "Zero-price blocks during rapid block production" (FIXED in RC25)
### "Insufficient fee inputs for calculated fee" on redemption (FIXED in RC24)
### "listdigidollaraddresses returns mock data" (FIXED in RC24)

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues

When reporting bugs include: what happened, steps to reproduce, platform, and error messages from debug.log.
