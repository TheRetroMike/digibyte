# DigiDollar BIP9 Activation — Complete Explainer

## Overview

DigiDollar activates on the DigiByte blockchain through **BIP9 version bit signaling** — the same proven mechanism used by Bitcoin for SegWit and other soft forks. This ensures DigiDollar only activates when a supermajority of miners explicitly signal support, preventing chain splits and ensuring network consensus.

**Key principle:** Nothing consensus-critical for DigiDollar works until activation. DD/oracle RPCs, DD transactions, DD opcodes, oracle price relay, oracle consensus relay, MuSig2 relay, `getoracles`, and signed oracle version heartbeats are dormant until the activation predicates below pass.

---

## BIP9 Deployment Parameters

### Mainnet
| Parameter | Value |
|-----------|-------|
| Bit | 23 |
| Start Time | June 1, 2026 (epoch 1780272000) |
| Timeout | June 1, 2027 (epoch 1811808000) |
| Min Activation Height | 23,627,520 |
| Confirmation Window | 40,320 blocks (~1 week) |
| Threshold | 70% (28,224 of 40,320) |

### Testnet (testnet26)
| Parameter | Value |
|-----------|-------|
| Bit | 23 |
| Start Time | Genesis timestamp (already past) |
| Timeout | Jan 1, 2028 (epoch 1830297600) |
| Min Activation Height | 600 |
| Confirmation Window | 200 blocks |
| Threshold | 70% (140 of 200) |

### Regtest
| Parameter | Value |
|-----------|-------|
| Status | ALWAYS_ACTIVE |
| Min Activation Height | 0 |

---

## BIP9 State Machine

DigiDollar follows the standard BIP9 state transitions:

```
DEFINED ──→ STARTED ──→ LOCKED_IN ──→ ACTIVE
   │            │
   │            └──→ FAILED (if timeout reached)
   └──→ FAILED (if timeout reached before start)
```

### Phase 1: DEFINED (blocks 0–199 on testnet)
- **What happens:** Nothing. DigiDollar deployment exists in the code but signaling hasn't begun.
- **Miner behavior:** Miners don't need to do anything. Block versions don't include bit 23.
- **User experience:** DigiDollar tab visible in Qt but shows "DigiDollar is not yet active on this blockchain" with current BIP9 status.
- **RPC behavior:** DD price/position/transaction/oracle-operation RPCs return error: "DigiDollar is not yet active on this blockchain". Local wallet oracle key-management RPCs (`createoraclekey`, `exportoracleprivkey`, `importoracleprivkey`) remain available so operators can prepare or recover keys before activation.
- **P2P behavior:** Oracle price/bundle/consensus/attestation/MuSig2 nonce/context/partial-sig/getoracles messages, including signed `oraclehb` heartbeats, are silently dropped until `IsOracleP2PActive` returns true.
- **Consensus:** DD transactions rejected with "digidollar-not-active". DD opcodes are not dispatched with DigiDollar semantics until `SCRIPT_VERIFY_DIGIDOLLAR` is set.

### Phase 2: STARTED (blocks 200–399 on testnet)
- **What happens:** Miners can now signal support by setting bit 23 in their block version.
- **Miner behavior:** `getblocktemplate` automatically includes bit 23 in the version field because `gbt_force=true`. Any miner using GBT (including cpuminer) signals automatically — no configuration needed.
- **Signaling check:** `getdeploymentinfo` RPC shows signal count and progress toward threshold.
- **User experience:** Same as DEFINED — everything still blocked. Qt overlay shows "STARTED" status.
- **Threshold:** 140 of 200 blocks in the window must signal bit 23 (70%).

### Phase 3: LOCKED_IN (blocks 400–599 on testnet)
- **What happens:** Threshold reached! Activation is guaranteed but delayed until `min_activation_height`.
- **Miner behavior:** Bit 23 is forced into block versions (`nVersion |= Mask`). All blocks signal.
- **User experience:** Still blocked. Qt overlay shows "LOCKED_IN" status. Users know activation is imminent.
- **Why the delay:** `min_activation_height` ensures all nodes have time to upgrade before DD transactions become valid.

