# DigiByte Core v9.26.0-rc45 Release Notes

RC45 is a DigiDollar RC44 follow-up stabilization release candidate on the same public `testnet26` chain.

Development branch: `feature/digidollar-v1`

Release: https://github.com/DigiByte-Core/digibyte/releases/tag/v9.26.0-rc45

---

## Read This First

RC45 does not reset public DigiDollar testnet.

RC44 moved public testing to `testnet26` and finalized the 35-slot oracle roster with a 7-signature MuSig2 quorum. RC45 keeps the same chain, same ports, same activation parameters, same 35 active oracle slots, and same `v0x03` oracle bundle format while hardening final wallet recovery, operator key handling, OP_CHECKPRICE disablement, and DigiDollar health reconstruction before the next public rollout step.

Upgrade target:

- RC44 `testnet26` nodes should upgrade to RC45.
- Do not wipe `testnet26` just because of RC45.
- Do not return to `testnet25`; RC45 is still the RC44 public testnet chain.
- Existing oracle operators should keep their assigned testnet26 slot and key material unless separately coordinated.
- Back up wallets and oracle key material before upgrading.

What changed from RC44:

- DigiDollar wallet restore/rescan now recovers richer DD transaction history, including multi-output receives, DD change handling, and restored change classification.
- Local pending DigiDollar mints remain visible in the wallet UI/RPC instead of disappearing while they are still local or awaiting relay/confirmation.
- `senddigidollar` help and parsing were corrected so dual-unit help text cannot imply a 100x over-send.
- DigiDollar health reconstruction after startup was hardened and flushed before scans so restart state cannot poison consensus-health views.
- Residual RPC-cache consensus-health poisoning was fixed so RPC-facing health cache data cannot disagree with consensus state.
- OP_CHECKPRICE is deterministically disabled and the remaining dead price-hook surface was removed from init/docs/operator guidance.
- Oracle operators now have explicit oracle key export/import RPC support for safer wallet migration and recovery.
- Multi-oracle end-to-end coverage now detects already-mined MuSig2 bundles instead of masking restore/history failures.
- Operator, activation, wallet-integration, OP_CHECKPRICE, architecture, and roster documentation were aligned with the RC45 behavior.

What did not change:

- Testnet name remains `testnet26`.
- Data directory remains `testnet26`.
- Testnet P2P port remains `12033`.
- Default testnet RPC port remains `14026`.
- DigiDollar activation remains BIP9 bit 23, minimum height 600.
- Oracle roster remains 35 active slots, IDs `0` through `34`.
- Oracle quorum remains 7 signatures from the configured 35-key MuSig2 roster.
- Oracle bundle format remains `v0x03`.
- Mainnet activation status does not change.
- DigiDollar economic rules, ERR policy, DCA policy, address formats, and wallet database format do not change.

---

## Summary

RC45 is the final RC44 hardening pass for wallet recovery, oracle operator key operations, and consensus-health correctness.

The release focuses on failure modes that matter before broader public testing: restored wallets must show the same DigiDollar history after rescans, local pending mints must remain visible to users, health state must reconstruct deterministically after restart, OP_CHECKPRICE must stay fully disabled, and oracle operators need a supported export/import path for oracle key migration instead of fragile manual wallet handling.

RC45 is not a network reset and not an economic redesign.

---

## One-Line Fix Summary

- Wallet rescan: restored DigiDollar change history now comes back correctly after rescans.
- Multi-output receives: wallet restore now reconstructs DD receive history across multi-output transactions.
- Sender-side change: restored DD change is kept out of receive history so send rows stay semantically correct.
- Pending mints: local pending DigiDollar mints remain visible instead of vanishing while still pending.
- `senddigidollar` help: dual-unit help text now avoids the old 100x over-send ambiguity.
- Startup health: DigiDollar system-health reconstruction is flushed before wallet/state scans.
- Consensus health: RPC cache data can no longer poison residual consensus-health views.
- OP_CHECKPRICE: disabled deterministically and documented as disabled; dead price-hook init surface removed.
- Oracle key migration: added RPC support to export/import oracle keys for safer operator recovery.
- E2E coverage: multi-oracle tests now detect existing MuSig2 bundles and stop masking restore failures.
- Operator docs: oracle setup, activation, wallet integration, OP_CHECKPRICE, and roster docs were refreshed for RC45.

