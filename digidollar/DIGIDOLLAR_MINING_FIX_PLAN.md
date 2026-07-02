# DigiDollar Mining Fix Plan

Date: 2026-06-25

Status: discussion plan only. Do not implement from this file until reviewed.

## Goal

Fix the mining interface issues found during the modified mainnet DigiDollar
oracle test without creating fork risk.

The core requirement is:

**Non-upgraded miners and pools must still be able to create valid normal DGB
blocks. DigiDollar-aware miners and pools must be able to mine DD mint/redeem
blocks safely by preserving the oracle commitment.**

We must not create a situation where old or partially upgraded mining software
receives a template that looks normal but actually requires DigiDollar-specific
coinbase handling.

## Current Findings

We have three separate paths to fix or harden.

### 1. Core `getblocktemplate`

Core currently exposes DigiDollar oracle data through GBT when a candidate block
contains an oracle coinbase output.

Relevant code paths:

- `src/rpc/mining.cpp`
- `src/node/miner.cpp`
- `src/oracle/bundle_manager.cpp`

The risk is not consensus itself. The risk is that an external builder can take
a GBT template, rebuild coinbase incorrectly, include DD mint/redeem
transactions, and submit a block that fails validation.

Failure mode:

```text
bad-oracle-missing
```

### 2. Local `cpuminer-multi`

The local `cpuminer-multi` GBT path calculates the normal block merkle root from
raw transaction `data`.

That is wrong for witness transactions. For the block header merkle root, it
must use each transaction's `txid`.

Relevant code path:

- `/home/jared/Code/cpuminer-multi/cpu-miner.c`

Failure mode:

```text
bad-txnmrklroot
```

### 3. DigiHashV2 / `node-stratum-pool`

DigiHashV2 delegates block construction to `node-stratum-pool`.

`node-stratum-pool` already uses `txid` for merkle branches, which is good. But
its coinbase builder currently does not copy DigiDollar's
`default_oracle_commitment` into the generated coinbase.

Relevant code paths:

- `/home/jared/Code/digihashv2/libs/poolWorker.js`
- `/home/jared/Code/node-stratum-pool/lib/pool.js`
- `/home/jared/Code/node-stratum-pool/lib/blockTemplate.js`
- `/home/jared/Code/node-stratum-pool/lib/transactions.js`

Failure mode:

```text
bad-oracle-missing
```

## Non-Negotiable Safety Rules

1. Non-upgraded miners must still produce valid DGB blocks.
2. Non-upgraded miners must not accidentally receive DD mint/redeem work that
   requires special coinbase handling.
3. Miners and pools must explicitly opt into oracle-priced DD GBT behavior
   before receiving DD mint/redeem work.
4. DD mint/redeem transactions must only appear in GBT templates for builders
   that can preserve the oracle commitment.
5. DD transfer-only transactions should keep mining normally when they are
   otherwise consensus-valid; they do not require an oracle bundle.
6. If an oracle commitment is required and missing, the block must still be
   rejected by consensus.
7. We do not weaken DigiDollar validation to make broken miners pass.

## Correct Core GBT Fix

### Add An Oracle-Priced DigiDollar GBT Opt-In

Core should treat oracle-priced DigiDollar mining support as an explicit GBT
opt-in.

Proposed request shape:

```json
{
  "rules": ["segwit", "digidollar-oracle"]
}
```

or, if we decide capability is cleaner:

```json
{
  "rules": ["segwit"],
  "capabilities": ["digidollar-oracle"]
}
```

Recommended choice:

Use `rules: ["segwit", "digidollar-oracle"]`.

Reason:

DigiDollar mint/redeem blocks require the miner or pool to understand the
oracle coinbase commitment requirement. That is closer to a required GBT rule
than a cosmetic capability.

Do not use the existing `digidollar` rule name for this opt-in. That name is
already the BIP9 deployment rule and is forced into GBT by versionbits metadata.
The mining opt-in needs a separate name so "this node understands DigiDollar
activation" is not confused with "this miner preserves the oracle coinbase
commitment".

Important scope:

