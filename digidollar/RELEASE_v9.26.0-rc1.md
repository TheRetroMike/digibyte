**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im - Active development discussion happens here!

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

### Step 1: Download and Extract

Download the appropriate file for your platform from the Downloads section below. Extract to a location of your choice.

---

## Windows Setup (Detailed)

### Step 1: Create Data Directory

1. Press `Win + R` to open Run dialog
2. Type `%USERPROFILE%` and press Enter
3. Create a new folder called `DigiByte-DigiDollar`
4. Inside that folder, create a new text file

### Step 2: Create Config File

1. Inside `DigiByte-DigiDollar`, right-click and select **New > Text Document**
2. Name it exactly: `digibyte.conf` (make sure to remove `.txt` extension)
3. If you can't see file extensions: In File Explorer, click **View** > check **File name extensions**
4. Right-click `digibyte.conf` and select **Edit** (or open with Notepad)
5. Paste this exact content:

```ini
# DigiDollar Testnet Configuration
testnet=1
server=1
txindex=1

[test]
digidollar=1
addnode=oracle1.digibyte.io
```

6. Save and close Notepad

### Step 3: Create Shortcut to Launch

**Option A: Command Prompt (Recommended for troubleshooting)**
1. Open Command Prompt (search "cmd" in Start menu)
2. Navigate to where you extracted the files, for example:
   ```
   cd C:\Users\YourName\Downloads\digibyte-9.26.0-rc1-win64\bin
   ```
3. Run:
   ```
   digibyte-qt.exe -datadir=%USERPROFILE%\DigiByte-DigiDollar
   ```

**Option B: Create Desktop Shortcut**
1. Right-click on `digibyte-qt.exe` and select **Create shortcut**
2. Right-click the new shortcut and select **Properties**
3. In the **Target** field, add at the end (after the quotes):
   ```
    -datadir=%USERPROFILE%\DigiByte-DigiDollar
   ```
   So it looks like: `"C:\...\digibyte-qt.exe" -datadir=%USERPROFILE%\DigiByte-DigiDollar`
4. Click **OK**
5. Double-click the shortcut to launch

### Step 4: Verify It's Working

When the wallet opens:
- The title bar should say **"DigiByte Core - Wallet [testnet5]"**
- You should see a **DigiDollar** tab in the sidebar
- The network should start syncing (may take a few minutes)

**Common Windows Issues:**
- If you see "testnet" in the title bar but no DigiDollar tab, your config file is missing `digidollar=1` under `[test]`
- If Windows Security blocks the program, click "More info" then "Run anyway"
- If the wallet crashes on startup, make sure the data directory exists and the config file is valid

---

## macOS Setup

### Step 1: Create Data Directory and Config

Open Terminal (Applications > Utilities > Terminal) and run these commands:

```bash
# Create data directory
mkdir -p ~/Library/Application\ Support/DigiByte-DigiDollar

# Create config file
cat > ~/Library/Application\ Support/DigiByte-DigiDollar/digibyte.conf << 'EOF'
# DigiDollar Testnet Configuration
testnet=1
server=1
txindex=1

[test]
digidollar=1
addnode=oracle1.digibyte.io
EOF
```

### Step 2: Launch the Wallet

**Option A: From Terminal (Recommended)**
```bash
# Navigate to extracted folder (adjust path as needed)
cd ~/Downloads/digibyte-9.26.0-rc1-x86_64-apple-darwin/bin

# For Intel Mac:
./digibyte-qt -datadir="$HOME/Library/Application Support/DigiByte-DigiDollar"

# For Apple Silicon (M1/M2/M3):
# Use the arm64 build instead
```

**Option B: Create an Automator App**
1. Open Automator (Applications > Automator)
2. Choose "Application"
3. Add "Run Shell Script" action
4. Paste:
   ```bash
   /path/to/digibyte-qt -datadir="$HOME/Library/Application Support/DigiByte-DigiDollar"
   ```
5. Save as "DigiDollar Testnet.app"

**macOS Security Note:** You may need to allow the app in System Preferences > Security & Privacy after first launch attempt.

---

## Linux Setup

### Step 1: Create Data Directory and Config

Open a terminal and run:

```bash
# Create data directory
mkdir -p ~/.digibyte-digidollar

# Create config file
cat > ~/.digibyte-digidollar/digibyte.conf << 'EOF'
# DigiDollar Testnet Configuration
testnet=1
server=1
txindex=1

[test]
digidollar=1
addnode=oracle1.digibyte.io
EOF
```

### Step 2: Launch the Wallet

```bash
# Navigate to extracted folder (adjust path as needed)
cd ~/Downloads/digibyte-9.26.0-rc1-x86_64-linux-gnu/bin

# Make executable and run
chmod +x digibyte-qt
./digibyte-qt -datadir=~/.digibyte-digidollar
```

