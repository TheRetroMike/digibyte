# Red Hornet Final Audit Rerun - 2026-06-18

Branch: `feature/digidollar-v1`
Starting HEAD: `c7b8f8c17401`
Final audit HEAD before report-only commit: `bfb2c27315`
Mode: fresh validation-first rerun from `Z_RED_HORNET_FINAL_PROMPT.md`
Local run date: `2026-06-18` MDT

This section is the active ledger for the current rerun. Older sections below
remain historical leads only and are not treated as current proof unless this
run revalidates them against the current tree.

## Current Verdict

Status: PASS WITH LAUNCH READINESS ITEMS.

No confirmed production DigiDollar consensus, funds-flow, wallet, Qt, RPC,
oracle price, MuSig2 bundle, P2P, activation, chainparams, mempool, restart,
restore, rescan, reindex, or fork-risk bug was found in the current tree.

Two low-severity audit/operator-surface issues were confirmed and fixed:

- `DD-FINAL-001` - audit/test plumbing. Commit `ea00d6009d`
  (`test: harden DigiDollar multi-oracle harness`).
- `DD-FINAL-002` - tracked docs/operator wording drift. Commit `bfb2c27315`
  (`docs: align DigiDollar oracle operator surface`).

The pre-existing untracked `RELEASE_v9.26.0-rc46.md` was present before this
audit work and was not touched.

## Baseline And Final Gate Evidence

Baseline commands and results:

- `git status --short`: initial status showed only existing untracked
  `RELEASE_v9.26.0-rc46.md`.
- `make -C src -j"$(nproc)" digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt`:
  PASS.
- `./src/test/test_digibyte --show_progress`: PASS, 3,398 test cases, no
  errors detected.
- `test/functional/test_runner.py --jobs=4`: PASS, 376 entries with expected
  skips only.
- Initial `./test_multi_oracle_testnet.sh`: FAIL before audit waves. Eve could
  not bind the hard-coded private node P2P port `12033` because an unrelated
  public testnet26 Qt node was already listening on `12033` and `14126`.
- `./test_multi_oracle_testnet.sh` after the first DD-FINAL-001 repair: PASS,
  431 total tests, 431 passed, 0 failed, 0 warnings. Log:
  `/tmp/digidollar_debug_logs/test_run_20260618_083223.log`.
- Final `./test_multi_oracle_testnet.sh` after the tier-9 harness coverage
  repair: PASS, 434 total tests, 434 passed, 0 failed, 0 warnings. Log:
  `/tmp/digidollar_debug_logs/test_run_20260618_091035.log`.
- Final `bash -n test_multi_oracle_testnet.sh`: PASS.
- Final `python3 test/lint/lint-digidollar-operator-surface.py`: PASS.
- Final `git diff --check`: PASS.

Final multi-oracle evidence included:

- 24 active oracle signers distributed across 8 Qt wallet nodes.
- 7-of-35 oracle consensus verified with live price `$0.002581`.
- `sendoracleprice` confirmed unavailable.
- Live exchange outlier filter observed in logs.
- Continued oracle consensus after live refresh.
- On-chain v0x03 MuSig2 bundle found with prefix `6abf0103` and 96-byte script.
- All current collateral tiers `0..9` minted successfully, including tier 9.
- Tier 0 unlock, early-redemption rejection, partial-redemption rejection, two
  successful tier 0 redemptions, transfer chains, network DD-supply invariant
  checks, wallet restart, backup/restore, reindex, rescan, descriptor export and
  import restore, redeemed-position inactivity, and DD history restoration.

## Confirmed Issues Fixed

### DD-FINAL-001 - Local multi-oracle harness could collide with public testnet

Severity: Low. Affected invariant: final audit/test plumbing, not production
DigiDollar consensus.

Reachability: any machine already running a normal testnet26 node on public P2P
port `12033` could make the required local 8-node mini-testnet gate fail before
DigiDollar behavior was tested.

Evidence:

- Initial `./test_multi_oracle_testnet.sh` failed because Eve could not bind
  `0.0.0.0:12033`.
- [test_multi_oracle_testnet.sh](/home/jared/Code/digibyte/test_multi_oracle_testnet.sh:131)
  now assigns Eve to private harness port `12036`.
- [test_multi_oracle_testnet.sh](/home/jared/Code/digibyte/test_multi_oracle_testnet.sh:280)
  adds a reusable occupied-port preflight.
- [test_multi_oracle_testnet.sh](/home/jared/Code/digibyte/test_multi_oracle_testnet.sh:932)
  runs that preflight before deleting datadirs and launching nodes.
