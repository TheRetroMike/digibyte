# DigiByte v9.26.0-rc22 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ SAME TESTNET — NO RESET

**RC22 uses the same testnet19 chain as RC19/RC20/RC21.** No data migration needed.

- **No chain reset** — your blockchain data, wallets, and oracle keys all carry over
- **Same ports:** P2P **12033**, RPC **14025**
- **Same oracle consensus:** **5-of-9** Schnorr threshold
- **Just update the binary** and restart your node

### 🔑 Oracle Operators — IMPORTANT

After upgrading to RC22, **you must reload your oracle wallet and restart your oracle:**

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

Qt users: **File → Open Wallet → oracle**, then **Help → Debug Window → Console** → `startoracle <your_oracle_id>`

> **⚠️ After every node restart, you must re-load your wallet and re-start the oracle.** Your key persists in the wallet — you never need to run `createoraclekey` again.

---

## What's New in RC22

RC22 is a **critical release** that fixes two major bugs reported by community testers:

1. **Oracle Schnorr signature bundles were not being embedded in mined blocks** — the entire oracle-to-block pipeline has been fixed and verified end-to-end on testnet
2. **Unconfirmed DigiDollar mints showed as spendable balance** — freshly minted DD could appear available before confirmation

**If your oracle prices show stale data or your mined blocks don't contain oracle bundles, upgrade to RC22.**

---

## 🐛 Bug Fix #1: Oracle Bundles Not Reaching Mined Blocks

**Reported by:** JohnnyLawDGB on Gitter (Feb 15, 2026)

**The problem:** Despite oracles broadcasting signed price attestations via P2P, the Phase 2 multi-oracle Schnorr signature bundles were never actually making it into mined blocks. The consensus price was stale or missing from the chain.

**Root causes (4 issues):**

### 1. Oracle message drain on every block template

`AddOracleBundleToBlock()` called `pending_messages.clear()` after adding the oracle output. Since `getblocktemplate` creates a new template on every miner poll (~10 seconds), the pending messages were drained immediately — only the very first template after an oracle broadcast contained data.

**Fix:** Moved `pending_messages.clear()` to `ConnectBlock()`, which only runs when a block with valid oracle data is actually connected to the chain.

### 2. Hardcoded oracle output position

`ValidateBlockOracleData()` assumed the oracle OP_RETURN was always at `vout[1]`. But with SegWit active, the witness commitment occupies `vout[1]` and the oracle output moves to `vout[2]`. Blocks with oracle data were failing validation.

**Fix:** Now scans ALL coinbase outputs for the `OP_RETURN OP_ORACLE` marker instead of hardcoding an index. Security comes from Schnorr signature verification, not output position.

### 3. RegenerateCommitments stripping oracle data

`RegenerateCommitments()` in the mining code stripped all OP_RETURN outputs from the coinbase before adding the witness commitment, destroying the oracle output that was already added.

**Fix:** After regenerating the witness commitment, the oracle bundle is re-added to the coinbase.

### 4. Miners never received oracle data (THE BIG ONE)

`getblocktemplate` returned `coinbasevalue` (the reward amount) and `default_witness_commitment`, but miners like cpuminer build their own coinbase transaction from these fields — only including the reward output and witness commitment. The oracle output was invisible to external miners.

**Fix:** When oracle data is present, `getblocktemplate` now serves the full pre-built coinbase via `coinbasetxn` (BIP 22), serialized without witness data so miners compute the correct txid for the merkle root. The witness nonce is re-added automatically by `UpdateUncommittedBlockStructures` when the block is submitted. **This requires ZERO changes to any mining software** — any GBT-compliant miner that supports `coinbasetxn` (including cpuminer, bfgminer, cgminer) will automatically include oracle data.

### 🔒 Security hardening

Added a consensus rule rejecting blocks with multiple `OP_ORACLE` outputs in the coinbase (`bad-oracle-multiple-outputs`), preventing potential coinbase manipulation attacks.

**Testnet verification:** Block 15128+ confirmed with 3 coinbase outputs (reward + witness commitment + oracle bundle), 5 valid Schnorr signatures, consensus price $0.004069 cached on-chain. cpuminer: 100% acceptance rate with unmodified binary.

---

## 🐛 Bug Fix #2: Unconfirmed DD Mints Shown as Spendable

**Reported by:** Shenger on Gitter (Feb 16, 2026)