**Optional: Create Desktop Launcher**
```bash
cat > ~/.local/share/applications/digidollar-testnet.desktop << 'EOF'
[Desktop Entry]
Name=DigiDollar Testnet
Exec=/path/to/digibyte-qt -datadir=%h/.digibyte-digidollar
Icon=digibyte
Type=Application
Categories=Finance;
EOF
```

---

## Understanding the Config File

The config file uses sections for network-specific settings:

```ini
# Global settings (apply to all networks)
testnet=1          # Enables testnet mode
server=1           # Enables RPC server
txindex=1          # Full transaction index (required for DigiDollar)

# Testnet-specific settings (only apply in testnet mode)
[test]
digidollar=1                      # Enable DigiDollar features
addnode=oracle1.digibyte.io       # Connect to oracle node
debug=digidollar                  # Optional: Enable debug logging
```

**Important:** The `digidollar=1` MUST be under `[test]` section, not at the top of the file. The `testnet=1` enables testnet mode, but `digidollar=1` under `[test]` enables DigiDollar features specifically for testnet.

---

## Getting Testnet DGB

Since no faucet is available yet, you'll mine testnet DGB directly. This only works on testnet.

### Method 1: GUI Console (Easiest)

1. In DigiByte-Qt, go to the **Receive** tab
2. Click **Create new receiving address** and copy your `dgbt1...` address
3. Go to **Window > Console**
4. Type: `generatetoaddress 1 dgbt1qYOURADDRESSHERE`
5. Press Enter - this mines 1 block
6. Repeat or wait for 100 block confirmations before spending

### Method 2: Command Line

**Windows:**
```cmd
digibyte-cli.exe -datadir=%USERPROFILE%\DigiByte-DigiDollar generatetoaddress 1 dgbt1qYOURADDRESSHERE
```

**macOS:**
```bash
./digibyte-cli -datadir="$HOME/Library/Application Support/DigiByte-DigiDollar" generatetoaddress 1 dgbt1qYOURADDRESSHERE
```

**Linux:**
```bash
./digibyte-cli -datadir=~/.digibyte-digidollar generatetoaddress 1 dgbt1qYOURADDRESSHERE
```

**Note:** Mined coins require 100 block confirmations before spending.

---

## Testing DigiDollar Features

Once your wallet is synced and you have testnet DGB:

1. **View Network Status** - The DigiDollar Overview tab shows oracle price and network collateralization
2. **Mint DUSD** - Lock DGB as collateral to create DigiDollars
3. **Send DUSD** - Transfer DigiDollars to other testnet addresses
4. **Redeem DUSD** - Burn DigiDollars to unlock your DGB collateral

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (testnet5) |
| Default P2P Port | 12028 |
| Default RPC Port | 14024 |
| Oracle Node | oracle1.digibyte.io:12028 |
| Address Prefix | dgbt1... (bech32) |

---

## Troubleshooting

### "DigiDollar tab not appearing"
- Verify `digidollar=1` is under `[test]` section in config
- Verify `testnet=1` is in config (not under any section)
- Restart the wallet after config changes

### "Not connecting to network"
- Check your firewall allows port 12028
- Add `addnode=oracle1.digibyte.io` to config
- Verify internet connection

### "Oracle price shows 0 or N/A"
- Wait for sync to complete
- The oracle broadcasts price updates every few minutes
- Check Window > Console: `getoracleprice`

### "Windows: Config file not found"
- Make sure the file is named `digibyte.conf` not `digibyte.conf.txt`
- Enable "File name extensions" in File Explorer to see real extensions
- Config must be in the data directory, not the program folder

---

## Documentation

| Document | Description |
|----------|-------------|
| [DIGIDOLLAR_EXPLAINER.md](DIGIDOLLAR_EXPLAINER.md) | High-level overview of DigiDollar for users |
| [DIGIDOLLAR_ARCHITECTURE.md](DIGIDOLLAR_ARCHITECTURE.md) | Technical architecture and implementation details |
| [DIGIDOLLAR_ORACLE_EXPLAINER.md](DIGIDOLLAR_ORACLE_EXPLAINER.md) | How the oracle price feed system works |
| [DIGIDOLLAR_ORACLE_ARCHITECTURE.md](DIGIDOLLAR_ORACLE_ARCHITECTURE.md) | Oracle network technical specification |

---

## Downloads

| Platform | File |
|----------|------|
| Linux x86_64 | `digibyte-9.26.0-rc1-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc1-aarch64-linux-gnu.tar.gz` |
| Windows 64-bit | `digibyte-9.26.0-rc1-win64.zip` |
| macOS Intel | `digibyte-9.26.0-rc1-x86_64-apple-darwin.tar.gz` |
| macOS Apple Silicon | `digibyte-9.26.0-rc1-arm64-apple-darwin.tar.gz` |

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues

