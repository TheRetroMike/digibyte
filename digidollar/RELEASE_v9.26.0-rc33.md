# DigiByte v9.26.0-rc33 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ✅ SAME TESTNET — NO RESET

**RC33 uses the same `testnet23` chain as RC32.** No reset, no new genesis, no network-magic change, no port change. Existing chain data, wallets, DD positions, and oracle keys carry forward.

Oracle operators running RC32 can drop in RC33 binaries and resume. There are **no oracle key rotations in RC33**.

---

## What's New in RC33

RC33 is a **massive DigiDollar hardening release** on top of RC32. It ships the Red Hornet DigiDollar/oracle hardening work, the follow-up Red Hornet security campaign, Qt/DD wallet display fixes, stronger wallet/RPC/index/reorg accounting, deterministic oracle validation fixes, MuSig2 replay/resource hardening, mining-template fixes, and broader registered functional/fuzz/unit coverage.

The short version: RC33 fixes the bugs found after RC32, keeps the same `testnet23`, and makes DigiDollar mint/transfer/redeem/oracle behavior much harder to break before the next public testnet push.

## RC33 fix summary since RC32

- **DigiDollar transfer display fixed in Qt** — local `sendmanydigidollar` receives now show up in wallet history, transaction rows display real DD amounts, and outgoing/incoming signs are preserved across the Overview and Transactions views.
- **Qt DigiDollar layout polish** — lock-tier text/columns no longer truncate important lock-period information.
- **DigiDollar DCA and collateral rounding hardened** — fractional DCA multipliers and satoshi collateral requirements now round conservatively instead of truncating below policy.
- **DigiDollar consensus validation hardened** — reordered mint outputs, ambiguous OP_RETURN metadata, transfer amount spoofing, redemption metadata ambiguity, and invalid mint side effects are covered by stricter validation.
- **Wallet DigiDollar accounting recovery fixed** — wallet state now survives rescans, restores, failed redemptions, pending redemption restarts, transfer reorgs, foreign mint discovery, and mixed/non-DD metadata edge cases more safely.
- **RPC DigiDollar validation tightened** — address validation, amount parsing, named-option defaults, redemption preflight, collateral quote bounds, estimate math, protection status, pending mint status, and selected-input DD change reporting are corrected.
- **Qt DigiDollar safety displays fixed** — watch-only receive handling, recoverable mint owner-key handling, position/redeem displays, and safety/status views were tightened.
- **DigiDollar stats/index reorg behavior fixed** — reordered valid mint outputs and reorged collateral-vault metadata are now accounted for consistently.
- **DD dust policy corrected** — DigiDollar protocol outputs that must be zero-value or special-form no longer get rejected/bypassed by generic dust assumptions in the wrong direction.
- **Oracle block validation made deterministic** — block validation no longer falls back to local oracle/mock/cache state when a deterministic block oracle price is required.
- **Oracle cache/reorg handling fixed** — oracle price cache updates roll back correctly across reorgs and avoid invalid-block side effects.
- **Oracle stale-data handling tightened** — stale pending messages, stale RPC status, stale miner-template bundles, stale pre-activation reloads, and retry edge cases are handled more defensively.
- **MuSig2/oracle P2P replay hardening** — duplicate signers, attestation replay poisoning, consensus replay hash poisoning, bundle replay hash bypasses, early partial-sig replay, and early partial buffer starvation were fixed.
- **MuSig2 bitmap/roster handling hardened** — epoch rosters are validated, bitmap extraction is bounded, and v0x03 bitmap script creation is more defensive.
- **Mining and `getblocktemplate` fixed for DD/oracle commitments** — DD block-template validation, oracle-price retry DoS behavior, and the oracle GBT schema were corrected.
- **Mock/regtest oracle behavior aligned** — mock oracle bundle timestamps and regtest bundle timestamp behavior now match the signing/validation expectations.
- **Test/fuzz coverage expanded and registered** — DD/oracle/MuSig2 parser, price, wallet, index, reorg, RPC, Qt, mining, and functional tests were expanded; previously missing DigiDollar/oracle scripts are now registered in the standard functional runner.
- **Documentation updated for RC33** — DigiDollar/oracle docs and repo maps were aligned with the current RC33 code surface.

---

## Red Hornet audit coverage included in RC33

RC33 includes the implementation fixes from two Red Hornet DigiDollar/oracle campaigns and the follow-up continuation pass.

Reference reports in this tree:

- `reports/red_hornet_final_report.md`
- `reports/red_hornet_ledger.md`
- `reports/red_hornet_security_final_report.md`
- `reports/red_hornet_security_ledger.md`

### Fixed Red Hornet implementation ranges

| Area | Fixed IDs | Summary |
|------|-----------|---------|
| DigiDollar consensus / validation | `DD-RH-001`, `003`, `004`, `005`, `007`, `010`, `011`, `050`, `052`, `056`, `060`, `061`, `062`, `065`, `070`, `101` | Hardens mint/transfer/redeem validation, DCA/collateral math, deterministic oracle pricing, failed redemption behavior, block-template DD validation, dust policy, and health side effects. |
| DigiDollar wallet / accounting | `DD-RH-012`–`020`, `053`, `054`, `055`, `067`, `071`, `073`, `074`, `077` | Fixes wallet recovery/accounting, DD-locked preset input bypass, pending mint status, selected-input change reporting, transfer reorg restore, rescan claim behavior, non-DD metadata crediting, pending redeem recovery, and network address validation. |
| DigiDollar RPC | `DD-RH-006`, `008`, `009`, `021`–`028`, `036`, `042`, `057`, `058`, `072`, `076`, `078`, `079` | Tightens protection status, DD address validation/import semantics, collateral quotes, estimate math, redemption preflight/address handling, named options, and amount parsing. |
| Qt DigiDollar | `DD-RH-029`–`033`, `080`, `081` | Fixes safety/status displays, watch-only receive addresses, and recoverable mint owner-key handling. |
| DigiDollar stats/index/reorg | `DD-RH-059`, `066`, `068` | Fixes order-independent mint accounting, oracle price-cache rollback, and stats-index vault metadata across reorgs. |
| Oracle / MuSig2 / P2P | `DD-RH-035`, `038`–`049`, `082`, `083`, `087`–`093`, `095`, `096`, `098`–`100`, `102`–`104` | Hardens oracle/MuSig2 validation, epoch rosters, bitmap bounds, stale messages/status/bundles, duplicate signers, replay poisoning, early partials, invalid-block side effects, miner retry DoS, GBT schema, and mock/regtest timestamps. |
| Coverage / harnesses | `DD-RH-051` plus supporting test/fuzz commits | Adds or repairs functional, unit, wallet, Qt, index, oracle, MuSig2, and fuzz coverage for the fixed classes. |

### Architecture / product decisions still intentionally open

These were **not** silently changed in RC33 because they require protocol, wallet-storage, oracle-liveness, or product-support decisions:

- `ARCH-RH-001` — strict post-activation `OP_ORACLE` handling versus liveness escape hatches.
- `ARCH-RH-002` / `DD-RH-002` — whether IBD/catch-up `skipOracleValidation` can remain consensus-affecting.
- `ARCH-RH-003` — final watch-only DigiDollar address storage/rescan/listing model.
- `ARCH-RH-004` — ERR behavior and burn-validation design.
- `DD-RH-034` — Qt mint owner-key generation/storage timing and HD recovery model.
- `DD-RH-037` — mainnet active oracle roster size versus 17-slot bitmap/protocol capacity.
- `DD-RH-069` — collateral-script policy for non-DD collateral spends after timelock without DD burn.
- `DD-RH-075` — encrypted wallet-storage migration for oracle operator private keys.
- `DD-RH-084` — mainnet active oracle roster IDs versus consensus/MuSig2 bitmap total.
- `DD-RH-085` — whether Phase Three-active heights must reject v0x02 Phase Two bundles.
- `DD-RH-086` — fail-open versus fail-closed behavior for malformed/unknown `OP_ORACLE` outputs.
- `DD-RH-094` — MuSig2 signer reselection/retry policy for low-ID nonce-trimming liveness attacks.
- `DD-RH-097` — network/genesis/deployment domain separation for MuSig2 signing.
- `LEGACY-DD-WALLET` — whether legacy wallets are unsupported for DigiDollar bech32m receive/mint, or need a compatible design.
- `DD-RH-059-WALLET-STORAGE` — wallet position storage still needs an approved schema path for arbitrary collateral vault output indexes.

