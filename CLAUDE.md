# CLAUDE.md — AI Agent Guide for DigiByte Core / DigiDollar

## What this repo is

DigiByte Core v9.26 (Bitcoin Core v26.2 lineage) on the `feature/digidollar-v1` branch, augmented with the **DigiDollar** native USD-pegged stablecoin and a multi-oracle DGB/USD price feed network with MuSig2 Schnorr threshold consensus.

Working directory: `/home/jared/Code/digibyte`. Current DigiDollar v1 campaign branch: `feature/digidollar-v1`. Upstream main branch: `develop`.

## Required reading order before any DigiDollar / oracle work

1. `CLAUDE.md` — This guide, branch rules, and current code-surface warnings
2. `ARCHITECTURE.md` — Core DigiByte system design
3. `REPO_MAP.md` — Core DigiByte file index (excludes DigiDollar/oracle subsystem)
4. `REPO_MAP_GUIDE.md` — How to maintain and interpret the repo maps
5. `DIGIDOLLAR_ARCHITECTURE.md` — DigiDollar mint/transfer/redeem/collateral/health/state
6. `DIGIDOLLAR_ORACLE_ARCHITECTURE.md` — Exchange aggregation, bundle lifecycle, MuSig2, P2P
7. `REPO_MAP_DIGIDOLLAR.md` — DigiDollar/oracle file index (production, wallet, RPC, Qt, tests, fuzz)
8. `DIGIDOLLAR_EXPLAINER.md` — User-facing DigiDollar V1 protocol summary
9. `DIGIDOLLAR_ORACLE_EXPLAINER.md` — User-facing oracle/MuSig2 summary
10. `DIGIDOLLAR_ACTIVATION_EXPLAINER.md` — BIP9 gating of DD/oracle surface
11. `DIGIDOLLAR_WALLET_INTEGRATION.md` — Wallet/RPC integration guide
12. `DIGIDOLLAR_EXCHANGE_INTEGRATION.md` — Exchange/custody integration guide
13. `ORACLE_DISCOVERY_ARCHITECTURE.md` — Oracle endpoint discovery design
14. `DIGIDOLLAR_ORACLE_SETUP.md` — Oracle setup and migration runbook

Also check `docs/ORACLE_OPERATOR_GUIDE.md` for operator-facing steps and the newest relevant `RELEASE_v9.26.0-rc*.md` before public instructions or release claims.

## Repo layout (DigiDollar / oracle surface)

```
src/digidollar/      # 5 modules: digidollar, health, scripts, txbuilder, validation
src/oracle/          # Oracle daemon + MuSig2: bundle_manager, exchange, mock_oracle,
                     # node, signing_orchestrator, musig2_{aggregator,session,
                     # session_manager,orchestrator,oracle_participation,messages,
                     # session_mining}
src/consensus/       # dca, err, digidollar, digidollar_transaction_validation,
                     # digidollar_tx, volatility (DigiDollar/oracle consensus rules)
src/index/           # digidollarstatsindex (DD supply/health index)
src/primitives/      # oracle.h (price message + bundle types)
src/rpc/             # digidollar.cpp (18 base RPCs); digidollar_transactions.cpp is
                     # legacy / unregistered
src/wallet/          # digidollarwallet.{cpp,h}, ddcoincontrol.h; wallet/rpc/wallet.cpp
                     # registers 17 wallet-context DD/oracle RPCs
src/qt/              # DD tab + widgets/dialogs: digidollar{tab,mintwidget,sendwidget,
                     # receivewidget,redeemwidget,overviewwidget,positionswidget,
                     # transactionswidget,coincontroldialog,receiverequest},
                     # ddaddressbookpage, digidollar_qt_translate
src/test/            # ~150 DigiDollar/oracle/MuSig2/Red-Hornet unit tests + fuzz/
src/wallet/test/     # 6 DD wallet test sources (persistence, security, Wave 16/17,
                     # rh59 lock-bypass)
                     # — linked into src/test/test_digibyte, not a separate binary
src/qt/test/         # digidollarwidgettests, digidollarwave19widgettests
test/functional/     # ~80 DD/oracle Python tests registered in test_runner.py
```

## DigiDollar opcode soft-fork additions

