# DigiByte Core v9.26.0-rc43 Release Notes

RC43 is a DigiDollar RC42 follow-up stabilization release candidate on the same public `testnet25` chain.

Development branch: `feature/digidollar-v1`

Release: https://github.com/DigiByte-Core/digibyte/releases/tag/v9.26.0-rc43

---

## Read This First

RC43 does not reset public DigiDollar testnet.

RC41 moved public testing to `testnet25`. RC42 kept that network and added the active `digibyte-maxi` oracle slot. The RC43 fix pass keeps the same chain, same ports, same activation heights, same 35-slot oracle roster capacity, and same `v0x03` oracle bundle format while updating mainnet/testnet to 21 active oracle keys with a 7-signature quorum.

Upgrade target:

- RC42 `testnet25` nodes should upgrade to RC43.
- RC41 `testnet25` nodes can also upgrade directly to RC43.
- Do not wipe `testnet25` just because of RC43.
- Do not return to `testnet24`; RC43 is still the RC41/RC42 public testnet chain.
- Existing oracle operators should keep their assigned testnet25 slot and key material unless separately coordinated.

What changed from RC42:

- Rapid DigiDollar mint, send, and redeem wallet-state handling was hardened.
- Rapid DD stress coverage now exercises 20 mints, 20 redemptions, and 20 sends end to end.
- DigiDollar wallet-state fuzzing now covers rapid mint/send/redeem reject, abandon, and confirm orderings.
- The DigiDollar tab now shows a first-entry experimental risk warning with a "don't show again" option after acceptance.
- DigiDollar transaction details no longer open duplicate dialogs on double-click.
- DD Overview recent transaction columns now align cleanly for pending rows and mixed amount widths.
- DigiDollar Qt amount labels now use the consistent `<amount> $DD` format throughout overview, send, mint, redeem, vault, transaction, and dialog surfaces.
- Oracle operators can now run `createoraclekey` before DigiDollar activation to generate fresh mainnet oracle pubkeys, while real DigiDollar/oracle actions remain disabled until activation.
- Oracle startup validation logging is gated to avoid misleading startup noise before the node is ready.
- Mainnet and testnet oracle consensus now use 21 active slots and require 7 MuSig2 signatures.
- RC42 DGBstats, oracle onboarding, tooltip, modal, transaction-detail, and dark-mode fixes are carried forward.

What did not change:

- Testnet name remains `testnet25`.
- Testnet P2P port remains `12032`.
- Default testnet RPC port remains `14026`.
- DigiDollar testnet activation height remains `600`.
- Oracle testnet activation height remains `600`.
- Oracle epoch length remains `40` blocks.
- Oracle bundle format remains `v0x03`.
- Testnet active oracle roster is 21 keys.
- Oracle quorum is 7 signatures.
- Mainnet activation status does not change.
- Mainnet oracle operation remains disabled before activation; only local oracle key creation is allowed.
- DigiDollar economic rules, ERR policy, DCA policy, address formats, and wallet database format do not change.

---

## Summary

RC43 is the RC42 stabilization follow-up.

It focuses on real wallet-state failure modes found during rapid DigiDollar use: repeated DD sends, rapid batch mints, rapid redemptions, local/conflicting wallet transactions, false redeemed vault display, and the old `!wtx.InMempool()` crash path.

RC43 also finishes the RC42 Qt usability cleanup with the DigiDollar experimental warning, duplicate detail-dialog prevention, and DD Overview recent transaction column alignment.

For mainnet launch planning, RC43 also resolves the oracle-key chicken-and-egg issue: operators can generate fresh mainnet oracle keys before DigiDollar is active, without enabling oracle operation or any DigiDollar protocol action.

RC43 is not an economic redesign and not a new testnet reset.

---

## One-Line Fix Summary

