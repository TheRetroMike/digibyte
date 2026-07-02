# DigiDollar Documentation Final QA Report — 2026-05-20

## Scope

- Branch: `feature/digidollar-v1`
- Workspace: `/home/jared/Code/digibyte`
- Campaign rule: documentation/report-only; no production-code edits; no push.
- Target docs audited:
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
  - `DIGIDOLLAR_EXCHANGE_INTEGRATION.md`
  - `ORACLE_DISCOVERY_ARCHITECTURE.md`
  - `DIGIDOLLAR_ORACLE_SETUP.md`
- Secondary/operator doc audited: `docs/ORACLE_OPERATOR_GUIDE.md`
- Release/report context read: root `RELEASE_v9.26.0-rc40.md`, `doc/release-notes/RELEASE_v9.26.0-rc40.md`, `final_audit.MD`, `reports/final_audit_report.md`, `reports/final_audit_ledger.md`, `reports/red_hornet_final_report.md`, `reports/red_hornet_final_launch_report.md`, `reports/red_hornet_final_ledger.md`.

## Documents Touched

Agents 1-4 updated the 14 target docs before this final QA pass. Agent 5 then made in-place consistency edits, and the final second-pass validation tightened stale line anchors and phase wording in:

- `CLAUDE.md`
- `DIGIDOLLAR_ACTIVATION_EXPLAINER.md`
- `DIGIDOLLAR_ARCHITECTURE.md`
- `DIGIDOLLAR_EXCHANGE_INTEGRATION.md`
- `DIGIDOLLAR_EXPLAINER.md`
- `DIGIDOLLAR_ORACLE_ARCHITECTURE.md`
- `DIGIDOLLAR_ORACLE_EXPLAINER.md`
- `DIGIDOLLAR_ORACLE_SETUP.md`
- `DIGIDOLLAR_WALLET_INTEGRATION.md`
- `ORACLE_DISCOVERY_ARCHITECTURE.md`
- `docs/ORACLE_OPERATOR_GUIDE.md`
- `reports/doc_audit_report.md`

No files under `src/`, `test/functional/`, build products, or production code were edited by this pass.

## Major Stale Claims Fixed

- Current public testnet references now consistently identify `testnet24`, P2P port `12031`, RPC port `14026`, activation height `600`, and the testnet24 data/log paths. Retired `testnet23`/12030 references remain only in `DIGIDOLLAR_ORACLE_SETUP.md` historical/decommissioning footnotes.
- `docs/ORACLE_OPERATOR_GUIDE.md` was brought forward from stale RC34/testnet23 operator instructions to RC40/testnet24 instructions, including endpoint examples, debug-log paths, port table, oracle-slot table, and RPC/source line references.
- `CLAUDE.md` now lists the final 14 target docs in the same required reading order used by this campaign, with `docs/ORACLE_OPERATOR_GUIDE.md` and newest RC notes called out as secondary checks.
- RPC surface claims now match source: 32 registered DD/oracle commands total, split as 17 node-context commands in `RegisterDigiDollarRPCCommands()` at `src/rpc/digidollar.cpp:5570` and 15 wallet-context commands in `GetWalletRPCCommands()` at `src/wallet/rpc/wallet.cpp:888` / lines 962-976. `listdigidollarunspent` and `listdigidollarutxos` are included; 31 commands are activation-gated and `getdigidollardeploymentinfo` is the intentional ungated probe.
- Oracle roster language now distinguishes mainnet reserve metadata from testnet: mainnet has 30 `vOracleNodes` metadata slots with slots 0-16 active; testnet24 has only the 17 active slots configured; regtest has 7 slots. Header defaults are `ORACLE_TOTAL_COUNT=30`, `ORACLE_ACTIVE_COUNT=17`, and `ORACLE_CONSENSUS_REQUIRED=9`, with chainparams authoritative per network.
- Activation/opcode wording no longer says DD opcodes are pre-activation NOPs. Docs now describe them as Tapscript OP_SUCCESSx-class opcodes whose DigiDollar semantics are only dispatched when `SCRIPT_VERIFY_DIGIDOLLAR` is set.
- Exchange/oracle source references were refreshed for the current tree: active fetchers at `src/oracle/exchange.cpp:1042-1071`, `FilterOutliers` at `src/oracle/exchange.cpp:1192`, live oracle source floor override at `src/oracle/node.cpp:445-450`, and aggregator default at `src/oracle/exchange.h:235`.
- Validation/source line references for MuSig2 bundle checks and mempool oracle-quote checks were refreshed to current `src/validation.cpp` locations.
- Second-pass code validation refreshed stale semantic line anchors that still pointed at existing files but no longer described the named behavior: collateral burn enforcement, non-DD collateral spend rejection, lock-tier canonical windows, oracle price checks, volatility pre-mutation checks, coinbase price-cache gating, MuSig2 bundle extraction, miner graceful-degradation paths, P2P message handlers, and DD amount/limit constants.
- Repo-map wording no longer describes `GetOracleKeys`, `RegisterScriptMetadata`, `OracleConsensusMsg`, `OracleAttestationMsg`, or the Phase 2-named regression tests as future/current Phase 1/Phase 2 protocol plans; they are described as current helper, compatibility, or regression surfaces.

