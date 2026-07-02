# DigiDollar Mining Paths Explainer

Date: 2026-06-25

This explains the main ways blocks can be mined during DigiDollar testing, and
why the issues we found show up differently depending on the mining path.

## 1. `generatetoaddress`

This is Core mining internally.

The node builds the whole block itself:

1. Selects transactions from the mempool.
2. Creates the coinbase transaction.
3. Adds the witness commitment.
4. Adds the DigiDollar oracle bundle when needed.
5. Recalculates the merkle root.
6. Solves the block.
7. Submits the block.

Why it works:

Core controls every step, so this is the clean reference path. If
`generatetoaddress` mines a DD mint or redeem transaction, Core adds the oracle
bundle correctly.

Issue found:

No issue found in this path so far.

## 2. `getblocktemplate`

This is not mining by itself.

`getblocktemplate` is Core handing an external miner or pool a block recipe.

Core provides data such as:

- previous block hash
- block version
- target / difficulty
- transactions to include
- coinbase value
- witness commitment
- DigiDollar oracle commitment, when needed

The external miner or pool must finish the block correctly.

Issue risk:

If the external builder includes DD mint or redeem transactions but does not
include the DigiDollar oracle commitment in the coinbase, Core rejects the block:

```text
bad-oracle-missing
```

Simple version:

Core gives the recipe. The miner or pool still has to bake it correctly.

## 3. `cpuminer-multi`

This is a solo external miner using `getblocktemplate`.

The flow is:

1. `cpuminer-multi` asks Core for a template.
2. It builds or uses a coinbase transaction.
3. It calculates the merkle root.
4. It hashes block headers.
5. It submits the solved block back to Core.

Issue found:

Our local `cpuminer-multi` calculates the merkle root incorrectly when the
template contains witness transactions.

It hashes each transaction's raw `data` field. For witness transactions, that
can produce the witness hash instead of the normal transaction ID.

For the block header merkle root, it must use the transaction's `txid`.

DigiDollar mint and redeem transactions use witness data, so this bug becomes
visible during DigiDollar testing.

Failure seen:

```text
bad-txnmrklroot
```

Simple version:

The miner solved a block, but the header committed to the wrong merkle root, so
Core rejected it.

## 4. Pool / GPU / ASIC Mining

GPU and ASIC miners usually do not build full blocks.

The pool builds the block job, and the miners only hash the headers they are
given.

The flow is:

```text
Core getblocktemplate
  -> pool builds coinbase, merkle root, and Stratum job
  -> GPU / ASIC miners hash shares
  -> pool submits the solved block
```

DigiHashV2 uses this model. It delegates the actual block construction work to
`node-stratum-pool`.

What looks good:

`node-stratum-pool` uses `txid` for merkle branches, so it does not appear to
have the same witness merkle bug as `cpuminer-multi`.

Issue found:

`node-stratum-pool` currently preserves the witness commitment, but it does not
copy DigiDollar's `default_oracle_commitment` into the coinbase.

So if the pool includes a DD mint or redeem transaction but omits the oracle
commitment, Core rejects the block:

```text
bad-oracle-missing
```

Simple version:

The GPU or ASIC miner is not the problem. The pool's block builder must include
the DigiDollar oracle output when Core provides it.

## Summary Table

| Mining path | Who builds the block? | Main issue found |
| --- | --- | --- |
| `generatetoaddress` | Core | No issue found. Core builds the complete valid block. |
| `getblocktemplate` | External miner or pool finishes Core's recipe | Builder must preserve oracle commitment for DD mint/redeem blocks. |
| `cpuminer-multi` | `cpuminer-multi` | Uses raw witness tx `data` for merkle root instead of `txid`. Can fail with `bad-txnmrklroot`. |
| Pool / GPU / ASIC | Pool builds block, miners only hash | Pool must copy `default_oracle_commitment`. Otherwise DD mint/redeem blocks fail with `bad-oracle-missing`. |

## Bottom Line

`generatetoaddress` works because Core builds everything internally.

External miners and pools must do two things correctly:

1. Use txid-based merkle roots.
2. Include the DigiDollar oracle commitment in the coinbase when the template
   contains DD mint or redeem transactions.

Miners do not need to be oracles. They only need a correctly built block.
