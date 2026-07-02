**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im - Active development discussion happens here!

---

## What's New in RC4 (34 commits since RC3)

### Network Reset - testnet8
- **New testnet8** - Fresh testnet environment with clean blockchain state
- **New P2P port 12030** - All nodes and oracles now use port 12030
- **Difficulty retargeting enabled** - Production-ready mining difficulty on testnet

### Critical Bug Fixes
- **Fix Qt freeze** - Removed verbose per-UTXO logging that caused GUI to hang when loading large wallets
- **Fix large DD amounts** - Increased CScriptNum max size to 8 bytes to handle large DigiDollar values correctly
- **Fix DD amount parsing** - Wallet now correctly parses large DigiDollar amounts
- **Fix collateral calculation** - Consistent collateral calculation between GUI and backend
- **Auto-rebuild indexes** - Blockchain indexes automatically rebuild on chain mismatch (prevents corruption)

### New Features
- **DigiDollarStatsIndex** - New aggregate statistics index for network-wide DD metrics
- **Lock tier tracking** - Wallet DDTransaction struct now tracks lock tier information
- **DD Transactions tab** - New tab showing full DigiDollar transaction history
- **Human-readable lock periods** - Lock periods now display as "3 months", "6 months", etc. in GUI
- **ERR tier info** - getdigidollarstats RPC now includes Emergency Reserve Ratio tier information
- **Integrated transaction list** - DigiDollar transactions now appear in main wallet transaction list

### Redemption Model Changes
- **Exact-amount redemptions only** - Partial redemptions no longer allowed; must redeem full collateral position
- **Updated redeem widget** - GUI redesigned to support exact-amount redemption model

### Fee Rate Improvements
- **Increased minimum fee** - DigiDollar transactions now require 0.1 DGB minimum fee for reliable network relay
- **Updated fee rate** - MIN_DD_FEE_RATE set to 35,000,000 sat/kB to ensure propagation

### Test Improvements
- **Comprehensive v8 test script** - New Qt testnet test script with stats index support
- **Updated unit tests** - All 670 DigiDollar unit tests pass with new fee requirements
- **Updated functional tests** - All 11 DigiDollar functional tests pass
- **Qt test stability** - Improved RPC wait and batch block generation for reliable testing

### UI/Theme Fixes
- **Dark/light theme compatibility** - DD Transactions tab now works correctly in both themes
- **Complete CSS styling** - Full styling for DD Transactions widget

### Code Refactoring
- **DCA stub implementations** - DCA functions changed to stub implementations for cleaner architecture

---

## Key Features Since RC1 (Cumulative)

These critical fixes were introduced in earlier RC versions but are essential to understand what makes RC4 stable:

### From RC2 - Core Transaction Fixes
- **DD key persistence** - DigiDollar keys now persist across wallet restarts (was causing "missing or spent" errors)
- **Taproot signing fixed** - DD transfers and redemptions now sign correctly with Taproot keys
- **UTXO scanning fixed** - Wallet correctly scans and tracks received DD tokens
- **DD redemption change tracking** - Proper address key generation for redemption change outputs
- **ValidationContext with CCoinsViewCache** - Proper UTXO lookup during DD transaction validation

### From RC3 - Wallet Safety Fixes
- **DGB loss bug fixed** - Wallet-controlled change addresses now used for ALL DD transactions (previously DGB change was sent to random keys the wallet didn't control, causing permanent fund loss)
- **Non-P2TR outputs allowed** - P2WPKH change outputs now correctly accepted in mint transactions (was rejected with "bad-collateral-script")
- **Windows 64-bit integer fixes** - Fixed LLP64 integer truncation for DD/collateral values on Windows

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

Testnet data stored in: `%APPDATA%\DigiByte\testnet8\`

### Steps:
1. Press `Win + R`, type `%APPDATA%\DigiByte` and press Enter
2. Create a new text file named `digibyte.conf` (make sure to remove `.txt` extension)
3. Paste the config contents above and save
4. Run `digibyte-qt.exe` from the extracted folder
5. The wallet will create `testnet8\` subfolder automatically

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet8]"**
- You should see a **DigiDollar** tab in the sidebar

---

## macOS Setup

### Data Directory
```
~/Library/Application Support/DigiByte/
```
Config file: `~/Library/Application Support/DigiByte/digibyte.conf`

Testnet data stored in: `~/Library/Application Support/DigiByte/testnet8/`

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
xattr -cr ~/Downloads/digibyte-9.26.0-rc4-*-apple-darwin
```

3. Run the wallet:
```bash
# For Apple Silicon (M1/M2/M3/M4):
~/Downloads/digibyte-9.26.0-rc4-arm64-apple-darwin/bin/digibyte-qt

# For Intel Mac:
~/Downloads/digibyte-9.26.0-rc4-x86_64-apple-darwin/bin/digibyte-qt
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet8]"**
- You should see a **DigiDollar** tab in the sidebar

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
tar xzf digibyte-9.26.0-rc4-x86_64-linux-gnu.tar.gz
./digibyte-9.26.0-rc4-x86_64-linux-gnu/bin/digibyte-qt
```

### Verify It's Working
- Title bar should say **"DigiByte Core - Wallet [testnet8]"**
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
4. **Redeem DUSD** - Burn DigiDollars to unlock your DGB collateral (full position only)
5. **View History** - New DD Transactions tab shows complete transaction history

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (testnet8) |
| Default P2P Port | 12030 |
| Default RPC Port | 14024 |
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
| Linux x86_64 | `digibyte-9.26.0-rc4-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc4-aarch64-linux-gnu.tar.gz` |
| Windows 64-bit | `digibyte-9.26.0-rc4-win64.zip` |
| macOS Intel | `digibyte-9.26.0-rc4-x86_64-apple-darwin.tar.gz` |
| macOS Apple Silicon | `digibyte-9.26.0-rc4-arm64-apple-darwin.tar.gz` |

---

## Upgrading from RC3

**Important:** RC4 uses a new testnet (testnet8) with port 12030.

1. Update your config - the port is now 12030 (default, no need to specify):
   ```ini
   addnode=oracle1.digibyte.io
   ```

2. Old testnet7 data can be deleted - the new testnet8 data will be stored automatically in:
   - Windows: `%APPDATA%\DigiByte\testnet8\`
   - macOS: `~/Library/Application Support/DigiByte/testnet8/`
   - Linux: `~/.digibyte/testnet8/`

3. **Breaking change:** Partial redemptions are no longer supported. You must redeem your full collateral position at once.

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
