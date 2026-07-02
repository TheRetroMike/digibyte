# DigiByte Core v9.26.0-rc46 Release Notes

RC46 is the DigiDollar mainnet-oracle-key-generation and final public `testnet26` validation release candidate.

Development branch: `feature/digidollar-v1`

Release: https://github.com/DigiByte-Core/digibyte/releases/tag/v9.26.0-rc46

---

## Read This First

RC46 does **not** reset public DigiDollar testnet.

RC44 created the fresh public `testnet26` chain and finalized the 35-slot / 7-signature MuSig2 oracle roster. RC45 hardened wallet recovery, oracle key export/import, OP_CHECKPRICE disablement, and DigiDollar health reconstruction. RC46 keeps that same network and tightens the last wallet/Qt failure-reporting issue, wallet rescan/import behavior, DigiDollar history performance, fee safety checks, operator documentation, multi-oracle harness behavior, and final audit reporting before using this build for:

1. generating final mainnet oracle public keys, and
2. running the final public `testnet26` validation pass.

Upgrade target:

- RC44/RC45 `testnet26` nodes should upgrade to RC46.
- Do **not** wipe `testnet26` just because of RC46.
- Do **not** return to `testnet25`; RC46 is still the RC44/RC45 public testnet chain.
- Existing testnet oracle operators should keep their assigned `testnet26` slot and testnet key material unless separately coordinated.
- Mainnet oracle operators should generate **new mainnet oracle keys** with RC46; do not reuse old testnet keys as mainnet keys.
- Back up wallets and oracle key material before and after generating oracle keys.

What changed from RC45:

- Qt DigiDollar mint failures now abandon rejected local mint drafts when relay/commit fails, matching the RPC mint path.
- Rejected local mints no longer linger in wallet history as phantom “DigiDollar Collateral Lock / Transfer” rows.
- DigiDollar wallet OP_RETURN parsing now skips mint/redeem metadata before amount parsing and ignores oversized pushes, preventing noisy `script number overflow` exceptions from mint owner-pubkey pushes.
- A Qt regression test now forces a rejected mint and verifies no non-abandoned DD mint draft remains in the wallet.
- The Qt mint tab now formats the DigiDollar USD-equivalent preview as cents (`100.00 $USD`) instead of oracle-price precision (`100.000000 $USD`), while leaving oracle DGB/USD price formatting unchanged.
- Bounded wallet rescans now stay bounded for DigiDollar instead of triggering a full DigiDollar UTXO scan after the requested block range completes.
- Descriptor imports now fail fast if a wallet rescan is already running, instead of silently waiting behind the active scan and looking like a slow import.
- DigiDollar wallet ownership checks now cache foreign-output misses, avoiding repeated full descriptor/Taproot scans during Qt DigiDollar tab refreshes and history rebuilds.
- `sendall` now rejects very large automatic sweep fees unless the caller explicitly chooses the fee behavior with `fee_rate` or `send_max`.
- DigiByte's intended `0.1 DGB/kB` default minimum transaction fee remains in place, and DigiDollar redemptions now enforce the `0.1 DGB` anti-spam fee floor.
- The multi-oracle testnet harness now performs stronger preflight/runtime checks and fails harder on bad oracle state instead of letting partial runs look healthy.
- Operator-facing docs were aligned around the supported wallet-stored oracle-key flow: `createoraclekey`, optional `exportoracleprivkey`, `importoracleprivkey` for recovery only, and `startoracle` without private-key handling.
- The final Red Hornet DigiDollar audit ledger/report was added for launch-readiness tracking.
- Version and wallet artwork were bumped to RC46.

What did not change:

- Testnet name remains `testnet26`.
- Data directory remains `testnet26`.
- Testnet P2P port remains `12033`.
- Default testnet RPC port remains `14026`.
- Testnet genesis, magic, activation height, and chain parameters do not change.
- DigiDollar activation remains BIP9 bit 23, minimum height 600 on testnet26.
- Oracle roster remains 35 active slots, IDs `0` through `34`.
- Oracle quorum remains 7 signatures from the configured 35-key MuSig2 roster.
- Oracle bundle format remains `v0x03`.
- Mainnet DigiDollar activation status does not change in RC46.
- DigiDollar economic rules, ERR policy, DCA policy, address formats, and wallet database format do not change.
- DigiByte's default minimum wallet fee policy does not change; the RC46 fee work preserves the `0.1 DGB/kB` floor and adds safer guardrails around expensive automatic sweeps.