This opt-in is for DD work that needs an oracle price. It is not meant to block
DD transfer-only transactions, which do not require an oracle bundle.

### Legacy GBT Behavior

If the GBT caller does not opt into DigiDollar:

- Core should still return a valid template.
- Core should exclude DD mint/redeem transactions from the template.
- Core may still include DD transfer-only transactions, if they are otherwise
  valid for block inclusion.
- Core may still return normal DGB transactions.
- Core should not add a DigiDollar oracle bundle to the coinbase.
- Core should not return `default_oracle_commitment`.
- Core should not return a DigiDollar-only `coinbasetxn`.
- Any block built from that template should be a valid normal DGB block.

This is the fork-risk prevention step.

Simple rule:

**No DigiDollar opt-in means no DD mint/redeem transactions in the GBT
template.**

Stronger safety rule:

**No DigiDollar opt-in also means no opportunistic oracle bundle in the GBT
coinbase.**

This is a policy choice to discuss, not a consensus requirement. Consensus only
requires the oracle bundle for mint/redeem blocks. The reason to consider this
rule is to keep legacy GBT callers from receiving coinbase data they do not know
they must preserve. It may temporarily reduce DD mint/redeem throughput if
miners are not upgraded, but DD transfer-only transactions can keep flowing.

### DigiDollar-Aware GBT Behavior

If the GBT caller opts into DigiDollar:

- Core may include DD mint/redeem transactions only if a fresh oracle bundle is
  available.
- Core must expose `default_oracle_commitment` when a DD mint/redeem transaction
  is present.
- Core should expose `default_oracle_commitment` when a fresh oracle bundle is
  available even if the candidate block has no mint/redeem transaction. That
  lets upgraded miners and pools keep the on-chain price feed moving in plain
  blocks.
- Core may stamp the oracle bundle into the template coinbase for internal
  mining and DigiDollar-aware templates.
- Core should mark the template as requiring DigiDollar-aware handling.

Recommended response behavior:

- Add `!digidollar-oracle` to the returned `rules` array when the template
  contains DD mint/redeem transactions.
- Include `default_oracle_commitment` when a fresh oracle bundle is available for
  a DigiDollar-aware template. It is consensus-required only when mint/redeem is
  present, but preserving it in plain blocks is required for price-feed liveness.
- Keep `default_witness_commitment` behavior unchanged.

### GBT Template Cache Must Include DigiDollar Awareness

Current GBT caches a static block template and only rebuilds when the chain tip,
mempool update counter, or requested mining algorithm changes.

If DigiDollar opt-in changes whether oracle-priced DD transactions and oracle
coinbase outputs are allowed, then that awareness must become part of the GBT
cache key.

Required cache behavior:

- Track the last template's DigiDollar-aware flag.
- Rebuild when the current request's DigiDollar-aware flag differs from the
  cached template's flag.
- Never serve an oracle-priced DD template to a legacy caller.
- Never serve a legacy stripped template to a DD-aware caller if mint/redeem txs
  are now eligible.

This is mandatory fork-risk protection.

### BlockAssembler Should Get Explicit Mining Policy Options

The clean Core hook is `BlockAssembler::Options`.

Add options with names like:

```cpp
bool include_oracle_priced_digidollar_txs{true};
bool include_oracle_bundle{true};
```

Then GBT can call `BlockAssembler` differently depending on the request:

- legacy GBT: `include_oracle_priced_digidollar_txs=false`,
  `include_oracle_bundle=false`
- DD-aware GBT: `include_oracle_priced_digidollar_txs=true`,
  `include_oracle_bundle=true`
- internal mining / `generatetoaddress`: keep defaults true

Important:

The existing unit-test contract says internal mining should stamp oracle bundles
into plain non-DD blocks when a completed MuSig2 session exists. Do not break
that. The legacy-GBT suppression is only for external callers that did not opt
into DigiDollar-aware coinbase handling.

The DD mint/redeem filter should live in the miner transaction-selection path,
before a price-dependent DD tx enters the candidate block. Do not rely on a late
post-processing pass as the primary safety mechanism.

Reason:

The existing code already has the right logical split:

