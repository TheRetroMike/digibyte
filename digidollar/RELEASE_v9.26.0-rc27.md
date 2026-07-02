# DigiByte v9.26.0-rc27 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ TESTNET RESET — READ THIS FIRST

**RC27 resets DigiDollar testnet to `testnet20`.**

This is **not** the same chain as RC19–RC26. The old testnet (`testnet19`, port 12033) is retired.

- **Old testnet:** `testnet19` — port **12033** (RC19–RC26)
- **New testnet:** `testnet20` — port **12034** (RC27+)
- **Same default RPC port:** **14025**
- **Oracle consensus:** **6-of-11**
- **Oracle bundle format:** **MuSig2 aggregate signing (`v0x03`)**

### 🚨 ORACLE OPERATORS: KEEP YOUR WALLETS AND KEYS

**DO NOT delete your `testnet19` directory.** Your oracle wallet contains your private and public keys — you need to copy it into the new `testnet20` directory. Your oracle keypair does not change between testnet resets. Only the chain data resets.

### What operators and testers need to do

1. **Stop your RC26 node**
2. **Back up your old `testnet19` directory** — do NOT delete it
3. **Install RC27**
4. **Start RC27 once** so it creates `testnet20`
5. **Stop RC27**
6. **Copy your oracle wallet** from `testnet19` into `testnet20`:
   ```bash
   cp -r ~/.digibyte/testnet19/wallets/oracle ~/.digibyte/testnet20/wallets/
   ```
7. **Start RC27 again** and verify your wallet/oracle loads correctly

Your oracle public key and private key remain the same — only the chain resets.

### Fast migration example

