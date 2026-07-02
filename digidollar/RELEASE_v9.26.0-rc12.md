# DigiByte v9.26.0-rc12 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## What's New in RC12

### 🔮 Phase 2: Multi-Oracle Consensus (4-of-7 Schnorr Threshold)

The headline feature of RC12 is the activation of **Phase Two oracle consensus** on testnet. This moves from the single-oracle (1-of-1) system used in RC11 to a decentralized **4-of-7 Schnorr threshold** system with real exchange price data.

**Key changes:**
- **4-of-7 consensus:** Any 4 of 7 authorized oracle operators must provide valid signed price messages for a bundle to be accepted into a block
- **7 named oracle operators:** Jared (0), Green Candle (1), Bastian (2), DanGB (3), Shenger (4), Ycagel (5), Aussie (6)
- **Live exchange prices:** Oracles fetch real DGB/USD prices from Binance, CoinGecko, KuCoin, Gate.io, and Crypto.com
- **Phase Two activation at block 100:** Testnet activates multi-oracle consensus early for testing
- **Phase Two compact format (v0x02):** New on-chain encoding for multi-oracle bundles, maintaining the same 22-byte footprint
- **Phase-aware block validation:** `ValidateBlockOracleData()` now routes to Phase One or Phase Two validation based on block height

### 🔑 Wallet-Based Oracle Key Management (New!)

RC12 introduces **two new RPC commands** that make oracle setup dead simple — no manual key handling required:

#### `createoraclekey` — Generate your oracle keypair
```bash
digibyte-cli -testnet -rpcwallet=oracle createoraclekey <oracle_id>
```
- Generates a Schnorr keypair and stores the private key securely in your wallet
- Returns both the **X-only public key** (32 bytes, used in chainparams) and **compressed public key** (33 bytes, for reference)
- The private key never leaves your wallet

#### `startoracle` — Start your oracle with your ID
```bash
digibyte-cli -testnet -rpcwallet=oracle startoracle <oracle_id>
```
- Automatically loads the private key from your wallet — no raw hex needed
- Starts the oracle price feed thread
- Optional: pass `private_key_hex` as second parameter for manual key injection

> **⚠️ After restarting `digibyted`, you must run `startoracle` again.** The key persists in the wallet, but the oracle thread does not auto-start.

### 🌐 Live Exchange Price Fetching (New!)

RC12 oracles now fetch **real DGB/USD prices** from live exchanges via HTTPS. No more mock data — oracles pull from multiple sources and aggregate a reliable price.

**Supported Exchanges:**
| Exchange | Status |
|----------|--------|
| Binance | ✅ Live |
| CoinGecko | ✅ Live |
| KuCoin | ✅ Live |
| Gate.io | ✅ Live |
| Crypto.com | ✅ Live |

**Build system changes:**
- Added `openssl` (1.1.1w) and `libcurl` (8.5.0) to Guix depends for all platforms
- macOS builds link CoreFoundation + SystemConfiguration frameworks for libcurl proxy support
- Cross-platform CA certificate bundle detection with auto-retry fallback

### 📊 Complete Oracle RPC Command Reference

RC12 includes a full suite of Oracle RPC commands — split into **monitoring commands** (anyone can use) and **operator commands** (oracle operators only).

#### Monitoring Commands

| Command | Description |
|---------|-------------|
| `listoracle` | Shows your **local** oracle status. Is it running? What ID? What price am I submitting? What wallet has the key? If nothing's running, tells you to run `startoracle`. |
| `getoracles` | **Network-wide** view of ALL 7 oracles. Config (pubkey, endpoint), status, and actual last reported price from on-chain data — not just local runtime. This is what a stats site would call. |
| `getoracleprice` | Returns the single **consensus price** (median of all reporting oracles). This is the price DigiDollar actually uses for minting/redemption. |
| `getalloracleprices` | **Forensics deep dive.** Per-oracle breakdown with: exact price each oracle reported, % deviation from median, signature validity, which block it was in. Use this to catch anyone gaming the system. |

#### Operator Commands

