# DigiByte v9.26.0-rc20 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ SAME TESTNET — NO RESET

**RC20 uses the same testnet19 chain as RC19.** No data migration needed.

- **No chain reset** — your blockchain data, wallets, and oracle keys all carry over
- **Same ports:** P2P **12033**, RPC **14025**
- **Same oracle consensus:** **5-of-9** Schnorr threshold
- **Just update the binary** and restart your node

### 🔑 Oracle Operators — IMPORTANT

After upgrading to RC20, **you must reload your oracle wallet and restart your oracle:**

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

Qt users: **File → Open Wallet → oracle**, then **Help → Debug Window → Console** → `startoracle <your_oracle_id>`

> **⚠️ After every node restart, you must re-load your wallet and re-start the oracle.** Your key persists in the wallet — you never need to run `createoraclekey` again.

---

## What's New in RC20

RC20 is a **stability and CI/CD hardening release** with 22 commits since RC19. It fixes two critical bugs reported by community testers (fresh node sync failure and stale wallet validation), adds the Round 2 oracle consensus attestation P2P protocol, and resolves 12 CI/CD pipeline issues.

---

## 🐛 Critical Bug Fixes

### IBD Sync Stuck at Block 7586 (Fresh Node Sync Failure)

**Reported by:** Community testers on Gitter

**The problem:** Fresh nodes syncing the testnet chain would get permanently stuck at block 7586 (the first DigiDollar transaction) with `bad-oracle-price`. The node would refuse to sync further.

**Root cause:** `IsInitialBlockDownload()` uses a latch mechanism with `max_tip_age=1h`. On testnet (~9,900 blocks), a fresh node catches up to within 1 hour of the tip very quickly. Once that happens, the IBD flag latches `false` permanently — even though the node is still connecting historical blocks. When it hits block 7586, there's no oracle price available (no oracle bundle in the coinbase, no P2P oracle data yet), so validation rejects it with `bad-oracle-price`.

**The fix:** `ConnectBlock` now detects when a node is catching up (`pindex->nHeight < m_best_header->nHeight`) and skips oracle validation when no deterministic block oracle price exists. This is safe because those blocks are already part of the best chain accepted by peers. Once the node reaches the tip, full P2P oracle price validation kicks in for live blocks.

**Impact:** Fresh nodes now sync the entire chain without getting stuck. Reindex also works correctly.

### Mint Tab Validation Stale After Balance Change

**Reported by:** Community testers on Gitter

**The problem:** If you had the "Mint DD" tab open and received DGB, the yellow "Insufficient DGB collateral" warning stayed even though you now had enough DGB to mint. You had to navigate away and back for it to notice the balance change.

**The fix:** `updateBalance()` now calls `updateAmountValidation()` and `updateMintButton()` so the validation state refreshes immediately when your balance changes.

### Stale Oracle Data in `getoracles` RPC

**The problem:** The `getoracles` RPC was showing stale status for offline oracles instead of reflecting current state.

**The fix:** Oracle status now reflects live state, correctly showing which oracles are online and reporting.

---

## 🔐 NEW: Round 2 Oracle Consensus Attestation P2P Protocol

RC20 adds the P2P message handlers for the Phase 2 multi-oracle consensus attestation protocol:

- **New P2P message types:** `ORACLECONSENSUS` and `ORACLEATTESTATION`
- **BroadcastConsensusProposal:** Oracles propose consensus prices to peers
- **Attestation tracking:** Nodes track which oracles have attested to each consensus round
- **JohnnyLawDGB** added to oracle display names at index 8
- **12 new test cases** covering Round 2 consensus attestation

These handlers prepare the network for fully decentralized oracle consensus, where oracle nodes coordinate price agreement through P2P messaging rather than independent computation.

---

## 🔧 CI/CD Pipeline Fixes (PR #380)

12 commits fixing CI/CD reliability issues:

| Fix | Description |
|-----|-------------|
| OpenSSL build failure | Moved `-D_GNU_SOURCE` from `config_opts` to `CFLAGS/CPPFLAGS` where OpenSSL expects them |
| libcurl depends failure | Fixed build flags routing through make variables |
| FD overflow crashes | Resolved file descriptor overflow causing test failures on CI runners |
| Missing log categories | Fixed test failures from unregistered DigiDollar debug log categories |
| P2P test flakes on macOS | Excluded flaky P2P tests on macOS CI runners |
| Functional test timeouts | Added `timeout-factor` for CI runner resource constraints |
| Parallel test flakes | Reverted to `--jobs=1` to prevent P2P port conflicts in CI |
| Branch triggers | Fixed push triggers for feature/fix branches and added `workflow_dispatch` |
| Static cast cleanup | Replaced C-style casts with `static_cast` in `RaiseFileDescriptorLimit` |
| DD transfer test | Fixed functional test that relied on sender node mining the transfer block |

---

## 📊 All Changes: RC19 → RC20 (One-Line Summary)

| Category | Count | Summary |
|----------|-------|---------|
| 🐛 Bug fixes | 3 | IBD sync stuck at block 7586, stale mint validation, stale oracle RPC |
| 🔐 Protocol | 4 | Round 2 P2P message types, consensus proposal broadcast, attestation tracking |
| 🔧 CI/CD | 12 | OpenSSL/libcurl build fixes, FD overflow, P2P test flakes, macOS runners |
| 📚 Documentation | 2 | ARCHITECTURE.md + REPO_MAP updates |
| 🧪 Tests | 1 | Round 2 consensus attestation protocol tests |

**Total: 22 commits (excluding merges)**

---

## 🧪 Testing

### Test Results
- **1,948+ C++ unit tests** — all passing
- **311 functional tests** — all passing
- **21 Qt widget tests** — all passing (1 new: mint validation on balance change)
- **13 DigiDollar consensus tests** — all passing (1 new: IBD oracle skip)
- **12 Round 2 attestation tests** — all passing

---

## Technical Changes

| File | Change |
|------|--------|
| `src/validation.cpp` | IBD catch-up heuristic — skip oracle validation during sync |
| `src/qt/digidollarmintwidget.cpp` | Refresh validation state on balance change |
| `src/net_processing.cpp` | ORACLECONSENSUS and ORACLEATTESTATION P2P handlers |
| `src/oracle/bundle_manager.cpp` | BroadcastConsensusProposal + attestation tracking |
| `src/protocol.cpp/h` | New P2P message type constants |
| `src/rpc/oracle.cpp` | JohnnyLawDGB oracle name, stale data fix |
| `src/test/digidollar_consensus_tests.cpp` | IBD oracle skip test |
| `src/qt/test/digidollarwidgettests.cpp` | Mint validation balance change test |
| `contrib/guix/*`, `ci/*` | CI/CD pipeline fixes |

---

## Commits Since RC19

### Bug Fixes
```
a4b3bd5890 fix: IBD catch-up sync stuck at block 7586 with bad-oracle-price
cd4dc4f3f9 fix: mint validation updates on balance change
63620442e0 fix: getoracles RPC showing stale data for offline oracles
a8f57877aa fix: mine DD transfer on sender node to ensure block inclusion
```

### Oracle Protocol (Round 2)
```
3cc9255765 net_processing: Add ORACLECONSENSUS and ORACLEATTESTATION P2P handlers
19e213e9f5 oracle: Add BroadcastConsensusProposal and attestation tracking
d29af474d3 protocol: Add ORACLECONSENSUS and ORACLEATTESTATION P2P message types
1995821c27 rpc: Add JohnnyLawDGB to oracle_names at index 8
```

### Tests
```
1017ff01a9 test: Add Round 2 consensus attestation protocol tests
```

