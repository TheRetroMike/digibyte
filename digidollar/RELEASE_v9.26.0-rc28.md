# DigiByte v9.26.0-rc28 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ TESTNET RESET — READ THIS FIRST

**RC28 resets DigiDollar testnet to `testnet21`.**

This is **not** the same chain as RC27 (`testnet20`) or earlier. The old testnets are retired.

- **Old testnet:** `testnet20` — port **12034** (RC27)
- **New testnet:** `testnet21` — port **12035** (RC28+)
- **New RPC port:** **14026** (was 14025)
- **Oracle consensus:** **6-of-11**
- **Oracle bundle format:** **MuSig2 aggregate signing (`v0x03`)**

### Why another reset?

RC27 was the major release with MuSig2, 6-of-11 consensus, and all the bug fixes. However, testnet20 inherited chain data from testnet19 because both shared the same genesis block hash and network magic bytes. RC28 fixes this permanently:

1. **New network magic bytes** — testnet20 nodes cannot connect to testnet21
2. **New genesis block** — completely fresh chain, no contamination possible
3. **New P2P port** (12035) — prevents accidental cross-network connections
4. **Fixed `getoracles` RPC** — all 11 oracle names display correctly (Ogilvie and ChopperBrian were showing as "Unknown")

### 🚨 ORACLE OPERATORS: KEEP YOUR WALLETS AND KEYS

**DO NOT delete your `testnet20` directory.** Your oracle wallet contains your private and public keys — you need to copy it into the new `testnet21` directory. Your oracle keypair does not change between testnet resets. Only the chain data resets.

### What operators and testers need to do

1. **Stop your RC27 node**
2. **Back up your old `testnet20` directory** — do NOT delete it
3. **Install RC28**
4. **Start RC28 once** so it creates `testnet21`
5. **Stop RC28**
6. **Copy your oracle wallet** from `testnet20` into `testnet21`:
   ```bash
   cp -r ~/.digibyte/testnet20/wallets/oracle ~/.digibyte/testnet21/wallets/
   ```
7. **Start RC28 again** and verify your wallet/oracle loads correctly

Your oracle public key and private key remain the same — only the chain resets.

### Fast migration example

```bash
digibyte-cli -testnet stop
# replace binaries with RC28

digibyted -testnet -daemon
# stop once testnet21 is created if you need to copy wallets
digibyte-cli -testnet stop

cp -r ~/.digibyte/testnet20/wallets/oracle ~/.digibyte/testnet21/wallets/

digibyted -testnet -daemon
```

If you are an oracle operator:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### 🔑 Oracle Operators

Oracle auto-start behavior from prior releases still applies:
- **Unencrypted wallets:** oracle starts automatically when wallet loads
- **Encrypted wallets:** oracle starts automatically after `walletpassphrase`

If auto-start does not trigger for any reason, use the manual `startoracle` command above.

### ⚠️ IMPORTANT: Update your `digibyte.conf`

If you have hardcoded port or addnode entries in your config, update them:

```ini
# Old (RC27)
addnode=oracle1.digibyte.io:12034

# New (RC28)
addnode=oracle1.digibyte.io:12035
```

The default RPC port has also changed from **14025** to **14026**. Update any scripts or monitoring tools.

---

## What's New in RC28

RC28 is a **patch release** on top of RC27. RC27 was the major release with MuSig2 aggregate signing, 6-of-11 oracle consensus, and extensive bug fixes. RC28 fixes issues discovered during RC27 testnet deployment.

### 1. Testnet reset to `testnet21` — chain contamination fix

RC27's `testnet20` shared the same genesis block hash as `testnet19`, which caused old chain data to contaminate the new testnet. RC28 prevents this permanently:

- **New network magic bytes:** `0xfd 0xd2 0xb9 0xe3` (was `0xfc 0xd1 0xb8 0xe2`)
- **New default P2P port:** `12035` (was `12034`)
- **New default RPC port:** `14026` (was `14025`)
- **New data directory:** `testnet21/` (was `testnet20/`)
- **New genesis block** with timestamp: "DigiDollar: 6-of-11 MuSig2 Oracle Consensus on The DigiByte Blockchain"

Different magic bytes cause immediate disconnect of incompatible peers — no more cross-network contamination.

### 2. Fixed `getoracles` RPC — all 11 names display correctly