- [test_multi_oracle_testnet.sh](/home/jared/Code/digibyte/test_multi_oracle_testnet.sh:1321)
  accepts the current top-level `getdigidollardeploymentinfo.status` response
  shape while preserving the older nested fallback.
- [test_multi_oracle_testnet.sh](/home/jared/Code/digibyte/test_multi_oracle_testnet.sh:1461)
  extends the all-tier mint wave to current tiers `0..9`.
- [test_multi_oracle_testnet.sh](/home/jared/Code/digibyte/test_multi_oracle_testnet.sh:2439)
  prints the corrected total minted value after tier 9 coverage.

Fix: isolated Eve from the public testnet26 default port, added a mini-testnet
port preflight, fixed deployment-status parsing, and aligned the harness output
and mint loop with the current 10-tier contract.

Commit: `ea00d6009d` - `test: harden DigiDollar multi-oracle harness`

Validation:

- `./test_multi_oracle_testnet.sh`: PASS, 434/434. Log:
  `/tmp/digidollar_debug_logs/test_run_20260618_091035.log`.
- `bash -n test_multi_oracle_testnet.sh`: PASS.
- `git diff --check`: PASS.

### DD-FINAL-002 - Tracked operator/docs surface had stale oracle wording

Severity: Low. Affected invariant: operator-facing documentation and launch
communication accuracy, not production consensus.

Reachability: tracked docs still described stale operator behavior or old
quorum language that conflicts with current code and the final gate.

Evidence:

- [DIGIDOLLAR_ORACLE_SETUP.md](/home/jared/Code/digibyte/DIGIDOLLAR_ORACLE_SETUP.md:352)
  now documents that wallet RPC paths can show the wallet-stored oracle key
  before `startoracle`, with `is_running=false` until runtime start.
- [DIGIDOLLAR_WALLET_INTEGRATION.md](/home/jared/Code/digibyte/DIGIDOLLAR_WALLET_INTEGRATION.md:492)
  has the same current `getoraclepubkey` operator surface.
- [ORACLE_BUNDLE_EXPLAINER.md](/home/jared/Code/digibyte/ORACLE_BUNDLE_EXPLAINER.md:343)
  no longer frames the current signer set as "The 9 Oracles".
- [ORACLE_BUNDLE_EXPLAINER.md](/home/jared/Code/digibyte/ORACLE_BUNDLE_EXPLAINER.md:695)
  now describes fail-closed liveness below 7 valid mainnet/testnet V1 oracles.
- [ARCHITECTURE.md](/home/jared/Code/digibyte/ARCHITECTURE.md:1184)
  and [ARCHITECTURE.md](/home/jared/Code/digibyte/ARCHITECTURE.md:1185)
  now state 7 signatures from 35 configured active keys for mainnet and
  testnet26.
- [test/lint/lint-digidollar-operator-surface.py](/home/jared/Code/digibyte/test/lint/lint-digidollar-operator-surface.py:132)
  guards the stale active-key wording from returning.

Fix: updated the tracked docs and added lint guards for the stale strings.

Commit: `bfb2c27315` - `docs: align DigiDollar oracle operator surface`

Validation:

- `python3 test/lint/lint-digidollar-operator-surface.py`: PASS.
- `git diff --check`: PASS.

## Wave Coverage Summary

Five independent audit roles plus the main synthesis pass reviewed the current
tree from the required docs and current code:

- Production plumbing and activation: no production code bug found in build,
  activation, chainparams, signer rosters, or mixed-node behavior.
- Oracle price, MuSig2, and P2P: no reachable oracle/bundle consensus bug found.
- Funds flow, wallet, RPC, and restore: no mint, transfer, redeem, collateral,
  rescan, restore, or live RPC bug found.
- Qt/operator/docs surface: no Qt fund-flow bug found; stale docs listed above
  fixed.
- Fork-risk and launch readiness: no current fork trigger found; roster changes
  remain consensus-sensitive launch process items.

## Rejected Or Reclassified Leads

- The Eve bind failure was not a DigiDollar consensus or wallet failure. It was
  local harness port overlap with an unrelated public testnet26 Qt process.
- `src/rpc/digidollar_transactions.cpp` remains legacy/unregistered and is not
  the live RPC surface for this audit.
- `SystemHealthMonitor::GetHealthReport()` mock-height paths and the hardcoded
  active-oracle count are not reached by the registered public stats paths used
  by `getdigidollarstats` or `getprotectionstatus`; this is low-priority
  telemetry cleanup, not a launch-blocking consensus issue.
- Old comments about `skipOracle` and older red-team notes are stale against
  current tests and code.
- The `ctx.coins == nullptr` collateral-release fallback is not reached by
  production mempool, block, or miner validation paths that pass coins.
