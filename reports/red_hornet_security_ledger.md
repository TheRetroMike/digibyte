# Red Hornet DigiDollar Security Vulnerability Campaign Ledger

Campaign: second Red Hornet security campaign using `Z_RED_HORNET.md`
Repository: `/home/jared/Code/digibyte`
Branch: `feature/digidollar-v1`
Ledger path: `reports/red_hornet_security_ledger.md`
Final report path: `reports/red_hornet_security_final_report.md`
Policy: no push; one local commit per confirmed vulnerability or tightly coupled security fix; attack/regression test and production fix together.

## Required Context

Main agent read the required context set before code analysis:

1. `CLAUDE.md`
2. `ARCHITECTURE.md`
3. `REPO_MAP.md`
4. `REPO_MAP_GUIDE.md`
5. `DIGIDOLLAR_ARCHITECTURE.md`
6. `DIGIDOLLAR_ORACLE_ARCHITECTURE.md`
7. `REPO_MAP_DIGIDOLLAR.md`
8. `DIGIDOLLAR_EXPLAINER.md`
9. `DIGIDOLLAR_ORACLE_EXPLAINER.md`
10. `DIGIDOLLAR_ACTIVATION_EXPLAINER.md`
11. `DIGIDOLLAR_WALLET_INTEGRATION.md`
12. `DIGIDOLLAR_EXCHANGE_INTEGRATION.md`
13. `DIGIDOLLAR_OPRETURN_PQC_MINT_PLAN.md`
14. `ORACLE_DISCOVERY_ARCHITECTURE.md`
15. `DIGIDOLLAR_ORACLE_SETUP.md`

Important context notes:

- Production code is the authority where explainer docs are stale.
- The MINT v1 OP_RETURN owner pubkey exposure is an explicitly documented architectural/PQC concern, not an implemented fix target for this campaign without Jared approval.
- `sendoracleprice` is expected to remain removed; regtest-only mock/submission paths must stay gated.
- Mainnet/testnet/regtest oracle validator parity was fixed in the prior campaign and should be rechecked, not assumed.

## Initial Git State

Command:

```bash
git status --short && git branch --show-current && git log --oneline -5
```

Result:

- Branch: `feature/digidollar-v1`
- Pre-existing untracked file: `Z_RED_HORNET_v2.md`
- Recent commits:
  - `f7fd01637c reports: finalize Red Hornet DigiDollar audit package`
  - `85c93a4fdc test digidollar: repair DD-RH-051 extra functional coverage`
  - `87051c5999 digidollar redeem: fix DD-RH-050 failed redemption state mutation`
  - `584b8372aa test wallet: exercise DD-RH-012 state validation on live chain setup`
  - `305dd806ca test oracle: align DD-RH-010 block validation split`

## Live Attack Surface Enumeration

Each wave must run:

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|attack|security|wallet|qt' \
  | sort
