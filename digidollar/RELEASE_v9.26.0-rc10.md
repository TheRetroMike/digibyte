**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im - Active development discussion happens here!

---

## What's New in RC10

### Critical Bug Fixes
- **Fix Lock Date display** - Now uses actual mint transaction timestamp instead of block calculation
- **Fix wallet restore via descriptor import** - Full DD balance recovery with rescan
- **Fix TRANSFER change detection** - Properly detects DD change outputs after wallet restore
- **Fix MINT rescan chronological order** - Correct UTXO processing order during rescan
- **Fix 1969 timestamps** - Transaction timestamps now display correctly after wallet restore
- **Fix redeem button crash** - RPC name mismatch causing crash resolved

### Wallet Restore Improvements
- **IsDDOutputMine()** - New function for descriptor wallet compatibility
- **Descriptor wallet support** - TRANSFER rescan uses IsDDOutputMine for proper detection
- **MINT/change output detection** - Wallet restore correctly identifies all DD outputs

### UI/UX Improvements
- **DD Vault tab overhaul** - Lock Date column, "Locked" button for immature vaults
- **Compact GUI** - Reduced font sizes, padding, and minimum window heights
- **Styled tooltips** - Consistent theming across Overview balance labels
- **Table column balancing** - Improved layout for DD transaction tables

### Architecture Changes
- **2-path redemption model** - Simplified to Normal and ERR paths only (removed 4 legacy paths)
- **Separate change addresses** - Collateral and DGB change now use distinct wallet addresses
- **Dust remainder handling** - DD transfers allow dust remainder for full balance sends

### Network
- **Testnet12** - New network on port 12034

---

## Commits Since RC9

- Fix Lock Date to use actual mint transaction timestamp
- Improve DD Vault tab display with correct dates and statuses
- Fix wallet restore via descriptor import + rescan
- Fix TRANSFER change detection after wallet restore
- Fix wallet restore detecting MINT and change outputs
- Fix TRANSFER rescan to use IsDDOutputMine for descriptor wallets
- Fix MINT rescan to use chronological UTXO processing
- Add IsDDOutputMine() for descriptor wallet compatibility
- Fix redeem button crash from RPC name mismatch
- Fix transaction timestamps showing 1969 after wallet restore
- Improve DD Vault tab UX with Lock Date column and status clarity
- Compact GUI with reduced font sizes and padding
- Improve window resizing with reduced minimum heights
- Balance table columns and set minimum window height
- Add styled tooltips to Overview balance labels
- Use separate wallet addresses for collateral and DGB change
- Allow DD transfers with dust remainder for full balance sends
- Simplify to 2-path redemption model (Normal + ERR)
- Update validation, transaction builder, and wallet code for 2-path model

---

## Upgrade Notes

**RC10 uses testnet12 network (port 12034). You must delete old testnet data when upgrading.**

### If Upgrading from RC9 or Earlier:
1. Close your old wallet
2. Delete old testnet data:
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet10\` and `testnet11\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet10/` and `testnet11/`
   - **Linux:** Delete `~/.digibyte/testnet10/` and `~/.digibyte/testnet11/`
3. Download and install RC10
4. Launch with `-testnet` flag

---

## Key Features Since RC1 (Cumulative)

### From RC9 - Coin Control & Multi-Input
- DD Coin Control for Redemptions and Sends
- QR Code Popup Dialog for DD Receive tab
- 3-second Confirmation Delay for DD sends
- Clean DGB/DD Address Separation
- Decentralized DD Amount Lookup via blockchain

### From RC8 - MAST Simplification
- Reduced to 2 redemption paths (Normal and ERR only)
- Removed Partial Redemption (users must redeem full vault amounts)
- Added CLTV to ERR path (both paths require timelock expiry)

### From RC7 - ERR & Oracle Fixes
- Corrected ERR semantics (increases DD burn, not reduces collateral)
- Multi-oracle consensus infrastructure
- Wallet restore fixes for redeemed vaults

### From RC6 - Network Reset & HD Keys
- Deterministic DigiDollar keys using HD derivation
- Tier derivation fixes for wallet restore

### From RC5 - Wallet & Validation Fixes
- Conflicted DD transactions handled properly
- Minimum mint amount rule with activation height

### From RC4 - Network Reset & Core Fixes
- Fix Qt freeze from verbose logging
- Fix large DD amounts (8-byte CScriptNum)
- DigiDollarStatsIndex for network metrics

### From RC3 - Wallet Safety Fixes
- DGB loss bug fixed with wallet-controlled change addresses
- Non-P2TR outputs allowed for change

### From RC2 - Core Transaction Fixes
- DD key persistence across wallet restarts
- Taproot signing fixed for transfers and redemptions

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
algo=sha256d
addnode=oracle1.digibyte.io
rpcuser=digibyte
rpcpassword=digibyte123
```

