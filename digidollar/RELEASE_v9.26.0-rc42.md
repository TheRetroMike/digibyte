# DigiByte Core v9.26.0-rc42 Release Notes

RC42 is a DigiDollar RC41 follow-up bug-fix, wallet/RPC hardening, Qt usability/visual-QA, oracle-doc, and DGBstats release candidate on the same public `testnet25` chain.

Development branch: `feature/digidollar-v1`

Release: https://github.com/DigiByte-Core/digibyte/releases/tag/v9.26.0-rc42

---

## Read This First

RC42 does not reset public DigiDollar testnet.

RC41 moved public testing to `testnet25`. RC42 keeps that same network, same genesis, same ports, same activation heights, same 35-slot oracle roster capacity, and same oracle bundle format. Operators upgrading from RC41 should upgrade binaries and keep using their existing `testnet25` data unless they are intentionally starting a fresh node.

Upgrade target:

- RC41 `testnet25` nodes should upgrade to RC42.
- Do not wipe `testnet25` just because of RC42.
- Do not return to `testnet24`; RC42 is still the RC41 public testnet chain.
- Existing oracle operators should keep their assigned RC41/testnet25 slot and key material unless separately coordinated; RC42 adds `digibyte-maxi` at testnet slot 17.

What changed from RC41:

- Wallet/RPC mint and redeem relay handling was hardened.
- Rapid DigiDollar mint, send, and redeem bursts were hardened so batches of 20 settle without duplicate inputs, local conflicts, abandoned sends, or false redeemed vaults.
- Fragmented UTXO mint consolidation was completed.
- DigiDollar Qt send/status/redeem/double-click workflows were fixed.
- DigiDollar now shows a first-entry experimental risk warning with a "don't show again" option after acceptance.
- Qt tooltip and DigiDollar modal styling were visually QAed and corrected.
- Tier-0/minimum-mint user feedback and validation were clarified.
- DGBstats oracle/testnet25 copy and fractional difficulty display were fixed.
- The local multi-oracle testnet harness was kept runnable with live price movement and now stress-tests rapid DigiDollar mints, sends, and redeems.
- Testnet oracle slot 17 was assigned to active operator `digibyte-maxi` without changing the 9-signature oracle quorum.

What did not change:

- Testnet name remains `testnet25`.
- Testnet P2P port remains `12032`.
- Default testnet RPC port remains `14026`.
- DigiDollar testnet activation height remains `600`.
- Oracle testnet activation height remains `600`.
- Oracle epoch length remains `40` blocks.
- Oracle bundle format remains `v0x03`.
- Mainnet activation status does not change.
- DigiDollar economic rules, ERR policy, DCA policy, address formats, and wallet database format do not change.

---

## Summary

RC42 closes the RC41 issue-validation pass.

It fixes confirmed RC41 regressions in DigiDollar wallet/RPC commit ordering, rapid mint/send/redeem wallet state, fragmented mint consolidation, Qt status refresh, locked-wallet redeem state, dark-mode/readability styling, DigiDollar transaction details, DigiDollar modal theming, minimum mint copy, tier-0 validation, and DGBstats testnet/oracle displays.

RC42 is not an economic redesign and not a new testnet reset.

---

## One-Line Fix Summary