| Command | Description |
|---------|-------------|
| `createoraclekey <id>` | Generate a new oracle Schnorr keypair in your wallet. One-time setup. |
| `getoraclepubkey <id>` | Show the oracle public key stored in your wallet. |
| `startoracle <id>` | Start running as an oracle operator (requires wallet with oracle key). Must re-run after every node restart. |
| `stoporacle <id>` | Stop your oracle. |
| `sendoracleprice` | Manually submit a price to the network (for testing/debugging). |

#### Usage Examples
```bash
# Check if your oracle is running
digibyte-cli -testnet -rpcwallet=oracle listoracle

# View all 7 oracles network-wide
digibyte-cli -testnet getoracles

# Get the consensus price DigiDollar uses
digibyte-cli -testnet getoracleprice

# Deep dive: see what each oracle reported
digibyte-cli -testnet getalloracleprices
```

### 📋 Quick Oracle Setup (2 Commands!)

```bash
# 1. Create your oracle keypair (one-time)
digibyte-cli -testnet -rpcwallet=oracle createoraclekey 5

# 2. Start your oracle (after every node restart)
digibyte-cli -testnet -rpcwallet=oracle startoracle 5
```

That's it. Send the X-only public key from step 1 to the DigiByte Core maintainer. Once your key is added to `chainparams.cpp`, step 2 is all you need.

See **`DIGIDOLLAR_ORACLE_SETUP.md`** for the complete oracle operator guide.

### 🐛 Critical Bug Fix: BIP9 DigiDollar Activation

- **Fixed BIP9 activation state machine** — In RC11, `ALWAYS_ACTIVE` combined with an early return in `ReadRegTestArgs()` prevented `-digidollaractivationheight` from working. RC12 ensures DigiDollar activation progresses through the full DEFINED → STARTED → LOCKED_IN → ACTIVE state machine on all networks.

### 🔧 Other Changes

- **`listoracles` → `getoracles` rename** — Renamed for clarity; `getoracles` returns network-wide oracle data, `listoracle` (new) shows local status
- **`getalloracleprices` RPC** — New forensics command showing per-oracle price breakdown, deviation, and signature validity
- **Real BIP9 signaling on testnet** — DigiDollar now uses proper BIP9 deployment signaling instead of `ALWAYS_ACTIVE`
- **MockOracleManager restricted to regtest** — Security fix: mock oracle data only available in regtest, removed from testnet/mainnet
- **Oracle P2P propagation fixes** — Fixed discovery, message broadcasting, and Phase 2 signature validation
- **7 real oracle keys for testnet** — 4-of-7 consensus with named operators (Jared, Green Candle, Bastian, DanGB, Shenger, Ycagel, Aussie)
- **2-year lock tier** — New collateral lock option across consensus, RPC, wallet, and GUI
- **Multiple consensus bug fixes** — uint64 overflow in collateral calc, fee subtraction, transfer conservation, oracle price ceiling ($100), GUI price display (was 1000x off)
- **Unbounded memory fix** — Script metadata map capped at 10,000 entries
- **Oracle ID validation fix** — Now correctly uses 0-based indexing (0–29)
- **Oracle unit test fixes** — 5 bugs fixed in oracle test suite
- **Phase Two RegTest support** — RegTest now configured for 3-of-5 oracle consensus
- **Phase Two integration tests** — New multi-oracle RegTest integration script
- **Pending message lifecycle tests** — Tests for Phase Two oracle message handling
- **Bughunt regression tests** — New tests from overnight automated code review
- **Wallet splash image** — Updated from RC11 to RC12 branding
- **DD Transactions auto-refresh** — DigiDollar Transactions tab now refreshes automatically
- **Consolidated oracle docs** — Single source of truth in `DIGIDOLLAR_ORACLE_SETUP.md`, old docs moved to `docs/`

---

## Technical Changes