- Rapid DD mints: serialized mint coin selection, signing, wallet commit, and DD position persistence so concurrent mints cannot reuse the same DGB inputs.
- Rapid DD sends: committed DD sends through the wallet relay path once and stopped unsafe fee-input chains from building on stale wallet state.
- Rapid DD redeem: rejected or stale redemption transactions are abandoned cleanly so wallet inputs release and vault state does not remain stuck.
- `!wtx.InMempool()` crash: recursive wallet abandonment now skips live confirmed/mempool descendants instead of aborting during parent removal.
- Sender DD history: outgoing DD change is no longer recorded as a sender-side receive before the send row exists.
- False redeemed vaults: rapid DD send stress now verifies unrelated vaults do not flip to `redeemed`.
- Rapid-state fuzzing: added a DigiDollar wallet-state model for mint/send/redeem confirm, reject, and abandon sequences.
- Multi-oracle stress: `test_multi_oracle_testnet.sh` now runs 20 rapid mints, 20 rapid redemptions, and 20 rapid 5 DD sends.
- DigiDollar experimental warning: first entry into the DigiDollar tab shows a clear risk warning with an accepted "don't show again" preference.
- Experimental warning copy: simplified the warning text to be direct, readable, and explicit about proceeding at the user's own risk.
- Duplicate DD details: DD transaction double-clicks now open one details dialog instead of two.
- DD Overview alignment: recent transaction amount/status/date columns now keep stable alignment for pending rows and different amount widths.
- `$DD` labels: Qt now formats DigiDollar amounts consistently as `<amount> $DD` across tabs, balances, dialogs, transaction rows, and details.
- Pre-activation oracle keys: `createoraclekey` now works before DigiDollar activation for local wallet key setup and mainnet pubkey collection.
- Activation safety: `startoracle`, price signing, mint, redeem, transfer, validation, and protocol state-changing RPCs remain blocked until DigiDollar is active.
- Oracle startup logs: startup validation is gated so early chainstate/config readiness does not produce misleading operator noise.
- RC42 carry-forward: DGBstats difficulty/oracle copy, Qt tooltip readability, green DD modal styling, DD transaction details theming, and tier-0 copy fixes remain included.

---

## Testnet25 Network Details

| Item | RC43 value |
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
| Oracle roster | 35 reserved slots, 21 active mainnet/testnet slots |
| Oracle quorum | 7 signatures from the configured active MuSig2 keyset |
| Oracle bundle format | `v0x03` MuSig2 aggregate bundle |

Older operator notes that mention `testnet24` or P2P port `12031` are stale for RC43. Use the values above.

RC43 carries forward RC42 testnet slot 17 for `digibyte-maxi` and adds active slots 18-20:

- Slot 17 `digibyte-maxi`: `03649d750bcad5b42b3dd0f11c8d98d62ed5afd515cd986663f81c35f086e58d47`
- Slot 18 Anthony: `0345f8cb22dfde6aff8f18552c338256e0df551ca2df007f6449d6da1dbb7f4d89`
- Slot 19 `mbah_jambon`: `031758a6d7f1f87c95d1a4a38415608d41463a504ea28da7c6129e2a9d654add42`
- Slot 20 Camden: `03018c81746d6ddc326c993d9f2e7f2015554e97a261f3b5fe637ac5098f421a4c`
- Quorum impact: mainnet/testnet quorum is 7 signatures from the active 21-key roster.

Minimum RC43 operator checklist:

1. Back up wallets and oracle key material.
2. Install/run RC43.
3. Keep using `-testnet` and the existing `testnet25` datadir.
4. Keep P2P port `12032` open and advertised.
5. Confirm `getblockchaininfo` reports the testnet25 genesis chain.
6. Confirm `getoracles` shows the expected 35-slot roster, with active configured operators online and reserve slots inactive.
7. Confirm assigned oracle slot/key material still matches the RC43/testnet25 chainparams roster.

---

## Validation Status

Focused RC43 validation completed on May 27, 2026 from `feature/digidollar-v1`.

