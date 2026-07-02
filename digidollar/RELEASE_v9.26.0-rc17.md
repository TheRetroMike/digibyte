# DigiByte v9.26.0-rc17 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ TESTNET RESET — READ THIS FIRST

**RC17 resets the testnet to a new chain: testnet17.** This means:

- **Your old testnet13 blockchain data must be deleted** (see Upgrade Notes below)
- **The chain starts from block 0** — everyone syncs fresh
- **New ports:** P2P port is now **12031**, RPC port is **14025**
- **BIP9 activation is LIVE** — DigiDollar activates via real miner signaling, not auto-activation

### 🔑 Oracle Operators — IMPORTANT

**Your oracle wallet and keys are NOT lost.** They are stored in your old testnet data directory. To migrate:

1. **Before deleting old data**, copy your oracle wallet:
   - **Linux:** Copy `~/.digibyte/testnet13/wallets/oracle/` to a safe location
   - **Windows:** Copy `%APPDATA%\DigiByte\testnet13\wallets\oracle\` to a safe location
   - **macOS:** Copy `~/Library/Application Support/DigiByte/testnet13/wallets/oracle/` to a safe location
2. **Install RC17** and start the node (it creates the new `testnet17` directory)
3. **Copy your saved oracle wallet** into the new testnet17 wallets directory:
   - **Linux:** `~/.digibyte/testnet17/wallets/oracle/`
   - **Windows:** `%APPDATA%\DigiByte\testnet17\wallets\oracle\`
   - **macOS:** `~/Library/Application Support/DigiByte/testnet17/wallets/oracle/`
4. **Load and start your oracle:**
   ```bash
   digibyte-cli -testnet loadwallet "oracle"
   digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
   ```

Your existing Schnorr keypair carries over — you do **NOT** need to run `createoraclekey` again. The private key lives in your wallet file.

### 📡 BIP9 Activation Process

RC17 uses **real BIP9 miner signaling** for DigiDollar activation. This means:

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

## What's New in RC17

RC17 is a **testnet reset release** with **3 security fixes, 1 Qt bug fix, SHA256D difficulty tuning, real BIP9 activation, and comprehensive test coverage improvements** since RC16.

---

### 🔴 Security: __int128 → uint64 Overflow in Collateral Calculations

At extreme values (maximum DigiDollar amount + minimum DGB price + maximum collateral ratio), the 128-bit intermediate collateral calculation could silently overflow when cast to `uint64_t`, wrapping the required collateral to near-zero. An attacker could mint DigiDollars with almost no collateral locked.

**Four locations fixed:**
- Consensus validation (`ValidateCollateralAmount`)
- Transaction builder (`TxBuilder::CalculateCollateral`)
- `calculatecollateralrequirement` RPC
- `estimatecollateral` RPC

All four now compare the `__int128` result against `MAX_MONEY` before casting to `uint64_t`, rejecting any overflow.

---

### 🔴 Security: Floating-Point in Consensus Code

`ValidateCollateralReleaseAmount` used `double` division for partial redemption calculations. IEEE 754 floating-point imprecision means different CPU architectures could compute slightly different results — a potential chain split where one node accepts a block and another rejects it.

**Fix:** Replaced all floating-point math with `__int128` integer arithmetic in consensus code paths. Rule: **never use float/double in consensus.**

---

### 🔴 Security: Transfer Conservation Bypass

When a DigiDollar transfer transaction's input amount lookup failed, the old code silently set `inputDD = outputDD`, making the conservation check pass by default. This could allow a malformed transaction to pass validation without actually verifying that inputs equal outputs.

**Fix:** Now returns a hard error (`dd-input-amounts-unknown`) instead of silently passing.

---

### 🔧 Qt: DD Send Widget Yellow Border Stuck After Sending

After sending DigiDollars, the yellow warning border on the amount field persisted even after the pending transaction confirmed. The `updateBalance()` function wasn't re-validating the amount field when the balance changed.

**Fix:** Added `updateAmountValidation()` call to `updateBalance()` so the border clears when the pending DD confirms.

---

### ⛏️ SHA256D Initial Difficulty Tuning

SHA256D is dramatically faster than other algorithms on CPUs (~180 Mhash/s on 12 cores vs ~300 Khash/s for scrypt). Without an explicit initial difficulty target, SHA256D defaulted to `powLimit` which produced instant blocks.

Tested multiple difficulty levels empirically:
- `>> 25`: Too hard, 0% acceptance rate
- `>> 28`: Too easy, instant blocks
- `>> 30`: Still too fast, ~4-5 second block times
- `>> 32`: Too slow, ~22 second block times
- **`>> 31`: Sweet spot** — reasonable block times with DigiShield adjusting from there

Multi-algo activates at block 100 on testnet. Blocks 0-99 are scrypt-only.

---

### 🧪 Test Coverage Improvements

- **Comprehensive RPC gating test** — verifies all 27 DD/Oracle RPCs are properly gated behind BIP9 activation
- **Encrypted wallet test race condition fix** — eliminated timing-dependent test failure
- **`getdigidollardeploymentinfo` ungated** — this informational RPC now works before activation so users can monitor BIP9 signaling progress

---

## Technical Changes

| File | Change |
|------|--------|
| `configure.ac` | Version bump RC16 → RC17 |
| `src/kernel/chainparams.cpp` | Testnet17 chain params, SHA256D `>> 31` initial difficulty, BIP9 activation (bit 23, 70% threshold, 200-block window) |
| `src/chainparamsbase.cpp` | Testnet17 data dir and ports (P2P 12031, RPC 14025) |
| `src/validation.cpp` | Fix __int128→uint64 overflow in consensus collateral validation; replace float with integer math |
| `src/wallet/txbuilder.cpp` | Fix __int128→uint64 overflow in TxBuilder collateral calc |
| `src/rpc/digidollar.cpp` | Fix __int128→uint64 overflow in RPC collateral calculations; ungate `getdigidollardeploymentinfo` |
| `src/qt/digidollarsendwidget.cpp` | Re-validate amount border when DD balance updates |
| `src/qt/res/icons/digibyte_wallet.png` | Update wallet splash to RC17 |
| `src/test/digidollar_rpc_gating_tests.cpp` | Comprehensive RPC gating test for 27 DD/Oracle RPCs |
| `src/test/wallet_tests.cpp` | Fix encrypted wallet test race condition |

---

## Commits Since RC16

### Security Fixes
```
370d8cb8e4 security: fix 3 vulnerabilities found during DD transaction audit
8a169127be security: fix __int128→uint64 overflow in TxBuilder collateral calc
8de117954a security: fix __int128→uint64 overflow in RPC collateral calculations
```

### Bug Fixes
```
352b701efc qt: re-validate amount border when DD balance updates
a36aa045be fix: ungate getdigidollardeploymentinfo RPC + fix activation test
05ecf4a257 fix: include trusted pending DD balance in send widget spendable amount
0f052131d9 fix: wire up GetPendingDDBalance() in getdigidollarbalance RPC
affb830c4d fix: gate new-peer mempool seeding on Dandelion enabled
35405ea65c fix: Dandelion++ infinite relay loop — duplicate detection and log spam
```

### Consensus & Chain
```
a074b3ce98 consensus: set SHA256D initial difficulty to >> 31 on testnet
a0de19bb1c fix: testnet difficulty settings + reset to testnet17 (RC17)
891f5a6220 fix: proper BIP9 activation sequence for DigiDollar on testnet
af16284931 fix: gate ALL DigiDollar RPCs behind BIP9 activation check
```

### UI
```
b422ebd807 ui: DigiDollar tab always visible with activation status overlay
c4344d675c qt: Add DDSendFee transaction type for DigiDollar transfer fees
```

### Tests & Docs
```
852896be3d test: add comprehensive RPC gating test for all 27 DD/Oracle RPCs
ae1f6b93d6 test: fix encrypted wallet test race condition
02ca53530e docs: DIGIDOLLAR_ACTIVATION_EXPLAINER.md — complete BIP9 activation guide
e72e790dfc docs: rewrite oracle setup guide
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