- Compact individual price attestations are not independently chain-bound, but
  the accepted on-chain v0x03 aggregate bundle is validated and chain-bound;
  this remains a protocol-design note, not a reachable current bug.
- Claims that tiers end at `0..8` are stale. Current code and the final harness
  validate tiers `0..9`.

## Launch Readiness Items

These are not code bugs from this audit, but they remain launch-process
requirements:

- Do not add, remove, or rotate active oracle slots without a coordinated
  release or deterministic height-scheduled roster plan.
- Collect sustained mainnet operator quorum evidence well above the 7-signature
  threshold across restarts, wallet unlocks, network churn, backup/restore, and
  reindex/rescan workflows.
- Decide explicitly whether any future testnet minimum-mint parameter change is
  a product goal; it was not changed in this audit.

# Red Hornet Final Audit Completion - Restore-History Fix - 2026-06-13 UTC

Branch: `feature/digidollar-v1`
Mode: validation-first 24-wave rerun from `Z_RED_HORNET_FINAL_PROMPT.md`
Local run date: `2026-06-12` MDT

This top section is the current result. Older sections below are retained as
historical audit notes and lead material, but this section supersedes any older
statement that restored-wallet DD receive-history category drift was still open.

## Current Fix Completed

Confirmed bug:

- Descriptor-restored wallets could rebuild the same DD balance and positions as
  the original wallet while producing different DD transaction history category
  counts.
- The full Qt multi-oracle E2E reproduced the issue before this fix: Bob had
  `82` original DD history rows with `22` receives, while `bob_restored` had
  `83` rows with `23` receives.
- The extra restored receive was a final all-owned DD change output from a
  self-fragment transaction. A preceding redeem-change DD UTXO also was not
  restored during rescan, causing the restored send amount for the later
  self-fragment transaction to be reconstructed incorrectly.

Fix:

- Restore REDEEM DD change outputs during wallet rescan so later restored
  transfer accounting sees the same spendable DD inputs as the original wallet.
- Rebuild redeemed amount history as spent DD minus DD change when redeem change
  exists.
- Infer final all-owned transfer outputs as DD change during descriptor restore
  only under narrow conservation and ordering constraints, so self-fragment
  recipient rows remain receives but appended change is not counted as a
  restored receive row.
- Added functional regression coverage in
  `test/functional/wallet_digidollar_rescan.py` for outgoing DD change,
  redeem-change restoration, spent self-fragment receive history, and
  self-fragment-with-change history.

Commit: `5a7b1e9062` - `digidollar: restore DD change history on rescan`

Final validation:

- `./test_multi_oracle_testnet.sh`: PASS, `430` checks passed, `0` failed,
  `0` warnings. Bob original/restored DD history matched exactly:
  `82` total, `mint:20`, `send:26`, `receive:22`, `redeem:12`.
- `python3 test/functional/wallet_digidollar_rescan.py`: PASS.
- `python3 test/functional/test_runner.py --jobs=4`: PASS, `376` entries,
  `17` expected skips, runtime `688s`.
- `./src/test/test_digibyte --show_progress`: PASS, `3,398` test cases, no
  errors detected.
- `QT_QPA_PLATFORM=offscreen src/qt/test/test_digibyte-qt -platform offscreen`:
  PASS, all Qt suites passed.
- `make -C src -j"$(nproc)" digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt`:
  PASS.
- `PRINT_ALL_FUZZ_TARGETS_AND_ABORT=1 ./src/test/fuzz/fuzz`: PASS,
  DigiDollar/oracle targets enumerated.
- Selected DigiDollar/oracle fuzz smokes passed with deterministic stdin seeds:
  `dd_script_parsing`, `dd_validate_mint`, `dd_validate_redeem`,
  `dd_txbuilder_redeem_consensus_round_trip`,
  `dd_wallet_rapid_state_model`, `oracle_bundle_validation`,
  `oracle_validate_block_data`, `oracle_musig2_bundle`,
  `oracle_price_message_sign_verify`, `musig2_session_manager_drive`.
- `git diff --check`: PASS after report correction.

Current code verdict: PASS WITH ARCHITECTURE REVIEW ITEMS. No open confirmed
funds-flow, consensus, restore/rescan, oracle, or Qt state bug remains from this
rerun. Remaining items are launch/process decisions: oracle roster governance,
sustained mainnet operator quorum proof, encrypted oracle wallet unlock
procedures, and explicit product choice for any testnet min-mint change.

# Red Hornet Final Audit Rerun - Current HEAD - 2026-06-12