| Gate | Status |
| --- | --- |
| Build: `make -C src -j4 digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt` | PASS |
| Unit tests: `./src/test/test_digibyte --show_progress` | PASS, 3387 test cases |
| Qt tests: `QT_QPA_PLATFORM=offscreen ./src/qt/test/test_digibyte-qt` | PASS |
| Functional tests: `test/functional/test_runner.py --jobs=4` | PASS, 371 listed jobs passed in 665 seconds runtime / 2470 seconds accumulated runtime |
| Fuzz smoke: all `./src/test/fuzz/fuzz` targets with deterministic seed corpus | PASS, 248 targets in 29 seconds |
| Multi-oracle testnet25 script: `./test_multi_oracle_testnet.sh` | PASS end to end, 383 total checks, 382 OK, 0 failed, warning-only live-market observations |
| DD Overview alignment QA: `DIGIBYTE_QT_SAVE_DD_OVERVIEW_QA=1 QT_QPA_PLATFORM=offscreen ./src/qt/test/test_digibyte-qt overviewRecentTransactionAmountIsRightAligned` | PASS, screenshot captured |
| Pre-activation oracle key gating: `test/functional/digidollar_rpc_gating.py` | PASS, `createoraclekey` allowed at DEFINED state and 30 protocol/action RPCs still blocked |
| Oracle key regression set: `test/functional/test_runner.py --jobs=3 digidollar_rpc_gating.py digidollar_oracle_keygen.py digidollar_encrypted_wallet.py digidollar_wave18_rpc_matrix.py` | PASS |
| DGBstats tests carried from RC42: `npm run test:run` | PASS, 23 files / 523 tests |
| DGBstats build carried from RC42: `npm run build` | PASS with pre-existing ESLint warnings |
| DGBstats E2E carried from RC42: `npm run test:e2e` | FAIL, broad pre-existing Playwright environment/data failures |
| DGBstats Server tests carried from RC42: `npm test` | PASS, 8 files / 152 tests |
| Whitespace check: `git diff --check` | PASS |

Important validation notes:

- The final multi-oracle run used live market data and passed all DigiDollar mint, redeem, transfer, persistence, reindex, restore, and oracle checks with 21 active local oracles and 7-of-21 consensus.
- The final multi-oracle script included 20 rapid tier-0 mints, 20 rapid redemptions, a 20-output DD self-fragmentation step, and 20 rapid 5 DD sends.
- The final multi-oracle script log was `/tmp/digidollar_debug_logs/test_run_20260527_080130.log`.
- The final full functional run used explicit `--jobs=4` and passed in 665 seconds runtime / 2470 seconds accumulated runtime.
- The post-RC43 oracle-key gating check proves `createoraclekey` is wallet-only pre-activation setup and does not loosen the activation gate around oracle operation or DigiDollar protocol actions.
- The DD Overview alignment screenshot was saved to `/tmp/digibyte_dd_overview_recent_alignment_qa.png`.
- DGBstats Playwright E2E remained red independently of the RC43 fixes, with broad loading-state, mocked-data, touch-target, browser-matrix, and route-specific expectations failing in this environment.

---

## Qt Visual QA

RC43 includes automated offscreen Qt tests and screenshot evidence for the new DigiDollar overview alignment fix.

New RC43 screenshot:

- DigiDollar Overview recent transaction alignment: `/tmp/digibyte_dd_overview_recent_alignment_qa.png`

RC42 visual QA carried forward:

- Global/custom tooltip readability: `/tmp/digibyte_tooltip_qa.png`
- DigiDollar transaction details dark mode: `/tmp/digibyte_dd_transaction_details_dark_qa.png`
- DigiDollar transaction details light mode: `/tmp/digibyte_dd_transaction_details_light_qa.png`
- DigiDollar coin control dark mode: `/tmp/digibyte_dd_coin_control_dark_qa.png`
- DigiDollar coin control light mode: `/tmp/digibyte_dd_coin_control_light_qa.png`
- DigiDollar payment request dark mode: `/tmp/digibyte_dd_receive_request_dark_qa.png`
- DigiDollar payment request light mode: `/tmp/digibyte_dd_receive_request_light_qa.png`
- DigiDollar address book dark mode: `/tmp/digibyte_dd_address_book_dark_qa.png`
- DigiDollar address book light mode: `/tmp/digibyte_dd_address_book_light_qa.png`

Visual QA covered dark-mode global tooltips, DigiDollar tooltips, long wrapped tooltip paths, pending transaction tooltip normalization, shutdown window contrast, DigiDollar Send Total DD contrast, locked-wallet redeem state, DD Send local Note copy, DD Overview double-click navigation, DD Transactions double-click details, green DD-specific modal styling, and DD Overview recent transaction text alignment.

---

## Mainnet Oracle Key Collection

RC43 allows fresh mainnet oracle pubkeys to be collected before DigiDollar activates.

Only `createoraclekey <oracle_id>` is allowed before activation, and only as local wallet key management. It generates and stores the operator's private key in their wallet, then returns the compressed public key for chainparams inclusion. Nothing is broadcast, no oracle starts, no prices are signed, and no DigiDollar state changes.