```

Initial result: command completed and returned the live DigiDollar/oracle/security surface, including production DD/oracle files, RPC/wallet/Qt slices, red-team unit tests through `rh65`, wallet tests, fuzz harnesses, and functional tests. The exact command output was very large because it also matched existing build artifacts under `.deps` and object files; source/test filenames are treated as the review surface.

## Vulnerability/Fix Ledger

### DD-RH-052 - Block validation used local oracle fallback when no block oracle bundle was extracted

- Severity: High.
- Status: fixed and committed.
- Commit: `d0606db668` (`validation oracle: fix DD-RH-052 block price fallback`).
- Affected invariant: oracle prices used for block validation must be deterministic and block-committed; DD mints must not be validated using node-local cache/mock prices.
- Reachable exploit path:
  - `src/oracle/bundle_manager.cpp:2368-2380` permits post-activation blocks with no or unextractable `OP_ORACLE` output as a transition escape hatch.
  - `src/validation.cpp:2830-2842` leaves `blockOraclePrice == 0` when extraction fails.
  - Before the fix, `src/validation.cpp:1877-1911` then fell back to local P2P/cache/mock oracle state in the `ConnectBlock` DD transaction validation path.
  - Nodes with different local oracle state could disagree on whether a DD mint was sufficiently collateralized.
- Exact fixed code: `src/validation.cpp:1877`; fail-closed block path guard at `src/validation.cpp:1887`.
- Regression test: `src/test/rh63_oracle_validator_escape_hatches_tests.cpp:525`.
- Failing evidence before fix:
  - `src/test/test_digibyte --run_test=rh63_oracle_validator_escape_hatches_tests/rh63_07_block_path_rejects_local_oracle_fallback --log_level=all --report_level=short`
  - Failed with `GetOraclePriceForTransaction(dummy_tx, 4001, 0) == 0` reporting `6500 != 0`.
- Fix summary: if `nHeight > 0` and no positive `blockOraclePrice` was extracted from the block, return `0` immediately. The DD validator then rejects mint validation for lack of a deterministic oracle price. The mempool path still uses `nHeight == 0` and can consult advisory local oracle state.
- Passing evidence after fix:
  - Focused test passed: `1 test case, 4 assertions`.
  - `src/test/test_digibyte --run_test=rh63_oracle_validator_escape_hatches_tests --log_level=error --report_level=short`: passed `7 test cases, 35 assertions`.
  - `src/test/test_digibyte '--run_test=oracle_*' --log_level=error --report_level=short`: passed `265 test cases, 1002 assertions`.
  - `src/test/test_digibyte '--run_test=digidollar_*' --log_level=error --report_level=short`: passed `1740 test cases, 2 warning-only test cases, 7911105 assertions`.

### DD-RH-053 - Preset wallet inputs could select DD-locked token/collateral outpoints

- Severity: Medium.
- Status: fixed and committed.
- Commit: `1d081a8b79` (`wallet digidollar: fix DD-RH-053 preset input lock bypass`).
- Affected invariant: DD token/collateral outpoints tracked by the DigiDollar wallet must not be spent through normal DGB wallet funding paths; they must go through DD-aware transfer/redeem flows.
- Reachable exploit path:
  - RPCs with explicit inputs (`send`, `walletcreatefundedpsbt`, `fundrawtransaction` style flows) route selected outpoints into `FetchSelectedInputs`.
  - Before the fix, `src/wallet/spend.cpp:259` accepted selected inputs without consulting `DigiDollarWallet::IsLockedByDD`.
  - An attacker or user with explicit input control could select DD token/collateral outpoints and drive them through normal wallet funding logic, risking DD wallet/accounting desynchronization.
- Exact fixed code: `src/wallet/spend.cpp:259`; DD lock rejection at `src/wallet/spend.cpp:266`.
- Regression test: `src/wallet/test/rh59_coincontrol_dd_lock_bypass_tests.cpp:183`.
- Failing evidence before fix:
  - `src/test/test_digibyte --run_test=rh59_coincontrol_dd_lock_bypass_tests/rh59_02_dd_wallet_locked_outpoint_also_bypassable --log_level=all --report_level=short`
  - Failed because the selected DD-locked collateral/token outpoints were accepted.
- Fix summary: `FetchSelectedInputs` now checks the wallet-owned `DigiDollarWallet` sidecar and rejects any pre-selected input with `IsLockedByDD(outpoint) == true`. Ordinary manual `lockunspent` override behavior is unchanged.
- Passing evidence after fix:
  - Focused test passed: `1 test case, 8 assertions`.
  - `src/test/test_digibyte --run_test=rh59_coincontrol_dd_lock_bypass_tests --log_level=error --report_level=short`: passed `3 test cases, 13 assertions`.
  - `src/test/test_digibyte --run_test=spend_tests --log_level=error --report_level=short`: passed `2 test cases, 21 assertions`.
  - `src/test/test_digibyte --run_test=wallet_tests --log_level=error --report_level=short`: passed `16 test cases, 286 assertions`.
  - `src/test/test_digibyte '--run_test=digidollar_*' --log_level=error --report_level=short`: passed `1740 test cases, 2 warning-only test cases, 7911105 assertions`.

## False Positives Rejected

- Wave 1: regular `lockunspent` manual-selection override is not a vulnerability by itself. Bitcoin Core explicitly allows manually selected locked coins; DD-RH-053 is limited to DigiDollar protocol locks (`IsLockedByDD`) and leaves ordinary `setLockedCoins` behavior unchanged.
- Wave 1: `GetBestHeight()` and `TxBuilder::GetCurrentSystemCollateral()` placeholder paths were reviewed by Agent A and rejected as confirmed vulnerabilities because they did not gate production DD consensus validation.
- Wave 1: the RH63 transition escape hatch itself remains a liveness/design issue, but after DD-RH-052 DD transaction validation does not use local oracle state in block validation when no bundle was extracted.

## Theoretical / Not Yet Reachable

- Wave 1: full removal of RH63 post-activation oracle escape hatches may be desirable before launch, but that is a broader liveness/activation policy decision. DD-RH-052 closed the reachable DD mint validation exploit path without changing the transition rule.
- Wave 1: `OracleBundleManager::ValidateBundle()` v3 acceptance split appears to be covered by production `ValidatePhaseThreeBundle` callers; retain as a regression/watch item unless a production caller reaches the weaker path.

## ARCHITECTURAL_REVIEW_REQUIRED

- `ARCH-RH-002` / reconfirmed in Wave 2: IBD/catch-up `skipOracleValidation` can make DD block validity depend on local sync state. `src/validation.cpp:2958` sets `fSkipOracle = fInIBD || (fCatchingUp && blockOraclePrice <= 0)`, and `src/digidollar/validation.cpp:753` plus `src/digidollar/validation.cpp:1141` skip oracle-price and collateral-ratio enforcement under that flag. Existing tests in `src/test/digidollar_skip_oracle_tests.cpp:92` and `src/test/digidollar_skip_oracle_tests.cpp:133` encode the split. This is consensus/sync architecture, so no patch was made without Jared approval.
- Full post-activation oracle-output strictness remains a broader liveness/activation policy decision. DD-RH-052 closed the reachable local-oracle fallback path without removing every transition escape hatch.
- `DD-RH-075` / Wave 10: oracle operator private keys are stored as plaintext `ORACLE_KEY` wallet DB records and are not migrated by wallet encryption. Fixing this safely requires a wallet-storage/encryption migration design for oracle keys, so no patch was made without Jared approval.

## Wave 1 - Threat Model, Baseline, Attack-Test Inventory

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: build the DD/oracle security invariant map from production code.
- Agent B - Invariant/test breaker: inventory existing redteam/attack/regression/fuzz coverage.
- Agent C - Boundary adversary: run baseline DD/oracle security-relevant tests and record failures/flakes.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle behavior.

Results:

- Confirmed and fixed DD-RH-052 (High): deterministic block validation must not fall back to local oracle cache/mock price. Commit `d0606db668`.
- Confirmed and fixed DD-RH-053 (Medium): normal wallet preset-input paths must reject DD-locked token/collateral outpoints. Commit `1d081a8b79`.
- No out-of-scope generic DigiByte findings were promoted.

Commands and results:

- Required live surface enumeration: completed.
- `make -C src -j$(nproc) test/test_digibyte`: passed build/link.
- `src/test/test_digibyte --run_test=rh63_oracle_validator_escape_hatches_tests/rh63_07_block_path_rejects_local_oracle_fallback --log_level=all --report_level=short`: failed before fix with `6500 != 0`, passed after fix.
- `src/test/test_digibyte --run_test=rh59_coincontrol_dd_lock_bypass_tests/rh59_02_dd_wallet_locked_outpoint_also_bypassable --log_level=all --report_level=short`: failed before fix because DD-locked inputs were accepted, passed after fix.
- `src/test/test_digibyte --run_test=rh63_oracle_validator_escape_hatches_tests --log_level=error --report_level=short`: passed `7 test cases, 35 assertions`.
- `src/test/test_digibyte --run_test=rh59_coincontrol_dd_lock_bypass_tests --log_level=error --report_level=short`: passed `3 test cases, 13 assertions`.
- `src/test/test_digibyte --run_test=spend_tests --log_level=error --report_level=short`: passed `2 test cases, 21 assertions`.
- `src/test/test_digibyte --run_test=wallet_tests --log_level=error --report_level=short`: passed `16 test cases, 286 assertions`.
- `src/test/test_digibyte '--run_test=oracle_*' --log_level=error --report_level=short`: passed `265 test cases, 1002 assertions`.
- `src/test/test_digibyte '--run_test=digidollar_*' --log_level=error --report_level=short`: passed `1740 test cases, 2 warning-only test cases, 7911105 assertions`.

### DD-RH-054 - Unbroadcast mint positions were shown as active/unlocked and redeemable

- Severity: Medium.
- Status: fixed and committed.
- Commit: `a4d4813389` (`rpc digidollar: fix DD-RH-054 pending mint status`).
- Affected invariant: RPC/wallet surfaces must not make a user believe a DD collateral position is active or redeemable before the mint transaction is accepted into chain history.
- Reachable exploit path:
  - Start a wallet with `-walletbroadcast=0`.
  - Call `mintdigidollar`; `src/rpc/digidollar.cpp:1185` commits the transaction to the wallet, but `src/wallet/wallet.cpp:2456-2458` returns before broadcast when wallet broadcast is disabled.
  - Before the fix, `listdigidollarpositions` used only `pos.is_active` and `blocks_remaining`, so the locally persisted but zero-confirmation position could display as `active` or `unlocked`, with `can_redeem=true` if its lock height had passed.
- Exact fixed code: `src/rpc/digidollar.cpp:2057`; confirmation-aware status and redeemability at `src/rpc/digidollar.cpp:2060` and `src/rpc/digidollar.cpp:2094`.
- Regression test: `test/functional/digidollar_pending_position_status.py`.
- Failing evidence before fix:
  - `python3 test/functional/digidollar_rpc_display_bugs.py`
  - Failed in the new pending-position subcase with `KeyError: 'confirmations'`; before the fix there was no confirmation field and the zero-confirmation position was not marked pending.
- Fix summary: `listdigidollarpositions` now returns `confirmations`, reports active zero-confirmation positions as `pending`, and requires `confirmations > 0` before `can_redeem` can be true.
- Passing evidence after fix:
  - `make -C src -j$(nproc) digibyted`: passed.
  - `python3 test/functional/digidollar_pending_position_status.py`: passed.

### DD-RH-055 - `senddigidollar` reports wallet remainder as change instead of selected-input change

- Severity: Low.
- Status: confirmed open; not fixed/committed in this wave.
- Affected invariant: RPC responses must not mislead wallets/exchanges about the actual DD change output created by a transfer.
- Reachable exploit path:
  - Wallet has multiple confirmed DD UTXOs, for example `1000` and `5000` cents.
  - Sending `500` cents selects the `1000` cent UTXO and should create `500` cents of DD change.
  - Before any attempted fix, `src/rpc/digidollar.cpp:1402` returned `balance - amount`, so the RPC reported `5500` cents of change instead of the actual `500`.
- Failing evidence:
  - A temporary functional regression in `test/functional/digidollar_transfer.py` failed pre-fix with `AssertionError: not(5500 == 500)`.
- Why not fixed yet:
  - The narrow production patch to return selected-input change from `TransferDigiDollarMany` was backed out because the post-DD-RH-052 block path refuses local/mock oracle fallback during mining. The focused functional test could not get confirmed DD mints and therefore could not prove the fix. The issue remains logged as confirmed/open and needs a regression that creates confirmed DD using a valid block oracle bundle or a lower-level wallet test that exercises real selected-input change.

## Wave 2 - Inflation and Supply Integrity

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: attack mint/transfer paths for unauthorized DD creation.
- Agent B - Invariant/test breaker: attack conservation/supply tests and missing assertions.
- Agent C - Boundary adversary: attack RPC/wallet/functional paths for supply/accounting drift.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle behavior.

Results:

- Reconfirmed `ARCH-RH-002`: IBD/catch-up `skipOracleValidation` can accept undercollateralized mints in a sync-state-dependent path. This is architectural/consensus design and was not changed without Jared approval.
- Confirmed and fixed DD-RH-054 (Medium): unbroadcast mint positions now display as `pending` and non-redeemable. Commit `a4d4813389`.
- Confirmed and left open DD-RH-055 (Low): `senddigidollar` reports whole-wallet remainder as `change_amount`; fix requires a reliable regression path with confirmed DD after DD-RH-052.

Rejected false positives:

- Extra DD mint outputs: live mint validator rejects multiple DD token outputs.
- Non-DD or coinbase source transactions creating transferable DD: current extractors and block checks reject these paths.
- Transfer amount mismatch: current validation resolves DD input amounts and requires strict equality with DD outputs.
- Partial/excess collateral release: current redemption checks require full burn/release consistency in the reviewed paths.

Theoretical / not yet reachable:

- `sendmanydigidollar` restore/rescan may underreport aggregate outgoing history by reconstructing only the first recipient amount. Agent C found the accounting path, but no deterministic restore/rescan reproducer was completed in this wave.
- Stateful fuzz coverage for full DD validation with a real coins view remains thin.

Commands and results:

- Required live surface enumeration: completed.
- `python3 test/functional/digidollar_rpc_display_bugs.py`: failed before DD-RH-054 fix in the new pending-position subcase with missing `confirmations` field; the script also still contains older unrelated display failures.
- `python3 test/functional/digidollar_transfer.py`: temporary DD-RH-055 regression failed before fix attempt with `5500 != 500`; subsequent focused regression was blocked by unconfirmed DD mints after DD-RH-052.
- `make -C src -j$(nproc) digibyted`: passed after DD-RH-054 final patch.
- `python3 test/functional/digidollar_pending_position_status.py`: passed after DD-RH-054 final patch.

### DD-RH-056 - Validator DCA table was weaker than wallet/RPC builder policy

- Severity: High.
- Status: fixed and committed.
- Commit: `d3078dd9e7` (`digidollar dca: fix DD-RH-056 validator table split`).
- Affected invariant: a DD mint must not be accepted with less collateral than the active DCA policy requires.
- Reachable exploit path:
  - Honest wallet/RPC mint builders read `ConsensusParams::dcaLevels` through `DigiDollar::GetDCAMultiplier` and require `2.0x` DCA at system health `100..109`, and `1.25x` at `120..149`.
  - Before the fix, consensus validation used `DynamicCollateralAdjustment::HEALTH_TIERS` through `src/digidollar/validation.cpp:515-516`, which required only `1.5x` at `100..109` and `1.2x` at `120..149`.
  - A hand-built DD mint could bypass the honest builder and pass validation with less collateral than the policy surface required.
- Exact fixed code:
  - `src/consensus/dca.cpp:22` now aligns `HEALTH_TIERS` with `ConsensusParams::dcaLevels`.
  - `src/consensus/dca.cpp:398` updates precision self-checks for the corrected boundaries.
  - `src/test/rh64_dca_table_disagreement_tests.cpp:60` now asserts builder and validator multipliers match at the vulnerable boundaries and across health `0..30000`.
- Failing evidence before fix:
  - `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=rh64_dca_table_disagreement_tests --log_level=all --report_level=short`
  - Failed with validator/builder mismatches at health `100`, `105`, `109`, `120`, `125`, and `149`.
  - Failed the concrete collateral check with `validator_ratio == expected_ratio` reporting `750 != 1000`, and `validator_required == expected_required` reporting `7500000000000 != 10000000000000`.
- Fix summary: align validator-facing DCA tiers to chainparams: `0..109 => 2.0x`, `110..119 => 1.5x`, `120..149 => 1.25x`, `150..30000 => 1.0x`; update DCA tests and functional expectations to the corrected policy.
- Passing evidence after fix:
  - `src/test/test_digibyte --run_test=rh64_dca_table_disagreement_tests --log_level=error --report_level=short`: passed `3 test cases, 30028 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_dca_tests --log_level=error --report_level=short`: passed `22 test cases, 131 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_rh32_collateral_dca_tests --log_level=error --report_level=short`: passed `18 test cases, 405 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_t2_05_tests --log_level=error --report_level=short`: passed `10 test cases, 46 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_err_attack_tests --log_level=error --report_level=short`: passed `23 test cases, 51 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_rh13_economic_tests --log_level=error --report_level=short`: passed `21 test cases, 69 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_rh35_chaos_tests --log_level=error --report_level=short`: passed `24 test cases, 155 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_rh40_regression_tests --log_level=error --report_level=short`: passed `26 test cases, 610 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_validation_tests --log_level=error --report_level=short`: passed with two existing warning-marked cases, `99 test cases`, `248 assertions`.
  - `git diff --check`: passed before commit.

### DD-RH-057 - `estimatecollateral` contains a reachable signed overflow in unused USD-value math

- Severity: Low.
- Status: confirmed open; not fixed in Wave 3.
- Affected invariant: RPC helper paths must not execute undefined signed arithmetic for valid boundary inputs.
- Reachable path: `estimatecollateral 10000000 0 100` can compute a large `requiredDGB` and then multiply `static_cast<int64_t>(requiredDGB) * oraclePriceMicroUSD` in `src/rpc/digidollar.cpp:2904`. The calculated value is not returned because `usd_value` is now sourced from `ddAmount / 100.0`, so the immediate user-visible impact is limited.
- Why not fixed yet: a deterministic non-sanitizer regression was not completed in this wave. Preserve for a sanitizer-backed or helper-level regression, then fix in a separate commit.

### DD-RH-058 - `calculatecollateralrequirement` quotes amounts outside mint bounds

- Severity: Low.
- Status: confirmed open; not fixed in Wave 3.
- Affected invariant: RPC collateral quotes should not tell users or integrations that invalid mint amounts are actionable.
- Reachable path: `calculatecollateralrequirement` validates positive amount, lock days, and price, but unlike `estimatecollateral` it does not apply `DigiDollar::IsValidMintAmount`/post-activation mint bounds before returning a quote.
- Why not fixed yet: this is an RPC validation hardening issue, not a mint acceptance bypass. It needs a focused functional RPC regression and a separate commit.

### DD-RH-059 - Stats index assumes fixed mint output order while consensus accepts reordered mint outputs

- Severity: Medium.
- Status: confirmed open; not fixed in Wave 3.
- Affected invariant: index-backed DD supply/collateral stats must account for every consensus-valid mint.
- Reachable path:
  - Consensus mint validation scans all outputs and permits a non-P2TR DGB change output before the vault/token/OP_RETURN outputs.
  - `src/index/digidollarstatsindex.cpp:214-252` assumes `vout[0]` is the vault, `vout[1]` is the token, and `vout[2]` is OP_RETURN, so a consensus-valid reordered mint can be skipped by the stats index.
- Why not fixed yet: requires a focused index regression that crafts/mines a reordered consensus-valid mint and then teaches the index to scan outputs like validation. It is in scope but belongs to an index/reorg/cache wave or a separate fix commit.

## Wave 3 - Collateral Accounting, Rounding, Overflow

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: attack collateral math, cents/COIN conversions, overflow clamps.
- Agent B - Invariant/test breaker: attack boundary tests for ratios, MAX_MONEY, `__int128` casts, invalid amounts.
- Agent C - Boundary adversary: attack multi-output txbuilder collateral/change/fee behavior.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle behavior.

Results:

- Confirmed and fixed DD-RH-056 (High): validator DCA table now matches chainparams/wallet/RPC policy. Commit `d3078dd9e7`.
- Confirmed and left open DD-RH-057 (Low): reachable signed overflow/UB in unused `estimatecollateral` USD-value math.
- Confirmed and left open DD-RH-058 (Low): `calculatecollateralrequirement` quotes invalid mint amounts.
- Confirmed and left open DD-RH-059 (Medium): DD stats index can miss consensus-valid reordered mint outputs.
- Re-carried DD-RH-055 (Low): `senddigidollar` change amount is still open and needs a reliable confirmed-DD regression path after DD-RH-052.

Rejected false positives:

- Main mint collateral calculation overflow in `CalculateRequiredCollateral`: rejected as currently guarded with `__int128` and capped at `MAX_MONEY`.
- TxBuilder returning `0` when required collateral exceeds `MAX_MONEY`: rejected as a wallet-side fail-closed path, not an undercollateralized mint acceptance path.
- Partial redemption proportional collateral math: not reachable through current wallet/RPC redemption because exact full-vault redemption is enforced before builder use.
- Legacy `src/consensus/digidollar_transaction_validation.cpp` 64-bit collateral math: no production caller found for the active DD validation path.

Theoretical / not yet reachable:

- Process-local health cache and ERR/DCA sync-state dependency remains an architectural risk related to `ARCH-RH-002`, but Wave 3 did not produce a new deterministic exploit beyond the already recorded IBD/catch-up skip-oracle issue.
- Qt mint random owner keys and mint change fallback were flagged by Agent C; both are wallet/Qt boundary risks that require focused wallet/Qt crash/restore regression in Waves 9-12 before promotion/fix.

Commands and results:

- Required live surface enumeration: completed, output count `2034` in `/tmp/red_hornet_wave3_surface.txt`.
- `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=rh64_dca_table_disagreement_tests --log_level=all --report_level=short`: failed before DD-RH-056 fix with the intended DCA mismatch.
- `make -C src -j$(nproc) test/test_digibyte`: passed after DD-RH-056 patch.
- `src/test/test_digibyte --run_test=rh64_dca_table_disagreement_tests --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_dca_tests --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_rh32_collateral_dca_tests --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_t2_05_tests --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_err_attack_tests --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_rh13_economic_tests --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_rh35_chaos_tests --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_rh40_regression_tests --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_validation_tests --log_level=error --report_level=short`: passed with two existing warning-marked cases.

### DD-RH-060 - Invalid mint attempts could poison volatility state before rejection

- Severity: Medium.
- Status: fixed and committed.
- Commit: `6d5ea56332` (`digidollar volatility: fix DD-RH-060 invalid mint poisoning`).
- Affected invariant: malformed DigiDollar inputs must not mutate consensus-adjacent state or freeze later valid DD operations.
- Reachable exploit path:
  - `src/digidollar/validation.cpp:2057-2064` previously updated volatility state and called `VolatilityMonitor::RecordPrice()` for every DD mint-shaped transaction before dispatching to `ValidateMintTransaction`.
  - An attacker could submit a malformed mint that fails normal mint validation, for example an invalid `50` cent DD amount, while still appending the context oracle price into `VolatilityMonitor::priceHistory`.
  - Enough rejected mint attempts with manipulated context prices could change `ShouldFreezeMinting()` / `ShouldFreezeAll()` outcomes for later valid mint, transfer, or redeem attempts.
- Exact fixed code:
  - `src/digidollar/validation.cpp:2091-2132` now dispatches the type-specific validator first and returns immediately on failure.
  - `src/digidollar/validation.cpp:2120-2129` mutates volatility state only after the DD transaction has been accepted by type-specific validation.
- Regression test:
  - `src/test/digidollar_validation_tests.cpp:397` adds `transaction_validation_invalid_mint_does_not_mutate_volatility_state`.
- Failing evidence before fix:
  - `src/test/test_digibyte --run_test=digidollar_validation_tests/transaction_validation_invalid_mint_does_not_mutate_volatility_state --log_level=all --report_level=short`
  - Failed because `VolatilityMonitor::GetPriceHistory().empty()` was false after the invalid mint was rejected with `bad-dd-mint-amount`.
- Fix summary: move volatility price recording and height update behind successful DD type-specific validation. Invalid DD mints still reject with the original reason but can no longer append oracle prices or freeze follow-on operations.
- Passing evidence after fix:
  - `make -C src -j$(nproc) test/test_digibyte`: passed build/link.
  - `src/test/test_digibyte --run_test=digidollar_validation_tests/transaction_validation_invalid_mint_does_not_mutate_volatility_state --log_level=error --report_level=short`: passed `1 test case, 5 assertions`.
  - `src/test/test_digibyte --run_test=digidollar_validation_tests,digidollar_volatility_tests,digidollar_mint_tests,digidollar_redeem_tests,digidollar_err_tests,digidollar_err_attack_tests,digidollar_rh07_redemption_attacks,digidollar_rh41_timewarp_difficulty_tests --log_level=error --report_level=short`: passed `266 test cases`, with `2` existing warning-only test cases.
  - `src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_t6_04d_volatility_state_not_reverted_on_disconnect --log_level=error --report_level=short`: passed `1 test case, 3 assertions`.
  - `git diff --check`: passed.

## Wave 4 - Timelock, ERR, DCA, Volatility Bypass

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: attack redemption paths and protection state transitions.
- Agent B - Invariant/test breaker: tests for height/time edges, ERR burn requirements, DCA thresholds.
- Agent C - Boundary adversary: functional replay/restart/reorg scenarios around protections.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle behavior.

Results:

- Confirmed and fixed DD-RH-060 (Medium): rejected mint-shaped transactions no longer mutate volatility price history or freeze state.
- Confirmed architecture item `ARCH-RH-003`: volatility freeze is consensus-affecting but process-local, wall-clock influenced, and not persisted/reverted as chain state. No protocol patch was made without Jared approval.
- Confirmed architecture item `ARCH-RH-004`: ERR redemption currently routes every unhealthy-system redemption into an incomplete path that hard-rejects with `err-validation-incomplete`. No protocol patch was made without Jared approval.

Rejected false positives:

- Early redemption by setting `nLockTime=0` or below the committed lock height was not promoted. The structural DD validator checks transaction locktime, and valid collateral scripts include CLTV; current proof did not show a mined valid collateral spend bypassing script-level timelock enforcement.
- Minting collateral with a key-path spend to bypass CLTV was not proven reachable in this wave. Mint validation reconstructs the expected collateral P2TR/NUMS output and rejects mismatches before redemption.
- Pending redemption restart corruption was not confirmed. Existing wallet restart/rescan paths reactivate positions when the collateral UTXO remains live.

Theoretical / not yet reachable:

- A no-restart wallet UX/liveness gap may remain if a pending redemption leaves the mempool and the wallet does not promptly revalidate position state. No loss or deterministic exploit was proven in Wave 4.
- Functional protection coverage is weak: `test/functional/digidollar_protection.py` can pass with zero DD supply and zero locked collateral. This is a test-gap hardening item, not itself a live vulnerability.

ARCHITECTURAL_REVIEW_REQUIRED:

- `ARCH-RH-003` / High: `src/digidollar/validation.cpp:1435`, `src/digidollar/validation.cpp:1611`, `src/digidollar/validation.cpp:2120`, and `src/consensus/volatility.cpp:30-39` make volatility freeze decisions from static process-local state. `src/consensus/volatility.cpp:39` records prices with wall-clock timestamps, and `src/consensus/volatility.cpp:271` has reconstruction support but no proven production connect/disconnect persistence/replay path. Recommendation: either make volatility freeze state deterministic chain state with connect/disconnect/restart reconstruction, or remove it from consensus rejection and treat it as policy/UI until the protocol design is approved.
- `ARCH-RH-004` / High: `src/digidollar/validation.cpp:1611-1616` routes unhealthy-system redemptions to `ValidateEmergencyRedemptionConditions`, but `src/digidollar/validation.cpp:1729-1732` always rejects with `err-validation-incomplete`. `src/digidollar/validation.cpp:2068-2076` comments that normal redemptions should not be blocked during RED phase, but the lower redemption dispatcher still blocks them under `ctx.systemCollateral < 100`. Recommendation: Jared must choose one protocol behavior before launch: fully implement ERR burn validation, explicitly allow normal redemptions until ERR is complete, or disable/user-hide ERR claims.

Commands and results:

- Required live surface enumeration: completed, output count `2034` in `/tmp/red_hornet_wave4_surface.txt`.
- `src/test/test_digibyte --run_test=digidollar_validation_tests/transaction_validation_invalid_mint_does_not_mutate_volatility_state --log_level=all --report_level=short`: failed before fix with non-empty price history after invalid mint rejection.
- `make -C src -j$(nproc) test/test_digibyte`: passed after DD-RH-060 patch.
- `src/test/test_digibyte --run_test=digidollar_validation_tests/transaction_validation_invalid_mint_does_not_mutate_volatility_state --log_level=error --report_level=short`: passed.
- `src/test/test_digibyte --run_test=digidollar_validation_tests,digidollar_volatility_tests,digidollar_mint_tests,digidollar_redeem_tests,digidollar_err_tests,digidollar_err_attack_tests,digidollar_rh07_redemption_attacks,digidollar_rh41_timewarp_difficulty_tests --log_level=error --report_level=short`: passed with two existing warning-only test cases.
- `src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_t6_04d_volatility_state_not_reverted_on_disconnect --log_level=error --report_level=short`: passed.
- Agent B baseline command `./src/test/test_digibyte --run_test=digidollar_err_tests,digidollar_err_attack_tests,digidollar_dca_tests,digidollar_volatility_tests,digidollar_rh07_redemption_attacks_tests,digidollar_rh20_time_ordering_tests,digidollar_rh41_timewarp_difficulty_tests,digidollar_rh64_dca_table_disagreement_tests`: passed `133 test cases`.
- Agent B `python3 test/functional/digidollar_protection.py`: passed but recorded as weak coverage because it completed with zero DD supply/collateral.
- Agent C `./src/test/test_digibyte --run_test=digidollar_rh41_timewarp_difficulty_tests`: passed `8 test cases`.
- Agent C `./src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_t6_04d_volatility_state_not_reverted_on_disconnect`: passed `1 test case`.

### DD-RH-061 - Mint validation accepted OP_RETURN transaction-type mismatches

- Severity: Medium.
- Status: fixed and committed.
- Commit: `c677a8056b` (`digidollar mint: fix DD-RH-061 OP_RETURN type mismatch`).
- Affected invariant: a DD mint must have self-consistent consensus transaction type metadata; wallet/index/extractor surfaces must not see a different DD type than consensus validation accepted.
- Reachable exploit path:
  - `src/digidollar/validation.cpp` parsed the current `"DD"` OP_RETURN `txType` during mint validation but did not require it to equal `DD_TX_MINT`.
  - A hand-built transaction with DD mint `nVersion` and a transfer-shaped OP_RETURN could satisfy mint collateral/amount checks while downstream parsers classified the metadata differently.
- Exact fixed code: `src/digidollar/validation.cpp` rejects mint OP_RETURN type mismatches with `bad-mint-opreturn-type`.
- Regression test: `src/test/digidollar_validation_tests.cpp` adds `transaction_validation_mint_rejects_opreturn_type_mismatch`.
- Failing evidence before fix:
  - `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_validation_tests/transaction_validation_mint_rejects_opreturn_type_mismatch --log_level=all --report_level=short`
  - Failed because the mismatched mint was accepted.
- Fix summary: mint validation now enforces that current-format DD OP_RETURN metadata carries `DD_TX_MINT`.
- Passing evidence after fix:
  - Focused regression passed.
  - `src/test/test_digibyte --run_test=digidollar_validation_tests,digidollar_mint_tests --log_level=error --report_level=short`: passed `128` test cases plus `2` existing warning-only cases.
  - `src/test/test_digibyte --run_test=digidollar_rh06_mint_attacks --log_level=error --report_level=short`: passed `36` test cases.
  - `src/test/test_digibyte --run_test=digidollar_rh12_script_attacks_tests --log_level=error --report_level=short`: passed `12` test cases.

### DD-RH-062 - Redemption validation could read legacy OP_RETURN while later extraction read current OP_RETURN

- Severity: High.
- Status: fixed and committed.
- Commit: `55c2e95f1a` (`digidollar redeem: fix DD-RH-062 OP_RETURN ambiguity`).
- Affected invariant: a redemption cannot unlock collateral without burning the exact DD amount that later wallet/consensus source extraction treats as spent.
- Reachable exploit path:
  - An attacker spends a redeemable mint collateral input and DD inputs totaling `100000` cents.
  - The transaction includes a zero-value DD change output, a legacy `OP_RETURN OP_DIGIDOLLAR 90000`, and a current `OP_RETURN "DD" REDEEM 100000`.
  - Before the fix, `ValidateRedemptionTransaction` used generic `ExtractDDAmount()` on the first DD-looking OP_RETURN and treated the change as `90000` cents, so it believed `10000` cents were burned and released collateral.
  - Later source extraction from block DB ignores legacy `OP_DIGIDOLLAR` metadata and reads the current `"DD"` OP_RETURN, so the same change output can be treated as `100000` cents.
- Exact fixed code: `src/digidollar/validation.cpp` now requires exactly one current-format DD OP_RETURN of type `DD_TX_REDEEM` for redemption metadata and rejects legacy/conflicting DD metadata.
- Regression test: `src/test/digidollar_rh07_redemption_attacks_tests.cpp` adds `rh07_02d_legacy_opreturn_redeem_change_preserves_burn`.
- Failing evidence before fix:
  - `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_rh07_redemption_attacks/rh07_02d_legacy_opreturn_redeem_change_preserves_burn --log_level=all --report_level=short`
  - Failed because validation accepted the redemption and later extraction saw the full `100000` cent change amount.
- Fix summary: redemption metadata parsing is strict and single-source. Legacy DD OP_RETURNs are rejected on redemption, conflicting DD metadata is rejected, tx type must be `REDEEM`, and the DD amount must be positive.
- Passing evidence after fix:
  - `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_rh07_redemption_attacks/rh07_02d_legacy_opreturn_redeem_change_preserves_burn --log_level=error --report_level=short`: passed.
  - `src/test/test_digibyte --run_test=digidollar_rh07_redemption_attacks,digidollar_no_partial_redeem_tests,digidollar_redeem_tests,digidollar_validation_tests,digidollar_transfer_tests,digidollar_change_tests --log_level=error --report_level=short`: passed `199` test cases plus `2` existing warning-only cases.
  - `git diff --check`: passed.

## Wave 5 - Script, OP_RETURN, Metadata, Address Abuse

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: malformed script/template/OP_RETURN exploit paths.
- Agent B - Invariant/test breaker: fuzz/regression coverage for parser and classifier edges.
- Agent C - Boundary adversary: wallet/index/scan confusion and address display/routing attacks.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle behavior.

Results:

- Confirmed and fixed DD-RH-061 (Medium): mint OP_RETURN type now must match mint transaction version. Commit `c677a8056b`.
- Confirmed and fixed DD-RH-062 (High): redemption metadata parsing no longer allows legacy/current OP_RETURN disagreement. Commit `55c2e95f1a`.
- Confirmed candidate DD-RH-063: wallet/rescan may credit fake or unspendable DD-looking P2TR outputs from spoofed metadata. This needs a focused wallet regression before patch.
- Confirmed candidate DD-RH-064: DigiDollar address validation appears to accept mainnet/testnet/regtest DD prefixes across networks. This needs a focused address/RPC regression before patch.
- Re-carried DD-RH-059: stats index output-order mismatch remains open for an index/reorg wave.

Rejected false positives:

- Non-DD source transaction inflation was not promoted; current production extractors reject the reviewed source paths.
- Multiple current-format `"DD"` OP_RETURN mint/transfer cases were already blocked in the reviewed validator paths.
- Malformed `CScriptNum` dispatcher crashes were not reproduced; existing exceptions reject malformed metadata in the reviewed paths.

Theoretical / not yet reachable:

- Oracle V01/V02 bundle formats appear to tolerate trailing bytes in some parser paths. No production acceptance exploit was proven in Wave 5; carry to oracle waves.
- Raw RPC and restored wallet history can display DD token P2TR outputs as normal DGB Taproot addresses. This is potentially deceptive, but Wave 5 did not prove a direct loss path beyond the already logged wallet/accounting concerns.
- Miner-only rescan confusion with decoy OP_RETURN was preserved for wallet/rescan waves; no deterministic exploit was completed.

Commands and results:

- Required live surface enumeration: completed, output count `2034`.
- `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_validation_tests/transaction_validation_mint_rejects_opreturn_type_mismatch --log_level=all --report_level=short`: failed before DD-RH-061 fix, passed after fix.
- `src/test/test_digibyte --run_test=digidollar_validation_tests,digidollar_mint_tests --log_level=error --report_level=short`: passed after DD-RH-061 fix.
- `src/test/test_digibyte --run_test=digidollar_rh06_mint_attacks --log_level=error --report_level=short`: passed after DD-RH-061 fix.
- `src/test/test_digibyte --run_test=digidollar_rh12_script_attacks_tests --log_level=error --report_level=short`: passed after DD-RH-061 fix.
- `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_rh07_redemption_attacks/rh07_02d_legacy_opreturn_redeem_change_preserves_burn --log_level=all --report_level=short`: failed before DD-RH-062 fix, passed after fix.
- `src/test/test_digibyte --run_test=digidollar_rh07_redemption_attacks,digidollar_no_partial_redeem_tests,digidollar_redeem_tests,digidollar_validation_tests,digidollar_transfer_tests,digidollar_change_tests --log_level=error --report_level=short`: passed after DD-RH-062 fix.

### DD-RH-065 - Miner template prevalidation skipped valid DD mints after deterministic block-oracle enforcement

- Severity: Medium.
- Status: fixed and committed.
- Commit: `6b481044b2` (`miner oracle: fix DD-RH-065 DD block template validation`).
- Affected invariant: after activation, valid DD transactions accepted into mempool must be mineable with a deterministic block oracle bundle; activation boundary tests must prove post-activation acceptance is not mempool-only.
- Reachable exploit/liveness path:
  - DD-RH-052 made `GetOraclePriceForTransaction(tx, nHeight > 0, blockOraclePrice=0)` refuse local oracle fallback.
  - `src/node/miner.cpp:191` used `nHeight = pindexPrev->nHeight + 1` while selecting DD transactions from mempool, before the block oracle bundle was added to the coinbase.
  - As a result, valid DD mints entered mempool after activation but the miner skipped them with `bad-oracle-price`; `test/functional/digidollar_activation_boundary.py` failed because the mined block omitted the DD mint.
  - In regtest activation windows where BIP9 is active before `nDigiDollarPhase2Height`, block building also needed a valid phase-one oracle bundle instead of an empty or mismatched bundle.
- Exact fixed code:
  - `src/node/miner.cpp:191` uses advisory mempool oracle pricing for template preselection only.
  - `src/validation.cpp:2843` extracts deterministic block oracle price during `TestBlockValidity` as well as real `ConnectBlock`, but cache mutation stays disabled during `fJustCheck`.
  - `src/oracle/bundle_manager.cpp:549` computes the phase-specific required bundle count and can create a regtest mock phase-one bundle before Phase Two activates.
  - `src/validation.cpp:144` accepts v0x01 oracle bundles only before Phase Two.
- Regression test: existing `test/functional/digidollar_activation_boundary.py` now asserts the post-activation DD mint is included in the mined block and still purged from mempool after reorg below activation.
- Failing evidence before fix:
  - `python3 test/functional/digidollar_activation_boundary.py` failed with `AssertionError: DD tx should be in mined block`.
  - Debug log showed `CreateNewBlock(): skipping DD tx ... bad-oracle-price`.
- Fix summary: template preselection uses local/mempool oracle state as a policy hint; block validity remains deterministic because `ConnectBlock` reads the committed block oracle bundle. Regtest phase-one mock bundles are generated only on regtest and only when the active phase requires one oracle message.
- Passing evidence after fix:
  - `make -C src -j$(nproc) digibyted test/test_digibyte`: passed build/link.
  - `python3 test/functional/digidollar_activation_boundary.py`: passed.
  - `python3 test/functional/digidollar_activation.py && python3 test/functional/digidollar_rpc_gating.py`: passed.
  - `src/test/test_digibyte --run_test=oracle_phase2_tests --log_level=error --report_level=short`: passed `29` test cases.
  - `src/test/test_digibyte --run_test=oracle_bundle_manager_tests --log_level=error --report_level=short`: passed `25` test cases.
  - `src/test/test_digibyte --run_test=oracle_miner_tests --log_level=error --report_level=short`: passed `6` test cases.
  - `src/test/test_digibyte --run_test=digidollar_activation_tests,digidollar_rh31_consensus_fork_tests,digidollar_rh47_consensus_fork_deep_tests,rh51_checkphase3_v1_split_tests,rh63_oracle_validator_escape_hatches_tests,rh65_mainnet_testnet_validator_parity_tests --log_level=error --report_level=short`: passed `75` test cases.
  - `git diff --check`: passed before commit.

## Wave 6 - Activation and Consensus-Split Risks

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: validation/chainparams/deployment gates and shared hooks.
- Agent B - Invariant/test breaker: enabled/disabled boundary and mempool/validation mismatch tests.
- Agent C - Boundary adversary: multi-node functional activation/reorg attack scenarios.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle behavior.

Results:

- Confirmed and fixed DD-RH-065 (Medium): valid post-activation DD mempool mints were not mineable because miner template prevalidation used block-path oracle semantics before adding the oracle bundle. Commit `6b481044b2`.
- Reconfirmed `ARCH-RH-002`: IBD/catch-up `skipOracleValidation` can accept economically invalid DD mints when oracle data is missing/malformed. No consensus architecture patch was made without Jared approval.
- Reconfirmed post-activation RH63 oracle escape hatches. DD-RH-052 and DD-RH-065 keep DD block validation deterministic for blocks with DD txs, but the broader optional/malformed oracle-output policy remains architectural.

Rejected false positives:

- Mempool and block DD activation gates use `IsDigiDollarEnabled` consistently for DD transaction admission; the reviewed mismatch did not permit inactive DD consensus acceptance.
- `getdigidollardeploymentinfo` is intentionally ungated and informational.

Theoretical / not yet reachable:

- Oracle P2P activation is height-gated via `nOracleActivationHeight`, not BIP9 DD activation. This creates pre-DD oracle traffic surface on mainnet, but no direct consensus exploit was proven in Wave 6.
- Regtest has BIP9/height-gate asymmetry (`ALWAYS_ACTIVE` versus `nDDActivationHeight=650`); this remains a test-network hazard unless it affects production-like functional tests.

Commands and results:

- Required live surface enumeration: completed, output count `2034` in `/tmp/red_hornet_wave6_surface.txt`.
- `src/test/test_digibyte --run_test=digidollar_activation_tests,digidollar_rh31_consensus_fork_tests,digidollar_rh47_consensus_fork_deep_tests,rh51_checkphase3_v1_split_tests,rh63_oracle_validator_escape_hatches_tests,rh65_mainnet_testnet_validator_parity_tests --log_level=error --report_level=short`: passed before the DD-RH-065 fix.
- `python3 test/functional/digidollar_activation.py`: passed.
- `python3 test/functional/digidollar_activation_boundary.py`: failed before DD-RH-065 with the DD mint left in mempool and omitted from the mined block; passed after the fix.
- `python3 test/functional/digidollar_rpc_gating.py`: passed.
- Post-fix oracle/miner/activation suites listed under DD-RH-065 all passed.

### DD-RH-066 - Oracle price cache survives reorg when oracle coinbase output is not vout[1]

- Severity: Medium.
- Status: fixed and committed.
- Commit: `f54bf8c59d` (`validation oracle: fix DD-RH-066 reorg cache rollback`).
- Affected invariant: reorgs must not leave oracle prices from disconnected blocks visible to DD validation, wallet/RPC pricing, or regtest mock oracle state.
- Reachable exploit path:
  - `src/oracle/bundle_manager.cpp:1102` scans every coinbase output for `OP_RETURN OP_ORACLE`.
  - `src/validation.cpp:2846` used that extractor on connect, so a valid block with `vout[0]=subsidy`, `vout[1]=normal payout`, and `vout[2]=OP_RETURN OP_ORACLE` updated the oracle cache.
  - `src/validation.cpp:2480` only checked `coinbase.vout[1]` on disconnect. If `vout[1]` was not the oracle output, `RemovePriceCache()` was skipped and regtest `MockOracleManager` stayed at the disconnected block's price.
- Exact fixed code:
  - `src/validation.cpp:2480` now mirrors connect behavior by calling `OracleBundleManager::ExtractOracleBundle(*block.vtx[0], disconnected_bundle)` before removing the height's cache entry.
  - `src/test/rh61_coinbase_price_cache_poisoning_tests.cpp` now clears the singleton oracle manager before/after each RH-61 test so intentional cache-poisoning tests do not leak into later reorg tests.
- Regression test:
  - New `test/functional/digidollar_oracle_reorg_cache.py` mines an active-DigiDollar regtest block with a spendable `vout[1]` and a compact oracle bundle at `vout[2]`, verifies the attacker price is applied, invalidates the block, and requires the mock oracle price to restore to the previous block.
- Failing evidence before fix:
  - `python3 test/functional/digidollar_oracle_reorg_cache.py` failed with `AssertionError: not(777777 == 500000)`.
- Fix summary: disconnect now removes oracle cache entries using the same all-output bundle scan as connect and validation, so any block that can update the oracle price can also roll that price back.
- Passing evidence after fix:
  - `make -C src -j$(nproc) digibyted`: passed.
  - `python3 test/functional/digidollar_oracle_reorg_cache.py`: passed.
  - `make -C src -j$(nproc) test/test_digibyte`: passed build/link.
  - `src/test/test_digibyte --run_test=digidollar_rh16_reorg_attacks_tests,rh61_coinbase_price_cache_poisoning_tests --log_level=error --report_level=short`: passed `18` test cases.
  - `python3 test/functional/digidollar_activation_boundary.py`: passed.
  - `git diff --check`: passed.

### DD-RH-067 - Abandoned DD transfer after reorg leaves sender's original DD UTXO missing

- Severity: Medium.
- Status: fixed and committed.
- Commit: `99b3c618a7` (`wallet digidollar: fix DD-RH-067 transfer reorg restore`).
- Affected invariant: wallet reorg/abandon handling must not permanently hide confirmed active-chain DD UTXOs or leave users unable to spend their restored DD after a transfer block is disconnected.
- Reachable exploit path:
  - `src/wallet/wallet.cpp:1568` calls `DigiDollarWallet::ProcessTransactionForDD()` on block connect.
  - `src/wallet/digidollarwallet.cpp:4032` erases confirmed-spent DD UTXOs and persists `EraseDDUTXO`.
  - `src/wallet/wallet.cpp:1581` marks disconnected block transactions inactive, but before this fix did not rebuild `dd_utxos` after disconnect or abandonment.
  - When the disconnected transfer was later removed from mempool and abandoned, normal wallet `IsSpent()` made the original DD output available again, but the DigiDollar `dd_utxos` map/database no longer tracked it.
- Exact fixed code:
  - `src/wallet/wallet.cpp:1390` now rebuilds DD UTXOs after `AbandonTransaction()`.
  - `src/wallet/wallet.cpp:1653` now rebuilds DD UTXOs after block disconnect state changes.
  - `src/wallet/digidollarwallet.cpp:3739` skips abandoned/conflicted wallet transactions during full DD UTXO scans so orphaned DD outputs are not resurrected as wallet state.
- Regression test:
  - New `test/functional/wallet_digidollar_transfer_reorg.py` mints DD, confirms a self-transfer that spends the original DD UTXO, invalidates the transfer block, restarts without mempool persistence or wallet rebroadcast, abandons the disconnected transfer, and requires the original `100000` cents to be spendable again.
  - The test is registered in `test/functional/test_runner.py`.
- Failing evidence before fix:
  - `python3 test/functional/wallet_digidollar_transfer_reorg.py` failed with `AssertionError: not(0 == 100000)` after `abandontransaction`.
- Fix summary: DD wallet UTXO tracking is rebuilt whenever wallet transaction state changes can make a previously confirmed DD spend inactive/abandoned; abandoned/conflicted DD-creating transactions are excluded from the rebuilt DD UTXO set.
- Passing evidence after fix:
  - `make -C src -j$(nproc) digibyted test/test_digibyte`: passed.
  - `python3 test/functional/wallet_digidollar_transfer_reorg.py`: passed.
  - `python3 test/functional/wallet_digidollar_reorg.py`: passed.
  - `python3 test/functional/wallet_digidollar_mint_reorg.py`: passed.
  - `src/test/test_digibyte --run_test=digidollar_utxo_lifecycle_tests,digidollar_wallet_security_tests,digidollar_rh28_wallet_chains_tests --log_level=error --report_level=short`: passed `57` test cases.
  - `python3 test/functional/wallet_digidollar_rescan.py`: passed.
  - `python3 test/functional/digidollar_transfer.py`: passed.
  - `git diff --check`: passed.

### DD-RH-068 - Stats index loses vault metadata across redeem reorg

- Severity: Medium.
- Status: fixed and committed.
- Commit: `76d4c1c852` (`index digidollar: fix DD-RH-068 stats reorg vault metadata`).
- Affected invariant: reorgs must not corrupt DigiDollar system supply/collateral reporting. A redeemed vault on an alternate branch must decrement `getdigidollarstats` exactly once.
- Reachable exploit path:
  - `src/index/digidollarstatsindex.cpp:270` wrote `DBVaultKey(mint_outpoint)` metadata on mint.
  - `src/index/digidollarstatsindex.cpp:300` depended on that side table to identify the DD amount/collateral for a redemption.
  - Before this fix, `src/index/digidollarstatsindex.cpp:307` erased the side-table entry on first redemption.
  - `CustomRewind()` restored aggregate totals from the pre-redeem height but did not restore the erased `DBVaultKey`, so a re-mined redeem on branch B could validate and confirm while the stats index failed to subtract supply/collateral.
- Exact fixed code:
  - `src/index/digidollarstatsindex.cpp` now treats vault side-table rows as immutable mint metadata rather than active-vault rows; redemption updates aggregate totals but does not erase the mint metadata.
  - `CustomRewind()` comments now document that active state is represented by per-height totals, while the side table exists to recover original mint amounts across reorgs.
- Regression test:
  - New `test/functional/digidollar_stats_reorg.py` mints a tier-0 vault, redeems it on branch A, invalidates the redeem block, then mines the same redeem on branch B and requires `getdigidollarstats` to return zero supply and zero active positions.
  - The test is registered in `test/functional/test_runner.py`.
- Failing evidence before fix:
  - `python3 test/functional/digidollar_stats_reorg.py` failed with `AssertionError: not(100000 == 0)` after branch B confirmed the redeem.
- Fix summary: keep vault mint metadata available after redemption so alternate-branch redemptions after a reorg can still be identified and subtracted by the stats index.
- Passing evidence after fix:
  - `make -C src -j$(nproc) digibyted`: passed.
  - `python3 test/functional/digidollar_stats_reorg.py`: passed.
  - `python3 test/functional/digidollar_redeem_stats.py`: passed.
  - `git diff --check`: passed.

## Wave 7 - Reorg, Replay, Rollback, Cache Corruption

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: connect/disconnect state mutation and cached metrics attacks.
- Agent B - Invariant/test breaker: tests for duplicate/reversed/missing state updates.
- Agent C - Boundary adversary: restart/reindex/rescan/reorg functional abuse.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle behavior.

Results so far:

- Confirmed and fixed DD-RH-066 (Medium): oracle cache disconnect logic only inspected `coinbase.vout[1]`, while connect and validation accepted oracle bundles in any coinbase output. Commit `f54bf8c59d`.
- Confirmed and fixed DD-RH-067 (Medium): wallet DD UTXO state was not rebuilt after a confirmed DD transfer block was disconnected and abandoned, leaving the sender's original DD UTXO missing from wallet balance/coin selection. Commit `99b3c618a7`.
- Confirmed and fixed DD-RH-068 (Medium): `digidollarstatsindex` aggregate rewind restored totals but lost vault metadata needed to account for an alternate-branch redeem after reorg. Commit `76d4c1c852`.

Rejected false positives:

- Direct `RemovePriceCache()` stale latest-price behavior was already fixed: `src/oracle/bundle_manager.cpp:2286` rewinds `cached_price` to the highest remaining cached height.
- Collateral position inactive-after-reorg was not promoted: `src/wallet/wallet.cpp:1603` reactivates inactive positions for disconnected collateral spends, and `src/wallet/digidollarwallet.cpp:4002` restores live collateral positions during post-scan validation.

Theoretical / not yet reachable:

- Restart cache availability mismatch: startup reloads recent oracle prices from the last 20 blocks, while the time freshness window can be longer than 20 blocks during an outage. No deterministic consensus or wallet-loss exploit has been proven yet.
- Health monitor redeem rollback can skip `OnRedeemDisconnected` if the original mint block cannot be read during disconnect. Current tests document the fragility, but no normal, non-corrupt active-chain exploit was proven in Wave 7.
- Script metadata registry entries are not chain-height scoped and are not cleaned on disconnect. Reviewed consensus paths prefer txindex/block-db amount lookup and reject unknown amounts, so this remains a local-state risk rather than a confirmed consensus exploit.

Commands and results so far:

- Required live surface enumeration: completed, output count `2034` in `/tmp/red_hornet_wave7_surface.txt`.
- `python3 test/functional/digidollar_oracle_reorg_cache.py`: failed before DD-RH-066, passed after fix.
- `src/test/test_digibyte --run_test=digidollar_rh16_reorg_attacks_tests,rh61_coinbase_price_cache_poisoning_tests --log_level=error --report_level=short`: failed once due to RH-61 singleton cache leakage, then passed after RH-61 test cleanup.
- `python3 test/functional/digidollar_activation_boundary.py`: passed after DD-RH-066.
- `git diff --check`: passed after DD-RH-066.
- `python3 test/functional/wallet_digidollar_transfer_reorg.py`: failed before DD-RH-067, passed after fix.
- `python3 test/functional/wallet_digidollar_reorg.py`: passed.
- `python3 test/functional/wallet_digidollar_mint_reorg.py`: passed.
- `src/test/test_digibyte --run_test=digidollar_utxo_lifecycle_tests,digidollar_wallet_security_tests,digidollar_rh28_wallet_chains_tests --log_level=error --report_level=short`: passed.
- `python3 test/functional/wallet_digidollar_rescan.py`: passed.
- `python3 test/functional/digidollar_transfer.py`: passed.
- `python3 test/functional/digidollar_stats_reorg.py`: failed before DD-RH-068, passed after fix.
- `python3 test/functional/digidollar_redeem_stats.py`: passed.

## Wave 8 - Mempool Relay, Conflict, Replacement

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: mempool admission and conflict handling for DD/oracle txs.
- Agent B - Invariant/test breaker: negative tests for replacement/double-spend/malformed conflict cases.
- Agent C - Boundary adversary: multi-node mempool functional attack scenarios.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle behavior.

Commands and results so far:

- Required live surface enumeration: completed, output count `2037` in `/tmp/red_hornet_wave8_surface.txt`.

Confirmed vulnerabilities:

- DD-RH-069 (Critical, `ARCHITECTURAL_REVIEW_REQUIRED`): non-DD collateral spend can potentially unlock DD collateral after the script timelock without requiring a DD burn.
  - Affected invariant: collateral must not unlock unless the matching DD is burned through a valid redemption.
  - Reachability path: `src/digidollar/scripts.cpp:61` creates the normal redemption Taproot leaf as CLTV plus owner signature only; `src/digidollar/scripts.cpp:113` commits that leaf into the collateral output; `src/digidollar/validation.cpp:2107` returns true for non-DD transactions; `src/validation.cpp:778` and `src/validation.cpp:2921` only run DD validation for DD-marked transactions; `src/wallet/digidollarwallet.cpp:6270` reconstructs the same normal script path for spending.
  - Proof evidence reviewed: `src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_non_dd_tx_can_spend_collateral_utxo --catch_system_errors=no --log_level=test_suite --report_level=short` passed for Agent A, proving current tests can construct a non-DD collateral spend path that DD validation does not reject.
  - Why not fixed in Wave 8: forcing all collateral spends through DD redemption validation is a consensus/protocol design choice. Options include removing the normal script leaf, making the script enforce DD burn directly, or adding consensus lookup of every spent input to detect DD collateral even when the spending transaction is not DD-marked. This needs Jared approval before implementation.
  - Status: open, architectural review required.

- DD-RH-070 (Medium): DD-marked transactions bypassed dust policy for unrelated positive-value DGB outputs.
  - Affected invariant: malformed DD inputs must not create practical UTXO-bloat or relay DoS beyond intended limits.
  - Reachability path: `src/policy/policy.cpp:102` classified the whole transaction as DigiDollar by version marker; `src/policy/policy.cpp:160` skipped dust checks for every output of such transactions; `src/digidollar/validation.cpp:1270` transfer validation skips non-zero DGB outputs for DD conservation, so an otherwise DD-shaped transaction could carry unrelated one-satoshi DGB outputs through standard relay policy.
  - Failing regression before fix: after strengthening `src/test/digidollar_rh17_mempool_attacks_tests.cpp`, `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_rh17_mempool_attacks_tests/rh17_05_dust_exemption_utxo_bloat --log_level=all --report_level=short` failed with `isStandard=true` and empty reason for positive-value DGB dust outputs in a valid DD-versioned transaction.
  - Fix summary: `src/policy/policy.cpp` now treats only valid DD transaction types as DD for policy exemptions and only exempts zero-value P2TR DD token outputs from dust checks. Ordinary DGB outputs inside DD transactions must satisfy the normal dust threshold.
  - Tests added/upgraded: `rh17_05_dust_exemption_utxo_bloat` now proves zero-value DD token outputs remain standard, then requires positive-value DGB dust outputs to be rejected with reason `dust`.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_rh17_mempool_attacks_tests/rh17_05_dust_exemption_utxo_bloat --log_level=error --report_level=short`: passed.
    - `src/test/test_digibyte --run_test=digidollar_rh17_mempool_attacks_tests,digidollar_rh33_mempool_relay_tests --log_level=error --report_level=short`: passed, `22` cases / `352` assertions.
    - `python3 test/functional/digidollar_transfer.py`: passed.
    - `python3 test/functional/digidollar_activation_boundary.py`: passed.
    - `git diff --check`: passed.
  - Commit: `3d77327ef8` (`policy digidollar: fix DD-RH-070 dust exemption`).
  - Status: fixed and committed.