**The problem:** Freshly minted DigiDollars appeared in the spendable balance immediately, before the minting transaction was confirmed in a block. This could mislead users into thinking they had confirmed funds.

**Root cause:** An earlier fix (RC16, commit `84e7222fe7`) re-included trusted unconfirmed DD outputs in the balance to allow consecutive sends. But it didn't distinguish between mint transactions and transfer transactions — newly minted DD shouldn't be spendable until confirmed.

**Fix:** `DD_TX_MINT` outputs are now excluded from the spendable balance until confirmed. `DD_TX_TRANSFER` change outputs remain spendable immediately, preserving the consecutive-sends fix.

---

## 🧪 Testing

### New Tests
8 new or updated test cases covering the oracle pipeline:

| Test | What It Verifies |
|------|-----------------|
| `addoraclebundle_basic` | Oracle bundle added to coinbase correctly |
| `addoraclebundle_clears_on_connect` | Pending messages cleared only on block connection, not template creation |
| `validate_scans_all_outputs` | Validation finds oracle data regardless of output position |
| `validate_rejects_multiple_oracle` | Blocks with multiple OP_ORACLE outputs rejected |
| `regenerate_preserves_oracle` | RegenerateCommitments doesn't strip oracle data |
| `coinbasetxn_no_witness` | GBT serves coinbasetxn without witness serialization |
| `unconfirmed_mint_not_spendable` | DD_TX_MINT excluded from spendable until confirmed |
| `transfer_change_stays_spendable` | DD_TX_TRANSFER change remains immediately spendable |

### Test Results
- **1,969 C++ unit tests** — all passing
- **DigiDollar functional tests** — all passing
- **Testnet live verification** — oracle bundles confirmed in mined blocks

---

## 📊 All Changes: RC21 → RC22

| Category | Count | Summary |
|----------|-------|---------|
| 🐛 Bug fixes | 2 | Oracle bundle pipeline (4 sub-issues), unconfirmed mint balance |
| 🔒 Security | 1 | Reject multiple OP_ORACLE outputs in coinbase |
| 🧪 Tests | 8 | Oracle pipeline + wallet balance tests |
| 📦 Version | 1 | Bump to v9.26.0-rc22, update wallet image |

**Total: 3 commits** (2 bug fixes + 1 version bump)

---

## Technical Changes

| File | Change |
|------|--------|
| `src/oracle/bundle_manager.cpp` | Move `pending_messages.clear()` to ConnectBlock; scan all outputs for OP_ORACLE; reject multiple OP_ORACLE outputs |
| `src/node/miner.cpp` | Re-add oracle bundle after RegenerateCommitments |
| `src/rpc/mining.cpp` | Serve `coinbasetxn` with non-witness serialization when oracle data present |
| `src/validation.cpp` | Extract oracle data and clear pending on ConnectBlock |
| `src/wallet/spend.cpp` | Exclude unconfirmed DD_TX_MINT from spendable balance |
| `src/test/digidollar_oracle_tests.cpp` | 5 new oracle pipeline tests |
| `src/test/digidollar_wallet_tests.cpp` | 3 new/updated wallet balance tests |
| `configure.ac` | Version bump RC21 → RC22 |
| `src/qt/res/icons/digibyte_wallet.png` | Updated wallet splash image |

---

## Commits Since RC21

### Bug Fixes
```
f4b11b1fea wallet: exclude unconfirmed DD mint outputs from spendable balance
f7a4b1d2b0 oracle: fix end-to-end bundle pipeline — bundles now reach mined blocks
```

### Version
```
<pending>  Bump version to v9.26.0-rc22, update wallet image
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
- DigiByte Core RC22 built from source with curl support (or download the binary)
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

### Existing Oracle — Upgrading from RC21

**No chain reset.** Just update the binary and restart:

```bash
# Step 1: Stop your node
digibyte-cli -testnet stop

# Step 2: Replace the binary with RC22

# Step 3: Start your node
digibyted -testnet -daemon

# Step 4: Load your wallet and start oracle
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### Existing Oracle — Upgrading from RC18 or Earlier

**Your oracle keys are safe.** Copy your oracle wallet from the old testnet directory to `testnet19`:

1. **Before deleting old data**, copy your oracle wallet:
   - **Linux:** Copy `~/.digibyte/testnet18/wallets/oracle/` to a safe location
   - **Windows:** Copy `%APPDATA%\DigiByte\testnet18\wallets\oracle\` to a safe location
   - **macOS:** Copy `~/Library/Application Support/DigiByte/testnet18/wallets/oracle/` to a safe location
2. **Install RC22** and start the node (it uses the `testnet19` directory)
3. **Copy your saved oracle wallet** into testnet19:
   - **Linux:** `~/.digibyte/testnet19/wallets/oracle/`
   - **Windows:** `%APPDATA%\DigiByte\testnet19\wallets\oracle\`
   - **macOS:** `~/Library/Application Support/DigiByte/testnet19/wallets/oracle/`
4. **Load and start your oracle:**
   ```bash
   digibyte-cli -testnet loadwallet "oracle"
   digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
   ```

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
| 8 | JohnnyLawDGB | ✅ Active |

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

### Upgrading from RC21

**No chain reset.** Simply replace the binary and restart:

1. **Stop your node:** `digibyte-cli -testnet stop`
2. **Replace the binary** with the RC22 download
3. **Start your node:** `digibyted -testnet -daemon` (or launch Qt)
4. **Reload your oracle** (if applicable — see Oracle section above)

Your blockchain data, wallets, oracle keys, and all configuration carry over unchanged.

### Upgrading from RC19/RC20

Same as above — RC22 is fully backward-compatible with the testnet19 chain.

### Upgrading from RC18 or Earlier

**RC19 reset the testnet chain.** If you're coming from RC18 or earlier, follow the RC19 migration steps:

1. **Save your oracle wallet** (if you run an oracle — see Oracle migration section above)
2. **Delete old testnet data:**
   - **Linux:** Delete `~/.digibyte/testnet18/` (and any older testnet directories)
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet18\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet18/`
3. **Download and install RC22**
4. **Update your `digibyte.conf`** — the `addnode` addresses remain the same:
   ```ini
   testnet=1
   
   [test]
   digidollar=1
   addnode=oracle1.digibyte.io
   ```
5. **Launch with `-testnet`** — a new `testnet19` directory will be created automatically
6. **Restore your oracle wallet** if applicable (see migration steps above)

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

- `startoracle` must be re-run after every node restart
- DigiDollar features are disabled until BIP9 activation completes (~block 600 with continuous mining)
- Oracle prices show as 0 or "Not Reporting" until enough oracle operators restart their oracles
- Restoring wallets via `listdescriptors`/`importdescriptors` may show stale redeem buttons for already-redeemed DigiDollars (cosmetic — attempting to redeem shows proper error)

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
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc22-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc22-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc22-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc22-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc22-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc22-aarch64-linux-gnu.tar.gz` |

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
- This is expected if BIP9 hasn't activated yet. DigiDollar features unlock after miners signal support and the activation threshold is reached.
- Monitor progress: `digibyte-cli -testnet getdigidollardeploymentinfo`

### "Oracle prices stale or missing from blocks" (FIXED in RC22)
- This was caused by oracle bundles not being included in mined blocks. Upgrade to RC22 — the oracle pipeline is now fully functional.

### "Unconfirmed mint balance shown as spendable" (FIXED in RC22)
- Freshly minted DD now correctly requires confirmation before appearing as spendable.

### "Sync stuck at block 7586" (FIXED in RC21)
- This was caused by the ERR minting check failing during IBD. Upgrade to RC21+ and resync.

### "Can't connect to network"
- RC19+ uses **port 12033** (not 12032 or earlier). Check your firewall.
- Make sure `addnode=oracle1.digibyte.io` is under `[test]` in your config

### "Old testnet data"
- If coming from RC18 or earlier, delete ALL old testnet directories (testnet10 through testnet18) — RC19+ uses testnet19

### "No wallet is loaded" when running oracle commands
- Load your wallet first: `digibyte-cli -testnet loadwallet "oracle"`
- For Qt: **File → Open Wallet → oracle**

### "Mining not working"
- Blocks 0-99 are scrypt-only. After block 100, multi-algo activates.
- Set `algo=sha256d` in config for fastest CPU mining after block 100

### "Oracle price shows 0 or N/A"
- Oracle prices require BIP9 activation first
- Wait for activation and sufficient oracle operators to come online after upgrading

### "Redeem button shows for already-redeemed DigiDollars"
- Known cosmetic issue when restoring wallets via descriptor import. The button shows but attempting to redeem displays the correct error message.

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

When reporting bugs, start your message with **BUG:** and include: what happened, steps to reproduce, platform (Windows/Linux/Mac), and any error messages from your debug.log file.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
