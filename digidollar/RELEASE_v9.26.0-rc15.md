# DigiByte v9.26.0-rc15 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## What's New in RC15

RC15 is a major stability and security release with **1 critical security fix, 5 security hardening improvements, 10 bug fixes, and 2 quality-of-life improvements** since RC14.

---

### 🔴 Critical Security Fix

**Your locked DGB collateral is now truly locked.**

Previously, someone who minted DigiDollars could immediately withdraw the DGB they locked as collateral — before the timelock expired. This would create DigiDollars backed by nothing. We fixed this by making the collateral output mathematically impossible to spend via the fast path. All spending must now go through the timelock script. This is the single most important fix in RC15.

---

### 🔒 Security Hardening (5 fixes)

**1. Oracle price messages are now verified against authorized keys.**
Before, anyone could broadcast fake oracle price messages to the network because the signature was checked against whatever key was in the message itself — not against the real authorized oracle keys. Now every oracle message is verified against the keys hardcoded in the software.

**2. Encrypted connections to price feeds can no longer silently fall back to unencrypted.**
If the oracle couldn't find SSL certificates on the system, it would quietly disable encryption and fetch prices over plain HTTP. An attacker on the network could intercept and modify prices. That fallback is gone — if encryption fails, the fetch fails. We also added limits on redirects, file sizes, and timeouts.

**3. Your collateral can no longer be accidentally unlocked.**
The "unlock all coins" function and the `lockunspent` RPC command could accidentally unlock DGB that was locked as DigiDollar collateral. If you then spent that DGB, your DigiDollars would become unbacked. DD-locked coins are now permanently protected from these commands.

**4. Validation no longer silently skips checks when data is missing.**
Three places in the code would assume a DigiDollar transaction was valid if they couldn't look up the data to verify it. Now they reject the transaction instead. This prevents invalid transactions from sneaking through on nodes with incomplete data.

**5. Collateral is re-locked after blockchain reorganizations.**
If the network experienced a chain reorganization that reversed a DD redemption, the collateral was left unlocked — creating a brief window where it could be spent. Now it's immediately re-locked when a block is disconnected.

---

### 🐛 Bug Fixes (10 fixes)

**1. Self-minted DigiDollars no longer disappear after wallet rescan.** *(Reported by shenger)*
If you deleted your testnet data and resynced, or loaded your wallet after syncing, all DigiDollars you minted yourself would vanish from your balance. The wallet couldn't recognize its own DD outputs during rescan. Fixed — your minted DD now survives resync.

**2. Nodes no longer need `-txindex` to validate DigiDollar transactions.**
Without the transaction index enabled, nodes could reject perfectly valid blocks containing DD redemptions — potentially causing the network to split. Now uses a direct block database lookup that works on every node, no special configuration needed.

**3. Oracle price no longer shows "stale" when oracles are actively reporting.**
The `getoracleprice` command was incorrectly reporting the price as stale during the first epoch, even though oracles were actively submitting fresh prices. Fixed to use the correct reference point.

**4. Fixed 3 test failures from the security fixes.**
The NUMS collateral key change broke some signing paths and price unit comparisons in tests. All resolved.

**5. DD amount lookups now use the most reliable data source first.**
Previously, the code could use stale cached data instead of the authoritative blockchain data, causing edge-case validation failures.

**6. Windows oracles no longer lose network connectivity after 6–24 hours.** *(Reported by DanGB)*
The price fetcher was creating and destroying a network connection for every single HTTP request — about 40,000 times per day. On Windows, this exhausted available network sockets. Now reuses a single persistent connection. Also fixed a memory leak.

**7. Freshly minted DigiDollars no longer appear spendable before confirmation.** *(Reported by shenger)*
Right after minting DD, the full amount appeared in your balance immediately. If you tried to send it before it confirmed, the transaction would fail. Now minted DD only appears in your balance after at least 1 confirmation. Your own transfer change is still immediately available.

**8. You can now send multiple DD transactions in rapid succession.** *(Reported by Bastian)*
Sending 5 DD transfers in a row would fail after about 4 sends. Two causes: the wallet wouldn't reuse its own unconfirmed DGB change for fees, and DD change from your own transfers was incorrectly filtered out. Both fixed — you can now chain as many sends as you want.