Defined in `src/script/script.h:209-220`:
- `OP_DIGIDOLLAR    = 0xbb` (Tapscript OP_SUCCESSx slot pre-activation)
- `OP_DDVERIFY      = 0xbc` (Tapscript OP_SUCCESSx slot pre-activation)
- `OP_CHECKPRICE    = 0xbd` — **deterministically DISABLED (reserved)** as of DD-FINAL-005 / AR-0. Post-activation it consumes its witness operand and always pushes `vchFalse` (fail closed); it no longer consults any oracle price. It previously consulted the live node-local/wall-clock price (`g_get_oracle_consensus_price` → `OracleBundleManager::GetLatestPrice`), which was non-deterministic across nodes and a chain-split risk; no DigiDollar script emits OP_CHECKPRICE, so disabling it has zero protocol impact. A future price opcode must bind to the block's own committed v0x03 bundle price (`src/script/interpreter.cpp:708`).
- `OP_CHECKCOLLATERAL = 0xbe` — compares stack ratio to threshold; consumes `<ratio> <threshold>` and pushes `ratio>=threshold`
- `OP_ORACLE        = 0xbf` — coinbase oracle bundle marker

These are OP_SUCCESSx-class opcodes that become functional only when `SCRIPT_VERIFY_DIGIDOLLAR` is set, which only happens when BIP9 `DEPLOYMENT_DIGIDOLLAR` is ACTIVE (bit 23). See `IsDigiDollarOpcode` / `IsOpSuccessForFlags` at `src/script/interpreter.cpp:439-453`.

## Activation summary (BIP9 bit 23)

| Network | Start | Min activation height | Window | Threshold | Status |
|---------|-------|----------------------|--------|-----------|--------|
| Mainnet | 2026-06-01 (epoch 1780272000) | 23,627,520 | 40,320 blocks (~1 week) | 70% (28,224 of 40,320) | Pending |
| Testnet (testnet26) | BIP9 start/reset genesis 1780156800 | 600 | 200 blocks | 70% (140 of 200) | Check `getdigidollardeploymentinfo` |
| Regtest | ALWAYS_ACTIVE | 0 | 144 blocks (BIP9 default) | 75% (108 of 144) | Active |

`nDDActivationHeight`, `nOracleActivationHeight`, and `nDigiDollarMuSig2Height` collapse to the same height trigger on mainnet (23,627,520 / 23,627,520 / 23,627,520) and testnet (600 / 600 / 600). Default regtest keeps DD/oracle height gates at 650 / 650 while the BIP9 deployment is `ALWAYS_ACTIVE` with `min_activation_height=0`; `nDigiDollarMuSig2Height` follows that effective BIP9 boundary and is `0`, so v0x03 quotes are valid whenever DigiDollar is active. The direct `-digidollaractivationheight=N` regtest knob retargets both the BIP9 minimum and the static DD/oracle/MuSig2 height gates. Generic `-vbparams=digidollar:...` still overrides BIP9 only; because MuSig2 follows the effective DigiDollar BIP9 boundary, a BIP9-only test override can move MuSig2 validation without moving the static DD/oracle P2P gates. Startup oracle-price reconstruction follows the same BIP9 predicate as block connection, so default-regtest BIP9-active blocks below 650 are not dropped during restart/reindex cache rebuilds. The variable in code is `nDigiDollarMuSig2Height`, not the older `nDigiDollarPhase3Height` (`src/consensus/params.h:195`). Once DigiDollar is active, v0x03 MuSig2 is the only on-chain bundle format ever accepted.

## Oracle roster

| Network | Total slots | Active | Consensus |
|---------|-------------|--------|-----------|
| Mainnet | 35 (`vOracleNodes`) | 35 (`consensus.vOraclePublicKeys` slots 0-34) | 7 signatures from active keyset |
| Testnet | 35 | 35 (slots 0-34) | 7 signatures from active keyset |
| Regtest | 7 | 7 | 4-of-7 |

Slots 0-34 on mainnet and testnet are active. Slot 28 uses the DigiHash Mining
Pool key, slot 31 uses the Peer2Peer / DigiRoos key, and all 35 configured
slots contain valid compressed secp256k1 oracle keys. ID 35 is outside the
configured roster and must be rejected by RPC/P2P/bitmap bounds checks.

