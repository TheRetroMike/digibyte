# DigiByte v9.26.0-rc18 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ TESTNET RESET — READ THIS FIRST

**RC18 resets the testnet to a new chain: testnet18.** This means:

- **Your old testnet blockchain data must be deleted** (see Upgrade Notes below)
- **The chain starts from block 0** — everyone syncs fresh
- **New ports:** P2P port is now **12032**, RPC port is **14025**
- **BIP9 activation is LIVE** — DigiDollar activates via real miner signaling, not auto-activation

### 🔑 Oracle Operators — IMPORTANT

**Your oracle wallet and keys are NOT lost.** They are stored in your old testnet data directory. To migrate:

1. **Before deleting old data**, copy your oracle wallet:
   - **Linux:** Copy `~/.digibyte/testnet17/wallets/oracle/` to a safe location
   - **Windows:** Copy `%APPDATA%\DigiByte\testnet17\wallets\oracle\` to a safe location
   - **macOS:** Copy `~/Library/Application Support/DigiByte/testnet17/wallets/oracle/` to a safe location
2. **Install RC18** and start the node (it creates the new `testnet18` directory)
3. **Copy your saved oracle wallet** into the new testnet18 wallets directory:
   - **Linux:** `~/.digibyte/testnet18/wallets/oracle/`
   - **Windows:** `%APPDATA%\DigiByte\testnet18\wallets\oracle\`
   - **macOS:** `~/Library/Application Support/DigiByte/testnet18/wallets/oracle/`
4. **Load and start your oracle:**
   ```bash
   digibyte-cli -testnet loadwallet "oracle"
   digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
   ```

Your existing Schnorr keypair carries over — you do **NOT** need to run `createoraclekey` again. The private key lives in your wallet file.

### 📡 BIP9 Activation Process

RC18 uses **real BIP9 miner signaling** for DigiDollar activation. This means:

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

## What's New in RC18

RC18 is a **testnet reset release** with a **critical multi-algo difficulty fix**, Mask Values privacy support, consecutive send improvements, and UI polish.

### 🔴 CRITICAL: Multi-Algo Difficulty Fix (GetAlgo)

A hardcoded mainnet height check (`145,000`) in `CBlockIndex::GetAlgo()` broke difficulty adjustment for **all non-Scrypt algorithms on testnet**. Because testnet never reaches height 145,000 during testing, all 5 algos were stuck at minimum difficulty, producing instant blocks. The fix removes the height check entirely and uses version bit parsing, which works correctly on all networks.

---

### All Fixes: RC15 → RC18 (Compact Summary)

#### RC18 Fixes
- 🔴 **GetAlgo mainnet height removal** — removed hardcoded height 145,000 from `CBlockIndex::GetAlgo()` that broke multi-algo difficulty on testnet; now uses version bit parsing on all networks
- 🔒 **Mask Values privacy** — DigiDollar tabs (DD Balance, DGB Locked, Active Positions, DD Pending, Send Amount, Available Balance, Position List) now properly masked when "Mask Values" is enabled, with full signal chain support
- 💸 **Consecutive DD sends** — allow spending trusted unconfirmed DigiDollar UTXOs so users can send multiple DD transactions without waiting for confirmations
- 🖥️ **Redeem display improvement** — REDEEM transactions now show collateral return and fee change as separate line items instead of a single combined amount
- 🧪 **Mask Values privacy tests** — TDD test suite for all 7 DigiDollar widget mask behaviors

#### RC17 Fixes
- 🔴 **__int128 → uint64 overflow** — fixed silent overflow in collateral calculations at extreme values (4 locations: consensus, TxBuilder, 2 RPCs); now rejects amounts exceeding `MAX_MONEY`
- 🔴 **Floating-point in consensus** — replaced `double` division with `__int128` integer math in `ValidateCollateralReleaseAmount` to prevent cross-platform chain splits
- 🔴 **Transfer conservation bypass** — failed input lookups no longer silently pass; returns hard error `dd-input-amounts-unknown`
- 🔧 **DD Send yellow border stuck** — `updateBalance()` now re-validates amount field so warning border clears after pending DD confirms
- ⛏️ **SHA256D initial difficulty** — set to `>> 31` on testnet for reasonable CPU mining block times (~15s target)
- 🧪 **RPC gating test** — comprehensive test covering all 27 DD/Oracle RPCs gated behind BIP9 activation
- 🧪 **Encrypted wallet test fix** — eliminated timing-dependent race condition
- 📡 **`getdigidollardeploymentinfo` ungated** — informational RPC works before activation for monitoring BIP9 progress

#### RC16 Fixes
- 🔧 **BIP9 activation framework** — real miner signaling (bit 23, 70% threshold, 200-block window) replacing auto-activation
- 🔧 **DD tab always visible** — shows activation status overlay instead of hiding tab pre-activation
- 🔧 **DDSendFee transaction type** — separate fee display for DigiDollar transfers in Qt
- 🔧 **Pending DD balance in send widget** — includes trusted pending balance as spendable
- 🔧 **`getdigidollarbalance` pending support** — wires up `GetPendingDDBalance()` in RPC
- 🔧 **Dandelion++ relay loop fix** — duplicate detection prevents infinite relay and log spam
- 🔧 **Dandelion mempool seeding gate** — new-peer mempool seeding gated on Dandelion enabled

#### RC15 Fixes
- 🔧 **DigiDollar MINT/SEND/REDEEM** — core transaction types with full consensus validation
- 🔧 **Oracle price consensus** — 5-of-8 Schnorr threshold with 6 exchange sources
- 🔧 **Collateral management** — position tracking, liquidation protection, DCA multiplier
- 🔧 **Qt wallet integration** — full DigiDollar tab with send, receive, mint, redeem, history, coin control
- 🔧 **Multi-algo testnet** — 5 algorithms (SHA256D, Scrypt, Skein, Qubit, Odocrypt) from block 100

---

## Technical Changes

| File | Change |
|------|--------|
| `configure.ac` | Version bump RC17 → RC18 |
| `src/kernel/chainparams.cpp` | Testnet18 chain params, P2P port 12032 |
| `src/chainparamsbase.cpp` | Testnet18 data dir and ports (P2P 12032, RPC 14025) |
| `src/chain.cpp` | Remove hardcoded mainnet height 145,000 from `GetAlgo()`; use version bit parsing |
| `src/qt/digidollarsendwidget.cpp` | Mask Values privacy support for send amount / available balance widgets |
| `src/qt/digidollarwidget.cpp` | Mask Values privacy for DD Balance, DGB Locked, Active Positions, DD Pending widgets |
| `src/qt/digidollarpositionmodel.cpp` | Mask Values privacy for position list |
| `src/qt/transactionrecord.cpp` | Show collateral return and fee change separately in REDEEM display |
| `src/wallet/spend.cpp` | Allow spending trusted unconfirmed DD UTXOs for consecutive sends |
| `src/test/digidollar_mask_values_tests.cpp` | TDD tests for Mask Values across 7 DD widgets |

---

## Commits Since RC17

### Critical Fixes
```
12123c9edb fix: Remove hardcoded mainnet height from CBlockIndex::GetAlgo()
```

### Bug Fixes
```
84e7222fe7 fix: allow spending trusted unconfirmed DD UTXOs (consecutive sends)
7008db3d75 fix: Add Mask Values privacy support to DigiDollar widgets
134c0556ac qt: Show collateral return and fee change separately in REDEEM display
```

### Tests
```
26aa3b561a test: Add Mask Values privacy tests for DigiDollar widgets (TDD)
```

### Release
```
178a445298 bump: v9.26.0-rc18 — testnet18, port 12032, mask values privacy
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
- DigiByte Core RC18 built from source with curl support (or download the binary)
- An assigned oracle ID (0–7 for testnet, contact the maintainer)

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