Branch: `feature/digidollar-v1`
Starting HEAD: `0970f7f150da`
Mode: fresh validation-first 24-wave rerun from `Z_RED_HORNET_FINAL_PROMPT.md`

This section supersedes older entries below for the current rerun. Older report
content remains historical lead material only and is not treated as current
evidence unless revalidated against `0970f7f150da`.

## Current Rerun Ground Rules

- Validate every issue against current code before accepting it as real.
- For confirmed bugs, write or identify a fail-first test before changing code.
- Do not make architecture-sensitive consensus, activation, roster, bundle,
  MuSig2, P2P, RPC schema, wallet DB, or Qt fund-flow redesigns without Jared's
  explicit approval.
- Record false positives and theoretical risks separately from confirmed bugs.

## Current Required Context Read

Read before wave execution:

- `Z_RED_HORNET_FINAL_PROMPT.md`
- `CLAUDE.md`
- `ARCHITECTURE.md`
- `REPO_MAP.md`
- `REPO_MAP_GUIDE.md`
- `DIGIDOLLAR_ARCHITECTURE.md`
- `DIGIDOLLAR_ORACLE_ARCHITECTURE.md`
- `REPO_MAP_DIGIDOLLAR.md`
- `DIGIDOLLAR_EXPLAINER.md`
- `DIGIDOLLAR_ORACLE_EXPLAINER.md`
- `DIGIDOLLAR_ACTIVATION_EXPLAINER.md`
- `DIGIDOLLAR_WALLET_INTEGRATION.md`
- `doc/DIGIDOLLAR_OPRETURN_PQC_MINT_PLAN.md`
- `ORACLE_DISCOVERY_ARCHITECTURE.md`
- `ORACLE_BUNDLE_EXPLAINER.md`
- `DIGIDOLLAR_ORACLE_SETUP.md`
- `docs/ORACLE_OPERATOR_GUIDE.md`
- `RELEASE_v9.26.0-rc44.md`
- `RELEASE_v9.26.0-rc43.md`
- `RC43_TESTNET25_FORK_EXPLAINER.md`

Key context validated so far:

- Current contract is 35 active mainnet/testnet oracle slots with 7-signature
  quorum; ID 35 is outside the roster.
- `nDigiDollarMuSig2Height = 0`; v0x03 MuSig2 is the only accepted on-chain
  oracle bundle format once DigiDollar is active.
- `OP_CHECKPRICE` is reserved/disabled and must not read live node-local oracle
  state.
- DigiDollar transfer/redeem input chaining is confirmed-only.
- Oracle endpoint discovery beyond static chainparams metadata and `getoracles`
  pull telemetry is design-only, not implemented V1 behavior.
- Older docs/reports contain stale 21-slot and legacy oracle-bundle language;
  code and RC44 docs must be used as truth during this rerun.

## Current Baseline Evidence

- `git status --short`: clean before rerun.
- `make -C src -j"$(nproc)" digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt`: PASS.
- `./src/test/test_digibyte --show_progress`: PASS, no errors detected.
- `QT_QPA_PLATFORM=offscreen src/qt/test/test_digibyte-qt -platform offscreen`: PASS, all tests passed.
- `git diff --check`: PASS.
- `test/functional/test_runner.py --jobs=4`: RUNNING.
- `./test_multi_oracle_testnet.sh`: pending baseline run after functional suite.

## Current Wave Ledger

### Wave 0 - Baseline, Inventory, and Threat Model

Sub-agent assignments: main agent only; no sub-agents requested or used.

Live surface enumeration:

- Source inventory command run from prompt.
- Unit test inventory command run.
- Fuzz target inventory command run.
- Functional test inventory command run.

Current baseline is not complete until the full functional runner and
multi-oracle script finish. No new bug hunting result is accepted as final
until baseline status is known or a baseline failure is triaged.

Threat model carried into all waves:

- Consensus split via activation/roster/bundle interpretation mismatch.
- Invalid mint/redeem acceptance, DD inflation, collateral early spend, partial
  redemption, or burn bypass.
- Wallet restore/rescan/reorg divergence that changes spendable DD, position
  status, or receive/send history.
- Oracle liveness and safety failures: fake price injection, stale bundle use,
  missing v0x03 bundle acceptance, bad MuSig2 bitmap/signature validation, P2P
  resource exhaustion.
- Qt/RPC user-flow bugs that could misstate spendability, lock time, redemption
  state, balances, or required wallet unlock/key state.

Commands:

- `git status --short`
- `make -C src -j"$(nproc)" digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt`
- `./src/test/test_digibyte --show_progress`
- `QT_QPA_PLATFORM=offscreen src/qt/test/test_digibyte-qt -platform offscreen`
- `test/functional/test_runner.py --jobs=4`
- `git diff --check`

