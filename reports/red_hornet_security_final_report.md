# Red Hornet DigiDollar Security Campaign Final Report

Date: April 29, 2026
Repository: `/home/jared/Code/digibyte`
Branch: `feature/digidollar-v1`
Ledger: `reports/red_hornet_security_ledger.md`

## Executive Summary

The second 20-wave Red Hornet campaign is complete. Scope stayed inside DigiDollar/oracle and directly gating shared code: wallet, RPC, miner, validation, index, P2P, Qt, and fuzz/test harnesses.

The campaign fixed 40 confirmed reachable vulnerabilities or security-relevant bugs, left 4 confirmed implementation bugs open, and preserved 10 architecture/protocol/storage decisions for Jared approval. Every production fix was committed separately by vulnerability or tightly coupled fix. No push was performed.

The full post-fix test matrix passed, with one explicitly recorded exploratory legacy-wallet compatibility failure: `digidollar_basic.py --legacy-wallet` cannot generate a bech32m DigiDollar address. That is not counted as a fixed vulnerability; it needs a product/protocol decision on legacy wallet support for DD receive/mint.

## Fixed Vulnerabilities

| ID | Severity | Area | Commit |
|---|---:|---|---|
| DD-RH-052 | High | Block validation oracle fallback | `d0606db668` |
| DD-RH-053 | Medium | Wallet DD lock bypass | `1d081a8b79` |
| DD-RH-054 | Medium | Pending mint status/redeemability | `a4d4813389` |
| DD-RH-056 | High | DCA validator/wallet policy split | `d3078dd9e7` |
| DD-RH-060 | Medium | Invalid mint volatility poisoning | `6d5ea56332` |
| DD-RH-061 | Medium | Mint OP_RETURN type mismatch | `c677a8056b` |
| DD-RH-062 | High | Redemption OP_RETURN ambiguity | `55c2e95f1a` |
| DD-RH-065 | Medium | Miner DD template validation | `6b481044b2` |
| DD-RH-066 | Medium | Oracle cache reorg rollback | `f54bf8c59d` |
| DD-RH-067 | Medium | Wallet transfer reorg restore | `99b3c618a7` |
| DD-RH-068 | Medium | Stats-index reorg vault metadata | `76d4c1c852` |
| DD-RH-070 | Medium | DD dust-policy bypass | `3d77327ef8` |
| DD-RH-071 | Medium | Foreign mint rescan claim | `91f5c751d6` |
| DD-RH-072 | Medium | Unsafe redemption preflight | `dab712c2c5` |
| DD-RH-073 | High | Non-DD metadata wallet credit | `78641061dc` |
| DD-RH-074 | High | Pending redeem state corruption | `9386619eb2` |
| DD-RH-076 | High | Redemption ignored requested address | `40f5ce8d1d` |
| DD-RH-077 | Medium | Cross-network DD address validation | `59019bac2f` |
| DD-RH-078 | Low | Named RPC option defaults | `18364bbe9f` |
| DD-RH-079 | Low | Redeem amount parsing mismatch | `e7460b6d3d` |
| DD-RH-080 | High | Qt watch-only receive addresses | `037b11ef15` |
| DD-RH-081 | High | Qt recoverable mint owner keys | `f076874896` |
| DD-RH-082 | High | Oracle epoch roster validation | `fe05ecfc60` |
| DD-RH-083 | Medium | MuSig2 bitmap extraction bounds | `da6b541ba4` |
| DD-RH-087 | Medium | Stale pending oracle messages | `f6c4f77543` |
| DD-RH-088 | Low | Stale oracle RPC status | `2217a58061` |
| DD-RH-089 | Medium | Stale template oracle bundles | `e81d5ccbdc` |
| DD-RH-090 | Low | Duplicate signer consensus count | `f445b6fc0b` |
| DD-RH-091 | Medium | Attestation replay poisoning | `24ebc85519` |
| DD-RH-092 | Low | Consensus replay hash poisoning | `2878e11741` |
| DD-RH-093 | Low | Bundle replay hash bypass | `d82f35c677` |
| DD-RH-095 | Medium | Early MuSig2 partial replay | `e59d89ccc8` |
| DD-RH-096 | Medium | Early partial buffer starvation | `88f1568e5b` |
| DD-RH-098 | Medium | Pre-activation oracle price reload | `b17e5ef922` |
| DD-RH-099 | Medium | Invalid-block oracle cache side effects | `c1c1896fb8` |
| DD-RH-100 | Medium | Oracle-price retry DoS in miner | `6af2d0ed4d` |
| DD-RH-101 | Medium | TestBlockValidity DD health side effects | `32570f2bf1` |
| DD-RH-102 | Low | Oracle GBT RPC schema | `c49438caba` |
| DD-RH-103 | Low | Mock bundle timestamp signing | `03c28004da` |
| DD-RH-104 | Low | Mock bundle timestamp overflow edge | `630ae01c5c` |