`src/primitives/oracle.h:19-21` declares header defaults `ORACLE_CONSENSUS_REQUIRED=7`, `ORACLE_ACTIVE_COUNT=35`, `ORACLE_TOTAL_COUNT=35`. Chainparams sets `nOracleConsensusRequired`, `nOraclePubkeyCount`, and `nOracleTotalOracles` per network at startup, so those chainparams values are what the validator and MuSig2 aggregator use.

## DigiDollar critical constants

```python
# DigiByte chain values (do not use Bitcoin defaults)
BLOCK_TIME            = 15            # seconds
COINBASE_MATURITY     = 8             # blocks (100 after height threshold = COINBASE_MATURITY_2)
SUBSIDY               = 72000         # DGB
MAX_MONEY             = 21_000_000_000

# Fees (DigiByte uses DGB/kB, not DGB/vB)
MIN_RELAY_TX_FEE      = 0.001         # DGB/kB
DEFAULT_TRANSACTION_FEE = 0.1         # DGB/kB

# Network
P2P_PORT_MAINNET      = 12024
P2P_PORT_TESTNET      = 12033  # testnet26 per src/kernel/chainparams.cpp

# Address formats
REGTEST_BECH32        = 'dgbrt'
TESTNET_BECH32        = 'dgbt'
DD_ADDR_PREFIX        = 'DD'  / 'TD' / 'RD' (mainnet/testnet/regtest)

# DigiDollar-specific
DD_AMOUNT_UNIT        = 1 cent (10000 = $100.00)
DD_TX_VERSION_MARKER  = 0x0770 (low 16 bits of nVersion)
DD_TX_TYPE_FIELD      = (nVersion >> 24) & 0xFF  # 1=MINT, 2=TRANSFER, 3=REDEEM
MINT_MIN              = 10_000   cents
MINT_MAX              = 10_000_000 cents
LOCK_TIERS            = 0..9 (1h, 30d, 90d, 180d, 1y, 2y, 3y, 5y, 7y, 10y)
                        # canonical durations enforced at consensus —
                        # mint lock height must be in the claimed tier window
                        # [canonical_blocks, canonical_blocks + 100]
                        # under-locked/custom durations rejected
                        # (src/digidollar/validation.cpp)
LOCK_TIER_OPRETURN    = stored explicitly in mint OP_RETURN; consensus rejects
                        bad-mint-lock-tier-duration when remaining lock blocks
                        fall outside the canonical confirmation window for the
                        claimed tier
```

## Where DigiDollar/oracle is gated at runtime

| Layer | Gate | Reference |
|-------|------|-----------|
| RPC | `DigiDollar::IsDigiDollarEnabled(tip, chainman)` at the top of each DD/oracle RPC | `src/rpc/digidollar.cpp` (10+ callsites) |
| Mempool | `IsDigiDollarEnabled` + `HasDigiDollarMarker`; DD txs additionally require a recent valid MuSig2 oracle quote via `HasRecentValidMuSig2OracleQuote` | `src/validation.cpp:224-286, 976-989` |
| Block | `IsDigiDollarEnabled` + `HasDigiDollarMarker` during `ConnectBlock`; coinbase oracle bundles require V1 MuSig2 version via `CheckMuSig2OracleBundleVersion` | `src/validation.cpp:185, 2808-3214` |
| Script | `SCRIPT_VERIFY_DIGIDOLLAR` flag | `src/validation.cpp` |
| P2P | `IsOracleP2PActive` at the top of all oracle handlers, including `oraclehb`, `oracleprice`, `oraclebundle` (accepted-and-dropped — V1 puts the bundle on-chain), `oracleconsns`, `oracleattest`, `oramusnonce`, `oramusigctx`, `oramusigpsig`, and `getoracles`. Wire names defined in `src/protocol.cpp:53-62`. | `src/net_processing.cpp` |
| Qt | `DigiDollarTab` activation overlay; widgets check `isVisible()` before polling | `src/qt/digidollartab.cpp` |
| Price cache | `UpdatePriceCache` gated on `DEPLOYMENT_DIGIDOLLAR` | `src/validation.cpp` (rh61 fix) |

## RPC surface (35 commands)

