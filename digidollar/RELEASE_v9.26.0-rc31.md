# DigiByte v9.26.0-rc31 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ✅ SAME TESTNET — NO RESET

**RC31 uses the same `testnet23` chain as RC30.** No reset, no new genesis, no network-magic change, no port change. Existing chain data, wallets, and oracle keys carry forward. Oracle operators running RC30 can drop in RC31 binaries and resume.

The one operator-facing change is a **three-slot oracle key rotation** (slots 11, 12, 13) — see §1 below. Slots 0–10 and 14–16 are unchanged.

---

## What's New in RC31

RC31 is a **hardening + operator-rotation release** on top of RC30. It ships MuSig2 correctness fixes, oracle roster alignment guards, a second wave of Red Hornet adversarial tests (RH-50 through RH-65) with targeted consensus/wallet/validation fixes, Qt DigiDollar polish, and rotated keys for three operators whose RC28/RC30 keypairs needed replacement.

## RC31 fix summary since RC30

- **Oracle slots 11, 12, 13 rotated** — hallvardo, DaPunzy, and DigiByteForce ship RC31 with fresh chainparams pubkeys after operator key-material replacement on their running nodes. First real-world exercise of DigiDollar's oracle key-replacement flow for three operators in the same release.
- **MuSig2: next-epoch session pre-started in the tail of the current epoch** — prevents the "first block of a new epoch drops all oracle messages" window during fast testnet epoch rollovers.
- **MuSig2: remote partial signatures verified before session admission** (`bundle_manager.cpp`) — stops invalid/forged partial sigs from being admitted to the local aggregation set even before trimming (W3-H-01).
- **MuSig2: `m_pending_partialsigs` buffer capped and pruned** — closes the unbounded-growth DoS where a hostile peer floods partial-sig messages for future/past epochs (RH-58 / W6-H-01).
- **Oracle roster alignment enforced at startup** — `ValidateOracleNodeAlignment()` runs on every node at init and refuses to start if `consensus.vOraclePublicKeys` disagrees with `vOracleNodes` (RH-50).
- **OP_CHECKPRICE now reads the live oracle consensus price** — the mock-price fallback used for mini-testnet bring-up is removed from the release path; scripts verify against the same price every node sees (W2 follow-up to RH-53).
- **OP_ORACLE added to the `IsOpSuccess` DigiDollar-opcode exclusion** — matches the rest of the DD opcodes and prevents the tapscript success-by-unknown-opcode path from silently accepting malformed OP_ORACLE encodings (W2-H-01).
- **BIP34 coinbase-height CScriptNum usage wrapped in oracle validators** — `ValidatePhase3OracleBundle` / `CheckBlock` oracle paths no longer throw `scriptnum_error` on crafted coinbases that nevertheless satisfy standard BIP34 (RH-52 PoC).
- **`ValidateDigiDollarTransaction` dispatcher wrapped in try/catch** — any `CScriptNum::scriptnum_error` escaping from a DD-script evaluator now becomes a policy reject instead of a node crash on the mempool path (RH-60 / W8).
- **`UpdatePriceCache` gated on BIP9 `DEPLOYMENT_DIGIDOLLAR`** — prevents a hostile miner from seeding the cached oracle price via a coinbase OP_ORACLE payload before DigiDollar is active (RH-61 / W9-C-01).
- **Mainnet/testnet oracle-validation parity** — the mainnet-only short-circuit in `bundle_manager.cpp` is removed, so both networks run the same validator with the same bundle rules (RH-65).
- **Qt DigiDollar balance refresh on tab open and on wallet updates** — stale DD balances no longer persist until the next block.
- **Qt DD receive panel now syncs with the selected receive request** — clicking a saved request populates the right-hand detail pane correctly.
- **Qt DD receive request double-click fixed** — the legacy duplicate handler path that swallowed the second click is removed.
- **Qt dark-mode peers detail panel contrast fixed** — peer detail labels are now readable against the dark background.
- **Logging hygiene** — `AcceptToMemoryPool` DigiDollar rejection log is now gated behind `BCLog::DIGIDOLLAR`, and oracle hot-path logs (`bundle_manager.cpp`, `digidollar/validation.cpp`) moved to `LogPrint(BCLog::DIGIDOLLAR, …)` so a default-logging node no longer floods `debug.log`.
- **Red Hornet wave-2 test corpus** — RH-51 through RH-65 land 16 new adversarial test files under `src/test/` covering activation-gate asymmetry, OP_CHECKPRICE weaponization, OP_ORACLE opsuccess escape, MuSig2 partial-sig aggregation, oversized-bitmap inflation, Trim→Aggregate TOCTOU, pending-partialsigs DoS, coin-control DD-lock bypass, mempool CScriptNum escape, coinbase price-cache poisoning, `senddigidollar` amount parser pathology, oracle validator escape hatches, DCA table disagreement, and mainnet/testnet validator parity.