## Code Surfaces Inspected

- Branch/status/log: `feature/digidollar-v1`, `git status --short`, `git log --oneline -40`.
- Chain/network truth: `src/chainparamsbase.cpp`, `src/kernel/chainparams.cpp`.
- RPC registration: `src/rpc/digidollar.cpp`, `src/wallet/rpc/wallet.cpp`, `src/rpc/digidollar_transactions.cpp`.
- Oracle primitives/roster constants: `src/primitives/oracle.h`, `src/primitives/oracle.cpp`.
- Oracle implementation: `src/oracle/exchange.{h,cpp}`, `src/oracle/node.cpp`, `src/oracle/bundle_manager.cpp`, `src/oracle/musig2_*`.
- Wallet/oracle key paths: `src/wallet/wallet.cpp`, `src/wallet/walletdb.cpp`, `src/wallet/digidollarwallet.cpp`.
- Activation/validation: `src/validation.cpp`, `src/consensus/params.h`, `src/consensus/{dca,err,volatility,digidollar}.*`, `src/digidollar/{scripts,validation,health,txbuilder}.*`.
- Script/P2P/Qt surfaces referenced by docs: `src/script/{script.h,interpreter.cpp}`, `src/protocol.cpp`, `src/net_processing.cpp`, `src/qt/digidollar*`, `src/qt/ddaddressbookpage.*`, `src/qt/digidollar_qt_translate.h`.
- Test surface inventory from the required `find ... | grep` command, with generated/build artifacts ignored for reasoning.

## Commands Run

```bash
git branch --show-current
git status --short
git log --oneline -40
git diff --name-only
git diff --stat

for f in "${TARGET_DOCS[@]}" docs/ORACLE_OPERATOR_GUIDE.md RELEASE_v9.26.0-rc40.md final_audit.MD; do test -f "$f" && echo "OK $f" || echo "MISSING $f"; done

find src/digidollar src/oracle src/consensus src/primitives src/rpc src/wallet src/qt src/test src/test/fuzz test/functional -type f | grep -Ei 'digidollar|oracle|musig2|dca|err|volatility|wallet|qt|activation|deployment' | sort

rg -n 'testnet23|Testnet23|12030|RC34 is the latest|RC34 validation|30 commands|13 wallet|src/rpc/digidollar.cpp:(4981|5222)|30/15/8|15-of-30|8-of-15|treated as NOPs|DD opcodes are NOPs' "${TARGET_DOCS[@]}" docs/ORACLE_OPERATOR_GUIDE.md

rg -n '32 registered|31 are activation-gated|17 base|15 wallet|listdigidollarunspent|listdigidollarutxos|RegisterDigiDollarRPCCommands|5570|962-976|962–976|testnet24|12031|14026|14126' "${TARGET_DOCS[@]}" docs/ORACLE_OPERATOR_GUIDE.md

rg -n 'RegisterDigiDollarRPCCommands|GetWalletRPCCommands|listdigidollarunspent|listdigidollarutxos|nDefaultPort|testnet24|12031|nOraclePubkeyCount|nOracleConsensusRequired|vOracleNodes|ORACLE_CONSENSUS_REQUIRED|ORACLE_ACTIVE_COUNT' src/rpc/digidollar.cpp src/wallet/rpc/wallet.cpp src/kernel/chainparams.cpp src/primitives/oracle.h

rg -n '\[[^]]+\]\([^)]+\)' "${TARGET_DOCS[@]}" docs/ORACLE_OPERATOR_GUIDE.md
rg -n '^#+ ' "${TARGET_DOCS[@]}" docs/ORACLE_OPERATOR_GUIDE.md
rg -n 'src/[A-Za-z0-9_./-]+:[0-9]+' "${TARGET_DOCS[@]}" docs/ORACLE_OPERATOR_GUIDE.md
rg -n 'src/digidollar/validation.cpp:1888|2047|2055|2212|2219|980-1023|1221-1227|822-825|2243|2308|src/validation.cpp:3266|src/oracle/bundle_manager.cpp:739|src/validation.cpp:2987|690-727|src/consensus/digidollar.h:50-61|src/consensus/digidollar.h:64-66|936-983|validation.cpp:~765|validation.cpp:~2706' "${TARGET_DOCS[@]}" docs/ORACLE_OPERATOR_GUIDE.md reports/doc_audit_report.md
rg -n 'ORACLEPRICE|ORACLEBUNDLE|GETORACLES|ORACLECONSENSUS|ORACLEATTESTATION|ORACLEMUSIGNONCE|ORACLEMUSIGCONTEXT|ORACLEMUSIGPARTIALSIG|ORACLEHEARTBEAT' src/net_processing.cpp
nl -ba src/digidollar/validation.cpp | sed -n '1128,1170p;1308,1390p;1568,1600p;2188,2220p;2290,2328p;2508,2660p;2664,2742p'
nl -ba src/validation.cpp | sed -n '176,286p;970,990p;2800,2860p;3058,3108p;3320,3380p'
nl -ba src/oracle/bundle_manager.cpp | sed -n '748,925p;1000,1057p;2067,2133p;2139,2244p'
nl -ba src/node/miner.cpp | sed -n '523,585p;849,864p'
nl -ba src/consensus/digidollar.h | sed -n '57,73p'
nl -ba src/consensus/dca.cpp | sed -n '239,242p'

python3 - <<'PY'
# local markdown link checker, ignoring fenced code blocks
PY

git diff --check
git status --short
```

