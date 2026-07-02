# DigiDollar Testnet Oracle Setup - V1

*Updated: 2026-05-30*

This guide describes the current DigiDollar V1 oracle flow on `testnet26`.
Older `sendoracleprice` / single-oracle setup instructions are obsolete:
`sendoracleprice` was removed and raw v0x01/v0x02 oracle bundles are rejected.

## Testnet Parameters

- Network: `testnet26`
- P2P port: `12033`
- Bech32 HRP: `dgbt`
- DigiDollar activation: height 600 / BIP9 active
- Oracle activation: same trigger as DigiDollar
- On-chain bundle format: MuSig2 v0x03 only
- Consensus roster: 35 active slots, IDs `0..34`; slot ID `35` is outside the RC44 roster
- Quorum: 7 signatures from the active keyset

## Required Configuration

Use a normal testnet node with wallet support enabled. For an oracle operator,
the wallet must hold the operator key for the configured oracle slot.

Example `digibyte.conf` entries:

```ini
testnet=1
server=1
txindex=1
digidollar=1
```

Do not add `oracle` or `oracleid` config keys. Oracle identity is wallet/RPC
driven in V1: create or load the key with `createoraclekey`, then start the
operator with `startoracle` after DigiDollar is active.

The oracle private key should be created or loaded through the wallet RPC flow:

```bash
./src/digibyte-cli -testnet -rpcwallet=<wallet> createoraclekey <oracle_id>
./src/digibyte-cli -testnet -rpcwallet=<wallet> startoracle <oracle_id>
```

`startoracle` loads the wallet key, starts exchange-price fetching, broadcasts
signed oracle messages, participates in MuSig2, and makes completed v0x03
bundles available to miners.

## Validation Checks

Check deployment state:

```bash
./src/digibyte-cli -testnet getdigidollardeploymentinfo
```

Check oracle status:

```bash
./src/digibyte-cli -testnet getoracles
./src/digibyte-cli -testnet listoracle
./src/digibyte-cli -testnet getoracleprice
./src/digibyte-cli -testnet getalloracleprices 10
```

The node should report recent MuSig2-backed prices once v0x03 bundles are being
mined. DD transactions will not enter the mempool without a recent valid MuSig2
quote, but ordinary DGB transactions continue to relay and mine without oracle
data.

## Miner Behavior

Miners include an oracle bundle only when the block template contains
DigiDollar activity and a valid v0x03 bundle is available. Non-DD blocks do not
need oracle data. If a DD template cannot obtain a valid v0x03 bundle, the
miner drops the DD transactions rather than mining a legacy fallback.

## Removed Commands

Do not use these older instructions:

- `sendoracleprice`
- `submitoracleprice`
- manual v0x01/v0x02 OP_ORACLE payloads
- retired single-slot testnet setup

They do not describe V1 production or testnet behavior.
