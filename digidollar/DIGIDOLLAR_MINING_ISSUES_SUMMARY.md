# DigiDollar Mining Issues Summary

Date: 2026-06-25

This document summarizes what we found during the modified mainnet DigiDollar
oracle test. It is written for people who understand the DigiDollar testing
effort, but have not followed every debugging step.

## Short Version

We found more than one issue, and they were getting mixed together.

DigiDollar itself did activate. Oracle signing also worked. The problem was in
how mined blocks were being assembled by different mining paths.

The key point is simple:

**A miner does not need to be an oracle to mine DigiDollar blocks. But the block
producer must build the block correctly. For DD mint/redeem blocks, that means
the block must include the fresh oracle bundle in the coinbase and the merkle
root must be calculated correctly.**

Internal Core mining through `generatetoaddress` does this correctly. Some
external mining paths did not.

## What DigiDollar Requires From Mining

After DigiDollar is active:

- Normal DGB blocks can still be mined normally.
- DD transfer-only blocks do not need a price bundle.
- DD mint and redeem blocks need a fresh oracle price bundle.
- That oracle bundle is placed in the coinbase transaction as an
  `OP_RETURN OP_ORACLE` output.
- The final block must have a correct txid-based merkle root.

So the mining rule is not "only oracles can mine."

The real rule is:

**Any miner or pool can mine, but the block template/coinbase builder must carry
the oracle commitment into the final solved block when the block contains a DD
mint or redeem transaction.**

## Issue 1: Oracle Bundle Was Not Reaching The Chain

The first issue looked like this:

- MuSig2 oracle signing completed.
- The aggregate signature was valid.
- Oracle messages were flowing.
- But `bundle_count` stayed at zero.
- `getoracleprice` stayed zero.
- Minting stayed blocked network-wide.

The reason was that the final v0x03 oracle bundle is not gossiped as a normal
P2P object. It is committed into the coinbase of a mined block.

That means a bundle only reaches the chain when the node producing the block
template has a completed oracle session available and includes that bundle in
the coinbase.

This does not mean the miner needs oracle keys. It means the node serving
`getblocktemplate`, or the node doing local mining, must have the completed
bundle available at template time.

## Issue 2: `generatetoaddress` Worked Because Core Built The Whole Block

`generatetoaddress` is the clean reference path.

When Core mines internally:

1. Core builds the block.
2. Core selects the mempool transactions.
3. Core adds the witness commitment.
4. Core adds the DigiDollar oracle bundle when needed.
5. Core recalculates the merkle root.
6. Core solves and submits the block.

That is why a script using `generatetoaddress` across the five DigiByte algos
can confirm DigiDollar mints correctly.

This is important because it proves the consensus rules can accept the block
when the block is assembled correctly.

## Issue 3: Local `cpuminer-multi` Has A Real GBT Merkle Bug

We found a separate bug in the local `cpuminer-multi` code.

When `cpuminer-multi` receives a `getblocktemplate` response, it builds the
block merkle root by hashing each transaction's raw `data` field.

That is wrong when the template contains witness transactions.

For SegWit-style blocks, the normal block merkle root must use each
transaction's `txid`, not the witness hash. DigiDollar mint/redeem transactions
carry witness data, so this bug becomes visible during DigiDollar testing.

The failure looks like this:

```text
bad-txnmrklroot
```

That means the solved block had the wrong merkle root in the header.

This is not a DigiDollar consensus failure. It is an external miner block
assembly failure.

## Issue 4: DigiHashV2 / `node-stratum-pool` Builds Blocks Differently

DigiHashV2 does not directly build the block. It delegates that work to
`node-stratum-pool`.

The pool path is:

1. DigiHashV2 starts the pool worker.
2. `node-stratum-pool` calls `getblocktemplate`.
3. `node-stratum-pool` builds the coinbase transaction.
4. `node-stratum-pool` builds the merkle branch and Stratum job.
5. GPU, ASIC, or CPU miners hash the header from the pool job.
6. The pool submits a completed candidate block back to the daemon.

This means GPU and ASIC miners are not the risky part. They are only hashing the
job they are given.

The risky part is the pool's block builder.

The good news: `node-stratum-pool` already uses `txid` for merkle branches, so
it does not appear to have the same witness merkle bug as `cpuminer-multi`.

The bad news: its coinbase builder currently preserves the witness commitment
but does not preserve DigiDollar's `default_oracle_commitment`.

So if the pool includes a DD mint or redeem transaction but leaves out the
oracle coinbase output, Core rejects the block as:

```text
bad-oracle-missing
```

Again, this does not mean pools cannot mine DigiDollar. It means the pool
software must include the oracle commitment when Core provides one.

## Issue 5: Core GBT Is Currently Trying To Help, But The Contract Is Awkward

Core's `getblocktemplate` currently exposes DigiDollar oracle data in two ways
when the candidate block has an oracle output:

- `coinbasetxn`
- `default_oracle_commitment`