- `BlockNeedsOraclePrice()` returns true only for `DD_TX_MINT` and
  `DD_TX_REDEEM`.
- `TransactionNeedsOraclePriceForMiner()` also returns true only for
  `DD_TX_MINT` and `DD_TX_REDEEM`.

The fix should follow that split. DD transfer-only transactions do not require
an oracle bundle and should not be arbitrarily stripped from legacy GBT.

### Avoid Unsafe Dummy `coinbasetxn`

Current GBT can return a `coinbasetxn` built from Core's template coinbase.
Because GBT does not know the external miner's payout address, that coinbase can
carry a dummy payout script.

That is dangerous for real mining software.

Correct long-term contract:

- Prefer `coinbasevalue` plus `default_oracle_commitment`.
- Let miners and pools build their own payout output.
- Require DigiDollar-aware builders to copy `default_oracle_commitment` into
  coinbase when present.

Optional future improvement:

Add a GBT request field such as `coinbaseaddress` or `payout_script` so Core can
return a safe `coinbasetxn` with the miner's real payout. That should be treated
as a separate enhancement, not the primary safety fix.

## Correct `cpuminer-multi` Fix

### Fix The Merkle Root Calculation

In the GBT path, `cpuminer-multi` should:

1. Continue appending each transaction's raw `data` to the submitted block.
2. Use each transaction's `txid` field as the merkle leaf when `txid` is
   present.
3. Fall back to hashing `data` only for old templates that do not provide
   `txid`.
4. Preserve byte order correctly when converting RPC txid hex into internal
   merkle bytes.

This fixes witness transaction templates generally, not just DigiDollar.

### Add DigiDollar-Aware GBT Support

If we want `cpuminer-multi` to mine DD mint/redeem blocks, it must also become
DigiDollar-aware:

1. Add an explicit opt-in, for example `--digidollar`, defaulting off.
2. When enabled, request GBT with `rules: ["segwit", "digidollar-oracle"]`.
3. Build its own coinbase payout from the configured payout address.
4. Append a zero-value output using `default_oracle_commitment` when present.
5. Include the witness commitment correctly.
6. Recalculate the coinbase txid and normal merkle root from txids.
7. Submit the full block with the oracle output and raw transaction data.

If we only fix the merkle bug but do not add oracle-commitment handling, then
`cpuminer-multi` should request legacy-safe templates and mine valid normal DGB
blocks plus any valid DD transfer-only transactions. It should not receive DD
mint/redeem transactions until it can preserve `default_oracle_commitment`.

Recommended first step:

- Fix txid-based merkle roots.
- Keep local cpuminer on legacy-safe GBT unless/until its oracle commitment
  support is added and tested.

## Correct DigiHashV2 / Pool Fix

### Fix `node-stratum-pool` Coinbase Outputs

`node-stratum-pool` should copy Core's `default_oracle_commitment` into the
coinbase whenever it is present.

Correct behavior:

1. Pool calls GBT with DigiDollar opt-in.
2. Core returns DD mint/redeem transactions only when the pool opted in and a
   fresh oracle commitment is available.
3. Core returns `default_oracle_commitment` when required for mint/redeem and
   may return it for plain blocks to keep oracle prices flowing on-chain.
4. Pool builds its normal payout coinbase.
5. Pool adds a zero-value output for `default_oracle_commitment`.
6. Pool preserves/adds the witness commitment.
7. Pool builds merkle branches using txids.
8. Pool submits a full block with the oracle output present.

### Keep GPU / ASIC Miners Unchanged

GPU and ASIC miners should not need DigiDollar logic.

They only receive Stratum jobs and hash headers. The pool is responsible for
constructing a valid block.

### Avoid Global Breakage In Multi-Coin Pool Code

`node-stratum-pool` supports more than DigiByte. Do not add DigiDollar behavior
globally in a way that changes other coins.

Recommended approach:

- Add a coin config flag such as `"digidollar": true`.
- Only add `rules: ["digidollar-oracle"]` when that flag is enabled.
- Only add `default_oracle_commitment` when the daemon provides it.
- Leave other coins unchanged.
- GPU / ASIC workers connected over Stratum do not change. They keep hashing the
  pool job; the pool is the DigiDollar-aware block builder.

