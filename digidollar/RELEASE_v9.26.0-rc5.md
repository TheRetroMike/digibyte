**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im - Active development discussion happens here!

---

## What's New in RC5

### Wallet & Transaction Fixes
- **Conflicted DD transactions handled properly** - Conflicted DigiDollar transactions now correctly show as abandoned
- **Display abandoned status** - UI now shows abandoned status for DigiDollar transactions
- **Tier label mapping fixed** - Corrected tier-to-label mapping in DD Overview and Transactions widgets
- **Descriptor wallet DD spending** - Fixed DigiDollar spending for descriptor wallets
- **Theme crash fix** - Fixed wallet crash related to theme switching

### Consensus & Validation
- **Minimum mint amount rule** - Added activation height for minimum mint amount ($100 on testnet)
- **Historical block validation** - Skip oracle-dependent validation for historical blocks during sync
- **Oracle validation optimization** - Skip oracle validation during block connect for faster sync
- **ValidationContext flag** - Added skipOracleValidation flag for cleaner validation logic

### UI/Qt Improvements
- **Mint validation feedback** - Min/max amount validation feedback in Mint widget
- **macOS CSS compatibility** - Fixed transaction view styling on macOS
- **Cross-platform theming** - Ubuntu & Mac theming fixes for consistent appearance
- **Tooltip & Receive page** - Various UI polish fixes

### Performance
- **MAX_TX_INPUTS limit** - Prevents oversized transactions from being rejected
- **Faster IBD sync** - Reduced IBD threshold from 24h to 1h
- **Reduced oracle log spam** - Cleaner logs during normal operation
- **IBD widget throttling** - DD widgets pause updates during initial sync
- **Incremental UTXO processing** - Efficient DD UTXO tracking replaces full scans

### Index
- **DigiDollarStatsIndex enabled** - Network statistics index active by default

---

## Key Features Since RC1 (Cumulative)

These critical fixes were introduced in earlier RC versions but are essential to understand what makes RC5 stable:

### From RC4 - Network Reset & Core Fixes
- **New testnet8** - Fresh testnet environment with clean blockchain state
- **New P2P port 12030** - All nodes and oracles now use port 12030
- **Fix Qt freeze** - Removed verbose per-UTXO logging that caused GUI to hang
- **Fix large DD amounts** - Increased CScriptNum max size to 8 bytes
- **DigiDollarStatsIndex** - Network-wide DD metrics
- **Exact-amount redemptions** - Must redeem full collateral position

### From RC3 - Wallet Safety Fixes
- **DGB loss bug fixed** - Wallet-controlled change addresses for ALL DD transactions
- **Non-P2TR outputs allowed** - P2WPKH change outputs accepted in mint transactions
- **Windows 64-bit integer fixes** - Fixed LLP64 integer truncation

### From RC2 - Core Transaction Fixes
- **DD key persistence** - DigiDollar keys persist across wallet restarts
- **Taproot signing fixed** - DD transfers and redemptions sign correctly
- **UTXO scanning fixed** - Wallet correctly tracks received DD tokens

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only 2.23 DGB per person on Earth right now). Everything happens inside DigiByte Core wallet. You never give up control of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

## Current Status