- Pending mint/change visibility: added regression coverage proving mint DGB change remains wallet-owned/spendable and pending DigiDollar state remains visible.
- Mint wallet relay: fixed duplicate broadcast-before-wallet-commit so RPC/Qt mints commit through the wallet relay path once and avoid `!wtx.InMempool()`.
- Fragmented UTXO minting: fixed auto-consolidation to run enough passes and retry minting from usable consolidation outputs.
- DigiDollar send/status refresh: moved slow refresh paths off the blocking UI path and added explicit DigiDollar update signals.
- Startup position validation: kept the expected pre-chainstate readiness warning harmless and added retry validation once chainstate is ready.
- Redeem locked-wallet state: refreshed redeem UI on wallet lock/unlock and clarified when a redeemable vault still cannot sign.
- Dark-mode tooltips: normalized native and custom tooltip rendering so dark-mode tooltips are readable across normal wallet and DigiDollar views.
- Shutdown dialog contrast: added explicit dark-mode shutdown dialog styling.
- DigiDollar Send Total DD contrast: fixed dark-mode label/value contrast for the Total DD row.
- Literal `<qt>` tooltip tags: stripped Qt rich-text envelopes before custom tooltip escaping so wrapped tooltips no longer show raw tags.
- Minimum mint amount copy: below-minimum mint feedback now reports the actual configured chain minimum.
- Tier-0 / 1-hour mint clarity: accepted tier 0 in wallet validation and separated lock duration from effective redeem availability in user copy.
- DGBstats testnet difficulties: fixed fractional testnet algo difficulty rendering and Core algo-name normalization.
- DGBstats oracle onboarding: updated oracle/testnet25 copy to six active exchanges, v0x03-only bundles, 60-second exchange fetch/broadcast, and assigned-slot coordination.
- DigiDollar Overview double-click: double-clicking a recent DigiDollar transaction now switches to DD Transactions and focuses the matching row.
- DigiDollar Transactions double-click: double-clicking a DD transaction now opens a DigiDollar details window like normal DGB transactions.
- DigiDollar duplicate details: prevented DD transaction double-clicks from opening two detail dialogs for one row.
- DigiDollar transaction details theme: DD transaction details use green DigiDollar styling in light and dark mode instead of normal DGB blue.
- DigiDollar modal dialogs: DD coin selection, payment request, and address book dialogs now force green DigiDollar light/dark styling.
- DigiDollar experimental warning: first entry into the DigiDollar tab shows a direct risk warning with an accepted "don't show again" preference.
- DigiDollar Send note field: renamed the misleading local `Label` field to local `Note` and updated tooltip/copy.
- Rapid batch mint state: serialized mint selection/commit with DD wallet state so concurrent mints cannot reuse the same DGB inputs.
- Rapid DD send state: committed DD sends through the wallet once, rejected unsafe fee-input chains, and stopped outgoing change from recording as sender-side receives.
- Rapid redeem state: rejected or stale redemption transactions are abandoned cleanly so inputs release and vaults do not get stuck pending.
- Wallet abandonment crash: recursive abandonment now skips live confirmed/mempool descendants instead of aborting on `!wtx.InMempool()`.
- Rapid-state fuzzing: added a DigiDollar rapid mint/send/redeem state-model fuzz target covering reject, abandon, and confirm orderings.
- Redeem wallet relay: fixed RPC redeem to commit through wallet relay once and make rapid duplicate redeems fail cleanly.
- Owner-key guard test: corrected the source guard that proves DigiDollar owner keys are stored before wallet-owned commit.
- Multi-oracle harness: added 20 rapid mints, 20 rapid redemptions, and 20 rapid sends to `test_multi_oracle_testnet.sh`.
- Testnet oracle slot 17: added active operator `digibyte-maxi` with pubkey `03649d750bcad5b42b3dd0f11c8d98d62ed5afd515cd986663f81c35f086e58d47`; testnet active roster is now 18 keys and quorum remains 9 signatures.
- RC41 ledger: documented final validation status, test evidence, visual QA evidence, and commit hashes for all 18 tracked issues.

---

## Testnet25 Network Details

| Item | RC42 value |
| --- | --- |
| Testnet name | `testnet25` |
| Data directory | `testnet25` |
| Genesis hash | `0x901d46e44cd40764de5ce383717b0d6afd96190e2c6b931a4737ebc8cda96df4` |
| Merkle root | `0xd3ba96686218ada443cc6ad23563b0e6a5aa4990dc8d8e6c0c3ba5dd0ef7538b` |
| Genesis time | `2026-05-21 20:00:00 UTC` |
| Genesis nonce | `384415` |
| Network magic | `fe c5 b8 e6` |
| Default P2P port | `12032` |
| Default RPC port | `14026` |
| DigiDollar activation height | `600` |
| Oracle activation height | `600` |
| Oracle epoch length | `40` blocks |
| Oracle roster | 35 reserved slots, 18 active testnet slots in RC42 |
| Oracle quorum | 9 signatures from the configured active MuSig2 keyset |
| Oracle bundle format | `v0x03` MuSig2 aggregate bundle |

Older operator notes that mention `testnet24` or P2P port `12031` are stale for RC42. Use the values above.

RC42 also activates testnet slot 17 for `digibyte-maxi`. This adds the slot-17 x-only key to `consensus.vOraclePublicKeys`, raises the testnet active key count to 18, marks the slot active in `vOracleNodes`, and keeps the oracle quorum at 9 signatures.

Minimum RC42 operator checklist:

1. Back up wallets and oracle key material.
2. Install/run RC42.
3. Keep using `-testnet` and the existing `testnet25` datadir.
4. Keep P2P port `12032` open and advertised.
5. Confirm `getblockchaininfo` reports the testnet25 genesis chain.
6. Confirm `getoracles` shows the expected 35-slot roster, with active configured operators online and reserve slots inactive.
7. Confirm assigned oracle slot/key material still matches the RC42/testnet25 chainparams roster.