Rejected false positives:

- Unconfirmed DD chaining/package relay: rejected. Wallet excludes unconfirmed DD UTXOs and validation rejects DD inputs with `MEMPOOL_HEIGHT` at `src/digidollar/validation.cpp:1338` and `src/digidollar/validation.cpp:1473`.
- DD transfer UTXO loss after reorg-to-mempool: rejected after DD-RH-067. `wallet_digidollar_transfer_reorg.py`, `wallet_digidollar_reorg.py`, and `wallet_digidollar_mint_reorg.py` passed in Wave 8 evidence.
- DD txs surviving reorg below activation: rejected. `src/validation.cpp:395` purges DD mempool entries after reorg below activation, and `digidollar_activation_boundary.py` passed.
- Forged oracle P2P pubkey acceptance: rejected. `src/net_processing.cpp:5462` verifies against configured chainparams pubkeys.
- Malformed DD OP_RETURN `CScriptNum` mempool exception: rejected as already fixed. `src/test/test_digibyte --run_test=rh60_mempool_dd_scriptnum_escape_tests --catch_system_errors=no --log_level=error --report_level=short` passed for Agent A.

Theoretical / not yet reachable:

- DD validation still runs before some cheap mempool gates such as finality/conflict/input availability. This is a low/medium DoS-hardening opportunity, but no crash, invalid acceptance, or resource blowup beyond ordinary rejected-tx cost was proven in Wave 8.
- Wallet-generated DD transactions are effectively non-RBF today. This avoids third-party replacement, but leaves fee-bump UX risk during fee spikes.
- Oracle P2P activation is height-only while DD activation is BIP9/min-height gated. No DD consensus exploit was proven because DD validation remains gated, but the boundary mismatch should stay on the architecture backlog.

Test gaps / non-security failures:

- `test/functional/rpc_getoracles_pending.py` fails before its stale-pending P2P check because it expects `last_price_usd` to be `int`/`float`; authproxy returns a `Decimal`. This is test drift, not a confirmed vulnerability.
- `test/functional/feature_oracle_p2p.py` exits green while catching `Method not found` for obsolete RPCs such as `broadcastoracleprice` / `getoraclemessages`; this is false-green coverage.
- `oracle_bundle_timing_tests` had two failing assertions around current regtest mock fallback producing an oracle output where the test expected none. This needs an owner decision on intended regtest fallback behavior.

Commands and results:

- Required live surface enumeration: completed, output count `2037` in `/tmp/red_hornet_wave8_surface.txt`.
- `src/test/test_digibyte --run_test=rh60_mempool_dd_scriptnum_escape_tests --log_level=error --report_level=short`: passed, `3` cases / `8` assertions.
- `src/test/test_digibyte --run_test=digidollar_rh17_mempool_attacks_tests,digidollar_rh33_mempool_relay_tests --log_level=error --report_level=short`: passed after DD-RH-070, `22` cases / `352` assertions.
- `python3 test/functional/digidollar_transfer.py`: passed.
- `python3 test/functional/digidollar_activation_boundary.py`: passed.
- Agent B focused suite `digidollar_rh17_mempool_attacks_tests,digidollar_rh33_mempool_relay_tests,digidollar_rh28_wallet_chains_tests,oracle_bundle_manager_tests,oracle_p2p_tests,oracle_phase2_tests,rh58_pending_partialsigs_unbounded_growth_tests`: passed, `143` cases.
- Agent C functional passes: `wallet_digidollar_transfer_reorg.py`, `wallet_digidollar_reorg.py`, `wallet_digidollar_pending_redeem_restart.py`, `digidollar_pending_position_status.py`, `digidollar_network_relay.py`, `digidollar_rpc_oracle.py`.
- Agent C C++ focused P2P suite `digidollar_rh17_mempool_attacks_tests,digidollar_rh33_mempool_relay_tests,oracle_p2p_tests,musig2_p2p_handling_tests,musig2_p2p_ingestion_tests`: passed, `57` cases / `494` assertions.
- `python3 test/functional/rpc_getoracles_pending.py`: failed due stale Decimal type assertion.
- `src/test/test_digibyte --run_test=musig2_p2p_network_attacks_tests,oracle_price_staleness_tests,oracle_bundle_timing_tests --log_level=error --report_level=short`: failed two `oracle_bundle_timing_tests` assertions, recorded as test/behavior drift pending owner decision.
- `git diff --check`: passed before DD-RH-070 commit.

## Wave 9 - Wallet Restore, Rescan, Persistence, Backup

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: wallet DB/persistence and restore attack paths.
- Agent B - Invariant/test breaker: lost/miscounted positions after rescan/reload, backup restore, abandoned/conflicted filtering, and wallet DB serialization.
- Agent C - Boundary adversary: encrypted/watch-only/backup restore and external wallet boundaries.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle wallet behavior.

Commands and results:

- Required live surface enumeration: completed, output count `2037` in `/tmp/red_hornet_wave9_surface.txt`.
- `python3 test/functional/wallet_digidollar_rescan.py`: passed before and after DD-RH-071.
- `python3 test/functional/wallet_digidollar_reindex.py`: passed.
- `python3 test/functional/wallet_digidollar_encryption.py`: passed before and after DD-RH-071. Note: `redeemdigidollar` locked-wallet error code remains `-1` in the test log, but the operation is still blocked while locked.
- `python3 test/functional/digidollar_wallet_restore_redeem.py`: passed before and after DD-RH-071.
- `python3 test/functional/wallet_digidollar_descriptors.py`: passed.
- `python3 test/functional/wallet_digidollar_encrypted_received_redeem.py`: passed before and after DD-RH-071.
- `src/test/test_digibyte --run_test=digidollar_wallet_security_tests,digidollar_persistence_wallet_tests,digidollar_restore_tests,digidollar_wallet_tests --log_level=error --report_level=short`: passed after DD-RH-071, `182` cases / `525` assertions.
- `src/test/test_digibyte --run_test=digidollar_wallet_security_tests/rh08_03b_foreign_mint_with_wallet_dgb_output_not_claimed --log_level=error --report_level=short`: failed before DD-RH-071, passed after fix.
- `python3 test/functional/digidollar_watchonly_rescan.py`: exited success, but still logged failed watch-only descriptor imports. Treated as weak evidence/test drift, not as a strong pass.
- `git diff --check`: passed.

Confirmed vulnerabilities:

- DD-RH-071 (Medium): restored/rescanned wallet could claim a foreign DD mint if the mint transaction also paid an ordinary DGB output to the wallet.
  - Affected invariant: wallet restore/rescan must not corrupt DD ownership/accounting state; RPC/Qt must not show foreign collateral positions as wallet spendable/redeemable.
  - Reachability path:
    - `src/wallet/wallet.cpp:1479` sends every DD transaction to `ProcessDDTxForRescan()` during rescan before normal wallet ownership filtering.
    - `src/wallet/digidollarwallet.cpp:2220` previously accepted a mint as wallet-owned if any input spent a wallet-owned ordinary DGB UTXO.
    - `src/wallet/digidollarwallet.cpp:2243` previously accepted a mint as wallet-owned if any non-OP_RETURN output was spendable by the wallet, including ordinary positive-value DGB change/payment.
    - Once misclassified, `src/wallet/digidollarwallet.cpp:2267` restored a collateral position and `src/wallet/digidollarwallet.cpp:2352` tracked the foreign DD token UTXO.
    - `src/rpc/digidollar.cpp:2094` reports redeem/spendable status without proving the DD owner key for each listed position.
  - Failing regression before fix:
    - Added `rh08_03b_foreign_mint_with_wallet_dgb_output_not_claimed`.
    - `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_wallet_security_tests/rh08_03b_foreign_mint_with_wallet_dgb_output_not_claimed --log_level=error --report_level=short` failed with both DD-RH-071 assertions: the wallet claimed a foreign collateral position and foreign DD token output because the mint paid a normal DGB output to the wallet.
  - Fix summary:
    - `src/wallet/digidollarwallet.cpp` now treats a mint as wallet-owned during rescan only if vout `1` is a zero-value P2TR DD token output and `IsDDOutputMine()` proves wallet spendability of that DD token.
    - Ordinary DGB inputs, DGB change outputs, and DGB payment outputs no longer prove DD mint ownership.
    - Encrypted owner-key matching now checks encrypted owner pubkeys even for the same mint txid, so a locked encrypted wallet can recognize its own DD outputs by public key without decrypting secrets.
  - Tests added/upgraded:
    - `src/wallet/test/digidollar_wallet_security_tests.cpp` added `rh08_03b_foreign_mint_with_wallet_dgb_output_not_claimed`.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_wallet_security_tests/rh08_03b_foreign_mint_with_wallet_dgb_output_not_claimed --log_level=error --report_level=short`: passed.
    - `src/test/test_digibyte --run_test=digidollar_wallet_security_tests,digidollar_persistence_wallet_tests,digidollar_restore_tests,digidollar_wallet_tests --log_level=error --report_level=short`: passed.
    - `python3 test/functional/wallet_digidollar_rescan.py`: passed.
    - `python3 test/functional/digidollar_wallet_restore_redeem.py`: passed.
    - `python3 test/functional/wallet_digidollar_encryption.py`: passed.
    - `python3 test/functional/wallet_digidollar_encrypted_received_redeem.py`: passed.
    - `git diff --check`: passed.
  - Commit: `91f5c751d6` (`wallet digidollar: fix DD-RH-071 foreign mint rescan claim`).
  - Status: fixed and committed.

Rejected false positives:

- DD spendable balance inflation from stale/abandoned/conflicted transactions: rejected. `ScanForDDUTXOs()` clears DD UTXOs and skips abandoned/conflicted wallet transactions, and Wave 7/9 reorg/rescan tests passed.
- Descriptor restore losing legitimate active DD positions: rejected after `wallet_digidollar_rescan.py`, `digidollar_wallet_restore_redeem.py`, and descriptor restore coverage passed after DD-RH-071.
- Encrypted DD owner/address key leakage: rejected. DD keys are encrypted on wallet encryption, plaintext DD DB rows are erased, and write RPCs require unlock.

Theoretical / not yet reachable:

- Existing pre-fix poisoned wallet DB rows are not actively scrubbed by DD-RH-071. This is acceptable for pre-launch hardening but should remain on the migration/backfill checklist if any test wallets were already exposed to older builds.
- `IsMyDDAddress()` still appears to depend mainly on plaintext address-key maps and standard wallet ownership. No reachable loss/accounting path was proven.

Test gaps / non-security failures:

- `test/functional/wallet_digidollar_restore.py` exits green after swallowing an assertion-helper misuse (`assert_greater_than()` called with three args). This is false-green restore coverage and should be fixed as test hygiene.
- `test/functional/digidollar_watchonly_rescan.py` exits green even when watch-only descriptor imports fail with `Cannot import descriptor without private keys...`; it must assert successful watch-only import before claiming coverage.
- `test/functional/wallet_digidollar_persistence_restart.py` is stale and exits success after invalid `mintdigidollar(100, 365)` parameters.

## Wave 10 - Wallet Key Handling and Accounting Abuse

Status: complete.

Assignments:

- Agent A - Exploit-path attacker: key ownership, spendability, watch-only, locked-wallet paths.
- Agent B - Invariant/test breaker: unit/regression tests for wallet DD accounting invariants.
- Agent C - Boundary adversary: RPC/Qt wallet state deception and privacy leak checks.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle wallet behavior.

Commands and results:

- Required live surface enumeration: completed, output count `2037` in `/tmp/red_hornet_wave10_surface.txt`.
- `make -C src -j$(nproc) digibyted`: passed after DD-RH-072.
- `make -C src -j$(nproc) test/test_digibyte`: passed after DD-RH-072.
- `python3 test/functional/digidollar_rpc_amount_filters.py`: failed before DD-RH-072 with `No exception raised` for a partial `getredemptioninfo` amount; passed after fix.
- `python3 test/functional/wallet_digidollar_descriptors.py`: failed before DD-RH-072 because a watch-only/private-key-disabled wallet reported `getredemptioninfo()["can_redeem"] == true`; passed after fix.
- `python3 test/functional/digidollar_rpc_redemption.py`: passed after DD-RH-072.
- `python3 test/functional/digidollar_redeem.py`: passed after DD-RH-072.
- `src/test/test_digibyte --run_test=digidollar_wallet_security_tests/rh08_03c_non_dd_opreturn_cannot_credit_dd_balance --log_level=test_suite --report_level=short`: failed before DD-RH-073 with non-DD DD-looking tx credited by both incremental block processing and wallet startup scan; passed after fix.
- `src/test/test_digibyte --run_test=digidollar_wallet_security_tests,digidollar_persistence_wallet_tests,digidollar_restore_tests,digidollar_wallet_tests,digidollar_transfer_tests --log_level=error --report_level=short`: passed after DD-RH-073, `226` cases / `698` assertions.
- `python3 test/functional/digidollar_transfer.py`: passed after DD-RH-073.
- `python3 test/functional/wallet_digidollar_rescan.py`: passed after DD-RH-073.
- `python3 test/functional/digidollar_wallet_restore_redeem.py`: passed after DD-RH-073.
- `python3 test/functional/wallet_digidollar_descriptors.py`: passed after DD-RH-073.
- `python3 test/functional/wallet_digidollar_encryption.py`: passed after DD-RH-073.
- `python3 test/functional/wallet_digidollar_encrypted_received_redeem.py`: passed after DD-RH-073.
- `git diff --check`: passed after DD-RH-072.
- `make -C src -j$(nproc) digibyted test/test_digibyte`: passed after DD-RH-074.
- `python3 test/functional/wallet_digidollar_pending_redeem_restart.py`: failed before DD-RH-074 with `balance["total"] == 0` after restart, passed after fix with balance restored and redemption retry accepted.
- `src/test/test_digibyte --run_test=digidollar_wallet_security_tests,digidollar_persistence_wallet_tests,digidollar_restore_tests,digidollar_wallet_tests,digidollar_redeem_tests,digidollar_no_partial_redeem_tests --log_level=error --report_level=short`: passed after DD-RH-074, `216` cases / `611` assertions.
- `python3 test/functional/wallet_digidollar_reorg.py`: passed after DD-RH-074.
- `python3 test/functional/digidollar_redeem.py`: passed after DD-RH-074.
- `python3 test/functional/digidollar_wallet_restore_redeem.py`: passed after DD-RH-074.
- `python3 test/functional/wallet_digidollar_encrypted_received_redeem.py`: passed after DD-RH-074.
- `python3 test/functional/wallet_digidollar_active_restore_redeem.py`: passed after DD-RH-074.
- `git diff --check`: passed after DD-RH-074.

Confirmed vulnerabilities:

- DD-RH-072 (Medium): `getredemptioninfo` advertised impossible or unsafe redemption states for watch-only wallets and partial redemption amounts.
  - Affected invariant: RPC/wallet surfaces cannot deceive users or automation into an impossible redemption path, and redemption cannot be partially simulated when the real vault close is exact/full-amount only.
  - Exploit path:
    - `src/rpc/digidollar.cpp:2980` parsed an optional `dd_amount`.
    - Before the fix, `src/rpc/digidollar.cpp:3052` considered only active/unlocked/collateral state and did not reject private-key-disabled watch-only wallets.
    - Before the fix, `src/rpc/digidollar.cpp:3057` reported `redeemable_dd` as `min(requested, minted)`, even though `redeemdigidollar` only supports closing the whole vault.
    - Wallets, exchanges, and UI code using `getredemptioninfo` as preflight could show a watch-only position as redeemable or quote a partial redemption that would fail later.
  - Failing regression before fix:
    - `test/functional/digidollar_rpc_amount_filters.py` added a DD-RH-072 negative case for partial amounts; it failed because no RPC error was raised.
    - `test/functional/wallet_digidollar_descriptors.py` added a watch-only `getredemptioninfo` check; it failed because `can_redeem` was true.
  - Fix summary:
    - `src/rpc/digidollar.cpp:2937` documents exact/full-vault semantics for the optional amount.
    - `src/rpc/digidollar.cpp:3014` now checks confirmation state and `src/rpc/digidollar.cpp:3015` checks private-key-disabled wallets.
    - `src/rpc/digidollar.cpp:3043` rejects any non-zero `dd_amount` that does not equal the full minted amount.
    - `src/rpc/digidollar.cpp:3052` requires a confirmed, active, unlocked, collateral-backed position in a wallet with private keys before reporting `can_redeem`.
    - `src/rpc/digidollar.cpp:3057` always reports the full vault amount as `redeemable_dd`.
  - Tests added/upgraded:
    - `test/functional/digidollar_rpc_amount_filters.py` now rejects partial integer and decimal-string redemption preflights.
    - `test/functional/wallet_digidollar_descriptors.py` now proves a watch-only descriptor wallet can monitor a position but cannot report it as redeemable.
    - `test/functional/digidollar_rpc_redemption.py` now checks exact-only `getredemptioninfo` behavior.
    - `test/functional/digidollar_redeem.py` now uses exact/full-vault preflight amounts.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) digibyted`: passed.
    - `make -C src -j$(nproc) test/test_digibyte`: passed.
    - `python3 test/functional/digidollar_rpc_amount_filters.py`: passed.
    - `python3 test/functional/wallet_digidollar_descriptors.py`: passed.
    - `python3 test/functional/digidollar_rpc_redemption.py`: passed.
    - `python3 test/functional/digidollar_redeem.py`: passed.
    - `git diff --check`: passed.
  - Commit: `dab712c2c5` (`rpc digidollar: fix DD-RH-072 redemption preflight`).
  - Status: fixed and committed.