- **Single Oracle Testing** - This beta uses one oracle node for price feeds. Production will use a decentralized oracle network.
- **Testnet Only** - All DGB and DUSD on testnet have no real value.

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
algo=scrypt
addnode=oracle1.digibyte.io
rpcuser=digibyte
rpcpassword=digibyte123
```

---

## Windows Setup

### Step 1: Download and Install
Download `digibyte-9.26.0-rc5-win64-setup.exe` and install normally.

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
- Title bar should say **"DigiByte Core - Wallet [testnet8]"**
- You should see a **DigiDollar** tab in the sidebar

### Data Directory Reference
- Config: `%APPDATA%\DigiByte\digibyte.conf`
- Testnet data: `%APPDATA%\DigiByte\testnet8\`

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
algo=scrypt
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
- Title bar should say **"DigiByte Core - Wallet [testnet8]"**
- You should see a **DigiDollar** tab in the sidebar

### Data Directory Reference
- Config: `~/Library/Application Support/DigiByte/digibyte.conf`
- Testnet data: `~/Library/Application Support/DigiByte/testnet8/`

---

## Ubuntu/Linux Setup

### Data Directory
```
~/.digibyte/
```
Config file: `~/.digibyte/digibyte.conf`

Testnet data stored in: `~/.digibyte/testnet8/`

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
algo=scrypt
addnode=oracle1.digibyte.io
rpcuser=digibyte
rpcpassword=digibyte123
EOF
```

2. Extract and run:
```bash
cd ~/Downloads
tar xzf digibyte-9.26.0-rc5-x86_64-linux-gnu.tar.gz
./digibyte-9.26.0-rc5-x86_64-linux-gnu/bin/digibyte-qt
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet8]"**
- You should see a **DigiDollar** tab in the sidebar

---

## Getting Testnet DGB

### Option 1: GUI Console Mining

Mine testnet DGB directly using the GUI console:

1. Go to the **Receive** tab and create a new address (copy your `dgbt1...` address)
2. Go to **Window > Console**
3. Type: `generatetoaddress 1 dgbt1qYOURADDRESSHERE`
4. Press Enter to mine 1 block
5. Wait for 100 confirmations before spending mined coins

### Option 2: CPU Miner (Recommended for Continuous Mining)

Use cpuminer to mine testnet DGB in the background. Make sure your wallet is running and synced first.

**Download cpuminer:** https://github.com/pooler/cpuminer

Build from source or download a release for your platform.

**Run cpuminer:**

Linux/macOS:
```bash
./minerd -a scrypt -o http://127.0.0.1:14025 -O digibyte:digibyte123 -t 4 --coinbase-addr=dgbt1qYOURADDRESSHERE
```

Windows (PowerShell):
```powershell
.\minerd.exe -a scrypt -o http://127.0.0.1:14025 -O digibyte:digibyte123 -t 4 --coinbase-addr=dgbt1qYOURADDRESSHERE
```

**Parameters explained:**
- `-a scrypt` - Use scrypt algorithm (matches `algo=scrypt` in config)
- `-o http://127.0.0.1:14025` - RPC endpoint (testnet RPC port)
- `-O digibyte:digibyte123` - RPC credentials from your config (user:password)
- `-t 4` - Number of CPU threads to use (adjust based on your CPU)
- `--coinbase-addr=dgbt1q...` - Your testnet receive address

The miner will continuously mine blocks and send rewards to your address.

---

## Testing DigiDollar Features

Once your wallet is synced and you have testnet DGB:

1. **View Network Status** - The DigiDollar Overview tab shows oracle price and network collateralization
2. **Mint DUSD** - Lock DGB as collateral to create DigiDollars
3. **Send DUSD** - Transfer DigiDollars to other testnet addresses
4. **Redeem DUSD** - Burn DigiDollars to unlock your DGB collateral (full position only)
5. **View History** - New DD Transactions tab shows complete transaction history

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (testnet8) |
| Default P2P Port | 12030 |
| Default RPC Port | 14025 |
| Oracle Node | oracle1.digibyte.io:12030 |
| Address Prefix | dgbt1... (bech32) |

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
- Default algorithm is scrypt which requires more computation

### "Transaction fee too high"
- DigiDollar transactions require 0.1 DGB minimum fee - this is expected
- This ensures reliable network propagation

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc5-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc5-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc5-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc5-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc5-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc5-aarch64-linux-gnu.tar.gz` |

---

## Upgrading from RC4

1. Simply download and run RC5 - no config changes needed
2. Your existing testnet8 data will be used automatically
3. The macOS Qt fixes should resolve any text visibility issues

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