Status: in progress, waiting on full functional and multi-oracle baseline.

# Red Hornet Final Audit Ledger - DigiDollar / Oracle - 2026-06-12

Branch: `feature/digidollar-v1`
Final HEAD: `7bcfe516b680`
Mode: validation-first, full 0-24 wave audit

## Source Material Read

The required architecture and operator files were read before fixing or auditing:

- `CLAUDE.md`
- `ARCHITECTURE.md`
- `REPO_MAP.md`
- `REPO_MAP_GUIDE.md`
- `DIGIDOLLAR_ARCHITECTURE.md`
- `DIGIDOLLAR_ORACLE_ARCHITECTURE.md`
- `REPO_MAP_DIGIDOLLAR.md`
- `DIGIDOLLAR_EXPLAINER.md`
- `DIGIDOLLAR_ORACLE_EXPLAINER.md`
- `DIGIDOLLAR_ACTIVATION_EXPLAINER.md`
- `DIGIDOLLAR_WALLET_INTEGRATION.md`
- `doc/DIGIDOLLAR_OPRETURN_PQC_MINT_PLAN.md`
- `ORACLE_DISCOVERY_ARCHITECTURE.md`
- `ORACLE_BUNDLE_EXPLAINER.md`
- `DIGIDOLLAR_ORACLE_SETUP.md`
- `docs/ORACLE_OPERATOR_GUIDE.md`
- `RELEASE_v9.26.0-rc44.md`
- `RELEASE_v9.26.0-rc43.md`
- `RC43_TESTNET25_FORK_EXPLAINER.md`
- `Z_RED_HORNET_FINAL_PROMPT.md`

Prior reports were reviewed as historical leads only. Each issue in this run was revalidated against
the current tree before being accepted.

## Live Surface Inventory

Inventory commands run during the audit:

- Candidate live surface: 1,278 files
- DigiDollar/oracle/MuSig/RH unit-test lines from `--list_content`: 722
- Fuzz targets from `PRINT_ALL_FUZZ_TARGETS_AND_ABORT=1`: 248
- Functional test entries matching DigiDollar/oracle/wallet/MuSig/DD: 208

Key current-state facts:

- Mainnet/testnet26 oracle roster: 35 active public keys, 7 required signatures.
- Regtest roster: 7 keys, 4 required signatures.
- Local `-easypow` mini-testnet mode: 24 active local test keys.
- OP_CHECKPRICE: reserved/disabled; production path pushes false and does not query a live oracle
  price hook.
- Testnet26 surface: genesis and port/datadir reset are current; old Testnet25 references in tracked
  docs were stale and fixed.

## Severity Ledger

| ID | Severity | Status | Evidence | Commit |
| --- | --- | --- | --- | --- |
| RC44-UI-001 pending mint false redeemed | Medium | Fixed | Parent unit/functional failures | `5569e0dd55` |
| RC44-UI-002 row widgets lost during sorting | Medium | Fixed | Fail-first Qt row-widget test | `5569e0dd55` |
| RC44-UI-003 tier-zero buffer hidden | Low/UX | Fixed | Parent Qt tooltip failure and mint lock math | `5569e0dd55` |
| RC44-OPS-001 missing oracle key export/import RPCs | High ops/recovery | Fixed | RPC method missing, fail-first functional | `f60ca16043` |
| DOC-SURFACE-001 stale OP_CHECKPRICE docs | Medium ops/confusion | Fixed | Lint guard failure | `85bd1c7321`, `466ee04ac4`, `d41bf158bb` |
| DOC-SURFACE-002 stale activation/roster docs | Medium ops/fork-risk | Fixed | Lint guard failure | `466ee04ac4`, `d41bf158bb`, `7bcfe516b6` |
| Restored DD receive-history category count | Low | Fixed | E2E reproduced 82/22 receive original vs 83/23 restored; final E2E now 82/22 vs 82/22 | Current restore-history fix |
| Testnet min mint `$DD1` request | Product/consensus | Deferred | Requires explicit parameter decision | None |
| Static roster rotation risk | Architecture | Open review | Consensus-sensitive by design | None |
| `GetBestHeight()` hardcoded 0 | Latent cleanup | No live bug | No current callers found | None |

## Validation And Fix Detail

### Pending mints and false redeemed state

Pre-fix validation:

- `src/test/test_digibyte --run_test=digidollar_wave17_spendability_tests/w17_02b_wallet_local_dd_utxo_is_pending_not_spendable`
  failed on parent `3668971ab2` with `0 != 12500`.
- `python3 test/functional/digidollar_pending_position_status.py` failed on parent with zero active
  matching positions.