### CI/CD Pipeline (PR #380)
```
5bb31114ce ci: exclude flaky P2P tests on macOS CI runners
f0e53b4d9b ci: add timeout-factor for functional tests on CI runners
8f106f6cdb ci: revert functional tests to --jobs=1 to avoid P2P flakes
5d9f5d7ff7 ci: run functional tests with --jobs=2 for faster CI
9aa2f6fc8b fix: move OpenSSL compiler flags from config_opts to CFLAGS/CPPFLAGS
cfa54b5a9b fix: resolve CI depends build failures for OpenSSL and libcurl
c5b9060bb7 style: use static_cast instead of C-style casts in RaiseFileDescriptorLimit
8df2da6ee2 ci: remove push triggers for feature/fix branches
fff5f47c0c ci: add push triggers for feature/fix branches and workflow_dispatch
13dc517aaf fix: resolve CI test failures from FD overflow and missing log categories
```

### Documentation
```
81a6edbe02 docs: update ARCHITECTURE.md + REPO_MAP.md with DigiDollar Phase 2 + RED HORNET audit (2026-02-14)
50d5f2884a docs: Update REPO_MAP_DIGIDOLLAR with Round 2 protocol additions
```

### Version
```
951f4af72f Bump version to v9.26.0-rc20, update wallet image
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
- DigiByte Core RC20 built from source with curl support (or download the binary)
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

### Existing Oracle — Upgrading from RC19

**No chain reset.** Just update the binary and restart:

```bash
# Step 1: Stop your node
digibyte-cli -testnet stop

# Step 2: Replace the binary with RC20

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
2. **Install RC20** and start the node (it uses the `testnet19` directory)
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

### Upgrading from RC19

**No chain reset.** Simply replace the binary and restart:

1. **Stop your node:** `digibyte-cli -testnet stop`
2. **Replace the binary** with the RC20 download
3. **Start your node:** `digibyted -testnet -daemon` (or launch Qt)
4. **Reload your oracle** (if applicable — see Oracle section above)

Your blockchain data, wallets, oracle keys, and all configuration carry over unchanged.

### Upgrading from RC18 or Earlier

**RC19 reset the testnet chain.** If you're coming from RC18 or earlier, follow the RC19 migration steps:

1. **Save your oracle wallet** (if you run an oracle — see Oracle migration section above)
2. **Delete old testnet data:**
   - **Linux:** Delete `~/.digibyte/testnet18/` (and any older testnet directories)
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet18\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet18/`
3. **Download and install RC20**
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
- Oracle prices show as 0 or "Not Reporting" until enough oracle operators restart their oracles on RC20

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
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc20-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc20-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc20-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc20-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc20-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc20-aarch64-linux-gnu.tar.gz` |

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

### "Sync stuck at block 7586" (RC19 bug — FIXED in RC20)
- This was the IBD latch bug. Upgrade to RC20 and resync — it will sync the full chain without issues.
- If you copied blocks/chainstate from another node as a workaround, that still works too.

### "Can't connect to network"
- RC19/RC20 uses **port 12033** (not 12032 or earlier). Check your firewall.
- Make sure `addnode=oracle1.digibyte.io` is under `[test]` in your config

### "Old testnet data"
- If coming from RC18 or earlier, delete ALL old testnet directories (testnet10 through testnet18) — RC19/RC20 uses testnet19

### "No wallet is loaded" when running oracle commands
- Load your wallet first: `digibyte-cli -testnet loadwallet "oracle"`
- For Qt: **File → Open Wallet → oracle**

### "Mining not working"
- Blocks 0-99 are scrypt-only. After block 100, multi-algo activates.
- Set `algo=sha256d` in config for fastest CPU mining after block 100

### "Oracle price shows 0 or N/A"
- Oracle prices require BIP9 activation first
- Wait for activation and sufficient oracle operators to come online after upgrading to RC20

### "Node stuck 7 hours behind after reindex"
- Same IBD latch bug as block 7586 — fixed in RC20. Upgrade and resync.

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

When reporting bugs, start your message with **BUG:** and include: what happened, steps to reproduce, platform (Windows/Linux/Mac), and any error messages from your debug.log file.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