- DD-RH-073 (High): wallet DD accounting credited non-DigiDollar transactions that carried DD-looking OP_RETURN metadata.
  - Affected invariant: wallet restore, block processing, RPC balances, and exchange deposit monitoring must not credit DD unless the creating transaction is a real DigiDollar transaction.
  - Exploit path:
    - `src/wallet/wallet.cpp:1579` calls `ProcessTransactionForDD()` for every transaction in a connected block.
    - Before the fix, `src/wallet/digidollarwallet.cpp:4048` parsed DD-looking OP_RETURN amounts without first requiring a DigiDollar version marker.
    - Before the fix, `src/wallet/digidollarwallet.cpp:4139` stored the zero-value P2TR output as a DD UTXO if the output key matched a wallet DD address key.
    - Before the fix, wallet startup scan at `src/wallet/digidollarwallet.cpp:3714` repeated the same OP_RETURN-only classification over `mapWallet`.
    - A normal v2 transaction paying `0` DGB to a victim DD P2TR output plus `OP_RETURN "DD" <2> <amount>` could poison local DD balance and persisted DD UTXO state even though consensus DD spends later reject non-DD source transactions.
  - Failing regression before fix:
    - Added `rh08_03c_non_dd_opreturn_cannot_credit_dd_balance` in `src/wallet/test/digidollar_wallet_security_tests.cpp:345`.
    - `make -C src -j$(nproc) test/test_digibyte && src/test/test_digibyte --run_test=digidollar_wallet_security_tests/rh08_03c_non_dd_opreturn_cannot_credit_dd_balance --log_level=test_suite --report_level=short` failed with four DD-RH-073 assertions: incremental block processing credited the non-DD tx, persisted a DD UTXO, startup scan found one DD UTXO, and the fake outpoint remained tracked.
  - Fix summary:
    - `src/wallet/digidollarwallet.cpp:3719` now skips non-DD transactions during startup DD UTXO scans.
    - `src/wallet/digidollarwallet.cpp:3747` requires OP_RETURN tx type to match the authoritative DigiDollar version type.
    - `src/wallet/digidollarwallet.cpp:4041` still erases confirmed spends of known DD UTXOs, but returns before creating any new DD credits unless the transaction has a DigiDollar version marker.
    - `src/wallet/digidollarwallet.cpp:4065`, `src/wallet/digidollarwallet.cpp:6838`, and `src/wallet/digidollarwallet.cpp:6992` reject mismatched OP_RETURN/version types in wallet receive paths.
    - `src/wallet/digidollarwallet.cpp:6813`, `src/wallet/digidollarwallet.cpp:6965`, and `src/wallet/digidollarwallet.cpp:7162` add defensive non-DD gates to legacy/public incoming-DD paths.
  - Tests added/upgraded:
    - `src/wallet/test/digidollar_wallet_security_tests.cpp:345` creates a normal v2 transaction with DD-looking metadata and a wallet-controlled zero-value P2TR output, then asserts both incremental processing and wallet startup scan refuse to credit it.
  - Passing evidence after fix:
    - `src/test/test_digibyte --run_test=digidollar_wallet_security_tests/rh08_03c_non_dd_opreturn_cannot_credit_dd_balance --log_level=error --report_level=short`: passed.
    - `src/test/test_digibyte --run_test=digidollar_wallet_security_tests,digidollar_persistence_wallet_tests,digidollar_restore_tests,digidollar_wallet_tests,digidollar_transfer_tests --log_level=error --report_level=short`: passed.
    - `python3 test/functional/digidollar_transfer.py`: passed.
    - `python3 test/functional/wallet_digidollar_rescan.py`: passed.
    - `python3 test/functional/digidollar_wallet_restore_redeem.py`: passed.
    - `python3 test/functional/wallet_digidollar_descriptors.py`: passed.
    - `python3 test/functional/wallet_digidollar_encryption.py`: passed.
    - `python3 test/functional/wallet_digidollar_encrypted_received_redeem.py`: passed.
    - `git diff --check`: passed.
  - Commit: `78641061dc` (`wallet digidollar: fix DD-RH-073 non-DD metadata credit`).
  - Status: fixed and committed.

- DD-RH-074 (High): pending `redeemdigidollar` mutated persisted DD balance and position state before confirmation, corrupting restart/retry recovery if the redemption left mempool.
  - Affected invariant: reorgs, restarts, wallet reloads, and mempool conflicts cannot corrupt DD supply/collateral/accounting state; redemption must not burn wallet-visible DD or unlock/close collateral state until the chain confirms the spend.
  - Exploit path:
    - `src/rpc/digidollar.cpp:1857` commits the accepted redemption transaction to the wallet after mempool acceptance.
    - Before the fix, the following block immediately erased selected DD UTXOs from memory and wallet DB and wrote DD change as if the redeem were confirmed.
    - Before the fix, `src/rpc/digidollar.cpp:1885` also marked the position inactive and unlocked collateral/DD-token outpoints before confirmation.
    - If the node restarted with `-persistmempool=0 -walletbroadcast=0`, the redeem transaction was no longer in mempool or chain, but persisted DD UTXO state still showed no balance. The position was restored as active, yet a retry could not find the DD required to redeem it.
  - Failing regression before fix:
    - `test/functional/wallet_digidollar_pending_redeem_restart.py:68` now asserts the DD balance is restored after restart and `test/functional/wallet_digidollar_pending_redeem_restart.py:71` retries redemption.
    - `python3 test/functional/wallet_digidollar_pending_redeem_restart.py` failed before the fix with `AssertionError: not(0 == 100000)`.
  - Fix summary:
    - `src/rpc/digidollar.cpp:1864` now defers DD UTXO erasure and DD change tracking until `ProcessTransactionForDD` sees the redeem confirmed in a block.
    - `src/rpc/digidollar.cpp:1890` keeps collateral and DD-token locks in place while the redeem is unconfirmed, so dropped or reorged redemptions do not expose the position to ordinary wallet spending.
    - `src/wallet/wallet.cpp:1401` adds `AbandonStaleDigiDollarRedeems()` for unconfirmed non-mempool DD redeem wallet transactions.
    - `src/wallet/wallet.cpp:3527` runs that stale-redeem abandon step after startup mempool sync and before DD wallet scanning, allowing live chain state to restore DD balance and retry ability.
  - Tests added/upgraded:
    - `test/functional/wallet_digidollar_pending_redeem_restart.py` now proves position state, DD balance, and redemption retry all recover when a pending redeem disappears across restart.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) digibyted test/test_digibyte`: passed.
    - `python3 test/functional/wallet_digidollar_pending_redeem_restart.py`: passed.
    - `src/test/test_digibyte --run_test=digidollar_wallet_security_tests,digidollar_persistence_wallet_tests,digidollar_restore_tests,digidollar_wallet_tests,digidollar_redeem_tests,digidollar_no_partial_redeem_tests --log_level=error --report_level=short`: passed.
    - `python3 test/functional/wallet_digidollar_reorg.py`: passed.
    - `python3 test/functional/digidollar_redeem.py`: passed.
    - `python3 test/functional/digidollar_wallet_restore_redeem.py`: passed.
    - `python3 test/functional/wallet_digidollar_encrypted_received_redeem.py`: passed.
    - `python3 test/functional/wallet_digidollar_active_restore_redeem.py`: passed.
    - `git diff --check`: passed.
  - Commit: `9386619eb2` (`wallet digidollar: fix DD-RH-074 pending redeem recovery`).
  - Status: fixed and committed.

ARCHITECTURAL_REVIEW_REQUIRED:

- DD-RH-075 (High, `ARCHITECTURAL_REVIEW_REQUIRED`): oracle operator private keys are written as plaintext wallet DB records.
  - Affected invariant: oracle operator key handling must not leave long-lived signing secrets exposed in wallet storage after wallet encryption.
  - Reachable path:
    - `src/rpc/digidollar.cpp:4213` generates and stores an oracle key from `createoraclekey`.
    - `src/wallet/wallet.cpp:4583` routes storage through `CWallet::StoreOracleKey()`.
    - `src/wallet/walletdb.cpp:772` serializes `CPrivKey` directly under `DBKeys::ORACLE_KEY`.
    - Wallet encryption at `src/wallet/wallet.cpp:874` explicitly migrates DigiDollar owner/address keys through `EncryptDDKeys()`, but there is no equivalent encrypted oracle-key migration path.
  - Why not patched in this wave: fixing this safely requires a wallet-storage/encryption format and migration decision for existing `ORACLE_KEY` rows, plus operator recovery/backup compatibility rules. The campaign instructions classify wallet-storage redesign as requiring Jared approval before implementation.
  - Recommended decision: add encrypted oracle-key records parallel to DD key encryption, migrate/delete plaintext oracle keys atomically during wallet encryption, and make `GetOracleKey()` require unlock for encrypted oracle keys. Add DB migration tests before implementation.

Rejected false positives:

- Locked-wallet redemption bypass through `redeemdigidollar`: rejected as a direct bypass because real redemption still needs signing and wallet unlock/private-key access. DD-RH-072 fixed the misleading preflight result; no production path was proven that signs a redemption while the wallet remains locked.

Theoretical / not yet reachable:

- `listdigidollarpositions` and some UI consumers may still be too optimistic about spendability language for locked wallets, but after DD-RH-072 no reachable signing or redemption bypass was proven.

## Wave 11 - RPC Abuse and Schema Confusion

Status: completed.

Assignments:

- Agent A - Exploit-path attacker: command registration, auth-sensitive surfaces, and unsafe hidden DigiDollar/oracle RPC paths.
- Agent B - Invariant/test breaker: invalid parameter/schema/unit/error-path tests for DigiDollar/oracle RPCs.
- Agent C - Boundary adversary: wallet/no-wallet/locked-wallet RPC matrix and boundary behavior.

Scope note: DigiDollar/oracle only; shared files only where they directly gate DigiDollar/oracle RPC behavior.

Commands and results:

- Required live surface enumeration: completed, output count `2037` in `/tmp/red_hornet_wave11_surface.txt`.
- `python3 test/functional/digidollar_rpc_redemption.py`: failed before DD-RH-076 because the redemption transaction did not pay the requested unlock address while the RPC response echoed that address.
- `make -C src -j$(nproc) digibyted test/test_digibyte`: passed after DD-RH-076.
- `python3 test/functional/digidollar_rpc_redemption.py`: passed after DD-RH-076.
- `python3 test/functional/digidollar_redeem.py`: passed after DD-RH-076.
- `python3 test/functional/wallet_digidollar_pending_redeem_restart.py`: passed after DD-RH-076.
- `python3 test/functional/digidollar_wallet_restore_redeem.py`: passed after DD-RH-076.
- `git diff --check`: passed after DD-RH-076.
- `python3 test/functional/digidollar_rpc_addresses.py`: failed before DD-RH-077 because regtest accepted re-encoded `DD` and `TD` DigiDollar addresses as valid; passed after fix.
- `make -C src -j$(nproc) digibyted test/test_digibyte`: passed after DD-RH-077.
- `./src/test/test_digibyte --run_test=digidollar_address_tests,digidollar_txbuilder_tests,digidollar_transfer_tests,digidollar_wallet_tests,digidollar_rh46_rpc_input_validation_tests --log_level=error --report_level=short`: passed after DD-RH-077, `265` cases / `720` assertions.
- `python3 test/functional/digidollar_send.py`: passed after DD-RH-077.
- `python3 test/functional/digidollar_transfer.py`: passed after DD-RH-077.
- `python3 test/functional/digidollar_rpc_amount_filters.py`: passed after DD-RH-077.
- `git diff --check`: passed after DD-RH-077.
- `python3 test/functional/digidollar_rpc_amount_filters.py`: failed before DD-RH-078 with `JSON value of type null is not of expected type bool (-3)` for `listdigidollarpositions(tier_filter=0)`.
- `make -C src -j$(nproc) digibyted test/test_digibyte`: passed after DD-RH-078.
- `python3 test/functional/digidollar_rpc_amount_filters.py`: passed after DD-RH-078.
- `python3 test/functional/digidollar_rpc_addresses.py`: passed after DD-RH-078.
- `./src/test/test_digibyte --run_test=digidollar_rh46_rpc_input_validation_tests,digidollar_rpc_tests,oracle_rpc_tests --log_level=error --report_level=short`: passed after DD-RH-078, `120` cases / `1522` assertions.
- `git diff --check`: passed after DD-RH-078.
- `python3 test/functional/digidollar_rpc_redemption.py`: failed before DD-RH-079 with `JSON value of type string is not of expected type number (-3)` for `redeemdigidollar(position_id, "100.00", address)`.
- `make -C src -j$(nproc) digibyted test/test_digibyte`: passed after DD-RH-079.
- `python3 test/functional/digidollar_rpc_redemption.py`: passed after DD-RH-079.
- `python3 test/functional/digidollar_redeem.py`: passed after DD-RH-079.
- `python3 test/functional/digidollar_rpc_amount_filters.py`: passed after DD-RH-079.
- `./src/test/test_digibyte --run_test=digidollar_redeem_tests,digidollar_no_partial_redeem_tests,digidollar_rh46_rpc_input_validation_tests --log_level=error --report_level=short`: passed after DD-RH-079, `96` cases / `200` assertions.
- `git diff --check`: passed after DD-RH-079.

Confirmed vulnerabilities:

- DD-RH-076 (High): `redeemdigidollar` ignored the requested collateral return address but reported that address as paid.
  - Affected invariant: RPC/wallet surfaces cannot deceive users into loss; redemption must return collateral to the address the caller requested or reject the request.
  - Exploit path:
    - `src/rpc/digidollar.cpp:1554` documents a `redemption_address` parameter and `src/rpc/digidollar.cpp:1580` parses it.
    - Before the fix, the transaction builder ignored that parameter and always generated a fresh wallet destination for `redeemParams.collateralDest`.
    - Before the fix, the RPC response still returned the caller-supplied string as `unlock_address`.
    - An exchange or operator could submit a customer DGB address, log the RPC response as paid to that customer, while the actual redemption transaction paid a different wallet-generated address.
  - Failing regression before fix:
    - `test/functional/digidollar_rpc_redemption.py:136` added `test_redeem_uses_requested_unlock_address`.
    - `python3 test/functional/digidollar_rpc_redemption.py` failed with `AssertionError: redemption transaction did not pay the requested unlock address`.
  - Fix summary:
    - `src/rpc/digidollar.cpp:1580` now tolerates null optional holes for the address parameter.
    - `src/rpc/digidollar.cpp:1713` decodes and validates a supplied DGB redemption address, then uses it as `redeemParams.collateralDest`.
    - `src/rpc/digidollar.cpp:1721` still generates a wallet-controlled destination when no address is supplied.
    - `src/rpc/digidollar.cpp:1742` keeps DGB fee change on a separate wallet-controlled destination.
    - `src/rpc/digidollar.cpp:1924` and `src/rpc/digidollar.cpp:1938` report the actual collateral return address used.
  - Tests added/upgraded:
    - `test/functional/digidollar_rpc_redemption.py:136` decodes the redemption transaction and proves an explicit unlock address appears in the output scripts.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) digibyted test/test_digibyte`: passed.
    - `python3 test/functional/digidollar_rpc_redemption.py`: passed.
    - `python3 test/functional/digidollar_redeem.py`: passed.
    - `python3 test/functional/wallet_digidollar_pending_redeem_restart.py`: passed.
    - `python3 test/functional/digidollar_wallet_restore_redeem.py`: passed.
    - `git diff --check`: passed.
  - Commit: `40f5ce8d1d` (`rpc digidollar: fix DD-RH-076 redemption address`).
  - Status: fixed and committed.

- DD-RH-077 (Medium): cross-network DigiDollar address prefixes were accepted by validation and send paths.
  - Affected invariant: RPC/Qt/wallet surfaces cannot misroute DD value or mislead senders into accepting addresses for the wrong network.
  - Exploit path:
    - `src/base58.cpp:188` decoded any syntactically valid `DD`, `TD`, or `RD` DigiDollar address and `src/base58.cpp:281` reported it valid without checking the active chain.
    - Before the fix, `src/rpc/digidollar.cpp:2321` returned `validateddaddress(...).isvalid == true` for cross-network prefixes.
    - Before the fix, `src/rpc/digidollar.cpp:1344` and `src/rpc/digidollar.cpp:1491` accepted the same cross-network address objects for `senddigidollar` and `sendmanydigidollar`.
    - A mainnet or regtest withdrawal service could validate and send to a payload encoded with a different network's `DD`/`TD`/`RD` prefix, violating the user's explicit network intent.
  - Failing regression before fix:
    - `test/functional/digidollar_rpc_addresses.py:143` re-encodes a valid regtest DD address with mainnet and testnet version bytes.
    - `python3 test/functional/digidollar_rpc_addresses.py` failed because `validateddaddress` returned `isvalid: true` for those cross-network addresses.
  - Fix summary:
    - `src/base58.cpp:286` adds `CDigiDollarAddress::IsValidForCurrentNetwork()` and `src/base58.cpp:315` adds a static current-network validator.
    - `src/rpc/digidollar.cpp:284` centralizes the expected DD address prefix for the active chain and `src/rpc/digidollar.cpp:306` returns a clear network-mismatch error.
    - `src/rpc/digidollar.cpp:1383`, `src/rpc/digidollar.cpp:1531`, `src/rpc/digidollar.cpp:2369`, `src/rpc/digidollar.cpp:2571`, and `src/rpc/digidollar.cpp:2679` now reject wrong-network DD addresses for sends, validation, import, and address-scoped balance queries.
    - `src/wallet/digidollarwallet.cpp:1156` and `src/wallet/digidollarwallet.cpp:5060` reject wrong-network addresses in wallet transfer backends, covering Qt and internal callers.
    - `src/digidollar/txbuilder.cpp:448` rejects wrong-network transfer recipients in the transaction builder.
  - Tests added/upgraded:
    - `test/functional/digidollar_rpc_addresses.py:23` adds Base58Check helpers for two-byte DD address versions.
    - `test/functional/digidollar_rpc_addresses.py:143` adds a regtest cross-network prefix rejection test.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) digibyted test/test_digibyte`: passed.
    - `python3 test/functional/digidollar_rpc_addresses.py`: passed.
    - `./src/test/test_digibyte --run_test=digidollar_address_tests,digidollar_txbuilder_tests,digidollar_transfer_tests,digidollar_wallet_tests,digidollar_rh46_rpc_input_validation_tests --log_level=error --report_level=short`: passed.
    - `python3 test/functional/digidollar_send.py`: passed.
    - `python3 test/functional/digidollar_transfer.py`: passed.
    - `python3 test/functional/digidollar_rpc_amount_filters.py`: passed.
    - `git diff --check`: passed.
  - Commit: `59019bac2f` (`wallet digidollar: fix DD-RH-077 network address validation`).
  - Status: fixed and committed.

- DD-RH-078 (Low): named optional DigiDollar/oracle RPC arguments with omitted earlier options crashed instead of using documented defaults.
  - Affected invariant: RPC surfaces must not deceive or break automated DigiDollar/oracle users through schema/default handling; balance, position, transaction, and oracle status queries must honor documented defaults.
  - Exploit path:
    - JSON-RPC named arguments fill omitted earlier optional arguments with `null`.
    - Before the fix, `src/rpc/digidollar.cpp:2054` called `get_bool()` on the null `active_only` slot for `listdigidollarpositions(tier_filter=0)`.
    - The same pattern existed in read and boundary RPCs including `listdigidollaraddresses`, `getdigidollarbalance`, `listdigidollartxs`, `getoracles`, plus optional comment/address slots in write RPCs.
    - Wallet/exchange monitoring code using documented named filters could fail closed or lose status visibility during DD mint/redeem monitoring.
  - Failing regression before fix:
    - `test/functional/digidollar_rpc_amount_filters.py:62` added named-argument calls for `listdigidollarpositions`, `getdigidollarbalance`, `listdigidollaraddresses`, and `listdigidollartxs`.
    - `python3 test/functional/digidollar_rpc_amount_filters.py` failed with `JSON value of type null is not of expected type bool (-3)` on `listdigidollarpositions(tier_filter=0)`.
  - Fix summary:
    - `src/rpc/digidollar.cpp:284` adds `OptionalParamIsSet()` for optional positional/named slots.
    - `src/rpc/digidollar.cpp:596`, `src/rpc/digidollar.cpp:678`, `src/rpc/digidollar.cpp:923`, `src/rpc/digidollar.cpp:1381`, `src/rpc/digidollar.cpp:1519`, `src/rpc/digidollar.cpp:1581`, `src/rpc/digidollar.cpp:1628`, `src/rpc/digidollar.cpp:2054`, `src/rpc/digidollar.cpp:2214`, `src/rpc/digidollar.cpp:2474`, `src/rpc/digidollar.cpp:2574`, `src/rpc/digidollar.cpp:2670`, `src/rpc/digidollar.cpp:2829`, `src/rpc/digidollar.cpp:3016`, `src/rpc/digidollar.cpp:3205`, `src/rpc/digidollar.cpp:4008`, and `src/rpc/digidollar.cpp:4355` now treat null optional slots as omitted defaults.
  - Tests added/upgraded:
    - `test/functional/digidollar_rpc_amount_filters.py:62` exercises named optional DD RPC arguments with earlier defaults omitted.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) digibyted test/test_digibyte`: passed.
    - `python3 test/functional/digidollar_rpc_amount_filters.py`: passed.
    - `python3 test/functional/digidollar_rpc_addresses.py`: passed.
    - `./src/test/test_digibyte --run_test=digidollar_rh46_rpc_input_validation_tests,digidollar_rpc_tests,oracle_rpc_tests --log_level=error --report_level=short`: passed.
    - `git diff --check`: passed.
  - Commit: `18364bbe9f` (`rpc digidollar: fix DD-RH-078 named option defaults`).
  - Status: fixed and committed.