| File | Change |
|------|--------|
| `configure.ac` | Version bump RC11 → RC12; add CoreFoundation/SystemConfiguration frameworks for macOS libcurl |
| `src/kernel/chainparams.cpp` | Testnet oracle params: 4-of-7, Phase2Height=100, 7 named oracle operators; RegTest: same |
| `src/rpc/digidollar.cpp` | New `createoraclekey`, wallet-based `startoracle`, `listoracle`, `getoracles`, `getalloracleprices`, `getoraclepubkey`, `stoporacle`, `sendoracleprice` RPCs |
| `src/wallet/` | Oracle key persistence in descriptor wallets |
| `src/oracle/bundle_manager.cpp` | Phase-aware `ValidateBlockOracleData()`, Phase Two `CreateOracleScript()` (v0x02), Phase Two `ExtractOracleBundle()` (v0x02), Phase Two `AddOracleBundleToBlock()` |
| `src/oracle/exchange.cpp` | Live exchange price fetching (Binance, CoinGecko, KuCoin, Gate.io, Crypto.com), cross-platform CA cert detection, SSL fallback retry |
| `src/oracle/node.cpp` | Oracle P2P propagation, discovery, Phase 2 signature validation fixes |
| `src/versionbits.cpp` | BIP9 activation fix for DigiDollar state machine; real BIP9 signaling on testnet |
| `depends/packages/openssl.mk` | New: OpenSSL 1.1.1w static build for all platforms |
| `depends/packages/libcurl.mk` | New: libcurl 8.5.0 linked against static OpenSSL for HTTPS oracle fetching |
| `DIGIDOLLAR_ORACLE_SETUP.md` | Comprehensive oracle operator setup guide (single source of truth) |
| `ORACLE_OPERATOR_GUIDE.md` | Third-party oracle operator instructions |

---

## Known Issues

- Testnet oracle keys (oracles 1–4) use known test values (SHA256 hash-derived). Real operator keys will be swapped before mainnet.
- Mainnet oracle consensus remains disabled (`nDigiDollarPhase2Height = INT_MAX`)
- `startoracle` must be re-run after every `digibyted` restart

---

## Upgrade Notes

**RC12 uses testnet13 network (port 12030). This is a new testnet — fresh chain, no data carries over from RC10/RC11.**

### If Upgrading from RC9 or Earlier:
1. Close your old wallet
2. Delete old testnet data:
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet10\` and `testnet11\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet10/` and `testnet11/`
   - **Linux:** Delete `~/.digibyte/testnet10/` and `~/.digibyte/testnet11/`
3. Download and install RC12
4. Launch with `-testnet` flag

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only 2.23 DGB per person on Earth right now). Everything happens inside DigiByte Core wallet. You never give up control of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Current Status

- **Phase 2 Multi-Oracle Testing** — This release activates 4-of-7 Schnorr threshold oracle consensus on testnet with live exchange prices.
- **Testnet Only** — All DGB and DUSD on testnet have no real value.

---

## Quick Start Guide

### Step 1: Download

Download the appropriate file for your platform from the Downloads section below and extract it.

### Step 2: Create Config File

Create the config file in your platform's data directory with the following contents:

```ini
# DigiByte Configuration
# Global settings (apply to all networks)
testnet=1
server=1
txindex=1

# Testnet-specific settings
[test]
digidollar=1
digidollarstatsindex=1
algo=sha256d
addnode=oracle1.digibyte.io
rpcuser=digibyte
rpcpassword=digibyte123
```

---

## Windows Setup

### Step 1: Download and Install
Download `digibyte-9.26.0-rc12-win64-setup.exe` and install normally.

### Step 2: First Launch (Testnet Mode)
You must launch in testnet mode. Open **PowerShell** and run:
```powershell
& "C:\Program Files\DigiByte\digibyte-qt.exe" -testnet
```

The wallet will start in testnet mode and create the data directory automatically.

### Step 3: Create Config File
1. Press `Win + R`, type `%APPDATA%\DigiByte` and press Enter
2. Create a new text file named `digibyte.conf` (remove the `.txt` extension)
3. Paste the config contents from Step 2 above and save

### Step 4: Restart
Close the wallet and launch again from PowerShell:
```powershell
& "C:\Program Files\DigiByte\digibyte-qt.exe" -testnet
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet13]"**
- You should see a **DigiDollar** tab in the sidebar