---

## Validation Status

Focused RC42 validation completed on May 26, 2026 from `feature/digidollar-v1`. Rapid-state follow-up validation completed on May 27, 2026 from the same branch.

| Gate | Status |
| --- | --- |
| Build: `make -C src -j4 digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt` | PASS |
| Unit tests: `./src/test/test_digibyte --show_progress` | PASS, 3387 test cases |
| Qt tests: `QT_QPA_PLATFORM=offscreen ./src/qt/test/test_digibyte-qt` | PASS |
| Functional tests: `test/functional/test_runner.py --jobs=4` | PASS, 371 listed jobs passed in 665 seconds runtime / 2470 seconds accumulated runtime |
| Fuzz smoke: all `./src/test/fuzz/fuzz` targets with deterministic seed corpus | PASS, 248 targets in 29 seconds |
| Multi-oracle testnet25 script: `./test_multi_oracle_testnet.sh` | PASS end to end, 383 total checks, 382 OK, 0 failed, warning-only live-market observations |
| DGBstats tests: `npm run test:run` | PASS, 23 files / 523 tests |
| DGBstats build: `npm run build` | PASS with pre-existing ESLint warnings |
| DGBstats E2E: `npm run test:e2e` | FAIL, broad pre-existing Playwright environment/data failures |
| DGBstats Server tests: `npm test` | PASS, 8 files / 152 tests |
| Whitespace check: `git diff --check` | PASS |

Important validation notes:

- The final multi-oracle run used live market data and passed all DigiDollar mint, redeem, transfer, persistence, reindex, restore, and oracle checks with 18 active local oracles and unchanged 9-of-35 consensus.
- The final multi-oracle script included 20 rapid tier-0 mints, 20 rapid redemptions, a 20-output DD self-fragmentation step, and 20 rapid 5 DD sends.
- The final multi-oracle script log was `/tmp/digidollar_debug_logs/test_run_20260527_080130.log`.
- The functional suite reported environment-gated skips only and warned that `feature_assumeutxo.py` and `feature_assumevalid.py` are not in the configured test list.
- The final full functional run used explicit `--jobs=4` and passed in 665 seconds runtime / 2470 seconds accumulated runtime.
- DGBstats Playwright E2E remained red independently of the RC42 fixes, with broad loading-state, mocked-data, touch-target, browser-matrix, and route-specific expectations failing in this environment.

---

## Qt Visual QA

RC42 included automated offscreen Qt tests and X11 visual QA for the affected wallet surfaces.

Screenshots inspected after the fixes:

- Global/custom tooltip readability: `/tmp/digibyte_tooltip_qa.png`
- DigiDollar transaction details dark mode: `/tmp/digibyte_dd_transaction_details_dark_qa.png`
- DigiDollar transaction details light mode: `/tmp/digibyte_dd_transaction_details_light_qa.png`
- DigiDollar coin control dark mode: `/tmp/digibyte_dd_coin_control_dark_qa.png`
- DigiDollar coin control light mode: `/tmp/digibyte_dd_coin_control_light_qa.png`
- DigiDollar payment request dark mode: `/tmp/digibyte_dd_receive_request_dark_qa.png`
- DigiDollar payment request light mode: `/tmp/digibyte_dd_receive_request_light_qa.png`
- DigiDollar address book dark mode: `/tmp/digibyte_dd_address_book_dark_qa.png`
- DigiDollar address book light mode: `/tmp/digibyte_dd_address_book_light_qa.png`

Visual QA covered dark-mode global tooltips, DigiDollar tooltips, long wrapped tooltip paths, pending transaction tooltip normalization, shutdown window contrast, DigiDollar Send Total DD contrast, locked-wallet redeem state, DD Send local Note copy, DD Overview double-click navigation, DD Transactions double-click details, and green DD-specific modal styling in dark and light mode.

---

## Commit Summary Since RC41