- Qt fail-first coverage reproduced pending mint as redeemable/redeemed rather than confirming.

Fix:

- Wallet-local unbroadcast DD mints now persist mint metadata.
- Reconciliation keeps pending mint positions visible.
- Qt status maps pending mints to `Confirming`, not `Redeemed`.
- Redeem button is disabled for pending mints.

Verification:

- Unit target passed.
- Functional pending-position test passed.
- Full Qt suite passed.
- Full functional suite passed.
- Multi-oracle E2E passed.

### Table sorting and Health/Actions widgets

Pre-fix validation:

- Sorting during row population could move rows before widgets were attached.
- Fail-first Qt coverage reproduced the missing/misplaced widget behavior.

Fix:

- `populatePositionsTable()` preserves the prior sorting state, disables sorting while building rows,
  and restores sorting afterwards.

Verification:

- Qt DigiDollar widget tests passed.
- Full Qt suite passed offscreen.

### Tier-zero lock display

Pre-fix validation:

- Mint lock code adds `MINT_LOCK_CONFIRMATION_BUFFER_BLOCKS = 100`.
- Parent UI label/tooltip did not disclose the buffer.

Fix:

- Tier-zero UI now says `1 hour + 100 block buffer`.
- Block-time display rounds minutes up.

Verification:

- Updated Qt expectations passed.

### Oracle key export/import recovery

Pre-fix validation:

- No `exportoracleprivkey`, `importoracleprivkey`, or `migrateoraclekey` RPC existed.
- Functional test failed with RPC method-not-found `-32601`.

Fix:

- Added wallet RPC `exportoracleprivkey <oracle_id>`.
- Added wallet RPC `importoracleprivkey <oracle_id> <private_key_hex> [replace=false]`.
- Import/export honor encrypted-wallet unlock rules and private-key-disabled wallets.
- Unauthorized imported keys are stored but reported as unauthorized; `startoracle` remains the
  enforcement point for current chainparams authorization.

Verification:

- `test/functional/digidollar_oracle_keygen.py` passed.
- `digidollar_wave16_persistence_tests/w16_10_wallet_encryption_protects_oracle_keys` passed.
- RPC schema/unit groups passed.
- Full functional suite passed.

### Restored wallet DD receive-history category count

Pre-fix validation:

- Full Qt E2E descriptor restore reproduced a real history mismatch while DD balances and positions
  remained correct: Bob original had `82` DD history rows with `22` receives, but `bob_restored` had
  `83` rows with `23` receives.
- Direct inspection of the saved failing datadir found the extra restored row was an `8000` cent
  receive on a self-fragment transaction whose intended recipient prefix was `20 x 500` and whose
  final output was DD change.
- The same saved chain showed a preceding redeem produced DD change that was not restored into the
  descriptor wallet's DD UTXO set during rescan. That made the later transfer reconstruction see an
  incomplete spent-DD input set and synthesize the wrong send/receive history.

Fail-first coverage:

- Added focused functional regressions to `wallet_digidollar_rescan.py` for:
  - outgoing DD change not counted as restored receive history;
  - REDEEM DD change UTXO and history restoration;
  - spent self-fragment receive history;
  - self-fragment with appended DD change preserving receive count and send amount.

Fix:

- During REDEEM rescan, remove spent DD inputs, restore owned DD change outputs, persist restored DD
  change UTXOs, and store recovered owner keys when available.
- Reconstruct redeem history amount from spent DD minus DD change when that data is available.
- During TRANSFER restore, infer a final all-owned DD output as change only when conservation,
  output order, positive recipient prefix, and script/amount-shape checks support that inference.

Verification:

- Patched daemon validated against the saved failing Bob datadir: original and freshly restored
  histories both returned `82` rows with `receive:22`.
- `wallet_digidollar_rescan.py` passed.
- Full Qt multi-oracle E2E passed with Bob original/restored history both at `82` total rows:
  `mint:20`, `send:26`, `receive:22`, `redeem:12`.
- Full functional runner, core unit suite, Qt offscreen suite, fuzz smokes, and diff hygiene passed.

### OP_CHECKPRICE and operator docs

Pre-fix validation:

- Current code has OP_CHECKPRICE disabled.
- Tracked docs still described live hook behavior.
- Lint guards failed when added.

Fix:

- Updated docs and comments to state OP_CHECKPRICE is reserved/disabled.
- Added lint coverage for stale live-hook wording.

Verification:

- `test/lint/lint-digidollar-operator-surface.py` passed.
- OP_CHECKPRICE unit groups passed.

## Wave Results

### Wave 0 - Baseline, Inventory, Threat Model

Result: PASS.