### Phase 4: ACTIVE (block 600+ on testnet)
- **What happens:** DigiDollar is fully operational. MuSig2 oracle bundles are required in DD mint/redeem blocks; DD transfer-only and ordinary DGB blocks can omit the coinbase oracle bundle.
- **RPC behavior:** DD/oracle RPCs become functional (18 base in `src/rpc/digidollar.cpp`, 17 wallet-context in `src/wallet/rpc/wallet.cpp`).
- **P2P behavior:** Oracle messages (`oracleprice`, `oracleconsns`, `oracleattest`, `oramusnonce`, `oramusigctx`, `oramusigpsig`, `oraclehb`, `getoracles`) are processed, relayed, and validated according to the table below. Legacy `oraclebundle` messages are accepted on-wire but explicitly dropped — V1 carries the bundle on-chain in the coinbase, not via the bundle gossip message.
- **Consensus:** DD transactions are validated. DD opcodes are enforced via `SCRIPT_VERIFY_DIGIDOLLAR` (set in script flags when `DeploymentActiveAt(DEPLOYMENT_DIGIDOLLAR)` returns true).
- **Qt behavior:** Activation overlay disappears. Full DD tab (overview, send, receive, mint, redeem, positions, transactions) becomes accessible.
- **Oracle behavior:** Authorized oracle operators (slots 0-34 in `consensus.vOraclePublicKeys`, mainnet/testnet) can start their daemon, broadcast off-chain attestations and version heartbeats, participate in MuSig2 nonce/context/partial-sig rounds, and aggregate into the v0x03 on-chain bundle that miners embed in the coinbase for price-dependent DD blocks. `nDigiDollarMuSig2Height` is aligned with the effective DigiDollar activation boundary — MuSig2 v0x03 is the only accepted on-chain format from the moment DigiDollar/oracle consensus activates.

> **Activation boundary nuance (1-block off-by-one).** `getdeploymentinfo` exposes a BIP9 view (`bip9.status = active`) that flips at the period boundary block — i.e. the block whose `pindexPrev->nHeight + 1 == min_activation_height`. The height-based gate `Consensus::IsOracleActive(params, height) == (height >= nOracleActivationHeight)` flips one block later, at `height == min_activation_height` itself. There is therefore a single-block window where `bip9.status` reports `active` but `IsOracleActive(tip)` is still `false`. This is harmless on production because `IsDigiDollarEnabled(prev_block)` already returns true at the period boundary and the miner refuses price-dependent DD mint/redeem templates without a valid v0x03 bundle. DD transfer-only blocks do not need a block oracle price. Boundary tests should use the height-based predicate (`IsOracleActive`/`IsDigiDollarEnabled`) rather than `getdeploymentinfo.bip9.status` when they need the consensus-rule moment, and Wave 12's `DD-FA-SEC-010` fix in `SpendsDigiDollarCollateralVault` deliberately uses `min(nDDActivationHeight, BIP9 min_activation_height)` for the same reason.

> **Regtest activation knobs.** The direct `-digidollaractivationheight=N` regtest knob defined in `src/chainparams.cpp:102-109` now retargets both BIP9 `min_activation_height` and the static DD/oracle height gates (`nDDActivationHeight` / `nOracleActivationHeight`) in `src/kernel/chainparams.cpp:1222-1225`. Generic `-vbparams=digidollar:start:timeout:minheight` still overrides BIP9 only; because MuSig2 follows the effective DigiDollar BIP9 boundary, a BIP9-only test override can move MuSig2 validation without moving the static DD/oracle P2P gates. Default regtest remains intentionally special: BIP9 is `ALWAYS_ACTIVE` with `min_activation_height=0`, while the DD/oracle P2P height gates default to `650` for local testing. `nDigiDollarMuSig2Height` follows the effective BIP9 boundary (`0`) so v0x03 quotes are valid whenever DigiDollar is active. Startup oracle-price reconstruction follows the BIP9 predicate used by block connection, so BIP9-active default-regtest blocks below 650 are not skipped during restart/reindex cache rebuilds.

---

## What Gets Gated (Complete List)

### RPC Commands

The DigiDollar/oracle RPC surface is split between the node-context registration in `src/rpc/digidollar.cpp` (registered via `RegisterDigiDollarRPCCommands`) and the wallet-context registration in `src/wallet/rpc/wallet.cpp` (added inside `GetWalletRPCCommands`).