## Supporting Commits

These commits were security-test or suite-maintenance work, not separate confirmed vulnerabilities:

| Commit | Purpose |
|---|---|
| `7b5140f994` | Harden v0x03 bitmap script creation around DD-RH-083. |
| `9a1628c55e` | Enable and fix oracle parser/price fuzz harnesses. |
| `8d78851fa9` | Refresh stale oracle unit-test expectations. |
| `d21b43c96a` | Refresh functional tests for always-present DigiDollar oracle mining commitments. |

## Open Confirmed Bugs

| ID | Severity | Status | Plain-English risk |
|---|---:|---|---|
| DD-RH-055 | Low | Open | `senddigidollar` reports whole-wallet remainder as `change_amount`, not actual selected-input DD change. This can mislead integrations/UI. |
| DD-RH-057 | Low | Open | `estimatecollateral` has reachable signed overflow/undefined behavior in unused USD-value math at extreme valid inputs. |
| DD-RH-058 | Low | Open | `calculatecollateralrequirement` quotes collateral for amounts that mint validation would reject. |
| DD-RH-059 | Medium | Open | `digidollarstatsindex` assumes fixed mint output order while consensus accepts reordered valid mint outputs, so index stats can miss a valid mint. |

## Architecture Review Required

These were not patched because they require consensus, wallet-storage, oracle-protocol, MuSig2-protocol, or product-support decisions.

| ID | Severity | Decision Needed |
|---|---:|---|
| ARCH-RH-002 | High | Decide whether IBD/catch-up `skipOracleValidation` can remain consensus-affecting. |
| ARCH-RH-003 | High | Decide whether volatility freeze is chain state or policy/UI only. |
| ARCH-RH-004 | High | Decide ERR behavior: implement burn validation, allow normal redemption until ERR exists, or hide/disable ERR claims. |
| DD-RH-069 | Critical | Decide how collateral scripts prevent non-DD collateral spends after timelock without DD burn. |
| DD-RH-075 | High | Decide encrypted wallet-storage migration for oracle operator private keys. |
| DD-RH-084 | High | Fix mainnet active oracle roster IDs that exceed consensus/MuSig2 bitmap total. |
| DD-RH-085 | High | Decide whether Phase Three-active heights must reject v0x02 Phase Two oracle bundles. |
| DD-RH-086 | Medium | Decide whether malformed/unknown `OP_ORACLE` outputs should fail closed after activation. |
| DD-RH-094 | High | Decide MuSig2 signer reselection/retry policy for low-ID nonce trimming liveness attacks. |
| DD-RH-097 | High | Add network/genesis/deployment domain separation to MuSig2 nonce/partial/aggregate signing if approved. |
| LEGACY-DD-WALLET | Medium | Decide whether legacy wallets are unsupported for DigiDollar bech32m receive/mint, or design legacy-compatible DD key/address support. |

## Final Test Evidence