Validated current branch, HEAD, build targets, live surface, test surface, oracle roster, and stale
prompt assumptions. The prompt's "21 active slots" claim was rejected as stale; code is 35/7 for
mainnet/testnet26.

### Wave 1 - Build, Registration, Plumbing

Result: PASS after fixes.

Validated DigiDollar/oracle source registration, RPC registration, test harness registration, and Qt
test registration. Added missing oracle key export/import RPCs and verified they are registered.

### Wave 2 - Activation Gates

Result: PASS.

Validated activation-gated DD operations and oracle P2P behavior. Key creation/export/import are
local wallet recovery operations and remain possible before activation; oracle signing/start and DD
funds operations remain gated.

### Wave 3 - Chainparams and Testnet Reset

Result: PASS with architecture note.

Verified Testnet26 current surface and fixed stale Testnet25 tracked documentation. `-easypow` local
mini-testnet deliberately changes local testing consensus and must not be used as a public testnet
operator mode.

### Wave 4 - Oracle Roster and Threshold

Result: PASS with architecture note.

Validated 35 active public keys and 7 required signatures in mainnet/testnet26. Static roster changes
are consensus-sensitive and must be coordinated by release or future height-scheduled roster logic.

### Wave 5 - Price Freshness and Aggregation

Result: PASS.

Validated price-source freshness, exchange aggregation, zero/negative/extreme price rejection,
outlier handling, and live consensus behavior through unit, functional, and multi-oracle coverage.

### Wave 6 - MuSig2 Lifecycle

Result: PASS.

Validated nonce, epoch, partial-signature, aggregate-signature, bitmap, and restart/session behavior
through targeted units, P2P functionals, fuzz smoke, and multi-oracle E2E.

### Wave 7 - Oracle Bundle Validation

Result: PASS.

Validated v0x03 bundle parsing, bitmap size and unused-bit constraints, chain-hash-bound domain
separation, aggregate signature checks, and rejection paths.

### Wave 8 - Oracle P2P and Pending State

Result: PASS.

Validated activation gates, rate limits, pending-message bounds, inventory handling, and DoS resource
coverage for oracle P2P messages.

### Wave 9 - Mint Path

Result: PASS after RC44 fix.

Validated mint amount parsing, collateral math, lock-buffer behavior, wallet-local unbroadcast mints,
pending position visibility, and rapid mint stress in multi-oracle E2E.

### Wave 10 - Send and Transfer Path

Result: PASS.

Validated send/transfer ownership, parsing, mempool acceptance, wallet accounting, and rapid transfer
stress.

### Wave 11 - Receive, Address, Ownership, Rescan

Result: PASS.

Validated DigiDollar receive addresses, ownership detection, watch-only behavior, descriptor export
and import, rescan, and restored-wallet spendability.

### Wave 12 - Redemption Path

Result: PASS.

Validated exact-amount redemption, no partial redeem, early redeem rejection, oracle price dependency,
double-redeem protection, and rapid redeem stress.

### Wave 13 - DCA, ERR, Health

Result: PASS.

Validated collateral ratio, liquidation/ERR gates, DCA state, health RPC surface, restart/rebuild
paths, and stale-cache protections already present in the tree.

### Wave 14 - Persistence, Restore, Reorgs

Result: PASS after restore-history fix.

Validated wallet restart, backup/restore, rescan, reindex, descriptor import/export, active versus
redeemed state, redeem-change DD UTXO restoration, and restored DD transaction-history category
counts. The prior Bob descriptor-restore warning is fixed: original and restored histories both
reported `82` total rows with `22` receive rows in the final E2E rerun.

### Wave 15 - RPC Surface

Result: PASS after export/import fix.

Validated help/schema behavior, method registration, amount parsing docs, error surfaces, and new
oracle key recovery RPCs.

### Wave 16 - Qt Wallet UI

Result: PASS after RC44 fix.

Validated pending mint status, disabled redeem action while confirming, sorting-safe row widget
population, lock-buffer display, encrypted/private-key-disabled states, and full offscreen Qt suite.

### Wave 17 - Mempool, Miner, Block Template, Chainstate

Result: PASS.

Validated mempool/miner parity, stale cache rejection, block template DD/oracle handling, verifychain
coverage, and spendability checks.

### Wave 18 - End-to-End Multi-Oracle

Result: PASS.

`./test_multi_oracle_testnet.sh` passed 430 checks with 0 failures and 0 warnings. It validated 24
active local test oracles, 7-of-35 threshold, live price consensus, fresh v0x03 MuSig2 bundle,
mint/redeem/transfer chains, stress sends, restarts, backup/restore, reindex, rescan, and descriptor
restore.