Expected mainnet operator flow:

1. Install the pre-activation RC43-or-later build.
2. Create or load the intended oracle wallet and unlock it if encrypted.
3. Run `createoraclekey <oracle_id>`.
4. Send only the returned `pubkey` to the maintainer.
5. Keep the wallet backed up; it contains the private oracle key.
6. Wait for the final activation release that hardcodes the collected pubkeys.

Still blocked before activation: `startoracle`, price signing, oracle operation, minting, redeeming, sending/transferring `$DD`, validation/state mutation, and all other DigiDollar protocol actions.

---

## Commit Summary Since RC42

- `4fc8f7d83e` fix: allow pre-activation oracle key generation
- `f53bbc7c78` fix: standardize DigiDollar Qt currency labels
- `06903d2b39` fix: gate oracle startup validation logs
- `b48a0aa6ce` feat: warn before entering DigiDollar tab
- `e459a96f80` fix: avoid duplicate DigiDollar transaction detail dialogs
- `4f4c7a415e` copy: clarify DigiDollar experimental warning
- `1a6836f4e6` release: bump version to v9.26.0-rc43
- `b53f7ac5ba` fix: harden rapid DigiDollar wallet state transitions
- `13b1de4788` test: fuzz rapid DigiDollar wallet state transitions
- `13953fdf6e` test: stress rapid DigiDollar ops in oracle testnet
- `7eff73e5eb` docs: update RC42 rapid wallet-state validation
- `4acd8cee62` fix: align DigiDollar overview recent transaction columns

Related DGBstats commits carried forward from RC42:

- `/home/jared/Code/dgbstats` `931984f3f48a473d1b15095d35858e150e56512e` fixed fractional testnet difficulty display and Core algo-name normalization.
- `/home/jared/Code/dgbstats` `8e68bcebc1437c0a21677dcb0c145dee194315b3` updated oracle/testnet25 onboarding copy.

---

## Notes For Testers

Please focus RC43 testing on:

- Upgrading RC42 nodes without wiping `testnet25`.
- Rapid batches of up to 20 mints, sends, and redeems without mining between RPC calls.
- Repeated small DD sends from an existing DigiDollar balance.
- Rapid batch 100 DD mints from wallets with many DGB UTXOs.
- Redeems after rapid mint/send activity and restart.
- Verifying DD vaults do not show `Redeemed` unless a redeem actually confirmed.
- DigiDollar first-entry experimental warning wording and "don't show again" behavior.
- DD Transactions double-click details opening exactly one dialog.
- DD Overview recent transaction row alignment for pending and confirmed rows.
- DD Overview recent transaction double-click navigation to the DD Transactions tab.
- Consistent `<amount> $DD` labels across DigiDollar overview, send, mint, redeem, vault, transaction, and dialog surfaces.
- Pre-activation `createoraclekey <oracle_id>` on a not-yet-active chain, verifying it returns a fresh public key while `startoracle` and all real DigiDollar actions stay blocked.
- Tooltips in light and dark mode across normal DGB pages and DigiDollar pages.
- DigiDollar coin selection, payment request, and address book dialogs in light and dark mode.
- DGBstats testnet difficulty display and oracle/testnet25 operator copy.

Oracle operators should keep using `testnet25`, P2P port `12032`, and their assigned RC42/RC43 testnet slots.

---

## Known Risks

- RC43 does not include mainnet activation. Mainnet launch still requires the explicit release and activation decision.
- Mainnet reserve oracle slots remain placeholders until operators provide mainnet oracle pubkeys and a later release adds those keys to chainparams. RC43 now supports generating those keys before activation through `createoraclekey`.
- If fewer than 9 valid active oracle operators are online and fresh, new oracle bundles should fail closed.
- DGBstats Playwright E2E remains red independently of the RC43 fixes; the DGBstats changes carried into RC43 are covered by passing Vitest tests and production build.

---

## Bottom Line

RC43 is the RC42 stabilization follow-up for the same public `testnet25`.

It keeps the testnet, oracle quorum, and DigiDollar economics unchanged, hardens rapid DigiDollar wallet-state behavior, improves stress/fuzz coverage, finishes the requested Qt warning/dialog/alignment/`$DD` cleanup, enables safe pre-activation mainnet oracle pubkey collection, and leaves the network ready for continued testnet25 validation.