---

## 1. DigiDollar consensus and accounting hardening

RC33 tightens the rules around how DD mint, transfer, and redemption transactions are interpreted:

- Valid reordered mint outputs are accounted for by structure rather than fixed output indexes where the affected path permits valid ordering.
- Transfer OP_RETURN metadata now has stricter amount/output matching so later spend extraction cannot be tricked by earlier fake metadata.
- Redemption metadata handling is less ambiguous and failed redemption attempts no longer mutate wallet state incorrectly.
- DCA and collateral math now round up where truncation could understate required collateral.
- Invalid mint attempts cannot poison volatility/system-health state.
- Non-DD metadata cannot be credited as DigiDollar balance.

This is the core “do not let accounting drift from consensus” RC.

---

## 2. Wallet, rescan, restore, and reorg fixes

RC33 fixes multiple wallet-side state recovery paths:

- Wallet-created local `sendmanydigidollar` receive rows are visible.
- `senddigidollar` reports actual selected-input DD change instead of whole-wallet remainder.
- DD transfer rows restore correctly across reorgs.
- Pending redeem state survives restart/recovery more safely.
- Foreign DD mints can be claimed during wallet rescans when the wallet controls the relevant key material.
- DD-locked inputs are rejected from normal preset-input wallet funding paths.
- Address validation is network-aware so wrong-network DD addresses are not accepted in the wrong context.

---

## 3. RPC hardening

RC33 tightens DD and oracle RPC behavior:

- `getprotectionstatus` / DD protection status no longer reports false emergency state with no DD liabilities.
- `calculatecollateralrequirement` rejects amounts that mint validation would reject.
- `estimatecollateral` removes overflow-prone dead math and keeps returned USD value tied to the requested DD amount.
- `redeemdigidollar` amount parsing, redemption address handling, and preflight checks are safer.
- Named option defaults are corrected.
- Oracle RPC status avoids reporting stale oracle state as fresh.
- `getblocktemplate` includes the corrected oracle schema behavior.

---

## 4. Oracle and MuSig2 hardening

RC33 significantly hardens the oracle path:

- Block validation uses deterministic block oracle data, not local fallback state, where consensus validation requires a block price.
- Oracle cache updates roll back correctly on reorg and avoid invalid-block side effects.
- Stale pending oracle messages and stale template bundles are cleared/refused more defensively.
- Epoch roster validation and MuSig2 bitmap bounds were tightened.
- Duplicate signers and replayed attestations/bundles/partials are rejected in more places.
- Early MuSig2 partial-signature handling no longer allows replay/buffer starvation behavior.
- Miner oracle-price retry behavior no longer creates a DoS loop.

---

## 5. Qt DigiDollar fixes

RC33 includes visible Qt wallet polish and safety fixes:

- DD transaction rows show DD amounts instead of misleading DGB/zero display.
- Overview send/redeem prefixes show the right sign.
- Lock-tier columns have enough width to avoid truncation.
- Watch-only DD receive addresses and recoverable mint owner-key displays behave more safely.
- History signs are preserved in the main transactions tab.

---

## 6. Validation results

| Category | Result | Status |
|----------|--------|--------|
| Unit suite | `./src/test/test_digibyte --show_progress` — 2967 test cases in the final Red Hornet continuation evidence | ✅ |
| Extended functional runner | `test/functional/test_runner.py --extended --jobs=4` — 360 registered scripts, runtime 804s in continuation evidence | ✅ |
| Standard functional runner | `test/functional/test_runner.py --jobs=8` — 318/318 configured scripts in security campaign evidence | ✅ |
| Direct DigiDollar/oracle functional loop | 64 direct DD/oracle/wallet scripts passed in security campaign evidence | ✅ |
| Fuzz smoke | 40 DD/oracle/MuSig2/RPC/script fuzz targets with empty/ascii/zero256 inputs passed in security campaign evidence | ✅ |
| `git diff --check` | Passed in final continuation evidence | ✅ |
| Local RC33 Qt testnet startup | In-source RC33 Qt launched on `testnet23`; local oracle wallet loaded; oracle slot 15 running locally | ✅ |
| Live oracle validation | Phase Two bundles validated with at least 9 signatures; local oracle 15 reported and broadcast prices around 3831–3842 micro-USD | ✅ |
| DigiDollar live stats | DigiDollar active on `testnet23`; health/status RPCs returned healthy system state during local RC33 smoke | ✅ |