- DD-RH-079 (Low): `redeemdigidollar` advertised an amount parameter but rejected decimal-string amounts accepted by DigiDollar preflight/send RPCs.
  - Affected invariant: RPC/wallet surfaces must not mislead redemption automation; the redemption command must parse the same DD amount formats documented and accepted by adjacent DD RPCs.
  - Exploit path:
    - `src/rpc/digidollar.cpp:1596` declares `dd_amount` as an RPC amount.
    - `getredemptioninfo(position_id, "100.00")` already accepts decimal-string DD amounts through `ParseDigiDollarRpcAmount`.
    - Before the fix, `src/rpc/digidollar.cpp:1627` parsed `redeemdigidollar` with `getInt<int64_t>()`, so the exact same `"100.00"` amount failed with a type error before redemption validation.
    - Wallets, exchanges, or operators could successfully preflight a decimal-string redemption and then fail the actual redemption call.
  - Failing regression before fix:
    - `test/functional/digidollar_rpc_redemption.py:148` changed the full-vault redemption call to use `"100.00"`.
    - `python3 test/functional/digidollar_rpc_redemption.py` failed with `JSON value of type string is not of expected type number (-3)`.
  - Fix summary:
    - `src/rpc/digidollar.cpp:1627` now uses `ParseDigiDollarRpcAmount()` for `redeemdigidollar`, matching send and redemption-preflight amount parsing.
  - Tests added/upgraded:
    - `test/functional/digidollar_rpc_redemption.py:148` now proves an exact full-vault decimal-string amount can redeem to the requested unlock address.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) digibyted test/test_digibyte`: passed.
    - `python3 test/functional/digidollar_rpc_redemption.py`: passed.
    - `python3 test/functional/digidollar_redeem.py`: passed.
    - `python3 test/functional/digidollar_rpc_amount_filters.py`: passed.
    - `./src/test/test_digibyte --run_test=digidollar_redeem_tests,digidollar_no_partial_redeem_tests,digidollar_rh46_rpc_input_validation_tests --log_level=error --report_level=short`: passed.
    - `git diff --check`: passed.
  - Commit: `e7460b6d3d` (`rpc digidollar: fix DD-RH-079 redeem amount parsing`).
  - Status: fixed and committed.

Potential vulnerabilities under Wave 11 proof/fix:

- Medium: wallet `fee_rate` parameters are misleading or ignored in several DigiDollar write RPCs. Impact is being separated from schema hygiene before promotion.
- Medium: no-index monitoring RPCs can repeatedly force disk flush plus full UTXO scans. Reachability is RPC-authenticated; resource impact proof is deferred to Wave 18.

Rejected false positives:

- Legacy `src/rpc/digidollar_transactions.cpp` commands are not registered in production.
- `sendoracleprice` remains removed/unregistered.
- Regtest mock oracle mutation RPCs are registered globally but are execution-gated to regtest.
- `startoracle` with an arbitrary private key does not bypass oracle authorization on testnet/mainnet because oracle key validation checks chainparams before running.
- Locked-wallet `startoracle` swallowing `RPC_WALLET_UNLOCK_NEEDED` was rejected: `EnsureWalletIsUnlocked()` throws a `UniValue` JSON-RPC error, while the local catch block only catches `std::exception`, so the unlock-needed RPC error propagates.
- `calculatecollateralrequirement` oracle-price help text unit mismatch is a real documentation/schema hazard but no reachable security impact was proven: the RPC is an estimator only and consensus mint validation uses live oracle data, not this caller-supplied estimate.
- Ignored `fee_rate` parameters were recorded as a hardening backlog item, not a confirmed vulnerability: no reachable loss-of-funds, consensus, or accounting impact was proven, and DD fee policy changes require a separate wallet/RPC design decision.

Theoretical / not yet reachable:

- `stoporacle` is a core RPC and any RPC-authenticated caller can stop a local oracle and clear pending messages. This is operationally sensitive, but authorization is currently coarse-grained at RPC credential level; no protocol bypass was proven.
- Authenticated no-index monitoring RPC calls may create avoidable validation latency by repeating full scans; Wave 18 will revisit this under explicit resource-exhaustion scope.

## Wave 12 - Qt Security and Loss-of-Funds UX Bugs

Status: completed.

Assignments:

- Agent A - Exploit-path attacker: stale/misleading Qt state that can cause DD/collateral loss.
- Agent B - Invariant/test breaker: Qt signal/slot/model tests and dangerous mismatches.
- Agent C - Boundary adversary: user-flow simulations for mint/send/redeem/positions/transactions.

Scope note: DigiDollar/oracle only; shared Qt/wallet files only where they directly gate DigiDollar/oracle UI behavior.

Commands and results:

- Required live surface enumeration: completed, output count `2037` in `/tmp/red_hornet_wave12_surface.txt`.

Confirmed vulnerabilities:

- DD-RH-080 (High): Qt could generate DigiDollar receive addresses for private-key-disabled/watch-only wallets.
  - Affected invariant: RPC/Qt/wallet surfaces cannot deceive users into receiving DD that the selected wallet cannot spend or account for.
  - Exploit path:
    - `src/qt/digidollarreceivewidget.cpp` calls `WalletModel::getNewDigiDollarAddress()` when the receive form creates a DD address.
    - Before the fix, `src/qt/walletmodel.cpp:1383` generated a new DD address without checking `privateKeysDisabled()`.
    - DD wallet accounting intentionally hides private-key-disabled/watch-only DD balances, so a user could share a Qt-generated address and later receive DD that the wallet UI cannot safely spend or show as usable.
    - The RPC path already rejected private-key-disabled DD address generation, so Qt was a reachable bypass of the intended safety rule.
  - Failing regression before fix:
    - `src/qt/test/digidollarwidgettests.cpp:285` added `privateKeyDisabledWalletCannotGenerateDigiDollarAddress()`.
    - `QT_QPA_PLATFORM=minimal ./src/qt/test/test_digibyte-qt -platform minimal` failed with actual address `RD3KmJuxrBkTn6Q1ZxEumTrKpLpe21k6qGUGkjyCfQoc3hES4Ei1`, expected empty string.
  - Fix summary:
    - `src/qt/walletmodel.cpp:1395` now refuses to generate a DD receive address when the Qt wallet model is backed by a private-key-disabled wallet.
    - `src/qt/test/digidollarwidgettests.cpp:285` locks the regression into the Qt wallet model test suite.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) digibyted test/test_digibyte qt/test/test_digibyte-qt`: passed.
    - `QT_QPA_PLATFORM=minimal ./src/qt/test/test_digibyte-qt -platform minimal`: passed, `DigiDollarWidgetTests` totals `37 passed, 0 failed`.
    - `./src/test/test_digibyte --run_test=digidollar_restore_tests,digidollar_wallet_tests,digidollar_key_encryption_tests,digidollar_wallet_security_tests,digidollar_rh46_rpc_input_validation_tests --log_level=error --report_level=short`: passed, `231` test cases / `608` assertions.
    - `python3 test/functional/wallet_digidollar_active_restore_redeem.py`: passed.
    - `python3 test/functional/digidollar_rpc_addresses.py`: passed.
    - `git diff --check`: passed.
  - Commit: `037b11ef15` (`qt digidollar: fix DD-RH-080 watch-only receive addresses`).
  - Status: fixed and committed.

- DD-RH-081 (High): Qt minting generated a random non-HD owner key for DD token outputs.
  - Affected invariant: wallet restore/rescan/backup flows cannot lose ownership of GUI-created DigiDollar positions.
  - Exploit path:
    - `src/qt/digidollarminwidget.cpp` user flow reaches `WalletModel::mintDigiDollar()`.
    - Before the fix, `src/qt/walletmodel.cpp:866` used `ownerKey.MakeNewKey(true)` for the DD token owner key.
    - The RPC mint path used a wallet-derived key, but the Qt path did not, so a GUI-created position could survive only in the local wallet file and fail descriptor/seed recovery after backup restore.
  - Failing regression before fix:
    - `src/qt/test/digidollarwidgettests.cpp:372` added `qtMintStoresDescriptorRecoverableOwnerKey()`.
    - With the old random-key Qt mint path, `QT_QPA_PLATFORM=minimal ./src/qt/test/test_digibyte-qt -platform minimal` failed at `dd_wallet->GetDDOutputSpendingKey(dd_txout, recovered_key)`.
  - Fix summary:
    - `src/wallet/wallet.cpp:2679` and `src/wallet/wallet.h:773` move the HD-derived DigiDollar key helper onto `CWallet`.
    - `src/rpc/digidollar.cpp:922` and `src/rpc/digidollar.cpp:2122` now use the shared helper instead of a private RPC-only helper.
    - `src/qt/walletmodel.cpp:866` now derives the Qt mint owner key from the wallet, making GUI mints follow the recoverable RPC behavior.
    - `src/qt/test/digidollarwidgettests.cpp:372` mints through `WalletModel` and proves the DD output spending key is recoverable from wallet-managed key material.
  - Passing evidence after fix:
    - `make -C src -j$(nproc) digibyted test/test_digibyte qt/test/test_digibyte-qt`: passed.
    - `QT_QPA_PLATFORM=minimal ./src/qt/test/test_digibyte-qt -platform minimal`: passed, `DigiDollarWidgetTests` totals `37 passed, 0 failed`.
    - `./src/test/test_digibyte --run_test=digidollar_restore_tests,digidollar_wallet_tests,digidollar_key_encryption_tests,digidollar_wallet_security_tests,digidollar_rh46_rpc_input_validation_tests --log_level=error --report_level=short`: passed, `231` test cases / `608` assertions.
    - `python3 test/functional/wallet_digidollar_active_restore_redeem.py`: passed.
    - `python3 test/functional/digidollar_rpc_addresses.py`: passed.
    - `git diff --check`: passed.
  - Commit: `f076874896` (`qt digidollar: fix DD-RH-081 recoverable mint owner keys`).
  - Status: fixed and committed.

Rejected false positives:

- Wrong-network Qt DD send addresses are blocked by the wallet/backend network-prefix validation added for DD-RH-077, so the Qt validator mismatch alone does not produce a broadcastable loss path.
- Wrong-wallet Qt execution was rejected because each `WalletView`/DigiDollar tab is bound to its own `WalletModel`; no cross-wallet DD transaction execution path was proven.
- Locked-wallet and watch-only send/mint/redeem button states are guarded by backend wallet checks before transaction creation or broadcast; no accepted transaction path was proven.
- Mint collateral display omits fees, but backend coin selection includes fees and fails unfunded transactions; this was not promoted to a loss-of-funds vulnerability in this wave.

Theoretical / not yet reachable:

- Qt mint/redeem confirmations do not show the final DGB fee.
- Qt DD send balance can stay stale until the next wallet/balance notification in DD-only flows.
- Qt redeem UI does not fully surface backend `status`, `can_redeem`, confirmation count, final unlock address, and DGB amount.
- Qt mint Clear resets to tier 0, which is currently a testing-only tier.
- Qt send amount error text mentions 8 decimals while DD validation enforces 2 decimals.

Fixes landed:

- `037b11ef15` closes DD-RH-080.
- `f076874896` closes DD-RH-081.

ARCHITECTURAL_REVIEW_REQUIRED:

- None added in Wave 12.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle Qt and wallet code paths.

## Wave 13 - Oracle Roster, Threshold, Config Attacks

Status: completed.

Assignments:

- Agent A - Exploit-path attacker: wrong oracle roster/order/threshold acceptance paths.
- Agent B - Invariant/test breaker: bitmap/config mismatch tests and malformed bundle regressions.
- Agent C - Boundary adversary: operator config/RPC/P2P boundary abuse.

Scope note: DigiDollar/oracle only; shared chainparams, validation, RPC, and P2P files only where they directly gate oracle roster/config behavior.

Commands and results:

- Required live surface enumeration: completed, output count `2037` in `/tmp/red_hornet_wave13_surface.txt`.
- `./src/test/test_digibyte --run_test=oracle_phase2_tests/phase2_extraction_derives_epoch_from_coinbase_height --log_level=error --report_level=short`: failed before DD-RH-082 with `extracted.epoch == expected_epoch` failure `[0 != 100]`.
- `make -C src -j$(nproc) test/test_digibyte`: passed after DD-RH-082 build.
- `./src/test/test_digibyte --run_test=oracle_phase2_tests/phase2_extraction_derives_epoch_from_coinbase_height --log_level=error --report_level=short`: passed after DD-RH-082, `1` test case / `8` assertions.
- `./src/test/test_digibyte --run_test=oracle_phase2_tests,oracle_bundle_manager_tests,rh50_oracle_keyset_alignment_tests,musig2_activation_tests --log_level=error --report_level=short`: passed after DD-RH-082, `78` test cases / `582` assertions.
- `./src/test/test_digibyte --run_test=rh56_oversized_bitmap_message_inflation_tests/rh56_extract_rejects_oversized_bitmap --log_level=error --report_level=short`: failed before DD-RH-083 with `messages=256, consensus_total=7`.
- `./src/test/test_digibyte --run_test=rh56_oversized_bitmap_message_inflation_tests --log_level=error --report_level=short`: passed after DD-RH-083, `3` test cases / `7` assertions.
- `./src/test/test_digibyte --run_test=oracle_phase2_tests/phase2_extraction_derives_epoch_from_coinbase_height --log_level=error --report_level=short`: passed after DD-RH-083, `1` test case / `8` assertions.
- `./src/test/test_digibyte --run_test=rh56_oversized_bitmap_message_inflation_tests,oracle_phase2_tests,oracle_bundle_manager_tests,musig2_bundle_manager_tests,musig2_aggregator_tests --log_level=error --report_level=short`: passed after DD-RH-083, `81` test cases / `49106` assertions.
- `git diff --check`: passed.

Confirmed vulnerabilities:

- DD-RH-082 (High): Phase Two oracle extraction hardcoded the bundle epoch to `0`, so roster validation used the epoch-0 active oracle set for later blocks.
  - Affected invariant: oracle prices cannot be accepted from the wrong roster/order/threshold for the block epoch being validated.
  - Exploit path:
    - `src/oracle/bundle_manager.cpp:1330` set extracted Phase Two bundles to epoch `0`.
    - `src/oracle/bundle_manager.cpp:2687` validates Phase Two messages by calling `GetActiveOraclesForEpoch(bundle.epoch)`.
    - At block height `1000` on regtest, `GetCurrentEpoch(1000)` is `100`, but a parsed Phase Two bundle was checked against epoch `0`.
    - Once oracle rotation differs between epochs, a quorum from the wrong selected roster could be counted for the current block.
  - Failing regression before fix:
    - `src/test/oracle_phase2_tests.cpp:131` added `phase2_extraction_derives_epoch_from_coinbase_height()`.
    - The test built a Phase Two OP_ORACLE coinbase at height `1000` and failed because `extracted.epoch` was `0` instead of `100`.
    - `src/test/digidollar_redteam_tests.cpp:7266` was upgraded from documenting the bug to asserting the fixed behavior.
  - Fix summary:
    - `src/oracle/bundle_manager.cpp:1103` now parses the BIP34 coinbase height once during oracle extraction.
    - `src/oracle/bundle_manager.cpp:1269` derives Phase One bundle epoch from that height when available.
    - `src/oracle/bundle_manager.cpp:1346` derives Phase Two bundle epoch from that height when available.
  - Tests added/upgraded:
    - `src/test/oracle_phase2_tests.cpp:131` new focused regression.
    - `src/test/digidollar_redteam_tests.cpp:7266` existing red-team test flipped from bug-documenting to fixed-invariant.
  - Commit: `fe05ecfc60` (`oracle phase2: fix DD-RH-082 epoch roster validation`).
  - Status: fixed and committed.

- DD-RH-083 (Medium): v0x03 MuSig2 extraction decoded malformed oversized bitmaps with `bitmap_size * 8`, inflating synthetic participants before consensus rejection.
  - Affected invariant: malformed oracle inputs cannot create practical resource amplification or misleading pre-validation bundle state.
  - Exploit path:
    - `src/oracle/bundle_manager.cpp:1197` decoded v0x03 participation bitmaps using `max(bitmap.size() * 8, nOracleTotalOracles)`.
    - A 32-byte bitmap on regtest decoded as 256 participants even though consensus has 7 oracle slots.
    - `src/oracle/bundle_manager.cpp:1204` then pushed 256 synthetic `COraclePriceMessage` entries before `ValidatePhaseThreeBundle()` later rejected the malformed bitmap.
  - Failing regression before fix:
    - `src/test/rh56_oversized_bitmap_message_inflation_tests.cpp:183` was flipped into `rh56_extract_rejects_oversized_bitmap()`.
    - Before the fix it failed with `messages=256, consensus_total=7`.
  - Fix summary:
    - `src/oracle/bundle_manager.cpp:1198` now decodes v0x03 bitmaps with the consensus oracle count only.
    - `src/oracle/bundle_manager.cpp:1201` rejects malformed/empty decode results before synthetic messages are created.
    - `src/test/musig2_bundle_manager_tests.cpp:35` updates the v0x03 fixture to use a valid regtest 7-slot bitmap.
  - Tests added/upgraded:
    - `src/test/rh56_oversized_bitmap_message_inflation_tests.cpp:183` now asserts extraction rejection for oversized bitmaps.
    - `src/test/musig2_bundle_manager_tests.cpp:196` verifies normal valid v0x03 extraction still succeeds for 7 regtest slots.
  - Commit: `da6b541ba4` (`oracle musig2: fix DD-RH-083 bitmap extraction bounds`).
  - Status: fixed and committed.

Rejected false positives:

- The old mainnet oracle-validation bypass is rejected as already fixed: the current `ValidateBlockOracleData()` path documents removal of the mainnet short-circuit and is reached from block validation.
- Fake-signature v0x02 MuSig2 downgrade is rejected today: existing RH05 coverage rejects fake-signed v0x02 bundles via Phase Two signature validation.
- Below-threshold, empty, and all-zero v0x03 bitmaps are rejected by `ValidatePhaseThreeBundle()`.
- Phase Three aggregate validation does not accept reserve IDs from the mainnet reserve-slot issue because bitmap decoding uses `params.nOracleTotalOracles`.

Theoretical / not yet reachable:

- `OracleBundleManager::ValidateBundle()` returns true for any v0x03 bundle and is an API footgun if a future caller treats it as full validation; current ConnectBlock validation does not rely on it for v0x03.
- `MuSig2OracleAggregator::EncodeBitmap()` still has a public hardcoded `ORACLE_CONSENSUS_REQUIRED` threshold. Current aggregate-key validation uses runtime thresholds, so no consensus bypass was proven in this wave.
- P2P handlers that accept oracle IDs using `GetOracleNode(id)` plus `is_active` may inherit the mainnet reserve-slot issue if the protocol decision is to keep only slots `0..16` consensus-active.

ARCHITECTURAL_REVIEW_REQUIRED:

- DD-RH-084 (High): mainnet declares 17 consensus/MuSig2 oracle slots but marks oracle IDs `17..29` active in `vOracleNodes`.
  - Risk: Phase Two active-set selection can sample reserve IDs from the 30 active nodes while Phase Three bitmap logic only supports IDs `0..16`; operator RPC/status surfaces also present reserve IDs as active operator slots.
  - Evidence:
    - `src/kernel/chainparams.cpp:313` sets mainnet Phase Two threshold to 9-of-17.
    - `src/kernel/chainparams.cpp:325` sets `nOraclePubkeyCount = 17`.
    - `src/kernel/chainparams.cpp:381` and following entries mark IDs `17..29` active.
    - `src/primitives/oracle.cpp:509` selects from all `is_active` nodes.
    - `src/oracle/bundle_manager.cpp:2687` uses that selected set for Phase Two validation.
    - `src/kernel/chainparams.cpp:1199` validates only slots `0..nOraclePubkeyCount-1`, so the extra active nodes pass config self-check.
    - `src/rpc/digidollar.cpp:4093` and `src/rpc/digidollar.cpp:4201` describe operator IDs `0..29`, which can mislead reserve operators if only 17 slots should be consensus-active.
  - Options:
    - Mark IDs `17..29` inactive until a protocol upgrade explicitly rotates them into consensus.
    - Raise the consensus/MuSig2 slot count and bitmap rules to 30 with a new threshold.
    - Keep reserve IDs active only for discovery/status but exclude them from Phase Two selection and operator-start eligibility.
  - Recommendation: Jared should decide the intended RC31 roster model before code changes; this is chainparams/protocol behavior and was not changed in Wave 13.

- DD-RH-085 (High): Phase Three-active heights still allow valid v0x02 Phase Two oracle bundles.
  - Risk: if the intended protocol is "MuSig2 only after Phase Three", a valid Phase Two bundle after Phase Three activation is a downgrade path.
  - Evidence:
    - `src/oracle/bundle_manager.cpp:2413` only sends `bundle.version == 3` to `ValidatePhaseThreeBundle()`.
    - Non-v3 bundles fall through to `ValidatePhaseTwoBundle()` at `src/oracle/bundle_manager.cpp:2443`.
    - `src/validation.cpp:144` permits v0x02 in contextual oracle version checks.
  - Recommendation: Jared should approve whether Phase Three is a hard v0x03 requirement or whether v0x02 fallback is an intentional transition mechanism.

- DD-RH-086 (Medium): malformed/unknown OP_ORACLE outputs are accepted after activation as transition-period behavior.
  - Risk: miners can include malformed oracle outputs that validation ignores, preserving liveness but weakening "oracle data present means valid oracle data" assumptions.
  - Evidence:
    - Failed extraction returns accepted in `src/oracle/bundle_manager.cpp:2401`.
    - Existing RH05 coverage documents unknown version `0xFF` acceptance.
  - Recommendation: Jared should decide whether this liveness escape hatch remains intentional post-launch or should become a consensus rejection once oracle launch is stable.

Fixes landed:

- `fe05ecfc60` closes DD-RH-082.
- `da6b541ba4` closes DD-RH-083.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle roster, config, chainparams, and parser code paths.

## Wave 14 - Oracle Feed Manipulation and Staleness

Status: in progress.

Assignments:

- Agent A - Exploit-path attacker: stale timestamp, outlier poisoning, fallback, exchange spoof paths.
- Agent B - Invariant/test breaker: aggregation/freshness tests and boundary cases.
- Agent C - Boundary adversary: functional/RPC status flows for bad feeds and recovery.

Scope note: DigiDollar/oracle only; shared RPC/P2P code only where it directly gates oracle price relay, freshness, and status reporting.

Commands and results so far:

- Required live surface enumeration: completed, output count `2037` in `/tmp/red_hornet_wave14_surface.txt`.
- `make -C src -j$(nproc) test/test_digibyte`: passed after adding DD-RH-087 regression to rebuild the unit-test binary.
- `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/phase2_rejects_stale_or_future_message_on_insert`: failed before DD-RH-087 with stale and future Phase Two signed messages accepted into pending state.
- `make -C src -j$(nproc) test/test_digibyte && ./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/phase2_rejects_stale_or_future_message_on_insert`: passed after DD-RH-087.
- `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests,oracle_p2p_tests,oracle_phase2_tests,oracle_price_staleness_tests`: passed after DD-RH-087, `95` test cases.
- `git diff --check`: passed after DD-RH-087.

Confirmed vulnerabilities:

- DD-RH-087 (Medium): Phase Two live oracle pending insertion accepted stale or future signed messages.
  - Affected invariant: oracle prices cannot be accepted from stale or future timestamps through relay or bundled relay paths.
  - Exploit path:
    - `src/net_processing.cpp:5607` handles `ORACLEBUNDLE` and calls `AddOracleMessage()` for each bundled message at `src/net_processing.cpp:5723`.
    - `src/oracle/bundle_manager.cpp:2193` validated Phase Two live messages by price, oracle ID, chainparams key binding, and signature only.
    - `src/oracle/bundle_manager.cpp:100` purged previously stale pending entries, but did not reject the incoming stale/future message itself.
    - A peer with valid signed oracle messages could deliver stale/future messages through bundled relay and have them stored long enough to count toward pending consensus/cache update.
  - Failing regression before fix:
    - `src/test/oracle_bundle_manager_tests.cpp:911` added `phase2_rejects_stale_or_future_message_on_insert()`.
    - Before the fix, `AddOracleMessage()` returned true for both stale and future signed Phase Two messages and pending count became `1`.
  - Fix summary:
    - `src/oracle/bundle_manager.cpp:2215` now rejects incoming Phase Two messages with `timestamp > GetTime() + 60` or `timestamp < GetTime() - ORACLE_MAX_AGE_SECONDS` before signature/storage.
    - This protects both direct `ORACLEPRICE` storage and `ORACLEBUNDLE` storage because both paths converge on `AddOracleMessage()`.
  - Tests added:
    - `src/test/oracle_bundle_manager_tests.cpp:911` direct stale/future insertion regression.
  - Commit: `f6c4f77543` (`oracle relay: fix DD-RH-087 stale pending messages`).
  - Status: fixed and committed.

- DD-RH-088 (Low): Oracle RPC status reported expired on-chain oracle bundles as fresh/reporting after the usable price cache had expired.
  - Affected invariant: RPC/operator surfaces cannot mislead operators or users about stale oracle prices.
  - Exploit path:
    - `src/rpc/digidollar.cpp:3236` correctly got `0` from `OracleBundleManager::GetLatestPrice()` once the cache expired.
    - `src/rpc/digidollar.cpp:3242` then scanned the last 20 blocks and counted the old bundle by height alone.
    - `src/rpc/digidollar.cpp:3300` treated a recent block height as fresh even when `GetTime() - bundle.timestamp > ORACLE_MAX_AGE_SECONDS`.
    - The shared scan path for `getalloracleprices`/`getoracles` at `src/rpc/digidollar.cpp:3618` also reused stale on-chain bundle messages as current reporting data.
  - Failing regression before fix:
    - `test/functional/digidollar_oracle_rpc_staleness.py` creates a valid 4-of-7 regtest oracle bundle, advances mock time by `ORACLE_MAX_AGE_SECONDS + 1` without mining, and then asserts RPCs mark oracle data stale/no_data.
    - Before the fix, `getoracleprice` returned `price_micro_usd=0` but `is_stale=false`.
  - Fix summary:
    - `src/rpc/digidollar.cpp:181` adds one shared freshness predicate requiring a positive timestamp, no more than 60 seconds in the future, and no older than `ORACLE_MAX_AGE_SECONDS`.
    - `getoracleprice`, `getalloracleprices`, `getoracles`, and `listoracle` now ignore stale/future on-chain and pending oracle timestamps for current status/reporting.
  - Tests added/upgraded:
    - `test/functional/digidollar_oracle_rpc_staleness.py` new functional regression.
    - `test/functional/rpc_getoracles_pending.py` accepts Decimal JSON number decoding and the existing `outlier` status vocabulary so the neighboring RPC suite validates the status paths.
  - Test evidence:
    - `test/functional/digidollar_oracle_rpc_staleness.py`: failed before fix, passed after fix.
    - `test/functional/digidollar_oracle_consistency.py`: passed after fix.
    - `test/functional/rpc_getoracles_pending.py`: passed after fix and narrow test harness correction.
  - Commit: `2217a58061` (`rpc oracle: fix DD-RH-088 stale status reporting`).
  - Status: fixed and committed.