**Node-context (18, registered in `src/rpc/digidollar.cpp:6266`):**
- `getdigidollarstats`, `getdcamultiplier`, `calculatecollateralrequirement`, `getdigidollardeploymentinfo`, `importdigidollaraddress`, `estimatecollateral`
- `getoracleprice`, `getalloracleprices`, `getprotectionstatus`, `getoracles`, `getoraclesigners`, `listoracle`, `stoporacle`, `getoraclepubkey`
- Regtest helpers: `setmockoracleprice`, `getmockoracleprice`, `simulatepricevolatility`, `enablemockoracle`

**Wallet-context (17, added in `src/wallet/rpc/wallet.cpp:888`):**
`mintdigidollar`, `senddigidollar`, `sendmanydigidollar`, `redeemdigidollar`, `listdigidollarpositions`, `listdigidollaraddresses`, `getredemptioninfo`, `getdigidollarbalance`, `getdigidollaraddress`, `listdigidollartxs`, `listdigidollarunspent`, `listdigidollarutxos`, `validateddaddress`, `createoraclekey`, `exportoracleprivkey`, `importoracleprivkey`, `startoracle`.

`createoraclekey`, `exportoracleprivkey`, and `importoracleprivkey` are not activation-gated because they only manage wallet-local oracle signing keys. They do not start an oracle or publish prices. `startoracle` remains activation-gated.

**Removed / never present:** `sendoracleprice` was deleted as a fake-price-injection vulnerability; `submitoracleprice` does not exist anywhere in the source tree. Oracle prices come exclusively from live exchange aggregation aggregated under MuSig2. `src/rpc/digidollar_transactions.cpp` declares `getdigidollarinfo`, `transferdigidollar`, `createrawddtransaction`, and `listredeemablepositions`, but the file is **not registered** anywhere — treat it as legacy/unused.

**Gate pattern:** DD price/position/transaction/oracle-operation RPCs call `DigiDollar::IsDigiDollarEnabled(tip, chainman)` near the top of their handler. That helper checks the BIP9 `DEPLOYMENT_DIGIDOLLAR` state via `DeploymentActiveAfter()`. Local wallet key-management RPCs (`createoraclekey`, `exportoracleprivkey`, `importoracleprivkey`) and the deployment-status probe are intentionally usable before activation.

### P2P Message Handlers

`src/protocol.cpp` defines the on-wire command names; `src/net_processing.cpp` handles each one. Price, consensus, MuSig2, and `getoracles` handlers bail out early when `Consensus::IsOracleActive(params, height)` is false.

| Wire command (`src/protocol.cpp`) | C++ constant | Handler in `src/net_processing.cpp` | Notes |
|-----------------------------------|--------------|-------------------------------------|-------|
| `oracleprice` | `NetMsgType::ORACLEPRICE` | line 5452 | Off-chain oracle attestation (input to MuSig2) |
| `oraclebundle` | `NetMsgType::ORACLEBUNDLE` | line 5619 | **Accepted on the wire but always dropped** — V1 puts the bundle on-chain in the coinbase, not via gossip |
| `oracleconsns` | `NetMsgType::ORACLECONSENSUS` | line 5636 | Off-chain consensus proposal driving MuSig2 |
| `oracleattest` | `NetMsgType::ORACLEATTESTATION` | line 5755 | Per-oracle attestation supporting a consensus proposal |
| `oramusnonce` | `NetMsgType::ORACLEMUSIGNONCE` | line 5859 | MuSig2 round-1 public nonces |
| `oramusigctx` | `NetMsgType::ORACLEMUSIGCONTEXT` | line 5971 | MuSig2 context proposal fixing participant/nonce/quote set before partial signatures |
| `oramusigpsig` | `NetMsgType::ORACLEMUSIGPARTIALSIG` | line 6064 | MuSig2 round-2 partial signatures bound to a session context |
| `oraclehb` | `NetMsgType::ORACLEHEARTBEAT` | line 6175 | Signed oracle software/protocol heartbeat; telemetry, not a price input |
| `getoracles` | `NetMsgType::GETORACLES` | line 6262 | Pull request for missing oracle messages; replies with fresh `oracleprice` messages and recent `oraclehb` heartbeats |

**Note:** The price/consensus/MuSig2/getoracles gates use `Consensus::IsOracleActive(params, ActiveChain().Height())`, which is `nHeight >= params.nOracleActivationHeight`. On mainnet and testnet `nOracleActivationHeight` equals `nDDActivationHeight` and the BIP9 `min_activation_height`; on default regtest BIP9 is `ALWAYS_ACTIVE` at min height 0 while the P2P height gates default to 650. A peer sending gated oracle messages before the height gate is silently ignored — no ban, no misbehaviour penalty — just dropped at the start of each handler.