Update DigiHashV2 DigiByte coin configs:

- `digibyte.scrypt.json`
- `digibyte.sha256.json`
- `digibyte.skein.json`
- `digibyte.qubit.json`
- `digibyte.odo.json`
- testnet equivalents if needed

## Required Tests

### Core Functional Tests

Add focused tests around GBT and DigiDollar mining.

Test 1: legacy-safe GBT

- Activate DigiDollar.
- Create a DD mint transaction in mempool.
- Call `getblocktemplate({"rules":["segwit"]})`.
- Assert the DD mint/redeem transaction is not in the template.
- If a DD transfer-only transaction is also in mempool, assert it can remain in
  the template when otherwise valid.
- Assert `default_oracle_commitment` is absent.
- Assert `coinbasetxn` is absent unless required by some non-DigiDollar rule.
- Assert the template can be assembled into a valid normal DGB block.

Test 2: DigiDollar-aware GBT

- Activate DigiDollar.
- Publish a fresh oracle quote.
- Create a DD mint transaction.
- Call `getblocktemplate({"rules":["segwit","digidollar-oracle"]})`.
- Assert the DD mint/redeem transaction is present.
- Assert `default_oracle_commitment` is present.
- Assemble a block like an external builder with the oracle output included.
- Submit it.
- Assert it is accepted.

Test 3: DD-aware plain block carries oracle commitment

- Activate DigiDollar.
- Publish a fresh oracle quote.
- Leave the mempool without DD mint/redeem transactions.
- Call `getblocktemplate({"rules":["segwit","digidollar-oracle"]})`.
- Assert `default_oracle_commitment` is present.
- Build and submit the block with the oracle output included.
- Assert it is accepted and the oracle bundle is visible on-chain.

Test 4: internal mining still bootstraps oracle price

- Use `generatetoaddress` or the unit-test `BlockAssembler` path.
- Publish a fresh oracle quote.
- Mine a plain block with no DD mint/redeem transactions.
- Assert the block coinbase carries `OP_RETURN OP_ORACLE`.
- This preserves the existing no-deadlock behavior.

Test 5: missing oracle output rejection

- Use a DigiDollar-aware template with a DD mint/redeem transaction.
- Build a block but deliberately omit `default_oracle_commitment`.
- Submit it.
- Assert rejection:

```text
bad-oracle-missing
```

Test 6: non-DD transactions still mine

- Activate DigiDollar.
- Create only normal DGB transactions.
- Call legacy GBT.
- Build and submit the block.
- Assert it is accepted.

Test 7: cache separation

- Build a DD-aware template with DD mint/redeem transactions eligible.
- Then call legacy GBT without changing tip or mempool.
- Assert the legacy response does not reuse the DD-aware template.
- Then call DD-aware GBT again.
- Assert the DD-aware response still includes eligible DD mint/redeem txs and
  `default_oracle_commitment`.

### `cpuminer-multi` Tests

Add a small deterministic test or fixture for GBT merkle behavior:

- Build a synthetic GBT transaction entry with both `txid` and witness-bearing
  `data`.
- Assert the merkle leaf uses `txid`.
- Assert hashing raw `data` would produce a different value.
- Assert the submitted block still appends the raw `data`.

Add a second fixture for DigiDollar-aware coinbase handling:

- Build a fake GBT response with `default_oracle_commitment`,
  `default_witness_commitment`, one normal transaction, and a payout address.
- Run the coinbase-building path with DigiDollar opt-in enabled.
- Assert the serialized coinbase contains exactly one oracle output with the
  daemon-provided script.
- Assert the witness commitment is still present.
- Assert the merkle root is recalculated from the new coinbase txid plus GBT
  transaction txids.
- Assert the default, non-DigiDollar mode does not request `rules:
  ["digidollar-oracle"]`.

If full automated tests are not practical in that C repo, add a small standalone
test program or script that runs in CI/manual validation.

### `node-stratum-pool` Tests

Add a focused Node test:

- Build a fake GBT response with `default_oracle_commitment`.
- Run the coinbase generation code.
- Assert the serialized coinbase includes that exact oracle commitment.
- Assert witness commitment remains present.
- Assert no oracle output is invented when the field is absent.
- Assert the output count matches the actual number of outputs after adding both
  witness and oracle commitments.
- Assert `GetBlockTemplateParameters()` adds `rules: ["segwit",
  "digidollar-oracle"]`
  only when `options.coin.digidollar === true`.

Optional integration test:

- Feed a fake template with txids and transaction data into `BlockTemplate`.
- Ensure the merkle branch uses `txid`, not raw transaction data.
- Feed the same template through `serializeBlock()` and assert raw transaction
  `data` is still appended.

### DigiHashV2 Config Tests

Add a small config smoke test:

- Load each `coins/digibyte.*.json` file.
- Assert `"digidollar": true` is present for the five DigiByte mainnet algos.
- Assert unrelated coin configs do not automatically get DigiDollar behavior.
- If testnet coin configs are used for DigiDollar mining, assert the same flag
  there too.

## Acceptance Criteria

Before release, all of these must be true:

1. Legacy GBT clients mine valid normal DGB blocks after DigiDollar activation.
2. Legacy GBT clients do not receive DD mint/redeem transactions by default.
3. DigiDollar-aware GBT clients receive DD mint/redeem transactions only with a
   fresh oracle commitment.
4. DigiDollar-aware GBT clients can also preserve oracle commitments in plain
   blocks so on-chain prices keep updating before the next mint/redeem.
5. A DD-aware externally assembled block with the oracle commitment is accepted.
6. The same DD block without the oracle commitment is rejected.
7. `generatetoaddress` / internal mining still stamps oracle bundles into plain
   blocks when a completed MuSig2 session exists.
8. `cpuminer-multi` no longer submits witness-template blocks with bad merkle
   roots.
9. DigiHashV2 / `node-stratum-pool` includes `default_oracle_commitment` in the
   generated coinbase.
10. GPU / ASIC miners do not require changes when mining through a fixed pool.
11. Existing DigiDollar unit and functional tests still pass.
12. Full functional test runner still passes.

## Rollout Strategy

Recommended order:

1. Add failing Core functional tests for legacy-safe and DD-aware GBT behavior.
2. Implement the Core GBT opt-in filter.
3. Run focused Core tests, then DigiDollar unit and functional tests.
4. Add failing `node-stratum-pool` tests for GBT rules and oracle coinbase
   output support.
5. Implement pool oracle-commitment output support.
6. Add DigiHashV2 coin config flags and config smoke tests.
7. Run pool/DigiHash tests.
8. Add failing `cpuminer-multi` fixtures for txid merkle and oracle coinbase.
9. Patch `cpuminer-multi` txid merkle handling and DD-aware opt-in coinbase
   output support.
10. Validate cpuminer against a local node with a witness transaction template
    and then with a DD mint/redeem template.
11. Run full Core functional tests.
12. Only after all three paths pass, test modified mainnet again.

## Open Questions To Decide Before Coding

1. Should the GBT opt-in be `rules: ["digidollar-oracle"]`, `capabilities:
   ["digidollar-oracle"]`, or both?
2. Should Core ever return `coinbasetxn` for DigiDollar templates without a
   provided payout address?
3. Do we want `cpuminer-multi` to become fully DigiDollar-aware now, or only fix
   its witness merkle bug and leave DD-aware mining to pools/internal mining
   first?
4. Should DD transfer-only transactions remain eligible in legacy GBT templates
   when they pass normal validation?
5. Should legacy GBT suppress opportunistic oracle bundle stamping even for
   otherwise normal DGB blocks?
6. Should `cpuminer-multi` DD support be an explicit runtime option such as
   `--digidollar`, or should it be enabled automatically when the daemon returns
   the `digidollar-oracle` rule?

## Recommended Answers

1. Use `rules: ["segwit", "digidollar-oracle"]` for DD-aware GBT. Do not reuse
   the existing BIP9 `digidollar` rule name as the mining opt-in.
