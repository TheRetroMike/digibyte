**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im - Active development discussion happens here!

---

## What's New in RC2 (12 commits since RC1)

### Validation Fixes
- Fix "bad-redeem-dd-not-burned" error - DD input/output amounts now correctly calculated via UTXO lookup
- Add CCoinsViewCache to ValidationContext - Enables distinguishing DD tokens (nValue=0) from fee UTXOs

### Wallet Fixes
- DD key persistence - Keys now saved to wallet database and survive restarts
- UTXO scanning for received tokens - Wallet now tracks DD received from others, not just minted
- DD change output support - Partial redemptions return DD change to wallet

### RPC/Transaction Fixes
- DD redemption change tracking - Change outputs correctly tracked after partial redemption
- DD address key generation - New keys properly generated for redemption change

### Network Health
- Vault counting fix - UTXO scan now only counts MINT transactions as vaults (not sends/receives)

### Address Encoding
- Base58 network type fix - DD addresses now use correct network prefix (dgbt1 for testnet)

### Test Improvements
- Mempool sync before mining - Prevents race condition in multi-node tests
- Wallet restart persistence test - Verifies DD balances survive Qt restart
- Address prefix checks - Tests use correct regtest prefix (dgbrt1)

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

### Step 1: Download and Extract

Download the appropriate file for your platform from the Downloads section below. Extract to a location of your choice.

---

## Building From Source

If you prefer to compile DigiByte from source, follow these instructions:

### Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config \
    bsdmainutils python3 libssl-dev libevent-dev libboost-all-dev \
    libsqlite3-dev libminiupnpc-dev libnatpmp-dev libzmq3-dev \
    libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools \
    libqrencode-dev
```

**macOS (with Homebrew):**
```bash
brew install automake libtool boost pkg-config libevent qt@5 qrencode \
    miniupnpc libnatpmp zeromq sqlite
```

**Fedora/RHEL:**
```bash
sudo dnf install -y gcc-c++ libtool make autoconf automake python3 \
    openssl-devel libevent-devel boost-devel sqlite-devel \
    miniupnpc-devel libnatpmp-devel zeromq-devel qt5-qttools-devel \
    qt5-qtbase-devel qrencode-devel
```

### Clone and Build

```bash
# Clone the repository
git clone https://github.com/DigiByte-Core/digibyte.git
cd digibyte

# Checkout the RC2 tag
git checkout v9.26.0-rc2

# Generate build system
./autogen.sh

# Configure (adjust options as needed)
./configure --with-gui=qt5 --enable-wallet

# Build (use number of CPU cores for parallel build)
make -j$(nproc)

# Optional: Install system-wide
sudo make install
```

### Build Options

| Option | Description |
|--------|-------------|
| `--with-gui=qt5` | Build with Qt5 GUI (recommended) |
| `--with-gui=no` | Build without GUI (daemon only) |
| `--disable-wallet` | Build without wallet support |
| `--enable-debug` | Build with debug symbols |
| `--disable-tests` | Skip building tests |

### Verify Build

After building, verify the executable exists:
```bash
ls -la src/qt/digibyte-qt    # GUI wallet
ls -la src/digibyted         # Daemon
ls -la src/digibyte-cli      # Command-line interface
```

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
   cd C:\Users\YourName\Downloads\digibyte-9.26.0-rc2-win64\bin
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

## macOS Setup (Detailed)

### For Apple Silicon (M1/M2/M3/M4)

Use the `arm64-apple-darwin` build for native performance on Apple Silicon Macs.

### For Intel Macs

Use the `x86_64-apple-darwin` build for Intel-based Macs.

### Step 1: Create Data Directory and Config

Open Terminal (Applications > Utilities > Terminal) and run these commands:

```bash
# Create data directory (using a separate directory to avoid conflicts with mainnet)
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

### Step 2: Extract and Prepare

```bash
# Navigate to Downloads (or wherever you downloaded the file)
cd ~/Downloads

# For Apple Silicon:
tar xzf digibyte-9.26.0-rc2-arm64-apple-darwin.tar.gz

# For Intel Mac:
tar xzf digibyte-9.26.0-rc2-x86_64-apple-darwin.tar.gz

# Make executable (required on macOS)
chmod +x digibyte-9.26.0-rc2-*/bin/*
```

### Step 3: Handle macOS Security (Gatekeeper)

macOS will block unsigned applications. You have two options:

**Option A: Remove Quarantine Attribute (Recommended)**
```bash
# For Apple Silicon:
xattr -cr digibyte-9.26.0-rc2-arm64-apple-darwin

# For Intel Mac:
xattr -cr digibyte-9.26.0-rc2-x86_64-apple-darwin
```

**Option B: System Preferences**
1. Try to run the app (it will be blocked)
2. Go to **System Preferences > Security & Privacy > General**
3. Click **"Allow Anyway"** next to the blocked app message
4. Try running again and click **"Open"** in the dialog

### Step 4: Launch the Wallet

```bash
# For Apple Silicon:
~/Downloads/digibyte-9.26.0-rc2-arm64-apple-darwin/bin/digibyte-qt \
    -datadir="$HOME/Library/Application Support/DigiByte-DigiDollar"

# For Intel Mac:
~/Downloads/digibyte-9.26.0-rc2-x86_64-apple-darwin/bin/digibyte-qt \
    -datadir="$HOME/Library/Application Support/DigiByte-DigiDollar"
```

