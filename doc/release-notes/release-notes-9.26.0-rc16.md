# DigiByte v9.26.0-rc16 Release Notes

⚠️ **WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

Development Branch: https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

Join the Developer Chat: https://app.gitter.im/#/room/#digidollar:gitter.im

## Overview

RC16 focuses on DigiDollar wallet reliability, P2P transaction relay, oracle network stability, and Qt user experience improvements. All fixes ensure the DD wallet behaves identically to mainnet DGB patterns.

**Upgrade strongly recommended** — RC15 has a critical oracle P2P bug that causes cascading peer disconnections, leading to oracle consensus failure and inability to mine DigiDollar transactions.

## Testnet Quick Start

```bash
# Build from source
git clone -b feature/digidollar-v1 https://github.com/DigiByte-Core/digibyte.git
cd digibyte
./autogen.sh
./configure --with-gui=qt5
make -j$(nproc)

# Run testnet
src/qt/digibyte-qt -testnet

# Or headless
src/digibyted -testnet -daemon
```

**Recommended digibyte.conf for testnet:**
```
testnet=1
server=1
txindex=1

[test]
digidollar=1
digidollarstatsindex=1
algo=scrypt
rpcuser=digibyte
rpcpassword=<your-password>
fallbackfee=3
addnode=oracle1.digibyte.io
addnode=35.90.208.148
addnode=24.239.32.145
addnode=124.187.73.15
```

## Bug Fixes

### Critical: Premature DD UTXO Erasure (`d591861`)
Fixed a bug where DigiDollar UTXOs were being erased from the wallet's tracking database immediately when creating transactions, before they were confirmed on-chain. This caused the wallet to "forget" about DD positions if a transaction didn't confirm right away.

**What changed:** DD UTXOs are now only erased upon block confirmation (1-conf), matching the exact same pattern mainnet DGB uses for `IsSpent()`. Removed premature `dd_utxos.erase()`/`batch.EraseDDUTXO()` calls from 5 TX-creation paths: TransferDigiDollar, UpdatePositionStatus, BurnDigiDollars, RedeemDigiDollar, and MarkDDUTXOsSpent.

**Tests:** 8 new tests in `digidollar_utxo_lifecycle_tests.cpp`.

### Critical: Dandelion++ P2P Transaction Relay (`0caa0e8`)
Fixed two bugs in the Dandelion++ privacy relay that caused transactions to get stuck:

1. **Stem relay spam:** Transactions were being re-routed through the stem phase on every `SendMessages()` cycle instead of just once. Added `m_dandelion_stem_routed` tracking set to prevent duplicate routing.

2. **New peer starvation:** When peers disconnected and new ones connected, the new peers had empty inventory sets and never learned about existing mempool transactions. Added mempool seeding on a peer's first INV cycle so new peers immediately learn about all pending transactions.

**Tests:** 2 new tests in `dandelion_tests.cpp`.

### Qt: Oracle Price Drift Detection (`12ecfd9`)
The mint widget now re-checks the oracle price at the moment you click "Mint" and warns you if the price has changed since the form was loaded, showing old vs. new collateral requirements. The redeem widget refreshes oracle price every 15 seconds. Oracle poll timer reduced from 30s to 12s for more responsive pricing.

### Wallet: DD Available vs Pending Balance Separation (`6e7fce9`)
`GetTotalDDBalance()` now returns only confirmed (1+ confirmations) DD balance. New `GetPendingDDBalance()` function for unconfirmed DD. The Qt overview widget now shows separate "Available" and "Pending" DD balances, matching how mainnet DGB displays confirmed vs unconfirmed.

### Fix: startoracle RPC Context (`209d99b`)
`startoracle` RPC now correctly uses wallet context instead of node context, preventing crashes when called from the Qt console.

## Consensus / Activation

### BIP9 Activation Gating (multiple commits)
- `SCRIPT_VERIFY_DIGIDOLLAR` flag now properly activated after BIP9 deployment (`32b4b9c`)
- Oracle P2P handlers gated by activation height (`49ac3375`)
- DD RPC commands (mint, transfer, burn, redeem) gated by BIP9 activation (`14374cd`)
- Pre-activation DD validation removed from `CheckTransaction` (`c086fd9`)
- DigiDollar tab hidden in Qt when not activated (`5bf1dba`)
- `nDDActivationHeight` aligned with BIP9 `min_activation_height` of 22,014,720 (`ec53a43`)

