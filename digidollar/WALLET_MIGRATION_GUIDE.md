# Oracle Wallet Migration Guide — DigiByte DigiDollar Testnet

## ARCHIVED RC30-ONLY

Do not use this guide for RC44/testnet26. Current RC44/testnet26 wallet and
oracle migration instructions live in `DIGIDOLLAR_ORACLE_SETUP.md`.

**Last Updated:** 2026-04-19
**Applies to:** historical v9.26.0-rc30 (`testnet23`) only

> **RC30 migration in one line:** copy `wallets/Oracle/` from `testnet21/` (rc28/rc29) into `testnet23/` (rc30), then patch the SQLite `application_id` from `0xFDD2B9E3` → `0xFDD2B9E4`. Everything else below explains how and why.

---

## Overview

When the historical rc30 release reset the chain from testnet21 to testnet23,
oracle wallets had to be migrated to the new testnet directory. The wallet
contains your Schnorr/MuSig2 oracle keypair. For current RC44/testnet26
instructions, use `DIGIDOLLAR_ORACLE_SETUP.md` instead.

However, simply copying `wallet.dat` into the new directory will fail due to two separate validation checks introduced in recent releases. This guide walks through every step to get your Oracle wallet working on the new chain.

---

## Prerequisites

- Linux x86_64 system
- Python 3.x installed
- Access to your old testnet wallet directory
- New DigiByte binary already downloaded and extracted
- **Do NOT delete your old testnet directory** — you need the wallet from it

---

## Step 1: Stop Your Current Node

```bash
# Stop the oracle first (if running)
digibyte-cli -datadir=/path/to/.digibyte-testnet -rpcwallet=Oracle stoporacle 0

# Stop the daemon
digibyte-cli -datadir=/path/to/.digibyte-testnet stop

# Wait for full shutdown (can take 60-90 seconds)
# Verify it's stopped:
ps aux | grep digibyted
```

---

## Step 2: Install New Binary

```bash
# Backup current binaries
cp -r /path/to/digibyte-testnet/bin /path/to/digibyte-testnet/bin.previous.bak

# Copy new binaries
cp /tmp/digibyte-NEW-VERSION/bin/digibyted /path/to/digibyte-testnet/bin/
cp /tmp/digibyte-NEW-VERSION/bin/digibyte-cli /path/to/digibyte-testnet/bin/
cp /tmp/digibyte-NEW-VERSION/bin/digibyte-tx /path/to/digibyte-testnet/bin/
cp /tmp/digibyte-NEW-VERSION/bin/digibyte-wallet /path/to/digibyte-testnet/bin/
cp /tmp/digibyte-NEW-VERSION/bin/digibyte-util /path/to/digibyte-testnet/bin/

# Verify
digibyted --version
```

---

## Step 3: Update digibyte.conf

Update your testnet config file with the new release's ports and settings. Check the release notes for port changes.

Example changes for rc30 (testnet23):

```ini
# In your digibyte.conf under [test] section:

rpcport=14026          # Unchanged since rc28
port=12030             # Was 12035 in rc28/rc29 (P2P port)
digidollar=1           # REQUIRED for oracles to run
txindex=1              # REQUIRED for DD-enabled nodes (enforced at startup)
walletcrosschain=1     # REQUIRED — allows wallet from previous chain
addnode=oracle1.digibyte.io:12030   # Update port in addnode
```

> **IMPORTANT:** The `walletcrosschain=1` setting is mandatory. Without it, the node will reject your wallet because it was last used on a different testnet chain. This is the second of two validation checks you must bypass.

---

## Step 4: Open Firewall Port

If the P2P port changed, open the new port:

```bash
sudo ufw allow NEW_PORT/tcp comment "DigiByte Testnet P2P"

# Example for rc30:
sudo ufw allow 12030/tcp comment "DigiByte Testnet P2P (rc30 testnet23)"
```

---

## Step 5: Start the New Daemon to Create the New Testnet Directory

```bash
digibyted -datadir=/path/to/.digibyte-testnet -daemon
```

Wait ~10 seconds for it to create the new directory structure (e.g., `testnet23/`):

```bash
ls /path/to/.digibyte-testnet/testnet23/
# Should show: blocks/ chainstate/ wallets/ debug.log etc.
```

Then **stop the daemon**:

```bash
digibyte-cli -datadir=/path/to/.digibyte-testnet stop

# Wait for full shutdown
sleep 15
ps aux | grep digibyted
```

---

