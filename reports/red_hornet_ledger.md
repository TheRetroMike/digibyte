# Red Hornet DigiDollar Security Campaign — Running Ledger

**Branch:** `feature/digidollar-v1`
**Started:** 2026-04-28
**Operator:** Jared (j2035@pm.me)
**Scope:** DigiDollar / oracle / MuSig2 attack surface only — see `Z_RED_HORNET_v2.md`.
**Tree at start:** existing untracked campaign files present: `Z_RED_HORNET_v2.md`, `reports/red_hornet_ledger.md`; HEAD `5a43bc5d64 test: adapt pruning coverage to DigiByte RC33 behavior`

This file is the single source of truth for confirmed vulnerabilities, false positives, theoretical risks, architectural-review items, tests added, fixes landed, and commit hashes. Every wave appends to it.

---

## Vulnerability Index

| ID | Severity | Subsystem | Title | Status | Commit |
|----|----------|-----------|-------|--------|--------|
| DD-RH-001 | Medium | DigiDollar block/health accounting | Valid reordered mint can pass consensus but be missed or mis-accounted by fixed-output health accounting. | Fixed, uncommitted | none |
| DD-RH-002 | High | DigiDollar consensus / mint validation | IBD/catch-up `skipOracleValidation` can accept undercollateralized DD mints, creating validation-mode consensus disagreement risk. | Open, `ARCHITECTURAL_REVIEW_REQUIRED` | none |
| DD-RH-003 | Low | DigiDollar DCA / collateral ratio | Fractional DCA multipliers rounded down, undercutting required collateral ratios. | Fixed, uncommitted | none |
| DD-RH-004 | Low | DigiDollar collateral math / RPC estimates | Required collateral used floor division, accepting one satoshi below the mathematical collateral requirement. | Fixed, uncommitted | none |
| DD-RH-005 | High | DigiDollar redemption / collateral release | Reordered mint collateral at nonzero vout could be treated as a redemption fee input, unlocking backing collateral without burning that mint's DD. | Fixed, uncommitted | none |
| DD-RH-006 | Low | DigiDollar RPC protection status | `getprotectionstatus` reported ERR/emergency when no DD liabilities existed and hardcoded volatility protection inactive. | Fixed, uncommitted | none |
| DD-RH-007 | Critical | DigiDollar transfer / OP_RETURN metadata | Transfer validation could accept later valid metadata while later spend extraction read an earlier fake DD OP_RETURN, enabling DD amount inflation. | Fixed, uncommitted | none |
| DD-RH-008 | Medium | DigiDollar address validation / RPC | Static and RPC DD address validation accepted corrupted or wrong-shape Base58Check strings as valid. | Fixed, uncommitted | none |
| DD-RH-009 | Medium | DigiDollar RPC watch-only import | `importdigidollaraddress` reported success and fake rescan counts without importing, watching, or rescanning the address. | Fixed, uncommitted | none |

---

## False-Positive / Already-Mitigated Index

| ID | Subsystem | Claim | Why rejected |
|----|-----------|-------|--------------|
| FP-RH-001 | RPC amount parsing | `senddigidollar` still has exception-unsafe `std::stod` / NaN-to-int casts. | Current production helper `ParseDigiDollarRpcAmount()` catches `std::stod`, verifies full string consumption, rejects non-finite values, bounds before casting, and is used by `senddigidollar` and multi-send parsing (`src/rpc/digidollar.cpp:235`, `src/rpc/digidollar.cpp:1318`, `src/rpc/digidollar.cpp:1477`). Existing `rh62` test mirrors old code rather than the live helper, so it is coverage debt, not a current reachable bug. |
| FP-RH-002 | Mint OP_RETURN fallback | A mint with OP_RETURN DD amount `0` can fall through to collateral-derived amount and be accepted. | Rejected in current code: the early mint output scan calls `ExtractDDAmount()` on the DD OP_RETURN and rejects invalid mint amounts before the fallback path is reached (`src/digidollar/validation.cpp:720`-`src/digidollar/validation.cpp:727`). A local regression attempt with OP_RETURN amount `0` rejected before any fix. |

---

## Theoretical / Not Yet Reachable

| ID | Subsystem | Claim | Why not reachable today |
|----|-----------|-------|------------------------|
| TR-RH-001 | Transfer OP_RETURN parsing | Extra amount pushes after the real DD output amount vector may be ignored. | Resolved as part of `DD-RH-007`: transfer validation now requires the OP_RETURN amount count to match the DD output count, and source extraction rejects ambiguous DD OP_RETURN metadata. |
| TR-RH-003 | RPC/wallet boundary | `importdigidollaraddress`, `getdigidollarbalance` options, `senddigidollar` `change_amount`, and `listdigidollarpositions min_amount` show reachable accounting/UX drift. | Agent C found reachable code paths, but Wave 2 did not yet classify user-loss severity or implement fixes. Carry to wallet/RPC waves 9-11. |
| TR-RH-004 | Qt/RPC collateral display | Qt `WalletModel::calculateRequiredCollateral()` has stale ratios for tiers 5-8; RPC collateral estimates report the base consensus requirement while `MintTxBuilder` locks a 1% safety margin. | Reachable UI/estimate drift, but Wave 3 did not add Qt harness coverage or decide whether RPC should report consensus minimum vs builder-padded collateral. Carry to RPC/Qt waves 11-12. |
| TR-RH-005 | DigiDollar RPC address listing | `listdigidollaraddresses` only lists addresses inferred from current DD UTXOs, hides generated zero-balance DD addresses, and hardcodes `iswatchonly=false`. | Real RPC/accounting UX gap, but fixing it requires a wallet address-index/watch-only storage design. Carry to wallet/RPC waves 9-11 and `ARCH-RH-003`. |
| TR-RH-006 | DigiDollar address network routing | `CDigiDollarAddress` accepts valid DD/TD/RD versions independent of the active chain, so a mainnet wallet may accept a testnet/regtest-prefixed DD address if checksum/version are valid. | Not fixed in Wave 5 because cross-network address policy needs an explicit compatibility decision. Carry to RPC/Qt waves. |

---

## Architectural-Review-Required

| ID | Subsystem | Issue | Decision needed |
|----|-----------|-------|-----------------|
| ARCH-RH-001 | Oracle consensus/liveness | Post-activation blocks with no `OP_ORACLE` output, or with an unparseable `OP_ORACLE` payload, still return valid through transition/liveness escape hatches (`src/oracle/bundle_manager.cpp:2327`, `src/oracle/bundle_manager.cpp:2336`). Existing `rh63_oracle_validator_escape_hatches_tests` proves this reachable and `rh65_mainnet_testnet_validator_parity_tests` documents the liveness intent. | Decide whether post-activation oracle data is mandatory consensus data or best-effort liveness data. Strict mode prevents miner suppression/stale-price stretches but can halt block production when oracle quorum is unavailable. Best-effort mode preserves block liveness but allows miners to omit/mangle oracle data and keep price caches stale. No consensus change applied without Jared approval. |
| ARCH-RH-002 | DigiDollar consensus / IBD validation | `ConnectBlock` sets `fSkipOracle=true` during IBD and for catch-up blocks with no block oracle price (`src/validation.cpp:2940`, `src/validation.cpp:2943`). `ValidateMintTransaction` then skips oracle-price rejection and all collateral sufficiency/ratio checks (`src/digidollar/validation.cpp:737`, `src/digidollar/validation.cpp:1126`). Existing `digidollar_skip_oracle_tests/skip_oracle_allows_insufficient_collateral_exploit` demonstrates that a structurally valid mint with 1000 satoshis collateral for 10000 DD cents passes when `skipOracleValidation=true` and fails with `insufficient-collateral` when false. | Decide how historical sync should validate DD mints. Recommended direction: split `skipOracleValidation` into deterministic historical-price handling vs non-deterministic runtime protections, and require collateral checks whenever a DD mint is post-activation and a deterministic block oracle price is available; decide whether missing price post-activation is a consensus reject or a launch-transition exception. No consensus change applied without Jared approval. |
| ARCH-RH-003 | DigiDollar wallet/RPC watch-only address support | `importdigidollaraddress` previously pretended to import and rescan, but there is no implemented DD watch-only address storage/rescan/listing model. Wave 5 changed the RPC to return `success=false` with an explicit warning instead of false success. | Decide whether DD watch-only addresses should be stored in the main wallet address book, a DD-specific watch-only table, descriptors, or a separate DD wallet index; decide rescan semantics and how `listdigidollaraddresses` should represent zero-balance and watch-only DD addresses. |

---

## Wave Log

(Each wave appends a section below.)

---

## Campaign Initialization — 2026-04-28

**Required context:** Main agent read the required files in the requested order:
`CLAUDE.md`, `ARCHITECTURE.md`, `REPO_MAP.md`, `REPO_MAP_GUIDE.md`, `DIGIDOLLAR_ARCHITECTURE.md`, `DIGIDOLLAR_ORACLE_ARCHITECTURE.md`, `REPO_MAP_DIGIDOLLAR.md`, `DIGIDOLLAR_EXPLAINER.md`, `DIGIDOLLAR_ORACLE_EXPLAINER.md`, `DIGIDOLLAR_ACTIVATION_EXPLAINER.md`, `DIGIDOLLAR_WALLET_INTEGRATION.md`, `DIGIDOLLAR_EXCHANGE_INTEGRATION.md`, `DIGIDOLLAR_OPRETURN_PQC_MINT_PLAN.md`, `ORACLE_DISCOVERY_ARCHITECTURE.md`, `DIGIDOLLAR_ORACLE_SETUP.md`.

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed. The requested command includes build artifacts and generated Qt/build files in addition to source/test files; production and test scope remains constrained to DigiDollar/oracle/MuSig2 slices.

**Initial git status command/result:**

```bash
git status --short
```

Result:

```text
?? Z_RED_HORNET_v2.md
?? reports/red_hornet_ledger.md
```

No code edits or commits made during initialization.

---

## Wave 1 — Threat Model, Baseline, Test Inventory

**Ledger path:** `reports/red_hornet_ledger.md`

**Assignments launched:**
- Agent A — Reachability prober / invariant mapper: production-code invariant map for mint, transfer, redeem, oracle, MuSig2, activation, block/mempool hooks, and state updates.
- Agent B — Invariant and test-gap reviewer: coverage matrix across DD/oracle/MuSig2/red-team/unit/fuzz/functional/wallet/Qt tests.
- Agent C — Boundary reviewer / baseline test runner: identify available test binaries, run representative baseline DD/oracle tests, record pass/fail/flake evidence.

**Scope note:** DigiDollar/oracle/MuSig2 only; no generic DigiByte review.

**Confirmed bugs:**

### DD-RH-003 — DCA fractional multipliers rounded collateral ratios down

**Severity:** Low

**Affected invariant:** A DD mint must use the intended collateral ratio after DCA. Fractional DCA multipliers must not reduce the effective required ratio.

**Reachability path:** `DynamicCollateralAdjustment::ApplyDCA()` multiplied an integer base ratio by a fractional DCA multiplier and truncated the double back to `int`. Examples from live consensus helper: 7-year tier at warning health used `212 * 1.2 = 254.4 -> 254` instead of a conservative 255; 2-year tier at critical health used `275 * 1.5 = 412.5 -> 412` instead of 413. Mint validation consumes this value through `GetEffectiveCollateralRatio()` and `CalculateRequiredCollateral()`, so the lower ratio is reachable during warning/critical health.

**Pre-fix affected code:** `src/consensus/dca.cpp:145`-`src/consensus/dca.cpp:148` before the fix.

**Failing regression evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_rh32_collateral_dca_tests/rh32_applydca_truncation_attack
```

Pre-fix result: failed with 2 failures, `[254 != 255]` and `[412 != 413]`.

**Fix summary:** `ApplyDCA()` now rounds fractional adjusted ratios upward with `std::ceil()` at `src/consensus/dca.cpp:146`-`src/consensus/dca.cpp:148`. `MintTxBuilder` now also ceilings its adjusted ratio before converting to integer at `src/digidollar/txbuilder.cpp:172`-`src/digidollar/txbuilder.cpp:175`. Stale DCA/red-team expectations were updated to the conservative rounding behavior.

**Tests added/upgraded:**
- `src/test/digidollar_rh32_collateral_dca_tests.cpp:97` `rh32_applydca_truncation_attack`.
- `src/test/digidollar_dca_tests.cpp` expectations updated to use ceiling for fractional DCA ratios.
- `src/test/digidollar_redteam_tests.cpp` precision expectation updated from truncation to ceiling.

**Post-fix evidence:** targeted regression, `digidollar_dca_tests`, `digidollar_rh32_collateral_dca_tests`, `rh64_dca_table_disagreement_tests`, and the broad `./src/test/test_digibyte '--run_test=digidollar_*'` run passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-006 — `getprotectionstatus` reported false ERR/emergency with zero DD supply

**Severity:** Low

**Affected invariant:** RPC/user-facing protection status must not mislead operators or users about ERR/volatility restrictions. With zero DD liabilities, ERR must not be reported active, and volatility fields should reflect the live protection state used by validation.

**Reachability path:** On an activated regtest node with no DD minted, `getprotectionstatus` set `systemHealth = 0`, then used `IsSystemEmergency(0)` directly. The RPC returned `err.active=true`, `err.status="emergency"`, `overall.status="emergency"`, and listed `err` as an active protection even though consensus `ShouldBlockMinting()` allows minting when `totalDDSupply <= 0`. The same RPC hardcoded volatility as inactive while validation checks `VolatilityMonitor::ShouldFreezeMinting()` / `ShouldFreezeAll()` for mint/transfer/redeem paths.

**Pre-fix affected code:** `src/rpc/digidollar.cpp:3449`-`src/rpc/digidollar.cpp:3491`.

**Failing regression evidence:**

```bash
python3 test/functional/digidollar_rpc_protection.py
```

Pre-fix result: failed with `AssertionError: Invalid ERR status: emergency` immediately after activation on a no-DD-supply chain.

**Fix summary:**
- `getdigidollarstats` and `getprotectionstatus` now only treat low health as ERR/emergency when `totalDD > 0`.
- `getprotectionstatus` returns valid ERR status strings (`normal`, `warning`, `active`, `critical`) and valid overall statuses (`secure`, `warning`, `critical`, `emergency`).
- With zero DD supply, `err.active=false`, `err.status="normal"`, and `overall.status="secure"`.
- Volatility fields now read live `VolatilityMonitor` state instead of hardcoding inactive/zero values.

**Post-fix affected code:** `src/rpc/digidollar.cpp:431`, `src/rpc/digidollar.cpp:3459`, `src/rpc/digidollar.cpp:3472`-`src/rpc/digidollar.cpp:3540`.

**Tests added/upgraded:** `test/functional/digidollar_rpc_protection.py:99` and `test/functional/digidollar_rpc_protection.py:149` assert no-supply ERR is inactive and overall status is secure.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/digidollar_rpc_protection.py
python3 test/functional/digidollar_protection_status.py
./src/test/test_digibyte --run_test=digidollar_rpc_tests
./src/test/test_digibyte --run_test=digidollar_volatility_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-004 — Required collateral floor division allowed one-satoshi undercollateralization

**Severity:** Low

**Affected invariant:** Collateral requirement math must reject any mint below the exact required collateral. Fractional satoshi requirements must round up.

**Reachability path:** `CalculateRequiredCollateral()` used integer floor division for `(dd_cents * COIN * effective_ratio * 100) / oracle_micro_usd`. For $100 DD, 1-hour tier, and a 6310 micro-USD oracle price, the mathematical requirement is `15847860538828` sats; pre-fix code returned `15847860538827` and `ValidateCollateralRatio(required - 1, ...)` accepted one satoshi below the rounded-up requirement.

**Pre-fix affected code:** `src/digidollar/validation.cpp:480` before the fix. Matching estimator paths in `src/digidollar/txbuilder.cpp` and `src/rpc/digidollar.cpp` also used floor division.

**Failing regression evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_rh32_collateral_dca_tests/rh32_required_collateral_rounds_up
```

Pre-fix result: failed with 2 failures, including `[15847860538827 != 15847860538828]` and acceptance of one satoshi below the rounded-up requirement.

**Fix summary:**
- Consensus collateral math now uses ceiling division at `src/digidollar/validation.cpp:478`-`src/digidollar/validation.cpp:481`.
- Mint txbuilder and RPC collateral-estimate paths now use the same ceiling division at `src/digidollar/txbuilder.cpp:172`-`src/digidollar/txbuilder.cpp:175`, `src/rpc/digidollar.cpp:679`-`src/rpc/digidollar.cpp:682`, and `src/rpc/digidollar.cpp:2797`-`src/rpc/digidollar.cpp:2800`.
- Stale red-team expectation for high-price truncation was updated: a fractional result now rounds up to 1 satoshi instead of truncating to 0.

**Tests added/upgraded:**
- `src/test/digidollar_rh32_collateral_dca_tests.cpp:249` `rh32_required_collateral_rounds_up`.
- `src/test/digidollar_redteam_tests.cpp` high-price collateral truncation expectation updated.

**Post-fix test evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_rh32_collateral_dca_tests/rh32_applydca_truncation_attack
./src/test/test_digibyte --run_test=digidollar_rh32_collateral_dca_tests/rh32_required_collateral_rounds_up
./src/test/test_digibyte --run_test=digidollar_rh32_collateral_dca_tests
./src/test/test_digibyte --run_test=digidollar_dca_tests
./src/test/test_digibyte --run_test=digidollar_validation_tests
./src/test/test_digibyte --run_test=digidollar_mint_tests
./src/test/test_digibyte --run_test=digidollar_txbuilder_tests
./src/test/test_digibyte --run_test=digidollar_rpc_tests
./src/test/test_digibyte --run_test=rh64_dca_table_disagreement_tests
./src/test/test_digibyte '--run_test=digidollar_*'
python3 test/functional/digidollar_rpc_collateral.py
python3 test/functional/digidollar_rpc_estimate.py
```

Results: all listed C++ tests and `digidollar_rpc_collateral.py` passed. `digidollar_rpc_estimate.py` still fails on stale test data that tries `$10,000` (`1000000` cents) after the RPC reports `Maximum mint amount is $1000 (100000 cents)`; this failure pre-existed the Wave 3 fix and is recorded as stale functional-test debt.

**Rejected false positives in Wave 3:**
- Mint OP_RETURN zero-amount fallback: rejected as `FP-RH-002`; current early OP_RETURN amount validation already rejects it before fallback.
- Redemption collateral loss via fee/change mixing: Agent C rejected it; builder returns full collateral separately and validator rejects collateral-as-fee inputs.
- Partial redemption theft: Agent C rejected it; RPC and validator enforce full-vault redemption.
- Transfer OP_RETURN builder/parser mismatch: Agent C rejected it for live behavior; builder and validator both use the raw transfer amount list.

**Theoretical / carried forward:**
- `TR-RH-004`: Qt `WalletModel` stale ratios and RPC/base-vs-builder collateral display drift belong in the RPC/Qt waves with proper harness coverage.
- High-recipient `sendmanydigidollar` fee growth versus fixed wallet fee-input selection needs a focused wallet/RPC test before it is counted as a bug.
- Tier edge coverage remains uneven; add a chainparams-driven `tier - 1`, exact, `tier + 1` table later.

**Agent A result summary:** no final text returned for Wave 3; no files edited by the agent.

**Agent B result summary:** confirmed broad coverage for mint amount bounds, collateral exactness, DCA, overflow clamps, oracle price bounds, and tier edges; highlighted missing full mint fallback regression, canonical tier-edge loops, and stale tests.

**Agent C result summary:** found Qt/RPC collateral estimate drift, `getredemptioninfo` display underreporting, and a high-recipient transfer fee-selection test gap; rejected redemption fee/collateral theft, partial redemption, and transfer OP_RETURN mismatch hypotheses. Ran txbuilder, transfer, redeem, no-partial-redeem, script attack, miner DD, RH-32, RH-64, and relevant functional tests; only `digidollar_rpc_estimate.py` failed due stale max-mint expectation.

**Wave 3 status:** complete. Two low-severity rounding bugs fixed (`DD-RH-003`, `DD-RH-004`), no commits, scope stayed within DigiDollar/oracle.

**Agent C baseline results received:** no repo edits.

Required surface enumeration was run exactly as requested and returned 1887 matching paths, including source, tests, build artifacts, Qt files, wallet files, fuzz targets, and functional tests.

Available binaries:

```text
src/test/test_digibyte
src/qt/test/test_digibyte-qt
src/test/fuzz/fuzz
```

Representative C++ baseline commands all passed:

```bash
./src/test/test_digibyte --run_test=digidollar_activation_tests
./src/test/test_digibyte --run_test=digidollar_validation_tests
./src/test/test_digibyte --run_test=digidollar_rpc_tests
./src/test/test_digibyte --run_test=digidollar_p2p_tests
./src/test/test_digibyte --run_test=oracle_config_tests
./src/test/test_digibyte --run_test=oracle_rpc_tests
./src/test/test_digibyte --run_test=oracle_p2p_tests
./src/test/test_digibyte --run_test=musig2_basic_tests
./src/test/test_digibyte --run_test=musig2_p2p_message_tests
./src/test/test_digibyte --run_test=rh63_oracle_validator_escape_hatches_tests
./src/test/test_digibyte --run_test=rh65_mainnet_testnet_validator_parity_tests
./src/test/test_digibyte --run_test=digidollar_persistence_wallet_tests
./src/test/test_digibyte --run_test=digidollar_wallet_security_tests
./src/test/test_digibyte --run_test=rh59_coincontrol_dd_lock_bypass_tests
```

Qt boundary: `./src/qt/test/test_digibyte-qt --list_content 2>&1 | grep -Ei 'digidollar|wallet|qt'` executed the Qt runner; `WalletTests` and `DigiDollarWidgetTests` passed. Warnings observed: unsupported `propagateSizeHints()` and missing `receivedColor`/`sentColor` table properties.

Functional baseline:

```bash
test/functional/digidollar_validate_address.py
python3 test/functional/digidollar_validate_address.py
test/functional/digidollar_rpc_gating.py
test/functional/rpc_getoracles_pending.py
python3 test/functional/feature_oracle_p2p.py
```

Results: direct execution of `digidollar_validate_address.py` failed with exit 126 because the file is not executable; rerun with `python3` passed. `digidollar_rpc_gating.py`, `rpc_getoracles_pending.py`, and `feature_oracle_p2p.py` passed. `feature_oracle_p2p.py` reported missing RPC/P2P implementation paths as expected and still ended with `Tests successful`.

Fuzz inventory/smoke:

```bash
PRINT_ALL_FUZZ_TARGETS_AND_ABORT=1 ./src/test/fuzz/fuzz 2>&1 | grep -Ei 'digi|dd|oracle|musig|wallet|p2p' | sort
FUZZ=dd_amount_validation ./src/test/fuzz/fuzz /dev/null
FUZZ=oracle_musig2_bitmap ./src/test/fuzz/fuzz /dev/null
FUZZ=dd_amount_validation ./src/test/fuzz/fuzz -runs=1
```

Results: target inventory passed; `dd_amount_validation` and `oracle_musig2_bitmap` `/dev/null` smokes passed; `-runs=1` is unsupported by this harness and failed by treating `-runs=1` as an input file.

**Main-agent classifications added during Wave 1:**
- `FP-RH-001`: current `senddigidollar` amount parsing is already mitigated; existing `rh62` mirrors pre-fix logic and should be modernized in a later test-cleanup pass.
- `ARCH-RH-001`: post-activation missing/unparseable oracle-output acceptance is reachable by existing tests but is an explicit liveness/consensus-policy decision, so no fix was applied.

**Agent A synthesis — production invariant map:**
- Activation gates are present in consensus, mempool, script flags, RPC, and oracle P2P. Weak spots to carry forward: oracle P2P uses height-based activation, `CheckBlock` falls back to BIP34 height when no block index is available, and missing oracle data is accepted as liveness behavior.
- Mint validation enforces output structure, owner pubkey, NUMS collateral reconstruction, DD amount/lock parsing, and collateral checks. Weak spots to carry forward: lock-tier mismatch tolerance, IBD/catch-up `skipOracleValidation`, and wallet builder placeholder system health.
- Transfer validation enforces confirmed-only DD inputs and strict DD conservation. Weak spot: amount/source recovery depends on tx metadata and block DB lookup.
- Redemption validation enforces confirmed DD input, burn accounting, normal timelock, full-burn release, and collateral-as-fee rejection. Weak spots: `ValidateScriptPathSpending()` is a stub; `ctx.coins == nullptr` release path is only safe if production always supplies coins; ERR redemption remains incomplete/rejecting.
- System health updates happen on connect/disconnect. Weak spots: fallback 150% health can mask DCA/ERR if data unavailable; wallet surfaces can use wallet-local health before consensus checks.

**Agent B synthesis — coverage inventory/gaps:**
- Inventory: 67 `digidollar*.cpp` unit/red-team files, 16 `oracle*.cpp`, 19 `musig2*.cpp`, 20 `rh*`/`redteam*` suites, 16 DD/oracle fuzz targets, 3 wallet DD/RH test files, `src/qt/test/digidollarwidgettests.cpp`, and 51 functional DD/oracle/wallet tests.
- Strong coverage exists for activation gates, mint output structure, oracle message/signature/timestamp, Phase 2 threshold/median, mainnet/testnet validator parity, MuSig2 session basics, and transfer conservation.
- Coverage gaps to carry forward: redemption/collateral-release hard-fail when source metadata cannot be recovered, ERR matrix, fresh mint lock-tier/script mismatch, transfer OP_RETURN amount-vector malformed cases, Phase 3 price range/activation tests, MuSig2 stale/unauthorized/replay P2P cases, static bitmap threshold vs chainparams, and deterministic mocked exchange-failure tests.

**Theoretical risks carried forward from Wave 1:**
- Oracle price cache removal on disconnect may only inspect `coinbase.vout[1]`, while extraction/cache update scans all coinbase outputs. This needs a reorg/replay proof in Wave 7/15 before it is counted as a bug.
- `ValidateBundle()` returns true for v3 and `GetBestHeight()` is stubbed, but both appear dead/unwired in current in-scope paths. Keep as theoretical unless later wiring proves reachability.
- Mint lock-tier mismatch tolerance may be exploitable only if collateral is computed from the weaker declared tier rather than actual lock period; Wave 3/5 should prove or reject.

**Wave 1 status:** complete. No confirmed bugs fixed, no production code changes, no commits.

---

## Wave 2 — Supply Integrity (Over-Issuance)

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1887 matching paths, including generated/build artifacts.

**Assignments launched:**
- Agent A — Reachability prober: production mint/transfer paths for unauthorized DD creation or supply growth.
- Agent B — Invariant and test-gap reviewer: conservation/supply tests and missing assertions.
- Agent C — Boundary reviewer: RPC/wallet/functional paths for supply/accounting drift.

**Scope note:** DigiDollar/oracle/MuSig2 only; no generic DigiByte review.

**Confirmed bugs:**

### DD-RH-001 — Valid reordered mint skipped/mis-accounted by fixed-output health accounting

**Severity:** Medium

**Affected invariant:** DD supply/collateral health accounting must match every consensus-valid mint and redemption source. Reordered valid mints must not corrupt cached DD supply/collateral or DCA/ERR health inputs.

**Reachability path:** `ValidateMintTransaction()` accepts mint outputs by scanning for exactly one positive P2TR collateral output, one zero-value DD P2TR output, and one DD OP_RETURN; it does not require the builder's output order. Before the fix, block accounting assumed fixed positions: connect used `tx.vout[2]` for OP_RETURN and `tx.vout[0]` for collateral, disconnect used `FindDDOpReturn()` for amount but still `tx.vout[0]` for collateral, and redeem source lookup assumed `origMintTx->vout[2]`. A raw or non-standard builder transaction with ordinary DGB change before the DD OP_RETURN remained consensus-valid but produced zero/missing accounting data.

**Pre-fix affected code:**
- `src/validation.cpp:2976` / `src/validation.cpp:2978`: connect used `tx.vout[2]` and `tx.vout[0]`.
- `src/validation.cpp:2420`: disconnect used `tx.vout[0]` for collateral.
- `src/validation.cpp:2991`: redeem source lookup used `origMintTx->vout[2]`.
- `src/digidollar/health.cpp:363`: UTXO scanner only considered `key.n == 0` candidates.

**Failing regression evidence:**

```bash
./src/test/test_digibyte --run_test=digidollar_validation_tests/mint_accounting_extraction_allows_change_before_opreturn
```

Pre-fix result: failed with 3 failures. The test built a structurally valid mint with outputs `[collateral, DD token, ordinary DGB change, DD OP_RETURN]`; `ValidateMintTransaction()` passed, while fixed-index accounting extraction returned false and left `extractedDD == -1`, `extractedCollateral == 0`.

**Fix summary:**
- Added `DigiDollar::ExtractMintAccountingAmounts()` in `src/digidollar/validation.cpp:372` and declaration in `src/digidollar/validation.h:181`.
- Helper requires mint tx type, finds the DD OP_RETURN by scan, extracts the DD amount, and scans for exactly one positive P2TR collateral output without assuming output order.
- Updated block disconnect mint accounting and redeem source lookup to use the helper at `src/validation.cpp:2417` and `src/validation.cpp:2434`.
- Updated block connect mint accounting and redeem source lookup to use the helper at `src/validation.cpp:2972` and `src/validation.cpp:2986`.
- Updated `SystemHealthMonitor::ScanUTXOSet()` to scan positive P2TR vault candidates at any output index and verify them through the helper at `src/digidollar/health.cpp:370` and `src/digidollar/health.cpp:397`.

**Tests added/upgraded:**
- `src/test/digidollar_validation_tests.cpp:3095` `mint_accounting_extraction_allows_change_before_opreturn`.

**Post-fix test evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_validation_tests/mint_accounting_extraction_allows_change_before_opreturn
./src/test/test_digibyte --run_test=digidollar_validation_tests
./src/test/test_digibyte --run_test=digidollar_health_tests
./src/test/test_digibyte --run_test=digidollar_rh16_reorg_attacks_tests
./src/test/test_digibyte --run_test=digidollar_rh34_multiblock_state_tests
./src/test/test_digibyte --run_test=digidollar_redeem_tests
./src/test/test_digibyte --run_test=digidollar_rh11_consensus_tests
./src/test/test_digibyte --run_test=digidollar_skip_oracle_tests
./src/test/test_digibyte '--run_test=digidollar_*'
```