- `ef3bb06ab6` release: bump version to v9.26.0-rc42
- `139e8ecb55` test: cover DigiDollar mint change and pending state
- `f148f5b4e6` fix: commit DigiDollar mint transactions through wallet relay once
- `33ca60e94f` fix: complete fragmented UTXO mint consolidation
- `d7097b05f6` fix: commit DigiDollar redeem transactions through wallet relay once
- `515ca6d8f4` fix: refresh redeem state when wallet lock state changes
- `cb44ecde3d` fix: correct DigiDollar send dark-mode total contrast
- `f1257101d0` fix: style shutdown dialog for dark mode
- `872126c66a` fix: normalize Qt tooltip rendering and dark-mode contrast
- `ead2b2e3f7` fix: rename DigiDollar send label field to local note
- `95a3a1da59` feat: open DigiDollar recent transaction details
- `2d8b0808de` fix: keep DigiDollar send and status refresh responsive
- `b4152410f3` fix: report DigiDollar minimum mint amount for below-min input
- `8c82100b23` fix: accept tier 0 wallet validation and clarify effective unlock
- `402c9cdd45` fix: retry DigiDollar position validation after startup readiness
- `65f15d4775` fix: clarify DigiDollar local transaction state
- `1aa0e43334` test: align DigiDollar owner key commit guard
- `1f50d91f89` docs: record RC41 issue validation results
- `8da7c73b97` fix: keep local multi-oracle testnet harness runnable
- `a5d8dd467f` fix: keep DigiDollar send total label dark
- `64c72f2a4e` fix: normalize Qt tooltip rendering
- `d0605a19ef` feat: open DigiDollar transaction details
- `204fd142a2` docs: update RC41 visual QA evidence
- `a28c7e2d76` fix: theme DigiDollar transaction details green
- `8ebab1fdae` docs: record DD transaction detail theme QA
- `87463868bb` DOC Cleanup
- `8dcf61c097` fix: force DigiDollar detail dialog green theme
- `5cfea2fea5` docs: record DD detail dialog fallback QA
- `d57b7875e8` fix: theme DigiDollar modal dialogs green
- `7319a73947` docs: record DD modal visual QA
- `14da7a8d9e` doc: finalize RC42 release notes
- `8e9b4b4c6a` test: remove brittle DigiDollar RPC timing assertion
- `f7f5e5a1ee` chainparams: add digibyte-maxi testnet oracle
- `06903d2b39` fix: gate oracle startup validation logs
- `b48a0aa6ce` feat: warn before entering DigiDollar tab
- `e459a96f80` fix: avoid duplicate DigiDollar transaction detail dialogs
- `4f4c7a415e` copy: clarify DigiDollar experimental warning
- `1a6836f4e6` release: bump version to v9.26.0-rc43
- `b53f7ac5ba` fix: harden rapid DigiDollar wallet state transitions
- `13b1de4788` test: fuzz rapid DigiDollar wallet state transitions
- `13953fdf6e` test: stress rapid DigiDollar ops in oracle testnet

Related DGBstats commits:

- `/home/jared/Code/dgbstats` `931984f3f48a473d1b15095d35858e150e56512e` fixed fractional testnet difficulty display and Core algo-name normalization.
- `/home/jared/Code/dgbstats` `8e68bcebc1437c0a21677dcb0c145dee194315b3` updated oracle/testnet25 onboarding copy.

---

## Notes For Testers

Please focus RC42 testing on:

- Upgrading RC41 nodes without wiping `testnet25`.
- Rapid mint and redeem attempts from RPC and Qt.
- Rapid batches of up to 20 mints, sends, and redeems without mining between RPC calls.
- Fragmented UTXO minting and consolidation.
- DigiDollar send/status refresh while the wallet is busy.
- Redeem screens when a vault is redeemable but the encrypted wallet is locked.
- Tooltips in light and dark mode across normal DGB pages and DigiDollar pages.
- DD Overview recent transaction double-click navigation.
- DD Transactions double-click details in light and dark mode.
- DigiDollar coin selection, payment request, and address book dialogs in light and dark mode.
- Tier-0 / 1-hour mint confirmation copy and effective unlock expectations.
- DGBstats testnet difficulty display and oracle/testnet25 operator copy.

Oracle operators should keep using `testnet25`, P2P port `12032`, and their assigned RC42/testnet25 slots.

---

## Known Risks

- RC42 does not include mainnet activation. Mainnet launch still requires the explicit release and activation decision.
- Mainnet reserve oracle slots remain placeholders until operators provide mainnet oracle keys and a later release adds those keys to chainparams.
- If fewer than 9 valid active oracle operators are online and fresh, new oracle bundles should fail closed.
- DGBstats Playwright E2E remains red independently of the RC42 fixes; the DGBstats changes in this pass are covered by passing Vitest tests and production build.

---

## Bottom Line

RC42 is the RC41 stabilization release candidate for the same public `testnet25`.

It keeps the testnet and DigiDollar economics unchanged, fixes the validated RC41 wallet/RPC and Qt issues, completes the requested visual QA cleanup, updates DGBstats/operator copy, and leaves the network ready for continued testnet25 validation.