### Data Directory Reference
- Config: `%APPDATA%\DigiByte\digibyte.conf`
- Testnet data: `%APPDATA%\DigiByte\testnet13\`

---

## macOS Setup

### Step 1: Download and Extract
Download the `.dmg` file for your Mac and open it. Drag **DigiByte-Qt** to your Desktop.

### Step 2: First Launch (Testnet Mode)
You must launch in testnet mode. Open **Terminal** and run:
```bash
cd ~/Desktop
./DigiByte-Qt.app/Contents/MacOS/DigiByte-Qt -testnet
```

If you get a security warning, right-click the app and select "Open", or run:
```bash
xattr -cr ~/Desktop/DigiByte-Qt.app
```
Then try the launch command again.

The wallet will start in testnet mode and create the data directory automatically.

### Step 3: Create Config File
In Terminal:
```bash
mkdir -p ~/Library/Application\ Support/DigiByte
cat > ~/Library/Application\ Support/DigiByte/digibyte.conf << 'EOF'
# DigiByte Configuration
# Global settings (apply to all networks)
testnet=1
server=1
txindex=1

# Testnet-specific settings
[test]
digidollar=1
digidollarstatsindex=1
algo=sha256d
addnode=oracle1.digibyte.io
rpcuser=digibyte
rpcpassword=digibyte123
EOF
```

### Step 4: Restart
Close the wallet and launch again from Terminal:
```bash
cd ~/Desktop
./DigiByte-Qt.app/Contents/MacOS/DigiByte-Qt -testnet
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet13]"**
- You should see a **DigiDollar** tab in the sidebar

### Data Directory Reference
- Config: `~/Library/Application Support/DigiByte/digibyte.conf`
- Testnet data: `~/Library/Application Support/DigiByte/testnet13/`

---

## Ubuntu/Linux Setup

### Data Directory
```
~/.digibyte/
```
Config file: `~/.digibyte/digibyte.conf`

Testnet data stored in: `~/.digibyte/testnet13/`

### Steps:
1. Open Terminal and create config:
```bash
mkdir -p ~/.digibyte
cat > ~/.digibyte/digibyte.conf << 'EOF'
# DigiByte Configuration
# Global settings (apply to all networks)
testnet=1
server=1
txindex=1

# Testnet-specific settings
[test]
digidollar=1
digidollarstatsindex=1
algo=sha256d
addnode=oracle1.digibyte.io
rpcuser=digibyte
rpcpassword=digibyte123
EOF
```

2. Extract and run:
```bash
cd ~/Downloads
tar xzf digibyte-9.26.0-rc12-x86_64-linux-gnu.tar.gz
./digibyte-9.26.0-rc12/bin/digibyte-qt
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet13]"**
- You should see a **DigiDollar** tab in the sidebar

---

## Getting Testnet DGB

### Option 1: GUI Console Mining

Mine testnet DGB directly using the GUI console:

1. Go to the **Receive** tab and create a new address (copy your `dgbt1...` address)
2. Go to **Window > Console**
3. Type: `generatetoaddress 1 dgbt1qYOURADDRESSHERE`
4. Press Enter to mine 1 block
5. Wait for 8 confirmations before spending mined coins (reduced from 100 in testnet)

### Option 2: CPU Miner (Recommended for Continuous Mining)

Use cpuminer to mine testnet DGB in the background. Make sure your wallet is running and synced first.

**Download cpuminer:** https://github.com/pooler/cpuminer

Build from source or download a release for your platform.

**Run cpuminer:**

Linux/macOS:
```bash
./minerd -a sha256d -o http://127.0.0.1:14026 -O digibyte:digibyte123 -t 4 --coinbase-addr=dgbt1qYOURADDRESSHERE
```

Windows (PowerShell):
```powershell
.\minerd.exe -a sha256d -o http://127.0.0.1:14026 -O digibyte:digibyte123 -t 4 --coinbase-addr=dgbt1qYOURADDRESSHERE
```

**Parameters explained:**
- `-a sha256d` — Use SHA256d algorithm (matches `algo=sha256d` in config)
- `-o http://127.0.0.1:14026` — RPC endpoint (testnet RPC port)
- `-O digibyte:digibyte123` — RPC credentials from your config (user:password)
- `-t 4` — Number of CPU threads to use (adjust based on your CPU)
- `--coinbase-addr=dgbt1q...` — Your testnet receive address

The miner will continuously mine blocks and send rewards to your address.

---