Results: all passed after the fix. The broad DigiDollar wildcard run passed 1738 test cases. One attempted unquoted shell command, `./src/test/test_digibyte --run_test=digidollar_*`, failed in zsh before running tests because the shell expanded the wildcard; rerun with quotes passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-002 — IBD/catch-up `skipOracleValidation` can accept undercollateralized mints

**Severity:** High

**Affected invariant:** A DD mint cannot be accepted unless collateral, DD amount, lock tier, health, price, script, and activation rules are valid. Consensus validation must not depend on whether the node is live-synced or in IBD/catch-up.

**Reachability path:** `ConnectBlock()` sets `fSkipOracle=true` during IBD and for catch-up blocks with no block oracle price (`src/validation.cpp:2940`, `src/validation.cpp:2943`), then passes it into the DigiDollar consensus context (`src/validation.cpp:2951`). `ValidateMintTransaction()` skips zero-price rejection when the flag is set (`src/digidollar/validation.cpp:737`) and skips required-collateral calculation, sufficiency, and ratio validation (`src/digidollar/validation.cpp:1126`). Existing `digidollar_skip_oracle_tests/skip_oracle_allows_insufficient_collateral_exploit` proves the validator accepts a 10000-cent mint with 1000 satoshis collateral under `skipOracleValidation=true` and rejects it with `insufficient-collateral` when false.

**Status:** open, `ARCHITECTURAL_REVIEW_REQUIRED`. No consensus/policy change applied without Jared approval. Recommended design question recorded as `ARCH-RH-002`.

**Rejected false positives in Wave 2:**
- Transfer DD over-output was rejected: current validation obtains DD input amounts from txindex/block-db/coins metadata and enforces `inputDD == outputDD`.
- Unconfirmed DD input chaining is fail-closed through `MEMPOOL_HEIGHT` source rejection.
- Coinbase DD creation is rejected in block validation and DD source extraction.
- Non-DD source transactions are rejected when DD source extraction cannot find a DD marker.

**Theoretical / not yet reachable carried forward:**
- `TR-RH-001`: extra transfer OP_RETURN amount pushes may be ignored, but no spendable over-issuance path was proven because conservation binds actual DD outputs.
- `TR-RH-003`: RPC/wallet boundary accounting issues from Agent C are reachable enough to revisit, but severity and fixes belong in wallet/RPC waves 9-11.

**Agent A result summary:** confirmed `DD-RH-002`; rejected transfer over-output, unconfirmed input chaining, non-DD source, and coinbase source hypotheses; noted metadata fallback as theoretical unless block lookup can be made to fail in production validation.

**Agent B result summary:** mint and transfer conservation coverage is broad. Recommended regressions for extra transfer OP_RETURN amount arity and mint zero OP_RETURN fallback. Identified stale/red-team tests that document behavior but do not exercise live validator paths.

**Agent C result summary:** found RPC/wallet boundary drift candidates: `importdigidollaraddress` reports success without importing/watching/rescanning, `getdigidollarbalance` exposes but does not apply `minconf`/`include_watchonly`, `senddigidollar` reports wallet remaining balance as `change_amount`, and `listdigidollarpositions min_amount` compares DGB units to DD cents. These are carried to wallet/RPC waves.