- DD-RH-089 (Medium): Phase Two block-template creation used aged pending oracle messages/attestations after they exceeded the oracle freshness window.
  - Affected invariant: miners must not build oracle bundles from stale pending data, and stale relay/cache state must not poison block templates.
  - Exploit path:
    - `src/oracle/bundle_manager.cpp:676` copied all `pending_messages` into Phase Two block-template consensus without checking wall-clock freshness.
    - `src/oracle/bundle_manager.cpp:714` then accepted matching pending attestations without checking freshness.
    - If oracle data was valid when received but aged past `ORACLE_MAX_AGE_SECONDS` before the next template, a miner could add an expired oracle output to its block template; block validation would later reject stale timestamps, so the direct impact is miner/template availability and wasted work rather than accepted inflation.
  - Failing regression before fix:
    - `src/test/oracle_bundle_manager_tests.cpp:938` added `phase2_block_template_ignores_aged_pending_messages()`.
    - Before the fix, the test failed because `AddOracleBundleToBlock()` appended a second coinbase output from aged pending data (`vout.size() == 2`).
  - Fix summary:
    - `src/oracle/bundle_manager.cpp:53` adds a shared live-freshness predicate for manager relay/template state.
    - `src/oracle/bundle_manager.cpp:363` rejects stale/future consensus attestations on insert.
    - `src/oracle/bundle_manager.cpp:433` computes consensus only from fresh pending messages.
    - `src/oracle/bundle_manager.cpp:632` ignores stale cached bundles, stale pending messages, and stale attestations while building block templates.
    - `src/oracle/bundle_manager.cpp:2185` filters stale messages in explicit bundle creation too.
  - Tests added:
    - `src/test/oracle_bundle_manager_tests.cpp:938` block-template stale pending regression.
  - Test evidence:
    - `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/phase2_block_template_ignores_aged_pending_messages`: failed before fix, passed after fix.
    - `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/phase2_rejects_stale_or_future_message_on_insert`: passed after fix.
    - `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests,oracle_phase2_tests,oracle_miner_tests,oracle_integration_tests,oracle_price_staleness_tests`: passed after fix, `81` test cases.
    - `git diff --check`: passed.
  - Commit: `e81d5ccbdc` (`oracle miner: fix DD-RH-089 stale template bundles`).
  - Status: fixed and committed.

Rejected false positives:

- Stale exchange-price rebroadcast was rejected: `OracleNode::ShouldBroadcast()` requires `HasFreshExchangePrice()`, and `oracle_price_staleness_tests/recent_broadcast_price_does_not_authorize_stale_rebroadcast` remains passing.
- Exchange aggregation with fewer than three sources remains a configuration/theoretical risk rather than a production exploit: production `OracleNode::FetchMedianPrice()` sets `SetMinRequiredSources(3)` before fetching.
- Historical on-chain price cache reload was rejected as an accepted-price bug: `UpdatePriceCache(height, price, source_time)` preserves the oracle source timestamp, and `GetLatestPrice()` rejects it after `ORACLE_MAX_AGE_SECONDS`.

Theoretical / not yet reachable:

- Quorum/outlier poisoning through selective visibility or colluding signed feeds remains theoretical. It requires oracle-key compromise, compromised exchange feeds, or network visibility control; no unauthenticated spoof path was proven.
- `GetPendingMessages()` still returns raw pending entries to callers, including test-injected stale entries. Production RPC/P2P/template paths now apply freshness filters before using them, so this remains a raw API footgun rather than a currently proven exploit path.

Fixes landed:

- `f6c4f77543` closes DD-RH-087.
- `2217a58061` closes DD-RH-088.
- `e81d5ccbdc` closes DD-RH-089.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle feed freshness, RPC status, P2P relay, and miner/template code paths.

## Wave 15 - Oracle P2P Forgery, Replay, Flooding

Status: completed.

Assignments:

- Agent A - Exploit-path attacker: `ORACLEPRICE`, `ORACLEBUNDLE`, `ORACLECONSENSUS`, `ORACLEATTESTATION`, `GETORACLES`, MuSig2 nonce/partial forgery and replay paths.
- Agent B - Invariant/test breaker: malformed, duplicate, reordered P2P oracle messages and weak/false-green P2P tests.
- Agent C - Boundary adversary: multi-node P2P abuse, restart behavior, GETORACLES boundaries, and attestation replay poisoning.

Scope note: DigiDollar/oracle only; shared P2P/protocol code only where it directly gates oracle relay, replay caches, and MuSig2/oracle message handling.

Commands and results:

- Required live surface enumeration: completed, output count `2038` in `/tmp/red_hornet_wave15_surface.txt`.
- `make -C src test/test_digibyte -j$(nproc)`: passed for each changed unit-test rebuild.
- `./src/test/test_digibyte --run_test=oracle_consensus_threshold_tests/duplicate_oracle_ids_do_not_count_toward_consensus`: failed before DD-RH-090, passed after fix.
- `./src/test/test_digibyte --run_test=oracle_consensus_threshold_tests,oracle_phase2_tests,rh05_bundle_validation_attacks_tests,digidollar_p2p_tests,oracle_bundle_manager_tests`: passed after DD-RH-090, `78` test cases.
- `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/attestation_replay_hash_binds_signature`: failed before DD-RH-091, passed after fix.
- `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests,oracle_p2p_tests,oracle_phase2_tests`: passed after DD-RH-091, DD-RH-092, and DD-RH-093; final run was `87` test cases.
- `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/consensus_hash_is_domain_separated_from_oracle_price`: failed before DD-RH-092, passed after fix.
- `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/bundle_hash_ignores_unauthenticated_block_hash`: failed before DD-RH-093, passed after fix.
- `git diff --check`: passed after each Wave 15 fix.

Confirmed vulnerabilities:

- DD-RH-090 (Low): `COracleBundle::HasConsensus()` counted duplicate oracle IDs as threshold-satisfying.
  - Affected invariant: oracle thresholds must count unique configured oracles, not repeated copies of one signed message.
  - Exploit path:
    - `src/primitives/oracle.cpp:371` returned consensus based on total message count.
    - A peer could send an `ORACLEBUNDLE` with the same valid signed oracle message repeated enough times to satisfy the P2P threshold predicate.
    - `src/net_processing.cpp:5704` used `bundle.HasConsensus()` before relaying; storage and block validation later deduped/rejected, so impact is relay-boundary deception and P2P resource waste, not accepted inflation.
  - Failing regression before fix:
    - `src/test/oracle_consensus_threshold_tests.cpp:252` added `duplicate_oracle_ids_do_not_count_toward_consensus()`.
    - Before the fix, the duplicate bundle returned `HasConsensus(required) == true` and `GetConsensusPrice(required) == 50000`.
  - Fix summary:
    - `src/primitives/oracle.cpp:371` now rejects any duplicate oracle ID and counts unique oracle IDs only.
  - Commit: `f445b6fc0b` (`oracle p2p: fix DD-RH-090 duplicate signer consensus`).
  - Status: fixed and committed.

- DD-RH-091 (Medium): invalid `ORACLEATTESTATION` messages could poison the seen-attestation replay cache before signature validation.
  - Affected invariant: malformed or forged oracle P2P messages must not block later valid oracle attestations.
  - Exploit path:
    - `src/net_processing.cpp:5883` computed the attestation hash from the incoming wire message.
    - Before the fix, `src/net_processing.cpp:5887` registered that hash before oracle ID, chainparams pubkey binding, and `VerifyPhase2()` checks.
    - The old hash was only the Phase Two signature hash `(oracle_id, price, timestamp)`, so a bad signature for a real consensus tuple could make the later valid signature look like a replay.
  - Failing regression before fix:
    - `src/test/oracle_bundle_manager_tests.cpp:414` added `attestation_replay_hash_binds_signature()`.
    - Before the fix, invalid and valid attestations for the same tuple had the same hash and registering the invalid one caused the valid one to be rejected as seen.
  - Fix summary:
    - `src/protocol.h:683` now hashes `oracle-attestation-v1`, oracle ID, price, timestamp, and signature for P2P attestation dedup.
    - `src/oracle/bundle_manager.cpp:1935` adds `HasSeenAttestation()` so P2P can check without inserting.
    - `src/net_processing.cpp:5887` checks the seen-attestation cache without mutating it, and `src/net_processing.cpp:5954` registers only after validation and successful storage.
  - Commit: `24ebc85519` (`oracle p2p: fix DD-RH-091 attestation replay poisoning`).
  - Status: fixed and committed.

- DD-RH-092 (Low): `ORACLECONSENSUS` used the same untagged replay-cache hash shape as Phase Two `ORACLEPRICE`, and proposals were marked seen before local validation.
  - Affected invariant: one oracle P2P message type must not be able to poison another type's replay cache or block a later locally valid proposal.
  - Exploit path:
    - `src/protocol.h:657` hashed consensus proposals as `(epoch, price, timestamp)`.
    - `src/primitives/oracle.cpp:148` hashed Phase Two prices as `(oracle_id, price, timestamp)`.
    - When `epoch == oracle_id`, a rejected consensus proposal could collide with an oracle price hash in the shared `seen_message_hashes` cache.
    - Before the fix, `src/net_processing.cpp:5770` registered the proposal before `ValidateConsensusProposal()` at `src/net_processing.cpp:5812`.
  - Failing regression before fix:
    - `src/test/oracle_bundle_manager_tests.cpp:371` added `consensus_hash_is_domain_separated_from_oracle_price()`.
    - Before the fix, the proposal hash equaled the oracle price hash.
  - Fix summary:
    - `src/protocol.h:657` now domain-separates consensus proposal hashes with `oracle-consensus-v1`.
    - `src/net_processing.cpp:5856` now registers a consensus proposal in the shared seen cache only after epoch, price, and local-consensus validation pass.
  - Commit: `2878e11741` (`oracle p2p: fix DD-RH-092 consensus replay hash poisoning`).
  - Status: fixed and committed.

- DD-RH-093 (Low): `ORACLEBUNDLE` dedup included unauthenticated `block_hash`, allowing replay hash bypass.
  - Affected invariant: unauthenticated P2P wrapper fields must not control replay-cache identity for expensive oracle bundle validation.
  - Exploit path:
    - `src/protocol.cpp:277` included `bundle << block_hash` in `OracleBundleMsg::GetHash()`.
    - `src/net_processing.cpp:5617` deduped bundles by that hash, while the handler did not validate or authenticate `block_hash`.
    - A peer could replay the same signed bundle with changed `block_hash` values, forcing repeated signature verification and rate-limit consumption. Inner-message dedup prevented storage/relay when nothing new was present, so impact is bounded P2P resource waste.
  - Failing regression before fix:
    - `src/test/oracle_bundle_manager_tests.cpp:394` added `bundle_hash_ignores_unauthenticated_block_hash()`.
    - Before the fix, the same bundle with two different `block_hash` values had different replay hashes.
  - Fix summary:
    - `src/protocol.cpp:277` now domain-separates the P2P bundle hash with `oracle-bundle-v1` and hashes the signed bundle contents only.
  - Commit: `d82f35c677` (`oracle p2p: fix DD-RH-093 bundle replay hash bypass`).
  - Status: fixed and committed.

Rejected false positives:

- `ORACLEPRICE` arbitrary pubkey spoofing is rejected: the handler binds the pubkey from chainparams before signature verification and only registers the message after successful storage.
- `ORACLEBUNDLE` arbitrary-key forgery is rejected: the handler rebinds every message pubkey from chainparams, verifies signatures, checks consensus and epoch, and does not relay when no new inner message is stored.
- `GETORACLES` stale replay is rejected after Wave 14: stale/future pending messages are skipped in responses and the request path is rate-limited.
- MuSig2 nonce/partial forgery is rejected in this wave: P2P handlers bind chainparams keys and verify signatures before session ingestion.

Theoretical / not yet reachable:

- MuSig2 nonce and partial-signature duplicates are deduped after signature verification/rate counting, so valid replay floods can still spend crypto and per-peer quota before being dropped; no practical unbounded resource path was proven.
- P2P price ingestion accepts any chainparams-configured oracle ID under `ORACLE_TOTAL_COUNT`; active/epoch selection is enforced later in block validation, so this remains tied to the DD-RH-084 roster design item.
- `feature_oracle_p2p.py` and older P2P unit tests still contain false-green or shadow-struct coverage patterns; useful hardening backlog, but not counted as a live vulnerability without a production bypass.

Fixes landed:

- `f445b6fc0b` closes DD-RH-090.
- `24ebc85519` closes DD-RH-091.
- `2878e11741` closes DD-RH-092.
- `d82f35c677` closes DD-RH-093.

ARCHITECTURAL_REVIEW_REQUIRED: none newly added in Wave 15.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle P2P, protocol hash, replay-cache, and oracle bundle threshold code paths.

## Wave 16 - MuSig2 Session Poisoning and Epoch Confusion

Status: completed.

Assignments:

- Agent A - Exploit-path attacker: MuSig2 session lifecycle, nonce participant set, epoch/message binding, and partial-signature arrival ordering.
- Agent B - Invariant/test breaker: stale nonces, wrong signer sets, timeout/retry edges, direct helper validation gaps, and MuSig2 negative-test coverage.
- Agent C - Boundary adversary: miner/oracle timing, cross-epoch flow, operator stop/start state, and Phase 2/Phase 3 boundary behavior.

Scope note: DigiDollar/oracle only; shared P2P/protocol code only where it directly gates MuSig2/oracle message ingestion and bundle validation.

Commands and results:

- Required live surface enumeration: completed, output count `2038` in `/tmp/red_hornet_wave16_surface.txt`.
- `make -C src test/test_digibyte -j$(nproc)`: passed after the DD-RH-095 changes.
- `./src/test/test_digibyte --run_test=musig2_signing_orchestration_tests/early_partial_sigs_replay_when_session_enters_signing --catch_system_errors=no --log_level=test_suite`: failed before DD-RH-095 with `GetPartialSigCount() == 0` and no completed session; passed after fix.
- `./src/test/test_digibyte --run_test=musig2_signing_orchestration_tests --catch_system_errors=no --log_level=test_suite`: passed, `4` test cases.
- `./src/test/test_digibyte --run_test=rh58_pending_partialsigs_unbounded_growth_tests --catch_system_errors=no --log_level=test_suite`: passed, `4` test cases.
- `./src/test/test_digibyte --run_test=musig2_signing_orchestration_tests,rh58_pending_partialsigs_unbounded_growth_tests,musig2_p2p_ingestion_tests,musig2_oracle_node_tests,musig2_session_tests,musig2_p2p_message_tests,musig2_p2p_network_attacks_tests --catch_system_errors=no --log_level=test_suite`: passed, `66` test cases.
- `./src/test/test_digibyte --run_test=oracle_bundle_manager_tests,oracle_phase2_tests,oracle_p2p_tests,musig2_bundle_manager_tests,musig2_bundle_mining_tests,musig2_bundle_creation_tests,musig2_bundle_format_tests --catch_system_errors=no --log_level=error`: failed with pre-existing/adjacent v0x03 bundle extraction failures in `musig2_bundle_creation_tests` and one order-dependent `musig2_bundle_mining_tests` failure after prior bundle tests. These are carried into Wave 17 because they are serialization/bitmap/bundle-state issues, not caused by DD-RH-095.
- `./src/test/test_digibyte --run_test=musig2_bundle_creation_tests --catch_system_errors=no --log_level=error`: failed, `10` v0x03 extraction failures; recorded for Wave 17.
- `./src/test/test_digibyte --run_test=musig2_bundle_mining_tests --catch_system_errors=no --log_level=error`: passed alone, `2` test cases. The earlier mining failure is order-dependent state leakage.
- `git diff --check`: passed.

Confirmed vulnerabilities:

- DD-RH-095 (Medium): valid early MuSig2 partial signatures were buffered but never replayed after the local session entered `SIGNING`.
  - Affected invariant: MuSig2 partial signatures must bind to the intended epoch/message/participant set and must not be lost solely because peer timing is faster than the local nonce-aggregation state.
  - Exploit path:
    - A valid `ORACLEMUSIGPARTIALSIG` passes P2P authentication in `src/net_processing.cpp` and reaches `OracleSigningOrchestrator::IngestRemotePartialSig()`.
    - If the local session is still `CREATED`, `NONCES_COLLECTING`, or `NONCES_COMPLETE`, `src/oracle/signing_orchestrator.cpp:154` cannot add the partial because `MuSig2SigningSession::AddPartialSignatureVerified()` only accepts `SIGNING`.
    - The old code stored the message in `m_pending_partialsigs` but no production path read that map after the session transitioned to `SIGNING`.
    - A fast peer, delayed local nonce aggregation, or adversarial ordering could make otherwise valid threshold partials unavailable until fallback or timeout.
  - File references:
    - Buffer/add path: `src/oracle/signing_orchestrator.cpp:135`.
    - Verified partial application: `src/oracle/signing_orchestrator.cpp:173`.
    - Bounded pending queue: `src/oracle/signing_orchestrator.cpp:200`.
    - Replay drain: `src/oracle/signing_orchestrator.cpp:219`.
    - Drain hook before aggregation: `src/oracle/signing_orchestrator.cpp:618`.
    - Clear reset: `src/oracle/signing_orchestrator.cpp:63`.
    - Test reproducer: `src/test/musig2_signing_orchestration_tests.cpp:245`.
  - Failing regression before fix:
    - `src/test/musig2_signing_orchestration_tests.cpp:245` builds real threshold MuSig2 partial signatures, delivers them before the receiving session is `SIGNING`, advances the session to `SIGNING`, and calls the block tick.
    - Before the fix, `receiving_ptr->GetPartialSigCount()` stayed `0`, `GetCompletedSession()` returned false, and signed price/timestamp stayed `0`.
  - Expected secure behavior: valid early partial signatures are replayed once the receiving session has the aggregate nonce/message context needed to verify them.
  - Fix summary:
    - `src/oracle/signing_orchestrator.cpp:173` centralizes parsing and verification against the chainparams oracle key and the session's MuSig2 cache.
    - `src/oracle/signing_orchestrator.cpp:219` moves and erases pending messages for the epoch, re-verifies each, and drops invalid/nonmatching entries.
    - `src/oracle/signing_orchestrator.cpp:618` drains the queue whenever the session is `SIGNING`, before final aggregation.
    - `src/oracle/signing_orchestrator.cpp:63` now clears pending partials during orchestrator reset.
    - `src/test/rh58_pending_partialsigs_unbounded_growth_tests.cpp` was updated so the old buffer-DoS regression now asserts bounded, pruned, drained, and reset behavior.
  - Commit: `e59d89ccc8` (`oracle musig2: fix DD-RH-095 early partial replay`).
  - Status: fixed and committed.

Rejected false positives:

- Direct `ValidatePhaseThreeBundle()` helper lacks a timestamp freshness check by itself, but the live block-validation path calls post-validation freshness and future-time checks after version-specific validation. This is a helper/API footgun, not a reachable consensus acceptance path today.
- Legacy `StartMuSig2Session()` unsigned/shadow paths have no live P2P/block-template caller in the current Phase 3 path; not counted as a reachable vulnerability.
- Phase 2 v0x02 fallback while Phase 3 is active remains the existing DD-RH-085 architectural-review item, not a newly implemented Wave 16 fix.

Theoretical / not yet reachable:

- DD-RH-094 (High, `ARCHITECTURAL_REVIEW_REQUIRED`): deterministic `TrimNoncesToThreshold()` keeps the lowest oracle IDs, so a compromised low-ID oracle can contribute a nonce, get selected, and then withhold its partial signature. `AddPartialSignatureVerified()` rejects nonselected signers, and timeout currently fails the session instead of reselection/retry. This is a MuSig2 liveness/protocol-design issue requiring Jared approval for reselection, retry, signer-score, or timeout-policy changes.
- Proactive Phase 2 consensus proposals can use wall-clock-derived epoch before chain epoch cache is populated. P2P validation rejects wrong epochs and the miner path later broadcasts the correct epoch, so this is a liveness delay/theoretical boundary issue, not a proven acceptance bypass.
- `stoporacle` does not explicitly reset the signing orchestrator. DD-RH-095 now makes `Clear()` reset pending partials, but whether `stoporacle` should call `Clear()` is an operator-lifecycle decision to revisit with RPC behavior in a later wave.
- MuSig2 P2P handlers still validate oracle IDs against configured total oracles rather than the exact active consensus set; this remains tied to DD-RH-084 roster architecture.

Tests added or upgraded:

- Added `musig2_signing_orchestration_tests/early_partial_sigs_replay_when_session_enters_signing`.
- Updated `rh58_pending_partialsigs_unbounded_growth_tests` from the old write-only-buffer proof to the post-fix bounded/pruned/drained/reset invariants.

Fixes landed:

- `e59d89ccc8` closes DD-RH-095.

ARCHITECTURAL_REVIEW_REQUIRED:

- DD-RH-094: MuSig2 deterministic threshold participant selection can be liveness-poisoned by a withholding low-ID signer; protocol-level reselection/retry policy requires Jared approval before implementation.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle MuSig2 session orchestration, P2P partial ingestion, and oracle bundle-adjacent tests.

## Wave 17 - MuSig2 Serialization, Bitmaps, Partial Sigs, Replay

Status: completed.

Assignments:

- Agent A - Exploit-path attacker: bitmap/order/serialization and aggregate-verification bypass attempts, plus nonce/partial replay.
- Agent B - Invariant/test breaker: malformed partial signatures, aggregate bundles, v0x03 bitmap fixtures, and related negative coverage.
- Agent C - Boundary adversary: end-to-end v0x03 bundle acceptance/rejection matrix, mining-template state leakage, restart/replay boundaries.

Scope note: DigiDollar/oracle only; shared protocol/P2P/validation code considered only where it directly gates MuSig2/oracle bundles.

Commands and results:

- Required live surface enumeration: completed, output count `2038` in `/tmp/red_hornet_wave17_surface.txt`.
- `make -C src test/test_digibyte -j$(nproc)`: passed after the Wave 17 changes.
- `src/test/test_digibyte --run_test=musig2_bundle_creation_tests --catch_system_errors=no --log_level=error`: failed before v0x03 bitmap hardening with `10` stale bitmap/extraction failures; passed after fix, `18` cases.
- `src/test/test_digibyte --run_test=musig2_bundle_mining_tests --catch_system_errors=no --log_level=error`: passed after mining test isolation, `2` cases.
- `src/test/test_digibyte --run_test=musig2_bundle_creation_tests,musig2_bundle_mining_tests,oracle_bundle_manager_tests,oracle_phase2_tests,oracle_p2p_tests,musig2_bundle_manager_tests,musig2_bundle_format_tests --catch_system_errors=no --log_level=error`: passed after fix, `125` cases.
- `src/test/test_digibyte --run_test=musig2_signing_orchestration_tests/early_partial_buffer_dedups_by_oracle_before_capacity --catch_system_errors=no --log_level=test_suite`: failed before DD-RH-096 with `GetPartialSigCount() == 0`; passed after fix.
- `src/test/test_digibyte --run_test=musig2_signing_orchestration_tests,rh58_pending_partialsigs_unbounded_growth_tests,musig2_p2p_ingestion_tests,musig2_oracle_node_tests,musig2_session_tests,musig2_p2p_message_tests,musig2_p2p_network_attacks_tests --catch_system_errors=no --log_level=error`: passed after fix, `67` cases.
- `git diff --check`: passed.

Confirmed vulnerabilities:

- DD-RH-096 (Medium): one authenticated oracle could starve the early MuSig2 partial-signature replay buffer for an epoch.
  - Affected invariant: a single authenticated oracle must not be able to prevent otherwise valid threshold partial signatures from being replayed once the receiver reaches `SIGNING`.
  - Exploit path:
    - `ORACLEMUSIGPARTIALSIG` is authenticated at the P2P boundary, then reaches `OracleSigningOrchestrator::IngestRemotePartialSig()`.
    - If the local session is not yet `SIGNING`, `TryApplyRemotePartialSig()` cannot add the partial and `src/oracle/signing_orchestrator.cpp:200` buffers it.
    - The old buffer cap was per epoch message count only. One oracle could send `32` distinct signed garbage partial-sig envelopes for the current epoch, filling the buffer.
    - Later honest threshold partial signatures arriving before `SIGNING` were dropped because the buffer was already full. On drain, only the attacker garbage was replayed and rejected.
  - File references:
    - Vulnerable/fixed buffer path: `src/oracle/signing_orchestrator.cpp:200`.
    - Per-oracle dedup fix: `src/oracle/signing_orchestrator.cpp:207`.
    - Attack helper/test: `src/test/musig2_signing_orchestration_tests.cpp:157` and `src/test/musig2_signing_orchestration_tests.cpp:309`.
  - Failing regression before fix:
    - `musig2_signing_orchestration_tests/early_partial_buffer_dedups_by_oracle_before_capacity` injects a real receiving session, feeds `32` signed garbage early partials from one oracle, then feeds honest threshold partials before the session enters `SIGNING`.
    - Before the fix, the block tick drained zero valid partials: `GetPartialSigCount() == 0`.
  - Expected secure behavior: one oracle can occupy at most one pending early-partial slot per epoch, leaving room for honest threshold participants.
  - Fix summary:
    - `src/oracle/signing_orchestrator.cpp:207` now replaces an existing pending early partial from the same `oracle_id` instead of appending another entry.
    - The bounded epoch cap remains in place for memory safety.
  - Commit: `88f1568e5b` (`oracle musig2: fix DD-RH-096 early partial buffer starvation`).
  - Status: fixed and committed.

Hardening / test-gap fixes not counted as confirmed vulnerabilities:

- v0x03 bitmap script creation hardening:
  - `CreateOracleScript()` accepted any non-empty v0x03 participation bitmap even when the bitmap shape did not match the active chain's `nOracleTotalOracles`. Current production MuSig2 signing already emits the consensus-sized bitmap, so this was not proven externally exploitable as an attacker path.
  - Fix: `src/oracle/bundle_manager.cpp` now decodes the bitmap against consensus total oracles during script creation, rejects malformed/undersized participation, and makes `ValidateV03BundleFormat()` require the exact serialized size including epoch.
  - Tests: `src/test/musig2_bundle_creation_tests.cpp` now uses regtest's 7-oracle bitmap shape and explicitly rejects stale oversized bitmap fixtures; `src/test/musig2_bundle_mining_tests.cpp` now builds a threshold-complete session and restores mock-oracle state after each test.
  - Commit: `7b5140f994` (`oracle musig2: harden v03 bitmap script creation`).

Rejected false positives:

- Raw `MuSig2SigningSession::AddPartialSignature()` can still store parseable invalid scalar values, but live P2P ingestion and the signing orchestrator route remote partials through `AddPartialSignatureVerified()`. This remains an internal API hazard/test-gap, not a current network exploit.
- Oversized/malformed v0x03 bitmaps are rejected by extraction/validation after this wave's hardening and existing unused-bit checks.
- The order-dependent `musig2_bundle_mining_tests` failure was test-state leakage from `MockOracleManager` and an underbuilt one-signer fixture, not a production mining bypass.
- Malformed OP_ORACLE extraction that falls through transition leniency remains the existing DD-RH-086 architectural policy issue; no distinct v0x03 consensus bypass was proven in this wave.

Theoretical / not yet reachable:

- DD-RH-097 (High, `ARCHITECTURAL_REVIEW_REQUIRED`): MuSig2 nonce/partial/v0x03 aggregate signing domains do not bind the network/genesis/deployment domain, while mainnet and testnet currently share oracle pubkeys. A cross-network replay/liveness attack may be possible if epochs and timestamps line up. Fixing this safely requires a MuSig2/P2P protocol-domain decision and Jared approval.
- Mainnet v0x03 creation can be downgraded by the existing active-set mismatch where `nOracleTotalOracles` is smaller than the active oracle IDs. This is the same design family as DD-RH-084 and remains an architectural-review item unless Jared approves filtering/reconfiguring the mainnet active set.
- `getoraclestatus` reports synthesized v0x03 participant rows with `signature_valid=false` because those per-oracle message shells do not carry individual Phase 2 signatures. This is a low-severity status/telemetry misreporting risk, not a consensus bypass.
- `LoadPricesFromChain()` restart loading needs a dedicated Wave 18 review because it scans historical coinbase oracle data after the tip is active; not counted in Wave 17 until a deterministic production test/fix is completed.

Fixes landed:

- `7b5140f994` hardens v0x03 bitmap script creation and test fixtures.
- `88f1568e5b` closes DD-RH-096.

ARCHITECTURAL_REVIEW_REQUIRED:

- DD-RH-097: cross-network MuSig2/P2P domain separation for nonce, partial, and aggregate signatures.
- DD-RH-084 reiterated: mainnet active oracle roster exceeds consensus MuSig2 bitmap total.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle MuSig2 serialization, bitmap validation, P2P partial ingestion, and oracle bundle tests.

## Wave 18 - DoS and Resource Exhaustion

Status: completed.

Assignments:

- Agent A - Exploit-path attacker: unbounded DD/oracle maps/vectors, expensive validation, logging hot paths, malformed message resource costs.
- Agent B - Invariant/test breaker: malformed resource-heavy inputs, invalid-block side effects, restart/cache negative cases.
- Agent C - Boundary adversary: RPC/P2P/Qt/wallet responsiveness, fallback scan behavior, slow external/user boundary paths.

Scope note: DigiDollar/oracle only; shared validation, miner, RPC, P2P, and Qt code considered only where it directly gates DigiDollar/oracle behavior.

Commands and results:

- Required live surface enumeration: completed, output count `2038` in `/tmp/red_hornet_wave18_surface.txt`.
- `make -C src test/test_digibyte -j$(nproc)`: passed after Wave 18 edits.
- `src/test/test_digibyte --run_test=rh66_startup_oracle_price_loading_tests/load_prices_from_chain_skips_recent_pre_activation_oracle_outputs --catch_system_errors=no --log_level=error`: failed before DD-RH-098 because startup reload cached the pre-activation price; passed after fix.
- `src/test/test_digibyte --run_test=rh61_coinbase_price_cache_poisoning_tests,rh66_startup_oracle_price_loading_tests,oracle_price_staleness_tests,oracle_bundle_manager_tests --catch_system_errors=no --log_level=error`: passed after DD-RH-098, `50` test cases.
- `src/test/test_digibyte --run_test=rh67_invalid_block_oracle_cache_side_effect_tests/rejected_block_does_not_update_oracle_cache --catch_system_errors=no --log_level=error`: failed before DD-RH-099 with `GetOraclePriceForHeight(rejected_height) == 654321` and `GetLatestPrice() == 654321`; passed after fix.
- `src/test/test_digibyte --run_test=miner_dd_validation_tests/block_succeeds_without_dd_after_failure --catch_system_errors=no --log_level=error`: failed before DD-RH-100 with an unexpected `BuildTemplate()` exception; passed after fix.
- `src/test/test_digibyte --run_test=miner_dd_validation_tests/test_block_validity_retry --catch_system_errors=no --log_level=error`: failed before DD-RH-100 with an unexpected `BuildTemplate()` exception; passed after fix.
- `src/test/test_digibyte --run_test=rh61_coinbase_price_cache_poisoning_tests,rh66_startup_oracle_price_loading_tests,rh67_invalid_block_oracle_cache_side_effect_tests,oracle_price_staleness_tests,oracle_bundle_manager_tests,miner_dd_validation_tests --catch_system_errors=no --log_level=error`: passed after DD-RH-099 and DD-RH-100, `57` test cases.
- `git diff --check`: passed.

Confirmed vulnerabilities:

- DD-RH-098 (Medium): startup oracle price reload accepted recent pre-activation oracle outputs after the node tip was active.
  - Affected invariant: oracle prices loaded on restart must obey the same activation/freshness/validation gates as live block connection.
  - Exploit path:
    - A block just before activation could contain an extractable compact oracle output in coinbase.
    - After the node later reached an active tip and restarted, `OracleBundleManager::LoadPricesFromChain()` scanned recent blocks and cached extractable oracle data without checking that scanned block's activation state or validating the bundle.
    - The hot oracle cache could therefore restart on an attacker-controlled pre-activation price.
  - File references:
    - Fixed startup scan gates: `src/oracle/bundle_manager.cpp:2100`, `src/oracle/bundle_manager.cpp:2106`, `src/oracle/bundle_manager.cpp:2126`.
    - Regression: `src/test/rh61_coinbase_price_cache_poisoning_tests.cpp:481`.
  - Failing regression before fix:
    - `rh66_startup_oracle_price_loading_tests/load_prices_from_chain_skips_recent_pre_activation_oracle_outputs` mines a pre-activation oracle price within the startup scan window, clears the manager, calls `LoadPricesFromChain()`, and expects no cached price.
    - Before the fix, the cache loaded `42424242`.
  - Fix summary:
    - Startup loading now scans oldest-to-newest, skips heights before `nDDActivationHeight`, skips per-block inactive BIP9 states, and runs `OracleDataValidator::ValidateBlockOracleData()` before `UpdatePriceCache()`.
  - Commit: `b17e5ef922` (`oracle startup: fix DD-RH-098 pre-activation price reload`).
  - Status: fixed and committed.

- DD-RH-099 (Medium): invalid blocks could mutate global oracle price cache before later block checks rejected them.
  - Affected invariant: rejected blocks must not leave DigiDollar/oracle cache side effects behind.
  - Exploit path:
    - `ConnectBlock()` extracted a coinbase oracle bundle and updated `OracleBundleManager`, `MockOracleManager`, and pending-message state before later transaction/coinbase amount checks completed.
    - A peer or miner could provide a valid-PoW block carrying an oracle output plus an invalid coinbase amount. The block failed, but the local node's oracle cache kept the attacker's price.
  - File references:
    - Local extraction retained for deterministic DD validation: `src/validation.cpp:2841`.
    - Global cache update deferred until successful block validation: `src/validation.cpp:3100`.
    - Regression: `src/test/rh61_coinbase_price_cache_poisoning_tests.cpp:548`.
  - Failing regression before fix:
    - `rh67_invalid_block_oracle_cache_side_effect_tests/rejected_block_does_not_update_oracle_cache` builds a block with a compact oracle price and an overpaid coinbase, submits it, verifies the chain tip did not advance, and asserts the oracle cache is unchanged.
    - Before the fix, `GetOraclePriceForHeight(rejected_height)` and `GetLatestPrice()` both returned the attacker's `654321` price.
  - Fix summary:
    - `ConnectBlock()` now keeps the extracted block price as a local validation input only.
    - Global oracle cache mutation, regtest mock-price mutation, logging, and pending-message clearing are deferred until after all block validity checks pass.
  - Commit: `c1c1896fb8` (`oracle validation: fix DD-RH-099 invalid-block cache side effects`).
  - Status: fixed and committed.

- DD-RH-100 (Medium): miner block-template validation could throw instead of retrying without DD transactions when deterministic block oracle price was missing.
  - Affected invariant: malformed or temporarily unmineable DD transactions must not make block template creation fail for otherwise valid non-DD transactions.
  - Exploit path:
    - Mempool/package selection can validate a DD transaction using the advisory current oracle price.
    - The assembled candidate block may still lack a deterministic coinbase oracle price if bundle creation is unavailable or disabled.
    - `TestBlockValidity()` then rejects the DD mint with `bad-oracle-price`, but the retry path only recognized `insufficient-collateral`, so `CreateNewBlock()` threw instead of removing DD transactions and preserving the non-DD template.
  - File references:
    - Retry classifier: `src/node/miner.cpp:123`.
    - Retry decision in block-template validation: `src/node/miner.cpp:397`.
    - Existing regression cases: `src/test/miner_dd_validation_tests.cpp:220` and `src/test/miner_dd_validation_tests.cpp:307`.
  - Failing regression before fix:
    - `miner_dd_validation_tests/block_succeeds_without_dd_after_failure` and `miner_dd_validation_tests/test_block_validity_retry` both hit unexpected `BuildTemplate()` exceptions when the candidate block failed DD validation during `TestBlockValidity()`.
  - Fix summary:
    - The miner retry classifier now treats `bad-oracle-price` and `invalid-oracle-price` as retryable DigiDollar block-template failures, matching the existing insufficient-collateral behavior.
    - The template is rebuilt without DD transactions, so valid non-DD transactions remain mineable.
  - Commit: `6af2d0ed4d` (`miner digidollar: fix DD-RH-100 oracle-price retry DoS`).
  - Status: fixed and committed.

Rejected false positives:

- `LoadPricesFromChain()` is not still vulnerable after DD-RH-098: live code skips pre-activation heights, checks BIP9 for each scanned block, validates the oracle bundle, and the targeted regression passes.
- P2P pending-bundle growth is bounded by oracle ID and stale/hash cleanup, and `GETORACLES` has stale filtering and rate limiting. No unbounded unauthenticated growth path was proven.
- Wallet rescan/restore DD accounting looked covered by spendable ownership checks and post-rescan position validation in the inspected paths; no reachable corruption path was proven in this wave.
- Invalid DD OP_RETURN through mempool/RPC remains covered by existing structural checks; no production bypass or unbounded parser path was proven.

Theoretical / not yet reachable:

- Authenticated oracle P2P messages deserialize fixed-size cryptographic fields through variable-size vectors before later validation. The global P2P message cap bounds memory to normal network-message limits, so this is a fuzz/test-gap item rather than a confirmed DoS.
- Invalid `ORACLEBUNDLE` messages can still consume bounded seen-cache/rate-limit work before being rejected. No unbounded or unauthenticated resource path was proven after earlier replay-cache fixes.
- `getdigidollarstats` fallback with `-digidollarstatsindex=0` can perform full UTXO scans under `cs_main` and update global metrics through scanner code. This needs a dedicated concurrent RPC/TSAN proof before counting as a live security bug.
- Qt DigiDollar overview/redeem refresh timers synchronously call potentially heavy wallet/RPC paths. This is a loss-of-responsiveness risk and should get Qt tests with fake slow RPCs, but no fund-loss or validation bypass was proven.
- Oracle monitoring RPCs (`getoracles`, `getalloracleprices`) perform bounded historical disk scans under `cs_main`; authenticated callers can cause bounded contention but not an unbounded DoS in the current code.

Tests added or upgraded:

- Added `rh66_startup_oracle_price_loading_tests/load_prices_from_chain_skips_recent_pre_activation_oracle_outputs`.
- Added `rh67_invalid_block_oracle_cache_side_effect_tests/rejected_block_does_not_update_oracle_cache`.
- Reused and restored `miner_dd_validation_tests/block_succeeds_without_dd_after_failure` and `miner_dd_validation_tests/test_block_validity_retry` as the DD-RH-100 regression proof.

Fixes landed:

- `b17e5ef922` closes DD-RH-098.
- `c1c1896fb8` closes DD-RH-099.
- `6af2d0ed4d` closes DD-RH-100.

ARCHITECTURAL_REVIEW_REQUIRED: none newly added in Wave 18.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle startup price loading, block validation side effects, miner DD template retry behavior, and bounded RPC/P2P/Qt resource review.

## Wave 19 - Fuzz-Driven Adversarial Expansion

Status: completed.

Assignments:

- Agent A - Exploit-path attacker: identify parser/validation surfaces missing fuzz coverage and look for concrete parser-to-production exploit paths.
- Agent B - Invariant/test breaker: attack missing negative cases in oracle script parsing, block validation, and DigiDollar health/state invariants.
- Agent C - Boundary adversary: focused fuzz smoke runs, corpus/regression capture, and build-mode verification for DigiDollar/oracle fuzz targets.

Scope note: DigiDollar/oracle only; shared validation and P2P code considered only where it directly gates DD/oracle parsing, block validation, or fuzz harnesses.

Commands and results:

- Required live surface enumeration: completed, output count `2038` in `/tmp/red_hornet_wave19_surface.txt`. Subagents later observed `2050` because the same command also matches newly generated build artifacts under `.deps`/`.o`.
- `make -C src test/fuzz/fuzz -j$(nproc)`: passed after fuzz harness registration/init changes.
- `FUZZ=oracle_script_create_roundtrip src/test/fuzz/fuzz /tmp/red_hornet_wave19_fuzz/oracle_script_seed_1.bin`: initially crashed before harness init because the fuzz harness called secp256k1 key creation with an uninitialized ECC context; passed after adding `ECC_Start()` and `SelectParams(REGTEST)` to key-using oracle fuzz targets. This was harness-only and not counted as a production vulnerability.
- `FUZZ=oracle_script_extract src/test/fuzz/fuzz /tmp/red_hornet_wave19_fuzz/corpus`: passed after the harness was changed to exercise live `OP_RETURN OP_ORACLE` scripts instead of stale `DGB`-tag OP_RETURN data.
- `FUZZ=oracle_script_create_roundtrip src/test/fuzz/fuzz /tmp/red_hornet_wave19_fuzz/corpus`: passed.
- `FUZZ=oracle_bundle_validation src/test/fuzz/fuzz /tmp/red_hornet_wave19_fuzz/corpus`: passed.
- `FUZZ=oracle_bundle_v03_roundtrip src/test/fuzz/fuzz /tmp/red_hornet_wave19_fuzz/corpus`: passed.
- `FUZZ=oracle_price_message src/test/fuzz/fuzz /tmp/red_hornet_wave19_fuzz/corpus`: passed.
- `FUZZ=oracle_price_message_sign_verify src/test/fuzz/fuzz /tmp/red_hornet_wave19_fuzz/corpus`: passed.
- All compiled DD/oracle fuzz targets over `/tmp/red_hornet_wave19_fuzz/corpus`: passed in standalone fuzz-driver mode.
- `make -C src test/test_digibyte -j$(nproc)`: passed after DD-RH-101 fix.
- `src/test/test_digibyte --run_test=rh68_test_block_validity_health_metrics_side_effect_tests/test_block_validity_does_not_update_health_metrics --catch_system_errors=no --log_level=error`: failed before DD-RH-101 with `after.totalDDSupply == 10000 != 0` and `after.totalCollateral == 5000000000 != 0`; passed after fix.
- `src/test/test_digibyte --run_test=rh61_coinbase_price_cache_poisoning_tests,rh66_startup_oracle_price_loading_tests,rh67_invalid_block_oracle_cache_side_effect_tests,rh68_test_block_validity_health_metrics_side_effect_tests,miner_dd_validation_tests,digidollar_rh34_multiblock_state_tests,digidollar_err_attack_tests,digidollar_err_tests,oracle_price_staleness_tests,oracle_bundle_manager_tests --catch_system_errors=no --log_level=error`: passed after fix, `149` test cases.
- `git diff --check`: passed before committing DD-RH-101.

Confirmed vulnerabilities:

- DD-RH-101 (Medium): `TestBlockValidity()` could mutate global DigiDollar health metrics for a candidate block that was never connected.
  - Affected invariant: candidate block validation, rejected blocks, miner template checks, and other `fJustCheck` paths must not leave global DD supply/collateral state behind.
  - Exploit path:
    - Miner block-template validation calls `TestBlockValidity()`, which calls `ConnectBlock(..., fJustCheck=true)`.
    - `ConnectBlock()` validated DD mint/redeem transactions and immediately called `SystemHealthMonitor::OnMintConnected()` / `OnRedeemConnected()`.
    - The function later hit the `fJustCheck` return, so the block was not connected and the active chain did not advance, but the static health cache still reflected the candidate mint.
    - A local miner/operator workflow with DD transactions in candidate templates could therefore skew `GetSystemCollateralRatio()`/health-dependent DD decisions until a rescan/reset or compensating disconnect.
  - File references:
    - Rollback guard: `src/validation.cpp:2822`.
    - DD mint/redeem metric updates routed through the guard: `src/validation.cpp:3023` and `src/validation.cpp:3039`.
    - Commit only after successful block connection: `src/validation.cpp:3167`.
    - Regression test: `src/test/rh61_coinbase_price_cache_poisoning_tests.cpp:573`.
  - Failing regression before fix:
    - `rh68_test_block_validity_health_metrics_side_effect_tests/test_block_validity_does_not_update_health_metrics` builds an activated DD mint, attaches a valid signed Phase Two oracle bundle, runs `TestBlockValidity()`, confirms the active chain height is unchanged, and asserts cached health metrics are unchanged.
    - Before the fix, `totalDDSupply` became `10000` and `totalCollateral` became `5000000000` even though the candidate block was never connected.
  - Expected secure behavior: `TestBlockValidity()` and any later block-validation failure can use temporary DD health deltas for same-block validation, but must roll them back unless the block actually connects.
  - Fix summary:
    - `ConnectBlock()` now records DD health metric updates in a rollback guard.
    - The updates still apply during validation, preserving same-block behavior.
    - The guard reverses mint/redeem deltas on `fJustCheck` or any later failure and commits them only at successful block connect.
  - Commit: `32570f2bf179` (`validation digidollar: fix DD-RH-101 health metric side effects`).
  - Status: fixed and committed.

Hardening / test-gap fixes not counted as confirmed vulnerabilities:

- Oracle fuzz target enablement and parser coverage:
  - `src/Makefile.test.include` now builds the previously unregistered oracle fuzz sources `oracle_bundle_validation.cpp`, `oracle_price_message.cpp`, and `oracle_script_parsing.cpp`.
  - `src/test/fuzz/oracle_price_message.cpp` and `src/test/fuzz/oracle_script_parsing.cpp` now initialize ECC/regtest context before creating oracle keys.
  - `oracle_script_extract` now exercises live `OP_RETURN OP_ORACLE` scripts and malformed raw payload bytes instead of a stale `DGB` tag.
  - Commit: `9a1628c55e` (`oracle fuzz: enable parser and price harnesses`).

Rejected false positives:

- The initial invalid-block DD health poisoning attempt using an overpaid coinbase did not reproduce a metric side effect in the targeted test. The confirmed, reachable path is the miner/candidate `TestBlockValidity()` `fJustCheck` path captured by DD-RH-101.
- The `oracle_script_create_roundtrip` segfault was a fuzz harness initialization bug, not production reachability: the harness used key creation before secp256k1/ECC context startup.
- Direct oracle P2P fuzz gaps remain coverage gaps. The production P2P message cap, auth checks, deduplication, stale filtering, and rate limits blocked conversion into a proven unbounded resource vulnerability in this wave.
- Raw/noncanonical v01/v02 oracle payload trailing-byte acceptance is a reachable parser behavior but not a proven security vulnerability by itself because block-level validation and phase checks still gate live consensus acceptance.

Theoretical / not yet reachable:

- Oracle P2P command fuzzing is still indirect through generic `process_message`; add command-specific corpora for `ORACLEPRICE`, `ORACLEBUNDLE`, MuSig2 nonce/partial, consensus attestations, and `GETORACLES`.
- Full-block oracle coinbase fuzzing should call `OracleDataValidator::ValidateBlockOracleData()` and `CheckPhase3OracleBundleVersion()` with BIP34 heights and v01/v02/v03 payloads, not only `ExtractOracleBundle()`.
- DigiDollar validation fuzz should gain a mini `CCoinsViewCache`/prev-tx corpus so mint, transfer, and redemption fuzz cases exercise production-like `coins`, `txLookup`, oracle price, and burn/conservation paths instead of mostly `skipOracle=true` null-context paths.
- RPC/wallet DD amount/address parsers and exchange JSON/numeric parsers still need direct fuzz coverage or injectable pure-parser harnesses.

ARCHITECTURAL_REVIEW_REQUIRED:

- DD-RH-086 reiterated: post-activation malformed `OP_RETURN OP_ORACLE` marker payloads still fail open as transition behavior when extraction fails. Tightening this into a consensus rejection is an oracle-protocol/consensus policy change requiring Jared approval before implementation.
- DD-RH-085 reiterated: Phase 3-active heights still tolerate v0x02 oracle bundles in current live policy; changing that is a consensus/protocol design decision.

Fixes landed:

- `9a1628c55e` enables and hardens oracle fuzz parser/price harnesses.
- `32570f2bf179` closes DD-RH-101.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle fuzz harnesses, oracle parser coverage, DD health metrics, and validation side effects.

## Wave 20 - Final Exploit-Chain Sweep and Severity Summary

Status: in progress.

Assignments:

- Agent A - Exploit-path attacker: chain confirmed/nearby bugs into possible exploit paths and re-check recent security fixes for bypasses.
- Agent B - Invariant/test breaker: rerun and review the full DD/oracle security test matrix, including tests not registered in `test_runner.py`.
- Agent C - Boundary adversary: final report review, boundary caveats, open bugs, and architecture-review items.

Scope note: DigiDollar/oracle only; shared RPC mining and regtest block-generation paths were touched only because DigiDollar oracle commitments directly broke those paths.

Commands and interim results:

- Required live surface enumeration: completed, output count `2050` in `/tmp/red_hornet_wave20_surface.txt`.
- `make check -j$(nproc)`: initially failed in stale oracle unit-test expectations (`rh05_bundle_validation_attacks_tests`, `oracle_bundle_timing_tests`); passed after test-maintenance commit `8d78851fa9`.
- `test/functional/test_runner.py --jobs=$(nproc)`: initial full run stopped after early reproducible failures:
  - `mining_getblocktemplate_longpoll.py`, `feature_segwit.py`, and `feature_bip68_sequence.py` failed because `getblocktemplate` returned `coinbasetxn` and `default_oracle_commitment` fields missing from the RPC result schema.
  - `p2p_blockfilters.py` failed while mining post-activation regtest blocks with `bad-oracle-phase2`.
- `make -j$(nproc) src/digibyted src/test/test_digibyte`: passed after DD-RH-102/DD-RH-103 source fixes.
- `test/functional/test_runner.py --jobs=4 mining_getblocktemplate_longpoll.py feature_segwit.py feature_bip68_sequence.py p2p_blockfilters.py`: passed after DD-RH-102/DD-RH-103; `6/6` functional variants passed, runtime `67s`.
- Full `make check`, full registered functional tests, focused DD/oracle unregistered functional tests, descriptor-wallet variants, oracle boundary scripts, and fuzz smoke reruns completed after the final Wave 20 commits.

Confirmed vulnerabilities / reachable bugs:

- DD-RH-102 (Low): DigiDollar oracle `getblocktemplate` fields were returned without matching RPC result-schema entries.
  - Affected invariant: RPC/miner surfaces must not misdescribe DigiDollar oracle commitments or fail result-schema checks when oracle data is present.
  - Reachability path:
    - Regtest activates DigiDollar and `CreateNewBlock()` adds an oracle coinbase output.
    - `getblocktemplate` returns `coinbasetxn` and `default_oracle_commitment`.
    - RPC result checking rejects the response as an internal bug because those keys were not declared in `RPCHelpMan`.
  - File reference: `src/rpc/mining.cpp:726`.
  - Reproducer before fix: focused functional run failed `mining_getblocktemplate_longpoll.py`, `feature_segwit.py`, and `feature_bip68_sequence.py` with an internal `getblocktemplate` type-check error.
  - Expected secure behavior: DigiDollar-aware template fields must be documented in the RPC result schema and accepted by result checking.
  - Fix summary: declared optional `coinbasetxn`, `odokey`, and `default_oracle_commitment` fields in the `getblocktemplate` result schema.
  - Commit: `c49438caba` (`rpc mining: fix DD-RH-102 oracle GBT schema`).
  - Status: fixed and committed.

