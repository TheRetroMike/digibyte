# DigiDollar Oracle Testing Guide

This guide describes the current DigiDollar V1 oracle system used by RC44.

It is no longer a single-oracle Phase One setup. Production-style testnet uses a MuSig2 quorum:

- Public testnet: `testnet26`
- DigiDollar/oracle activation height: `600`
- Oracle epoch length: `40` blocks
- Oracle quorum: 7 signatures from the 21-active-key set inside a 35-slot reserve roster
- On-chain bundle format: v0x03 MuSig2 aggregate bundle
- Local developer harness: `./test_multi_oracle_testnet.sh`

The oracle price is valid only when a block contains a verified v0x03 bundle. Wallet caches, pending P2P messages, and operator heartbeats are useful for monitoring, but they are not consensus truth.

## Quick Operator Checks

```bash
digibyte-cli -testnet getblockchaininfo
digibyte-cli -testnet getnetworkinfo
digibyte-cli -testnet listoracle
digibyte-cli -testnet getoracles
digibyte-cli -testnet getoracleprice
```

`listoracle` shows the oracle running on the local node.

`getoracles` shows what this node currently knows about every configured oracle, including:

- Latest price source: local, pending, on-chain, or none.
- Whether the oracle is selected for the current epoch.
- Whether it is running locally.
- Latest signed version heartbeat.
- Reported client, P2P, oracle protocol, and MuSig2 context versions.

## Starting A Local Oracle

Use the private key assigned to the oracle ID. Do not use old Phase One demo keys on public testnet.

```bash
digibyte-cli -testnet startoracle <oracle_id> "<32-byte-private-key-hex>"
```

Then verify:

```bash
digibyte-cli -testnet listoracle
digibyte-cli -testnet getoracles true
```

Expected signs of health:

- `running: true` in `listoracle`.
- A fresh local price source when exchange fetching works.
- A fresh heartbeat for the oracle ID in `getoracles`.
- `musig2_context_version` matches the release.
- Other upgraded oracle nodes also show fresh heartbeats.

## What A Passing Epoch Looks Like

Every epoch should follow this pattern:

```text
Live exchange prices
      |
      v
Signed ORACLEPRICE gossip
      |
      v
Signed ORACLEMUSIGNONCE gossip
      |
      v
Chain-seeded signer ranking chooses threshold signers
      |
      v
Signed ORACLEMUSIGCONTEXT proposal with nonce and price evidence
      |
      v
Selected oracles broadcast ORACLEMUSIGPARTIALSIG
      |
      v
Aggregate Schnorr signature completes
      |
      v
Miner includes v0x03 bundle in coinbase
      |
      v
getoracleprice returns a nonzero validated chain price
```

## Main RC38 End-To-End Gate

The most important local release gate is:

```bash
./test_multi_oracle_testnet.sh
```

This script is the ecosystem test. It exercises live oracle price flow, MuSig2 signing, mining, minting, transfer chains, redemption, wallet persistence, restart, rescan, and reindex behavior on the local testnet harness.

Partial script progress is not a pass. It must finish end-to-end.

## Unit And Functional Gates

Run the full unit suite:

```bash
./src/test/test_digibyte --show_progress
```

Run the default functional suite:

```bash
test/functional/test_runner.py --jobs=4
```

Run the fuzz gate for all registered targets using the available corpus mode for the local build:

```bash
PRINT_ALL_FUZZ_TARGETS_AND_ABORT=1 ./src/test/fuzz/fuzz
test/fuzz/test_runner.py -l INFO --par=4 <corpus-path>
```

If no corpus is available and the build supports the empty-corpus runner, use:

```bash
mkdir -p /tmp/rc38_fuzz_corpus
test/fuzz/test_runner.py -l INFO --par=4 --empty_min_time=30 /tmp/rc38_fuzz_corpus
```

## Debug Log Checks

Useful filters:

```bash
tail -n 500 ~/.digibyte/testnet26/debug.log | rg -i 'oracle|musig|heartbeat|bundle|context|partial'
```

Healthy logs should show:

- Exchange price fetches.
- Oracle price broadcasts.
- Version heartbeat broadcasts.
- Nonce broadcasts for the current or next epoch.
- Context proposal acceptance.
- Partial signature broadcasts with a non-null context ID.
- MuSig2 session completion.
- Miner adding a v0x03 oracle bundle.

## Common Failure Patterns

### No price in `getoracleprice`

Meaning:

No recent block has a valid v0x03 oracle bundle.

Check:

- Are at least 9 oracle operators online and upgraded?
- Does `getoracles` show fresh heartbeats?
- Are fresh price messages present?
- Are nonce messages arriving for the current epoch?
- Are context proposals accepted or rejected?
- Are partial signatures all using the same `attempt_id` and `session_context_id`?

### Prices are visible but no bundle arrives

Meaning:

The issue is probably MuSig2 convergence, not exchange fetching.

Check:

- Context proposal evidence: nonce evidence and price evidence must verify.
- The selected signers must match chain-seeded ranking for known nonces.
- The proposer cannot use a remote seed; the local chain seed wins.
- Partial signatures from old attempts or old contexts must be ignored.

### Heartbeats are missing

Meaning:

The node has not seen a signed status heartbeat from that oracle.

Check:

- The operator is running RC38 or newer.
- The oracle key matches its assigned chainparams slot.
- P2P peers are connected.
- `debug.log` contains `Broadcast version heartbeat`.

Heartbeats are monitoring data only. Missing heartbeats do not invalidate the chain, but they are a strong clue that an operator is offline, isolated, or on an old build.

## Rules

Do not fix tests by weakening oracle safety:

- Do not add production fallback prices.
- Do not fake exchange prices in production-style testnet.
- Do not reduce the quorum.
- Do not accept unsigned or wrong-context MuSig2 messages.
- Do not reuse MuSig2 secret nonces.
- Do not let wallet/RPC caches become consensus truth.

If fewer than threshold oracles are available, the correct behavior is no new price bundle. That is fail-closed behavior, not a reason to invent a price.
