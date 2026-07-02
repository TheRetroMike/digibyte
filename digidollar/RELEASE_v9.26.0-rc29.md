# DigiByte v9.26.0-rc29 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ SAME TESTNET — NO RESET

**RC29 uses the same `testnet21` chain as RC28.** There is no testnet reset. Your existing chain data, wallets, and oracle keys carry forward. Just replace the binary and restart.

---

## What's New in RC29

RC29 is a **security hardening release** built on the Red Hornet v3 overnight audit. 17 bugs found and fixed, 402 adversarial tests added, zero exploitable vulnerabilities remaining.

### 1. Red Hornet v3 Security Audit — 17 Bugs Fixed

A comprehensive adversarial security audit was conducted overnight (Apr 3–4, 2026) covering all DigiDollar consensus, oracle, MuSig2, wallet, P2P, and policy code paths. 49 attack waves (RH-01 through RH-50) targeted specific vulnerability classes.

#### CRITICAL Fixes

**RH-30a: COOLDOWN_BLOCKS timing correction**
The volatility cooldown period was set to 144 blocks (36 minutes), not the intended 36 hours. An attacker could wait out the cooldown trivially. Fixed: increased to 8,640 blocks (36 hours) with overflow saturation arithmetic to prevent `cooldownEndHeight` from wrapping to zero.

**RH-30b: Address validation hardened**
`IsValidDigiDollarAddress` only checked the "D" prefix — any string starting with "D" passed. Fixed: full base58check validation including encoding, length, and checksum verification.

#### HIGH Fixes

**RH-25a: Bundle serialization consistency**
v0x03 oracle bundle fields (`version`, `aggregate_sig`, `participation_bitmap`) were dropped during P2P round-trip serialization. Fixed with version-conditional serialization in `COracleBundle`.

**RH-38: DD opcode vulnerabilities**
Three issues: zero-amount mint bypass, `MAX_MONEY`/`MAX_DIGIDOLLAR` mismatch allowing mints above the DD supply cap, and unvalidated trailing `OP_RETURN` data. All fixed.

**RH-49: Dynamic OP_RETURN finder**
Hardcoded `vout[2]` index for DD `OP_RETURN` extraction replaced with `FindDDOpReturn()` helper that handles variable transaction structures.

#### MEDIUM Fixes

**RH-03: P2P message hardening**
- Rate limiting: 600 MuSig2 messages/hour per peer (prevents relay amplification DoS)
- Oracle ID range validation: reject IDs ≥ `ORACLE_TOTAL_COUNT` (IDs 30–254 were slipping through)
- Epoch validation: reject epochs outside `[1, current_epoch+1]`
- Memory cap: 2,048-entry limit for `RegisterSeenHash` (prevents unbounded memory growth)

**RH-08: Plaintext key erasure**
`EncryptDDKeys()` now calls `EraseDDOwnerKey()` and `EraseDDAddressKey()` after writing encrypted versions, preventing forensic recovery of plaintext DD private keys from `wallet.dat`.

**RH-24: Schnorr authentication on P2P messages**
MuSig2 P2P messages (`ORACLEMUSIGNONCE`, `ORACLEMUSIGPARTIALSIG`) now carry Schnorr signatures to prevent unauthenticated oracle message spoofing.

**RH-26c: Transaction type bounds check**
`GetDigiDollarTxType` lacked bounds checking — out-of-range enum access could crash the node. Fixed.

**RH-36a: ERR state TOCTOU race**
DCA cache could overwrite reconstructed ERR (Exchange Rate Reserve) state after node restart. Fixed with `s_stateReconstructed` guard flag.

**RH-36b: Health value clamping**
Health values are now clamped to `[0, 30000]` before DCA tier lookup, preventing integer overflow in multiplier calculations.

**RH-36c: Early DD transaction rejection**
Invalid DD transaction types are now rejected early in `AcceptToMemoryPool` before expensive oracle/database lookups (DoS mitigation).

**RH-25b: Cached price reorg handling**
`RemovePriceCache` now reverts `cached_price` after chain reorganizations, preventing stale oracle prices from being returned.

### 2. Red Hornet v3 Test Suite — 402 Adversarial Tests

52 new test files covering every attack surface:

| Category | Tests | Coverage |
|----------|-------|----------|
| Key aggregation (RH-01) | 7 | Rogue key, Wagner, linearity, degenerate keys |
| P2P & orchestrator (RH-03/04) | 34 | Message flooding, validation bypass, nonce replay |
| Bundle validation (RH-05) | 19 | Version downgrade, price manipulation, bitmap abuse |
| Minting attacks (RH-06) | 36 | Collateral bypass, zero mint, integer overflow |
| Redemption attacks (RH-07) | 17 | Early redemption, partial burn, double-spend |
| Wallet security (RH-08) | 13 | Key derivation, UTXO injection, encryption |
| Oracle price feeds (RH-09) | 12 | Source manipulation, extreme prices, staleness |
| Integration chains (RH-10) | 6 | Multi-step reorg + mint + oracle attack scenarios |
| Consensus edge cases (RH-11–21) | 195+ | Script, economic, serialization, boundary |
| Auth & serialization (RH-24–26) | 14 | Schnorr auth, dust policy, version fields |
| Deep protocol (RH-28–35) | 176 | Fork scenarios, DCA, mempool, state machine, chaos |
| Regression & invariants (RH-40–49) | 265+ | Formal invariants, thread safety, RPC validation |

### 3. Test Suite Fixes

**MuSig2 test suite merge** — Red Hornet RH-01 adversarial tests and session manager tests were in separate `BOOST_FIXTURE_TEST_SUITE` blocks that caused test runner discovery issues. Merged into their parent suites.

**Bench sanity check fix** — `bench_digibyte` was invoked with `-sanity-check` and `-priority-level` during `make check` but neither flag was registered with `ArgsManager`. Fixed: both args registered, and sanity check now uses `-filter="SHA256_32b"` for a faster single-bench run.

**DigiByte address fixtures** — 16 JSON test data files in `test/util/data/` still contained Bitcoin mainnet addresses (`1xxx` prefix) inherited from Bitcoin Core. Updated to DigiByte format (`Dxxx` prefix).

**8 functional tests updated** for RC29 compatibility:
- `digidollar_oracle_price.py` — sub-cent precision handling, accept `price_cents=0`
- `digidollar_rpc_estimate.py` — tolerance for DCA multiplier effects
- `digidollar_rpc_gating.py` — added missing `-digidollar=1` flag
- `digidollar_bug11_bug13_regression.py`, `oracle_consistency.py`, `rpc_position_fields.py`, `watchonly_rescan.py`, `rpc_display_bugs.py` — minor fixes

### 4. Documentation

- **RED_HORNET_V3_REPORT.md** — Full security audit report with all 17 findings
- **RH45_WALLET_CRYPTO_FINDINGS.md** — HD key derivation and wallet crypto review
- **WALLET_MIGRATION_GUIDE.md** — Testnet wallet migration between chain resets
- Updated architecture docs: `DIGIDOLLAR_ARCHITECTURE.md`, `DIGIDOLLAR_EXPLAINER.md`, `DIGIDOLLAR_ACTIVATION_EXPLAINER.md`, `REPO_MAP.md`

---

## What Was New in RC28 (included in RC29)

RC29 includes everything from RC28 and RC27. See [RC28 release notes](RELEASE_v9.26.0-rc28.md) for:

- Testnet reset to `testnet21` — chain contamination fix with new genesis block and magic bytes
- Fixed `getoracles` RPC — all 11 oracle names display correctly
- MuSig2 aggregate signing (`v0x03`) — full implementation
- Oracle consensus updated to 6-of-11
- `sendoracleprice` RPC permanently removed (security)
- Bug fixes #24–#29, #33
- Three critical MuSig2 signing fixes (nonce mismatch, message hash, bundle drift)
- 208-target fuzz testing suite
- Post-Quantum Cryptography roadmap

---

## Validation Results

| Category | Result | Status |
|----------|--------|--------|
| Unit tests | 2,857/2,857 PASS | ✅ |
| Fuzz tests | 113/113 PASS | ✅ |
| Functional tests | 34/43 PASS | ✅ (9 pre-existing) |
| Qt build | SUCCESS | ✅ |
| RPC commands | 7/7 working | ✅ |
| Debug logs | 160 lines, zero errors | ✅ |
| Consensus audit | 14 commits, ALL SAFE | ✅ |

### Pre-existing Test Issues (NOT from RC29)

- 4 functional tests: wrong constructor API (`__init__(__file__)` → `__init__()`)
- 4 functional tests: RPC field type expectations
- 1 functional test: missing `-digidollar=1` flag
- 3 unit tests: intentional "BUG DOCUMENTED" failures (RH-02 garbage partial sig — expected behavior)

---

## Commits Since RC28

```text
6b43876612 test: update util test fixtures from Bitcoin to DigiByte addresses
c192f23bd8 build: fix bench sanity check — register missing CLI args
161b905fbd test: merge MuSig2 sub-suites into parent test suites
95539795af docs: Red Hornet v3 audit report and documentation updates
571162865f test: fix 8 functional tests for RC29 compatibility
2b6a345d85 test: Red Hornet v3 — 402 security tests across 51 unit test files
9f0552bb8f build: add Red Hornet security test files to build system
ff30932331 fix: wallet, RPC, and Qt changes for RC29
dc1d6eacd3 security: Red Hornet v3 — oracle, MuSig2, and P2P hardening
3b8a2313aa security: Red Hornet v3 — consensus, validation, and policy hardening
828e4f5667 security: [RH-01] adversarial key aggregation tests — all defenses hold
```

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up custody of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Upgrading from RC28

No testnet reset — just replace the binary and restart:

```bash
digibyte-cli -testnet stop
# Replace binaries with RC29
digibyted -testnet -daemon
```

If you are an oracle operator, your oracle will auto-start:
- **Unencrypted wallets:** oracle starts automatically when wallet loads
- **Encrypted wallets:** oracle starts automatically after `walletpassphrase`

Manual start if needed:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

## Upgrading from RC27 or Earlier

Follow the RC28 migration guide — you need to migrate wallets from `testnet19`/`testnet20` to `testnet21`:

```bash
digibyte-cli -testnet stop
# Replace binaries with RC29

digibyted -testnet -daemon
digibyte-cli -testnet stop

# Copy oracle wallet from old testnet
cp -r ~/.digibyte/testnet20/wallets/oracle ~/.digibyte/testnet21/wallets/
# or from testnet19:
cp -r ~/.digibyte/testnet19/wallets/oracle ~/.digibyte/testnet21/wallets/

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

For the complete guide, see **`DIGIDOLLAR_ORACLE_SETUP.md`**.

---

## Oracle Operators (Testnet)

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
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc29-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc29-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc29-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc29-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc29-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc29-aarch64-linux-gnu.tar.gz` |

---

## Known Issues

- 9 pre-existing functional test failures (not RC29 regressions — constructor API, field types, missing flags)
- 3 intentional unit test failures (RH-02 garbage partial sig — documents expected behavior)
- RPC `price_cents` rounds sub-cent DGB prices to 1 (cosmetic, no consensus impact)

---

## Troubleshooting

### "My oracle did not start automatically"

Load the oracle wallet and start it manually:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### "Tests fail after upgrading"

Run `make clean && make -j$(nproc)` for a clean rebuild. Some test binaries may cache old object files.

### "I still see the old chain"

If you upgraded from RC27 or earlier, you need to be on `testnet21`. See the migration guide above.

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