---

## Summary

RC46 is the final release-test candidate before the intended formal DigiDollar mainnet release path.

The main user-facing fixes are straightforward: failed Qt mints no longer leave phantom DigiDollar rows, DigiDollar tab/history refreshes no longer repeat expensive ownership scans, bounded rescans stay bounded, and automatic DGB sweeps refuse unusually large implicit fees unless the caller opts in.

The main operator-facing purpose is just as important: RC46 is the build oracle operators should use to generate their final **mainnet** oracle public keys. Those keys will be collected, reviewed, added to chainparams, and used for the coordinated mainnet activation release. RC46 itself does not activate DigiDollar on mainnet.

RC46 is not a network reset and not an economic redesign. The intended DigiByte fee floor remains `0.1 DGB/kB`, and DigiDollar redemptions keep the `0.1 DGB` anti-spam fee requirement.

---

## One-Line Fix Summary

- Qt mint rejection cleanup: rejected local DigiDollar mint drafts are abandoned immediately.
- Wallet history correctness: failed Qt mints no longer surface phantom DD lock/transfer rows.
- OP_RETURN parser hardening: MINT/REDEEM metadata is skipped before amount parsing and oversized pushes are ignored.
- Regression coverage: new Qt tests verify rejected mints do not leave non-abandoned DD drafts behind and mint USD preview formatting stays at cents precision.
- Qt mint UI polish: DigiDollar mint USD-equivalent preview now matches dollar/cents display expectations.
- Bounded rescan fix: `rescanblockchain <start> <stop>` no longer pays a full DigiDollar scan afterward.
- Import/rescan fix: descriptor imports fail fast while a wallet scan is already active.
- Qt performance fix: DigiDollar history ownership checks cache foreign-output misses to avoid repeated full descriptor scans.
- Fee safety: automatic high-fee `sendall` sweeps are blocked unless explicitly requested.
- DD fee policy: DigiDollar redemptions enforce the `0.1 DGB` anti-spam fee floor while preserving the normal DigiByte fee floor.
- Multi-oracle harness hardening: final public testnet runs now fail more clearly on bad setup/state.
- Operator docs: oracle setup docs now emphasize wallet-stored keys and safer recovery/export guidance.
- Audit record: final Red Hornet launch report and ledger added for release-readiness tracking.
- RC46 versioning: client RC number and wallet artwork updated for the mainnet-key/final-test build.

---

## Mainnet Oracle Key Generation Build

RC46 is the intended build for collecting final mainnet DigiDollar oracle public keys.

Mainnet oracle key generation is local wallet key management only. `createoraclekey` is allowed before DigiDollar activation because it only creates and stores a key in the local wallet. It does **not** start an oracle, sign prices, relay oracle data, mine blocks, or change consensus state.

### Mainnet Key Rules

- Generate a fresh mainnet oracle key for the assigned oracle ID.
- Do **not** reuse a `testnet26` oracle key for mainnet.
- Run on **mainnet**, not `-testnet`, when generating the mainnet key.
- Use a dedicated wallet, conventionally named `oracle`.
- Keep this wallet separate from wallets holding normal mainnet funds. Do not generate oracle keys inside a spending wallet that holds personal/business funds.
- Back up the dedicated `oracle` wallet before and after key generation.
- Share only the returned compressed `pubkey` value, the 33-byte / 66-hex-character key beginning with `02` or `03`.
- Do not share `private_key` output from `exportoracleprivkey`.
- The `pubkey_xonly` value is derived from `pubkey`; operators do not need to send it separately unless specifically asked.
- Do not run mainnet `startoracle` until the mainnet key is included in chainparams and the coordinated activation/start instructions are issued.

### Simple Mainnet Key Process

Use this process if you are an assigned DigiDollar oracle operator generating your final mainnet key for the maintainer.