---

## Testnet26 Network Details

| Item | RC45 value |
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

Older operator notes that mention `testnet25`, port `12032`, incomplete oracle placeholders, or OP_CHECKPRICE mock-price behavior are stale for RC45. Use the values above.

Minimum RC45 operator checklist:

1. Back up wallets and oracle key material.
2. Install/run RC45.
3. Keep using `-testnet` and the existing `testnet26` datadir.
4. Keep P2P port `12033` open and advertised.
5. Confirm `getblockchaininfo` reports the testnet26 genesis hash above.
6. Confirm `getoracles` shows active oracle IDs `0` through `34`.
7. If migrating an oracle wallet/key, use the new oracle key export/import RPC flow instead of manually copying partial wallet state.
8. Confirm local DigiDollar pending/history state survives restart/rescan before reporting operator readiness.

---

## Validation Status

RC45 code validation completed on June 13, 2026 from `feature/digidollar-v1` at code commit `5a7b1e90623fb5d76e222c9cf58287dd2b1b3642`. Later release-note-only commits do not change consensus, wallet, RPC, Qt, or test behavior.

| Gate | Status |
| --- | --- |
| Build targets: `digibyted`, `qt/digibyte-qt`, `test/test_digibyte`, `test/fuzz/fuzz`, `qt/test/test_digibyte-qt` | PASS |
| Unit tests: `./src/test/test_digibyte --show_progress` | PASS, 3398 Boost test cases |
| Qt tests: `QT_QPA_PLATFORM=offscreen ./src/qt/test/test_digibyte-qt -platform offscreen` | PASS, 121 passed / 0 failed / 3 expected visual-QA skips |
| Functional tests: `python3 test/functional/test_runner.py --jobs=4` | PASS, 376 entries, 17 expected skips, runtime 688s |
| Multi-oracle E2E: `./test_multi_oracle_testnet.sh` | PASS, 430 checks passed, 0 failed, 0 warnings |
| Restored wallet DD history check | PASS, Bob original/restored both `82` rows: `mint:20`, `send:26`, `receive:22`, `redeem:12` |
| Focused rescan regression: `python3 test/functional/wallet_digidollar_rescan.py` | PASS |
| Selected DD/oracle/MuSig fuzz stdin smokes | PASS |
| DigiDollar operator-surface lint: `python3 test/lint/lint-digidollar-operator-surface.py` | PASS |
| Whitespace check: `git diff --check` | PASS |

Primary multi-oracle E2E log: `/tmp/digidollar_debug_logs/test_run_20260612_230833.log`.

---

## Commits Since RC44

- `9df937ff11` test harness: fix DD-FINAL-001 multi-oracle E2E gate masking restore failures
- `97aa968c9c` rpc digidollar: fix DD-FINAL-002 senddigidollar dual-unit help (100x over-send)
- `67760db8c8` digidollar health: fix DD-FINAL-003 reconstruct system-health at startup (AR-CONSENSUS-1)
- `c09c6e0fb5` digidollar health: fix DD-FINAL-004 residual AR-CONSENSUS-1 (RPC-cache poisons consensus health)
- `1a2cab68c6` digidollar health: harden DD-FINAL-003 startup reconstruction — flush before scan
- `80fc8a0e8f` release: bump version to v9.26.0-rc45
- `b9ddae031e` script: fix DD-FINAL-005 (AR-0) — deterministically disable OP_CHECKPRICE
- `3668971ab2` docs+init: reflect DD-FINAL-005 OP_CHECKPRICE disable; drop dead price hook
- `5569e0dd55` digidollar: keep local pending mints visible
- `f60ca16043` digidollar: add oracle key export import RPCs
- `85bd1c7321` docs: align digidollar op_checkprice surface
- `466ee04ac4` docs: refresh digidollar operator surface
- `d41bf158bb` docs: align wallet integration activation notes
- `035b308164` test: clarify disabled op_checkprice coverage
- `7bcfe516b6` docs: align activation and roster plan
- `0970f7f150` digidollar: restore multi-output receive history
- `87561e21fd` digidollar: keep restored change out of receive history
- `aed6bb6045` test: detect existing musig bundle in e2e
- `5a7b1e9062` digidollar: restore DD change history on rescan
- `c1e94d2696` release: add RC45 release notes
