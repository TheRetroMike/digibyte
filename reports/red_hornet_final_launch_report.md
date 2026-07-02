# Red Hornet Final DigiDollar Launch Audit - 2026-06-18

Status: PASS WITH LAUNCH READINESS ITEMS

Branch: `feature/digidollar-v1`
Starting HEAD: `c7b8f8c17401`
Final audit HEAD before report-only commit: `bfb2c27315`
Local run date: `2026-06-18` MDT
Primary ledger: `reports/red_hornet_final_ledger.md`
Prompt executed: `Z_RED_HORNET_FINAL_PROMPT.md`

## Executive Summary

The Red Hornet final DigiDollar/oracle audit was rerun from scratch against the
current `feature/digidollar-v1` tree. Historical Red Hornet reports were used
only as lead material; current conclusions below are based on this run's code
inspection, sub-agent audit passes, and test evidence.

Final result: no open confirmed production code-security, funds-flow, wallet,
Qt, RPC, oracle, MuSig2, P2P, activation, chainparams, mempool, restart,
restore, rescan, reindex, or fork-risk bug was found.

Two low-severity issues were confirmed and fixed:

- `DD-FINAL-001`: local multi-oracle harness could collide with an unrelated
  public testnet26 node on P2P port `12033`. Fixed in `ea00d6009d`
  (`test: harden DigiDollar multi-oracle harness`).
- `DD-FINAL-002`: tracked docs/operator wording had stale oracle-key, signer,
  liveness, and active-key language. Fixed in `bfb2c27315`
  (`docs: align DigiDollar oracle operator surface`).

No production consensus, wallet, RPC, Qt, oracle-protocol, or funds-flow code
was changed in this audit.

## Validation Rule Used

No issue was treated as real until it was validated against the current
repository by at least one of:

- a failing command or reachable current-code path;
- a live final-gate failure;
- a current docs/operator-surface mismatch with a lint guard;
- or a current test result that proved the behavior.

Unreached, stale, or prompt-only claims were rejected or reclassified rather
than fixed as bugs.

## Commits Made

- `ea00d6009d` - `test: harden DigiDollar multi-oracle harness`
- `bfb2c27315` - `docs: align DigiDollar oracle operator surface`

## Confirmed Issues Fixed

### DD-FINAL-001 - Multi-oracle harness port and coverage drift

The initial `./test_multi_oracle_testnet.sh` baseline failed before behavior
testing because Eve used public testnet26 P2P port `12033`, which was already
occupied by an unrelated live Qt testnet node.

Fix:

- moved Eve's local mini-testnet P2P port to `12036`;
- added a port preflight before datadir deletion and node launch;
- accepted the current top-level `getdigidollardeploymentinfo.status` response
  shape while retaining the older nested fallback;
- expanded the final harness mint wave to all current tiers `0..9`;
- corrected tier labels and minted-total output.

Validation:

- `./test_multi_oracle_testnet.sh`: PASS, 434 passed, 0 failed, 0 warnings.
  Log: `/tmp/digidollar_debug_logs/test_run_20260618_091035.log`.
- `bash -n test_multi_oracle_testnet.sh`: PASS.
- `git diff --check`: PASS.

### DD-FINAL-002 - Operator/docs surface drift

Tracked docs still contained stale wording around pre-start `getoraclepubkey`,
old signer/liveness language, and old 21-active-key architecture wording.

Fix:

- documented that wallet RPC paths can expose the wallet-stored oracle key
  before `startoracle`, with `is_running=false` until runtime start;
- replaced current "9 oracles" liveness wording with the current 7-threshold
  language;
- aligned architecture wording to 7 signatures from 35 configured active keys
  on mainnet and testnet26;
- added operator-surface lint guards for the stale strings.

Validation:

- `python3 test/lint/lint-digidollar-operator-surface.py`: PASS.
- `git diff --check`: PASS.

## Final Test Matrix

All final gates that were run in this audit passed after the fixes:

- Build: `make -C src -j"$(nproc)" digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt` - PASS.
- Full unit: `./src/test/test_digibyte --show_progress` - PASS, 3,398 test cases, no errors.
- Full functional: `test/functional/test_runner.py --jobs=4` - PASS, 376 entries with expected skips only.
- Multi-oracle E2E: `./test_multi_oracle_testnet.sh` - PASS, 434 checks passed, 0 failed, 0 warnings.
- Harness syntax: `bash -n test_multi_oracle_testnet.sh` - PASS.
- Operator-surface lint: `python3 test/lint/lint-digidollar-operator-surface.py` - PASS.
- Whitespace check: `git diff --check` - PASS.

The final multi-oracle E2E verified:

- 24 active oracle signers across 8 Qt wallet nodes;
- live 7-of-35 oracle consensus;
- `sendoracleprice` unavailable;
- exchange outlier filtering observed;
- v0x03 MuSig2 bundle on-chain with 96-byte script;
- all current collateral tiers `0..9`;
- early redemption and partial redemption rejection;
- successful tier 0 redemptions;
- transfer-chain accounting and network DD-supply invariants;
- wallet restart, backup/restore, reindex, rescan, descriptor restore, redeemed
  position inactivity, and DD transaction history restoration.

## Claims Rejected Or Reclassified

- The initial Eve bind failure was harness/environment overlap, not DigiDollar
  consensus or wallet breakage.
- The legacy `src/rpc/digidollar_transactions.cpp` file is not the registered
  live RPC surface.
- Health-monitor mock-height and hardcoded active-count paths are not reached by
  the registered public stats paths used by this audit.
- `ctx.coins == nullptr` collateral-release fallback is not reached by
  production mempool, block, or miner validation paths.
- The current tier contract is `0..9`; older `0..8` references are stale.

## Launch Assessment

Code status from this audit: PASS.

Launch readiness still depends on process and operations:

- do not rotate active oracle slots without a coordinated release or
  deterministic height-scheduled roster plan;
- collect sustained mainnet operator quorum evidence well above the
  7-signature threshold under restart, unlock, network churn, backup/restore,
  reindex, and rescan conditions;
- decide explicitly whether any future testnet minimum-mint change is desired.

The pre-existing untracked `RELEASE_v9.26.0-rc46.md` was outside this audit
change set and was left untouched.
