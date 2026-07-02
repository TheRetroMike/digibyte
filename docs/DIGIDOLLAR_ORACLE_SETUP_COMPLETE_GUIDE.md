# DigiDollar Oracle Setup Guide - V1

*Updated: 2026-05-30*

This is the current V1 oracle setup guide. Earlier notes about a single-slot
testnet oracle, `sendoracleprice`, or compact v0x01 block data are
historical and no longer apply.

## What V1 Runs

- Exchange aggregation from the active exchange fetchers.
- Signed oracle P2P messages.
- MuSig2 nonce and partial-signature rounds.
- One on-chain v0x03 MuSig2 bundle in the coinbase when a block contains
  DigiDollar activity.
- 7 signatures from the 35-active mainnet/testnet keyset, and 4-of-7 quorum on regtest.

## Operator Prerequisites

1. Build a wallet-enabled node.
2. Run on the intended network with `server=1`.
3. Keep the oracle wallet online and unlocked only as required for oracle key
   access.
4. Confirm the operator's `oracle_id` is inside the consensus-active roster for
   the network.

## Configuration

Example:

```ini
server=1
txindex=1
digidollar=1
```

Do not add `oracle` or `oracleid` config keys. Oracle identity is wallet/RPC
driven in V1: create or load the key with `createoraclekey`, then start the
operator with `startoracle` after DigiDollar is active.

Network-specific options such as `testnet=1` or `regtest=1` are still required
for non-mainnet operation.

## Key Lifecycle

Create or load the oracle key through wallet RPC:

```bash
./src/digibyte-cli -rpcwallet=<wallet> createoraclekey <oracle_id>
./src/digibyte-cli -rpcwallet=<wallet> startoracle <oracle_id>
```

`createoraclekey` refuses out-of-roster IDs. `startoracle` loads the wallet key
and starts the oracle participant for that slot.

Stop local oracle operation with:

```bash
./src/digibyte-cli stoporacle <oracle_id>
```

## Health Checks

Use:

```bash
./src/digibyte-cli getoracles
./src/digibyte-cli listoracle
./src/digibyte-cli getoracleprice
./src/digibyte-cli getalloracleprices 10
```

Expected V1 behavior:

- recent blocks with DD activity contain one valid v0x03 bundle
- ordinary non-DD blocks may contain no oracle output
- legacy v0x01/v0x02 bundle payloads reject as malformed
- duplicate oracle outputs reject as `bad-oracle-multiple-outputs`
- DD-touching blocks without a bundle reject as `bad-oracle-missing`

## Removed Legacy Flow

The following commands and assumptions are not part of V1:

- `sendoracleprice`
- manual price injection through RPC
- single-slot testnet oracle consensus
- v0x01/v0x02 on-chain fallback
- unsigned compact oracle block data

V1 consensus accepts only MuSig2 v0x03 bundles after activation.