> **AUDIT NOTE:** As of the current code, `NetMsgType::ORACLEHEARTBEAT` uses `IsOracleP2PActive`, the same top-level activation helper used by the price, consensus, MuSig2, and `getoracles` handlers. It is also restricted to active consensus-roster oracle IDs, Schnorr-authenticated against chainparams, capped at 240 messages per peer per hour, deduplicated, and only returned by `getoracles` when recent.

> **Caveat.** The height predicate `IsOracleActive` is independent of the BIP9 deployment view (`getdeploymentinfo.bip9.status`). On production they collapse to the same trigger because chainparams pins `nOracleActivationHeight == nDDActivationHeight == BIP9 min_activation_height`. There is a single-block boundary window described in the Phase 4 nuance above where the BIP9 view says `active` but the height gate is still `false`. On default regtest, BIP9 `ALWAYS_ACTIVE` means block validation can be DD-active before the 650 P2P height gate; use `IsOracleActive` and `IsDigiDollarEnabled` deliberately for boundary tests when the P2P moment and consensus-rule moment differ.

### Consensus Validation (all BIP9-gated)

1. **Mempool acceptance** (`src/validation.cpp:976-989`): `DigiDollar::HasDigiDollarMarker(tx)` + `IsDigiDollarEnabled()` → rejects DD TXs with `TX_CONSENSUS "digidollar-not-active"`. After activation, mempool acceptance also requires that an oracle quote is available for any DD transaction (commit `81bf974f40`).
2. **Block validation** (`src/validation.cpp:2816-2854`): `DeploymentActiveAt(DEPLOYMENT_DIGIDOLLAR)` during `ConnectBlock()` delegates to `DeploymentActiveAfter(index.pprev, ...)`, so the candidate block is judged using the previous block's BIP9 state. Blocks containing DD TXs before activation are rejected. After activation, `ValidateBlockOracleData` (`src/oracle/bundle_manager.cpp:2151`) requires DD mint/redeem blocks to carry exactly one v0x03 MuSig2 oracle bundle in the coinbase. DD transfer-only and non-DD blocks may omit oracle data; if any block includes one it must still be a valid v0x03 bundle (commit `1e08bd811f`).
3. **Script verification** (`validation.cpp` script-flag setup): `SCRIPT_VERIFY_DIGIDOLLAR` flag only set when `DeploymentActiveAt()` returns true, so the Tapscript OP_SUCCESSx-class DD opcodes are not interpreted as DigiDollar operations before activation. Once active, `OP_CHECKPRICE` is reserved and deterministically disabled (`src/script/interpreter.cpp:708-735`): it consumes one stack item and pushes false rather than reading node-local oracle state.
4. **Mining graceful degradation** (`src/node/miner.cpp`, commit `6b5ff516c3`): `CreateNewBlock` strips price-dependent DD mint/redeem txs when no valid oracle bundle is available rather than aborting block assembly. Transfer-only DD txs are validated with oracle-price validation skipped because they do not need a block oracle price. The block is still produced; rejected DD txs remain in the mempool until either they confirm in a later attempt or are evicted.

Historical validation, IBD, and reorg handling follow the same predicates. `ValidateBlockOracleData()` returns true before the historical activation state, startup price-cache reconstruction only loads blocks that are BIP9-active for their historical context, IBD/catch-up skips oracle-dependent DD validation where the code cannot safely re-evaluate old wall-clock freshness with current state, and `DisconnectBlock()` removes the connected block's price-cache entry during reorg.

### Qt GUI

- **DigiDollar tab:** Always visible, but shows activation status overlay (QStackedWidget) when DD inactive
- **Sub-widget polling:** All DD widgets check `isVisible()` before making RPC calls — prevents RPC queue flooding when DD tab is hidden behind overlay
- **Activation check timer:** Runs every 5 seconds, calls `DigiDollar::IsDigiDollarEnabled()`. Stops and reveals DD functionality once active.

---

## Miner Signaling — How It Works

### Why miners signal automatically

The `VBDeploymentInfo` for DigiDollar has `gbt_force = true` (in `src/deploymentinfo.cpp`). This means:

1. During `STARTED` state, `getblocktemplate` includes bit 23 in `vbavailable`
2. Because `gbt_force=true`, the bit is NOT cleared even if the miner doesn't explicitly support "digidollar" in its GBT rules
3. The version field returned by `getblocktemplate` already has bit 23 set
4. cpuminer (and any GBT-based miner) uses this version directly → automatic signaling

### Block version format

```
Base version:  0x20000000 (BIP9 base)
+ Taproot bit: 0x00000004 (bit 2)
+ DD bit:      0x00800000 (bit 23)
= Combined:    0x20800004
```

During STARTED/LOCKED_IN, blocks should have version `0x20800004` or similar (with bit 23 set). Note: SegWit is a buried deployment in DigiByte (activated at a fixed height), not a version bits deployment, so it does not set any bit.

---

## Testing BIP9 Activation

### Functional Tests

1. **`digidollar_activation.py`** — Tests the full activation lifecycle:
   - Mines through DEFINED → STARTED → LOCKED_IN → ACTIVE
   - Verifies DD RPCs fail before activation, work after
   - Tests DD minting, sending, redeeming after activation
   - Checks version bits in block headers

2. **`digidollar_activation_boundary.py`** — Tests edge cases:
   - Exact block boundaries between phases
   - Threshold calculation (exactly 140/200)
   - Below-threshold signaling (remains STARTED)
   - min_activation_height enforcement

### Manual Testing Checklist

Before activation (any block < 600):
- [ ] DD price/position/transaction/oracle-operation RPCs return "DigiDollar is not yet active on this blockchain"
- [ ] Local oracle key-management RPCs (`createoraclekey`, `exportoracleprivkey`, `importoracleprivkey`) remain usable for pre-activation operator setup/recovery
- [ ] `getdeploymentinfo` shows correct BIP9 state
- [ ] Qt DD tab shows activation overlay
- [ ] No oracle messages processed (check debug.log; `IsOracleActive` returns false)
- [ ] DD transactions rejected from mempool with "digidollar-not-active"
- [ ] Block version includes bit 23 after STARTED (block 200+)

After activation (block 600+):
- [ ] DD RPCs functional
- [ ] Can mint DigiDollar (requires a valid MuSig2 oracle bundle in the mining template)
- [ ] Can send DigiDollar
- [ ] Can redeem DigiDollar
- [ ] Oracles can start (`createoraclekey` + `startoracle`) and broadcast attestations
- [ ] Oracle attestations, version heartbeats, and MuSig2 nonce/context/partial-sig messages propagate via P2P
- [ ] Qt DD tab shows full functionality
- [ ] `SCRIPT_VERIFY_DIGIDOLLAR` enabled in block script flags
- [ ] DD mint/redeem blocks include a v0x03 MuSig2 oracle bundle in the coinbase; DD transfer-only blocks can omit it (raw v0x01 / v0x02 wire payloads short-circuit inside `ExtractOracleBundle` and surface as `bad-oracle-malformed`; the `bad-oracle-legacy` branch is defense-in-depth and is not reached by current wire shapes)

---

## Mainnet Activation Timeline

On mainnet, the process is:

1. **Release:** Publish binaries with DigiDollar code and BIP9 deployment
2. **Upgrade period:** Miners and nodes upgrade (BIP9 start time: June 1, 2026)
3. **Signaling begins:** After start time (June 1, 2026), miners signal bit 23 in blocks
4. **Threshold reached:** 70% of blocks in a 40,320-block window (~1 week) signal support
5. **Lock-in period:** One more 40,320-block window for remaining nodes to upgrade
6. **Activation:** Block height reaches `min_activation_height` (23,627,520) and BIP9 is ACTIVE
7. **DigiDollar live:** All DD functionality enabled across the network

**Timeout:** If 70% signaling is not reached by June 1, 2027, the deployment transitions to FAILED. A new deployment with different parameters would be needed.

---

## Security Considerations

1. **Pre-activation protection:** All DD code paths are gated. A malicious node cannot trick other nodes into processing DD transactions or oracle messages before activation.

2. **No premature mining:** DD opcodes are not enforced as DigiDollar operations before activation. Even if someone crafts a transaction with DD opcodes, the DigiDollar semantics have no effect until `SCRIPT_VERIFY_DIGIDOLLAR` is set.