## Step 6: Copy ONLY the Oracle Wallet

Copy **only** the Oracle wallet directory — do NOT copy any chain data (blocks, chainstate, etc.):

```bash
cp -r /path/to/.digibyte-testnet/OLD_TESTNET/wallets/Oracle \
      /path/to/.digibyte-testnet/NEW_TESTNET/wallets/Oracle

# Example for rc28/rc29 → rc30 (both ran on testnet21, rc30 resets to testnet23):
cp -r ~/.digibyte-testnet/testnet21/wallets/Oracle \
      ~/.digibyte-testnet/testnet23/wallets/Oracle
```

Verify:

```bash
ls -la /path/to/.digibyte-testnet/NEW_TESTNET/wallets/Oracle/
# Should show only: wallet.dat
```

> **WARNING:** Do NOT copy blocks/, chainstate/, or any other files from the old testnet directory. Only the `wallets/Oracle/` folder. Copying chain data will corrupt the new testnet.

---

## Step 7: Patch the Wallet application_id (CRITICAL)

This is the key step. DigiByte's wallet files are SQLite databases. Each release stamps a unique `application_id` in the SQLite file header. When the new binary sees an old ID, it rejects the file with:

```
Failed to load database path '...'. Data is not in recognized format.
```

### 7a: Find the New Expected application_id

Start the daemon, create a temporary wallet, and read its ID:

```bash
# Start daemon
digibyted -datadir=/path/to/.digibyte-testnet -daemon
sleep 15

# Create a temp wallet
digibyte-cli -datadir=/path/to/.digibyte-testnet createwallet "TempWallet"

# Read both application IDs
python3 -c "
import struct
for label, path in [
    ('NEW (expected)', '/path/to/.digibyte-testnet/NEW_TESTNET/wallets/TempWallet/wallet.dat'),
    ('OLD (your Oracle)', '/path/to/.digibyte-testnet/NEW_TESTNET/wallets/Oracle/wallet.dat'),
]:
    with open(path, 'rb') as f:
        f.seek(68)
        app_id = struct.unpack('>I', f.read(4))[0]
    print(f'{label}: 0x{app_id:08X} ({app_id})')
"

# Clean up temp wallet
digibyte-cli -datadir=/path/to/.digibyte-testnet unloadwallet "TempWallet"
rm -rf /path/to/.digibyte-testnet/NEW_TESTNET/wallets/TempWallet

# Stop daemon
digibyte-cli -datadir=/path/to/.digibyte-testnet stop
sleep 15
```

Example output (rc28/rc29 → rc30 migration):

```
NEW (expected):   0xFDD2B9E4 (4258445796)
OLD (your Oracle): 0xFDD2B9E3 (4258445795)
```

### 7b: Patch Your Oracle Wallet

Replace `NEW_APP_ID` with the hex value from the step above:

```bash
python3 -c "
import struct, shutil

wallet_path = '/path/to/.digibyte-testnet/NEW_TESTNET/wallets/Oracle/wallet.dat'

# Step 1: Create a backup
shutil.copy2(wallet_path, wallet_path + '.pre-patch.bak')
print('Backup created')

# Step 2: Read current application_id
with open(wallet_path, 'rb') as f:
    f.seek(68)
    old_id = struct.unpack('>I', f.read(4))[0]
print(f'Old application_id: 0x{old_id:08X}')

# Step 3: Write the new application_id
new_id = 0xFDD2B9E4  # <-- rc30 value. Replace with YOUR new value from step 7a if migrating to a later release.
with open(wallet_path, 'r+b') as f:
    f.seek(68)
    f.write(struct.pack('>I', new_id))
print(f'New application_id: 0x{new_id:08X}')

# Step 4: Verify the patch
with open(wallet_path, 'rb') as f:
    f.seek(68)
    verify_id = struct.unpack('>I', f.read(4))[0]
assert verify_id == new_id, 'Patch verification failed!'
print(f'Verified: 0x{verify_id:08X} -- Patch successful!')
"
```

### What This Does (Technical Explanation)

| Detail | Value |
|--------|-------|
| **File format** | SQLite 3.x database |
| **Offset** | Byte 68 in the file header |
| **Length** | 4 bytes |
| **Byte order** | Big-endian (network byte order) |
| **Purpose** | SQLite's standard `application_id` field — apps use it to identify "this database belongs to me" |
| **Why it changes** | DigiByte updates this value between releases to prevent accidentally loading incompatible wallet schemas |
| **What we change** | Only these 4 bytes — the rest of the wallet (keys, transactions, descriptors) is untouched |