Known exploratory limitation from the reports:

- `digidollar_basic.py --legacy-wallet` cannot generate a bech32m DigiDollar address. This is recorded as a legacy-wallet support decision, not as a fixed RC33 vulnerability.

---

## Upgrade Path

RC33 is a drop-in binary replacement on `testnet23`.

1. Stop the RC32 node or Qt wallet.
2. Install the RC33 binary.
3. Start with the same data directory and wallet.
4. Keep `txindex=1` enabled for DD nodes.
5. Oracle operators: load/unlock the same oracle wallet and run `startoracle <your_oracle_id>` if auto-start does not resume.

No chain reset or wallet migration is required.

Manual oracle start if needed:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

Encrypted wallet operators should unlock first:

```bash
digibyte-cli -testnet -rpcwallet=oracle walletpassphrase "your passphrase" 600
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

---

## Oracle Operators (RC33)

No oracle slot changes from RC32.

| ID | Operator | Status |
|----|----------|--------|
| 0 | Oracle slot 0 | ✅ Active |
| 1 | Oracle slot 1 | ✅ Active |
| 2 | Oracle slot 2 | ✅ Active |
| 3 | Oracle slot 3 | ✅ Active |
| 4 | Oracle slot 4 | ✅ Active |
| 5 | Oracle slot 5 | ✅ Active |
| 6 | Oracle slot 6 | ✅ Active |
| 7 | Oracle slot 7 | ✅ Active |
| 8 | Oracle slot 8 | ✅ Active |
| 9 | Oracle slot 9 | ✅ Active |
| 10 | Oracle slot 10 | ✅ Active |
| 11 | Oracle slot 11 | ✅ Active |
| 12 | Oracle slot 12 | ✅ Active |
| 13 | Oracle slot 13 | ✅ Active |
| 14 | Oracle slot 14 | ✅ Active |
| 15 | Oracle slot 15 | ✅ Active |
| 16 | Oracle slot 16 | ✅ Active |

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
algo=scrypt
addnode=oracle1.digibyte.io
```

> **Note:** `txindex=1` is enforced at startup for DD-enabled nodes. Make sure it is in the correct section (`[test]` for testnet, `[main]` for mainnet). Global placement above all sections also works.

### Scrypt CPU mining against local Qt/node

For local testnet scrypt mining, both the node and miner must agree on scrypt:

```ini
[test]
algo=scrypt
```

Start the miner with the scrypt flag explicitly:

```bash
cd /path/to/cpuminer
./minerd -c dgb-solo-mining.json -a scrypt -t 12
```

Healthy mining output includes:

- `12 miner threads started, using 'scrypt' algorithm.`
- `Long-polling activated for http://127.0.0.1:14026/`
- no repeated HTTP 401/500 errors.

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc33-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc33-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc33-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc33-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc33-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc33-aarch64-linux-gnu.tar.gz` |

Binaries attached to the GitHub release once Guix builds complete and are verified.

---

## Troubleshooting

### "My oracle did not start automatically"

Load and unlock the oracle wallet, then start manually:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle walletpassphrase "your passphrase" 600
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### "My DD transfer history looks different after RC33"

RC33 fixes several wallet/Qt display paths. Local sends to your own DD addresses, `sendmanydigidollar` receives, and main transaction rows should now show clearer DD in/out rows and signed DD amounts.

### "A collateral quote now fails before minting"

That is intentional when the quote is outside live mint bounds. RC33 makes `calculatecollateralrequirement` match mint validation instead of quoting impossible mints.

### "A legacy wallet cannot create a DigiDollar receive address"

Legacy wallet support for DigiDollar bech32m receive/mint remains a product decision. Descriptor wallets remain the tested DD wallet path.

### "Node refuses to start: oracle roster alignment mismatch"

That guard is intentional. It means local chainparams have inconsistent oracle public-key/roster data. Run an official RC33 binary and do not hand-edit chainparams.

---

## Commits Since RC32

| Commit | RC33 one-line summary |
|---|---|
| `2193e0fa57` | Update release/documentation baseline after RC32. |
| `a6c928ddc0` | Bump version and Qt wallet branding to RC33. |
| `20b69c0945` | Wallet: show local sendmany DigiDollar receives. |
| `a9de824643` | Qt: show DigiDollar transfer amounts. |
| `2177d04ea7` | Qt: fix DD Overview send/redeem sign prefix. |
| `d62e0a338f` | Qt: prevent DigiDollar lock tier truncation. |
| `54f9526822` | DigiDollar: mark transfer DGB lookup override. |
| `128c71b436` | DigiDollar: remove duplicate validation declarations. |
| `f8b2b95628` | Test: raise dbcrash transaction relay fee. |
| `7c3b801165` | Test: update index prune heights for DigiByte. |
| `bb0a5b253c` | Docs: align DigiDollar/oracle docs with RC33 code surface. |
| `144bd619b3` | Test: run multi-suite C++ targets correctly. |
| `94c727a89f` | Qt: widen DigiDollar lock tier column. |
| `4e5dfa1e34` | Qt: preserve DigiDollar history signs in transactions tab. |
| `788470a98b` | Test: stabilize DigiDollar Dandelion relay confirmation. |
| `5a43bc5d64` | Test: adapt pruning coverage to DigiByte RC33 behavior. |
| `4740af7559` | DigiDollar DCA: fix DD-RH-003 fractional multiplier rounding. |
| `c3b61a3546` | DigiDollar collateral: fix DD-RH-004 satoshi rounding. |
| `a35cdb7783` | DigiDollar consensus: fix DD-RH-001 DD-RH-005 DD-RH-007 DD-RH-010 DD-RH-011. |
| `74a0f07df2` | Wallet digidollar: fix DD-RH-012 through DD-RH-020 accounting recovery. |
| `19a2df1db3` | RPC digidollar: fix DD-RH-006 DD-RH-008 DD-RH-009 DD-RH-021 through DD-RH-028 DD-RH-036 DD-RH-042. |
| `d8b9d3be5f` | Qt digidollar: fix DD-RH-029 through DD-RH-033 safety displays. |
| `b0dc484897` | Oracle MuSig2: fix DD-RH-035 and DD-RH-038 through DD-RH-049. |
| `b1b70b5b30` | Test fuzz: expand Red Hornet DigiDollar oracle coverage. |
| `305dd806ca` | Test oracle: align DD-RH-010 block validation split. |
| `584b8372aa` | Test wallet: exercise DD-RH-012 state validation on live chain setup. |
| `87051c5999` | DigiDollar redeem: fix DD-RH-050 failed redemption state mutation. |
| `85c93a4fdc` | Test digidollar: repair DD-RH-051 extra functional coverage. |
| `f7fd01637c` | Record the completed Red Hornet DigiDollar audit package. |
| `d0606db668` | Validation oracle: fix DD-RH-052 block price fallback. |
| `1d081a8b79` | Wallet digidollar: fix DD-RH-053 preset input lock bypass. |
| `a4d4813389` | RPC digidollar: fix DD-RH-054 pending mint status. |
| `d3078dd9e7` | DigiDollar DCA: fix DD-RH-056 validator table split. |
| `6d5ea56332` | DigiDollar volatility: fix DD-RH-060 invalid mint poisoning. |
| `c677a8056b` | DigiDollar mint: fix DD-RH-061 OP_RETURN type mismatch. |
| `55c2e95f1a` | DigiDollar redeem: fix DD-RH-062 OP_RETURN ambiguity. |
| `6b481044b2` | Miner oracle: fix DD-RH-065 DD block template validation. |
| `f54bf8c59d` | Validation oracle: fix DD-RH-066 reorg cache rollback. |
| `99b3c618a7` | Wallet digidollar: fix DD-RH-067 transfer reorg restore. |
| `76d4c1c852` | Index digidollar: fix DD-RH-068 stats reorg vault metadata. |
| `3d77327ef8` | Policy digidollar: fix DD-RH-070 dust exemption. |
| `91f5c751d6` | Wallet digidollar: fix DD-RH-071 foreign mint rescan claim. |
| `dab712c2c5` | RPC digidollar: fix DD-RH-072 redemption preflight. |
| `78641061dc` | Wallet digidollar: fix DD-RH-073 non-DD metadata credit. |
| `9386619eb2` | Wallet digidollar: fix DD-RH-074 pending redeem recovery. |
| `40f5ce8d1d` | RPC digidollar: fix DD-RH-076 redemption address. |
| `59019bac2f` | Wallet digidollar: fix DD-RH-077 network address validation. |
| `18364bbe9f` | RPC digidollar: fix DD-RH-078 named option defaults. |
| `e7460b6d3d` | RPC digidollar: fix DD-RH-079 redeem amount parsing. |
| `037b11ef15` | Qt digidollar: fix DD-RH-080 watch-only receive addresses. |
| `f076874896` | Qt digidollar: fix DD-RH-081 recoverable mint owner keys. |
| `fe05ecfc60` | Oracle phase2: fix DD-RH-082 epoch roster validation. |
| `da6b541ba4` | Oracle MuSig2: fix DD-RH-083 bitmap extraction bounds. |
| `f6c4f77543` | Oracle relay: fix DD-RH-087 stale pending messages. |
| `2217a58061` | RPC oracle: fix DD-RH-088 stale status reporting. |
| `e81d5ccbdc` | Oracle miner: fix DD-RH-089 stale template bundles. |
| `f445b6fc0b` | Oracle P2P: fix DD-RH-090 duplicate signer consensus. |
| `24ebc85519` | Oracle P2P: fix DD-RH-091 attestation replay poisoning. |
| `2878e11741` | Oracle P2P: fix DD-RH-092 consensus replay hash poisoning. |
| `d82f35c677` | Oracle P2P: fix DD-RH-093 bundle replay hash bypass. |
| `e59d89ccc8` | Oracle MuSig2: fix DD-RH-095 early partial replay. |
| `7b5140f994` | Oracle MuSig2: harden v0x03 bitmap script creation. |
| `88f1568e5b` | Oracle MuSig2: fix DD-RH-096 early partial buffer starvation. |
| `b17e5ef922` | Oracle startup: fix DD-RH-098 pre-activation price reload. |
| `c1c1896fb8` | Oracle validation: fix DD-RH-099 invalid-block cache side effects. |
| `6af2d0ed4d` | Miner digidollar: fix DD-RH-100 oracle-price retry DoS. |
| `9a1628c55e` | Oracle fuzz: enable parser and price harnesses. |
| `32570f2bf1` | Validation digidollar: fix DD-RH-101 health metric side effects. |
| `8d78851fa9` | Oracle tests: refresh bundle timing and RH05 expectations. |
| `c49438caba` | RPC mining: fix DD-RH-102 oracle GBT schema. |
| `03c28004da` | Oracle mock: fix DD-RH-103 bundle timestamp signing. |
| `630ae01c5c` | Oracle regtest: fix DD-RH-104 mock bundle timestamps. |
| `d21b43c96a` | Functional tests: refresh DigiDollar oracle mining expectations. |
| `a1b1fe936d` | Record the completed Red Hornet security campaign. |
| `414b9f6f89` | Ignore local Red Hornet scratch-note files. |
| `cd3f6425b5` | RPC digidollar: fix DD-RH-058 invalid collateral quotes. |
| `9335a841c2` | Wallet digidollar: fix DD-RH-055 wrong send change. |
| `a96a988499` | RPC digidollar: fix DD-RH-057 overflow-prone estimate math. |
| `7d6ee67f0f` | Index digidollar: fix DD-RH-059 reordered mint accounting. |
| `3a88816293` | Tests digidollar: reset volatility in RH68 block-validity test. |
| `8454c57363` | Functional digidollar: align mint test with regtest bounds. |
| `c7e0887b3f` | Functional tests: refresh index prune boundaries. |
| `318fc8edb9` | Functional tests: register DigiDollar oracle scripts. |
| `3d879cb140` | Record continuation evidence for the final RC33 Red Hornet fixes. |

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