- DD-RH-103 (Low): regtest mock oracle Phase Two bundles could self-invalidate when the wall clock ticked during bundle construction.
  - Affected invariant: block-generation test oracle bundles must serialize exactly the price/timestamp values signed by each oracle message.
  - Reachability path:
    - `MockOracleManager::CreateMockBundle()` signed each message using `GetTime()` as the message timestamp.
    - It later wrote `bundle.timestamp = GetTime()`.
    - If the clock ticked between those calls, `CreateOracleScript()` serialized the later bundle timestamp.
    - Block validation reconstructed each Phase Two message using the serialized bundle timestamp, so all signatures failed and mining returned `bad-oracle-phase2`.
  - File reference: `src/oracle/mock_oracle.cpp:140`.
  - Reproducer before fix: full functional run failed `p2p_blockfilters.py` at block generation with `CreateNewBlock: TestBlockValidity failed: bad-oracle-phase2`.
  - Expected secure behavior: the mock bundle must use one timestamp for both signatures and serialized consensus data.
  - Fix summary: capture one `bundle_timestamp` and use it for every mock oracle message and the bundle header.
  - Commit: `03c28004da` (`oracle mock: fix DD-RH-103 bundle timestamp signing`).
  - Status: fixed and committed.

- DD-RH-104 (Low): oracle timestamp validation could reject valid edge-height regtest blocks at the 32-bit block-time boundary.
  - Affected invariant: oracle timestamp checks must compare signed oracle timestamps to block time using arithmetic that cannot wrap at the `uint32_t` block timestamp boundary.
  - Reachability path:
    - The existing `rpc_blockchain.py` y2106 test sets mock time to `2^32 - 1`.
    - Regtest DigiDollar mining adds a mock oracle bundle to the candidate block.
    - `ValidateBlockOracleData()` checked `bundle.timestamp > block.nTime + 60`; at `block.nTime == 4294967295`, the tolerance addition wrapped in 32-bit block-time arithmetic.
    - The bundle timestamp equal to the block timestamp was therefore rejected as future-dated with `bad-oracle-timestamp`.
  - File references:
    - Block-time-bound mock bundle construction: `src/oracle/bundle_manager.cpp:872`, `src/oracle/mock_oracle.cpp:137`, `src/oracle/mock_oracle.h:86`.
    - Timestamp comparison fix: `src/oracle/bundle_manager.cpp:2613`.
  - Reproducer before fix: `rpc_blockchain.py` and `rpc_blockchain.py --v2transport` failed in `_test_y2106()` with `CreateNewBlock: TestBlockValidity failed: bad-oracle-timestamp`.
  - Expected secure behavior: a bundle timestamp equal to `block.nTime` is valid, including at `UINT32_MAX`.
  - Fix summary:
    - Regtest mock bundles now sign and serialize the actual block time supplied by `AddOracleBundleToBlock()`.
    - The future-timestamp tolerance now promotes `block.nTime` to `int64_t` before adding `60`.
  - Commit: `630ae01c5c` (`oracle regtest: fix DD-RH-104 mock bundle timestamps`).
  - Status: fixed and committed.

Test-maintenance fixes:

- `8d78851fa9` (`oracle tests: refresh bundle timing and RH05 expectations`) updated stale test expectations that no longer matched secure current behavior:
  - `rh05_bundle_validation_attacks_tests` now expects the extracted epoch to remain bound to the expected epoch.
  - `oracle_bundle_timing_tests` now disables the regtest mock oracle fallback when testing near-quorum/no-quorum timing behavior.
- `d21b43c96a` (`functional tests: refresh DigiDollar oracle mining expectations`) updated registered functional tests whose old assumptions were invalid once always-active regtest DigiDollar adds oracle commitments:
  - `mining_basic.py` now accepts `coinbasetxn` when `default_oracle_commitment` is present.
  - `feature_utxo_set_hash.py` and `rpc_dumptxoutset.py` use current deterministic hashes with DigiDollar oracle coinbase commitments.
  - `rpc_getblockfrompeer.py` asserts pruning invariants instead of exact old prune heights that changed with larger oracle-bearing blocks.

Rejected false positives / downgraded items:

- Agent C confirmed the old DD-RH-098 startup cache poisoning path is retired by current activation/validation checks.
- Agent C confirmed production mock oracle injection remains regtest-gated; the Wave 20 DD-RH-103 bug is a regtest/test-harness block-generation reliability bug, not a mainnet oracle-injection path.
- The focused `getblocktemplate` failure is a schema/user-boundary reliability issue. No unauthorized DD creation or forged oracle acceptance was proven from it.

Theoretical / not yet reachable:

- Agent B confirmed many DD/oracle functional scripts are present but not registered in `test/functional/test_runner.py`; final verification must run them directly.
- Agent C reiterated remaining P2P command fuzzing, full-block oracle coinbase fuzzing, DD validation coins-view fuzzing, and Qt responsiveness gaps.

ARCHITECTURAL_REVIEW_REQUIRED:

- Existing campaign items remain open for Jared decision: DD-RH-069, DD-RH-075, DD-RH-084, DD-RH-085, DD-RH-086, DD-RH-094, DD-RH-097, plus the process-local volatility-freeze and `skipOracleValidation` design concerns noted by Agent C.

Final Wave 20 verification:

- `make check -j$(nproc)`: passed. This covered unit tests, wallet tests, Qt tests, minisketch, secp256k1, utility tests, and the bench sanity check.
- `test/functional/test_runner.py --jobs=8`: first full registered run reached 317/318 passes and failed `feature_coinstatsindex.py` because repeated RPC binding left the node listening only on `::1`; standalone `test/functional/feature_coinstatsindex.py` immediately passed. The full registered suite was rerun and passed `318/318`, with expected environment skips, accumulated duration `2249s`, runtime `335s`.
- Direct unregistered DigiDollar/oracle script loop passed for `64` scripts matching `digidollar_*.py`, `wallet_digidollar_*.py`, `feature_oracle_p2p.py`, and `rpc_getoracles_pending.py`.
- Descriptor-wallet variant loop passed for `58` scripts with `add_wallet_options(parser)`, each with a unique `--portseed`, plus explicit `wallet_digidollar_descriptors.py --descriptors`.
- Explicit oracle boundary reruns passed:
  - `python3 test/functional/feature_oracle_p2p.py --portseed=4101`
  - `python3 test/functional/rpc_getoracles_pending.py --portseed=4102`
- Legacy-wallet exploratory variant:
  - `python3 test/functional/digidollar_basic.py --legacy-wallet` fails with `Error: No bech32m addresses available. (-12)`.
  - This is recorded as a compatibility/architecture test-matrix item rather than a newly fixed vulnerability because DigiDollar receive addresses require P2TR/bech32m, and legacy wallet RPC code explicitly rejects bech32m address generation. Jared needs to decide whether legacy wallets are unsupported for DD receive/mint, or whether DD needs a legacy-wallet key/address design.
- Fuzz smoke passed for `40` targets with three deterministic stdin samples each: empty input, ASCII `RedHornetDigiDollarOracleMuSig2:<target>`, and 256 zero bytes. Targets covered all compiled DD fuzz harnesses, oracle bundle/script/message harnesses, MuSig2 nonce/partial/session harnesses, plus `rpc`, `script_digibyte_consensus`, `eval_script`, `script_interpreter`, and `parse_script`.
- `git diff --check`: passed.

Final Wave 20 subagent synthesis:

- Agent A found no new bypass of DD-RH-098 through DD-RH-104 after the fixes. It reiterated architecture items DD-RH-084/DD-RH-085 (Phase Three downgrade/roster mismatch), DD-RH-086 (malformed unknown OP_ORACLE fail-open transition policy), DD-RH-094 (low-ID MuSig2 nonce trimming liveness risk), and DD-RH-097 (cross-network MuSig2 domain separation).
- Agent B confirmed the need to run unregistered DD/oracle functional scripts directly; those scripts are not all in `test_runner.py`. Direct and descriptor runs completed as above.
- Agent C confirmed current fixed-issue status and warned that the final package must keep open issues DD-RH-055/DD-RH-057/DD-RH-058/DD-RH-059 and architecture-review items out of the "fixed" count.

Final campaign status:

- Fixed and committed confirmed vulnerabilities in this second campaign: DD-RH-052, DD-RH-053, DD-RH-054, DD-RH-056, DD-RH-060, DD-RH-061, DD-RH-062, DD-RH-065, DD-RH-066, DD-RH-067, DD-RH-068, DD-RH-070, DD-RH-071, DD-RH-072, DD-RH-073, DD-RH-074, DD-RH-076, DD-RH-077, DD-RH-078, DD-RH-079, DD-RH-080, DD-RH-081, DD-RH-082, DD-RH-083, DD-RH-087, DD-RH-088, DD-RH-089, DD-RH-090, DD-RH-091, DD-RH-092, DD-RH-093, DD-RH-095, DD-RH-096, DD-RH-098, DD-RH-099, DD-RH-100, DD-RH-101, DD-RH-102, DD-RH-103, DD-RH-104.
- Open confirmed implementation bugs: DD-RH-055, DD-RH-057, DD-RH-058, DD-RH-059.
- Open architecture/protocol/storage decisions requiring Jared approval: ARCH-RH-002, ARCH-RH-003, ARCH-RH-004, DD-RH-069, DD-RH-075, DD-RH-084, DD-RH-085, DD-RH-086, DD-RH-094, DD-RH-097, and the legacy-wallet DD bech32m compatibility decision.
- Report package written to `reports/red_hornet_security_final_report.md`.

Status: completed. Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle and directly gating shared wallet, RPC, miner, validation, index, P2P, Qt, and test/fuzz surfaces.

## Post-Campaign Continuation Wave 21 - Open Implementation Bug Closure

Date: 2026-04-29. Ledger path: `reports/red_hornet_security_ledger.md`.

Assignments:

- Agent A - Exploit-path attacker: re-check open implementation bugs DD-RH-055/DD-RH-057/DD-RH-058/DD-RH-059 for current-code reachability and safest fix order.
- Agent B - Invariant/test breaker: inventory regression coverage and missing negative cases for DD-RH-055/DD-RH-057/DD-RH-058/DD-RH-059.
- Agent C - Boundary adversary: review RPC/wallet/index/user-boundary reachability for DD-RH-055/DD-RH-057/DD-RH-058/DD-RH-059.

Required context and surface enumeration:

- Main agent reread the 15 required campaign context files in order before code analysis.
- Each sub-agent was instructed to reread the same 15 files in order before analysis and to make no edits.
- Required live surface enumeration was rerun with:
  - `find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|attack|security|wallet|qt' | sort`
  - Result: completed; output count remained `2050`, including build artifacts from the exact requested path set.

Confirmed vulnerabilities fixed in this continuation:

- DD-RH-058 (Low): `calculatecollateralrequirement` quoted collateral for amounts outside the live mint bounds.
  - Affected invariant: RPC/user-facing collateral quotes must not bless a DigiDollar amount that mint validation will reject.
  - Exploit/reachability path: a user or integrator could call `calculatecollateralrequirement` with a regtest-invalid amount above `maxMintAmount`; the RPC returned a plausible collateral quote even though `mintdigidollar` and `estimatecollateral` reject the same amount. This is reachable through the public RPC surface.
  - File references:
    - Vulnerable path before fix: `src/rpc/digidollar.cpp:595`, where only positive amount, positive lock days, and positive oracle price were checked.
    - Fixed guard: `src/rpc/digidollar.cpp:609`, now using `DigiDollar::IsValidMintAmount(ddAmount, ddParams)`.
    - Regression test: `test/functional/digidollar_rpc_collateral.py:284`, asserting `calculatecollateralrequirement(100001, 365)` fails on regtest.
  - Pre-fix attack test evidence:
    - Command: `python3 test/functional/digidollar_rpc_collateral.py`
    - Result before production fix: failed as intended with `AssertionError: No exception raised` for the above-regtest-maximum collateral quote.
  - Expected secure behavior: `calculatecollateralrequirement` must reject the same out-of-bounds mint amounts as the actual mint and estimate RPC paths.
  - Fix summary: added the shared `DigiDollar::IsValidMintAmount` bounds check to `calculatecollateralrequirement`, returning the same minimum/maximum mint amount errors used by nearby RPC paths.
  - Tests added/upgraded:
    - Added an above-regtest-maximum negative assertion to `test/functional/digidollar_rpc_collateral.py`.
    - Refreshed stale collateral response amount-scaling values so the test stays within the regtest mint maximum.
    - Clarified a stale "below minimum" log line because regtest's minimum is `1` cent and `50` cents is valid there.
  - Post-fix verification:
    - `make -C src -j$(nproc) digibyted`: passed.
    - `python3 test/functional/digidollar_rpc_collateral.py`: passed.
    - `python3 test/functional/digidollar_rpc_estimate.py`: passed.
    - `make -C src -j$(nproc) test/test_digibyte`: passed.
    - `./src/test/test_digibyte --run_test=digidollar_rh46_rpc_input_validation_tests --log_level=error --report_level=short`: passed, `63` test cases and `119` assertions.
    - `./src/test/test_digibyte --run_test=digidollar_consensus_tests/mint_amount_validation_test --log_level=error --report_level=short`: passed, `1` test case and `9` assertions.
  - Commit status: committed as `cd3f6425b5` (`rpc digidollar: fix DD-RH-058 invalid collateral quotes`).
  - Current status: fixed and committed.

- DD-RH-055 (Low): `senddigidollar` reported whole-wallet remainder as DD `change_amount` instead of selected-input change.
  - Affected invariant: RPC/wallet surfaces must not misstate DD value movement in a way that can deceive integrations, exchange accounting, or UI users.
  - Exploit/reachability path: a wallet with multiple confirmed DD UTXOs could send from one selected UTXO while the RPC reported `wallet_balance - amount`. The transaction itself conserved DD, but the public RPC response overstated the returned change.
  - File references:
    - Vulnerable response path before fix: `src/rpc/digidollar.cpp:1359`.
    - Selected-input change now returned by wallet layer: `src/wallet/digidollarwallet.cpp:1137`, `src/wallet/digidollarwallet.cpp:1636`, `src/wallet/digidollarwallet.h:494`.
    - RPC now reports selected-input change: `src/rpc/digidollar.cpp:1319`, `src/rpc/digidollar.cpp:1321`, `src/rpc/digidollar.cpp:1359`.
    - Regression test: `test/functional/digidollar_send.py:45`, `test/functional/digidollar_send.py:100`.
  - Pre-fix attack test evidence:
    - Command: `python3 test/functional/digidollar_send.py`
    - Result before production fix: failed as intended with `AssertionError: not(5900 == 900)` after the RPC reported whole-wallet remainder.
  - Expected secure behavior: `change_amount` must describe the DD change created by the selected inputs in this transfer.
  - Fix summary: added an optional `dd_change_out` result to `TransferDigiDollarMany()` / `TransferDigiDollar()` and wired `senddigidollar` to return that selected-input change.
  - Tests added/upgraded:
    - `test/functional/digidollar_send.py` now mints two DD UTXOs, sends 100 cents, and asserts selected-input change is `900` cents.
  - Post-fix verification:
    - `make -C src -j$(nproc) digibyted`: passed.
    - `python3 test/functional/digidollar_send.py`: passed; response included `change_amount: 900`.
    - `python3 test/functional/digidollar_transfer.py`: passed.
  - Commit status: committed as `9335a841c2` (`wallet digidollar: fix DD-RH-055 wrong send change`).
  - Current status: fixed and committed.

- DD-RH-057 (Low): `estimatecollateral` contained unused signed multiplication that could overflow under extreme valid/custom price inputs.
  - Affected invariant: RPC amount-estimation paths must avoid undefined behavior and misleading display math even when core collateral math uses wider arithmetic safely.
  - Exploit/reachability path: `estimatecollateral` accepted positive custom oracle prices and computed required collateral with `__int128`, then executed a dead `CAmount` multiplication of `requiredDGB * oraclePriceMicroUSD`. The value was not returned, but the expression was still reachable and sanitizer-visible.
  - File references:
    - Safe required collateral math remains at `src/rpc/digidollar.cpp:2842`.
    - Dead overflow-prone calculation removed before result construction at `src/rpc/digidollar.cpp:2851`.
    - Existing redteam proof remains at `src/test/digidollar_redteam_tests.cpp:7745`.
  - Pre-fix evidence:
    - Existing C++ redteam proof `redteam_T4_01b_usd_value_display_overflow` showed the signed product can exceed `int64_t`.
    - The build also warned about the unused `usdValueCents` variable in `src/rpc/digidollar.cpp`, confirming the expression was compiled but dead.
  - Expected secure behavior: unused display math should not execute, and returned `usd_value` should remain based on the DD amount in cents.
  - Fix summary: removed the dead `usdValueMicroUSD` / `usdValueCents` calculation and left `usd_value` as `ddAmount / 100.0`.
  - Tests added/upgraded:
    - No new sanitizer build was available in this turn. The existing redteam overflow proof was retained and rerun.
  - Post-fix verification:
    - `make -C src -j$(nproc) digibyted`: passed, and the DD-RH-057 production warning disappeared.
    - `python3 test/functional/digidollar_rpc_estimate.py`: passed.
    - `./src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_T4_01b_usd_value_display_overflow --log_level=error --report_level=short`: passed, `1` test case and `2` assertions.
  - Commit status: committed as `a96a988499` (`rpc digidollar: fix DD-RH-057 overflow-prone estimate math`).
  - Current status: fixed and committed, with residual recommendation to run a UBSan build for sanitizer proof of this UB-only path.

- DD-RH-059 (Medium): DigiDollar stats index assumed mint outputs were fixed at `vout[0..2]`, missing a consensus-valid reordered mint.
  - Affected invariant: index/RPC accounting must track DD supply, locked collateral, and active vault count for every consensus-valid mint.
  - Exploit/reachability path: a valid mint can place ordinary DGB change before the DD OP_RETURN. Consensus accepts it, but the stats index previously checked `vout[0]` as vault, `vout[1]` as token, and `vout[2]` as OP_RETURN, so the mint was mined while `getdigidollarstats` showed zero new supply/positions.
  - File references:
    - Fixed order-independent mint accounting: `src/index/digidollarstatsindex.cpp:217`.
    - Fixed actual vault outpoint storage: `src/index/digidollarstatsindex.cpp:248`.
    - Fixed redemption accounting by stored vault outpoint rather than `n == 0`: `src/index/digidollarstatsindex.cpp:268`.
    - Regression test: `test/functional/digidollar_stats_reordered_mint.py:28`, `test/functional/digidollar_stats_reordered_mint.py:82`.
  - Pre-fix attack test evidence:
    - Command: `python3 test/functional/digidollar_stats_reordered_mint.py`
    - Result before production fix: mined the reordered mint, then failed as intended with `AssertionError: not(0 == 100)` for `total_dd_supply`.
  - Expected secure behavior: stats index must use the same order-independent mint accounting accepted by consensus and must bind redemption accounting to the actual collateral outpoint.
  - Fix summary: replaced fixed-index mint parsing with `DigiDollar::ExtractMintAccountingAmounts()`, located/stored the actual collateral vault outpoint, and removed the redeem-side `prevout.n == 0` assumption.
  - Tests added/upgraded:
    - Added `test/functional/digidollar_stats_reordered_mint.py`, which reorders a signed mint so DGB change precedes the DD OP_RETURN, re-signs it, mines it, and asserts stats include the mint.
    - Added the regression to `test/functional/test_runner.py`.
  - Post-fix verification:
    - `make -C src -j$(nproc) digibyted`: passed.
    - `python3 test/functional/digidollar_stats_reordered_mint.py`: passed.
    - `python3 test/functional/digidollar_stats_reorg.py`: passed.
    - `./src/test/test_digibyte --run_test=digidollar_validation_tests/mint_accounting_extraction_allows_change_before_opreturn --log_level=error --report_level=short`: passed, `1` test case and `7` assertions.
  - Commit status: committed as `7d6ee67f0f` (`index digidollar: fix DD-RH-059 reordered mint accounting`).
  - Current status: fixed and committed.

Rejected false positives / downgrades in this continuation:

- DD-RH-058 is not an inflation or consensus acceptance bug by itself. Production mint validation still rejected the invalid amount; the confirmed issue is an RPC boundary/deception bug that can mislead operators or tooling into using invalid collateral quotes.
- No generic DigiByte issue was pursued outside DigiDollar/oracle. Shared RPC code was inspected only where it directly gated DigiDollar behavior.

Theoretical / not yet reachable:

- Wallet restore/storage still has adjacent fixed-outpoint assumptions for collateral positions. Reordered mints with only DGB change before OP_RETURN are covered by DD-RH-059, but supporting arbitrary collateral-vault output indexes in wallet position storage appears to require a wallet-storage design decision before broadening the fix.

Open implementation bugs after this continuation update:

- None from the prior DD-RH-055/DD-RH-057/DD-RH-058/DD-RH-059 open implementation set remain unfixed locally.
- No uncommitted fix code remains for DD-RH-055/DD-RH-057/DD-RH-058/DD-RH-059; each fix is committed separately by vulnerability ID.

ARCHITECTURAL_REVIEW_REQUIRED:

- Existing campaign architecture items remain unchanged: ARCH-RH-002, ARCH-RH-003, ARCH-RH-004, DD-RH-069, DD-RH-075, DD-RH-084, DD-RH-085, DD-RH-086, DD-RH-094, DD-RH-097, and the legacy-wallet DD bech32m compatibility decision.
- DD-RH-059-WALLET-STORAGE: decide whether wallet position storage must support collateral vault outpoints at indexes other than `0`. The stats index no longer assumes a fixed vault index, but wallet position records still key collateral by `txid` and commonly reconstruct `COutPoint(txid, 0)`. Extending that safely appears to require a wallet-storage/schema decision.

Scope note: stayed within DigiDollar/oracle and directly gating RPC/test surfaces.

## Post-Campaign Continuation Wave 21 - Unit Test Isolation Follow-Up

Date: 2026-04-29. Ledger path: `reports/red_hornet_security_ledger.md`.

Jared reran the full Boost unit binary and hit a full-suite-only failure:

- Command: `./src/test/test_digibyte --show_progress`
- Initial result: failed in `rh68_test_block_validity_health_metrics_side_effect_tests/test_block_validity_does_not_update_health_metrics` because `DigiDollar::ValidateDigiDollarTransaction(mint_tx, dd_context, state)` returned false.
- Root cause: the RH68 test inherited global DigiDollar volatility freeze/history state from earlier tests in the full-suite order. The mint was otherwise valid, but validation rejected it as `minting-frozen-volatility`.
- Fix: `src/test/rh61_coinbase_price_cache_poisoning_tests.cpp` now resets `DigiDollar::Volatility::VolatilityMonitor` freeze and history before and after the RH68 test.
- Commit status: committed as `3a88816293` (`tests digidollar: reset volatility in RH68 block-validity test`).
- Post-fix verification:
  - `make -C src -j$(nproc) test/test_digibyte`: passed.
  - `./src/test/test_digibyte --run_test=rh68_test_block_validity_health_metrics_side_effect_tests/test_block_validity_does_not_update_health_metrics --log_level=error --report_level=short`: passed, `1` test case and `13` assertions.
  - `./src/test/test_digibyte --show_progress`: passed, `2967` test cases.

Scope note: stayed within DigiDollar/oracle and directly gating test surfaces.

## Post-Campaign Continuation Wave 21 - Full Functional Suite Follow-Up

Date: 2026-04-29. Ledger path: `reports/red_hornet_security_ledger.md`.

Jared requested the full unit and functional suites. The first full extended functional run was not accepted as passing:

- Command: `python3 test/functional/test_runner.py --extended --jobs=4`
- Initial result: failed after `323` registered scripts.
- Failures:
  - `digidollar_mint.py`: stale valid-amount cases called `calculatecollateralrequirement` above the regtest max and now correctly received `Maximum mint amount is $1000 (100000 cents)` after DD-RH-058.
  - `feature_index_prune.py`: stale hardcoded DigiByte prune-file boundaries expected `249`, `751`, and `2006`; current branch returned `222`, `918`, and `2136`.
- Additional runner gap: `test_runner.py` warned that `37` DigiDollar/oracle functional scripts were not registered, including `feature_oracle_p2p.py`, `rpc_getoracles_pending.py`, and multiple wallet restore/rescan/reorg tests.

Fixes committed:

- `8454c57363` (`functional digidollar: align mint test with regtest bounds`): updates `test/functional/digidollar_mint.py` to use regtest-valid collateral quote amounts and boundary values.
- `c7e0887b3f` (`functional tests: refresh index prune boundaries`): updates `test/functional/feature_index_prune.py` to the current prune heights and syncs indexes to `919` before disabling them, preserving the restart/failure invariant.
- `318fc8edb9` (`functional tests: register DigiDollar oracle scripts`): registers the missing DigiDollar/oracle scripts in `test/functional/test_runner.py`, with `wallet_digidollar_descriptors.py --descriptors`.

Verification:

- `python3 test/functional/digidollar_mint.py`: passed.
- `python3 test/functional/feature_index_prune.py`: passed.
- `python3 test/functional/test_runner.py --jobs=1 wallet_digidollar_descriptors.py --descriptors`: passed.
- `python3 test/functional/test_runner.py --extended --jobs=4`: passed, `360` registered scripts; accumulated test time `3021s`, runtime `804s`.
- `git diff --check`: passed before committing the test fixes.

Residual note:

- The full runner still warns that `feature_assumevalid.py` and `feature_assumeutxo.py` are not registered. `test/functional/test_runner.py` already documents both as disabled because they require DigiByte MultiAlgo-specific adaptation; they are generic chain tests, not DigiDollar/oracle tests.

Scope note: stayed within DigiDollar/oracle where possible; `feature_index_prune.py` was touched only because Jared requested the full functional suite and it was a deterministic registered-suite failure.