## Testing DigiDollar Features

Once your wallet is synced and you have testnet DGB:

1. **View Network Status** — The DigiDollar Overview tab shows oracle price and network collateralization
2. **Mint DUSD** — Lock DGB as collateral to create DigiDollars
3. **Send DUSD** — Transfer DigiDollars to other testnet addresses
4. **Redeem DUSD** — Burn DigiDollars to unlock your DGB collateral (full position only)
5. **View History** — DD Transactions tab shows complete transaction history (now auto-refreshes!)
6. **Coin Control** — Use manual DD input selection for advanced redemptions
7. **Address Book** — Save frequently used DD addresses for quick sending
8. **Export History** — Export DD transactions to CSV for record keeping

---

## Oracle Operator Setup (Phase 2 Test)

Want to run an oracle node for the Phase 2 multi-oracle test? Here's the simple version:

### Prerequisites
- DigiByte Core RC12 built from source with curl support
- An assigned oracle ID (0–4 for testnet, contact the maintainer)

### Two-Command Setup

```bash
# Step 1: Create wallet and generate oracle key (one-time)
digibyte-cli -testnet createwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle createoraclekey <your_oracle_id>

# Step 2: Start your oracle (after every node restart)
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

**Step 1** generates a Schnorr keypair, stores the private key in your wallet, and returns the public key. Send the **X-only public key** (32-byte hex) to the maintainer for inclusion in `chainparams.cpp`.

**Step 2** loads the private key from your wallet and starts the oracle price feed thread. Your node will automatically fetch DGB/USD prices from multiple exchanges (minimum 2 sources required) and broadcast signed price messages to the network.

For the complete guide including configuration, security, troubleshooting, and RPC reference, see **`DIGIDOLLAR_ORACLE_SETUP.md`**.

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (testnet13) |
| Default P2P Port | 12030 |
| Default RPC Port | 14025 |
| Oracle Node | oracle1.digibyte.io:12030 |
| Address Prefix | dgbt1... (bech32) |
| Phase Two Activation | Block 100 |
| Oracle Consensus | 4-of-7 Schnorr threshold |

### Fork Schedule (Testnet12)
| Feature | Block Height |
|---------|--------------|
| MultiAlgo | 100 |
| MultiShield | 200 |
| DigiSpeed | 400 |
| Odocrypt | 500 |
| DigiDollar | 550 |

### Emission Schedule (Testnet12)
| Period | Block Range | Reward |
|--------|-------------|--------|
| Period I | 0–66 | 72,000 DGB |
| Period IV | 67–199 | 8,000 DGB (with decay) |
| Period V | 200–399 | 2,459 DGB (with decay) |
| Period VI | 400+ | 1,078.5 DGB (with decay) |

---

## Commits Since RC11

### Oracle RPC & Key Management
- Implement `createoraclekey` wallet RPC for oracle keypair generation
- Implement wallet-based `startoracle` with automatic key loading
- Add oracle key persistence in descriptor wallets
- Replace `listoracles` with `getoracles` + add `listoracle` for local status
- Add `getalloracleprices` for per-oracle price transparency
- Fix oracle ID validation to use 0-based indexing

### Live Exchange Price Fetching
- Add `openssl` (1.1.1w) package to Guix depends for HTTPS support
- Add `libcurl` (8.5.0) dependency for oracle HTTP functionality
- Add CA certificate bundle detection for SSL/HTTPS oracle fetching
- Set proper system CA cert paths in OpenSSL build (`--openssldir=/etc/ssl`)
- Add `curl_global_init` for OpenSSL initialization
- Add SSL debug logging and fallback retry without verification
- Add cross-platform SSL CA handling and Windows libcurl linking
- Link CoreFoundation + SystemConfiguration frameworks on macOS for libcurl
- Remove hardcoded mock prices and fallback data from oracle
- Restrict MockOracleManager to regtest, remove mock exchange data

### Multi-Oracle Consensus (Phase 2)
- Activate Phase Two multi-oracle consensus on testnet (4-of-7)
- Add 7 real oracle keys for Phase Two testnet (Jared, Green Candle, Bastian, DanGB, Shenger, Ycagel, Aussie)
- Add Phase Two compact format (version 0x02) to CreateOracleScript
- Add Phase Two decoding to ExtractOracleBundle
- Add Phase Two bundle creation from pending messages in AddOracleBundleToBlock
- Update ValidateBlockOracleData with phase-aware consensus validation
- Configure RegTest for Phase Two oracle consensus
- Use Phase 2 signing in CreatePriceMessage
- Fix oracle P2P propagation, discovery, and Phase 2 signature validation
- Fix cached price to require consensus threshold, not individual messages
- Use OraclePriceMsg wrapper in P2P broadcast for consistency

### BIP9 & Consensus
- Fix BIP9 DigiDollar activation state machine (ALWAYS_ACTIVE + ReadRegTestArgs early return)
- Implement real BIP9 signaling for DigiDollar on testnet
- Unify oracle activation checks to use BIP9 deployment state
- Unify oracle price ceiling to $100 across all validation layers
- Add 2-year lock tier across consensus, RPC, wallet, and GUI

### Bug Fixes
- Fix uint64 overflow in collateral calculation (use __int128)
- Fix fee input subtraction from collateral release validation
- Fix transfer DD conservation check with UTXO lookup
- Fix GUI oracle price display (was 10,000x too high in RegTest)
- Fix hardcoded height/price in wallet transfer and redeem operations
- Cap script metadata map at 10,000 entries to prevent unbounded growth
- Add ERR state reconstruction from current system health
- Add volatility state reconstruction from block oracle prices
- Fix Schnorr signature verification mismatch in oracle system
- Fix duplicate ca_bundle_paths declaration in exchange.cpp
- Guard MockOracleManager in positions widget with regtest check
- Align RPC lock tier mapping with consensus (remove phantom 2-year tier)

### Tests
- Add TDD tests for Schnorr signature Phase2 verification
- Add Phase Two multi-oracle RegTest integration script
- Add multi-oracle testnet test script
- Add pending message lifecycle tests for Phase Two
- Add bughunt regression tests
- Add oracle wallet key tests and update validation tests
- Add Phase Two bundle lifecycle and test helpers
- Add oracle P2P message handlers to Python test framework
- Update existing oracle tests for Phase Two activation height
- Fix 5 oracle unit test failures

### Docs & Housekeeping
- Consolidate oracle setup guides into DIGIDOLLAR_ORACLE_SETUP.md
- Add ORACLE_OPERATOR_GUIDE.md for third-party oracle operators
- Comprehensive accuracy pass on DIGIDOLLAR_ORACLE_SETUP.md
- Clarify dual public key formats in createoraclekey docs
- Add bug hunt report, DigiDollar bugs tracker, and oracle discovery architecture
- Update wallet splash image from RC11 to RC12
- Add auto-refresh to DD Transactions tab
- Version bump to v9.26.0-rc12

---

## Troubleshooting

### "DigiDollar tab not appearing"
- Verify `digidollar=1` is under `[test]` section in config
- Verify `testnet=1` is at the top of config (not under any section)
- Restart the wallet after config changes

### "Not connecting to network"
- Check your firewall allows port 12030
- Verify `addnode=oracle1.digibyte.io` is under `[test]` in config

### "Oracle price shows 0 or N/A"
- Wait for sync to complete
- The oracle broadcasts price updates every few minutes
- Check Window > Console: `getoracleprice`

### "Mining not working"
- Ensure `algo=sha256d` is in your config under `[test]`
- SHA256d is recommended for fastest CPU mining

### "No wallet is loaded" when running oracle commands
- Add `-rpcwallet=oracle` to your `createoraclekey` and `startoracle` commands

### "Oracle not configured" from `startoracle`
- Run `createoraclekey` first to generate and store the key in your wallet

### "Transaction fee too high"
- DigiDollar transactions require 0.1 DGB minimum fee — this is expected
- This ensures reliable network propagation

### "Old testnet data causing crashes"
- Delete your testnet10 and testnet11 folders completely (see Upgrade Notes above)
- RC12 uses testnet13 blockchain (new chain, bumped from testnet12 in RC10/RC11)

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc12-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc12-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc12-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc12-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc12-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc12-aarch64-linux-gnu.tar.gz` |

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