The Python `struct` module handles the byte packing:
- `'>I'` = big-endian (`>`) unsigned 32-bit integer (`I`)
- `f.seek(68)` = move to byte position 68
- `f.write(struct.pack('>I', new_id))` = write exactly 4 bytes

---

## Step 8: Start Daemon and Load Wallet

```bash
# Start the daemon
digibyted -datadir=/path/to/.digibyte-testnet -daemon

# Wait for initialization
sleep 15

# Load the Oracle wallet
digibyte-cli -datadir=/path/to/.digibyte-testnet loadwallet "Oracle"
```

You should see:

```json
{
  "name": "Oracle"
}
```

If you see an error, check:
- **"Data is not in recognized format"** → Step 7 patch was not applied or used wrong ID
- **"Wallet files should not be reused across chains"** → `walletcrosschain=1` missing from config

---

## Step 9: Start the Oracle (After Block 600)

DigiDollar features activate via BIP9 signaling at block 600. Before that, oracle RPCs will return errors — this is expected.

```bash
# Check current block height
digibyte-cli -datadir=/path/to/.digibyte-testnet getblockchaininfo | head -5

# Once past block 600, start the oracle
digibyte-cli -datadir=/path/to/.digibyte-testnet -rpcwallet=Oracle startoracle 0
```

Expected output:

```json
{
  "success": true,
  "oracle_id": 0,
  "status": "running",
  "message": "Oracle started with key loaded from wallet 'Oracle'"
}
```

---

## Step 10: Verify Oracle Is Running

```bash
# Check oracle pubkey (should match your known key)
digibyte-cli -datadir=/path/to/.digibyte-testnet -rpcwallet=Oracle getoraclepubkey 0

# Check oracle status and price
digibyte-cli -datadir=/path/to/.digibyte-testnet listoracle
```

Your pubkey should be the same as before the reset. The keypair lives in the wallet — only the chain data is new.

---

## Quick Reference: Known application_id Values

| Release | Testnet dir | application_id (hex) | application_id (decimal) |
|---------|-------------|---------------------|--------------------------|
| rc27 and earlier | `testnet19/20` | `0xFCD1B8E2` | 4241602786 |
| rc28 | `testnet21`    | `0xFDD2B9E3` | 4258445795 |
| rc29 | `testnet21`    | `0xFDD2B9E3` | 4258445795 |
| rc30 historical | `testnet23` | `0xFDD2B9E4` | 4258445796 |

> **Note:** rc29 did not reset the chain — it shared testnet21 with rc28. rc30 is the first reset since rc28, which is why everyone migrating to rc30 patches `0xFDD2B9E3` → `0xFDD2B9E4`.
>
> Future releases may change this value again. Always use Step 7a to discover the correct ID from a fresh wallet created by the new binary.

---

## Troubleshooting

### "Data is not in recognized format"
The `application_id` patch was not applied or the wrong value was used. Re-run Step 7.

### "Wallet files should not be reused across chains"
Add `walletcrosschain=1` under the `[test]` section in `digibyte.conf`, then restart the daemon.

### "Oracle 0 not found. Use startoracle to initialize it first."
BIP9 hasn't activated yet. Wait until block 600+, then run `startoracle 0`.

### Oracle key doesn't match
If `getoraclepubkey` returns a different key, you may have copied the wrong wallet. Check that you're using the wallet from the correct previous testnet directory.

### Wallet backup
Your pre-patch backup is at `wallet.dat.pre-patch.bak` in the Oracle wallet directory. If anything goes wrong, restore it:

```bash
cp /path/to/wallets/Oracle/wallet.dat.pre-patch.bak /path/to/wallets/Oracle/wallet.dat
```

---

## Summary Checklist

- [ ] Stop old daemon
- [ ] Install new binaries
- [ ] Update `digibyte.conf` (ports, `walletcrosschain=1`, addnode)
- [ ] Open new P2P port in firewall
- [ ] Start new daemon to create testnet directory, then stop it
- [ ] Copy **ONLY** `wallets/Oracle/` to new testnet directory
- [ ] Patch `application_id` at byte offset 68 in `wallet.dat`
- [ ] Start daemon
- [ ] `loadwallet "Oracle"`
- [ ] Wait for block 600
- [ ] `startoracle 0`
- [ ] Verify with `getoraclepubkey` and `listoracle`