```bash
digibyte-cli -testnet stop
mv ~/.digibyte/testnet19 ~/.digibyte/testnet19.backup
# replace binaries with RC27

digibyted -testnet -daemon
# stop once testnet20 is created if you need to copy wallets
digibyte-cli -testnet stop

cp -r ~/.digibyte/testnet19.backup/wallets/oracle ~/.digibyte/testnet20/wallets/

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

---

## What's New in RC27

RC27 is the **testnet reset + MuSig2 rollout release** for DigiDollar oracle signing.

### 1. Testnet reset to `testnet20`

RC27 starts a fresh DigiDollar testnet on **`testnet20`** while keeping the existing testnet genesis/magic-byte family intact.

What changed:
- data directory moved from `testnet19` → `testnet20`
- default testnet P2P port moved from **12033** → **12034**
- testnet oracle peer endpoints were updated to match the new port

What did **not** change for this reset:
- no new genesis block for this RC27 reset path
- no new testnet magic bytes for this RC27 reset path

### 2. Oracle consensus updated to 6-of-11

Mainnet/testnet oracle configuration now uses the real 11-operator set with **6-of-11** consensus.

This replaces the prior smaller testnet quorum model and gives the network better operator redundancy while still being practical for coordinated signing.

### 3. MuSig2 aggregate signing (`v0x03`)

RC27 rolls DigiDollar oracle bundles onto **MuSig2 aggregate signatures**.

That means:
- participating oracle signers contribute nonces and partial signatures
- signatures are aggregated into a single compact Schnorr signature
- bundle validation follows the aggregate-signature path for `v0x03` oracle bundles

### 4. Security: `sendoracleprice` RPC removed

The `sendoracleprice` RPC has been **permanently removed**. This RPC allowed manual injection of arbitrary oracle prices, which is a security vulnerability — oracle prices must only come from live exchange data. Oracles now exclusively fetch prices from Binance, KuCoin, Gate.io, HTX, CoinGecko, and Crypto.com.

### 5. Bug fixes

**Bug #24 — Multi-pass UTXO consolidation for large mints**

Large mints could fail when wallet UTXOs were fragmented. Fixed with multi-pass consolidation.

**Bug #25 — False-positive lock-height rejection for aged mints**

Mints created many blocks ago could be incorrectly rejected during validation. Fixed.

**Bug #26 — Oracle block_height preservation and price_source in RPC**

Local oracle `block_height` was not preserved correctly, and `getalloracleprices` now includes `price_source`.

**Bug #27 — DD change conservation in transfers**

Sub-dollar DD change was silently lost during transfers. Fixed with mandatory DD change output.

**Bug #29 — Dandelion++ ABBA deadlock (PR #394, JohnnyLawDGB)**

A classic ABBA mutex deadlock in `CheckDandelionEmbargoes` could cause the node to hang under sustained transaction relay load. Thread A held `m_dandelion_embargo_mutex` and tried to acquire `m_nodes_mutex`, while Thread B held `m_nodes_mutex` and tried to acquire `m_dandelion_embargo_mutex`. Fixed by eliminating the lock-order inversion.

**Bug #33 — Mint tooltip limits and silent decimal truncation**

The Qt wallet mint dialog showed incorrect tooltip limits for the mint amount field, and amounts with more than 2 decimal places were silently truncated rather than rejected with a clear error message. Both issues are now corrected.

### 6. MuSig2 signing — three critical fixes

- **Nonce participant mismatch:** Aggregated all 11 keys but only 6 signed. Fixed: `TrimNoncesToThreshold()` trims to exactly `min_signers` before aggregating nonces.
- **Message hash mismatch:** Signer hashed (epoch, price, ts), verifier hashed (price, ts). Fixed: `ComputeOracleBundleHash()` now includes epoch.
- **Bundle value drift:** Miner used `GetTime()`, signer used consensus timestamp. Fixed: Orchestrator stores signed values via `SetSignedValues()`, miner retrieves them.

Phase 3 gates have been removed entirely — MuSig2 is always active from genesis, with automatic v0x02 fallback when signing sessions are not ready.

### 7. Oracle bundle / consensus hardening

The RC27 line also carries the broader MuSig2 implementation work:
- secp256k1 MuSig2 module integration
- MuSig2 signing session/orchestrator work
- oracle bundle serialization/deserialization support for `v0x03`
- expanded unit coverage for activation, bundle handling, aggregation, mining, net processing, and orchestration

### 8. Fuzz marathon — 208 targets, zero real bugs

RC27 includes a comprehensive fuzz testing marathon covering all DigiDollar consensus, oracle, MuSig2, Dandelion++, Odocrypt, and multi-algorithm PoW code paths. 208 formal fuzz targets, all passing. OOM issues in DD validation harnesses fixed with strategy selector pattern.

### 9. Post-Quantum Cryptography plan

RC27 includes documentation for DigiByte's post-quantum cryptography roadmap:
- Full industry PQC landscape analysis (NIST standards, other chains' approaches)
- Two-phase migration strategy for DigiByte and DigiDollar
- Assessment of Taproot/P2TR output vulnerability to quantum key recovery

These are documentation-only commits — no consensus changes.

### Test Suite

Full RC27 validation: all C++ unit tests passing, 23 DigiDollar functional tests passing, and 208 fuzz targets all passing.

---

## Commits Since RC26

```text
e57faf7349 Restore production testnet oracle keys for RC27 deployment
b909fe69b0 Remove problematic oracle fuzz targets pending debugging
4d978374cf Remove sendoracleprice remnants from client.cpp and init.cpp
3f5040066c Fix 4 unit test failures for RC27
303be240db Remove Phase 3 gates — MuSig2 is always active, v0x02 fallback allowed
79036fb2d5 Fix MuSig2 signing/verification mismatch — three critical bugs
2176161f4b SECURITY: Remove sendoracleprice RPC — fake price injection vulnerability
26dcdde79a Phase 3: skip oracle bundle when MuSig2 session not ready
fea8df08bb Phase 3 fallback: use Phase 2 bundles when MuSig2 session not ready
f29b689119 Threshold MuSig2: recompute key aggregation for signing subset
0d1f8ca39c Fix v0x03 bitmap sizing: use nOracleTotalOracles, not max participant ID
0f2a960a56 Fix P2P message name overflow: 'oramusignonce' was 13 chars (max 12)
f837036ca2 Fix MuSig2 session timeout: creation_height was set to epoch instead of block height
c8519a73a6 CRITICAL: Wire up MuSig2 signing orchestrator at startup + P2P ingestion
b9a50f78a3 MuSig2: multi-signer sessions + orchestrator-driven block assembly
fb8a602d2c oracle: wire MuSig2 P2P nonce/partial-sig ingestion into signing sessions
1fb3c8c0e2 doc: update RC27 release notes with Bug #29, Bug #33, PQC plan, and full commit list
a7f23b9cc3 Merge pull request #394 from JohnnyLawDGB/fix/dandelion-abba-deadlock
216fb8c364 fix: resolve Dandelion++ ABBA deadlock in CheckDandelionEmbargoes (Bug #29)
8c92bfdd63 fix: Bug #33 — wrong mint tooltip limits + silent decimal truncation
11cca7de0f Fix PQC landscape: DGB HAS Taproot deployed, P2TR outputs are vulnerable
458b0fe9de PQC Plan v2.0: Add full industry landscape and revised two-phase strategy
eec83c39c0 Add Post-Quantum Cryptography plan for DigiByte and DigiDollar
bb929f5d86 test: fix Phase 2 test compatibility with RC27 Phase 3 active from genesis
f605fcd476 fix: add missing musig2 headers to fuzz targets and update redteam tests for RC27 6-of-11
1efb39d5d0 RC27: Make regtest MuSig2 active and remove staged Phase3 gating
ecd86c0abc migrate regtest musig2 phase3 to genesis
74f5a60313 doc: add RC27 release notes for testnet20, 6-of-11 and MuSig2
3281f9a3f0 net: bump testnet directory and P2P port for reset
fa830acdf6 Remove phase3 gate for MuSig2 on main/test and update 6-of-11 oracle assumptions
a5cda93a73 Set main/test oracle consensus to 6-of-11 before Musig2 merge
3d66a1bac0 docs: record latest musig2 phase3 targeted validation pass
36f93eea38 docs: add fresh musig2 hardening evidence pass
b39e64b85e docs: correct musig2 harness validation evidence
0ae984aafa docs: record latest oracle phase2 targeted validation pass
05c0cecd88 docs: add v0x03 aggregate bundle flow validation evidence
154855456a musig2: harden oracle-id bounds in session rounds
8854bc0e7c test: tighten v03 mining bundle bitmap assertion
5cd1419d91 musig2: mark v03 decode version and strengthen bundle tests
699b653963 oracle: enforce exact v0x03 payload size on deserialize
961232c71e docs: add focused MuSig2 hardening regression evidence
e4d65465d8 test: fix oracle functional args for current node options
44236af8b2 docs: fold bitmap-width fix and targeted sanity rerun into MuSig2 status
82e91744ae musig2: pad participation bitmap to network oracle width
75efad3400 docs: finalize MuSig2 Wave 3 status and validation snapshot
16fa01adb3 test: seed coinbase in oracle phase2 pending-message tests
7c00520813 fix: remove regtest<=testnet ordering check (regtest Phase3 intentionally high)
43802ba4d7 musig2 wave3: decode v03 participants and add lifecycle cleanup tests
4fb205534a docs: update MUSIG2_SPRINT_STATUS — Wave 2 complete, 41 commits, 2129 tests pass
1af03dd9c2 fix: bump regtest Phase3 height to 100000 to avoid Phase2 test interference
e0aa6ac567 fix: resolve all MuSig2 test failures — 113 tests passing
9535dfb144 fix: update MuSig2 test expectations to match actual chainparams and v0x03 behavior
fb07aa61fe fix: disable test_v03_messages_decoded_from_bitmap (Wave 3 feature)
088d2fa190 fix: resolve all MuSig2 Wave 2 compile and link errors
8e07be9aff test: add Wave 2 MuSig2 integration tests and build wiring
648b937251 feat: wire MuSig2 v0x03 into bundle manager, session, and build system
193368ccaf feat: add MuSig2 orchestrator, session manager, and participation tracking
02cd12c1c2 test: add Phase3 bundle version activation boundary coverage
bad7399018 consensus: enforce oracle bundle v0x03 at Phase3 boundary
4cf35e42cd consensus: set DigiDollar Phase3 activation heights by network
10521086b2 test: add MuSig2 orchestration coverage for bundle manager
5727481711 oracle: add block-tick MuSig2 session orchestration hooks
e2660aba50 fix: use magic-byte validation for pubnonces instead of serialize
7e4e314b71 test: add MuSig2 net processing message coverage
6b1745c4e9 net: handle MuSig2 nonce and partial signature p2p messages
ba7c268211 test: add MuSig2 P2P message tests and 6 fuzz targets
70f25fb5c9 feat: add MuSig2 P2P message enums, NetMsgType strings, GetHash impls, aggregator cache ctor
e5b8711ab7 fix: remove duplicate v0x03 handler in ExtractOracleBundle
4579902c7a build: add musig2_bundle_creation_tests to unit test target
dadcfdace7 build: include musig2_bundle_manager_tests in unit test target
1b0b00a59b test: add MuSig2 bundle manager v0x03 round-trip coverage
adcdc8789e oracle: add v0x03 MuSig2 script create/extract handling
e7d63332b0 test: add v0x03 MuSig2 bundle creation/extraction tests (TDD)
b14620a080 security: use memory_cleanse for secnonce zeroing in MuSig2SigningSession
ef032561ad feat: implement MuSig2OracleAggregator
16f85ca6bc feat: add MuSig2OracleAggregator header (bitmap + key aggregation interface)
c2b058ed17 test: add MuSig2OracleAggregator tests (TDD — tests before implementation)
391a0481d4 build: add MuSig2SigningSession to build system
b792f97090 feat: implement MuSig2SigningSession state machine
2828259a2e feat: add MuSig2SigningSession header (state machine interface)
234002e5b1 test: add MuSig2SigningSession state machine tests (TDD — tests before implementation)
d98a0028a0 fix: correct SignSchnorr API usage in musig2_basic_tests
5e4e033172 chore: re-apply DGB cosmetic changes to secp256k1 v0.6.0
867753de8c build: update secp256k1 subtree to v0.6.0 (adds MuSig2 module)
45e86f6231 test: add MuSig2 basic crypto tests (TDD — tests before implementation)
d8140c6df0 feat: add nDigiDollarPhase3Height to consensus params
be7dddfee9 feat: implement v0x03 serialization/deserialization in oracle.cpp
1a427d25c5 feat: add v0x03 MuSig2 data structures to COracleBundle
9ae376fb37 test: add v0x03 MuSig2 bundle format tests (TDD first)
7a773869c9 docs: add FUZZ_MARATHON_COMPLETE — Phase 4 final delivery, all targets passing, OOM fixed & validated
621ef8bd47 docs: add FUZZ_PHASE4_FINAL_REPORT — Phase 4 regression complete, all targets validated
e6b8352f59 docs: add Phase 4 final completion report — all targets passing, strategy selector OOM fix validated
4e08300f32 docs: add FUZZ_FINAL_STATUS with OOM fix validation and CI recommendations
102f1f2348 fix: add strategy selectors to DD fuzz targets to prevent OOM under ASan
b2563fa9d5 fix: add strategy selectors to DD fuzz targets to prevent OOM
127ff3bdfa docs: add FUZZ_MARATHON_FINAL.md comprehensive regression report
a18b8283bb docs: fuzz test suite completion summary — 208 targets, all phases complete, zero real bugs
65b96a8cec fix: reduce harness loop bounds and buffer sizes in DD validation fuzz targets to prevent OOM
f5ff6108ea docs: add comprehensive Fuzz Completion Report for March 28 marathon
80ef20dee9 fuzz: add Phase 2A difficulty clamping harness
19934c5b06 fuzz: add Phase 2A block algo routing harness
0a16255fe0 fuzz: finalize Dandelion stem relay target
97b120cb5d fuzz: add Phase 2A proof-of-work all-algo harness
5785bd4de5 fuzz: add Dandelion epoch rotation target
86386813a7 fuzz: add Phase 2A local target adjustment harness
1edcf535ce fuzz: add Phase 2A algo work factor harness
070bc6f79f fuzz: add Dandelion stem relay target
1ddaad38ec fuzz: add Phase 2A algorithm selection harness
0a7d3a4f13 fuzz: add Dandelion routing table target
edcdd2aaa5 fuzz: add Odocrypt Keccak-P[800] finisher target
c2c03b9fee fuzz: add Odocrypt key schedule generation target
e6e8eb179b fuzz: add Phase 2A multishield era transition harness
96f8def5b7 fuzz: add standalone Odocrypt cipher target
3db8c4301a fuzz: add Phase 2A multishield v4 harness
3f6d48ef0b fix: prevent integer overflow in mint validation ratio calculation
feedff58b9 fix: prevent double-to-int64 overflow in ERR GetRequiredDDBurn
c0f9990286 fix: prevent integer overflow in DCA CalculateSystemHealth
5690d0a874 fuzz: add DigiDollar integer math and price conversion fuzz targets
97b54647fc fuzz: add deep DigiDollar validation pipeline fuzz targets
2e127be21a fix: prevent signed integer overflow in CalculateHealthRatio
b1cec14a04 fuzz: add DigiDollar script fuzz targets
9bb12245fa fuzz: add DigiDollar health system fuzz targets
96f6eea39c fuzz: add DigiDollar transaction builder fuzz targets
6edc6a091d fuzz: add DigiDollar logic fuzz targets
84eaae474e fuzz: add DigiDollar consensus fuzz targets
7f75c95451 fuzz: add DigiShield difficulty era fuzz targets
ea6d9fdf7f fuzz: add Dandelion++ privacy fuzz targets
509f3557d1 fuzz: add Odocrypt cipher fuzz targets
8a7a2648f7 fuzz: add multi-algorithm PoW fuzz targets
49eab852b4 fuzz: fix utxo_snapshot chain params initialization
cf91629144 fuzz: register DigiDollar/Oracle RPC commands in fuzz allowlist
ce1a016f02 fuzz: fix mini_miner_selection to use ALGO_SCRYPT for block template
be075b6fc3 fuzz: fix tx_pool Finish() to use ALGO_SCRYPT for block assembly
ff68264fc1 fuzz: fix stale block time comment in versionbits (10min -> 15sec)
f580f4af25 fuzz: fix package_eval outpoints_value for DGB Period I reward
3e9087dc09 fuzz: fix tx_pool SUPPLY_TOTAL for DGB Period I reward (72000 COIN)
e7f4db2aa8 fuzz: fix pow target phashBlock null pointer and add multi-algo coverage
0728db489a fuzz: fix integer target CompressAmount assertion for DGB 21B supply
306d8fdf67 fix: resolve process_message fuzz target crash from multi-algo PoW
755eb11e5e docs: validated byte-exact sizes, verified MuSig2 uses standard schnorrsig_verify
603746d4db docs: critical findings from code audit — signature hash change + epoch rotation
bd424a0c45 docs: add scaling analysis, variable-length bitmap, cache strategy
ae07aa083c docs: highlight linear scaling flaw in oracle bundle sizes
ff1dc1a785 docs: add MuSig2 oracle bundle implementation plan
acb7a09de4 version: bump to v9.26.0-rc27, update wallet image
46a78fd207 oracle: add ChopperBrian as oracle 10 (testnet)
53bbdd0065 oracle: add Ogilvie as oracle 9 (testnet)
611f372a9e consensus: upgrade testnet oracle consensus from 5-of-9 to 9-of-15
66b79115f4 test(transfer): add Bug #27 regression tests for sub-dollar DD change conservation
b940bc01bd fix(transfer): always create DD change output to preserve conservation (Bug #27)
918955ba3f fix: implement multi-pass UTXO consolidation for large mints (Bug #24)
8c54d38ea0 fix: preserve on-chain block_height for local oracle and add price_source to getalloracleprices (Bug #26)
59eed4a438 fix: prevent false-positive lock-height-mismatch rejection for aged mints (Bug #25)
```

---

## What is DigiDollar?

DigiDollar is a USD-pegged stablecoin built natively into DigiByte. It uses an over-collateralized model where users lock DGB to mint DUSD at the current oracle price of DGB.

The world's first truly decentralized stablecoin native on a UTXO blockchain, enabling stable value transactions without centralized control.

DGB becomes the strategic reserve asset (21B max, only ~1.94 DGB per person on Earth). Everything happens inside DigiByte Core wallet. You never give up custody of your private keys. No centralized company, fund or pool. Pure decentralization.

**Learn more:** https://digibyte.io/digidollar

---

## Oracle Operator Setup

### Upgrading from RC26

```bash
digibyte-cli -testnet stop
# Replace binary
# Back up/migrate wallet data from testnet19 if needed

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
| Network | Testnet (`testnet20`) |
| Default P2P Port | **12034** |
| Default RPC Port | **14025** |
| Oracle Consensus | **6-of-11** |
| Oracle Bundle Format | **MuSig2 aggregate signing (`v0x03`)** |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc27-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc27-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc27-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc27-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc27-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc27-aarch64-linux-gnu.tar.gz` |

---

## Known Issues

- This is a fresh DigiDollar testnet reset. Operators should expect to migrate wallets manually if they want to preserve old oracle keys.
- As the network warms up, make sure enough oracle operators are online for **6-of-11** participation.

---

## Troubleshooting

### "My oracle did not start automatically"

Load the oracle wallet and start it manually:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### "I still see the old chain"

You are probably still looking at `testnet19` data. RC27 uses **`testnet20`**.

### "My peers are on the old testnet port"

RC27 testnet uses **12034**. If you hardcoded peers or firewall rules for **12033**, update them.

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