| Command | Result |
|---|---|
| `make check -j$(nproc)` | Passed. Unit, wallet, Qt, minisketch, secp256k1, utility, and bench sanity checks passed. |
| `test/functional/test_runner.py --jobs=8` | Passed on rerun: `318/318`, expected environment skips, accumulated `2249s`, runtime `335s`. |
| `test/functional/feature_coinstatsindex.py` | Passed standalone after a first full-suite transient RPC port-bind failure. |
| Direct loop over `digidollar_*.py`, `wallet_digidollar_*.py`, `feature_oracle_p2p.py`, `rpc_getoracles_pending.py` | Passed for 64 scripts. |
| Descriptor loop over 58 `add_wallet_options(parser)` scripts with unique `--portseed`, plus `wallet_digidollar_descriptors.py --descriptors` | Passed. |
| `python3 test/functional/feature_oracle_p2p.py --portseed=4101` | Passed. |
| `python3 test/functional/rpc_getoracles_pending.py --portseed=4102` | Passed. |
| Fuzz smoke over 40 DD/oracle/MuSig2/RPC/script targets with empty/ascii/zero256 inputs | Passed. |
| `git diff --check` | Passed. |

Known non-passing exploratory command:

| Command | Result |
|---|---|
| `python3 test/functional/digidollar_basic.py --legacy-wallet` | Fails with `Error: No bech32m addresses available. (-12)`. Recorded as legacy-wallet DD support decision, not a fixed vulnerability. |

## Remaining Risks

- Several older functional scripts still pass while logging "expected in RED phase", "not implemented", or "skipped" paths. They are useful smoke tests, but not strong proof for every advertised behavior.
- Oracle P2P command-specific fuzzing should be expanded for `ORACLEPRICE`, `ORACLEBUNDLE`, MuSig2 nonce/partial, consensus attestations, and `GETORACLES`.
- Full-block oracle coinbase fuzzing should exercise `OracleDataValidator::ValidateBlockOracleData()` and phase checks, not just extractors.
- DigiDollar validation fuzzing should add production-like `CCoinsViewCache`/prev-tx corpora for mint, transfer, redeem, and burn/conservation paths.

## Conclusion

The implementation bugs fixed in this campaign are committed separately and verified. Legitimate DigiDollar/oracle mint, transfer, redeem, wallet restore/rescan, RPC, mining-template, oracle, MuSig2, Qt, and fuzz smoke behavior passes the final test matrix above.

The main remaining launch blockers are not hidden in chat memory: they are recorded as open bugs and architecture-review items in this report and in `reports/red_hornet_security_ledger.md`.

## Post-Campaign Continuation Addendum - 2026-04-29

Ledger path: `reports/red_hornet_security_ledger.md`. Scope stayed within DigiDollar/oracle and directly gating RPC, wallet, index, and test surfaces.

Jared requested follow-up work on the open implementation bugs from the previous campaign. The continuation fixes the prior open set DD-RH-055/DD-RH-057/DD-RH-058/DD-RH-059, with each implementation fix committed separately.

| ID | Severity | Status | Commit | Fix summary |
|---|---:|---|---|---|
| DD-RH-055 | Low | Fixed and committed | `9335a841c2` | `senddigidollar` now reports selected-input DD change returned by the wallet transfer path, not whole-wallet remainder. |
| DD-RH-057 | Low | Fixed and committed | `a96a988499` | Removed dead signed `CAmount` USD-value multiplication from `estimatecollateral`; returned `usd_value` remains `ddAmount / 100.0`. UBSan build remains recommended follow-up sanitizer proof. |
| DD-RH-058 | Low | Fixed and committed | `cd3f6425b5` | `calculatecollateralrequirement` now enforces live mint min/max bounds before quoting collateral. |
| DD-RH-059 | Medium | Fixed and committed | `7d6ee67f0f` | `digidollarstatsindex` now uses order-independent mint accounting and stores/redeems the actual collateral vault outpoint. |

New/updated tests:

| Test | Coverage |
|---|---|
| `test/functional/digidollar_rpc_collateral.py` | DD-RH-058 above-regtest-maximum collateral quote rejection. |
| `test/functional/digidollar_send.py` | DD-RH-055 selected-input DD change response (`900`, not `5900`). |
| `test/functional/digidollar_stats_reordered_mint.py` | DD-RH-059 consensus-valid reordered mint is included in stats. |
| `test/functional/digidollar_mint.py` | Existing mint functional test now uses regtest-valid amounts after DD-RH-058 made collateral quote bounds strict. |
| `test/functional/feature_index_prune.py` | Full extended functional suite now uses the current DigiByte prune boundaries and preserves the index restart/failure checks. |
| `test/functional/test_runner.py` | DD-RH-059 regression and the previously unregistered DigiDollar/oracle functional scripts are included in the standard functional test list. |
| `src/test/rh61_coinbase_price_cache_poisoning_tests.cpp` | RH68 now resets global volatility state so the full unit suite does not inherit `minting-frozen-volatility` from earlier tests. |

Continuation verification:

| Command | Result |
|---|---|
| `make -C src -j$(nproc) digibyted` | Passed. |
| `make -C src -j$(nproc) test/test_digibyte` | Passed. |
| `./src/test/test_digibyte --show_progress` | Passed, 2967 test cases. |
| `python3 test/functional/digidollar_rpc_collateral.py` | Passed. |
| `python3 test/functional/digidollar_rpc_estimate.py` | Passed. |
| `python3 test/functional/digidollar_send.py` | Passed. |
| `python3 test/functional/digidollar_transfer.py` | Passed. |
| `python3 test/functional/digidollar_mint.py` | Passed. |
| `python3 test/functional/feature_index_prune.py` | Passed. |
| `python3 test/functional/digidollar_stats_reordered_mint.py` | Passed. |
| `python3 test/functional/digidollar_stats_reorg.py` | Passed. |
| `python3 test/functional/test_runner.py --extended --jobs=4` | Passed, 360 registered scripts; accumulated test time 3021s, runtime 804s. Runner still warns that `feature_assumevalid.py` and `feature_assumeutxo.py` are not registered because they are documented as disabled pending DigiByte MultiAlgo adaptation. |
| `./src/test/test_digibyte --run_test=digidollar_rh46_rpc_input_validation_tests --log_level=error --report_level=short` | Passed, 63 test cases / 119 assertions. |
| `./src/test/test_digibyte --run_test=digidollar_consensus_tests/mint_amount_validation_test --log_level=error --report_level=short` | Passed, 1 test case / 9 assertions. |
| `./src/test/test_digibyte --run_test=digidollar_validation_tests/mint_accounting_extraction_allows_change_before_opreturn --log_level=error --report_level=short` | Passed, 1 test case / 7 assertions. |
| `./src/test/test_digibyte --run_test=digidollar_redteam_tests/redteam_T4_01b_usd_value_display_overflow --log_level=error --report_level=short` | Passed, 1 test case / 2 assertions. |
| `git diff --check` | Passed. |

Additional test-isolation commit:

- `3a88816293` (`tests digidollar: reset volatility in RH68 block-validity test`) fixes the full-suite unit failure where RH68 inherited a frozen DigiDollar volatility state and rejected its valid mint as `minting-frozen-volatility`.
- `8454c57363` (`functional digidollar: align mint test with regtest bounds`) fixes the full functional failure where `digidollar_mint.py` still treated above-regtest-maximum collateral quote amounts as valid after DD-RH-058.
- `c7e0887b3f` (`functional tests: refresh index prune boundaries`) fixes the full functional failure in `feature_index_prune.py` by using the current DigiByte prune-file boundaries and syncing indexes one block past the actual restart boundary.
- `318fc8edb9` (`functional tests: register DigiDollar oracle scripts`) adds the missing DigiDollar/oracle functional scripts to the standard runner, including the descriptor flag required by `wallet_digidollar_descriptors.py`.

Architecture-review addendum:

- DD-RH-059-WALLET-STORAGE: wallet position storage still commonly reconstructs collateral as `COutPoint(txid, 0)`. The stats index fix covers order-independent index/RPC accounting, but supporting arbitrary collateral vault output indexes in wallet position storage should be approved as a wallet-storage/schema decision before implementation.