---

## Oracle Operator Setup

### Prerequisites
- DigiByte Core RC17 built from source with curl support (or download the binary)
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

### Existing Oracle — Upgrading from RC16 or Earlier

**Your oracle keys are safe.** See the **Testnet Reset** section at the top for migration steps. The key point: copy your oracle wallet from the old `testnet13` directory to the new `testnet17` directory.

After migration:
```bash
# Step 1: Start your node with RC17
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

**RC17 uses a new chain: testnet17 (port 12031).** Your old testnet13 data is incompatible.

**Steps to upgrade:**

1. **Save your oracle wallet** (if you run an oracle — see Oracle migration section above)
2. **Close your old wallet**
3. **Delete old testnet data:**
   - **Linux:** Delete `~/.digibyte/testnet13/` (and any older testnet directories)
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet13\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet13/`
4. **Download and install RC17**
5. **Update your `digibyte.conf`** — the `addnode` addresses remain the same:
   ```ini
   testnet=1
   
   [test]
   digidollar=1
   addnode=oracle1.digibyte.io
   ```
6. **Launch with `-testnet`** — a new `testnet17` directory will be created automatically
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
| Network | Testnet (testnet17) |
| Default P2P Port | **12031** |
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
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc17-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc17-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc17-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc17-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc17-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc17-aarch64-linux-gnu.tar.gz` |

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
- This is expected! RC17 uses real BIP9 activation. DigiDollar features unlock after miners signal support and the activation threshold is reached.
- Monitor progress: `digibyte-cli -testnet getdigidollardeploymentinfo`

### "Can't connect to network"
- RC17 uses **port 12031** (not 12030). Check your firewall.
- Make sure `addnode=oracle1.digibyte.io` is under `[test]` in your config

### "Old testnet data"
- Delete ALL old testnet directories (testnet10, testnet11, testnet12, testnet13) — RC17 uses testnet17

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