3. **Oracle P2P safety:** Price, consensus, MuSig2, `getoracles`, and signed `oraclehb` heartbeat messages received before `IsOracleP2PActive` are silently dropped (not banned).

4. **Consensus safety:** Mempool policy rejects DD-marker transactions before activation, but block consensus preserves base-chain compatibility: pre-activation DD-looking markers are treated as ordinary DGB data and DigiDollar semantics are not applied until BIP9 is ACTIVE.

5. **BIP9 guarantees:** The activation mechanism is the same one Bitcoin used for SegWit. It's battle-tested across multiple blockchains and provides clear upgrade coordination.

---

## Mainnet `nOracleActivationHeight` and `nDDActivationHeight`

Both heights are intentionally aligned in `src/kernel/chainparams.cpp`:

| Network | `nDDActivationHeight` | `nOracleActivationHeight` | `nDigiDollarMuSig2Height` |
|---------|-----------------------|---------------------------|---------------------------|
| Mainnet | `23627520` | `consensus.nDDActivationHeight` (i.e. `23627520`) | `consensus.nDDActivationHeight` (i.e. `23627520`) |
| Testnet26 | `600` | `600` | `600` |
| Regtest | `650` | `650` | `0` |

A practical implication: there is no period in which the oracle P2P surface is live but DD itself is not, and there is no period in which DD is active but MuSig2 v0x03 is not yet the on-chain bundle format. On mainnet/testnet the three numeric heights collapse to one event; on default regtest MuSig2 follows the BIP9 `ALWAYS_ACTIVE` boundary while DD/oracle height gates remain at 650 for local testing. Documents that say "mainnet `nOracleActivationHeight = 3000000`" are stale; that earlier staging configuration was removed before V1 launch.

## File Reference

| Component | File | Function |
|-----------|------|----------|
| BIP9 deployment params | `src/kernel/chainparams.cpp` | `vDeployments[DEPLOYMENT_DIGIDOLLAR]` |
| BIP9 state machine | `src/versionbits.cpp` | `AbstractThresholdConditionChecker` / `VersionBitsConditionChecker` |
| Deployment info | `src/deploymentinfo.cpp` | `VersionBitsDeploymentInfo[]` |
| RPC activation gate | `src/rpc/digidollar.cpp` | `IsDigiDollarEnabled()` check in each RPC |
| P2P activation gate | `src/net_processing.cpp` | `IsOracleP2PActive()` in `ORACLEPRICE`/`ORACLEBUNDLE`/`ORACLECONSENSUS`/`ORACLEATTESTATION`/`ORACLEMUSIGNONCE`/`ORACLEMUSIGCONTEXT`/`ORACLEMUSIGPARTIALSIG`/`GETORACLES`/`ORACLEHEARTBEAT` |
| Mempool gate | `src/validation.cpp:976-989` | `IsDigiDollarEnabled()` in `AcceptToMemoryPool`; recent MuSig2 quote required for DD txs |
| Block validation gate | `src/validation.cpp:2816-2854` | `DeploymentActiveAt(DEPLOYMENT_DIGIDOLLAR)` in `ConnectBlock` |
| Script flags | `src/validation.cpp:2755-2798` | `SCRIPT_VERIFY_DIGIDOLLAR` flag (set in `GetBlockScriptFlags`) |
| Qt activation overlay | `src/qt/digidollartab.cpp` | `checkActivationStatus()` timer |
| Qt widget polling guard | `src/qt/digidollar*widget.cpp` | `if (!isVisible()) return;` |
| Oracle height gate | `src/consensus/params.h:243-245` | `IsOracleActive()` |
| DD enabled check | `src/digidollar/digidollar.cpp` | `IsDigiDollarEnabled()` |
| Oracle bundle V1 enforcement | `src/oracle/bundle_manager.cpp` (`ValidateBlockOracleData`, `ExtractOracleBundle`, `CreateOracleScript`) | Raw v0x01/v0x02 OP_RETURN payloads short-circuit in `ExtractOracleBundle` (returns false), surfaced by the validator as `bad-oracle-malformed`. Only v0x03 MuSig2 bundles are accepted; the `bad-oracle-legacy` branch is defense-in-depth |
| Reserved `OP_CHECKPRICE` behavior | `src/script/interpreter.cpp:708-735` | `OP_CHECKPRICE` is reserved and deterministically disabled; the old `g_get_oracle_consensus_price` hook remains only for tests |