### Critical: Oracle P2P Rate Limiter Causing Peer Disconnections (`7793cd3`)
The oracle message rate limiter in `net_processing.cpp` had a limit of 50 novel messages/peer/hour, but oracles broadcast every 15 seconds — producing 1,920 novel messages/hour with 8 oracles (7,200 on mainnet with 30). After ~12 minutes, every new oracle relay message called `Misbehaving(+5)`, rapidly banning peers and causing cascading loss of oracle data. Once oracle count dropped below 5, no consensus bundle could be created and all DD transactions became unmineable.

**Fixes:**
- Oracle broadcast interval reduced from 15s to 60s (exchanges don't update faster; 60s still gives 12-25x redundancy per epoch)
- Rate limit increased from 50 to 3,600 messages/peer/hour (30 oracles × 60/hr × 2x headroom)
- `Misbehaving()` penalty completely removed from rate limiter — oracle relay is legitimate P2P behavior, excess messages are silently dropped

**Tests:** 4 new tests verifying broadcast interval and rate limit constants.

## Other Changes

- Updated Aussie (oracle ID 6) public key (`883be11`)
- Fixed pre-existing test failures (`52544cd`)
- Added BIP9 activation boundary tests (`c107d61`, `fc24208`)

## Test Results

All 1515 unit tests pass. Regtest and testnet integration tests pass.

## Commits Since RC15

```
067a8beca8 fix: oracle P2P rate limiter causing cascading peer disconnections
0caa0e84a1 fix: resolve two critical Dandelion++ P2P transaction relay bugs
6e7fce9099 wallet/qt: separate DigiDollar available and pending balances
d591861158 wallet: prevent premature DD UTXO erasure — match mainnet DGB pattern
12ecfd9a13 qt: detect oracle price drift in mint/redeem widgets before TX submission
883be119fa chainparams: update Aussie (oracle ID 6) pubkey for RC15
52544cd173 test: fix pre-existing test failures
209d99b7f7 fix: startoracle RPC uses wallet context, not node context
c107d61040 test: add activation boundary test for DigiDollar BIP9 deployment
fc2420859d test: add activation boundary test for DigiDollar BIP9 deployment
c086fd94e1 consensus: remove pre-activation DD validation from CheckTransaction
ec53a4347d consensus: align nDDActivationHeight with BIP9 min_activation_height (22014720)
14374cd2d9 fix: gate mutating DD RPC commands by BIP9 activation
5bf1dbf8a8 ui: hide DigiDollar tab when not activated
32b4b9c160 fix: activate SCRIPT_VERIFY_DIGIDOLLAR flag after BIP9 deployment
49ac3375a0 fix: gate oracle P2P handlers by activation height
```

## Key Files Changed

| File | Change |
|------|--------|
| `configure.ac` | Version bump RC15 → RC16 |
| `src/net_processing.cpp` | Oracle rate limiter fix (50→3600, remove Misbehaving); Dandelion++ stem routing fix; new peer mempool seeding |
| `src/net.h` | Added `m_dandelion_stem_routed` tracking set |
| `src/dandelion.cpp` | Clear stem-routed set on peer disconnect/shuffle |
| `src/oracle/node.cpp` | Broadcast interval 15s → 60s |
| `src/oracle/node.h` | Default broadcast interval 300 → 60, added `GetBroadcastInterval()` |
| `src/oracle/bundle_manager.cpp` | Updated comments for 60s interval |
| `src/wallet/digidollarwallet.cpp` | Removed premature UTXO erasure from 5 TX paths; added `GetPendingDDBalance()` |
| `src/qt/digidollarmintwidget.cpp` | Oracle price re-check on mint click |
| `src/qt/digidollarredeemwidget.cpp` | 15s auto-refresh of oracle price |
| `src/qt/digidollaroverviewwidget.cpp` | Separate Available/Pending DD balance display |
| `src/kernel/chainparams.cpp` | Updated Aussie oracle 6 pubkey; BIP9 activation gating |
| `src/validation.cpp` | SCRIPT_VERIFY_DIGIDOLLAR flag; pre-activation DD validation removal |
| `src/rpc/digidollar.cpp` | Gate DD RPCs by BIP9 activation; startoracle wallet context fix |

## Reporting Issues

Please report bugs in the DigiDollar Gitter chat or open a GitHub issue:
- Gitter: https://app.gitter.im/#/room/#digidollar:gitter.im
- GitHub: https://github.com/DigiByte-Core/digibyte/issues