**9. All DigiDollar and Oracle commands now appear in Help → Command-line options.** *(Reported by shenger)*
The Help dialog was missing all DD and Oracle RPC commands. Now shows all 17 DigiDollar commands and 9 Oracle commands with descriptions. Also fixed the dialog layout — the left panel was rendering as an empty box.

**10. Removed CoinMarketCap price fetcher.**
CoinMarketCap requires a paid API key, which is fundamentally incompatible with a decentralized oracle protocol. You can't hardcode a shared API key in open source software. Removed entirely. Oracles now use 6 free public exchange APIs: Binance, CoinGecko, KuCoin, Gate.io, HTX, and Crypto.com.

---

### 🔑 Oracle Key Updates

**Aussie Epic (Oracle 6)** — New pubkey after RC13 wallet was corrupted by a folder rename. Old key no longer accessible. New key is active in this release.

---

## Technical Changes

| File | Change |
|------|--------|
| `configure.ac` | Version bump RC14 → RC15 |
| `src/kernel/chainparams.cpp` | Updated Aussie Epic (Oracle 6) pubkey |
| `src/oracle/exchange.cpp` | Persistent CURL handle reuse (fix socket exhaustion); removed CoinMarketCap fetcher; hardened TLS (no fallback to unencrypted) |
| `src/oracle/exchange.h` | Added `void* m_curl_handle` member for persistent connection; removed CoinMarketCapFetcher class |
| `src/oracle/node.cpp` | Bind oracle pubkey verification against chainparams authorized keys |
| `src/wallet/digidollarwallet.cpp` | Fix wallet rescan for self-minted DD; exclude unconfirmed DD from balance (using CachedTxIsTrusted); allow rapid consecutive transfers (fee change chaining + trusted DD change) |
| `src/wallet/spend.cpp` | Allow unconfirmed DGB change for DD fee selection (`m_include_unsafe_inputs = true`) |
| `src/consensus/tx_verify.cpp` | Reject DD transactions when validation data unavailable (no silent bypass) |
| `src/script/digidollar.cpp` | NUMS point for collateral Taproot internal key (prevents key-path spend) |
| `src/rpc/digidollar.cpp` | Re-lock DD collateral on block disconnection; protect DD locks from UnlockAllCoins/lockunspent |
| `src/init.cpp` | Register all 17 DD + 9 Oracle RPC commands in help; add DIGIDOLLAR and ORACLE option categories; remove coinmarketcap-api-key |
| `src/qt/utilitydialog.cpp` | Fix Help dialog parser for non-hyphenated commands; collapse empty left pane |
| `src/qt/res/icons/digibyte_wallet.png` | Update wallet splash to RC15 |
| `src/test/oracle_exchange_tests.cpp` | New CURL handle reuse tests; replace CMC test with CoinGecko |
| `src/test/digidollar_rpc_tests.cpp` | Fix test failures from NUMS key and price unit changes |
| `test/functional/digidollar_network_relay.py` | Fix getrawtransaction to work without -txindex |

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up control of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Wallet GUI Guide

### How to Use DigiDollar in the Wallet

1. **Open the DigiDollar Tab** — Click "DigiDollar" in the top navigation bar
2. **Mint DigiDollars** — Lock DGB as collateral to create DUSD. Enter the amount and confirm. Your DGB remains locked for the duration of the lock tier you select.
3. **Send DigiDollars** — Click "Send DGB" but use a DD address (starts with `dgbt1...`). Or use the DigiDollar tab's send function.
4. **Receive DigiDollars** — Click "Receive DGB" to get your DD-capable address, or use the DigiDollar tab.
5. **View History** — DD Transactions tab shows complete transaction history (auto-refreshes)
6. **Redeem DigiDollars** — Burn DUSD to unlock your DGB collateral after the lock period expires
7. **Coin Control** — Use manual DD input selection for advanced redemptions
8. **Address Book** — Save frequently used DD addresses for quick sending
9. **Export History** — Export DD transactions to CSV for record keeping

---

## Oracle Operator Setup

Want to run an oracle node? Here's the simple version:

### Prerequisites
- DigiByte Core RC15 built from source with curl support (or download the binary)
- An assigned oracle ID (0–7 for testnet, contact the maintainer)

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

> **⚠️ After restarting `digibyted`, you must run `startoracle` again.** The key persists in the wallet, but the oracle thread does not auto-start.

For the complete guide, see **`DIGIDOLLAR_ORACLE_SETUP.md`**.

### Current Oracle Operators (Testnet)

| ID | Operator | Status |
|----|----------|--------|
| 0 | Jared | ✅ Active |
| 1 | Green Candle | ✅ Active |
| 2 | Bastian | ✅ Active |
| 3 | DanGB | ✅ Active |
| 4 | Shenger | ✅ Active |
| 5 | Ycagel | ✅ Active |
| 6 | Aussie Epic | 🔑 New key in RC15 |
| 7 | LookIntoMyEyes | ✅ Active |

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
| `createoraclekey <id>` | Generate a new oracle Schnorr keypair in your wallet (one-time) |
| `getoraclepubkey <id>` | Show the oracle public key stored in your wallet |
| `startoracle <id>` | Start running as an oracle operator (must re-run after restart) |
| `stoporacle <id>` | Stop your oracle |
| `getoracleprice` | Get the consensus price DigiDollar uses for minting/redemption |
| `getalloracleprices` | Per-oracle breakdown: price, deviation, signature validity |
| `getoracles` | Network-wide view of all oracle operators and their status |
| `listoracle` | Show your local oracle status |
| `sendoracleprice` | Manually submit a price (testing/debugging) |

### Usage Examples
```bash
# Check the consensus price DigiDollar uses
digibyte-cli -testnet getoracleprice

# View all oracle operators network-wide
digibyte-cli -testnet getoracles

# See what each oracle reported (forensics)
digibyte-cli -testnet getalloracleprices

# Check your DD balance
digibyte-cli -testnet -rpcwallet=default getdigidollarbalance

# Mint 10 DUSD
digibyte-cli -testnet -rpcwallet=default mintdigidollar 1000

# Send 5 DUSD to someone
digibyte-cli -testnet -rpcwallet=default senddigidollar dgbt1... 500
```

---

## Commits Since RC14

### Security
```
3a5101133c security: use NUMS point for collateral Taproot internal key (CVE-grade)
a55ff1a54b security: bind oracle pubkey from chainparams before P2P signature verification
d2be3ccbaa security: remove TLS fallback in exchange fetcher, harden curl settings
cc21f0063d security: reject DD transactions when validation data unavailable instead of bypassing
f659604ce0 security: protect DigiDollar locks from UnlockAllCoins and lockunspent RPC
3559008524 security: re-lock DD collateral on block disconnection (reorg handler)
```

### Bug Fixes
```
91c854d08c fix: wallet rescan missing self-minted DigiDollars on fresh sync
a5a752ac29 fix: universal DD amount extraction via block database lookup
668a6696cd fix: universal DD amount extraction via block database lookup
e1df0849f2 fix(oracle): use current height for staleness when pending messages exist
1411390658 fix: resolve 3 root causes of DigiDollar test failures
704e4fce72 fix: oracle_p2p_tests use wrong chainparams (mainnet vs regtest)
ab0b8fd60c fix: reuse persistent CURL handle to prevent Windows socket exhaustion
2d1032b456 fix: exclude unconfirmed DD UTXOs from spendable balance
c2408fcc5f fix: allow rapid consecutive DD transfers (fee + change chaining)
44b86e9c7a fix: digidollar_network_relay.py use block hash for getrawtransaction
```

### Improvements
```
918c809572 remove: CoinMarketCap fetcher (paid API key incompatible with decentralized oracle)
49315c6218 fix: add DigiDollar and Oracle categories to command-line help
7b909ef23d fix: add all DD/Oracle RPC commands to Help dialog, fix empty left pane
d0c9f008e5 update: Aussie Epic oracle 6 pubkey for RC15
ffa46cbb0d release: bump version to v9.26.0-rc15
```

### Docs
```
d689a9afdc docs: update RC15 release notes with bug fixes and CMC removal
```

---

## Upgrade Notes

**RC15 uses the same testnet13 network as RC12–RC14 (port 12030). Your existing testnet data and wallets will work — no migration needed.**