18 commands registered via `RegisterDigiDollarRPCCommands()` in `src/rpc/digidollar.cpp:6266`:
`getdigidollarstats`, `getdcamultiplier`, `calculatecollateralrequirement`, `getdigidollardeploymentinfo`, `importdigidollaraddress`, `estimatecollateral`, `getoracleprice`, `getalloracleprices`, `getprotectionstatus`, `getoracles`, `getoraclesigners`, `listoracle`, `stoporacle`, `getoraclepubkey`, `setmockoracleprice` (regtest), `getmockoracleprice` (regtest), `simulatepricevolatility` (regtest), `enablemockoracle` (regtest).

17 wallet-context commands registered in `GetWalletRPCCommands()` at `src/wallet/rpc/wallet.cpp:888`:
`mintdigidollar`, `senddigidollar`, `sendmanydigidollar`, `redeemdigidollar`, `listdigidollarpositions`, `listdigidollaraddresses`, `getredemptioninfo`, `getdigidollarbalance`, `getdigidollaraddress`, `listdigidollartxs`, `listdigidollarunspent`, `listdigidollarutxos`, `validateddaddress`, `createoraclekey`, `exportoracleprivkey`, `importoracleprivkey`, `startoracle`.

`createoraclekey`, `exportoracleprivkey`, and `importoracleprivkey` are local wallet key-management RPCs and are intentionally usable before DigiDollar activation. They do not start an oracle, sign prices, relay oracle data, or change consensus state. `startoracle` and the price/DD operational RPCs remain activation-gated.

`sendoracleprice` and `submitoracleprice` are intentionally **absent** from the registration tables: `sendoracleprice` was removed as a fake-price-injection vulnerability, and `submitoracleprice` does not exist anywhere in the source tree. Oracle prices come exclusively from live exchange aggregation.

`src/rpc/digidollar_transactions.cpp` declares legacy entry points (`getdigidollarinfo`, `transferdigidollar`, `createrawddtransaction`, `listredeemablepositions`) plus duplicate names that are also defined in `src/rpc/digidollar.cpp` and `src/wallet/rpc/wallet.cpp` (`getdigidollaraddress`, `getdigidollarbalance`, `mintdigidollar`, `redeemdigidollar`, `getredemptioninfo`). Its `GetDigiDollarTransactionRPCCommands()` is **never called** — no caller exists in the build — so all RPCs in this file are inert. Treat as legacy/dead code unless rewired.

## Build / test commands

```bash
# Build
./autogen.sh && ./configure && make -j$(nproc)

# Run a specific C++ test suite
./src/test/test_digibyte --run_test=digidollar_validation_tests
./src/test/test_digibyte --list_content | grep -Ei 'digidollar|oracle|musig|^rh'

# Wallet-context DD tests (require ENABLE_WALLET; built into the same
# test_digibyte binary via src/Makefile.test.include when --enable-wallet is on,
# *not* a separate test_wallet_digibyte binary):
./src/test/test_digibyte --run_test=digidollar_persistence_wallet_tests
./src/test/test_digibyte --run_test=digidollar_wallet_security_tests
./src/test/test_digibyte --run_test=rh59_coincontrol_dd_lock_bypass_tests

# Run a functional test
./test/functional/digidollar_basic.py

# Find DigiDollar/oracle source without generated/build products
rg -n 'digidollar|DigiDollar|oracle|MuSig|musig' src/ test/ \
  -g '*.cpp' -g '*.h' -g '*.hpp' -g '*.py' \
  -g '!**/.deps/**' -g '!**/.libs/**' -g '!*.o' -g '!*.lo' \
  -g '!src/qt/moc_*.cpp' -g '!src/qt/test/moc_*.cpp' -g '!src/qt/forms/ui_*.h'
```

## Important notes