2. Do not return unsafe dummy-payout `coinbasetxn` as the primary contract.
3. Fix `cpuminer-multi` merkle handling first; add full DD-aware coinbase
   support only with tests.
4. Yes. Keep DD transfer-only transactions eligible in legacy GBT templates when
   otherwise valid. Exclude only DD mint/redeem work because those operations
   require an oracle price bundle.
5. Yes. Legacy GBT should suppress opportunistic oracle bundle stamping so old
   builders are never asked to preserve DigiDollar-only coinbase data.
   DigiDollar-aware GBT and internal mining should still stamp oracle bundles in
   plain blocks when available, because that is how the on-chain price feed
   bootstraps.
6. Use an explicit `--digidollar` option for `cpuminer-multi`. Default-safe
   behavior avoids surprising existing miners, while the modified-mainnet test
   can run cpuminer with DigiDollar support intentionally enabled.

## Code Validation Notes

These findings were checked against the current code before implementation:

- `getblocktemplate` already parses arbitrary request `rules` into
  `setClientRules`, so `rules: ["segwit", "digidollar-oracle"]` is feasible.
- Current GBT only requires `segwit`; it does not currently check for a
  DigiDollar opt-in.
- The existing `digidollar` deployment name has `gbt_force=true` in
  `src/deploymentinfo.cpp`, so it must not be reused as the explicit mining
  opt-in. The opt-in needs a distinct name such as `digidollar-oracle`.
- Current GBT cache state tracks tip, mempool update count, time, and mining
  algorithm, but not DigiDollar awareness. That must be fixed with the opt-in.
- Current GBT calls `BlockAssembler{active_chainstate, &mempool}` without any
  custom options, so `BlockAssembler::Options` is the right extension point.
- `BlockAssembler::Options` already exists, but currently only covers block
  weight, minimum fee, validity testing, and a test hook.
- `ValidateDDForBlockInclusion()` is the right miner-selection gate for
  excluding oracle-priced DD mint/redeem transactions from legacy GBT templates.
- Existing `RemoveDDTransactionsFromBlock()` strips only price-dependent
  mint/redeem transactions. That matches the consensus split and should not be
  widened to strip DD transfer-only transactions.
- `CreateNewBlock()` currently calls `AddOracleBundleToBlock()` whenever
  DigiDollar is enabled and a bundle is available, not only when the block has a
  DD mint/redeem transaction. Existing unit tests require this behavior for
  internal mining to avoid an oracle-price bootstrapping deadlock. Legacy GBT
  therefore needs an explicit option if we decide to suppress oracle-bundle
  stamping for non-opted-in external callers.
- Consensus validation only requires an oracle output when the block contains a
  price-dependent DD mint/redeem operation. Normal DGB blocks without oracle
  outputs remain valid.
- Consensus scans all coinbase outputs for `OP_RETURN OP_ORACLE`, so the pool
  can add the oracle output in a normal coinbase output list; it does not have
  to be a fixed output index.
- The normal block merkle root uses transaction `GetHash()` / txid, while the
  witness merkle root uses witness hashes. This validates the `cpuminer-multi`
  txid-merkle fix.
- `cpuminer-multi` currently hashes raw transaction `data` for each GBT
  transaction merkle leaf, confirming the `bad-txnmrklroot` diagnosis for
  witness-bearing templates.
- `node-stratum-pool` already uses `txid` for merkle branches and appends raw
  transaction `data` to the submitted block, which is the correct split.
- `node-stratum-pool` currently preserves `default_witness_commitment` but has
  no handling for `default_oracle_commitment`.
- Current DigiHashV2 DigiByte coin JSON files do not contain a DigiDollar flag,
  so adding one is the clean way to opt only DigiByte pools into DD-aware GBT.

## Bottom Line

The safe architecture is opt-in DigiDollar mining for external builders.

Non-upgraded miners keep mining normal DGB blocks.

Upgraded miners and pools explicitly request DigiDollar templates, preserve the
oracle commitment, and can safely mine DD mint/redeem blocks.

Consensus remains strict: a DD mint/redeem block without the oracle bundle must
fail.