---

## Windows Setup

### Step 1: Download and Install
Download `digibyte-9.26.0-rc10-win64-setup.exe` and install normally.

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
- Title bar should say **"DigiByte Core - Wallet [testnet12]"**
- You should see a **DigiDollar** tab in the sidebar

### Data Directory Reference
- Config: `%APPDATA%\DigiByte\digibyte.conf`
- Testnet data: `%APPDATA%\DigiByte\testnet12\`

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
- Title bar should say **"DigiByte Core - Wallet [testnet12]"**
- You should see a **DigiDollar** tab in the sidebar

### Data Directory Reference
- Config: `~/Library/Application Support/DigiByte/digibyte.conf`
- Testnet data: `~/Library/Application Support/DigiByte/testnet12/`

---

## Ubuntu/Linux Setup

### Data Directory
```
~/.digibyte/
```
Config file: `~/.digibyte/digibyte.conf`

Testnet data stored in: `~/.digibyte/testnet12/`

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
tar xzf digibyte-9.26.0-rc10-x86_64-linux-gnu.tar.gz
./digibyte-9.26.0-rc10/bin/digibyte-qt
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet12]"**
- You should see a **DigiDollar** tab in the sidebar

---

## Getting Testnet DGB

### Option 1: GUI Console Mining

Mine testnet DGB directly using the GUI console:

1. Go to the **Receive** tab and create a new address (copy your `dgbt1...` address)
2. Go to **Window > Console**
3. Type: `generatetoaddress 1 dgbt1qYOURADDRESSHERE`
4. Press Enter to mine 1 block
5. Wait for 8 confirmations before spending mined coins (reduced from 100 in testnet12)

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
- `-a sha256d` - Use SHA256d algorithm (matches `algo=sha256d` in config)
- `-o http://127.0.0.1:14026` - RPC endpoint (testnet RPC port)
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
6. **Coin Control** - Use manual DD input selection for advanced redemptions

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (testnet12) |
| Default P2P Port | 12034 |
| Default RPC Port | 14026 |
| Oracle Node | oracle1.digibyte.io:12034 |
| Address Prefix | dgbt1... (bech32) |

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
| Period I | 0-66 | 72,000 DGB |
| Period IV | 67-199 | 8,000 DGB (with decay) |
| Period V | 200-399 | 2,459 DGB (with decay) |
| Period VI | 400+ | 1,078.5 DGB (with decay) |

---

## Troubleshooting

### "DigiDollar tab not appearing"
- Verify `digidollar=1` is under `[test]` section in config
- Verify `testnet=1` is at the top of config (not under any section)
- Restart the wallet after config changes

### "Not connecting to network"
- Check your firewall allows port 12034
- Verify `addnode=oracle1.digibyte.io` is under `[test]` in config

### "Oracle price shows 0 or N/A"
- Wait for sync to complete
- The oracle broadcasts price updates every few minutes
- Check Window > Console: `getoracleprice`

### "Mining not working"
- Ensure `algo=sha256d` is in your config under `[test]`
- SHA256d is recommended for fastest CPU mining

### "Transaction fee too high"
- DigiDollar transactions require 0.1 DGB minimum fee - this is expected
- This ensures reliable network propagation

### "Old testnet data causing crashes"
- Delete your testnet10 and testnet11 folders completely (see Upgrade Notes above)
- RC10 requires a fresh testnet12 blockchain

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc10-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc10-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc10-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc10-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc10-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc10-aarch64-linux-gnu.tar.gz` |

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