## Link And Reference Checks

- Target doc existence check: all 14 target docs present.
- Secondary/context existence check: `docs/ORACLE_OPERATOR_GUIDE.md`, root `RELEASE_v9.26.0-rc40.md`, and `final_audit.MD` present.
- Markdown local-link check: `MISSING_LINKS 0` after ignoring fenced code blocks.
- Exact source-path check: `MISSING_EXACT_PATHS 0`.
- Source-reference line-bound check: `SOURCE_LINE_ISSUES 0`.
- Source-reference spot checks: key moved references for RPC registration, wallet RPC registration, chainparams testnet24/ports, oracle roster constants, exchange fetchers, live oracle source floor, auto-start, P2P handlers, miner degradation, and validation gates were checked against current source and updated where stale.

## Remaining AUDIT NOTEs

- `DIGIDOLLAR_ARCHITECTURE.md` keeps an AUDIT NOTE that `ValidateScriptPathSpending()` is not the consensus witness validator.
- `DIGIDOLLAR_ACTIVATION_EXPLAINER.md` keeps an AUDIT NOTE that `oraclehb` is authenticated/rate-limited telemetry but does not currently share the same top-of-handler height gate as the other oracle P2P messages.

## Discovery / Generation Notes

- `REPO_MAP.md` keeps a discovery note warning that live repo discovery can include generated/build products and should exclude `.deps/`, `.libs/`, object files, Qt generated files, built binaries, `depends/`, `guix-build-*`, and historical reference trees.
- `REPO_MAP_GUIDE.md` keeps a generation note that raw `find ... | grep` and recursive grep commands can pick up generated/build products.

## Release-Risk Items

- No new release-risk item was found in this documentation QA pass.
- The stale public operator-doc risk from the Red Hornet launch report (`testnet23`/12030 in operator setup material) has been fixed in `docs/ORACLE_OPERATOR_GUIDE.md`; historical retired-testnet notes remain explicitly labeled in `DIGIDOLLAR_ORACLE_SETUP.md`.
- Existing architecture-review items remain for Jared decision before final launch claims:
  - `DD-FA-ARCH-001` — oracle active-roster expansion/governance beyond the 17-slot launch set.
  - `DD-FA-ARCH-002` — wallet/RPC position storage/import policy around nonzero collateral output indexes.
  - `DD-FA-ARCH-004` — MuSig2 intra-epoch participant retry/reselection policy.
  - `DD-FA-ARCH-005` — one-connect-height mint validity from canonical lock-tier validation against remaining blocks.
- RC40 known launch risks still apply: mainnet activation requires an explicit launch decision; fewer than 9 fresh valid oracle operators fail closed; mixed older RC oracle nodes may not reliably complete the current MuSig2 signing flow.

## Final QA Result

- `git diff --check`: clean.
- `git status --short`: shows only documentation files under the 14-target set plus `docs/ORACLE_OPERATOR_GUIDE.md`. `reports/doc_audit_report.md` is written but ignored by `.gitignore` via `reports/**/*.md`, so it does not appear in the plain short status.
- `git status --short --ignored reports/doc_audit_report.md`: `!! reports/doc_audit_report.md`.
- Docs/report-only confirmation: yes. No production code was edited and nothing was pushed.