- **Confirmed-only DigiDollar transfers.** Unconfirmed DD chaining was removed (commit `0b4959f563`). Consensus refuses to resolve DD amounts from `MEMPOOL_HEIGHT` inputs for transfer/redeem; wallets must wait for confirmation between sends.
- **OP_CHECKPRICE is deterministically disabled (DD-FINAL-005).** Post-activation the opcode consumes its operand and unconditionally pushes `vchFalse` (`src/script/interpreter.cpp:708-735`); it no longer calls `g_get_oracle_consensus_price` (the hook is left null in production — see `src/init.cpp` Step 13). This removed the AR-0 fork vector (the old live-price consult was node-local/wall-clock dependent). The standalone `libdigibyteconsensus.so` behaviour is unchanged (also fails closed).
- **Mainnet/testnet validator parity.** The mainnet oracle-validation short-circuit was removed (commit `f0d9a7b2c7`). `OracleDataValidator::ValidateBlockOracleData` (`src/oracle/bundle_manager.cpp:2151`) now runs identically on mainnet and testnet, and only v0x03 MuSig2 bundles are accepted (commits `bbb85cf363`, `fa29405adc`, `f2bb0a19a4`). Raw v0x01/v0x02 OP_RETURN payloads short-circuit inside `ExtractOracleBundle`, so the validator emits `bad-oracle-malformed`. The `bad-oracle-legacy` branch only fires when extraction succeeds with a non-MuSig2 version, which is structurally unreachable for current v0x03 wire payloads — it remains as a defense-in-depth gate.
- **Price-dependent DD blocks must include exactly one v0x03 MuSig2 bundle.** Commit `1e08bd811f`: `ValidateBlockOracleData` returns `bad-oracle-missing` if a DD mint/redeem block has no oracle output, `bad-oracle-multiple-outputs` if it has more than one, and `bad-oracle-malformed` if extraction fails (which is the canonical reason raw v0x01/v0x02 produce). DD transfer-only and non-DD blocks may omit the bundle entirely.
- **Mempool requires an oracle quote for DD txs.** Commit `81bf974f40`: DD txs are not accepted into the mempool unless `HasRecentValidMuSig2OracleQuote` finds a recent valid v0x03 quote (`src/validation.cpp:224-286, 976-989`).
- **Custom lock durations rejected.** Consensus enforces canonical lock tiers with a 100-block confirmation buffer: `bad-mint-lock-period` for non-canonical periods, `bad-mint-lock-tier` for tier outside 0–9, and `bad-mint-lock-tier-duration` when remaining lock blocks fall outside `[canonical_blocks, canonical_blocks + 100]` for the claimed tier (commits `e1dd69f99b`, `11728a6980`).
- **DD supply alert, not a cap.** `AlertThresholds::ALERT_DD_SUPPLY` (`src/digidollar/health.h:84`, 10000000000 = 100M DD) is a monitoring threshold, not a consensus cap. `MAX_DIGIDOLLAR` is a per-output serialization bound. There is no global circulating-supply cap; total DD is constrained only by available collateral and the per-block minting rate.
- **Collateral vault spends require DD burn.** Non-DD transactions that try to spend a registered DigiDollar collateral vault are rejected with `bad-collateral-spend-missing-dd-burn`. Inside DD redemptions, `ValidateCollateralReleaseAmount` rejects partial burns with `bad-collateral-release-partial-burn` (`src/digidollar/validation.cpp`).
- **Qt mint derives HD owner keys.** Commit `1e95478b7e` made the Qt mint flow derive DD owner keys from the wallet's HD chain and persist them via `DigiDollarWallet::StoreOwnerKey` *before* broadcasting the mint tx, instead of generating an ephemeral random key after broadcast. Mint now requires an HD wallet with private keys enabled. RPC mint already used the same path.
- **Mining graceful degradation.** `CreateNewBlock` strips price-dependent DD mint/redeem txs when no valid oracle bundle is available, keeps ordinary DGB and price-independent DD transfer candidates flowing when valid, and continues block assembly instead of hanging (commit `6b5ff516c3`).
- **BIP324 V2 P2P transport** is supported and enabled with `-v2transport=1` (off by default).
- **Three-way comparison still applies for non-DigiDollar work.** Compare v9.26 ↔ v8.22.2 ↔ Bitcoin v26.2 in `digibyte-v8.22.2/` and `bitcoin-v26.2-for-digibyte/` when making changes to inherited code.
- **Avoid widening doc claims beyond what code shows.** All material claims in the approved docs (16 listed in `Z_PROMPTS.md`) must match `src/`. Treat code as truth.

## Quick orientation for sub-agents

When spawning a sub-agent on DigiDollar/oracle work, point it at this file plus the four most relevant docs (`DIGIDOLLAR_ARCHITECTURE.md`, `DIGIDOLLAR_ORACLE_ARCHITECTURE.md`, `REPO_MAP_DIGIDOLLAR.md`, `DIGIDOLLAR_ACTIVATION_EXPLAINER.md`) and an explicit list of files it should read/modify. Do not ask sub-agents to redesign architecture; treat existing approved docs as the contract.