The intent is good: give external miners enough information to include the
oracle bundle.

But there is a practical problem. The `coinbasetxn` path uses the template
coinbase that Core built with a dummy payout script. That can cause confusion
for real mining software, because miners and pools normally want to build their
own payout output.

The clean long-term contract should be:

- If a miner uses Core's full `coinbasetxn`, it must preserve the oracle output
  and pay correctly.
- If a pool rebuilds its own coinbase, it must copy `default_oracle_commitment`
  into the coinbase when present.
- If a miner or pool cannot include the oracle commitment, it must not mine DD
  mint/redeem transactions from that template.

## Why This Was Confusing

Several things were true at the same time:

- The chain was mining valid blocks.
- Oracle signing was completing.
- Some DD transactions confirmed through `generatetoaddress`.
- Some cpuminer blocks were rejected.
- Some blocks had no oracle bundle.
- Another miner's generate script confirmed transactions correctly.

That made it look like one single bug. It was not one single bug.

The situation is better explained as three different mining paths:

| Mining path | What happens |
| --- | --- |
| Core `generatetoaddress` | Works because Core builds the complete valid block internally. |
| Local `cpuminer-multi` using GBT | Can fail with `bad-txnmrklroot` because it hashes witness tx `data` instead of `txid`. |
| DigiHashV2 / `node-stratum-pool` | Merkle path looks better, but the pool must be updated to include `default_oracle_commitment`. |

## Why Testnet Did Not Expose This Cleanly

Testnet did have non-oracle miners. That part is true.

The missing coverage was more specific:

- We did not have enough testing of external GBT miners assembling DD
  mint/redeem blocks with witness transactions.
- We did not have enough testing of pool software rebuilding coinbase while
  preserving DigiDollar oracle commitments.
- Many successful testnet paths used Core's internal mining or mining setups
  that did not hit this exact combination of witness merkle handling plus
  oracle coinbase commitment handling.

So testnet proved a lot, but it did not fully prove every external miner and
pool block-building path.

## What Needs To Be Fixed

### 1. Fix `cpuminer-multi`

`cpuminer-multi` must use the GBT `txid` field for the normal block merkle root.

It can still include the full transaction `data` in the submitted block, but the
header merkle root must be built from txids.

This is standard SegWit/GBT behavior.

### 2. Fix `node-stratum-pool`

`node-stratum-pool` must include `default_oracle_commitment` as a zero-value
coinbase output whenever Core provides it.

It must continue to include the witness commitment correctly.

This allows DigiHashV2 pools to mine DD mint/redeem blocks without requiring
pool miners to know anything about DigiDollar internals.

### 3. Add Regression Tests

We need tests that prove:

- Core GBT exposes the oracle commitment when a DD mint/redeem tx is in the
  template.
- A block assembled like an external miner submits successfully when it includes
  the oracle commitment.
- The same block fails with `bad-oracle-missing` if the DD tx is included but
  the oracle commitment is removed.
- `cpuminer-multi` computes merkle roots from `txid`, not raw witness-bearing
  transaction data.
- `node-stratum-pool` coinbase output generation preserves
  `default_oracle_commitment`.

## What This Does Not Mean

This does not mean DigiDollar activation failed.

This does not mean only oracle nodes can mine.

This does not mean GPU or ASIC miners need DigiDollar-specific logic.

This does not mean the DigiDollar consensus rules are rejecting valid blocks.

It means some external block builders need to be updated so they build the same
valid block that Core already builds internally.

## Bottom Line

DigiDollar mining is not broken at the consensus level.

The problem is at the mining interface layer:

- GBT miners must build the merkle root correctly for witness transactions.
- Pools must preserve the DigiDollar oracle commitment in coinbase.
- Our tests need to cover those external block-builder paths before release.

Once those are fixed and tested, non-oracle miners and pools should be able to
mine DigiDollar mint/redeem blocks normally.

## Useful Code Paths

- Core GBT: `src/rpc/mining.cpp`
- Core block assembly: `src/node/miner.cpp`
- Oracle bundle coinbase insertion: `src/oracle/bundle_manager.cpp`
- Oracle signing session source: `src/oracle/signing_orchestrator.cpp`
- Local cpuminer GBT assembly: `/home/jared/Code/cpuminer-multi/cpu-miner.c`
- DigiHashV2 pool worker: `/home/jared/Code/digihashv2/libs/poolWorker.js`
- Stratum pool GBT/job code: `/home/jared/Code/node-stratum-pool/lib/pool.js`
- Stratum pool coinbase builder: `/home/jared/Code/node-stratum-pool/lib/transactions.js`
- Stratum pool merkle builder: `/home/jared/Code/node-stratum-pool/lib/blockTemplate.js`

## Standards Background

- BIP22 / BIP23 define `getblocktemplate` mining templates.
- BIP141 / BIP145 define the witness commitment and explain why the normal
  block merkle root uses transaction IDs, not witness hashes.