The `getoracles` RPC had only 9 entries in its name lookup table, causing oracles 9 (Ogilvie) and 10 (ChopperBrian) to display as "Unknown". Fixed in all three RPC endpoints that use the name list (`getoracles`, `getalloracleprices`, `listoracle`).

### 3. Version bump and wallet image

- Version bumped from `v9.26.0-rc27` to `v9.26.0-rc28`
- Wallet splash image updated to reflect RC28

---

## What Was New in RC27 (included in RC28)

RC28 includes everything from RC27. Here is the full RC27 feature set:

### Oracle consensus updated to 6-of-11

Mainnet/testnet oracle configuration now uses the real 11-operator set with **6-of-11** consensus. This replaces the prior smaller testnet quorum model and gives the network better operator redundancy while still being practical for coordinated signing.

### MuSig2 aggregate signing (`v0x03`)

RC27 rolled DigiDollar oracle bundles onto **MuSig2 aggregate signatures**:
- Participating oracle signers contribute nonces and partial signatures
- Signatures are aggregated into a single compact Schnorr signature
- Bundle validation follows the aggregate-signature path for `v0x03` oracle bundles

### Security: `sendoracleprice` RPC removed

The `sendoracleprice` RPC has been **permanently removed**. This RPC allowed manual injection of arbitrary oracle prices, which is a security vulnerability — oracle prices must only come from live exchange data. Oracles now exclusively fetch prices from Binance, KuCoin, Gate.io, HTX, CoinGecko, and Crypto.com.

### Bug fixes from RC27

**Bug #24 — Multi-pass UTXO consolidation for large mints**
Large mints could fail when wallet UTXOs were fragmented. Fixed with multi-pass consolidation.

**Bug #25 — False-positive lock-height rejection for aged mints**
Mints created many blocks ago could be incorrectly rejected during validation. Fixed.

**Bug #26 — Oracle block_height preservation and price_source in RPC**
Local oracle `block_height` was not preserved correctly, and `getalloracleprices` now includes `price_source`.

**Bug #27 — DD change conservation in transfers**
Sub-dollar DD change was silently lost during transfers. Fixed with mandatory DD change output.

**Bug #29 — Dandelion++ ABBA deadlock (PR #394, JohnnyLawDGB)**
A classic ABBA mutex deadlock in `CheckDandelionEmbargoes` could cause the node to hang under sustained transaction relay load. Fixed by eliminating the lock-order inversion.

**Bug #33 — Mint tooltip limits and silent decimal truncation**
The Qt wallet mint dialog showed incorrect tooltip limits, and amounts with more than 2 decimal places were silently truncated. Both fixed.

### MuSig2 signing — three critical fixes

- **Nonce participant mismatch:** Aggregated all 11 keys but only 6 signed. Fixed: `TrimNoncesToThreshold()` trims to exactly `min_signers` before aggregating nonces.
- **Message hash mismatch:** Signer hashed (epoch, price, ts), verifier hashed (price, ts). Fixed: `ComputeOracleBundleHash()` now includes epoch.
- **Bundle value drift:** Miner used `GetTime()`, signer used consensus timestamp. Fixed: Orchestrator stores signed values via `SetSignedValues()`, miner retrieves them.

Phase 3 gates have been removed entirely — MuSig2 is always active from genesis, with automatic v0x02 fallback when signing sessions are not ready.

### Oracle bundle / consensus hardening

The RC27/RC28 line carries the broader MuSig2 implementation work:
- secp256k1 MuSig2 module integration
- MuSig2 signing session/orchestrator work
- Oracle bundle serialization/deserialization support for `v0x03`
- Expanded unit coverage for activation, bundle handling, aggregation, mining, net processing, and orchestration

### Fuzz marathon — 208 targets, zero real bugs

RC27/RC28 includes a comprehensive fuzz testing marathon covering all DigiDollar consensus, oracle, MuSig2, Dandelion++, Odocrypt, and multi-algorithm PoW code paths. 208 formal fuzz targets, all passing. OOM issues in DD validation harnesses fixed with strategy selector pattern.

### Post-Quantum Cryptography plan

Documentation for DigiByte's post-quantum cryptography roadmap:
- Full industry PQC landscape analysis (NIST standards, other chains' approaches)
- Two-phase migration strategy for DigiByte and DigiDollar
- Assessment of Taproot/P2TR output vulnerability to quantum key recovery

---

## Commits Since RC27

```text
469d330c7a qt: update wallet splash image to v9.26.0-rc28
032598ec0c consensus: mine testnet21 genesis block and hardcode hash
ed2799cdc0 build: bump version to v9.26.0-rc28
35ed2bbbab consensus: reset testnet to testnet21 with new genesis and magic bytes
88285ec2ad rpc: add Ogilvie and ChopperBrian to getoracles name list
62202e32de fix: use std::array instead of C-array in musig2_basic_tests
```

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up custody of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Oracle Operator Setup

### Upgrading from RC27

```bash
digibyte-cli -testnet stop
# Replace binary
# Back up/migrate wallet data from testnet20

digibyted -testnet -daemon
# Stop once testnet21 is created
digibyte-cli -testnet stop

cp -r ~/.digibyte/testnet20/wallets/oracle ~/.digibyte/testnet21/wallets/

digibyted -testnet -daemon
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### Upgrading from RC26 or earlier

```bash
digibyte-cli -testnet stop
# Replace binary

digibyted -testnet -daemon
digibyte-cli -testnet stop

# Copy from whichever old testnet you had
cp -r ~/.digibyte/testnet19/wallets/oracle ~/.digibyte/testnet21/wallets/
# or
cp -r ~/.digibyte/testnet20/wallets/oracle ~/.digibyte/testnet21/wallets/

digibyted -testnet -daemon
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### New Oracle Setup

```bash
digibyted -testnet -daemon
digibyte-cli -testnet createwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle createoraclekey <your_oracle_id>
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
# Future restarts will auto-start your oracle.
```

For the complete guide including earlier migration details, see **`DIGIDOLLAR_ORACLE_SETUP.md`**.

### Current Oracle Operators (Testnet)

| ID | Operator | Status |
|----|----------|--------|
| 0 | Jared | ✅ Active |
| 1 | Green Candle | ✅ Active |
| 2 | Bastian | ✅ Active |
| 3 | DanGB | ✅ Active |
| 4 | Shenger | ✅ Active |
| 5 | Ycagel | ✅ Active |
| 6 | Aussie Epic | ✅ Active |
| 7 | LookIntoMyEyes | ✅ Active |
| 8 | JohnnyLawDGB | ✅ Active |
| 9 | Ogilvie | ✅ Active |
| 10 | ChopperBrian | ✅ Active |

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
| `createoraclekey <id>` | Generate oracle Schnorr keypair (one-time) |
| `getoraclepubkey <id>` | Show oracle public key from wallet |
| `startoracle <id>` | Start running as an oracle operator |
| `stoporacle <id>` | Stop your oracle |
| `getoracleprice` | Get the consensus price |
| `getalloracleprices` | Per-oracle price breakdown |
| `getoracles` | Network-wide oracle status |
| `listoracle` | Show local oracle status |

---

## Configuration

```ini
testnet=1

[test]
digidollar=1
txindex=1
addnode=oracle1.digibyte.io
```

> **Note:** `txindex=1` is enforced at startup for DD-enabled nodes. Make sure it's in the correct section (`[test]` for testnet, `[main]` for mainnet). Global placement (above all sections) also works.

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (`testnet21`) |
| Default P2P Port | **12035** |
| Default RPC Port | **14026** |
| Oracle Consensus | **6-of-11** |
| Oracle Bundle Format | **MuSig2 aggregate signing (`v0x03`)** |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc28-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc28-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc28-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc28-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc28-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc28-aarch64-linux-gnu.tar.gz` |

---

## Known Issues

- This is a fresh DigiDollar testnet reset. Operators should migrate wallets from `testnet20` (or `testnet19`) as described above.
- As the network warms up, make sure enough oracle operators are online for **6-of-11** participation.
- Genesis block hash: `af94bc0b267965d39faab989bd7cf5a59f641dd02ccd40b0f43cbb0bf62c6122`

---

## Troubleshooting

### "My oracle did not start automatically"

Load the oracle wallet and start it manually:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### "I still see the old chain"

You are probably still looking at `testnet20` data. RC28 uses **`testnet21`**.

### "My peers are on the old testnet port"

RC28 testnet uses **12035**. If you hardcoded peers or firewall rules for **12034**, update them.

### "RPC connection refused"

RC28 uses RPC port **14026** (was 14025). Update your scripts and config files.

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
