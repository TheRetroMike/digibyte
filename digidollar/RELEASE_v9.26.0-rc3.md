**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im - Active development discussion happens here!

---

## What's New in RC3 (10 commits since RC2)

### Critical Bug Fixes
- **Fix DGB loss bug** - Wallet-controlled change addresses now used for all DD transactions. Previously, DGB change was sent to random keys the wallet didn't control, causing permanent loss of funds.
- **Fix consensus rejection** - Non-P2TR outputs (like P2WPKH change) are now correctly allowed in mint transactions. Previously valid mints were rejected with "bad-collateral-script".

### Network Reset
- **New testnet7** - Fresh testnet environment separate from previous testnet data
- **New P2P port 12029** - All nodes and oracles now use port 12029
- **Revert EASY POW** - Production-ready mining difficulty for realistic testing

### Test Improvements
- Enhanced Qt testnet script with comprehensive DGB/DD balance tracking
- Updated validation test expectations for new change output handling

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
testnet=1
server=1
txindex=1

[test]
digidollar=1
algo=sha256d
addnode=oracle1.digibyte.io
```

---

## Windows Setup

### Data Directory
```
%APPDATA%\DigiByte\
```
Config file: `%APPDATA%\DigiByte\digibyte.conf`

Testnet data stored in: `%APPDATA%\DigiByte\testnet7\`

### Steps:
1. Press `Win + R`, type `%APPDATA%\DigiByte` and press Enter
2. Create a new text file named `digibyte.conf` (make sure to remove `.txt` extension)
3. Paste the config contents above and save
4. Run `digibyte-qt.exe` from the extracted folder
5. The wallet will create `testnet7\` subfolder automatically

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet7]"**
- You should see a **DigiDollar** tab in the sidebar

---

## macOS Setup

### Data Directory
```
~/Library/Application Support/DigiByte/
```
Config file: `~/Library/Application Support/DigiByte/digibyte.conf`

Testnet data stored in: `~/Library/Application Support/DigiByte/testnet7/`

### Steps:
1. Open Terminal and create config:
```bash
mkdir -p ~/Library/Application\ Support/DigiByte
cat > ~/Library/Application\ Support/DigiByte/digibyte.conf << 'EOF'
testnet=1
server=1
txindex=1

[test]
digidollar=1
algo=sha256d
addnode=oracle1.digibyte.io
EOF
```

2. Extract the downloaded archive and remove quarantine:
```bash
xattr -cr ~/Downloads/digibyte-9.26.0-rc3-*-apple-darwin
```

3. Run the wallet:
```bash
# For Apple Silicon (M1/M2/M3/M4):
~/Downloads/digibyte-9.26.0-rc3-arm64-apple-darwin/bin/digibyte-qt

# For Intel Mac:
~/Downloads/digibyte-9.26.0-rc3-x86_64-apple-darwin/bin/digibyte-qt
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet7]"**
- You should see a **DigiDollar** tab in the sidebar

---

## Ubuntu/Linux Setup

### Data Directory
```
~/.digibyte/
```
Config file: `~/.digibyte/digibyte.conf`

Testnet data stored in: `~/.digibyte/testnet7/`

### Steps:
1. Open Terminal and create config:
```bash
mkdir -p ~/.digibyte
cat > ~/.digibyte/digibyte.conf << 'EOF'
testnet=1
server=1
txindex=1

[test]
digidollar=1
algo=sha256d
addnode=oracle1.digibyte.io
EOF
```

2. Extract and run:
```bash
cd ~/Downloads
tar xzf digibyte-9.26.0-rc3-x86_64-linux-gnu.tar.gz
./digibyte-9.26.0-rc3-x86_64-linux-gnu/bin/digibyte-qt
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet7]"**
- You should see a **DigiDollar** tab in the sidebar

---

## Getting Testnet DGB

Mine testnet DGB directly using the GUI console:

1. Go to the **Receive** tab and create a new address (copy your `dgbt1...` address)
2. Go to **Window > Console**
3. Type: `generatetoaddress 1 dgbt1qYOURADDRESSHERE`
4. Press Enter to mine 1 block
5. Wait for 100 confirmations before spending mined coins

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
| Network | Testnet (testnet7) |
| Default P2P Port | 12029 |
| Default RPC Port | 14024 |
| Oracle Node | oracle1.digibyte.io:12029 |
| Address Prefix | dgbt1... (bech32) |

---

## Troubleshooting

### "DigiDollar tab not appearing"
- Verify `digidollar=1` is under `[test]` section in config
- Verify `testnet=1` is at the top of config (not under any section)
- Restart the wallet after config changes

### "Not connecting to network"
- Check your firewall allows port 12029
- Verify `addnode=oracle1.digibyte.io` is under `[test]` in config

### "Oracle price shows 0 or N/A"
- Wait for sync to complete
- The oracle broadcasts price updates every few minutes
- Check Window > Console: `getoracleprice`

### "Mining not working"
- Ensure `algo=sha256d` is in your config under `[test]`
- Default algorithm is scrypt which requires more computation

---

## Downloads

| Platform | File |
|----------|------|
| Linux x86_64 | `digibyte-9.26.0-rc3-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc3-aarch64-linux-gnu.tar.gz` |
| Windows 64-bit | `digibyte-9.26.0-rc3-win64.zip` |
| macOS Intel | `digibyte-9.26.0-rc3-x86_64-apple-darwin.tar.gz` |
| macOS Apple Silicon | `digibyte-9.26.0-rc3-arm64-apple-darwin.tar.gz` |

---

## Upgrading from RC2

**Important:** RC3 uses a new testnet (testnet7) with port 12029.

1. Update your config to remove the port from addnode (12029 is now default):
   ```ini
   addnode=oracle1.digibyte.io
   ```

2. Old testnet5 data can be deleted - the new testnet7 data will be stored automatically in:
   - Windows: `%APPDATA%\DigiByte\testnet7\`
   - macOS: `~/Library/Application Support/DigiByte/testnet7/`
   - Linux: `~/.digibyte/testnet7/`

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