1. Install or build RC46.
2. Start DigiByte Core on **mainnet**. Do not use `-testnet` for this key.
3. Create or load a dedicated wallet named `oracle`.
4. Encrypt the wallet if desired, then unlock it before key generation if it is encrypted.
5. Run:

   ```bash
   digibyte-cli -rpcwallet=oracle createoraclekey <your_oracle_id>
   ```

6. Send the maintainer only:

   ```text
   oracle_id=<your_oracle_id>
   pubkey=<33-byte compressed pubkey starting with 02 or 03>
   operator_name=<operator/display name>
   endpoint=<host:12024 if already known>
   ```

7. Back up the `oracle` wallet immediately after key generation.
8. Store that backup somewhere safe and offline. If you lose this wallet/key after the key is added to chainparams, you lose the ability to operate that assigned oracle slot without coordinated replacement.
9. Optional emergency recovery export:

   ```bash
   digibyte-cli -rpcwallet=oracle exportoracleprivkey <your_oracle_id>
   ```

   The returned `private_key` is sensitive signing material. Never paste it into chat, GitHub, logs, screenshots, shell history, public issue trackers, or any system you do not fully control. Prefer encrypted wallet backups and offline secret storage.

### Qt Mainnet Key Process

Qt users can generate the same mainnet oracle key without using the command line for wallet creation:

1. Start DigiByte Core Qt on mainnet. Make sure it is **not** in testnet mode.
2. Use **File → Create Wallet** and create a dedicated wallet named `oracle`.
3. Do not use a wallet that holds normal mainnet funds.
4. Back up the new wallet before key generation if practical.
5. Open **Help → Debug Window → Console**.
6. Select/load the `oracle` wallet if Qt prompts for wallet context.
7. Run:

   ```text
   createoraclekey <your_oracle_id>
   ```

8. Send only the returned `pubkey` and your assigned `oracle_id` to the maintainer.
9. Back up the `oracle` wallet again after key generation.
10. Do not run `startoracle` on mainnet until final coordinated activation instructions are published.

### Mainnet Activation Note

RC46 does not activate DigiDollar on mainnet by itself. Mainnet DigiDollar operation still requires a coordinated release/activation path with the final mainnet oracle keys included in chainparams. RC46 is for generating and validating those keys safely before that step.

---

## Final Testnet26 Validation Build

RC46 also remains the final public `testnet26` validation candidate.

Minimum testnet operator checklist:

1. Back up wallets and oracle key material.
2. Install/run RC46.
3. Keep using `-testnet` and the existing `testnet26` datadir.
4. Keep P2P port `12033` open and advertised.
5. Confirm `getblockchaininfo` reports the testnet26 genesis hash below.
6. Confirm `getoracles` shows active oracle IDs `0` through `34` with a 7-signature threshold.
7. Confirm local oracle operators report `listoracle` with `running=true`, `authorized=true`, and `price_source=local`.
8. Run the final DigiDollar test flow against RC46 before declaring the testnet pass complete.

---

## Testnet26 Network Details

| Item | RC46 value |
| --- | --- |
| Testnet name | `testnet26` |
| Data directory | `testnet26` |
| Genesis timestamp | `1780156800` (2026-05-30 16:00:00 UTC) |
| Genesis hash | `0x0c9af936f28f7bd0e90c8f6235399063a026ed267bb53da398313b5d7aa55d82` |
| Merkle root | `0x76eca59f6a477206c602b72682a901fe87c48d1a6335ec3d094d5837c508c197` |
| Genesis nonce | `1283721` |
| Network magic | `fe c6 b9 e7` |
| Default P2P port | `12033` |
| Default RPC port | `14026` |
| DigiDollar activation | BIP9 bit 23, minimum height 600 |
| Oracle roster | 35 active mainnet/testnet slots |
| Oracle quorum | 7 signatures from the configured active MuSig2 keyset |
| Oracle bundle format | `v0x03` MuSig2 aggregate bundle |

Older operator notes that mention `testnet25`, port `12032`, incomplete oracle placeholders, 17/21/24 active oracles, or OP_CHECKPRICE mock-price behavior are stale for RC46.

---

## Validation Status

