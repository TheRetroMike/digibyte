**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im - Active development discussion happens here!

---

## What's New in RC11

### DigiDollar Address Book
- **DD Address Book dialog** - Store and manage frequently used DigiDollar addresses
- **Label field in DD Send** - Add labels when sending, auto-populates from address book
- **Address Book theming** - Consistent dark/light theme styling for address book UI

### DD Transaction Enhancements
- **CSV export** - Export DigiDollar transaction history to CSV files
- **Note column** - Add notes to DD transactions for personal record keeping

### Wallet Restore & Recovery
- **Automatic wallet restore** - DD balances restore automatically without manual export/import

- **Fix DD UTXO tracking** - Proper UTXO reconstruction during wallet rescan
- **Fix real-time collateral validation** - MintDD tab validates collateral amounts correctly

### Bug Fixes
- **Separate DGB fee change address** - Redemptions now use distinct address for DGB fee change
- **Improve DD address validation** - Enhanced RPC validation for DigiDollar addresses
- **Fix AddressBook race condition** - Resolved proxy model update timing issues

### Oracle Improvements
- **Oracle Phase 2 fix** - Corrected multi-oracle consensus handling

### Testing
- **Comprehensive DD widget tests** - Full Qt widget test coverage for DigiDollar UI
- **DD RPC functional tests** - Automated tests for all DigiDollar RPC commands
- **DD wallet operation tests** - End-to-end wallet operation test coverage
- **Phase 2 multi-oracle tests** - Tests for upcoming multi-oracle consensus

---

## Commits Since RC10

- Add DigiDollar Address Book dialog for managing saved addresses
- Add Label field and address book integration to DD Send widget
- Add CSV export and Note column to DD Transactions tab
- Add Address Book DD theming for consistent UI styling
- Add exportdigidollardata and importdigidollardata RPC commands
- Enable automatic wallet restore without manual export/import
- Fix DD UTXO tracking during wallet rescan
- Fix real-time collateral validation in MintDD tab
- Fix separate address for DGB fee change during redemption
- Fix Oracle Phase 2 multi-oracle consensus handling
- Improve DigiDollar address validation RPC commands
- Fix AddressBook race condition with proxy model updates
- Add comprehensive DigiDollar Qt widget tests
- Add DigiDollar RPC functional tests
- Add DigiDollar wallet operation tests
- Add Phase 2 multi-oracle consensus tests
- Update digibyte_wallet.png branding

---

## Upgrade Notes

**RC11 uses testnet12 network (port 12034). No data wipe required if upgrading from RC10.**

### If Upgrading from RC9 or Earlier:
1. Close your old wallet
2. Delete old testnet data:
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet10\` and `testnet11\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet10/` and `testnet11/`
   - **Linux:** Delete `~/.digibyte/testnet10/` and `~/.digibyte/testnet11/`
3. Download and install RC11
4. Launch with `-testnet` flag

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
Download `digibyte-9.26.0-rc11-win64-setup.exe` and install normally.

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
tar xzf digibyte-9.26.0-rc11-x86_64-linux-gnu.tar.gz
./digibyte-9.26.0-rc11/bin/digibyte-qt
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
7. **Address Book** - Save frequently used DD addresses for quick sending
8. **Export History** - Export DD transactions to CSV for record keeping

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
- RC11 uses testnet12 blockchain (same as RC10)

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc11-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc11-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc11-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc11-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc11-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc11-aarch64-linux-gnu.tar.gz` |

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