### Wave 19 - Fuzz and Parser Robustness

Result: PASS.

Enumerated 248 fuzz targets. Selected DigiDollar/oracle parser, amount, validation, bundle, bitmap,
MuSig2, P2P, and price-message fuzz smokes passed with the correct corpus-file invocation.

### Wave 20 - Resource and Stability

Result: PASS.

Validated DoS/resource unit and functional coverage, pending maps, message bounds, and P2P rate-limit
surfaces.

### Wave 21 - Mainnet and Operator Readiness

Result: PASS for code, review required for operations.

Added key export/import recovery RPCs and refreshed operator documentation. Mainnet launch still
requires sustained quorum proof, wallet unlock procedures for encrypted operator wallets, key backup
discipline, and coordinated roster governance.

### Wave 22 - Fork Risk and Mixed Version

Result: PASS with architecture note.

Validated current mixed-node compatibility tests and current chainparams. RC43/testnet25 fork class
was reviewed: static roster/threshold changes are consensus-sensitive and must not be deployed
piecemeal.

### Wave 23 - Exploit Chain Sweep

Result: PASS.

No reachable exploit chain was found after fixes across pending mint state, wallet accounting, oracle
P2P, MuSig2 bundle acceptance, OP_CHECKPRICE, DCA/ERR, restore/rescan, and Qt action surfaces.

### Wave 24 - Final Signoff

Result: PASS WITH ARCHITECTURE REVIEW ITEMS.

Final build, unit, Qt, functional, multi-oracle, targeted, fuzz smoke, lint, whitespace, and status
checks were green.

## Final Commands And Results

- `make -C src -j"$(nproc)" digibyted test/test_digibyte test/fuzz/fuzz qt/test/test_digibyte-qt`
  - PASS
- `./src/test/test_digibyte --show_progress`
  - PASS, 3,398 test cases, no errors
- `QT_QPA_PLATFORM=offscreen ./src/qt/test/test_digibyte-qt -platform offscreen`
  - PASS
- `test/functional/test_runner.py --jobs=4`
  - PASS, 376 entries, 17 skipped
- `./test_multi_oracle_testnet.sh`
  - PASS, 430 passed, 0 failed, 0 warnings
- `test/lint/lint-digidollar-operator-surface.py`
  - PASS
- `git diff --check`
  - PASS
- `git status --short`
  - clean after final commit

Targeted unit groups passed:

- Oracle/MuSig2 bundle, domain, quorum, roster, and net-processing groups.
- Mint, transfer, redeem, wallet, restore, DCA, ERR, health, and volatility groups.
- Activation, parity, IBD security, reorg/replay, parser, RPC schema, P2P pending, resource, and
  compatibility groups.
- RH hardening groups for coinbase oracle manipulation, keyset alignment, OP_SUCCESS,
  MuSig2 aggregation/TOCTOU, amount parsing, validator escape hatches, and DCA table agreement.
- OP_CHECKPRICE disabled/reserved coverage groups.

Selected fuzz smoke targets passed:

- `dd_amount_validation`
- `dd_collateral_math`
- `dd_consensus_rules`
- `dd_txbuilder_mint`
- `dd_txbuilder_redeem`
- `dd_validate_mint`
- `dd_validate_redeem`
- `dd_validate_transfer`
- `dd_wallet_rapid_state_model`
- `oracle_bundle_hash_domain_sep`
- `oracle_bundle_v03_roundtrip`
- `oracle_bundle_validation`
- `oracle_bundle_version_reject`
- `oracle_id_bitmap_mutations`
- `oracle_musig2_aggregation`
- `oracle_musig2_bitmap`
- `oracle_musig2_bitmap_invariants`
- `oracle_p2p_wire_messages`
- `oracle_price_message`
- `oracle_price_message_sign_verify`
- `oracle_script_extract`
- `oracle_validate_block_data`

## Remaining Review Items

These are not open confirmed code bugs from this audit:

- Static oracle roster and quorum changes are consensus-sensitive. Use coordinated releases or a
  future deterministic height-scheduled/on-chain roster design.
- 7-of-35 is an explicit liveness/security tradeoff. Operator readiness should prove quorum is
  comfortably above 7 before mainnet activation.
- Decide whether testnet minimum mint remains `$DD100` or changes to `$DD1`.
- Remove or wire `GetBestHeight()` before future code depends on it.
- Clean ignored historical planning docs separately if they are still used operationally.

## Final Verdict

The validated RC44 cleanup is complete, the confirmed issues are fixed and committed, and the full
Red Hornet 0-24 audit completed with green code gates.

Verdict: PASS WITH ARCHITECTURE REVIEW ITEMS.