RC46 carries forward the full RC45 validation baseline and adds the post-RC45 wallet/Qt rejected-mint fix, bounded-rescan/import fixes, DigiDollar history performance fix, fee safety hardening, multi-oracle harness hardening, operator-doc lint alignment, and final audit report updates.

Known validation for the RC46 code line:

| Gate | Status |
| --- | --- |
| RC45 baseline build/unit/Qt/functional/multi-oracle/fuzz-smoke/operator-lint/whitespace gates | PASS on the RC45 codebase before RC46 branching |
| Rejected Qt mint fix validation from commit `670c7c6c72` | PASS: unit suite, Qt suite, DigiDollar functional suite, and wallet functional suite reported green |
| RC46 in-tree Qt compile smoke: `make -C src -j$(nproc) qt/digibyte-qt` | PASS |
| RC46 local testnet26 Qt/oracle smoke | PASS: `src/qt/digibyte-qt -testnet` runs on testnet26 and reports synced chain state locally |
| RC46 offscreen Qt test suite | PASS: 102 passed, 0 failed, 3 skipped |
| Operator surface lint after docs alignment | PASS |
| Final build gate: `make -C src -j$(nproc) digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt` | PASS |
| Final unit suite: `./src/test/test_digibyte --show_progress` | PASS |
| Final functional suite: `test/functional/test_runner.py --jobs=4` | PASS: full runner passed |
| Final Qt suite: `src/qt/test/test_digibyte-qt -platform offscreen` | PASS |
| Final multi-oracle testnet harness: `./test_multi_oracle_testnet.sh` | PASS: 434 passed, 0 failed |
| `git diff --check` after final RC46 release-note prep | PASS |

Recommended final release gate before publishing binaries:

```bash
make -C src -j$(nproc) digibyted qt/digibyte-qt test/test_digibyte qt/test/test_digibyte-qt
./src/test/test_digibyte --show_progress
QT_QPA_PLATFORM=offscreen ./src/qt/test/test_digibyte-qt -platform offscreen
python3 test/functional/test_runner.py --jobs=4
./test_multi_oracle_testnet.sh
python3 test/lint/lint-digidollar-operator-surface.py
git diff --check
```

---

## Commits Since RC45

- `873d6d068b` wallet: restore DigiByte fee floor and harden DD redemption fees
- `a557a74611` wallet: cache foreign DigiDollar output misses
- `14231a49f3` wallet: keep bounded rescans bounded for DigiDollar
- `8abd912ef3` wallet: fail descriptor imports before scan waits
- `9c6d0560eb` wallet: guard automatic sendall sweep fees
- `3d017c312e` wallet: align default min tx fee with relay (superseded by `873d6d068b`; final RC46 keeps the intended `0.1 DGB/kB` floor)
- `421dec56b1` release: finalize v9.26.0-rc46 notes
- `670c7c6c72` wallet/qt: clean up rejected DigiDollar mint, harden DD OP_RETURN parse
- `c7b8f8c174` release: bump version to v9.26.0-rc46
- `ea00d6009d` test: harden DigiDollar multi-oracle harness
- `bfb2c27315` docs: align DigiDollar oracle operator surface
- `2d96fa2068` audit: update Red Hornet final DigiDollar report
- `8c6e0a4e35` qt: format DigiDollar mint USD preview as cents
- `86abab4980` release: add v9.26.0-rc46 notes
- `1458d36e3b` cleanup: move prior release notes into `digidollar/`
- `d21c688c67` cleanup: move DigiDollar planning/migration docs and remove stale RC41/bug-hunt scratch files

---

## Technical Details

### Qt rejected mint cleanup

`WalletModel::mintDigiDollar()` now mirrors the RPC mint path when `CommitTransaction()` fails. If the rejected local transaction can be abandoned, Qt abandons it immediately with `AbandonTransaction()`. This prevents a rejected local mint draft from remaining in `mapWallet` as an inactive, non-abandoned DD-shaped transaction.

Without this cleanup, the generic wallet transaction history could decode the abandoned-looking local draft into DigiDollar history rows, creating a confusing UI artifact even though the DD vault correctly remained empty and no funds were lost.

