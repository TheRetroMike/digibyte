# DigiByte v9.26.0-rc24 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ SAME TESTNET — NO RESET

**RC24 uses the same testnet19 chain as RC19–RC23.** No data migration needed.

- **No chain reset** — your blockchain data, wallets, and oracle keys all carry over
- **Same ports:** P2P **12033**, RPC **14025**
- **Same oracle consensus:** **5-of-9** Schnorr threshold
- **Just update the binary** and restart your node

### 🔑 Oracle Operators — IMPORTANT

After upgrading to RC24, **you must reload your oracle wallet and restart your oracle:**

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

Qt users: **File → Open Wallet → oracle**, then **Help → Debug Window → Console** → `startoracle <your_oracle_id>`

> **⚠️ After every node restart, you must re-load your wallet and re-start the oracle.** Your key persists in the wallet — you never need to run `createoraclekey` again.

---

## What's New in RC24

RC24 is a **wallet RPC correctness release** focused on replacing mock/placeholder data with real wallet queries and fixing the critical redemption fee estimation bug. Thanks to everyone who reported bugs and helped test — your reports directly drove these fixes.

### Bug Fixes

1. **Redemption fails after first redeem (Bug #9 — Critical)** — Fee estimation was hardcoded at 0.1 DGB, but actual fees need ~0.21 DGB. Now dynamically calculated from transaction size and fee rate.
2. **Mint amount validation missing (Bug #11)** — `mintdigidollar` accepted amounts outside the $100–$10,000 range with no error. Now returns clear error messages.
3. **Transaction history missing block heights (Bug #12)** — `listdigidollartxs` showed blockheight=-1 for confirmed transactions. Now shows real block height and hash.
4. **Address list returned fake data (Bug #13)** — `listdigidollaraddresses` returned hardcoded mock addresses. Now queries real wallet UTXOs with correct network prefixes (DD/TD).
5. **Position info had broken fields (Bug #14)** — `listdigidollarpositions` had a meaningless health ratio and hardcoded "N/A" dates. Health ratio now uses oracle price, dates computed from block heights.
6. **Oracle RPCs showed conflicting data (Bug #15)** — `getoracles` and `getalloracleprices` used separate scanning code and could show different prices for the same oracle. Now share a single data source.
7. **Redemption fee displayed incorrectly (Bug #17)** — Fee field in redemption transactions was never set. Now records the actual fee paid.
8. **Redemption info returned mock data (Bug #18)** — `getredemptioninfo` returned the same hardcoded values for every position, even nonexistent ones. Now looks up real position data from the wallet.
9. **Wallet restore missed DD redemption state (PR #388)** — Restoring from backup didn't pick up DigiDollar redemption state. Fixed wallet rescan to find all DD transaction history.
10. **Fee UTXO amounts not passed to TxBuilder (PR #389 credit)** — `RedeemDigiDollar` wallet path passed nullptr for fee amounts, preventing proper transaction construction. Credit: JohnnyLawDGB.

### Other Changes

- **ZMQ IPC socket validation fix** (PR #387 — JohnnyLawDGB)
- **Dandelion++ test teardown fix** — macOS CI reliability improvement
- **DigiDollar documentation corrections** — validated against source code
- **Comprehensive regression tests** — 20+ new tests covering all bug fixes

---

## 🧪 Testing

### Test Results
- **1,966 C++ unit tests** — all passing
- **DigiDollar functional tests** — all passing
- **Live testnet verification** — all 30 DigiDollar/Oracle RPCs verified on testnet19

---

## Commits Since RC23

```
50a62bc33e fix(wallet): populate feeAmounts in RedeemDigiDollar wallet path
3f2add6387 fix(rpc): replace mock data in getredemptioninfo with real wallet position lookup
d2e35f3851 fix(rpc): move listdigidollaraddresses to wallet RPC table for proper wallet context
7d5a7b01d8 test: update existing tests for Bug #9 fee changes and Bug #12 wallet requirement
1bbcf21230 test: add regression tests for Bug #11 and Bug #13 fixes
9cde569c98 fix(wallet): proper fee estimation for DD redemption (Bug #9, Bug #17)
cc87d51d39 fix(rpc): reconcile getoracles and getalloracleprices data (Bug #15)
42a99d9ba5 fix(rpc): add computed fields to listdigidollarpositions (Bug #14)
b98c7c852a fix(rpc): replace mock data in listdigidollaraddresses with wallet queries (Bug #12)
d5b0427115 fix(rpc): validate mint amount against consensus limits in mintdigidollar (Bug #11)
6faee8374c fix(wallet): populate blockheight in DDTransaction from confirmed block state (Bug #13)
b49b5111a4 Merge pull request #388 — wallet restore redemption state
42cff0c711 Merge pull request #387 — ZMQ IPC socket validation
425e91cac6 docs(digidollar): validate and correct DigiDollar docs against source code
0116809d64 version: bump to v9.26.0-rc24, update wallet image
```

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up custody of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Oracle Operator Setup

### Upgrading from RC23

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
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc24-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc24-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc24-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc24-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc24-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc24-aarch64-linux-gnu.tar.gz` |

---

## Troubleshooting

### "Insufficient fee inputs for calculated fee" on redemption (FIXED in RC24)
Fee estimation was hardcoded too low. RC24 calculates fees dynamically based on transaction size.

### "listdigidollaraddresses returns mock data" (FIXED in RC24)
Now queries real wallet UTXOs with correct network-aware address prefixes.

### "getredemptioninfo shows same data for every position" (FIXED in RC24)
Now looks up real position data from the wallet. Returns error for nonexistent positions.

### "startoracle fails with 'Oracle not configured' after restart" (FIXED in RC23)
### "Oracle consensus stalled and never recovered" (FIXED in RC23)
### "Oracle prices stale or missing from blocks" (FIXED in RC22)
### "Sync stuck at block 7586" (FIXED in RC21)

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues

When reporting bugs include: what happened, steps to reproduce, platform, and error messages from debug.log.