### Step 5: Create a Convenient Launch Script (Optional)

```bash
# Create a launch script
cat > ~/Desktop/DigiDollar-Testnet.command << 'EOF'
#!/bin/bash
# Launch DigiDollar Testnet Wallet

# Detect architecture and use appropriate binary
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    BINARY="$HOME/Downloads/digibyte-9.26.0-rc2-arm64-apple-darwin/bin/digibyte-qt"
else
    BINARY="$HOME/Downloads/digibyte-9.26.0-rc2-x86_64-apple-darwin/bin/digibyte-qt"
fi

# Launch with testnet data directory
"$BINARY" -datadir="$HOME/Library/Application Support/DigiByte-DigiDollar"
EOF

# Make executable
chmod +x ~/Desktop/DigiDollar-Testnet.command
```

Double-click `DigiDollar-Testnet.command` on your Desktop to launch.

### Step 6: Create an App Bundle (Advanced)

For a more native macOS experience, create an Automator application:

1. Open **Automator** (Applications > Automator)
2. Choose **"Application"** as document type
3. Drag **"Run Shell Script"** from the library to the workflow
4. Set **Shell** to `/bin/bash`
5. Paste:
```bash
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    "$HOME/Downloads/digibyte-9.26.0-rc2-arm64-apple-darwin/bin/digibyte-qt" \
        -datadir="$HOME/Library/Application Support/DigiByte-DigiDollar"
else
    "$HOME/Downloads/digibyte-9.26.0-rc2-x86_64-apple-darwin/bin/digibyte-qt" \
        -datadir="$HOME/Library/Application Support/DigiByte-DigiDollar"
fi
```
6. Save as **"DigiDollar Testnet"** in Applications folder

### Verify macOS Setup

When the wallet opens:
- The title bar should say **"DigiByte Core - Wallet [testnet5]"**
- You should see a **DigiDollar** tab in the sidebar
- Check **Window > Console** and type `getdigidollarstats` to verify DigiDollar is active

**Common macOS Issues:**
- **"App is damaged and can't be opened"** - Run `xattr -cr` on the extracted folder
- **"App can't be opened because it is from an unidentified developer"** - Use System Preferences to allow
- **No DigiDollar tab** - Check that `digidollar=1` is under `[test]` in your config file
- **Can't connect to network** - Verify firewall allows incoming connections on port 12028

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
cd ~/Downloads/digibyte-9.26.0-rc2-x86_64-linux-gnu/bin

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

## Raspberry Pi / ARM64 Setup

For Raspberry Pi 4/5 or other ARM64 Linux devices:

```bash
# Download ARM64 build
wget https://github.com/DigiByte-Core/digibyte/releases/download/v9.26.0-rc2/digibyte-9.26.0-rc2-aarch64-linux-gnu.tar.gz

# Extract
tar xzf digibyte-9.26.0-rc2-aarch64-linux-gnu.tar.gz

# Create config (same as Linux)
mkdir -p ~/.digibyte-digidollar
cat > ~/.digibyte-digidollar/digibyte.conf << 'EOF'
testnet=1
server=1
txindex=1

[test]
digidollar=1
addnode=oracle1.digibyte.io
EOF

# Run (daemon mode recommended for Pi)
./digibyte-9.26.0-rc2-aarch64-linux-gnu/bin/digibyted -datadir=~/.digibyte-digidollar -daemon

# Check status
./digibyte-9.26.0-rc2-aarch64-linux-gnu/bin/digibyte-cli -datadir=~/.digibyte-digidollar getblockchaininfo
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

### "macOS: App is damaged and can't be opened"
- Run `xattr -cr` on the extracted folder to remove quarantine
- Or allow in System Preferences > Security & Privacy

### "Redemption fails with validation error"
- Ensure you have waited for the time-lock period to expire
- Check that you have sufficient DD balance
- Verify the vault position exists: `listdigidollarpositions`

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
| Linux x86_64 | `digibyte-9.26.0-rc2-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc2-aarch64-linux-gnu.tar.gz` |
| Windows 64-bit | `digibyte-9.26.0-rc2-win64.zip` |
| macOS Intel | `digibyte-9.26.0-rc2-x86_64-apple-darwin.tar.gz` |
| macOS Apple Silicon | `digibyte-9.26.0-rc2-arm64-apple-darwin.tar.gz` |

---

## Full Changelog from RC1 (12 commits)

| Commit | Description |
|--------|-------------|
| `580248761f` | test: Fix digidollar_redeem.py timing and robustness issues |
| `22cad82a59` | txbuilder: Document DD change output metadata handling |
| `f0dae7ed9d` | validation: Fix DD redemption input/output amount calculation |
| `975dc0d03a` | validation: Add CCoinsViewCache to DD ValidationContext for UTXO lookup |
| `e0e23b62e3` | test: Fix DD address prefix check for regtest network |
| `25dfc16654` | test: Add wallet restart persistence test to Qt testnet script |
| `00618b3842` | rpc: Fix DD redemption change tracking and address key generation |
| `089d699253` | health: Filter UTXO scan to only count MINT transactions as vaults |
| `0de593bb66` | wallet: Add DD key persistence and fix UTXO scanning for received tokens |
| `8e4e7d3601` | txbuilder: Implement DD change outputs for fungible redemption |
| `d9e6721c8a` | wallet: Add DD key persistence to wallet database |
| `146f829209` | base58: Fix DD address encoding to use correct network type |

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