### OP_RETURN parser hardening

`DigiDollarWallet::ProcessIncomingTransaction()` now skips `DD_TX_MINT` and `DD_TX_REDEEM` before extracting DD amounts from OP_RETURN pushes. Mint history is recorded by collateral-position handling, and redemption history/change is handled by the redemption path.

The amount parser also ignores pushes larger than 8 bytes before constructing `CScriptNum`. A mint OP_RETURN ends with a 32-byte owner pubkey push; parsing that as a number caused noisy caught `script number overflow` exceptions. RC46 removes that fragile path.

### Regression coverage

The new Qt regression `qtFailedMintAbandonsRejectedDraft` forces a mint commit rejection with a high relay-fee floor and asserts:

- the mint reports failure,
- no DD vault position is created, and
- no non-abandoned DD mint draft remains in the wallet.

### Qt mint USD preview formatting

The mint tab no longer reuses the six-decimal oracle-price formatter for the pegged DigiDollar USD-equivalent preview. A 100 DD mint now displays as `100.00 $USD`, and 100.1 DD displays as `100.10 $USD`, matching send-tab cents behavior while preserving six-decimal precision for live oracle prices such as DGB/USD.

A Qt regression test, `mintWidgetUsdEquivalentUsesCentsPrecision`, covers the zero, whole-dollar, and single-decimal display cases.

### Rescan and descriptor import fixes

Bounded rescans now keep DigiDollar work scoped to the requested range. A successful bounded `rescanblockchain <start> <stop>` reconciles position state without launching a full DigiDollar UTXO scan.

Descriptor imports now reserve the wallet rescan slot before waiting for chain sync. If another rescan is already running, `importdescriptors` returns the existing wallet-rescanning error quickly instead of blocking behind the active scan.

### DigiDollar history performance

DigiDollar ownership checks now cache known foreign Taproot output keys. This avoids repeatedly scanning every descriptor/Taproot script for the same non-wallet DigiDollar output during Qt tab changes, recent-transaction refreshes, and transaction-history rebuilds. The cache is cleared when wallet ownership inputs change.

### Fee safety

RC46 preserves DigiByte's intended default minimum wallet fee policy: `0.1 DGB/kB`.

The new fee work adds guardrails instead of lowering that floor. Automatic `sendall` sweeps without an explicit fee choice now reject unusually large implicit fees, and DigiDollar redemptions enforce the `0.1 DGB` absolute anti-spam fee floor.

### Multi-oracle harness hardening

The public testnet multi-oracle harness now has stronger guardrails around setup and runtime state. The goal is to catch broken oracle readiness, bad assumptions, or partial orchestration earlier, instead of letting a fragile run look like a valid final testnet pass.

### Operator surface alignment

The RC46 docs and lint rules are aligned around the safer oracle workflow:

- generate/store keys with `createoraclekey`,
- back up the wallet,
- use `exportoracleprivkey` only for controlled offline recovery,
- use `importoracleprivkey` only when restoring/migrating key material, and
- run `startoracle <oracle_id>` from the wallet-stored key after the key is authorized in chainparams.

### Final audit record

RC46 includes the final Red Hornet DigiDollar launch report and ledger under `reports/`. These files are release-readiness artifacts; they do not change consensus behavior.

---

## Operator Warnings

- `sendoracleprice` remains removed. Oracle prices must come from live exchange aggregation and MuSig2 oracle flow.
- OP_CHECKPRICE remains disabled for DigiDollar V1.
- Testnet26 oracle keys are for testnet26. Mainnet needs fresh mainnet key generation.
- Mainnet oracle keys must be generated from a mainnet wallet, not a `-testnet` wallet.
- Keep oracle wallets separate from wallets holding normal mainnet funds.
- Back up the dedicated `oracle` wallet after key generation.
- Never share oracle private keys.
- Never paste private-key export output into shell commands unless you fully understand shell history/process-list exposure. Prefer wallet backups and offline secret storage.
- `startoracle` reads the wallet-stored oracle key; do not pass private keys to `startoracle`.
- Mainnet oracle startup should wait for coordinated final instructions after mainnet keys are included in chainparams.