## Known Issues

- **RH-59 (W7) lockunspent + DD-lock bypass on preset coin-control inputs** — the W7 enforcement patch (`9971f3b2f2`) was reverted in tip commit `ce0abf4e3a` after it regressed a legitimate mint flow. RH-59 ships as a documented PoC; no wallet-side fix in RC31. Tracked as a follow-up.
- **Slot 12 (DaPunzy) startoracle** — encrypted-wallet operators may see `"Oracle key already exists in wallet for oracle_id 12"` after an earlier `createoraclekey`. Workaround on Gitter.
- **`sendmanydigidollar` RPC** not yet implemented — feature request tracked from the PayDesk team.

---

## 1. Oracle key rotations (slots 11, 12, 13)

Three operators replaced their keypair between RC30 and RC31. The new pubkeys are wired into `consensus.vOraclePublicKeys`, `vOracleNodes`, and the testnet mini-testnet debug block in `src/kernel/chainparams.cpp`. Slot assignments and operator names are unchanged.

Values below are the 32-byte x-only form as stored in `consensus.vOraclePublicKeys` (BIP-340 Schnorr). The compressed form shown by `getoracles` prepends either `02` or `03` depending on y-parity.

| Slot | Operator | RC30 pubkey (old, x-only) | RC31 pubkey (new, x-only) |
|-----:|----------|---------------------------|---------------------------|
| 11 | **hallvardo** | `dfcb956f9e6f8ceea00b067176baa118ba8f0fbdb171a821a362af19234e64bd` | `4ef063a67b35295e9eaaa9251bc7f0effbceaedc8e9bc92504b0da832744ca08` |
| 12 | **DaPunzy** | `0f84e9bacc11c6b3f58d979adf3b0e6899b69d9f6ecd3e8d48bee2a7ac5a8560` | `75d7494b55ebc3874450954ac5caca402bbd068467a89d48fe877f3e2d09eda9` |
| 13 | **DigiByteForce** | `a3758e484fe8d46ecd2f3c0a56cfc2365464d229737c18e75d97f8893134fab9` | `4770d416e5f751ff310fa8e35cd48d9709ee539cbaa07c67dbf52503518cc573` |

**Operator action required for slots 11/12/13:** ensure your RC31 wallet contains the private key that matches the new RC31 chainparams pubkey. After `loadwallet`, either auto-start will pick up the new key, or run `startoracle <id>` manually. All other slots (0–10, 14–16) are unchanged and require no operator action.

This is the first time DigiDollar's oracle key-replacement flow has been exercised by three operators in a single release, and validates the "rotate in-place without a testnet reset" path.

## 2. MuSig2 epoch pre-start

`OracleSigningOrchestrator` now proactively spins up the next epoch's MuSig2 session in the tail window of the current epoch. Before RC31, a fast testnet epoch rollover could drop the first nonce/partial-sig exchange of the new epoch because no local session existed yet; remote peers saw "unknown epoch" and dropped valid early messages. Pre-start eliminates the race; the new `no_prestart_mid_epoch` regtest edge case is covered in `musig2_signing_orchestration_tests.cpp`.

## 3. MuSig2 hardening (remote partial sigs)

Two related fixes land in `src/oracle/`:

- **Pre-aggregation verification** — `bundle_manager.cpp` calls `secp256k1_musig_partial_sig_verify` on every remote partial sig before it enters the local session, rejecting sigs made against a mismatched `keyagg_cache` (W3-H-01, follow-on to RC30's `AddPartialSignatureVerified`).
- **Bounded pending buffer** — `m_pending_partialsigs` is capped and aggressively pruned by epoch, closing the unbounded-growth DoS where a peer floods partial sigs for far-future or stale epochs (RH-58 / W6-H-01). `signing_orchestrator.cpp` now enforces both a size cap and a window-bounded epoch range.

## 4. Oracle roster alignment (RH-50)

`ValidateOracleNodeAlignment()` is called unconditionally during node startup (`src/common/init.cpp`). It verifies that every entry in `consensus.vOraclePublicKeys` has a matching compressed pubkey in `vOracleNodes` for the same slot, and vice versa. A mismatch refuses to start rather than silently running with divergent oracle tables. Paired with the new `rh50_oracle_keyset_alignment_tests` and `digidollar_hot_path_logging_tests` suites under `src/test/`.

## 5. Script / opcode hardening

- **`OP_CHECKPRICE`** — `src/script/interpreter.cpp` now reads the live oracle consensus price through a new wallet-side accessor plumbed in `init.cpp`; the mock-price fallback previously used for mini-testnet bring-up is gone from the release path. Scripts verifying against price now see the same number every node sees (fix for the RH-53 weaponization class).
- **`OP_ORACLE` in `IsOpSuccess` exclusion** — `src/script/script.cpp` now lists `OP_ORACLE` alongside the rest of the DD opcodes in the `IsOpSuccess` exclusion set, closing the tapscript "success by unknown opcode" silent-accept path (W2-H-01).

## 6. Validation hardening

- **BIP34 CScriptNum wrap in oracle validators** — `ValidatePhase3OracleBundle` and the `CheckBlock` oracle path now catch `CScriptNum::scriptnum_error` around coinbase-height decoding. RH-52 showed a crafted minimal-BIP34 encoding could throw out of the validator and bypass the check; RC31 converts that path into a bundle reject (`src/validation.cpp`, `src/oracle/bundle_manager.cpp`).
- **`ValidateDigiDollarTransaction` dispatcher try/catch** — any `CScriptNum::scriptnum_error` escaping a DD-script evaluator on the mempool path is converted into a policy reject instead of propagating (`src/digidollar/validation.cpp`, W8 / C4 fix for RH-60).
- **`UpdatePriceCache` BIP9 gate** — the validator only writes to `cached_price` once `DEPLOYMENT_DIGIDOLLAR` is ACTIVE (`src/validation.cpp`). Pre-activation blocks with coinbase OP_ORACLE payloads can no longer seed the price cache (RH-61 / W9-C-01).
- **Mainnet/testnet validator parity** — `bundle_manager.cpp` had a mainnet-only short-circuit that skipped a subset of bundle checks. Removed; both networks now execute the same validator (`rh65_mainnet_testnet_validator_parity_tests`).

## 7. Qt DigiDollar polish

All four fixes include new Qt test coverage in `src/qt/test/digidollarwidgettests.cpp`:

- **Balance refresh on tab open and wallet updates** (`digidollartab.cpp`, `walletview.cpp`) — stale DD balances no longer persist until the next block.
- **DD receive panel sync** (`digidollarreceivewidget.cpp`) — selecting a saved request populates the detail pane.
- **DD receive double-click** — legacy duplicate handler removed; single click still works, double click now does the expected "open / edit" action without swallowing the second event.
- **Dark-mode peers detail contrast** (`src/qt/res/css/dark.css`) — Peers → Detail labels are now readable against the dark theme.

## 8. Logging hygiene

Default-logging nodes (no `-debug=digidollar`) previously flooded `debug.log` with DD- and oracle-specific hot-path traces. RC31 moves the noisy lines behind `BCLog::DIGIDOLLAR`:

- `AcceptToMemoryPool` DD-rejection log → gated (`src/validation.cpp`).
- Oracle bundle-manager and `digidollar/validation.cpp` hot-path lines → gated.
- New regression suite `digidollar_hot_path_logging_tests` asserts that default logging stays quiet and `-debug=digidollar` re-enables the traces.

## 9. Red Hornet wave-2 test corpus (RH-51 through RH-65)

Sixteen new adversarial test files land in `src/test/`:

| ID | Target |
|----|--------|
| RH-50 | Oracle roster alignment (keyset + hot-path logging) — **fix shipped** |
| RH-51 | CheckPhase3 v1/v3 split — regtest activation-gate asymmetry (documentation) |
| RH-52 | BIP34 scriptnum escape in oracle validators — **fix shipped** |
| RH-53 | OP_CHECKPRICE mock-price weaponization — **fix shipped** (live consensus price) |
| RH-54 | OP_ORACLE `IsOpSuccess` exclusion — **fix shipped** |
| RH-55 | MuSig2 unverified partial-sig aggregation — **fix shipped** |
| RH-56 | Oversized-bitmap message inflation in `ExtractOracleBundle` |
| RH-57 | MuSig2 Trim→Aggregate TOCTOU race (documentation) |
| RH-58 | Orchestrator `m_pending_partialsigs` DoS — **fix shipped** (cap + prune) |
| RH-59 | `FetchSelectedInputs` lockunspent + DD-lock bypass (W7) — **fix reverted**, PoC retained |
| RH-60 | Mempool DD validator CScriptNum escape — **fix shipped** (dispatcher try/catch) |
| RH-61 | Coinbase OP_ORACLE price-cache poisoning — **fix shipped** (BIP9 gate) |
| RH-62 | `senddigidollar` amount-parser pathology (documentation + hardening tests) |
| RH-63 | Oracle validator escape hatches (W1-M-05 / W11-H-03) |
| RH-64 | DCA multiplier table disagreement (W12-H-01) |
| RH-65 | Mainnet/testnet validator parity — **fix shipped** |

Raw test-run report captured in **`DIGIDOLLAR_BUG_HUNT_REPORT.md`**.

---

## What Was New in RC30 (included in RC31)

RC31 includes everything from RC30. See [RC30 release notes](RELEASE_v9.26.0-rc30.md) for:

- Oracle quorum expanded **8-of-15 → 9-of-17** and the 17-slot roster with `testnet23` reset
- **MuSig2 Phase 3 correctness fixes** — epoch in payload, P2P Schnorr auth on nonce/partial-sig messages, remote partial-sig pre-aggregation verify, lazy session creation
- 3-byte participation bitmap (84-byte v0x03 payload), slot-ordered `vOraclePublicKeys`
- `ValidateOracleConfiguration` slot-order relaxation
- Qt DD amount-widget UX, `estimatecollateral` health path fix, Qt minting auto-consolidation, mempool-chained DD conservation

---

## Validation Results (RC31)

| Category | Result | Status |
|----------|--------|--------|
| Unit tests (oracle / MuSig2 / DigiDollar / RH-50–65 suites) | 16 new suites added, all green locally | ✅ |
| MuSig2 signing orchestration tests | `no_prestart_mid_epoch` edge case covered | ✅ |
| Qt DigiDollar widget tests | 4 new test files exercising the UI polish fixes | ✅ |
| Oracle hot-path logging regression | Passes with default logging and `-debug=digidollar` | ✅ |
| RH-65 mainnet/testnet parity | Both validators exercise the same code path | ✅ |
| testnet23 chain compatibility | Verified — no reset, RC30 wallet/oracle keys carry forward (except slots 11/12/13 rotations) | ✅ |

---

## Upgrade Path

RC31 is a drop-in binary replacement on `testnet23`.

1. Stop the RC30 node.
2. Install the RC31 binary.
3. Start. Data dir, wallet, and chainstate remain compatible — no migration step.
4. **Slots 11, 12, 13 operators only:** confirm the RC31 chainparams pubkey in §1 matches a private key in your oracle wallet. If you regenerated your keypair between RC30 and RC31, ensure the new one is the one loaded.

If you are an oracle operator for a slot **other than 11/12/13**, your oracle will resume under the existing wallet and key with no action needed:

- **Unencrypted wallets:** auto-start on wallet load.
- **Encrypted wallets:** auto-start after `walletpassphrase`.

Manual start if needed:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

---

## Oracle Operators (RC31)

| ID | Operator | Status |
|----|----------|--------|
| 0 | Jared | ✅ Active |
| 1 | Green Candle | ✅ Active |
| 2 | Bastian | ✅ Active |
| 3 | DanGB | ✅ Active |
| 4 | Shenger | ✅ Active |
| 5 | Ycagel | ✅ Active |
| 6 | Aussie | ✅ Active |
| 7 | LookInto | ✅ Active |
| 8 | JohnnyLawDGB | ✅ Active |
| 9 | Ogilvie | ✅ Active |
| 10 | ChopperBrian | ✅ Active |
| 11 | **hallvardo** | ✅ Active (**key rotated for RC31**) |
| 12 | **DaPunzy** | ✅ Active (**key rotated for RC31**) |
| 13 | **DigiByteForce** | ✅ Active (**key rotated for RC31**) |
| 14 | Neel | ✅ Active |
| 15 | DigiSwarm | ✅ Active |
| 16 | GTO90 | ✅ Active |

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (`testnet23`) |
| Genesis Hash | `0xa19e809bb060f7f50c05a9bec7fdefedd8497aa0bd6ccca6f55c86090963e4ca` |
| Network Magic | `fd d2 b9 e4` |
| Default P2P Port | **12030** |
| Default RPC Port | **14026** |
| Oracle Consensus | **9-of-17** |
| Oracle Bundle Format | **MuSig2 aggregate signing (`v0x03`, 84 bytes)** |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

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

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc31-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc31-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc31-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc31-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc31-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc31-aarch64-linux-gnu.tar.gz` |

Binaries attached to the GitHub release once Guix builds complete and are verified.

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

### "Node refuses to start: oracle roster alignment mismatch"

That is the new RH-50 guard working as intended. It indicates `consensus.vOraclePublicKeys` and `vOracleNodes` disagree in your local chainparams. Make sure you are running an official RC31 binary — do not hand-edit chainparams.

---

## Commits Since RC30

```
f3547ed736 Update WALLET_MIGRATION_GUIDE.md
c30b1219eb chainparams: rotate oracle slot 11 key for RC31
93fb188570 Bump version to v9.26.0-rc31, update wallet image, add release notes
dfd21b48fb musig2: pre-start next epoch's session in tail of current epoch
2ab3742096 test: fix no_prestart_mid_epoch regtest edge case
0be06d5ea8 Fix oracle roster alignment and gate hot-path logs
3ceb8e6b62 Update Z_PROMPTS.md
d31e35cfcd chainparams: rotate oracle slot 13 key for RC31
74ae748b6d chainparams: rotate oracle slot 12 key for RC31
06bb7ca709 validation: gate ATMP DD-failure log behind BCLog::DIGIDOLLAR
f2aa17bde8 qt: refresh DigiDollar balances on open and wallet updates
f77bd4618a qt: sync DD receive panel with selected request
6455b9e974 qt: fix dark-mode peers detail contrast
e8a4f376d9 qt: fix DD receive request double-click
96586c2513 test: rh52 PoC — BIP34 scriptnum escapes CheckBlock oracle validators
2b37384e79 validation: wrap BIP34 coinbase height CScriptNum in oracle validators
47829d5f6e test: rh51 — document regtest activation-gate asymmetry (hardening)
867d36f166 test: rh53 — OP_CHECKPRICE mock price weaponization (Wave-2 PoC)
20d56c34da script: include OP_ORACLE in IsOpSuccess DD-opcode exclusion (W2-H-01)
ef2c995b63 test: rh55 — MuSig2 unverified partial-sig aggregation (W3-H-01 PoC)
986eca83ce oracle: verify remote MuSig2 partial sigs before session admission (W3-H-01)
98f4860632 test: rh56 — oversized-bitmap message inflation in ExtractOracleBundle (W4)
3ff77cdf0d test: rh57 — MuSig2 Trim→Aggregate TOCTOU race documentation (W5)
f357d30366 test: rh58 — OracleSigningOrchestrator pending-partialsigs DoS PoC (W6-H-01)
35561d598b oracle: cap + prune m_pending_partialsigs buffer (W6-H-01)
de477c9d87 test: rh59 — FetchSelectedInputs bypasses lockunspent + DD-lock (W7)
9971f3b2f2 wallet: enforce lockunspent locks on preset coin-control inputs (W7)
65af337a65 test: rh60 — DD validator CScriptNum escape on mempool path (W8 / C4)
c66c853479 digidollar: wrap ValidateDigiDollarTransaction dispatcher in try/catch (W8 / C4)
1753e6c3f4 test: rh61 — miner coinbase OP_ORACLE price-cache poisoning (W9-C-01)
fd1ac41424 validation: gate UpdatePriceCache on BIP9 DEPLOYMENT_DIGIDOLLAR (W9-C-01)
4e843e7859 test: rh62 — senddigidollar amount-parser pathology (W10)
6bc5b027a6 test: rh63 — oracle validator escape hatches (W1-M-05 / W11-H-03)
7dcbd09df6 test: rh64 — DCA multiplier table disagreement (W12-H-01)
06e1362c16 Create DIGIDOLLAR_BUG_HUNT_REPORT.md
f0d9a7b2c7 oracle: remove mainnet oracle-validation short-circuit (testnet/mainnet parity)
f77678cd0f script: wire OP_CHECKPRICE to live oracle consensus price, remove mock fallback
811a761d31 test: sync is_op_success Python helper with C++ IsOpSuccess (OP_ORACLE 0xbf)
ce0abf4e3a wallet: revert rh59/W7 lockunspent enforcement on preset coin-control inputs
```

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