If you're upgrading from RC14, simply replace the binaries and restart.

### If Upgrading from RC11 or Earlier:
1. Close your old wallet
2. Delete old testnet data:
   - **Windows:** Delete `%APPDATA%\DigiByte\testnet10\` and `testnet11\`
   - **macOS:** Delete `~/Library/Application Support/DigiByte/testnet10/` and `testnet11/`
   - **Linux:** Delete `~/.digibyte/testnet10/` and `~/.digibyte/testnet11/`
3. Download and install RC15
4. Launch with `-testnet` flag

**Aussie Epic (Oracle 6):** Your new key is active in this release. After upgrading, run `startoracle 6` to begin reporting prices.

**Important:** DigiDollars minted on RC15 use a new collateral format (NUMS internal key). Existing mints from RC14 and earlier are unaffected and continue to work normally.

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
# Enable transaction index (useful for debugging, not required)
txindex=1

# Enable DigiDollar stats index for network-wide supply tracking
digidollarstatsindex=1

# For mining (SHA256d recommended for fastest CPU mining)
algo=sha256d
```

---

## Known Issues

- Fixed testnet mining difficulty causes slower-than-normal block times on some algorithms
- `startoracle` must be re-run after every `digibyted` restart

---

## Testing

All tests validated:
- ✅ 1,501 / 1,501 C++ unit tests pass (zero failures)
- ✅ 8 / 8 DigiDollar functional tests pass

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
| Oracle Consensus | 5-of-8 Schnorr threshold |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

### Oracle Operators (Testnet)

| ID | Name | Public Key (X-only, first 16 hex) |
|----|------|-----------------------------------|
| 0 | Jared | Lead maintainer |
| 1 | Green Candle | Community |
| 2 | Bastian | Community |
| 3 | DanGB | Community |
| 4 | Shenger | Community |
| 5 | Ycagel | Community |
| 6 | Aussie Epic | Community (new key in RC15) |
| 7 | LookIntoMyEyes | Community |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc15-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc15-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc15-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc15-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc15-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc15-aarch64-linux-gnu.tar.gz` |

---

## Quick Start

### New to DigiDollar?

1. Download the binary for your platform (see Downloads above)
2. Create your config file (see Configuration section)
3. Launch with `-testnet` flag: `./digibyte-qt -testnet`
4. Wait for the blockchain to sync (should be quick on testnet)
5. Once synced, the DigiDollar tab will appear
6. You need DGB to mint — ask in Gitter and someone can send you testnet DGB

### Want to Run an Oracle?

See the **Oracle Operator Setup** section above, or read `DIGIDOLLAR_ORACLE_SETUP.md` for the complete guide.

---

## Troubleshooting

### "Self-minted DigiDollars not showing"
- **If you deleted testnet13 and resynced:** This was fixed in RC15. Update to RC15 and resync — your self-minted DDs will now appear correctly.
- **If balances appear in DD-Transaction/DD-Vault but not in the main overview:** Same issue, fixed in RC15.

### "Failed to create or send the transaction" when sending multiple DDs
- This was fixed in RC15. Update and retry. You can now chain unlimited consecutive sends.

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

### "Transaction stuck / unconfirmed"
- **If you upgraded from RC13:** This was a known bug fixed in RC14 where minting DD could cause subsequent DGB sends to get stuck. After upgrading, your wallet will automatically re-lock the correct UTXOs on startup.
- If still stuck, try: `abandontransaction <txid>` in the console

### "No wallet is loaded" when running oracle commands
- Add `-rpcwallet=oracle` to your `createoraclekey` and `startoracle` commands

### "Oracle not configured" from `startoracle`
- Run `createoraclekey` first to generate and store the key in your wallet

### "Mining not working"
- Ensure `algo=sha256d` is in your config under `[test]`
- SHA256d is recommended for fastest CPU mining

### "Old testnet data causing crashes"
- Delete your testnet10 and testnet11 folders completely (see Upgrade Notes above)
- RC15 uses testnet13 blockchain

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

When reporting bugs, start your message with **BUG:** and include: what happened, steps to reproduce, platform (Windows/Linux/Mac), and any error messages from your debug.log file.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