**Wave 2 status:** complete. One bug fixed (`DD-RH-001`), one high-severity consensus-mode bug opened for architectural approval (`DD-RH-002`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 3 — Collateral Accounting, Rounding, Overflow

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1887 matching paths, including generated/build artifacts.

**Assignments launched:**
- Agent A — Reachability prober: collateral math, cents/COIN conversions, rounding, overflow/underflow, `__int128` casts, `MAX_MONEY`, and ratio clamps in production code.
- Agent B — Invariant and test-gap reviewer: boundary tests for collateral ratios, `MAX_MONEY`, unit conversions, invalid amounts, overflow clamps, and tier edges.
- Agent C — Boundary reviewer: txbuilder/RPC/wallet/functional paths for collateral/change/fee behavior and validator mismatch.

**Scope note:** DigiDollar/oracle/MuSig2 only; no generic DigiByte review.

**Confirmed bugs:** `DD-RH-003`, `DD-RH-004` fixed, uncommitted. Detailed bug records are in the vulnerability sections above and in the index.

**Rejected false positives:** `FP-RH-002` added for the zero-amount mint OP_RETURN fallback hypothesis.

**Theoretical / not yet reachable carried forward:**
- `TR-RH-004`: Qt/RPC collateral display drift, including stale Qt tier ratios and RPC estimate-vs-builder padding.
- High-recipient `sendmanydigidollar` fee growth versus fixed wallet fee-input selection needs a focused wallet/RPC test before it is counted as a bug.
- Tier edge coverage remains uneven; add a chainparams-driven `tier - 1`, exact, `tier + 1` table later.

**Tests added/upgraded:**
- `src/test/digidollar_rh32_collateral_dca_tests.cpp`: DCA ceiling expectations and one-satoshi-under required-collateral regression.
- `src/test/digidollar_dca_tests.cpp`: DCA expected ratios now use conservative ceiling.
- `src/test/digidollar_redteam_tests.cpp`: red-team precision/truncation expectations updated for conservative rounding.

**Post-fix commands/results:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_rh32_collateral_dca_tests
./src/test/test_digibyte --run_test=digidollar_dca_tests
./src/test/test_digibyte --run_test=digidollar_validation_tests
./src/test/test_digibyte --run_test=digidollar_mint_tests
./src/test/test_digibyte --run_test=digidollar_txbuilder_tests
./src/test/test_digibyte --run_test=digidollar_rpc_tests
./src/test/test_digibyte --run_test=rh64_dca_table_disagreement_tests
./src/test/test_digibyte '--run_test=digidollar_*'
python3 test/functional/digidollar_rpc_collateral.py
python3 test/functional/digidollar_rpc_estimate.py
```

Results: all listed unit tests and `digidollar_rpc_collateral.py` passed. `digidollar_rpc_estimate.py` failed because the functional test still expects a `$10,000` mint while the live RPC maximum is `$1,000`; recorded as stale functional-test debt, not a new production bug.

**Agent A result summary:** no final text returned for Wave 3; no files edited by the agent.

**Agent B result summary:** confirmed broad coverage for mint amount bounds, collateral exactness, DCA, overflow clamps, oracle price bounds, and tier edges; highlighted missing full mint fallback regression, canonical tier-edge loops, and stale tests.

**Agent C result summary:** found Qt/RPC collateral estimate drift, `getredemptioninfo` display underreporting, and a high-recipient transfer fee-selection test gap; rejected redemption fee/collateral theft, partial redemption, and transfer OP_RETURN mismatch hypotheses. Ran txbuilder, transfer, redeem, no-partial-redeem, script attack, miner DD, RH-32, RH-64, and relevant functional tests; only `digidollar_rpc_estimate.py` failed due stale max-mint expectation.

**Wave 3 status:** complete. Two low-severity rounding bugs fixed (`DD-RH-003`, `DD-RH-004`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 4 — Timelock, ERR, DCA, Volatility Bypass

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1887 matching paths, including generated/build artifacts.

**Assignments launched:**
- Agent A — Reachability prober: redemption paths and protection state transitions, including normal timelocks, ERR release, DCA, volatility gates, and block connect/disconnect behavior.
- Agent B — Invariant and test-gap reviewer: tests for height/time edges, ERR burn requirements, DCA thresholds, volatility freeze/unfreeze/cooldown, and redemption timelock boundaries.
- Agent C — Boundary reviewer: functional replay/restart/reorg scenarios around protections plus RPC/Qt/wallet redemption/protection boundaries.

**Scope note:** DigiDollar/oracle/MuSig2 only; no generic DigiByte review.

**Status:** complete.

### DD-RH-005 — Nonzero-vout mint collateral bypassed redemption fee-input collateral guard

**Severity:** High

**Affected invariant:** A redemption cannot unlock collateral without the correct DD burn for that collateral position. Including another position's collateral as a fee input must be rejected even if that position's collateral output is not `vout[0]`.

**Reachability path:** `ValidateMintTransaction()` accepts a mint with ordinary non-P2TR DGB change before the collateral and exactly one positive P2TR collateral output at a later index. `ValidateCollateralReleaseAmount()` then inspects nonzero-value redemption inputs after input 0 as fee inputs. Before the fix, its `isCollateralOutput` helper only treated `vout[0]` of a DD mint as collateral, so a consensus-valid nonzero-vout collateral UTXO from mint B could be included while redeeming mint A. The validator subtracted mint B's collateral as a fee input, making the net release look valid while mint B's DD remained circulating without backing.

**Pre-fix affected code:** `src/digidollar/validation.cpp:1888`-`src/digidollar/validation.cpp:1928` assumed mint collateral was fixed at `vout[0]`.

**Failing regression evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_t2_06d_reordered_collateral_as_fee_input
```

Pre-fix result: failed with 2 failures. The test proved mint B with collateral at `vout[2]` was accepted by `ValidateMintTransaction()`, then showed `ValidateCollateralReleaseAmount()` accepted a redeem that included that collateral as a fee input and produced no `bad-redeem-collateral-as-fee-input` reject reason.

**Fix summary:** Updated the redemption fee-input collateral detector to classify the unique positive P2TR output of any DD mint as collateral, independent of output index. The helper now checks DD marker/type, validates the candidate output is positive P2TR, scans the source mint for exactly one positive P2TR collateral output, and rejects that output as `bad-redeem-collateral-as-fee-input`.

**Post-fix affected code:** `src/digidollar/validation.cpp:1888`-`src/digidollar/validation.cpp:1928`.

**Tests added/upgraded:** `src/test/digidollar_redteam_tests.cpp:5424` `redteam_t2_06d_reordered_collateral_as_fee_input`.

**Post-fix test evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_t2_06d_reordered_collateral_as_fee_input
./src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_t2_06b_fee_input_collateral_masquerade
./src/test/test_digibyte --run_test=digidollar_validation_tests
./src/test/test_digibyte --run_test=digidollar_redeem_tests
./src/test/test_digibyte --run_test=digidollar_rh07_redemption_attacks
./src/test/test_digibyte --run_test=digidollar_no_partial_redeem_tests
./src/test/test_digibyte --run_test=digidollar_err_tests
./src/test/test_digibyte --run_test=digidollar_err_attack_tests
```

Results: all post-fix commands passed. One attempted command with suite name `digidollar_rh07_redemption_attacks_tests` failed with "no test cases matching filter"; rerun with the live suite name `digidollar_rh07_redemption_attacks` passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-006 — `getprotectionstatus` false emergency on zero DD supply and stale volatility reporting

**Severity:** Low

**Affected invariant:** RPC/user-facing protection status must accurately reflect active DigiDollar protection gates and must not report emergency redemption or volatility restrictions that are not active in validation.

**Reachability path:** On an activated chain with no DD minted, `getprotectionstatus` computed `systemHealth = 0` and passed it directly to `IsSystemEmergency()`. The RPC reported ERR/emergency as active even though there were no DD liabilities and consensus mint blocking explicitly treats `totalDDSupply <= 0` as non-emergency. The same RPC hardcoded volatility fields to inactive/zero, while validation uses `VolatilityMonitor::ShouldFreezeMinting()` and `VolatilityMonitor::ShouldFreezeAll()` for live protection gates.

**Pre-fix affected code:** `src/rpc/digidollar.cpp:3449`-`src/rpc/digidollar.cpp:3491`.

**Failing regression evidence:**

```bash
python3 test/functional/digidollar_rpc_protection.py
```

Pre-fix result: failed with `AssertionError: Invalid ERR status: emergency` on a no-DD-supply activated chain.

**Fix summary:** `getdigidollarstats` and `getprotectionstatus` now require `totalDD > 0` before treating low system health as ERR/emergency. `getprotectionstatus` also reports valid status strings and reads live `VolatilityMonitor` state instead of hardcoded inactive values.

**Post-fix affected code:** `src/rpc/digidollar.cpp:431`, `src/rpc/digidollar.cpp:3459`, `src/rpc/digidollar.cpp:3472`-`src/rpc/digidollar.cpp:3540`.

**Tests added/upgraded:** `test/functional/digidollar_rpc_protection.py` now asserts that zero DD supply reports ERR inactive, `err.status="normal"`, `overall.status="secure"`, and no active ERR protection.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/digidollar_rpc_protection.py
python3 test/functional/digidollar_protection_status.py
./src/test/test_digibyte --run_test=digidollar_rpc_tests
./src/test/test_digibyte --run_test=digidollar_volatility_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

**Additional Wave 4 command matrix:**

```bash
./src/test/test_digibyte --run_test=digidollar_timelock_tests
./src/test/test_digibyte --run_test=digidollar_dca_tests
./src/test/test_digibyte --run_test=digidollar_rh16_reorg_attacks_tests
./src/test/test_digibyte --run_test=digidollar_rh34_multiblock_state_tests
python3 test/functional/digidollar_wallet_restore_redeem.py
python3 test/functional/digidollar_rpc_dca.py
python3 test/functional/digidollar_protection.py
python3 test/functional/digidollar_redeem.py
```

Results: all passed. `digidollar_protection.py` logged an expected help/argument error for an intentionally wrong `redeemdigidollar` call and a tolerated rapid-redemption helper limitation, then ended `Tests successful`.

**Rejected false positives in Wave 4:**
- Timelock bypass via ERR/emergency path: rejected. Active redemption construction and validation still require the normal full-vault path; no reachable early collateral release was proven.
- Restart/rescan losing redeemed state: rejected by existing wallet restore/redeem functional flow and Agent C review.
- Qt positions table showing received DD as redeemable collateral position: rejected by Agent C; no live path found where received DD transfers become collateral-redeemable positions.

**Theoretical / not yet reachable carried forward:**
- Add a hard transaction-level lock-height/CLTV regression for a mint committed to a later lock height and an early redeem `nLockTime`, unless an existing script-path test is proven to cover the exact production path.
- Add full `ValidateDigiDollarTransaction()` tests for volatility freeze mint/transfer/redeem behavior and cooldown boundaries.
- ERR production-path tests remain incomplete because ERR redemption behavior is not fully implemented; do not count missing ERR design as a bug without a reachable acceptance path.
- `getredemptioninfo` advertises `emergency` / `liquidation` redemption paths while the wallet builder currently uses the normal path and ERR validation remains incomplete. Carry to Wave 11 RPC schema review.
- Qt redeem flow can present a confirmation for a locked position before backend rejection. Carry to Wave 12 as a possible loss-of-time/user-error UX bug, not yet a consensus/wallet-loss issue.

**Agent A result summary:** no final text returned for Wave 4; no files edited by the agent.

**Agent B result summary:** confirmed broad helper coverage for lock intervals, DCA thresholds, ERR thresholds, and volatility state transitions. Identified missing hard transaction-level timelock, production volatility gate, cooldown, ERR, and oracle-staleness negative cases.

**Agent C result summary:** found the zero-liability ERR RPC bug fixed as `DD-RH-006`, the stale volatility RPC reporting fixed as part of `DD-RH-006`, and carry-forward RPC/Qt redemption schema/UX concerns. Its `digidollar_rpc_redemption.py` run failed on a stale fake position ID (`Position ... not found in wallet (-8)`), and `wallet_digidollar_restore.py` exited 0 while logging an internal harness signature error in `assert_greater_than`; both are test debt, not confirmed production bugs.

**Wave 4 status:** complete. Two bugs fixed (`DD-RH-005`, `DD-RH-006`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 5 — Script, OP_RETURN, Metadata, Address Edge Cases

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1887 matching paths, including generated/build artifacts.

**Assignments launched:**
- Agent A — Reachability prober: malformed script/template/OP_RETURN handling paths.
- Agent B — Invariant and test-gap reviewer: fuzz/regression coverage for parser and classifier edges.
- Agent C — Boundary reviewer: wallet/index/scan confusion and address display/routing bugs.

**Scope note:** DigiDollar/oracle/MuSig2 only; no generic DigiByte review.

### DD-RH-007 — Transfer OP_RETURN spoof could inflate later spends

**Severity:** Critical

**Affected invariant:** DD transfers cannot create DD value, and source amount extraction must bind to the exact metadata accepted for the creating transaction.

**Reachability path:** `ValidateTransferTransaction()` scanned for the first DD OP_RETURN whose embedded type was `2` and used it for current transfer conservation. A transaction could put an earlier DD OP_RETURN with embedded type `1` and a larger amount before the valid transfer OP_RETURN; current validation accepted the transfer because the later type-2 metadata matched the real output. Later, `ExtractDDAmountFromTxRef()` read the first DD OP_RETURN in the creating transaction and interpreted the output as the larger fake amount, allowing a follow-up transfer to spend 10000 cents as 50000 cents.

**Pre-fix affected code:** `src/digidollar/validation.cpp:248`-`src/digidollar/validation.cpp:309` and `src/digidollar/validation.cpp:1214`-`src/digidollar/validation.cpp:1299`.

**Failing regression evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_validation_tests/transfer_rejects_first_opreturn_amount_spoof
```

Pre-fix result: failed with two failures: the conflicting-metadata transfer was accepted, and the follow-up inflated spend was accepted.

**Fix summary:** Transfer validation now requires exactly one DD OP_RETURN, requires the embedded OP_RETURN type to be `TRANSFER`, and requires OP_RETURN amount count to equal DD output count. Source amount extraction now uses the transaction version as the authoritative type, rejects mismatched OP_RETURN types, rejects multiple DD OP_RETURNs, and scans the whole output list to fail closed on ambiguity.

**Post-fix affected code:** `src/digidollar/validation.cpp:236`-`src/digidollar/validation.cpp:309`, `src/digidollar/validation.cpp:1214`-`src/digidollar/validation.cpp:1299`.

**Tests added/upgraded:** `src/test/digidollar_validation_tests.cpp:3070` `transfer_rejects_first_opreturn_amount_spoof`.

**Post-fix test evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=digidollar_validation_tests/transfer_rejects_first_opreturn_amount_spoof
./src/test/test_digibyte --run_test=digidollar_validation_tests
./src/test/test_digibyte --run_test=digidollar_transfer_tests
./src/test/test_digibyte --run_test=digidollar_txindex_tests
./src/test/test_digibyte --run_test=digidollar_rh12_script_attacks_tests
./src/test/test_digibyte --run_test=rh60_mempool_dd_scriptnum_escape_tests
python3 test/functional/digidollar_transfer.py
./src/test/test_digibyte --run_test=digidollar_redteam_tests
./src/test/test_digibyte '--run_test=digidollar_*'
```

Results: all passed after the fix. The broad wildcard passed 1741 cases after updating stale expectations from earlier fixes.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-008 — DD address validators accepted corrupted or wrong-shape strings

**Severity:** Medium

**Affected invariant:** RPC/Qt/wallet address validation must not mark malformed DD addresses as valid and route users into send/import flows that later fail or mislead.

**Reachability path:** `CDigiDollarAddress::IsValidDigiDollarAddress()` checked visible prefix plus Base58Check decode, but did not verify decoded payload length/version. `validateddaddress` used only length/prefix checks and returned `isvalid=true` for corrupted addresses. Agent C's functional run logged that a corrupted address passed; Agent B generated a Base58Check-valid wrong-shape payload beginning with `DD` that the static helper accepted before this fix.

**Pre-fix affected code:** `src/base58.cpp:286`-`src/base58.cpp:309`, `src/rpc/digidollar.cpp:2286`-`src/rpc/digidollar.cpp:2298`.

**Fix summary:** The static helper now delegates to `CDigiDollarAddress(str).IsValid()`, which enforces checksum, 34-byte payload, and known DD/TD/RD version bytes. `validateddaddress` now uses the same decoder and reports invalid addresses consistently.

**Post-fix affected code:** `src/base58.cpp:286`-`src/base58.cpp:293`, `src/rpc/digidollar.cpp:2286`-`src/rpc/digidollar.cpp:2296`.

**Tests added/upgraded:**
- `src/test/digidollar_rh40_regression_tests.cpp:129` now rejects a Base58Check-valid wrong-shape `DD...` string.
- `test/functional/digidollar_rpc_addresses.py` now requires corrupted DD addresses to return `isvalid=false`.

**Post-fix test evidence:**

```bash
make -C src -j2 test/test_digibyte digibyted
./src/test/test_digibyte --run_test=digidollar_rh40_regression_tests/rh40_address_version_byte_mismatch
./src/test/test_digibyte --run_test=digidollar_rh40_regression_tests
python3 test/functional/digidollar_rpc_addresses.py
```

Results: all passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-009 — `importdigidollaraddress` false-success stub

**Severity:** Medium

**Affected invariant:** RPC surfaces must not claim a DD address was imported, watched, or rescanned when no wallet state was changed.

**Reachability path:** `importdigidollaraddress` accepted addresses using length/prefix checks, returned `success=true`, and returned fake `transactions_found=3` when `rescan=true`. It did not decode the address, add watch-only wallet state, rescan, or affect `listdigidollaraddresses`.

**Pre-fix affected code:** `src/rpc/digidollar.cpp:2484`-`src/rpc/digidollar.cpp:2509`.

**Fix summary:** The RPC now uses `CDigiDollarAddress` for validation, returns `success=false`, `rescan_performed=false`, `transactions_found=0`, and a clear warning that DD watch-only import is not implemented. Full watch-only storage/rescan design is recorded as `ARCH-RH-003`.

**Post-fix affected code:** `src/rpc/digidollar.cpp:2484`-`src/rpc/digidollar.cpp:2506`.

**Tests added/upgraded:** `test/functional/digidollar_rpc_addresses.py` now expects valid imports to return explicit unsupported status and invalid imports to raise.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/digidollar_rpc_addresses.py
./src/test/test_digibyte --run_test=digidollar_rpc_tests
```

Results: all passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

**Additional Wave 5 command matrix:**

```bash
git diff --check
./src/test/test_digibyte '--run_test=digidollar_*'
```

Results: no whitespace errors; broad DigiDollar wildcard passed 1741 test cases.

**Rejected false positives / narrowed claims:**
- OP_RETURN extra transfer amount pushes are no longer counted as a separate unproven risk; they are covered by `DD-RH-007` amount-count enforcement.
- Normal RPC send/change accounting did not reproduce amount drift in Agent C's functional probe; mint/send/rescan balances stayed correct.
- Watch-only DD balance contamination remains defended in wallet scan paths by spendable-only checks, but watch-only import support itself remains unimplemented.

**Theoretical / not yet reachable carried forward:**
- `TR-RH-005`: `listdigidollaraddresses` hides generated zero-balance addresses and cannot show watch-only state.
- `TR-RH-006`: valid DD/TD/RD address versions are accepted independent of active chain; cross-network routing policy needs a decision.
- Oracle v0x03 deserializer exact-size malformed-payload tests are thin; carry to oracle/MuSig2 waves 13-17.
- RH62/RH18 parser tests still mirror old `std::stod` behavior rather than calling production RPC amount parsing; carry as test debt.

**ARCHITECTURAL_REVIEW_REQUIRED:** `ARCH-RH-003` opened for real DD watch-only address storage/rescan/listing semantics.

**Agent A result summary:** no final text returned for Wave 5; no files edited by the agent.

**Agent B result summary:** confirmed parser coverage gaps around transfer OP_RETURN arity/multiple metadata, stale RPC amount-parser tests, wrong-shape DD address static validation, and oracle payload malformed-size tests. The transfer metadata gap was fixed as `DD-RH-007`; the address static validation gap was fixed as `DD-RH-008`.

**Agent C result summary:** confirmed the `validateddaddress` false-valid behavior and the `importdigidollaraddress` false-success stub, both fixed as `DD-RH-008` and `DD-RH-009`. It also identified `listdigidollaraddresses` zero-balance/watch-only listing limitations for later wallet/RPC design review. Its `digidollar_watchonly_rescan.py` run failed due the known test harness `assert_greater_than()` signature bug, not a confirmed production bug.

**Wave 5 status:** complete. Three bugs fixed (`DD-RH-007`, `DD-RH-008`, `DD-RH-009`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 6 — Activation and Consensus-Disagreement Risks

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1887 matching paths, including generated/build artifacts.

**Assignments launched:**
- Agent A — Reachability prober: validation/chainparams/deployment gates and shared hooks.
- Agent B — Invariant and test-gap reviewer: enabled/disabled boundary and mempool/validation mismatch tests.
- Agent C — Boundary reviewer: multi-node functional activation/reorg scenarios.

**Scope note:** DigiDollar/oracle/MuSig2 only; no generic DigiByte review.

### DD-RH-010 — Context-free `CheckBlock` enforced oracle rules by static height

**Severity:** Medium

**Affected invariant:** Activation/deployment gates must not create consensus or relay disagreement by enforcing DigiDollar/oracle rules before BIP9 activation.

**Reachability path:** `CheckBlock()` ran `OracleDataValidator::ValidateBlockOracleData(block, nullptr, ...)` and `CheckPhase3OracleBundleVersion(block, nullptr, ...)`. With `pindex_prev == nullptr`, both helpers used BIP34 coinbase height and `nDDActivationHeight` rather than BIP9 state. A block at or above the static height could be rejected for oracle version/phase rules even if `DEPLOYMENT_DIGIDOLLAR` was not ACTIVE.

**Pre-fix affected code:** `src/validation.cpp:4392`-`src/validation.cpp:4400`, `src/oracle/bundle_manager.cpp:2297`-`src/oracle/bundle_manager.cpp:2305`, `src/validation.cpp:134`-`src/validation.cpp:146`.

**Failing regression evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=rh51_checkphase3_v1_split_tests/rh51_checkblock_defers_oracle_phase_rules_to_context
```

Pre-fix result: failed because `CheckBlock` rejected a v1 oracle bundle at height 650 with `bad-oracle-phase2` despite having no BIP9 context.

**Fix summary:** `CheckBlock()` no longer runs context-dependent oracle validation with a null block index. `ConnectBlock()` now runs full `OracleDataValidator::ValidateBlockOracleData(block, pindex->pprev, ...)` before the existing phase-version helper, so active blocks still get full oracle validation with the correct BIP9 context.

**Post-fix affected code:** `src/validation.cpp:2630`-`src/validation.cpp:2635`, `src/validation.cpp:4398`-`src/validation.cpp:4401`.

**Tests added/upgraded:**
- `src/test/rh51_checkphase3_v1_split_tests.cpp:152` now asserts `CheckBlock` defers oracle phase rules and contextual validation still rejects the same v1 bundle.
- `src/test/rh52_bip34_scriptnum_escape_tests.cpp:189` and `src/test/rh52_bip34_scriptnum_escape_tests.cpp:241` were updated because `CheckBlock` no longer runs the oracle BIP34 parser path.

**Post-fix test evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=rh51_checkphase3_v1_split_tests/rh51_checkblock_defers_oracle_phase_rules_to_context
./src/test/test_digibyte --run_test=rh51_checkphase3_v1_split_tests
./src/test/test_digibyte --run_test=rh52_bip34_scriptnum_escape_tests
./src/test/test_digibyte --run_test=rh65_mainnet_testnet_validator_parity_tests
./src/test/test_digibyte --run_test=digidollar_activation_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-011 — DD mempool transaction survived reorg below activation

**Severity:** High

**Affected invariant:** Reorgs and deployment rollback must not leave a DigiDollar transaction relayable or mineable when fresh mempool admission would reject it as inactive.

**Reachability path:** A DD mint accepted while `DEPLOYMENT_DIGIDOLLAR` was ACTIVE could stay in the mempool after invalidating back below activation. `MaybeUpdateMempoolForReorg()` filtered existing mempool entries for finality, sequence locks, and coinbase maturity, but did not re-apply the DD activation gate.

**Pre-fix affected code:** `src/validation.cpp:381`-`src/validation.cpp:436`.

**Failing regression evidence:**

```bash
python3 test/functional/digidollar_activation_boundary.py
```

Pre-fix result after adding the regression: failed with `DD tx <txid> must be removed from mempool after reorg below activation`; `getdigidollardeploymentinfo` reported `enabled=false` and the stale DD tx remained in `getrawmempool()`.

**Fix summary:** The reorg mempool filter now removes any transaction with a DigiDollar marker when `DigiDollar::IsDigiDollarEnabled(m_chain.Tip(), m_chainman)` is false for the new tip.

**Post-fix affected code:** `src/validation.cpp:392`-`src/validation.cpp:394`.

**Tests added/upgraded:** `test/functional/digidollar_activation_boundary.py:29` now starts with `-txindex=1`, and `test/functional/digidollar_activation_boundary.py:177` creates a post-activation DD mempool tx, invalidates back to `locked_in`, and asserts the DD tx is purged.

**Post-fix test evidence:**

```bash
make -C src -j2 test/test_digibyte digibyted
python3 test/functional/digidollar_activation_boundary.py
python3 test/functional/digidollar_activation.py
python3 test/functional/digidollar_rpc_gating.py
./src/test/test_digibyte --run_test=digidollar_activation_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

**Additional Wave 6 command matrix:**

```bash
./src/test/test_digibyte --run_test=rh51_checkphase3_v1_split_tests
./src/test/test_digibyte --run_test=rh52_bip34_scriptnum_escape_tests
./src/test/test_digibyte --run_test=rh65_mainnet_testnet_validator_parity_tests
./src/test/test_digibyte --run_test=digidollar_activation_tests
python3 test/functional/digidollar_activation.py
python3 test/functional/digidollar_rpc_gating.py
```

Results: all passed.

**Rejected false positives / narrowed claims:**
- DD mempool admission and block connection use the BIP9 activation predicate for fresh DD transactions; no fresh pre-activation DD acceptance path was found.
- Script flag activation for DigiDollar opcodes follows `DeploymentActiveAt`; no mismatch with the DD transaction gate was proven.
- Coinbase DD minting through skipped DD validation is blocked by the explicit coinbase DD marker check.

**Theoretical / not yet reachable carried forward:**
- `TestBlockValidity` still does not extract `blockOraclePrice` when `fJustCheck=true`; this may reject otherwise valid miner templates if the local oracle cache differs from the block bundle. Needs a direct reproducer before counting as a bug.
- Reorg/deactivation mempool cleanup now covers existing DD mempool entries, but a multi-node relay/compact-block test would give stronger coverage of propagation behavior.
- Phase 3 activation height is currently `0` on all configured networks; future nonzero Phase 3 deployment would need explicit production active-height checks in `ValidatePhaseThreeBundle`.

**ARCHITECTURAL_REVIEW_REQUIRED:**
- `ARCH-RH-001` remains open: post-activation blocks with missing/unparseable oracle bundles are still allowed, and DD block validation may then fall back to local P2P/mock oracle price. Enforcing mandatory valid oracle bundles for DD-bearing blocks is a protocol/liveness decision and was not implemented without Jared's approval.
- Oracle P2P activation still uses static `nOracleActivationHeight` rather than DD BIP9 state. Agent C observed DD active at height 432 with no `getoracles` discovery until height 650 under `-digidollaractivationheight=200`. This is a deployment design mismatch, not fixed in Wave 6.
- Qt activation UI latches `m_activated` and stops polling; a reorg below activation can leave DD tabs visible while backend rejects operations. Carry to Wave 12.

**Agent A result summary:** confirmed the context-free `CheckBlock` static-height oracle validation bug fixed as `DD-RH-010`; rejected DD fresh mempool/block activation mismatch, script flag mismatch, and coinbase DD mint bypass.

**Agent B result summary:** confirmed oracle validation was not fully contextual/BIP9-gated in production block validation (fixed as `DD-RH-010`), identified the no-valid-bundle local price fallback as architecture review, and flagged missing raw tx/block activation negatives.

**Agent C result summary:** confirmed stale DD mempool entries after reorg below activation (fixed as `DD-RH-011`), confirmed RPC gating passes, identified oracle P2P static-height activation and Qt latched activation UI for later waves.

**Wave 6 status:** complete. Two bugs fixed (`DD-RH-010`, `DD-RH-011`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 7 — Reorg, Replay, Rollback, Cache Handling

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1888 matching paths after adding Wave 7 wallet regressions.

**Assignments launched:**
- Agent A — Reachability prober: connect/disconnect state mutation and cached metrics.
- Agent B — Invariant and test-gap reviewer: duplicate/reversed/missing state updates.
- Agent C — Boundary reviewer: restart/reindex/rescan/reorg functional flows.

**Scope note:** DigiDollar/oracle/MuSig2 only; no generic DigiByte review.

### DD-RH-012 — Reorged-out confirmed redeem left wallet position redeemed

**Severity:** Medium

**Affected invariant:** Reorgs must not corrupt DigiDollar wallet collateral/position state. A redemption that is no longer confirmed must not permanently hide or disable the original live position.

**Reachability path:** A confirmed `redeemdigidollar` marks a wallet position inactive. `CWallet::blockDisconnected()` re-locked collateral outpoints, but did not reactivate an inactive DD position when the redeem block was disconnected. After invalidating the redeem block, `listdigidollarpositions(false)` still reported the position inactive/redeemed even though the active chain no longer confirmed the redeem.

**Pre-fix affected code:** `src/wallet/wallet.cpp:1581`-`src/wallet/wallet.cpp:1614`.

**Failing regression evidence:**

```bash
python3 test/functional/wallet_digidollar_reorg.py
```

Pre-fix result: failed after invalidating the redeem block with `not(False == True)` for the position active-state assertion.

**Fix summary:** `blockDisconnected()` now scans disconnected transaction inputs for tracked DD collateral, reactivates the inactive position via `UpdatePositionStatus()`, and re-locks the collateral outpoint when a confirmed redeem is reorged out.

**Post-fix affected code:** `src/wallet/wallet.cpp:1593`-`src/wallet/wallet.cpp:1614`.

**Tests added/upgraded:** `test/functional/wallet_digidollar_reorg.py:1` creates a mint, confirms a redeem, invalidates the redeem block, and asserts the position becomes active again while the reorged redeem may remain in mempool.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/wallet_digidollar_reorg.py
python3 test/functional/digidollar_wallet_restore_redeem.py
python3 test/functional/wallet_digidollar_rescan.py
python3 test/functional/digidollar_redeem.py
./src/test/test_digibyte --run_test=digidollar_wallet_tests
./src/test/test_digibyte --run_test=digidollar_rh16_reorg_attacks_tests
./src/test/test_digibyte --run_test=digidollar_rh34_multiblock_state_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-013 — `-reindex` marked live confirmed mint position redeemed

**Severity:** High

**Affected invariant:** Wallet replay/reindex must reconstruct active DigiDollar positions from the active chain without disabling collateral that has not been redeemed.

**Reachability path:** After minting and confirming DigiDollar, restarting with `-reindex=1` left `getdigidollarbalance` showing the confirmed DD but `listdigidollarpositions(false)` reported the position as inactive/redeemed. During replay, mint rescan and post-rescan validation used current/incomplete wallet or chain state to infer that the mint collateral was absent/spent.

**Pre-fix affected code:** `src/wallet/digidollarwallet.cpp:2241`-`src/wallet/digidollarwallet.cpp:2258`, `src/wallet/digidollarwallet.cpp:3855`-`src/wallet/digidollarwallet.cpp:3878`.

**Failing regression evidence:**

```bash
python3 test/functional/wallet_digidollar_reindex.py
```

Pre-fix result: failed after `-reindex=1` with `not(False == True)` for the live confirmed mint position active-state assertion.

**Fix summary:** Mint replay no longer treats `m_wallet->IsSpent(COutPoint(mint, 0))` as proof that collateral is redeemed while processing the mint itself; real redeems are handled when their redeem transaction is replayed. `ValidatePositionStates()` now skips destructive UTXO-set correction while the chain interface is not ready to broadcast, avoiding false inactive marks while block files are still loading during reindex/IBD.

**Post-fix affected code:** `src/wallet/digidollarwallet.cpp:2253`-`src/wallet/digidollarwallet.cpp:2257`, `src/wallet/digidollarwallet.cpp:3855`-`src/wallet/digidollarwallet.cpp:3858`.

**Tests added/upgraded:** `test/functional/wallet_digidollar_reindex.py:1` mints and confirms DD, restarts with `-reindex=1`, then asserts confirmed balance and active position state are preserved.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/wallet_digidollar_reindex.py
```

Results: passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-014 — Orphaned non-mempool mint counted as pending DD balance

**Severity:** Medium

**Affected invariant:** Wallet/accounting surfaces must not report DD balance for transactions that are neither confirmed nor pending in mempool.

**Reachability path:** After confirming a mint and invalidating its block, the mint transaction was not in `getrawmempool()`, but `getdigidollarbalance` still reported `unconfirmed=100000` and `total=100000`. `GetPendingDDBalance()` counted any DD UTXO with depth `< 1`, including inactive wallet transactions that were neither confirmed nor mempool-pending.

**Pre-fix affected code:** `src/wallet/digidollarwallet.cpp:3375`-`src/wallet/digidollarwallet.cpp:3386`.

**Failing regression evidence:**

```bash
python3 test/functional/wallet_digidollar_mint_reorg.py
```

Pre-fix result: failed after invalidating the mint block with `not(100000 == 0)` for the unconfirmed balance assertion.

**Fix summary:** `GetPendingDDBalance()` now counts a tracked DD UTXO as pending only when its wallet transaction has depth exactly `0` and `CWalletTx::InMempool()` is true. Negative-depth conflicts and inactive non-mempool wallet transactions no longer contribute to DD totals.

**Post-fix affected code:** `src/wallet/digidollarwallet.cpp:3380`-`src/wallet/digidollarwallet.cpp:3386`.

**Tests added/upgraded:** `test/functional/wallet_digidollar_mint_reorg.py:1` confirms a mint, invalidates its block, asserts the mint is absent from mempool, and asserts confirmed/unconfirmed/total DD balances are all zero.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/wallet_digidollar_mint_reorg.py
```

Results: passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

**Additional Wave 7 command matrix:**

```bash
./src/test/test_digibyte --run_test=digidollar_rh16_reorg_attacks_tests
./src/test/test_digibyte --run_test=digidollar_rh34_multiblock_state_tests
./src/test/test_digibyte --run_test=digidollar_rh25_serialization_cache_tests
./src/test/test_digibyte --run_test=digidollar_utxo_lifecycle_tests
python3 test/functional/digidollar_persistence.py
python3 test/functional/wallet_digidollar_persistence_restart.py
python3 test/functional/digidollar_wallet_restore_redeem.py
python3 test/functional/wallet_digidollar_rescan.py
python3 test/functional/wallet_digidollar_restore.py
python3 test/functional/digidollar_oracle_consistency.py
```

Results: the C++ suites and wallet restore/rescan flows passed. `digidollar_persistence.py` and `wallet_digidollar_persistence_restart.py` pass with known skipped/weak coverage. `wallet_digidollar_restore.py` exits success but logs the known `assert_greater_than()` signature bug, so it is not strong coverage. `digidollar_oracle_consistency.py` fails due a test bug using `assert_equal(oracles_prices[oid], price, "message")` as a three-way equality check.

**Rejected false positives / narrowed claims:**
- Plain wallet restart without reindex did not reproduce stale position state; live confirmed mint stayed active.
- Reorged redeem may remain in mempool after block invalidation; spendable balance can remain zero while that mempool spend is live. `DD-RH-012` is specifically about confirmed-chain position state being reversed.

**Theoretical / not yet reachable carried forward:**
- Oracle cache disconnect still removes cached price data by checking only coinbase `vout[1]`, while connect scans all coinbase outputs. Normal witness-commitment layouts accidentally trigger removal, but a custom valid coinbase with spendable `vout[1]` and oracle output at `vout[2]` needs a direct reproducer before it can be counted.
- Existing SystemHealth direct-call tests do not exercise full `ConnectBlock`/`DisconnectBlock` state mutation; strengthen in later regression waves if more health-cache bugs appear.

**ARCHITECTURAL_REVIEW_REQUIRED:** none newly opened in Wave 7.

**Agent A result summary:** no final text returned for Wave 7.

**Agent B result summary:** confirmed missing wallet DD state reversal on block disconnect, fixed as `DD-RH-012`; identified the oracle cache output-index asymmetry as theoretical pending direct reproduction; noted weak reorg/cache tests.

**Agent C result summary:** confirmed `-reindex` live-mint state corruption, fixed as `DD-RH-013`; confirmed invalidated-mint stale pending DD balance, fixed as `DD-RH-014`; rejected plain restart as a reproducer.

**Wave 7 status:** complete. Three bugs fixed (`DD-RH-012`, `DD-RH-013`, `DD-RH-014`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 8 — Mempool Relay, Conflict, Replacement

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1890 matching paths after adding Wave 8 wallet/mempool regression coverage.

**Assignments launched:**
- Agent A — Reachability prober: mempool admission and conflict handling for DD/oracle txs.
- Agent B — Invariant and test-gap reviewer: negative tests for replacement/double-spend/malformed conflict cases.
- Agent C — Boundary reviewer: multi-node mempool functional scenarios.

**Scope note:** DigiDollar/oracle/MuSig2 only; no generic DigiByte review.

### DD-RH-015 — Pending redeem lost from mempool left wallet position inactive

**Severity:** Medium

**Affected invariant:** Wallet/mempool lifecycle must not permanently disable collateral positions when an unconfirmed redeem is no longer pending or confirmed.

**Reachability path:** `RedeemDigiDollar()` marks a position inactive immediately after creating/broadcasting a redeem transaction. If the node restarts without mempool persistence and wallet rebroadcast disabled, the redeem is not in mempool and not confirmed, but the position remains inactive/redeemed. The active chain still contains the collateral UTXO, so the wallet view is stale and the user cannot naturally retry from the DD position list.

**Pre-fix affected code:** `src/wallet/digidollarwallet.cpp:4685`-`src/wallet/digidollarwallet.cpp:4698`, `src/wallet/digidollarwallet.cpp:3838`-`src/wallet/digidollarwallet.cpp:3898`.

**Failing regression evidence:**

```bash
python3 test/functional/wallet_digidollar_pending_redeem_restart.py
```

Pre-fix result: failed after restart with `not(False == True)` for the position active-state assertion; `redeem_txid` was absent from `getrawmempool()`.

**Fix summary:** `ValidatePositionStates()` now reconciles real collateral positions bidirectionally against the chain/mempool UTXO view: missing collateral marks active positions inactive, and present collateral reactivates inactive positions. It skips DD change compatibility entries (`dgb_collateral == 0`) and still skips destructive correction while chainstate is not ready.

**Post-fix affected code:** `src/wallet/digidollarwallet.cpp:3840`-`src/wallet/digidollarwallet.cpp:3897`.

**Tests added/upgraded:** `test/functional/wallet_digidollar_pending_redeem_restart.py:1` confirms a mint, creates a pending redeem, restarts with `-persistmempool=0 -walletbroadcast=0`, asserts the redeem is absent from mempool, and asserts the collateral position is active again.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/wallet_digidollar_pending_redeem_restart.py
python3 test/functional/wallet_digidollar_reorg.py
python3 test/functional/wallet_digidollar_reindex.py
python3 test/functional/wallet_digidollar_mint_reorg.py
./src/test/test_digibyte --run_test=digidollar_wallet_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

**Additional Wave 8 command matrix:**

```bash
./src/test/test_digibyte --run_test=digidollar_rh17_mempool_attacks_tests
./src/test/test_digibyte --run_test=digidollar_rh33_mempool_relay_tests
./src/test/test_digibyte --run_test=rh60_mempool_dd_scriptnum_escape_tests
python3 test/functional/digidollar_network_relay.py
```

Results: all passed. `digidollar_network_relay.py` covered multi-hop relay, star topology relay, mempool consistency, and Dandelion mode.

**Rejected false positives / narrowed claims:**
- RH17/RH33 invalid-DD-type DoS claims are stale in current code: `validation.cpp:781` rejects `DD_TX_NONE` before expensive oracle/block-DB setup.
- RH60's original unhandled `CScriptNum` mempool exception is already blocked by the dispatcher catch at `src/digidollar/validation.cpp:2100`-`src/digidollar/validation.cpp:2118`; direct regression tests pass.
- DD token mempool chains are blocked by confirmed-only DD input policy at `src/digidollar/validation.cpp:1332`-`src/digidollar/validation.cpp:1339`.
- RBF replacement of DD mints remains a policy/design consideration, but wallet-built DD mint inputs use final/default sequences and redeem uses `0xFFFFFFFE`, which does not opt in to BIP125 RBF.

**Theoretical / not yet reachable carried forward:**
- ATMP currently runs DD validation before later mempool conflict checks (`src/validation.cpp:775` before `src/validation.cpp:859`). A malformed DD tx spending an already-conflicting input may still exercise DD parser/oracle setup before conflict rejection. Needs a production `testmempoolaccept`/`sendrawtransaction` reproducer to count as resource-exhaustion.
- Functional relay coverage remains mostly positive-path and uses `sync_mempools`; add hard negative multi-node conflict/replacement tests in a future regression wave.
- Oracle P2P duplicate/malformed functional tests are still pass-through/stale; carry to Wave 15.

**ARCHITECTURAL_REVIEW_REQUIRED:** none newly opened in Wave 8.

**Agent A result summary:** no final text returned for Wave 8.

**Agent B result summary:** identified missing production mempool regressions for DD double-spend/replacement, stale direct-only RH60 coverage, positive-path functional relay coverage, and stale oracle P2P negative tests. Rejected stale invalid-type DoS and unconfirmed-DD-chain claims because current code blocks them.

**Agent C result summary:** not returned before Wave 8 synthesis; Wave 8 proceeded with local multi-node relay evidence and the confirmed `DD-RH-015` fix.

**Wave 8 status:** complete. One bug fixed (`DD-RH-015`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 9 — Wallet Restore, Rescan, Persistence, Backup

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1891 matching paths.

**Assignments launched:**
- Agent A — Reachability prober: wallet DB/persistence and restore paths.
- Agent B — Invariant and test-gap reviewer: lost/miscounted positions after rescan/reload.
- Agent C — Boundary reviewer: encrypted/watch-only/backup restore flows.

**Scope note:** DigiDollar/oracle wallet/RPC/restore/encryption/watch-only surfaces only; no generic DigiByte review.

### DD-RH-016 — Locked encrypted `getdigidollaraddress` exposed an internal descriptor failure

**Severity:** Low

**Affected invariant:** RPC/wallet surfaces must reject dangerous private-key flows cleanly and must not leave users with partially created DD receive state.

**Reachability path:** In an encrypted, locked wallet, `getdigidollaraddress` attempted to derive/store a DD receive key and import a Taproot descriptor without the wallet passphrase. The call failed with the low-level `AddDescriptorKey: writing descriptor private key failed (-1)` instead of the standard wallet unlock error, after entering DD key/address creation code.

**Pre-fix affected code:** `src/rpc/digidollar.cpp:2138`-`src/rpc/digidollar.cpp:2224` lacked the unlock preflight used by mint/send/redeem.

**Failing regression evidence:**

```bash
python3 test/functional/digidollar_encrypted_wallet.py
```

Pre-fix result after adding the regression: failed in `test_get_dd_address_locked_wallet` with unexpected RPC code `-1` and message `AddDescriptorKey: writing descriptor private key failed`, where `-13` was expected.

**Fix summary:** `getdigidollaraddress` now calls `wallet::EnsureWalletIsUnlocked(*pwallet)` before key derivation/import and rejects wallets with disabled private keys before creating DD receive state.

**Post-fix affected code:** `src/rpc/digidollar.cpp:2141`-`src/rpc/digidollar.cpp:2143`.

**Tests added/upgraded:** `test/functional/digidollar_encrypted_wallet.py:99` asserts locked encrypted `getdigidollaraddress` returns wallet unlock error `-13`.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/digidollar_encrypted_wallet.py
python3 test/functional/wallet_digidollar_encryption.py
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-017 — Watch-only DD descriptor wallets showed unspendable DD as default spendable balance

**Severity:** Medium

**Affected invariant:** Wallet/RPC surfaces must not mislead users into treating watch-only or non-spendable DD as spendable funds.

**Reachability path:** A watch-only descriptor wallet (`disable_private_keys=true`) imported public descriptors from a DD wallet and rescanned. Before the fix, `getdigidollarbalance()` returned the imported DD as default confirmed/total balance even though `include_watchonly` defaulted to false and `senddigidollar` later failed because the wallet had no spending key. `listdigidollarpositions()` also showed positions without spendability/watch-only markers.

**Pre-fix affected code:** `src/rpc/digidollar.cpp:2576`-`src/rpc/digidollar.cpp:2604` parsed only address/minconf and ignored `include_watchonly`; `src/rpc/digidollar.cpp:1933`-`src/rpc/digidollar.cpp:2075` returned positions without watch-only/spendable state; DD write RPCs did not explicitly reject disabled-private-key wallets.

**Failing regression evidence:**

```bash
python3 test/functional/wallet_digidollar_descriptors.py
```

Pre-fix result after strengthening the watch-only test: failed with `not(20000 == 0)` because the watch-only wallet's default DD balance included unspendable imported DD.

**Fix summary:** `getdigidollarbalance` now honors `include_watchonly` for disabled-private-key wallets: default balance is zero, and explicit `include_watchonly=true` exposes the monitoring balance. DD write/address-creation RPCs now reject disabled-private-key wallets with `RPC_WALLET_ERROR`. `listdigidollarpositions` now reports `spendable` and `iswatchonly`, and disables `can_redeem` for disabled-private-key wallets.

**Post-fix affected code:**
- `src/rpc/digidollar.cpp:857`-`src/rpc/digidollar.cpp:860`, `src/rpc/digidollar.cpp:1305`-`src/rpc/digidollar.cpp:1308`, `src/rpc/digidollar.cpp:1458`-`src/rpc/digidollar.cpp:1460`, `src/rpc/digidollar.cpp:1600`-`src/rpc/digidollar.cpp:1602`, `src/rpc/digidollar.cpp:2141`-`src/rpc/digidollar.cpp:2143`
- `src/rpc/digidollar.cpp:1969`-`src/rpc/digidollar.cpp:1971`, `src/rpc/digidollar.cpp:2015`-`src/rpc/digidollar.cpp:2019`, `src/rpc/digidollar.cpp:2067`-`src/rpc/digidollar.cpp:2070`
- `src/rpc/digidollar.cpp:2596`-`src/rpc/digidollar.cpp:2628`

**Tests added/upgraded:** `test/functional/wallet_digidollar_descriptors.py:415`-`test/functional/wallet_digidollar_descriptors.py:452` now asserts default watch-only DD balance is zero, explicit include-watchonly exposes monitoring balance, DD send fails with disabled-private-key error, and watch-only positions are marked non-spendable.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/wallet_digidollar_descriptors.py
./src/test/test_digibyte --run_test=digidollar_rpc_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

**Additional Wave 9 command matrix:**

```bash
python3 test/functional/wallet_digidollar_backup.py
python3 test/functional/digidollar_wallet_restore_redeem.py
./src/test/test_digibyte --run_test=digidollar_wallet_tests
```

Results: all passed.

**Rejected false positives / narrowed claims:**
- Encrypted wallet mint/send/redeem write paths mostly already required unlock; the reachable gap was specifically DD address generation.
- Plain active-position backup/restore and fully redeemed descriptor restore passed current tests after the Wave 7/8 wallet reconciliation fixes.
- Watch-only DD spend did not bypass signing or consensus; it failed later due missing keys. The confirmed bug was misleading default accounting/UI-facing RPC state, not unauthorized spend.

**Theoretical / not yet reachable carried forward:**
- Mixed private-key-enabled wallets with directly imported watch-only DD descriptors may still need per-output DD ownership classification. The confirmed reproducer used disabled-private-key descriptor wallets; `importdigidollaraddress` remains unsupported after `DD-RH-009`.
- Confirmed-redeem followed by simple restart is still a valuable coverage addition, but existing full redeem descriptor restore and pending-redeem restart paths passed.
- Stale functional tests `digidollar_watchonly_rescan.py` and `wallet_digidollar_restore.py` still have `assert_greater_than()` signature issues or false-green exception handling; preserve for Wave 19 test cleanup unless tied to a new reachable bug.

**ARCHITECTURAL_REVIEW_REQUIRED:** none newly opened in Wave 9.

**Agent A result summary:** no final text returned for Wave 9 before synthesis.

**Agent B result summary:** identified missing coverage for confirmed redeem restart, stale `ddutxo` persistence cleanup, backup/restore of spent positions, and stale persistence tests. No new confirmed production bug.

**Agent C result summary:** confirmed watch-only DD default balance/spendability misreporting (`DD-RH-017`) and locked encrypted DD address generation internal failure (`DD-RH-016`); rejected broad encrypted-wallet unlock failures because mint/send/redeem/oracle keygen mostly enforced unlock.

**Wave 9 status:** complete. Two bugs fixed (`DD-RH-016`, `DD-RH-017`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 10 — Wallet Key Handling and Accounting

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1891 matching paths at Wave 10 start, 1894 after adding three Wave 10 functional regressions.

**Assignments launched:**
- Agent A — Reachability prober: key ownership, spendability, watch-only, locked-wallet paths.
- Agent B — Invariant and test-gap reviewer: wallet DD accounting invariants.
- Agent C — Boundary reviewer: RPC/Qt wallet state display and privacy checks.

**Scope note:** DigiDollar/oracle wallet/RPC/Qt/key/accounting paths only; no generic DigiByte review.

### DD-RH-018 — Restored/encrypted DD output signing keys were not recovered for redemption

**Severity:** High

**Affected invariant:** Wallet restore, encryption, rescans, and DD fungibility must not strand collateral or make spendable DD unusable for redemption.

**Reachability paths:**
- A private descriptor restore reconstructed an active DD position and DD UTXO from chain data, but did not repopulate `dd_owner_keys[mint_txid]`. `redeemdigidollar` then failed with `Owner key not found for position (-4)` even though the restored wallet had the private descriptor key and displayed the position as spendable.
- An encrypted wallet receiving DD at a DD address stored the address key in `dd_crypted_address_keys`, but `AddReceivedDDUTXO()` only consulted plaintext `dd_address_keys`. If that received DD was later selected to redeem the wallet's collateral position, `SignRedemptionTransaction()` only tried `GetOwnerKey(outpoint.hash, ...)` and failed for the received transfer txid.

**Pre-fix affected code:**
- `src/wallet/digidollarwallet.cpp:2267`-`src/wallet/digidollarwallet.cpp:2295` restored positions without restoring the mint owner-key mapping.
- `src/wallet/digidollarwallet.cpp:7106`-`src/wallet/digidollarwallet.cpp:7115` now shows the fixed received-DD key path; previously this block read only `dd_address_keys`.
- `src/wallet/digidollarwallet.cpp:6365`-`src/wallet/digidollarwallet.cpp:6381` now shows the redemption fallback; previously it failed immediately when `GetOwnerKey(outpoint.hash, ...)` missed.

**Failing regression evidence:**

```bash
python3 test/functional/wallet_digidollar_active_restore_redeem.py
```

Pre-fix result: failed at `redeemdigidollar(position_id, amount)` with `Owner key not found for position (-4)`.

**Fix summary:** Added `DigiDollarWallet::GetDDOutputSpendingKey()` to recover/cache the internal Taproot signing key for spendable DD P2TR outputs from encrypted DD address-key storage or descriptor wallet metadata. Mint rescan promotes the recovered key into `dd_owner_keys[mint_txid]`; received-DD accounting promotes it into `dd_owner_keys[transfer_txid]`; redemption signing can recover it directly from the selected DD prevout before failing.

**Post-fix affected code:**
- `src/wallet/digidollarwallet.h:279`-`src/wallet/digidollarwallet.h:287`
- `src/wallet/digidollarwallet.cpp:978`-`src/wallet/digidollarwallet.cpp:1068`
- `src/wallet/digidollarwallet.cpp:2267`-`src/wallet/digidollarwallet.cpp:2287`
- `src/wallet/digidollarwallet.cpp:6365`-`src/wallet/digidollarwallet.cpp:6381`
- `src/wallet/digidollarwallet.cpp:7106`-`src/wallet/digidollarwallet.cpp:7115`

**Tests added/upgraded:**
- `test/functional/wallet_digidollar_active_restore_redeem.py:1` restores private descriptors into a blank descriptor wallet, verifies the active DD position, advances to unlock height, and redeems from the restored wallet.
- `test/functional/wallet_digidollar_encrypted_received_redeem.py:1` encrypts a wallet, mints a position, sends the original DD away, receives replacement DD back, and redeems the original collateral using the received DD.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/wallet_digidollar_active_restore_redeem.py
python3 test/functional/wallet_digidollar_encrypted_received_redeem.py
python3 test/functional/digidollar_encrypted_wallet.py
python3 test/functional/wallet_digidollar_encryption.py
python3 test/functional/digidollar_wallet_restore_redeem.py
python3 test/functional/wallet_digidollar_descriptors.py
./src/test/test_digibyte --run_test=digidollar_wallet_tests
./src/test/test_digibyte --run_test=digidollar_rpc_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-019 — Watch-only DD address/Qt surfaces mislabeled monitoring DD as owned or spendable

**Severity:** Medium

**Affected invariant:** RPC/Qt/wallet surfaces must not mislead users into treating watch-only DD as spendable funds.

**Reachability path:** After public-descriptor import into a disabled-private-key descriptor wallet, `getdigidollarbalance` default behavior was fixed in Wave 9, but adjacent surfaces remained unsafe. `listdigidollaraddresses(true)` and `validateddaddress` labeled watch-only DD addresses as `ismine=true, iswatchonly=false`. Qt read `DigiDollarWallet::GetTotalDDBalance()` directly through `WalletModel::getDigiDollarBalance()`, so a private-key-disabled wallet could display raw monitoring DD as available/spendable and use that value in the send form's preflight balance check.

**Pre-fix affected code:**
- `src/rpc/digidollar.cpp:2324`-`src/rpc/digidollar.cpp:2340`
- `src/rpc/digidollar.cpp:2406`-`src/rpc/digidollar.cpp:2448`
- `src/qt/walletmodel.cpp:649`-`src/qt/walletmodel.cpp:672`
- `src/qt/walletmodel.cpp:1240`-`src/qt/walletmodel.cpp:1258`

**Reproducer evidence:** Agent C reproduced a disabled-private-key wallet where default balance was zero but watch-only address display returned `ismine=true, iswatchonly=false`; static Qt path review showed direct raw DD balance use.

**Fix summary:** `validateddaddress` and `listdigidollaraddresses` now mark disabled-private-key wallet DD as `ismine=false, iswatchonly=true`, and default `listdigidollaraddresses()` hides those entries unless `include_watchonly=true`. Qt `WalletModel::getDigiDollarBalance()` returns zero for private-key-disabled wallets, and `sendDigiDollar()` rejects such wallets before balance preflight.

**Post-fix affected code:**
- `src/rpc/digidollar.cpp:2324`-`src/rpc/digidollar.cpp:2340`
- `src/rpc/digidollar.cpp:2406`-`src/rpc/digidollar.cpp:2448`
- `src/qt/walletmodel.cpp:649`-`src/qt/walletmodel.cpp:658`
- `src/qt/walletmodel.cpp:1240`-`src/qt/walletmodel.cpp:1250`

**Tests added/upgraded:**
- `test/functional/wallet_digidollar_descriptors.py:454`-`test/functional/wallet_digidollar_descriptors.py:466` asserts watch-only DD addresses are hidden by default and labeled `ismine=false, iswatchonly=true` when included; it also checks `validateddaddress`.
- `src/qt/test/digidollarwidgettests.cpp:245`-`src/qt/test/digidollarwidgettests.cpp:272` asserts `WalletModel::getDigiDollarBalance()` returns zero for a private-key-disabled wallet even if the raw DD wallet balance is nonzero.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted qt/test/test_digibyte-qt
python3 test/functional/wallet_digidollar_descriptors.py
python3 test/functional/digidollar_rpc_addresses.py
python3 test/functional/digidollar_validate_address.py
./src/qt/test/test_digibyte-qt -platform offscreen -functions
./src/test/test_digibyte --run_test=digidollar_rpc_tests
```

Results: all passed after the fix. One initial Qt run failed because the new fixture had not called `EnsureDDWallet()`; the fixture was corrected and the full Qt test binary then passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-020 — Mixed-recipient DD transfer credited the wrong amount when our output was not first

**Severity:** Medium

**Affected invariant:** A DD transfer must not create, destroy, misroute, or hide DD value in wallet accounting.

**Reachability path:** `ProcessTransactionForDD()` parsed transfer OP_RETURN amounts into output order, but advanced `ddOutputIndex` only after finding an output owned by the wallet. In a multi-recipient transfer where the wallet owned the second DD output, the wallet could credit `amounts[0]` instead of `amounts[1]`. The contrasting rescan/incoming path advanced its index for every DD output and had the correct behavior.

**Pre-fix affected code:** `src/wallet/digidollarwallet.cpp:4110`-`src/wallet/digidollarwallet.cpp:4158`.

**Fix summary:** The block-connected wallet path now advances the DD amount index for every P2TR DD output before ownership filtering, then credits owned outputs with the matching output-order amount.

**Post-fix affected code:** `src/wallet/digidollarwallet.cpp:4110`-`src/wallet/digidollarwallet.cpp:4160`.

**Tests added/upgraded:** `test/functional/wallet_digidollar_mixed_output_accounting.py:1` mints 10000 cents, sends 6000 cents to node 1 and 4000 cents to node 2 in one `sendmanydigidollar` transaction, then asserts node 2 receives exactly 4000 cents despite owning the second DD output.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/wallet_digidollar_mixed_output_accounting.py
python3 test/functional/digidollar_transfer.py
./src/test/test_digibyte --run_test=digidollar_wallet_tests
```

Results: all passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

**Additional Wave 10 command matrix:**

```bash
python3 test/functional/wallet_digidollar_active_restore_redeem.py
python3 test/functional/wallet_digidollar_encrypted_received_redeem.py
python3 test/functional/wallet_digidollar_mixed_output_accounting.py
python3 test/functional/wallet_digidollar_descriptors.py
python3 test/functional/digidollar_encrypted_wallet.py
python3 test/functional/wallet_digidollar_encryption.py
python3 test/functional/digidollar_wallet_restore_redeem.py
python3 test/functional/digidollar_transfer.py
python3 test/functional/digidollar_rpc_addresses.py
python3 test/functional/digidollar_validate_address.py
./src/qt/test/test_digibyte-qt -platform offscreen -functions
./src/test/test_digibyte --run_test=digidollar_wallet_tests
./src/test/test_digibyte --run_test=digidollar_rpc_tests
```

Results: all passed in the final Wave 10 sweep.

**Rejected false positives / narrowed claims:**
- Locked encrypted DD write paths were mostly already guarded after Wave 9; the reachable key-handling failure was restored/received DD output key recovery, not a broad unlock bypass.
- Watch-only DD did not become spendable through RPC/Qt; send/mint/redeem paths rejected disabled-private-key wallets. The confirmed bug was misleading address/balance/spendability display.
- RH59 coin-control lock-bypass tests are explicitly documented as a post-walk-back invariant pin, not a live DD consensus bypass. Preset input behavior remains a wallet UX/policy caveat, but the prior "VULN CONFIRMED" strings are historical test text and not counted as a new Wave 10 bug.

**Theoretical / not yet reachable carried forward:**
- Generated zero-balance DD receive addresses are still omitted from `listdigidollaraddresses`; this is a low UX/schema gap unless a user-loss path is demonstrated.
- Mixed private-key-enabled wallets with directly imported watch-only DD descriptors may still need per-output DD ownership classification; the confirmed fixed path remains disabled-private-key descriptor watch-only.
- `GetHDKeyForDigiDollar()` still falls back to a random key if HD key extraction fails. Agent A did not prove a normal funded-wallet path where this happens, but if reachable it would weaken seed-only restore expectations.
- `ProcessIncomingTransaction()` still has a stale exception path on mint OP_RETURN parsing (`script number overflow`) in restore logs, but `ProcessDDTxForRescan()` and `ScanForDDUTXOs()` recovered the tested states. Needs a separate reproducer before counting.

**ARCHITECTURAL_REVIEW_REQUIRED:** none newly opened in Wave 10.

**Agent A result summary:** confirmed the received-DD redemption signing key gap that was fixed under `DD-RH-018`; rejected broad locked-wallet/write-path bypasses because current RPCs preflight unlock/private-key-disabled state.

**Agent B result summary:** identified the mixed DD output amount indexing bug fixed as `DD-RH-020`; identified stale incoming-output unit tests and a pending-balance assertion gap; RH59 was reviewed as a documented policy/test artifact rather than a new confirmed production DD bug.

**Agent C result summary:** confirmed watch-only address labeling and Qt raw-balance display issues fixed as `DD-RH-019`; rejected default RPC watch-only balance contamination because `DD-RH-017` already fixed it.

**Wave 10 status:** complete. Three bugs fixed (`DD-RH-018`, `DD-RH-019`, `DD-RH-020`), no commits, scope stayed within DigiDollar/oracle.

---

## Wave 11 — RPC Validation and Schema Edges

**Ledger path:** `reports/red_hornet_ledger.md`

**Live surface enumeration command:**

```bash
cd /home/jared/Code/digibyte
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1894 matching paths recorded at `/tmp/red_hornet_wave11_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: command registration, sensitive surfaces, hidden paths, activation gates, dangerous commands. Agent A completed without final text; a follow-up request was blocked by the tool safety filter, so no Agent A findings were used.
- Agent B — Invariant and test-gap reviewer: invalid parameter/schema/unit/error-path tests.
- Agent C — Boundary reviewer: wallet/no-wallet/locked-wallet RPC matrix.

**Scope note:** DigiDollar/oracle RPC, wallet-boundary, oracle-operator, and schema paths only; no generic DigiByte review.

### DD-RH-021 — `listdigidollaraddresses` min-balance filter parsed DD cents as DGB amount

**Severity:** Medium

**Affected invariant:** RPC/wallet surfaces must not hide or misreport DD value in a way that can mislead users or automation.

**Reachability path:** `listdigidollaraddresses(false, 1000)` was documented as a 1000-cent filter, but the handler used `AmountFromValue()`. A wallet with 50000 DD cents was filtered out because `1000` was interpreted on the DGB amount scale before comparison to DD cents. The pre-fix functional log showed `Addresses with balance >= 1000 cents: 0` while total listed addresses was 1; the test previously swallowed the issue.

**Affected code:** `src/rpc/digidollar.cpp:2410`.

**Fix summary:** Parse `min_balance` with `ParseDigiDollarRpcAmount()` so integer values remain DD cents and decimal strings remain decimal dollars.

**Regression:** `test/functional/digidollar_rpc_addresses.py:234`-`test/functional/digidollar_rpc_addresses.py:243` now asserts a funded address appears for a 1000-cent filter and disappears above its balance.

**Test evidence:**

```bash
python3 test/functional/digidollar_rpc_addresses.py
```

Pre-fix baseline exposed the zero-address log. Post-fix result: passed; `Addresses with balance >= 1000 cents: 1`.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-022 — Integral decimal-string DD transfer amounts were treated as cents

**Severity:** Medium

**Affected invariant:** A DD transfer must move the amount the user requested; RPC amount syntax must not silently send 100x less than documented.

**Reachability path:** The shared DD RPC amount parser converted only non-integral doubles to cents. A string such as `"50.00"` parsed to `50.0`, equaled `floor(50.0)`, and returned 50 cents instead of 5000 cents for `senddigidollar` and `sendmanydigidollar`, despite help/comments advertising decimal dollars.

**Affected code:** `src/rpc/digidollar.cpp:237`-`src/rpc/digidollar.cpp:275`.

**Fix summary:** Track whether a string amount contains a decimal point. Decimal strings are converted as dollars even when the numeric value is integral; integer numeric values and integer strings remain DD cents.

**Regression:** `test/functional/digidollar_rpc_amount_filters.py:68`-`test/functional/digidollar_rpc_amount_filters.py:84` sends `"50.00"` and `"25.00"` and asserts the recipients receive 5000 and 2500 cents.

**Pre-fix failing evidence:**

```bash
python3 test/functional/digidollar_rpc_amount_filters.py
```

Initial run stopped first at `DD-RH-025`; Agent B separately confirmed the parser path. The same test covers this path and passed only after the parser fix.

**Post-fix test evidence:** `python3 test/functional/digidollar_rpc_amount_filters.py` passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-023 — `listdigidollarpositions` filters used DGB units and could not select tier 0

**Severity:** Medium

**Affected invariant:** Position RPCs must not hide active collateral positions or apply filters in the wrong unit.

**Reachability path:** `min_amount` was parsed with `AmountFromValue()` and compared to `pos.dd_minted` in DD cents, so a 7500-cent filter could hide a 20000-cent position. The tier filter used `tierFilter > 0`, making the valid testing tier 0 unfilterable.

**Affected code:** `src/rpc/digidollar.cpp:1954`, `src/rpc/digidollar.cpp:2007`-`src/rpc/digidollar.cpp:2008`, `src/rpc/digidollar.cpp:2027`-`src/rpc/digidollar.cpp:2029`.

**Fix summary:** Document tier 0, parse `min_amount` with the DD parser, and apply tier filtering for `tierFilter >= 0`.

**Regression:** `test/functional/digidollar_rpc_amount_filters.py:51`-`test/functional/digidollar_rpc_amount_filters.py:60` mints tier 0 and tier 1 positions, then asserts tier 0 filtering and a 7500-cent minimum return only the expected position.

**Post-fix test evidence:** `python3 test/functional/digidollar_rpc_amount_filters.py` passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-024 — `getredemptioninfo` parsed requested DD amount as DGB amount

**Severity:** Low

**Affected invariant:** Redemption information must report the requested DD amount accurately so users do not make burn/unlock decisions from wrong estimates.

**Reachability path:** `getredemptioninfo(position_id, 5000)` used `AmountFromValue()` and then capped the huge DGB-scaled value to the full position amount, so a partial 5000-cent query returned full-position `redeemable_dd`.

**Affected code:** `src/rpc/digidollar.cpp:2951`-`src/rpc/digidollar.cpp:2954`.

**Fix summary:** Parse optional `dd_amount` through the DD amount parser.

**Regression:** `test/functional/digidollar_rpc_amount_filters.py:62`-`test/functional/digidollar_rpc_amount_filters.py:66` asserts both integer cents and `"50.00"` return 5000 redeemable cents for a larger position.

**Post-fix test evidence:** `python3 test/functional/digidollar_rpc_amount_filters.py` passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-025 — `getdigidollarbalance` accepted `minconf` but ignored it

**Severity:** Medium

**Affected invariant:** Wallet/RPC accounting must respect confirmation thresholds used by users, exchanges, and automation before treating DD as settled.

**Reachability path:** The RPC parsed and validated `minconf`, but total balance used `GetTotalDDBalance()` without applying the requested threshold. A one-confirmation mint was still returned by `getdigidollarbalance("", 2)`.

**Pre-fix failing evidence:**

```bash
python3 test/functional/digidollar_rpc_amount_filters.py
```

Failed with `AssertionError: not(25000 == 0)` at `getdigidollarbalance("", 2)`.

**Affected code:** `src/rpc/digidollar.cpp:2603`-`src/rpc/digidollar.cpp:2684`.

**Fix summary:** Build confirmed DD balance from wallet DD UTXOs by checking each output's wallet depth against `minconf`; address-specific queries use the same depth-aware path. Pending balance remains reported separately for default/one-confirmation views and is excluded when `minconf > 1`.

**Regression:** `test/functional/digidollar_rpc_amount_filters.py:44`-`test/functional/digidollar_rpc_amount_filters.py:49` asserts one-confirmation DD is excluded at `minconf=2` and included after another block.

**Post-fix test evidence:** `python3 test/functional/digidollar_rpc_amount_filters.py` passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-026 — `createoraclekey` generated private keys in disabled-private-key wallets

**Severity:** Medium

**Affected invariant:** Watch-only / disabled-private-key wallets must not silently create or store private oracle material.

**Reachability path:** Agent C created a disabled-private-key wallet and `createoraclekey` returned success, storing oracle private key material despite `private_keys_enabled=false`. The handler checked only wallet presence/unlock before key generation.

**Affected code:** `src/rpc/digidollar.cpp:4163`-`src/rpc/digidollar.cpp:4168`; key generation/storage follows at `src/rpc/digidollar.cpp:4189`-`src/rpc/digidollar.cpp:4199`.

**Fix summary:** Reject `WALLET_FLAG_DISABLE_PRIVATE_KEYS` before unlock/key generation.

**Regression:** `test/functional/digidollar_oracle_keygen.py:45`-`test/functional/digidollar_oracle_keygen.py:50` creates a disabled-private-key wallet and asserts `createoraclekey` fails with the private-keys-disabled error.

**Post-fix test evidence:** `python3 test/functional/digidollar_oracle_keygen.py` passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-027 — `startoracle` could fail as an internal RPC schema error

**Severity:** Low

**Affected invariant:** Oracle operator RPCs must return domain errors/status, not internal schema failures that obscure operational state.

**Reachability path:** `startoracle` declared `warning` as required in its RPC result schema but only emitted it conditionally. Existing `digidollar_oracle_keygen.py` caught the thrown internal schema error and treated it as a loose key-related failure, masking the bug.

**Affected code:** `src/rpc/digidollar.cpp:4238`-`src/rpc/digidollar.cpp:4244`.

**Fix summary:** Mark `warning` optional in the RPC result schema.

**Regression:** `test/functional/digidollar_oracle_keygen.py:91`-`test/functional/digidollar_oracle_keygen.py:107` and `test/functional/digidollar_oracle_keygen.py:118`-`test/functional/digidollar_oracle_keygen.py:125` now assert any `startoracle` exception is not an internal schema error and accept normal structured results.

**Post-fix test evidence:** `python3 test/functional/digidollar_oracle_keygen.py` passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-028 — `listoracle` no-running path tripped RPC schema checking

**Severity:** Low

**Affected invariant:** Oracle boundary RPCs must expose safe operator status for normal no-oracle-running states.

**Reachability path:** On an active chain with no local oracle running, `listoracle` returned only `running=false` and `message`, but its RPC result schema marked all running-oracle fields as required. The RPC failed with `Internal bug detected: RPC call "listoracle" returned incorrect type`.

**Pre-fix failing evidence:**

```bash
python3 test/functional/digidollar_oracle_keygen.py
```

Failed at `node.listoracle()` with missing `oracle_id`, `name`, `pubkey`, `price_micro_usd`, `price_usd`, `last_update`, and `enabled` fields.

**Affected code:** `src/rpc/digidollar.cpp:4002`-`src/rpc/digidollar.cpp:4010`; no-running return path at `src/rpc/digidollar.cpp:4111`-`src/rpc/digidollar.cpp:4112`.

**Fix summary:** Mark the running-only fields optional in the RPC result schema.

**Regression:** `test/functional/digidollar_oracle_keygen.py:39`-`test/functional/digidollar_oracle_keygen.py:43` asserts the no-running result returns cleanly.

**Post-fix test evidence:** `python3 test/functional/digidollar_oracle_keygen.py` passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

**Wave 11 command matrix:**

```bash
make -C src -j2 digibyted
python3 test/functional/digidollar_rpc_amount_filters.py        # failed pre-fix for DD-RH-025, passed post-fix
python3 test/functional/digidollar_oracle_keygen.py              # failed pre-fix for DD-RH-028, passed post-fix
python3 test/functional/digidollar_rpc_addresses.py
python3 test/functional/digidollar_rpc_gating.py
python3 test/functional/digidollar_rpc.py
python3 test/functional/wallet_digidollar_descriptors.py
python3 test/functional/digidollar_encrypted_wallet.py
./src/test/test_digibyte --run_test=digidollar_rpc_tests
./src/test/test_digibyte --run_test=oracle_rpc_tests
./src/test/test_digibyte --run_test=digidollar_rh46_rpc_input_validation_tests
```

Results: all post-fix commands passed. `digidollar_rpc.py` still contains explicit skipped sections for stale RPC implementation tests; recorded below as a test gap, not as a passing negative matrix.

**Rejected false positives / narrowed claims:**
- No-wallet matrix returned normal `-18 No wallet is loaded` behavior for wallet-context DD/oracle RPCs.
- Locked encrypted wallet read-only DD RPCs worked, while mutating/key-generation RPCs returned unlock errors; no broad locked-wallet bypass was proven.
- Disabled-private-key wallets already rejected DD mint/send/multisend/redeem/getaddress after prior fixes; the reachable new gap was `createoraclekey`.
- `importdigidollaraddress` false-success was already fixed under `DD-RH-009`; current tests codify unsupported watch-only import as `success=false`.
- Oracle positive schema coverage for `getoracles` / `getalloracleprices` was not contradicted by Wave 11.

**Theoretical / not yet reachable / test gaps:**
- Several RPC result schemas still use `STR_AMOUNT` while returning raw integer DD cents. This is client/schema risk and should be normalized, but no direct loss path was proven in Wave 11.
- `digidollar_rpc.py` still skips parameter validation, error handling, response format, and command integration sections, and contains stale comments/expectations. This is a coverage gap for Wave 19 unless tied to another reachable bug.
- `startoracle` can return `success=true` with `status="stopped"` on regtest because wallet-loaded keys can initialize while no price thread runs. The schema failure is fixed; the success/status semantics may need operator UX review if mainnet behavior is ambiguous.

**ARCHITECTURAL_REVIEW_REQUIRED:** none newly opened in Wave 11.

**Agent B result summary:** confirmed DD amount parser bugs, ignored `minconf`, DD-unit filter bugs, stale/skipped RPC negative tests, and RPC schema/help mismatches. `DD-RH-021` through `DD-RH-025` were fixed from this review.

**Agent C result summary:** confirmed `createoraclekey` disabled-private-key leakage and `listoracle`/`startoracle` schema failures. No-wallet, locked-wallet, and most disabled-private-key DD RPC paths were rejected as already guarded.

**Wave 11 status:** complete. Eight bugs fixed (`DD-RH-021` through `DD-RH-028`), no commits, scope stayed within DigiDollar/oracle.

## Wave 12 — Qt UX Bugs With Loss-of-Funds Potential

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave12_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: stale/misleading UI state that can cause DD/collateral loss.
- Agent B — Invariant and test-gap reviewer: Qt signal/slot/model tests for dangerous mismatches.
- Agent C — Boundary reviewer: user-flow simulations for mint/send/redeem/positions/transactions.

**Scope note:** Qt, wallet-model, and wallet/RPC surfaces directly used by DigiDollar UI only.

### DD-RH-029 — Vault health display treated oracle micro-USD as cents

**Severity:** Medium

**Affected invariant:** Qt/wallet surfaces must not mislead users about collateral health in a way that can delay necessary action.

**Reachability path:** The DD Vault table pulled `MockOracleManager::GetCurrentPrice()` / oracle bundle prices in micro-USD per DGB, but `DigiDollarPositionsWidget::CalculatePositionHealth()` treated the value as cents per DGB. A $100 DD / 300 DGB vault at $0.50/DGB should show 150% health; the UI displayed the capped 500% value.

**Affected code:** `src/qt/digidollarpositionswidget.cpp:1044`-`src/qt/digidollarpositionswidget.cpp:1067`.

**Fix summary:** Convert oracle micro-USD to cents per DGB before computing collateral value.

**Regression:** `src/qt/test/digidollarwidgettests.cpp:507`-`src/qt/test/digidollarwidgettests.cpp:547` sets a $0.50/DGB mock oracle, loads a 300 DGB / $100 position, and asserts the health bar is 150.

**Pre-fix failing evidence:**

```bash
./src/qt/test/test_digibyte-qt -platform offscreen -functions | rg -n "positionsWidget|DigiDollarWidgetTests"
```

Failed at `positionsWidgetHealthUsesMicroUsdOraclePrice`.

**Post-fix test evidence:** `./src/qt/test/test_digibyte-qt -platform offscreen -functions` passed; DigiDollarWidgetTests reported 35 passed after all Wave 12 fixes.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-030 — Vault initial load was throttled when wallet model arrived before client model

**Severity:** Low

**Affected invariant:** Qt positions state must not hide active collateral positions or delay redemption/monitoring actions due to model-order races.

**Reachability path:** `DigiDollarPositionsWidget::setWalletModel()` called `updatePositions()` before a client model was set. `updatePositions()` updated the throttle timestamp before `loadPositionsFromWallet()` returned early for missing `m_clientModel`, so the immediate `setClientModel()` refresh was skipped and the table stayed empty until the timer.

**Affected code:** `src/qt/digidollarpositionswidget.cpp:260`-`src/qt/digidollarpositionswidget.cpp:280`.

**Fix summary:** Return before touching the throttle timestamp unless both wallet and client models are present.

**Regression:** `src/qt/test/digidollarwidgettests.cpp:476`-`src/qt/test/digidollarwidgettests.cpp:505` sets wallet model first, client model second, and asserts the table immediately contains the mock position.

**Pre-fix failing evidence:** Same focused Qt command above failed at `positionsWidgetInitialLoadNotThrottled`.

**Post-fix test evidence:** `./src/qt/test/test_digibyte-qt -platform offscreen -functions` passed.

**Commit status:** uncommitted.

### DD-RH-031 — Qt mint collateral estimate omitted builder safety margin and stale tier ratios

**Severity:** Medium

**Affected invariant:** Users must approve the actual DGB collateral amount that will be locked for a DD mint.

**Reachability path:** The mint widget and `WalletModel::calculateRequiredCollateral()` used duplicated floating-point formulas while production `MintTxBuilder::CalculateRequiredCollateral()` adds a 1% safety margin and uses consensus tier ratios. For a $100 DD tier-1 mint at $0.50/DGB, Qt displayed / prechecked 1000 DGB while the builder locked 1010 DGB.

**Affected code:** `src/qt/digidollarmintwidget.cpp:864`-`src/qt/digidollarmintwidget.cpp:877`; `src/qt/walletmodel.cpp:1333`-`src/qt/walletmodel.cpp:1368`.

**Fix summary:** Make both the widget and wallet model call `DigiDollar::MintTxBuilder::CalculateRequiredCollateral()` using the current oracle price and tier-to-days mapping. Also corrected the invalid tier error text from 0-8 to 0-9.

**Regression:** `src/qt/test/digidollarwidgettests.cpp:304`-`src/qt/test/digidollarwidgettests.cpp:345` asserts both wallet-model collateral and visible widget collateral equal 1010 DGB for the reproducer.

**Pre-fix failing evidence:** The new regression targets the old duplicated formulas, which returned/displayed 1000 DGB for the same inputs. The failing assertion was captured during Wave 12 triage before final green run.

**Post-fix test evidence:** `make -C src -j2 qt/test/test_digibyte-qt && ./src/qt/test/test_digibyte-qt -platform offscreen -functions` passed.

**Commit status:** uncommitted.

### DD-RH-032 — Redeem widget failed to load RPC position amounts and could not safely gate timelocked vaults

**Severity:** Medium

**Affected invariant:** Qt redemption surfaces must show the correct position and must not invite redemption until the timelock has expired.

**Reachability path:** `listdigidollarpositions` returns `dgb_collateral` as an RPC amount string. `DigiDollarRedeemWidget::loadPositionDetails()` read it with `get_real()`, fell into the exception/reset path, and displayed a default empty position. Once loading is made robust, the same code path also needed explicit `blocks_remaining` gating so a loaded timelocked vault is not treated as redeemable.

**Affected code:** `src/qt/digidollarredeemwidget.cpp:770`-`src/qt/digidollarredeemwidget.cpp:884`; redeem button gate at `src/qt/digidollarredeemwidget.cpp:716`-`src/qt/digidollarredeemwidget.cpp:725`.

**Fix summary:** Parse RPC amount strings safely, add a direct wallet fallback for the same wallet model, set redeemable amount to zero while `blocks_remaining > 0`, leave the amount field empty for locked vaults, and require expired timelock in `validateRedeemable()`, `validateDDBalance()`, and button enablement.

**Regression:** `src/qt/test/digidollarwidgettests.cpp:407`-`src/qt/test/digidollarwidgettests.cpp:454` loads a timelocked mock position, asserts the actual $100 position details loaded, and asserts the redeem button stays disabled with 0 redeemable DD.

**Pre-fix failing evidence:**

```bash
make -C src -j2 qt/test/test_digibyte-qt && ./src/qt/test/test_digibyte-qt -platform offscreen -functions
```

Failed at `redeemWidgetKeepsTimelockedPositionDisabled`: the position detail label stayed at `0.00000000 DD`, proving the RPC amount path reset instead of loading the selected vault.

**Post-fix test evidence:** Same command passed; DigiDollarWidgetTests reported 35 passed.

**Commit status:** uncommitted.

### DD-RH-033 — Private-key-disabled wallets showed vault-table Redeem actions

**Severity:** Low

**Affected invariant:** Watch-only / disabled-private-key Qt surfaces must not present unspendable collateral as user-redeemable.

**Reachability path:** The DD Vault table computed `canRedeem` from timelock expiry and active status only. A private-key-disabled descriptor wallet that observed a matured position could get a green enabled `Redeem` action even though the wallet cannot sign the DD burn/collateral unlock.

**Affected code:** `src/qt/digidollarpositionswidget.cpp:497`-`src/qt/digidollarpositionswidget.cpp:499`.

**Fix summary:** Require `!wallet().privateKeysDisabled()` before marking a vault row redeemable.

**Regression:** `src/qt/test/digidollarwidgettests.cpp:549`-`src/qt/test/digidollarwidgettests.cpp:584` loads a matured position into a private-key-disabled wallet and asserts the action button is disabled and labeled `Locked`.

**Post-fix test evidence:** `./src/qt/test/test_digibyte-qt -platform offscreen -functions` passed.

**Commit status:** uncommitted.

### DD-RH-034 — ARCHITECTURAL_REVIEW_REQUIRED — Qt mint owner keys are random/non-HD and stored after broadcast

**Severity:** High

**Affected invariant:** Minted DD positions must remain redeemable after wallet restore, backup, restart, and crash boundaries.

**Reachability path:** Qt mint generates `ownerKey.MakeNewKey(true)` in `WalletModel::mintDigiDollar()` and stores it in the DD wallet only after `wallet().commitTransaction()`. RPC mint uses the wallet-derived `GetHDKeyForDigiDollar()` helper, but that helper is private to `src/rpc/digidollar.cpp`. Therefore, a Qt-minted position depends on extra wallet database state that is not seed-derived, and a crash between successful commit/broadcast and `StoreOwnerKey()` can leave collateral without the stored owner key.

**Affected code:** `src/qt/walletmodel.cpp:866`-`src/qt/walletmodel.cpp:870`, broadcast at `src/qt/walletmodel.cpp:1111`-`src/qt/walletmodel.cpp:1121`, owner-key storage at `src/qt/walletmodel.cpp:1126`-`src/qt/walletmodel.cpp:1138`; RPC HD helper at `src/rpc/digidollar.cpp:97`-`src/rpc/digidollar.cpp:151`.

**Required decision:** Move the HD-derived DD key helper into shared wallet code and make Qt mint use it, or route Qt mint through the hardened wallet RPC path. Also decide whether to persist owner key before broadcast, derive position keys from a deterministic position path, or add an atomic pending-mint record.

**Status:** Open; not implemented because this is a wallet-storage / key-lifecycle design change requiring Jared approval. No commit.

**Wave 12 command matrix:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' | sort
make -C src -j2 qt/test/test_digibyte-qt
./src/qt/test/test_digibyte-qt -platform offscreen -functions
python3 test/functional/wallet_digidollar_descriptors.py
python3 test/functional/digidollar_rpc_gating.py
python3 test/functional/digidollar_rpc_amount_filters.py
```

Results: all post-fix commands passed. The first full Qt run after adding the redeem regression failed as noted under `DD-RH-032`; final Qt run passed with 35 DigiDollar widget tests and all non-DD Qt groups passing.

**Rejected false positives / narrowed claims:**
- DigiDollar tab activation latch after reorg below activation was not counted as a loss bug in Wave 12: current backend/mempool/RPC gates reject DD operations below activation. It remains a UI refresh hardening item.
- Qt pre-activation mint/send direct calls were not counted as confirmed value-loss bugs because validation and RPC/consensus gates reject below-activation operations before wallet state is committed.
- The redeem-timelock enabled-button hypothesis was not counted separately because the pre-fix widget could not load real RPC positions due `DD-RH-032`. The timelock gate was fixed as hardening while closing `DD-RH-032`.
- Private-key-disabled Qt mint returns a signing/key error before broadcast; no collateral lock or DD loss path was proven. The broader owner-key lifecycle problem is tracked as `DD-RH-034`.
- Transactions tab address-search placeholder mismatch was recorded as UI polish/test gap, not a Red Hornet bug.
- Qt send fee confirmation hardcodes an approximate DGB fee. No transaction-value loss or DD misrouting path was proven in Wave 12; keep for UX backlog unless tied to fee/payment confirmation redesign.

**Theoretical / not yet reachable / test gaps:**
- Add explicit Qt tests for DigiDollar tab activation reorg refresh if a test harness can drive BIP9 state transitions in Qt.
- Add Qt tests for locked encrypted wallet states across mint/send/redeem dialogs, beyond current WalletModel and functional coverage.
- Consider a Qt-side integration path that calls wallet RPCs for mint to reduce duplicate validation/mint-building code.

**Agent A result summary:** confirmed Qt mint owner-key lifecycle risk (`DD-RH-034`) and collateral-understatement (`DD-RH-031`); send fee confirmation remained unproven as value-loss.

**Agent B result summary:** confirmed Qt mint/model collateral drift, owner-key lifecycle risk, and stale tier message; the stale tier message was fixed with `DD-RH-031`.

**Agent C result summary:** confirmed activation-latch concern as UI hardening but not a reachable backend bypass, identified redeem-position gating, and private-key-disabled vault-action issues (`DD-RH-032`, `DD-RH-033`).

**Wave 12 status:** complete. Five bugs fixed (`DD-RH-029` through `DD-RH-033`), one open architecture item (`DD-RH-034`), no commits, scope stayed within DigiDollar/oracle Qt/wallet surfaces.

## Wave 13 — Oracle Roster, Threshold, Config

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave13_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: wrong roster/order/threshold handling paths.
- Agent B — Invariant and test-gap reviewer: bitmap/config mismatch tests and malformed bundle regressions.
- Agent C — Boundary reviewer: operator config/RPC/P2P boundaries.

**Scope note:** oracle roster, threshold, MuSig2 aggregation, RPC/operator, and P2P surfaces only.

### DD-RH-035 — Regtest MuSig2 aggregate keys rejected the configured 4-of-7 threshold

**Severity:** Medium

**Affected invariant:** MuSig2 participant thresholds must bind to the active chain's configured oracle threshold, not a compile-time mainnet constant.

**Reachability path:** Regtest consensus config uses `nOracleConsensusRequired=4` and `nOracleTotalOracles=7`. `MuSig2OracleAggregator::ComputeAggregatePubkey()` sorted the requested signer IDs but then called public `EncodeBitmap()`, which rejects fewer than compile-time `ORACLE_CONSENSUS_REQUIRED` signers. This made an exactly threshold-sized 4-of-7 signer set unreachable through the configured regtest MuSig2 path. The same aggregator is used by oracle signing session flow and Phase 3 bundle validation.

**Affected code:** `src/oracle/musig2_aggregator.cpp:111`-`src/oracle/musig2_aggregator.cpp:131`; regression at `src/test/musig2_aggregator_tests.cpp:378`-`src/test/musig2_aggregator_tests.cpp:402`.

**Pre-fix failing evidence:**

```bash
./src/test/test_digibyte --run_test=musig2_aggregator_regtest_tests/regtest_aggregate_pubkey_uses_configured_threshold
```

Failed with the new assertion message: `regtest 4-of-7 MuSig2 aggregation must accept exactly the configured threshold`.

**Fix summary:** Added an internal bitmap encoder for aggregate-key cache keys that validates duplicates and bounds without the compile-time threshold check. `ComputeAggregatePubkey()` now reads `Params().GetConsensus().nOracleConsensusRequired` and `nOracleTotalOracles`, enforces the configured threshold and total, and leaves public `EncodeBitmap()` behavior unchanged for existing RC30 tests.

**Post-fix test evidence:**

```bash
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=musig2_aggregator_regtest_tests/regtest_aggregate_pubkey_uses_configured_threshold
./src/test/test_digibyte --run_test=musig2_aggregator_tests
./src/test/test_digibyte --run_test=musig2_aggregator_regtest_tests
```

All passed after the fix.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-036 — `startoracle` reported success while the oracle was stopped

**Severity:** Medium

**Affected invariant:** Oracle operator RPCs must not report a running oracle when the local node only initialized a key or node object.

**Reachability path:** On regtest and other non-testnet chain types where `OracleNode::Start()` does not run the price thread, `startoracle` with a wallet-loaded oracle key could return `success=true` and `status="stopped"`. An operator or automation layer could treat the node as producing prices even though no price thread was active.

**Affected code:** helper at `src/rpc/digidollar.cpp:204`-`src/rpc/digidollar.cpp:240`; result schema at `src/rpc/digidollar.cpp:4240`-`src/rpc/digidollar.cpp:4249`; handler path at `src/rpc/digidollar.cpp:4293`-`src/rpc/digidollar.cpp:4370`; functional regression at `test/functional/digidollar_oracle_keygen.py:95`-`test/functional/digidollar_oracle_keygen.py:129`.

**Pre-fix failing evidence:**

```bash
python3 test/functional/digidollar_oracle_keygen.py
```

Failed after the regression tightened expectations because `startoracle` returned `{'success': True, 'status': 'stopped', ...}` for an initialized-but-not-running oracle.

**Fix summary:** `TryStartOracleFromPrivateKey()` now returns `false` when it only initializes an oracle but no price thread starts, while optionally reporting `initialized=true`. `startoracle` now exposes an `initialized` result field, keeps wallet-loaded-key detection separate from `success`, and returns an explicit `price thread not active on this network` message for initialized/stopped states.

**Post-fix test evidence:**

```bash
make -C src -j2 digibyted
python3 test/functional/digidollar_oracle_keygen.py
```

Passed after the fix; the functional test was rerun again in the final Wave 13 matrix and passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-037 — ARCHITECTURAL_REVIEW_REQUIRED — Mainnet has 30 active oracle nodes but 17 consensus bitmap/pubkey slots

**Severity:** High

**Affected invariant:** The oracle roster used by selection, RPC/operator surfaces, P2P admission, Phase 2 validation, and Phase 3 bitmap validation must describe the same active signing set for a given epoch.

**Reachability path:** Mainnet chainparams define `nOracleTotalOracles=17`, `nOracleConsensusRequired=9`, and 17 MuSig2 public keys, but `vOracleNodes` contains 30 entries marked active. `SelectOraclesForEpoch()` filters all active nodes and selects 17 from that 30-node set. Phase 3 bitmap validation only has 17 slots, while Phase 2 fallback remains accepted and uses active-oracle selection. Operator/RPC/P2P surfaces also accept IDs up to `ORACLE_TOTAL_COUNT` / 29. This makes reserve IDs 17-29 visible as active operators and potentially eligible in Phase 2 active sets while Phase 3 cannot encode them.

**Affected code:** mainnet threshold and pubkey count at `src/kernel/chainparams.cpp:313`-`src/kernel/chainparams.cpp:325`; active reserve nodes at `src/kernel/chainparams.cpp:381`-`src/kernel/chainparams.cpp:395`; epoch selection at `src/primitives/oracle.cpp:509`-`src/primitives/oracle.cpp:551`; Phase 2 block validation at `src/oracle/bundle_manager.cpp:2348`-`src/oracle/bundle_manager.cpp:2380` and `src/oracle/bundle_manager.cpp:2622`; RPC/operator surfaces at `src/rpc/digidollar.cpp:3864`, `src/rpc/digidollar.cpp:3955`, and `src/rpc/digidollar.cpp:4274`-`src/rpc/digidollar.cpp:4290`; P2P admission at `src/net_processing.cpp:5469`.

**Why not fixed in Wave 13:** Changing the mainnet oracle roster, reserve semantics, Phase 2 fallback policy, or bitmap width is a consensus/protocol/operator-design decision. The review recorded the risk and continued with safe tests only.

**Options for Jared review:**
- Mark IDs 17-29 inactive and update RPC/operator surfaces to distinguish consensus-active signers from backups/reserves.
- Keep 30 active signers but align consensus totals, MuSig2 pubkeys, bitmap sizes, and thresholds to a 30-node roster.
- Disable or remove v0x02 fallback after Phase 3 activation if Phase 3 is intended to be mandatory.

**Recommendation:** For launch, make the 17-slot consensus-active roster explicit and mark reserves as inactive or backups-only, then decide whether Phase 2 fallback remains valid after Phase 3 activation.

**Status:** open; no code changes made for this item.

**Wave 13 command matrix:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' | sort
make -C src -j2 test/test_digibyte
make -C src -j2 digibyted
./src/test/test_digibyte --run_test=musig2_aggregator_regtest_tests/regtest_aggregate_pubkey_uses_configured_threshold
./src/test/test_digibyte --run_test=musig2_aggregator_tests
./src/test/test_digibyte --run_test=musig2_aggregator_regtest_tests
./src/test/test_digibyte --run_test=oracle_config_tests
./src/test/test_digibyte --run_test=oracle_rpc_tests
./src/test/test_digibyte --run_test=oracle_consensus_threshold_tests
./src/test/test_digibyte --run_test=rh50_oracle_keyset_alignment_tests
./src/test/test_digibyte --run_test=redteam_phase2_audit_tests
python3 test/functional/digidollar_oracle_keygen.py
python3 test/functional/rpc_getoracles_pending.py
python3 test/functional/feature_oracle_p2p.py
```

Results: all final commands passed. An earlier combined Boost filter attempt failed only because the filter syntax matched no test cases; it was not a code failure. `feature_oracle_p2p.py` still logs expected `Method not found (-32601)` warnings for TODO RPC methods but exits successfully.

**Rejected false positives / narrowed claims:**
- Attacker-supplied pubkeys in Phase 2 bundles were rejected as not reachable through the production on-chain v0x02 format. `ExtractOracleBundle()` binds each message to chainparams pubkeys, and P2P/local validation also rebinds against chainparams.
- Phase 2 median skew from invalid extra messages was rejected for on-chain v0x02 bundles after the existing T5-03 format change because v0x02 stores one consensus price/timestamp shared by the message entries.
- `ValidateBundle()` returning true for v0x03 was not counted as production reachable; current block validation calls `ValidatePhaseThreeBundle()` directly.
- Phase 2 duplicate oracle IDs are already rejected by the duplicate set in `ValidatePhaseTwoBundle()`.
- Phase 3 wrong signer order and duplicate bitmap cases are already covered by aggregator tests.
- `HasRequiredSignatures()` remains a weak helper if called directly, but current validation rejects duplicates before threshold counting; recorded as theoretical helper-risk only.

**Theoretical / not yet reachable / test gaps:**
- Oracle RPC display names are hardcoded only through slot 10, so active slots 11-16 can display as `Unknown`. No direct loss path was proven; keep as operator UX/test backlog.
- Existing RH56 coverage shows v0x03 extraction can inflate oversized bitmaps into synthetic messages before validation rejects them. This is currently a parser/resource hardening test gap unless Wave 18 proves a reachable resource-exhaustion path.

**Agent A result summary:** confirmed the mainnet active-roster mismatch as an architecture-review item (`DD-RH-037`) and rejected Phase 2 attacker-pubkey and Phase 2 median-skew hypotheses as blocked by extraction/binding format.

**Agent B result summary:** confirmed the configured-threshold aggregation bug (`DD-RH-035`), validated existing bitmap/order tests, and preserved oversized-bitmap parser inflation for resource-bound review.

**Agent C result summary:** confirmed `startoracle` initialized-but-stopped false success (`DD-RH-036`) and traced RPC/P2P active-ID surfaces that amplify the open roster architecture item.

**Wave 13 status:** complete. Two bugs fixed (`DD-RH-035`, `DD-RH-036`), one open architecture item (`DD-RH-037`), no commits, scope stayed within DigiDollar/oracle.

## Wave 14 — Oracle Feed Handling and Staleness

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave14_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: stale timestamp, outlier handling, fallback paths, exchange handling.
- Agent B — Invariant and test-gap reviewer: aggregation/freshness tests and boundary cases.
- Agent C — Boundary reviewer: functional/RPC status flows for bad feeds and recovery.

**Scope note:** oracle feed, cache, RPC status, pending-message, and MuSig2 bundle price surfaces only.

### DD-RH-038 — Historical oracle cache reload refreshed stale bundle timestamps

**Severity:** Medium

**Affected invariant:** Oracle prices must not become fresh merely because a node restarted, reloaded chain data, or reconnected a historical block.

**Reachability path:** `LoadPricesFromChain()` and `ConnectBlock()` called `UpdatePriceCache(height, price)` without the bundle's signed timestamp, and `UpdatePriceCache()` set `last_update_time=GetTime()`. A stale historical on-chain bundle could therefore rearm `GetLatestPrice()` and `GetStats().has_consensus` as fresh for wallet/RPC/mempool DigiDollar paths after restart or block reconnection.

**Affected code:** cache API and timestamp map at `src/oracle/bundle_manager.h:217`-`src/oracle/bundle_manager.h:223` and `src/oracle/bundle_manager.h:255`-`src/oracle/bundle_manager.h:258`; stale-aware stats at `src/oracle/bundle_manager.cpp:1869`-`src/oracle/bundle_manager.cpp:1878`; chain reload call at `src/oracle/bundle_manager.cpp:1977`-`src/oracle/bundle_manager.cpp:1981`; cache update/removal at `src/oracle/bundle_manager.cpp:2190`-`src/oracle/bundle_manager.cpp:2252`; `ConnectBlock()` update at `src/validation.cpp:2834`-`src/validation.cpp:2842`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=oracle_price_staleness_tests/cache_reload_must_not_refresh_stale_oracle_timestamp
```

Failed with `manager.GetLatestPrice() == 0` reporting `[500000 != 0]`.

**Fix summary:** `UpdatePriceCache()` now accepts a source timestamp and stores per-height cache timestamps. Chain reload and block connection pass the extracted bundle timestamp. `GetStats()` reports no consensus/latest price once the source timestamp is stale, and `RemovePriceCache()` restores the previous height's source timestamp on disconnect.

**Regression:** `src/test/oracle_price_staleness_tests.cpp:251`-`src/test/oracle_price_staleness_tests.cpp:275`.

**Post-fix test evidence:** focused regression passed; the full `oracle_price_staleness_tests`, `oracle_bundle_manager_tests`, `oracle_p2p_tests`, `digidollar_rh16_reorg_attacks_tests`, `rh61_coinbase_price_cache_poisoning_tests`, `oracle_price_feed_rh09_tests`, `oracle_consensus_threshold_tests`, and `oracle_integration_tests` were rerun and passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-039 — Oracle nodes could re-sign stale exchange prices with fresh timestamps

**Severity:** High

**Affected invariant:** Oracle messages signed with fresh timestamps must correspond to a fresh exchange-fed price, not a stale local fallback.

**Reachability path:** After a successful price fetch/broadcast, exchange fetch failures did not clear `current_price` or `last_broadcast_price`. `HasValidPrice()` treated recent `last_broadcast_timestamp` as valid, `ShouldBroadcast()` gated only on that validity, and `BroadcastCurrentPrice()` called `GetCurrentPrice()`, which could return the stale `last_broadcast_price`. Each rebroadcast then signed the old price with `timestamp=GetTime()` and refreshed `last_broadcast_timestamp`, keeping the stale price alive indefinitely while feeds were down.

**Affected code:** fresh-source helpers at `src/oracle/node.h:122`-`src/oracle/node.h:136`; production gating at `src/oracle/node.cpp:226`-`src/oracle/node.cpp:239`, `src/oracle/node.cpp:400`-`src/oracle/node.cpp:449`, and `src/oracle/node.cpp:453`-`src/oracle/node.cpp:457`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=oracle_price_staleness_tests/recent_broadcast_price_does_not_authorize_stale_rebroadcast
```

Failed with `check !node.ShouldBroadcastForTesting() has failed`.

**Fix summary:** Added `HasFreshExchangePrice()` and `GetFreshExchangePrice()`. `ShouldBroadcast()` now requires a fresh exchange price, and `BroadcastCurrentPrice()` signs only that fresh exchange price. The existing public `HasValidPrice()` remains available for display/read paths, but no longer authorizes broadcasts.

**Regression:** `src/test/oracle_price_staleness_tests.cpp:277`-`src/test/oracle_price_staleness_tests.cpp:295`.

**Post-fix test evidence:** focused regression passed. Final Wave 14 unit matrix including `oracle_price_staleness_tests`, `oracle_rpc_tests`, `oracle_p2p_tests`, and `oracle_wallet_autostart_tests` passed.

**Commit status:** uncommitted.

### DD-RH-040 — Pending-message live cache used a different median than consensus validation

**Severity:** Medium

**Affected invariant:** The live oracle price exposed to wallet/RPC/mempool paths must match the price that the same pending messages would validate on-chain.

**Reachability path:** `AddOracleMessage()` updated `cached_price` with `prices[prices.size()/2]`, so an even 4-message pending set `[10000, 10000, 20000, 20000]` exposed `20000`. `CalculateConsensusPrice()` uses the consensus median for even sets, yielding `(10000 + 20000) / 2 = 15000`. A node could display or validate DigiDollar operations against a live pending price that would not match the eventual bundle consensus price.

**Affected code:** pending-cache update now uses consensus calculation at `src/oracle/bundle_manager.cpp:204`-`src/oracle/bundle_manager.cpp:228`; consensus function at `src/oracle/bundle_manager.cpp:2822`-`src/oracle/bundle_manager.cpp:2891`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=oracle_p2p_tests/test_pending_cache_uses_consensus_median
```

Failed with `manager.GetLatestPrice() == consensus_price` reporting `[20000 != 15000]`.

**Fix summary:** `AddOracleMessage()` now builds a temporary `COracleBundle` from pending messages and calls `CalculateConsensusPrice()` before updating `cached_price` or generating consensus attestations. Invalid/no-price consensus leaves the cache unchanged.

**Regression:** `src/test/oracle_p2p_tests.cpp:914`-`src/test/oracle_p2p_tests.cpp:951`.

**Post-fix test evidence:** focused regression passed; final unit matrix included `oracle_p2p_tests`, `oracle_bundle_manager_tests`, `oracle_consensus_threshold_tests`, `oracle_price_staleness_tests`, `oracle_price_feed_rh09_tests`, and `oracle_integration_tests`, all passing.

**Commit status:** uncommitted.

### DD-RH-041 — Signed Phase 3 MuSig2 bundles accepted out-of-range consensus prices

**Severity:** High

**Affected invariant:** Phase 3 oracle bundles must reject invalid prices even if a threshold aggregate signature is present.

**Reachability path:** `ValidatePhaseThreeBundle()` checked version, aggregate signature size, bitmap, epoch, threshold, aggregate pubkey, and aggregate signature, but did not check `median_price_micro_usd` against `ORACLE_MIN_PRICE_MICRO_USD`/`ORACLE_MAX_PRICE_MICRO_USD`. A threshold set of regtest oracle keys could produce a valid aggregate signature over price `0`, and the pre-fix validator accepted it.

**Affected code:** new v0x03 price-range check at `src/oracle/bundle_manager.cpp:2728`-`src/oracle/bundle_manager.cpp:2739`; signed v0x03 regression helper/test at `src/test/musig2_bundle_manager_tests.cpp:62`-`src/test/musig2_bundle_manager_tests.cpp:173` and `src/test/musig2_bundle_manager_tests.cpp:237`-`src/test/musig2_bundle_manager_tests.cpp:255`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=musig2_bundle_manager_tests/validate_v03_rejects_signed_out_of_range_price
```

Failed because `ValidatePhaseThreeBundle(bundle, 0, params, error)` returned true for a valid aggregate signature over price `0`.

**Fix summary:** `ValidatePhaseThreeBundle()` now rejects v0x03 consensus prices outside the oracle price range before signature verification work.

**Post-fix test evidence:** focused regression passed. Final MuSig2/bundle matrix passed: `musig2_bundle_manager_tests`, `musig2_aggregator_tests`, `musig2_session_tests`, `musig2_oracle_node_tests`, `rh05_bundle_validation_attacks`, `rh56_oversized_bitmap_message_inflation_tests`, and `digidollar_rh40_regression_tests`.

**Commit status:** uncommitted.

### DD-RH-042 — `getoracleprice` and `listoracle` counted stale pending messages as live oracle data

**Severity:** Low

**Affected invariant:** RPC/operator status surfaces must not report stale pending oracle gossip as current reporting activity.

**Reachability path:** A valid pending P2P oracle message can remain in `pending_messages` after it ages past `ORACLE_MAX_AGE_SECONDS` if no new oracle message arrives to trigger the purge in `AddOracleMessage()`. `getoracles`/`getalloracleprices` already filtered stale pending messages, but `getoracleprice` counted them toward `oracle_count` and `freshestPendingTime`, and `listoracle` used them as a pending price fallback.

**Affected code:** stale pending filter in `getoracleprice` at `src/rpc/digidollar.cpp:3299`-`src/rpc/digidollar.cpp:3310`; stale pending filter in `listoracle` at `src/rpc/digidollar.cpp:4068`-`src/rpc/digidollar.cpp:4078`; regression at `src/test/digidollar_rpc_tests.cpp:28`-`src/test/digidollar_rpc_tests.cpp:37` and `src/test/digidollar_rpc_tests.cpp:310`-`src/test/digidollar_rpc_tests.cpp:350`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=digidollar_rpc_tests/test_getoracleprice_ignores_stale_pending_messages
```

After disabling the regtest mock oracle, the regression failed with `oracle_count` `[1 != 0]`, nonzero `last_update_time`, and `status` `warning` instead of `error`.

**Fix summary:** `getoracleprice` and `listoracle` now skip pending messages whose timestamp is older than `ORACLE_MAX_AGE_SECONDS`, matching the shared scanner used by `getoracles` and `getalloracleprices`.

**Post-fix test evidence:** focused regression passed; final unit matrix included `digidollar_rpc_tests` and `oracle_rpc_tests`, both passing.

**Commit status:** uncommitted.

**Wave 14 command matrix:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' | sort
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=oracle_price_staleness_tests/cache_reload_must_not_refresh_stale_oracle_timestamp
./src/test/test_digibyte --run_test=oracle_price_staleness_tests/recent_broadcast_price_does_not_authorize_stale_rebroadcast
./src/test/test_digibyte --run_test=oracle_p2p_tests/test_pending_cache_uses_consensus_median
./src/test/test_digibyte --run_test=musig2_bundle_manager_tests/validate_v03_rejects_signed_out_of_range_price
./src/test/test_digibyte --run_test=digidollar_rpc_tests/test_getoracleprice_ignores_stale_pending_messages
./src/test/test_digibyte --run_test=oracle_price_staleness_tests
./src/test/test_digibyte --run_test=oracle_p2p_tests
./src/test/test_digibyte --run_test=oracle_bundle_manager_tests
./src/test/test_digibyte --run_test=oracle_consensus_threshold_tests
./src/test/test_digibyte --run_test=oracle_price_feed_rh09_tests
./src/test/test_digibyte --run_test=oracle_integration_tests
./src/test/test_digibyte --run_test=oracle_rpc_tests
./src/test/test_digibyte --run_test=digidollar_rpc_tests
./src/test/test_digibyte --run_test=musig2_bundle_manager_tests
./src/test/test_digibyte --run_test=musig2_aggregator_tests
./src/test/test_digibyte --run_test=musig2_session_tests
./src/test/test_digibyte --run_test=musig2_oracle_node_tests
./src/test/test_digibyte --run_test=rh05_bundle_validation_attacks
./src/test/test_digibyte --run_test=rh56_oversized_bitmap_message_inflation_tests
./src/test/test_digibyte --run_test=digidollar_rh40_regression_tests
test/functional/digidollar_rpc_oracle.py
python3 test/functional/digidollar_oracle_phase2.py
python3 test/functional/rpc_getoracles_pending.py
python3 test/functional/feature_oracle_p2p.py
```

Results: all final commands passed. A combined functional `test_runner.py` invocation rejected these scripts because they are not in the runner allowlist, so the scripts were run directly. `feature_oracle_p2p.py` still logs expected `Method not found (-32601)` warnings for unimplemented TODO RPCs and exits successfully.

**Rejected false positives / narrowed claims:**
- Consensus block validation already rejects stale oracle bundle timestamps relative to block time at `src/oracle/bundle_manager.cpp:2439`-`src/oracle/bundle_manager.cpp:2454`; Wave 14 cache fixes address wallet/RPC/live-cache freshness, not a direct stale-block-acceptance path.
- Exchange HTTP fetching fails closed for missing TLS/CA/curl and aggregation requires enough valid exchange sources; no reachable bad-feed acceptance was proven in `src/oracle/exchange.cpp`.
- `getoracles` and `getalloracleprices` already filtered stale pending messages through the shared scanner at `src/rpc/digidollar.cpp:3685`-`src/rpc/digidollar.cpp:3704`; only `getoracleprice`/`listoracle` needed Wave 14 fixes.
- Historical lookup fallback to current/previous epoch bundle was not proven reachable for consensus validation because `ConnectBlock()` uses the block-extracted oracle price path.

**Theoretical / not yet reachable / test gaps:**
- Phase 2 bundles carry one consensus timestamp and cannot prove each underlying exchange-source timestamp on-chain; source freshness still depends on honest oracle node behavior and should be revisited if the oracle protocol adds source attestations.
- `GetPendingMessages()` remains a raw accessor and can return stale in-memory messages to internal callers; Wave 14 hardened user-facing RPC status paths, but a future cleanup could add a fresh-only accessor to avoid caller-by-caller filtering.
- `feature_oracle_p2p.py` contains expected TODO-method warnings; once the missing RPC/P2P hooks are implemented, convert those expected warnings into hard assertions.

**ARCHITECTURAL_REVIEW_REQUIRED items:** none newly added in Wave 14.

**Agent A result summary:** confirmed stale exchange prices could be re-signed indefinitely (`DD-RH-039`) and rejected stale bundle timestamp acceptance at block validation as already checked.

**Agent B result summary:** confirmed `DD-RH-039`, the pending-cache median split (`DD-RH-040`), and the v0x03 price-range validation gap (`DD-RH-041`).

**Agent C result summary:** confirmed stale cached stats/RPC status coupling covered by `DD-RH-038`, identified stale pending RPC status exposure fixed as `DD-RH-042`, and noted weak functional stale-status assertions for future hardening.

**Wave 14 status:** complete. Five bugs fixed (`DD-RH-038` through `DD-RH-042`), no new architecture-review item, no commits, scope stayed within DigiDollar/oracle.

## Wave 15 — Oracle P2P Handling

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave15_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: getoracles/pending bundle message handling paths.
- Agent B — Invariant and test-gap reviewer: malformed/duplicate/reordered P2P tests.
- Agent C — Boundary reviewer: multi-node P2P flows and restart behavior.

**Scope note:** oracle P2P, pending-message, consensus-proposal, attestation, GETORACLES, and MuSig2 P2P boundary code only.

### DD-RH-043 — Unauthenticated ORACLECONSENSUS proposals could make oracle nodes sign arbitrary consensus attestations

**Severity:** High

**Affected invariant:** Oracle signatures used as block-valid Phase 2 attestations must bind to the exact price/timestamp consensus the local node independently computed from pending oracle messages.

**Reachability path:** A peer could send an `ORACLECONSENSUS` proposal. The handler accepted proposals when the local node could not compute its own consensus, and otherwise allowed a 10% price window without checking timestamp equality. Running local oracle nodes then called `OracleNode::CreateConsensusAttestation()` and signed the peer-supplied `consensus_price`/`consensus_timestamp`. Those signatures are valid Phase 2 block material because v0x02 stores one consensus price/timestamp and verifies each signature over that tuple.

**Affected code:** pre-fix loose/absent cross-check in `src/net_processing.cpp:5817`-`src/net_processing.cpp:5837`; signing call in `src/net_processing.cpp:5835`-`src/net_processing.cpp:5840`; unchecked signing in `src/oracle/node.cpp:261`-`src/oracle/node.cpp:289`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/consensus_attestation_requires_exact_local_consensus_values
```

Failed with three assertions: the oracle signed without local quorum, signed a consensus price differing from local consensus, and signed a consensus timestamp differing from local consensus.

**Fix summary:** Added `OracleBundleManager::ValidateConsensusProposal()` and required exact local pending-message consensus before signing or relaying an `ORACLECONSENSUS` proposal. `OracleNode::CreateConsensusAttestation()` now refuses to sign when the proposal lacks local quorum or differs in price/timestamp.

**Affected code after fix:** declaration at `src/oracle/bundle_manager.h:97`-`src/oracle/bundle_manager.h:103`; validation at `src/oracle/bundle_manager.cpp:452`-`src/oracle/bundle_manager.cpp:479`; node signing guard at `src/oracle/node.cpp:261`-`src/oracle/node.cpp:269`; P2P proposal gate at `src/net_processing.cpp:5817`-`src/net_processing.cpp:5826`.

**Regression:** deterministic regtest key helpers at `src/test/oracle_bundle_manager_tests.cpp:24`-`src/test/oracle_bundle_manager_tests.cpp:42`; exact-consensus signing test at `src/test/oracle_bundle_manager_tests.cpp:455`-`src/test/oracle_bundle_manager_tests.cpp:492`.

**Post-fix test evidence:** focused regression passed. Final Wave 15 unit matrix passed: `oracle_bundle_manager_tests`, `oracle_p2p_tests`, `oracle_phase2_tests`, `redteam_phase2_audit_tests`, and `musig2_bundle_manager_tests`. Functional `feature_oracle_p2p.py`, `digidollar_oracle_phase2.py`, and `rpc_getoracles_pending.py` also passed.

**Commit status:** uncommitted. Local commits were not explicitly authorized.

### DD-RH-044 — GETORACLES served stale pending oracle messages

**Severity:** Low

**Affected invariant:** P2P recovery/sync surfaces must not propagate stale oracle price data after it has aged beyond the oracle freshness window.

**Reachability path:** `GETORACLES` directly iterated `OracleBundleManager::GetPendingMessages()` and sent every matching pending message. Stale cleanup happens inside `AddOracleMessage()`, so a quiet node could retain and resend stale pending entries indefinitely until another add path purged them. Receiving nodes still reject stale `ORACLEPRICE`, so this was a bandwidth/state-churn bug rather than a price-corruption bug.

**Affected code:** raw pending read/send path at `src/net_processing.cpp:6243`-`src/net_processing.cpp:6264`; stale purge only on add at `src/oracle/bundle_manager.cpp:100`-`src/oracle/bundle_manager.cpp:117`; raw accessor at `src/oracle/bundle_manager.cpp:294`-`src/oracle/bundle_manager.cpp:310`.

**Pre-fix failing evidence:**

```bash
python3 test/functional/rpc_getoracles_pending.py
```

Failed after advancing mock time by 3601 seconds with `AssertionError: not(1 == 0)`, proving a stale `oracleprice` response was still sent.

**Fix summary:** `GETORACLES` now skips pending messages older than `ORACLE_MAX_AGE_SECONDS` and messages more than 60 seconds in the future before sending responses.

**Affected code after fix:** stale/future filter at `src/net_processing.cpp:6247`-`src/net_processing.cpp:6268`.

**Regression:** P2P receiver helper and constants at `test/functional/rpc_getoracles_pending.py:16`-`test/functional/rpc_getoracles_pending.py:39`; activation height update at `test/functional/rpc_getoracles_pending.py:58`-`test/functional/rpc_getoracles_pending.py:71`; stale GETORACLES test at `test/functional/rpc_getoracles_pending.py:241`-`test/functional/rpc_getoracles_pending.py:264`.

**Post-fix test evidence:** focused functional regression passed after rebuilding `digibyted`. Final Wave 15 functional matrix passed.

**Commit status:** uncommitted.

**Wave 15 command matrix:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' | sort
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/consensus_attestation_requires_exact_local_consensus_values
./src/test/test_digibyte --run_test=oracle_bundle_manager_tests
./src/test/test_digibyte --run_test=oracle_p2p_tests
./src/test/test_digibyte --run_test=oracle_phase2_tests
./src/test/test_digibyte --run_test=redteam_phase2_audit_tests
./src/test/test_digibyte --run_test=musig2_bundle_manager_tests
python3 test/functional/rpc_getoracles_pending.py
make -C src -j2 digibyted test/test_digibyte
python3 test/functional/feature_oracle_p2p.py
python3 test/functional/digidollar_oracle_phase2.py
python3 test/functional/rpc_getoracles_pending.py
```

Results: final commands passed. `feature_oracle_p2p.py` still logs expected `Method not found (-32601)` warnings for TODO RPCs and exits successfully, so it is retained as smoke coverage only.

**Rejected false positives / narrowed claims:**
- `GETORACLES` stale resend was not escalated above Low because receiver-side `ORACLEPRICE` handling rejects stale timestamps before storage/relay; the reachable harm is stale bandwidth/state churn.
- Duplicate oracle IDs inside `ORACLEBUNDLE` are already rejected by on-chain `ValidatePhaseTwoBundle()` duplicate checks. The live P2P handler may still store/relay malformed duplicate-ID bundles before block validation; no direct consensus or price-corruption path was proven in Wave 15.
- `ORACLECONSENSUS` seen-hash registration before full validation uses the capped manager seen set, unlike the local static bundle set, so it was not counted as a standalone bug.
- Pre-activation `GETORACLES` no-response was rejected as expected behavior because regtest oracle P2P gates at the configured oracle activation height.

**Theoretical / not yet reachable / test gaps:**
- `ORACLEBUNDLE` keeps a local static `seen_bundle_hashes` set and inserts bundle hashes before full validation. Existing peer misbehavior/rate limits bound single-peer damage, but a multi-peer resource-exhaustion proof was not built in Wave 15. Carry to Wave 18 resource-bound review.
- The `ORACLEBUNDLE` P2P handler does not use the same duplicate-ID validator as block validation before relay. Treat as relay hardening/test backlog unless a resource or network-amplification reproducer is proven.
- `AddConsensusAttestation()` overwrites same-oracle attestations without comparing freshness. Current block construction only uses attestations matching locally computed consensus values, so this remains a liveness/state-churn risk pending a concrete P2P reproducer.
- MuSig2 nonce and partial-signature P2P handlers can relay old positive epochs before the session layer drops them. Carry to Waves 16-18.
- `feature_oracle_p2p.py` and older `oracle_p2p_tests.cpp` sections include expected-failure/TODO assertions and should be converted into hard production assertions once those RPC/P2P hooks exist.

**ARCHITECTURAL_REVIEW_REQUIRED items:** none newly added in Wave 15.

**Agent A result summary:** no usable final report was returned after the Wave 15 assignment; main-agent reachability work produced `DD-RH-043`.

**Agent B result summary:** flagged P2P relay/resource hardening gaps for `ORACLEBUNDLE`, stale attestations, and old MuSig2 epoch relay; none were counted as confirmed value-integrity bugs without a reproducer.

**Agent C result summary:** confirmed stale `GETORACLES` resend (`DD-RH-044`), rejected fresh pending recovery/restart recovery as working after activation, and noted functional P2P tests that are currently false-green smoke coverage.

**Wave 15 status:** complete. Two bugs fixed (`DD-RH-043`, `DD-RH-044`), no new architecture-review item, no commits, scope stayed within DigiDollar/oracle.

## Wave 16 — MuSig2 Session Lifecycle and Epoch Binding

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave16_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: session lifecycle, nonce participant set, epoch/message binding.
- Agent B — Invariant and test-gap reviewer: stale nonces, wrong signers, timeout/retry edges.
- Agent C — Boundary reviewer: miner/oracle timing and cross-epoch functional flows.

**Scope note:** MuSig2 signing orchestrator/session lifecycle, nonce participant freeze, epoch-height binding, and MuSig2 P2P lifecycle tests only.

### DD-RH-045 — Early authenticated remote MuSig2 nonces were silently dropped

**Severity:** Medium

**Affected invariant:** Valid authenticated oracle nonces for the active or next epoch must be retained so Phase 3 MuSig2 sessions can reach the threshold participant set.

**Reachability path:** `ORACLEMUSIGNONCE` handling validates signature/range/epoch in `net_processing`, registers and relays the message, then calls `OracleSigningOrchestrator::IngestRemoteNonce()`. The orchestrator lazily created a session for an unseen epoch, but left it in `CREATED`; `MuSig2SigningSession::AddPubnonce()` only accepts `NONCES_COLLECTING` or `NONCES_COMPLETE`, so the first valid remote nonce was discarded after P2P dedup already marked it seen.

**Affected code:** lazy nonce session creation and passive initialization at `src/oracle/signing_orchestrator.cpp:81`-`src/oracle/signing_orchestrator.cpp:131`; state gate at `src/oracle/musig2_session.cpp:181`-`src/oracle/musig2_session.cpp:193`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=musig2_signing_orchestration_tests/remote_nonce_lazy_session_accepts_first_nonce
```

Failed with nonce count `0` and the session still outside `NONCES_COLLECTING`/`NONCES_COMPLETE`.

**Fix summary:** `IngestRemoteNonce()` now initializes a lazy `CREATED` session passively with the active roster aggregate key before deserializing and adding the remote nonce. The roster used for passive initialization is restricted to active IDs valid under `nOracleTotalOracles`.

**Regression:** signed regtest nonce helper at `src/test/musig2_signing_orchestration_tests.cpp:21`-`src/test/musig2_signing_orchestration_tests.cpp:73`; direct lazy-session regression at `src/test/musig2_signing_orchestration_tests.cpp:144`-`src/test/musig2_signing_orchestration_tests.cpp:161`; updated P2P lifecycle regression at `src/test/musig2_p2p_network_attacks_tests.cpp:501`-`src/test/musig2_p2p_network_attacks_tests.cpp:563`.

**Post-fix test evidence:** focused regression passed; final Wave 16 MuSig2 matrix passed.

**Commit status:** uncommitted.

### DD-RH-046 — Trimmed MuSig2 participants could re-enter before nonce aggregation

**Severity:** High

**Affected invariant:** The nonce participant set, aggregate key, message hash, partial signatures, and final signature must bind to the same exact signer set.

**Reachability path:** The orchestrator trims nonces to the threshold set, recomputes the aggregate key for that set, then calls `AggregateNonces()`. Before the fix, `TrimNoncesToThreshold()` did not freeze `m_pubnonces` and the public state remained `NONCES_COMPLETE`; a late `AddPubnonce()` for a trimmed oracle ID could reinsert an out-of-set nonce between trim/keyagg and nonce aggregation. That corrupts the aggregate nonce under a validator-reconstructed bitmap/keyagg mismatch and blocks valid Phase 3 signing.

**Affected code:** pre-fix `AddPubnonce()` accepted new IDs while `NONCES_COMPLETE`; fixed participant freeze at `src/oracle/musig2_session.cpp:181`-`src/oracle/musig2_session.cpp:193` and `src/oracle/musig2_session.cpp:251`-`src/oracle/musig2_session.cpp:268`; freeze state stored at `src/oracle/musig2_session.h:232`-`src/oracle/musig2_session.h:234`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=rh57_musig2_trim_aggregate_toctou_tests/rh57_trim_does_not_freeze_pubnonces
```

Failed because late `AddPubnonce()` calls for trimmed oracle IDs `4`, `5`, and `6` returned true and the nonce count grew back to `7` instead of remaining at threshold `4`.

**Fix summary:** `TrimNoncesToThreshold()` now freezes the participant set even when no erasure is needed; `AddPubnonce()` rejects any new pubnonce after the participant set is frozen. Move construction/assignment preserves the freeze bit.

**Regression:** late re-entry rejection in the end-to-end RH57 aggregate proof at `src/test/rh57_musig2_trim_aggregate_toctou_tests.cpp:284`-`src/test/rh57_musig2_trim_aggregate_toctou_tests.cpp:306`; direct freeze invariant at `src/test/rh57_musig2_trim_aggregate_toctou_tests.cpp:410`-`src/test/rh57_musig2_trim_aggregate_toctou_tests.cpp:424`.

**Post-fix test evidence:** `rh57_musig2_trim_aggregate_toctou_tests`, `musig2_signing_orchestration_tests`, and `musig2_p2p_network_attacks_tests` passed after the fix; final Wave 16 MuSig2 matrix passed.

**Commit status:** uncommitted.

### DD-RH-047 — Lazy MuSig2 sessions used a hardcoded 50-block epoch height for timeout binding

**Severity:** Medium

**Affected invariant:** MuSig2 session timeout state must bind to the active chain's configured oracle epoch length, not a testnet constant.

**Reachability path:** Lazy sessions created from remote nonce or partial-signature arrival set `creation_height` to `epoch * 50`. Mainnet oracle epoch length is not 50, so a valid lazy-created session could be considered old and failed when `OnBlockConnected()` ticked the real epoch start height. That turns valid fast P2P messages into a per-epoch liveness failure on networks whose `nDDOracleEpochBlocks` differs from 50.

**Affected code:** chain-parameter helper at `src/oracle/signing_orchestrator.cpp:26`-`src/oracle/signing_orchestrator.cpp:34`; nonce lazy creation at `src/oracle/signing_orchestrator.cpp:89`-`src/oracle/signing_orchestrator.cpp:97`; partial-signature lazy creation at `src/oracle/signing_orchestrator.cpp:140`-`src/oracle/signing_orchestrator.cpp:149`; generic fallback creation at `src/oracle/signing_orchestrator.cpp:304`-`src/oracle/signing_orchestrator.cpp:309`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=musig2_signing_orchestration_mainnet_tests/remote_nonce_lazy_session_timeout_uses_chain_epoch_length
```

Failed before the production fix because a mainnet-params lazy session used `epoch * 50`; ticking the real epoch-start height moved the session to `FAILED`.

**Fix summary:** `OracleSigningOrchestrator` now derives fallback creation height from `Params().GetConsensus().nDDOracleEpochBlocks` and uses that helper for nonce-lazy, partial-lazy, and generic fallback session creation.

**Regression:** mainnet-params timeout-binding test at `src/test/musig2_signing_orchestration_tests.cpp:165`-`src/test/musig2_signing_orchestration_tests.cpp:197`.

**Post-fix test evidence:** focused mainnet regression passed; final Wave 16 MuSig2 matrix passed.

**Commit status:** uncommitted.

**Wave 16 command matrix:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' | sort
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=musig2_signing_orchestration_tests/remote_nonce_lazy_session_accepts_first_nonce
./src/test/test_digibyte --run_test=musig2_signing_orchestration_tests
./src/test/test_digibyte --run_test=musig2_signing_orchestration_mainnet_tests
./src/test/test_digibyte --run_test=rh57_musig2_trim_aggregate_toctou_tests/rh57_trim_does_not_freeze_pubnonces
./src/test/test_digibyte --run_test=rh57_musig2_trim_aggregate_toctou_tests
./src/test/test_digibyte --run_test=musig2_p2p_ingestion_tests
./src/test/test_digibyte --run_test=musig2_orchestrator_exploits_tests
./src/test/test_digibyte --run_test=musig2_p2p_network_attacks_tests
./src/test/test_digibyte --run_test=musig2_session_tests
./src/test/test_digibyte --run_test=musig2_bundle_manager_tests
```

Results: final commands passed. The pre-fix focused regressions for `DD-RH-045`, `DD-RH-046`, and `DD-RH-047` failed for the intended reasons before their fixes and passed after the fixes.

**Rejected false positives / narrowed claims:**
- Legacy `OracleBundleManager::StartMuSig2Session()` still contains an older unsigned nonce broadcast path, but no production caller was found. Current live Phase 3 production flow uses `OracleSigningOrchestrator::TickEpochSession()` and signs nonce/partial messages before broadcast.
- Cross-epoch v0x03 aggregate replay is blocked by `ValidatePhaseThreeBundle()` binding epoch, price, timestamp, bitmap-derived aggregate key, and `ComputeOracleMessageHash(epoch, price, timestamp)`.
- Partial signatures from nonparticipants are rejected by `AddPartialSignatureVerified()` when the oracle ID is not in the frozen nonce participant set.
- The direct `IngestRemoteNonce()` API trusts its caller; the external P2P boundary authenticates `ORACLEMUSIGNONCE` before calling it. No unauthenticated external nonce path was proven in Wave 16.

**Theoretical / not yet reachable / test gaps:**
- `m_pending_partialsigs` is still a buffered partial-signature path with bounded retention and stale pruning, but no replay/drain path on transition to `SIGNING`. This is a liveness/test-gap candidate for Wave 17 because a valid early partial-signature reproducer was not built in Wave 16.
- MuSig2 nonce/partial P2P epoch checks allow positive old epochs until session cleanup drops them. Existing auth, rate limiting, and session cleanup bound the impact; carry to Wave 18 resource-bound review unless a stronger replay reproducer is found.
- The participant freeze fix preserves state as `NONCES_COMPLETE` while freezing internally. A future refactor could make this explicit in the public state machine, but no architectural change is required for the narrow fix.

**ARCHITECTURAL_REVIEW_REQUIRED items:** none newly added in Wave 16.

**Agent A result summary:** confirmed early remote nonce loss and hardcoded epoch-height timeout binding; flagged pending partial-sig replay absence for the next MuSig2 wave.

**Agent B result summary:** confirmed the RH57 participant-set freeze bug and mapped stale/wrong-signer tests; no consensus-safety bypass was proven beyond the fixed liveness bug.

**Agent C result summary:** confirmed miner/oracle timing exposure from early nonce loss, rejected v0x03 cross-epoch replay as already bound by message hash and bitmap validation, and carried old-epoch P2P relay to resource review.

**Wave 16 status:** complete. Three bugs fixed (`DD-RH-045`, `DD-RH-046`, `DD-RH-047`), no new architecture-review item, no commits, scope stayed within DigiDollar/oracle.

## Wave 17 — MuSig2 Serialization, Bitmaps, Partial Sigs, Replay

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave17_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: bitmap/order/serialization/aggregate verification edges.
- Agent B — Invariant and test-gap reviewer: malformed partial sigs and aggregate bundle regressions.
- Agent C — Boundary reviewer: end-to-end bundle acceptance/rejection matrix.

**Scope note:** MuSig2 bitmap encoding/decoding, v0x03 aggregate bundle validation, MuSig2 P2P message serialization/replay, and related tests only.

### DD-RH-048 — v0x03 aggregate bundles accepted non-canonical bitmaps with unused high bits set

**Severity:** Medium

**Affected invariant:** The MuSig2 participation bitmap must have one canonical encoding for the exact signer set that validators use to reconstruct the aggregate key.

**Reachability path:** For a roster whose oracle count is not a multiple of 8, `DecodeBitmap()` iterated only through `total_oracles` bits and ignored unused high bits in the final byte. A valid v0x03 aggregate signature over signer set `{0,1,2,3}` could be replayed with an unused final-byte bit set. `ValidatePhaseThreeBundle()` decoded the same signer set, recomputed the same aggregate key, and accepted the same signature. Meanwhile extraction/user surfaces that decode using a bitmap-size-derived total can expose phantom participant IDs.

**Affected code:** canonical bitmap rejection at `src/oracle/musig2_aggregator.cpp:88`-`src/oracle/musig2_aggregator.cpp:110`; validator uses decoded bitmap at `src/oracle/bundle_manager.cpp:2793`-`src/oracle/bundle_manager.cpp:2832`.

**Pre-fix failing evidence:**

```bash
make -C src -j2 test/test_digibyte && ./src/test/test_digibyte --run_test=musig2_bundle_manager_tests/validate_v03_rejects_unused_bitmap_bits
```

Failed because `ValidatePhaseThreeBundle()` returned true after bit 7 was set in a regtest 7-oracle bitmap; the aggregate signature still verified under the decoded signer set.

**Fix summary:** `MuSig2OracleAggregator::DecodeBitmap()` now rejects nonzero unused bits in the final bitmap byte whenever `nOracleTotalOracles % 8 != 0`, eliminating alternate encodings for the same signer set.

**Regression:** direct decoder regression at `src/test/musig2_aggregator_tests.cpp:69`-`src/test/musig2_aggregator_tests.cpp:76`; signed v0x03 validation regression at `src/test/musig2_bundle_manager_tests.cpp:257`-`src/test/musig2_bundle_manager_tests.cpp:281`.

**Post-fix test evidence:** focused regressions passed. Final Wave 17 MuSig2 serialization/P2P matrix passed.

**Commit status:** uncommitted.

**Wave 17 command matrix:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' | sort
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=musig2_bundle_manager_tests/validate_v03_rejects_unused_bitmap_bits
./src/test/test_digibyte --run_test=musig2_aggregator_tests/test_bitmap_rejects_unused_high_bits
./src/test/test_digibyte --run_test=musig2_aggregator_tests
./src/test/test_digibyte --run_test=musig2_bundle_manager_tests
./src/test/test_digibyte --run_test=musig2_bundle_creation_tests
./src/test/test_digibyte --run_test=musig2_bundle_format_tests
./src/test/test_digibyte --run_test=musig2_p2p_message_tests
./src/test/test_digibyte --run_test=musig2_p2p_ingestion_tests
./src/test/test_digibyte --run_test=musig2_p2p_network_attacks_tests
./src/test/test_digibyte --run_test=musig2_orchestrator_exploits_tests
./src/test/test_digibyte --run_test=rh55_musig2_partial_sig_unverified_aggregation_tests
./src/test/test_digibyte --run_test=rh57_musig2_trim_aggregate_toctou_tests
./src/test/test_digibyte --run_test=rh56_oversized_bitmap_message_inflation_tests
./src/test/test_digibyte --run_test=rh05_bundle_validation_attacks
./src/test/test_digibyte --run_test=digidollar_rh40_regression_tests
```

Results: final commands passed. The pre-fix signed regression for `DD-RH-048` failed for the intended reason and passed after the decoder fix.

**Rejected false positives / narrowed claims:**
- Malformed v0x03 aggregate signatures with a correct 64-byte length are rejected by `ValidatePhaseThreeBundle()` Schnorr verification at `src/oracle/bundle_manager.cpp:2837`-`src/oracle/bundle_manager.cpp:2848`; existing RH05 forged-signature coverage passed.
- Empty, all-zero, below-threshold, and oversized bitmaps are rejected by decode/threshold/signature validation; RH05 and RH56 coverage passed after the canonical-bitmap fix.
- Bitmap/order mismatch with a valid aggregate signature for a different signer set does not pass validation because validators recompute the aggregate key from the submitted bitmap before Schnorr verification.
- MuSig2 nonce/partial message auth hashes bind epoch, oracle ID, and payload bytes, and P2P verifies the wrapper signature before relay/ingest; no unauthenticated replay path was proven in Wave 17.
- Raw RH55 comments claiming the production P2P path uses unverified partial signatures are stale; current `OracleBundleManager::ProcessRemoteMusigPartialSig()` calls `AddPartialSignatureVerified()`.

**Theoretical / not yet reachable / test gaps:**
- Authenticated old positive MuSig2 epochs still pass the P2P lower-bound check and can be relayed/registered before session cleanup drops or ignores them. This is a resource/replay-hardening candidate for Wave 18; no value-integrity impact was proven in Wave 17.
- `m_pending_partialsigs` has bounded retention and stale pruning, but still has no replay/drain on transition to `SIGNING`. Carry as a liveness test gap unless a valid early-partial reproducer is built.
- Functional tests have little to no v0x03 bundle/P2P/restart/reorg coverage; Wave 17 relied on unit/regression coverage.
- Malformed `OP_ORACLE` payloads that fail extraction still fall under existing `ARCH-RH-001` liveness/strictness decision. No consensus strictness change was applied without Jared approval.

**ARCHITECTURAL_REVIEW_REQUIRED items:** none newly added in Wave 17; `ARCH-RH-001` was reaffirmed by Agent C for malformed v0x03 extraction failures.

**Agent A result summary:** no usable final report was returned after the Wave 17 assignment; main-agent reachability work produced `DD-RH-048`.

**Agent B result summary:** confirmed the old-positive-epoch P2P relay gap and several missing negative tests, including signed-message fuzz shape, same-payload/different-auth dedup, wrong-bitmap signed aggregate validation, and live-path partial-sig poisoning coverage.

**Agent C result summary:** reaffirmed `ARCH-RH-001` for malformed oracle-output escape hatches, rejected forged aggregate signatures and common bitmap malformations as covered, and documented the functional v0x03 coverage gap.

**Wave 17 status:** complete. One bug fixed (`DD-RH-048`), no new architecture-review item, no commits, scope stayed within DigiDollar/oracle.

## Wave 18 — Resource Bounds and Stability

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave18_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: unbounded maps/vectors, expensive validation, logging hot paths.
- Agent B — Invariant and test-gap reviewer: malformed resource-heavy DD/oracle fuzz and unit coverage.
- Agent C — Boundary reviewer: RPC/P2P/Qt/wallet responsiveness and malformed-input failure modes.

**Scope note:** DD/oracle/MuSig2 resource bounds, P2P relay state, pending buffers, parser amplification, wallet/RPC/Qt responsiveness.

### DD-RH-049 — ORACLEBUNDLE dedup set grew before validation and had no size cap

**Severity:** Medium

**Affected invariant:** Malformed or unauthenticated oracle P2P inputs must not grow unbounded node memory before validation, signature checks, and rate limiting.

**Reachability path:** The `ORACLEBUNDLE` P2P handler deserialized an `OracleBundleMsg`, hashed it, and inserted the hash into a local static `seen_bundle_hashes` set before oversized-bundle rejection, message signature verification, consensus/epoch validation, or the 50/hour peer rate limiter. The set had no size cap and was only cleared every 7200 seconds. Unique malformed bundles therefore retained one `uint256` each even when immediately rejected.

**Affected code:** pre-fix local static set at `src/net_processing.cpp:5617`-`src/net_processing.cpp:5633`; fixed dedup route through capped `OracleBundleManager::RegisterSeenHash()` at `src/net_processing.cpp:5617`-`src/net_processing.cpp:5625`; cap implementation at `src/oracle/bundle_manager.cpp:1464`-`src/oracle/bundle_manager.cpp:1479`.

**Pre-fix evidence:** source-level reachability confirmed from the live handler order: insertion occurred before all validation/rate limiting and the local static set had no cap. A full `PeerManager::ProcessMessage()` memory regression was not added in Wave 18; the added regression guards the capped store now used by the handler.

**Fix summary:** `ORACLEBUNDLE` duplicate tracking now uses the shared oracle seen-hash store, which is capped to 2048 entries and already used by other oracle P2P message paths. This removes the unbounded two-hour local static set while preserving best-effort duplicate suppression.

**Regression:** capped untrusted P2P hash regression at `src/test/oracle_bundle_manager_tests.cpp:370`-`src/test/oracle_bundle_manager_tests.cpp:383`.

**Post-fix test evidence:** focused cap regression passed; final Wave 18 unit and functional matrix passed.

**Commit status:** uncommitted.

**Wave 18 command matrix:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' | sort
make -C src -j2 test/test_digibyte
./src/test/test_digibyte --run_test=oracle_bundle_manager_tests/register_seen_hash_caps_untrusted_p2p_hashes
./src/test/test_digibyte --run_test=oracle_bundle_manager_tests
./src/test/test_digibyte --run_test=oracle_p2p_tests
./src/test/test_digibyte --run_test=rh58_pending_partialsigs_unbounded_growth_tests
./src/test/test_digibyte --run_test=digidollar_hot_path_logging_tests
./src/test/test_digibyte --run_test=rh56_oversized_bitmap_message_inflation_tests
./src/test/test_digibyte --run_test=digidollar_rh46_rpc_input_validation_tests
./src/test/test_digibyte --run_test=digidollar_rpc_tests
./src/test/test_digibyte --run_test=musig2_p2p_ingestion_tests
./src/test/test_digibyte --run_test=musig2_p2p_network_attacks_tests
python3 test/functional/feature_oracle_p2p.py
python3 test/functional/rpc_getoracles_pending.py
```

Results: final commands passed. `feature_oracle_p2p.py` still logs expected `Method not found (-32601)` warnings for TODO RPCs and exits successfully. One accidental unit invocation with `--run_test=feature_oracle_p2p` returned "no test cases matching filter"; it was replaced by the direct functional command above.

**Rejected false positives / narrowed claims:**
- `GETORACLES` amplification is bounded by activation gating, 10/min/peer rate limiting, a 24-epoch request window, one pending message per oracle ID, and stale/future filtering.
- MuSig2 pending partial signatures are bounded by 32 entries per epoch and 8 epochs, and stale epochs are pruned in `CleanupOldSessions()`. The missing replay/drain remains a liveness gap, not an unbounded memory bug after the cap.
- MuSig2 aggregate pubkey cache is capped and evicts when full.
- `ORACLEBUNDLE` relay after validation is bounded by signature checks, consensus/epoch checks, and no relay when `AddOracleMessage()` stores nothing new.
- Hot-path DigiDollar RPC logging tests passed; no reachable log-amplification crash was proven.

**Theoretical / not yet reachable / test gaps:**
- `ExtractOracleBundle()` can still synthesize many v0x03 synthetic messages from oversized bitmaps before `ValidatePhaseThreeBundle()` rejects them. Tightening extraction without changing block-validation semantics intersects `ARCH-RH-001`, because returning extraction failure currently makes malformed oracle outputs pass as transition/no-data blocks. Carry as parser hardening plus architecture-review coupling.
- Authenticated old positive MuSig2 epochs can still pass P2P lower-bound checks and be relayed/registered before session cleanup. Requires a valid oracle key and is rate-limited; carry as Low resource/replay hardening unless a stronger reproducer is built.
- DD wallet/RPC list surfaces apply output caps after materializing full histories/position/address state. This is a local responsiveness issue for large wallets; fixing it requires pagination in wallet-facing providers.
- Fuzz coverage still under-targets adversarial OP_RETURN chunk framing and oversized signed MuSig2 partial-message vectors.

**ARCHITECTURAL_REVIEW_REQUIRED items:** none newly added in Wave 18. Parser hardening for malformed oracle outputs remains coupled to existing `ARCH-RH-001`.

**Agent A result summary:** no usable final report was returned after the Wave 18 assignment; main-agent and Agent C independently confirmed `DD-RH-049`.

**Agent B result summary:** confirmed the RH56 extraction amplification surface and identified missing fuzz/cap/pagination regressions; no new consensus over-acceptance was proven.

**Agent C result summary:** confirmed `DD-RH-049`, old authenticated MuSig2 epoch relay as Low resource/replay hardening, and large-wallet RPC/Qt materialization as a local responsiveness risk; rejected GETORACLES and pending-partial unbounded-growth claims under current caps.

**Wave 18 status:** complete. One bug fixed (`DD-RH-049`), no new architecture-review item, no commits, scope stayed within DigiDollar/oracle.

## Wave 19 — Fuzz Coverage Expansion

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave19_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: parser/validation surfaces missing fuzz coverage for DD/oracle and prior RH edges.
- Agent B — Invariant and test-gap reviewer: bitmap canonicalization, v0x03 deserialize/validate, signed MuSig2 nonce/partial message shapes, and OP_RETURN parser coverage.
- Agent C — Boundary reviewer: practical focused fuzz smoke commands and corpus/run constraints.

**Scope note:** DigiDollar/oracle/MuSig2 fuzz harnesses and smoke execution only.

**Confirmed bugs:** none. Wave 19 improved coverage for already-fixed issues and did not prove a new production bug.

**Tests added or upgraded:**
- `src/test/fuzz/oracle_musig2_bitmap.cpp:64`-`src/test/fuzz/oracle_musig2_bitmap.cpp:89` now fuzzes `MuSig2OracleAggregator::DecodeBitmap()` directly and asserts sorted IDs, in-range IDs, canonical re-encoding, and rejection of unused high bits in the final byte. This covers the DD-RH-048 class beyond unit regressions.
- `src/test/fuzz/oracle_musig2_nonce_msg.cpp:39`-`src/test/fuzz/oracle_musig2_nonce_msg.cpp:72` now includes the authenticated `signature` vector in deserialize/serialize round trips and exercises `GetSignatureHash()`.
- `src/test/fuzz/oracle_musig2_partialsig_msg.cpp:38`-`src/test/fuzz/oracle_musig2_partialsig_msg.cpp:70` now includes the authenticated `signature` vector, extends partial-signature length fuzzing to 128 bytes, and exercises `GetSignatureHash()`.

**Fixes landed:** no production fix in Wave 19.

**Commit status:** uncommitted at the time of this wave entry. Jared subsequently authorized local commits; these fuzz harness changes are planned as a coverage commit separate from production bug-fix commits.

**Wave 19 command matrix:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' | sort
make -C src -j2 test/fuzz/fuzz
PRINT_ALL_FUZZ_TARGETS_AND_ABORT=1 src/test/fuzz/fuzz 2>/tmp/rh_fuzz_targets.err | grep -Ei 'oracle|digidollar|musig2' | sort
FUZZ=oracle_musig2_bitmap src/test/fuzz/fuzz -runs=1000
FUZZ=musig2_nonce_message src/test/fuzz/fuzz -runs=1000
FUZZ=musig2_partialsig_message src/test/fuzz/fuzz -runs=1000
for n in 0 1 2 4 8 16 32 64 128 256; do head -c "$n" /dev/urandom | FUZZ=oracle_musig2_bitmap src/test/fuzz/fuzz >/dev/null || exit 1; done
for n in 0 1 2 4 8 16 32 64 128 256; do head -c "$n" /dev/urandom | FUZZ=musig2_nonce_message src/test/fuzz/fuzz >/dev/null || exit 1; done
for n in 0 1 2 4 8 16 32 64 128 256; do head -c "$n" /dev/urandom | FUZZ=musig2_partialsig_message src/test/fuzz/fuzz >/dev/null || exit 1; done
for n in 0 1 2 4 8 16 32 64 128 256; do head -c "$n" /dev/urandom | FUZZ=oracle_musig2_bundle src/test/fuzz/fuzz >/dev/null || exit 1; done
for n in 0 1 2 4 8 16 32 64 128 256; do head -c "$n" /dev/urandom | FUZZ=oracle_musig2_aggregation src/test/fuzz/fuzz >/dev/null || exit 1; done
for n in 0 1 2 4 8 16 32 64 128 256; do head -c "$n" /dev/urandom | FUZZ=musig2_session_state src/test/fuzz/fuzz >/dev/null || exit 1; done
```

Results: fuzz binary rebuilt successfully; registered DD/oracle/MuSig2 targets in this build were `musig2_nonce_message`, `musig2_partialsig_message`, `musig2_session_state`, `oracle_musig2_aggregation`, `oracle_musig2_bitmap`, and `oracle_musig2_bundle`. The `-runs=1000` invocations failed because this build provides the standalone fuzz main and treats non-input arguments as corpus file paths; the valid stdin smoke commands for all six registered DD/oracle/MuSig2 targets passed.

**Rejected false positives / narrowed claims:**
- The `-runs=1000` failures were harness-mode mismatches, not target crashes. The standalone main expects stdin or corpus paths unless the binary is built with libFuzzer support.
- Source files for `oracle_script_extract`, `oracle_script_create_roundtrip`, `oracle_bundle_validation`, `oracle_bundle_v03_roundtrip`, and several DigiDollar fuzz targets exist, but they are not registered in the current `src/test/fuzz/fuzz` binary observed in Wave 19. This is a build/registration coverage gap, not evidence of a production bug.

**Theoretical / not yet reachable / test gaps:**
- OP_RETURN oracle parser fuzzing still needs the relevant targets registered in the local fuzz binary before practical smoke coverage can be claimed.
- The current smoke commands execute deterministic harness paths but are not a substitute for sanitizer-backed libFuzzer campaigns over seeded corpora.
- The DD-RH-049 P2P dedup memory bug is guarded by a unit regression, but a full `PeerManager::ProcessMessage()` fuzz target that directly exercises `ORACLEBUNDLE` malformed-message retention would improve coverage.

**ARCHITECTURAL_REVIEW_REQUIRED items:** none newly added in Wave 19.

**Agent A result summary:** no usable final report was returned after the Wave 19 assignment; main-agent coverage work targeted DD-RH-048/DD-RH-049-adjacent fuzz gaps.

**Agent B result summary:** no usable final report was returned after the Wave 19 assignment; the added harness assertions cover the signed-message and bitmap-canonicalization gaps identified in prior waves.

**Agent C result summary:** no usable final report was returned after the Wave 19 assignment; main-agent harness inspection found the standalone fuzz-main invocation constraint and adjusted smoke commands accordingly.

**Wave 19 status:** complete. No new production bug, fuzz coverage upgraded, no new architecture-review item, commits pending under Jared's new authorization, scope stayed within DigiDollar/oracle.

## Wave 20 — Final Bug-Chain Sweep and Severity Summary

**Ledger path:** `reports/red_hornet_ledger.md`

**Enumeration command:**

```bash
find src/digidollar src/oracle src/wallet src/rpc src/qt src/test src/test/fuzz test/functional -type f \
  | grep -Ei 'digidollar|oracle|musig2|rh|red|wallet|qt' \
  | sort
```

**Enumeration result:** completed; 1895 matching paths recorded at `/tmp/red_hornet_wave20_surface.txt`.

**Assignments launched:**
- Agent A — Reachability prober: compound-path sweep across confirmed bugs and adjacent production paths.
- Agent B — Invariant and test-gap reviewer: final matrix, log review, and missing negative cases.
- Agent C — Boundary reviewer: final report, false positives, theoretical risks, hardening backlog.

**Scope note:** stayed inside DigiDollar/oracle and directly gated wallet/RPC/Qt/consensus/P2P/fuzz surfaces. Two direct-run generic functional scripts (`feature_assumeutxo.py`, `feature_assumevalid.py`) were observed failing but are outside the DigiDollar/oracle campaign scope and are not part of the configured functional runner.

### New confirmed bug

**DD-RH-050 — Medium — Redemption RPC mutated wallet DD state after mempool rejection**

- **Affected invariant:** A redemption cannot hide, erase, or mark DD/collateral state redeemed unless the redemption transaction is valid and accepted.
- **Reachability path:** `test/functional/digidollar_transactions.py` minted DD, transferred 1,000 cents, minted a second 15,000-cent vault, then redeemed the second vault. The wallet selected two DD inputs with 9,000 cents change. Validation read stale script-metadata amounts before source transaction data, so mempool rejected the redemption as a partial burn. The RPC still removed selected DD UTXOs, tracked fake change, marked the position inactive, and returned success.
- **Exact code references:** stale amount priority at `src/digidollar/validation.cpp:1475`; unsafe wallet commit/state mutation gate at `src/rpc/digidollar.cpp:1846`; reproducer assertion at `test/functional/digidollar_transactions.py:150`.
- **Evidence before fix:** `python3 test/functional/test_runner.py` failed `147/316 - digidollar_transactions.py` with `AssertionError: not(0 == 9000)`. Node logs showed mempool rejection `bad-collateral-release-partial-burn` followed by local DD UTXO erasure and position closure.
- **Fix summary:** redemption validation now reads the authoritative creating transaction via txindex/block-db before falling back to the script metadata registry; `redeemdigidollar` now requires `chain().broadcastTransaction()` success before `CommitTransaction()` and before DD UTXO/position/history mutation.
- **Regression evidence:** `python3 test/functional/digidollar_transactions.py` passed after the fix; the full configured functional runner later passed all 316 tests.
- **Commit:** `87051c5999 digidollar redeem: fix DD-RH-050 failed redemption state mutation`.
- **Status:** fixed and committed.

### New test-gap repair

**DD-RH-051 — Low / test coverage — Extra DigiDollar/oracle functional scripts were stale and not runnable**

- **Affected invariant:** Audit-only and regression tests must be runnable so wallet/RPC/oracle boundary regressions remain visible.
- **Reachability path:** the configured functional runner warned that multiple DigiDollar/oracle scripts were not in `test_runner.py`; direct execution found four scoped failures caused by stale test assumptions.
- **Exact code references:** real-position setup at `test/functional/digidollar_rpc_redemption.py:27`; regtest max mint coverage at `test/functional/digidollar_rpc_estimate.py:48`; watch-only assert repair at `test/functional/digidollar_watchonly_rescan.py:112`; oracle consistency assert repair at `test/functional/digidollar_oracle_consistency.py:79`.
- **Fix summary:** use a live minted position for `getredemptioninfo`; match regtest's $1,000 maximum mint amount; stop passing message strings to helpers that treat extra args as compared values; keep oracle consistency checks but use plain asserts for custom messages.
- **Regression evidence:** four focused scripts passed, then the scoped extra direct run passed 34/34.
- **Commit:** `85c93a4fdc test digidollar: repair DD-RH-051 extra functional coverage`.
- **Status:** fixed and committed.

### Commit conversion after Jared authorization

Jared explicitly authorized local commits after earlier waves had recorded fixes as uncommitted. The work was split by individual fix where practical and by tightly coupled subsystem where one shared production/test surface closed multiple Red Hornet IDs:

| Commit | Scope |
|---|---|
| `4740af7559` | `DD-RH-003` DCA fractional multiplier rounding |
| `c3b61a3546` | `DD-RH-004` collateral satoshi rounding |
| `a35cdb7783` | `DD-RH-001`, `DD-RH-005`, `DD-RH-007`, `DD-RH-010`, `DD-RH-011` consensus/validation fixes |
| `74a0f07df2` | `DD-RH-012` through `DD-RH-020` wallet accounting/recovery fixes |
| `19a2df1db3` | `DD-RH-006`, `DD-RH-008`, `DD-RH-009`, `DD-RH-021` through `DD-RH-028`, `DD-RH-036`, `DD-RH-042` RPC fixes |
| `d8b9d3be5f` | `DD-RH-029` through `DD-RH-033` Qt safety display fixes |
| `b0dc484897` | `DD-RH-035`, `DD-RH-038` through `DD-RH-049` oracle/MuSig2/P2P fixes |
| `b1b70b5b30` | Wave 19 fuzz coverage expansion |
| `305dd806ca` | Test expectation repair for `DD-RH-010` context-free block validation split |
| `584b8372aa` | Wallet test fixture repair for `DD-RH-012` live chain-state validation |
| `87051c5999` | `DD-RH-050` redemption state mutation fix |
| `85c93a4fdc` | `DD-RH-051` extra functional coverage repair |

### Final command matrix

```bash
make -j2 check
python3 test/functional/test_runner.py
python3 test/functional/digidollar_rpc_redemption.py && python3 test/functional/digidollar_rpc_estimate.py && python3 test/functional/digidollar_watchonly_rescan.py && python3 test/functional/digidollar_oracle_consistency.py
# Direct scoped extra DigiDollar/oracle scripts omitted by test_runner.py:
# 34 scripts, run one by one under timeout 600.
PRINT_ALL_FUZZ_TARGETS_AND_ABORT=1 src/test/fuzz/fuzz 2>&1 | rg 'digidollar|oracle|musig|dd_'
# 29 compiled DD/oracle/MuSig2 fuzz targets, four stdin sizes each: 0, 1, 32, 256 bytes.
```

**Results:**
- `make -j2 check`: passed after the final production fix.
- `python3 test/functional/test_runner.py`: passed, 316/316 configured tests, 17 expected skips, runtime 609 seconds.
- Focused `digidollar_transactions.py`: passed after `DD-RH-050`.
- Four repaired extra scripts: passed.
- Scoped extra direct DigiDollar/oracle scripts: passed, 34/34.
- Fuzz smoke: passed, 29 compiled DD/oracle/MuSig2 targets x 4 stdin sizes.
- Direct run of the full warning list initially showed 30/36 pass. The four scoped DigiDollar/oracle failures were fixed in `DD-RH-051`; the remaining two failures (`feature_assumeutxo.py`, `feature_assumevalid.py`) are generic unlisted tests outside the campaign scope and were not modified.

### Rejected false positives / narrowed claims

- The full-suite `digidollar_transactions.py` failure was not a flaky test; it produced `DD-RH-050` and was fixed.
- The four scoped extra functional failures were stale test assumptions or helper misuse, not new production bugs. They are recorded as `DD-RH-051` because the tests were useful but not runnable.
- `test_runner.py` refusing explicit unlisted scripts is a harness configuration limitation, not a node bug. Direct script execution was used for evidence.
- The generic `feature_assumeutxo.py` and `feature_assumevalid.py` failures are outside DigiDollar/oracle scope. They were recorded but not fixed in this campaign.

### Theoretical / not yet reachable / architecture-review carryover

- `ARCH-RH-001`: post-activation missing/unparseable `OP_ORACLE` handling still needs a design decision for liveness versus strict block rejection.
- `ARCH-RH-002` / `DD-RH-002`: `skipOracleValidation` during IBD/catch-up remains an explicit consensus architecture decision. Not fixed without Jared approval.
- `ARCH-RH-003`: watch-only DigiDollar address storage/rescan/listing model needs a wallet design decision.
- `DD-RH-034`: Qt mint owner-key generation/storage timing needs a wallet-storage/HD-key design decision.
- `DD-RH-037`: mainnet oracle roster size versus 17-slot bitmap design needs a protocol decision.
- `getredemptioninfo(position, amount)` still reports a capped `redeemable_dd` even though actual redemption requires exact full-vault amount. This is preserved as a Low UX/RPC-schema hardening item; no production fix was made during Wave 20 because changing the RPC contract needs review.

**Agent A result summary:** no final result was available before synthesis completed; main agent finished the compound-path sweep locally and found `DD-RH-050`.

**Agent B result summary:** no final result was available before synthesis completed; main agent finished the final matrix locally and found/closed `DD-RH-051`.

**Agent C result summary:** returned final-report structure and open-risk summary; content was integrated into the final package.

**Wave 20 status:** complete. `DD-RH-050` fixed, `DD-RH-051` test coverage repaired, final configured and scoped DD/oracle suites passed, no new architecture item added, scope stayed within DigiDollar/oracle.