### Existing Oracle — Upgrading from RC17 or Earlier

**Your oracle keys are safe.** See the **Testnet Reset** section at the top for migration steps. The key point: copy your oracle wallet from the old `testnet17` directory (or `testnet13` if upgrading from older RCs) to the new `testnet18` directory.

After migration:
```bash
# Step 1: Start your node with RC18
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

**RC18 uses a new chain: testnet18 (port 12032).** Your old testnet data is incompatible.

**Steps to upgrade:**

1. **Save your oracle wallet** (if you run an oracle — see Oracle migration section above)
2. **Close your old wallet**
3. **Delete old testnet data:**
   - **Linux:** Delete `~/.digibyte/testnet17/` (and any older testnet directories)
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet17\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet17/`
4. **Download and install RC18**
5. **Update your `digibyte.conf`** — the `addnode` addresses remain the same:
   ```ini
   testnet=1
   
   [test]
   digidollar=1
   addnode=oracle1.digibyte.io
   ```
6. **Launch with `-testnet`** — a new `testnet18` directory will be created automatically
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
| Network | Testnet (testnet18) |
| Default P2P Port | **12032** |
| Default RPC Port | **14025** |
| Oracle Node | oracle1.digibyte.io |
| Address Prefix | dgbt1... (bech32) |
| Multi-Algo Activation | Block 100 |
| BIP9 Signaling Start | Block 200 |
| BIP9 Threshold | 70% (140 of 200 blocks) |
| DD Activation (earliest) | Block 600 |
| Oracle Consensus | 5-of-8 Schnorr threshold |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc18-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc18-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc18-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc18-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc18-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc18-aarch64-linux-gnu.tar.gz` |

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
- This is expected! RC18 uses real BIP9 activation. DigiDollar features unlock after miners signal support and the activation threshold is reached.
- Monitor progress: `digibyte-cli -testnet getdigidollardeploymentinfo`

### "Can't connect to network"
- RC18 uses **port 12032** (not 12031 or 12030). Check your firewall.
- Make sure `addnode=oracle1.digibyte.io` is under `[test]` in your config

### "Old testnet data"
- Delete ALL old testnet directories (testnet10, testnet11, testnet12, testnet13, testnet17) — RC18 uses testnet18

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
