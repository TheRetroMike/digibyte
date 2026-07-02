# MAINNET PRE ORACLE GBT ISSUE

Date: 2026-06-25

Update: this file documents the first PRE oracle/GBT issue investigation. A
broader summary covering the later external miner and pool findings is now in
`DIGIDOLLAR_MINING_ISSUES_SUMMARY.md`.

## SHORT ANSWER

In the committed `v9.26.1-pre` mainnet-PRE code at `HEAD=e382064d6a`,
oracle-bundled blocks are effectively only produced by nodes that have a local
running oracle session.

That is not the intended design and not a consensus rule. External CPU miners do
not need oracle keys. The real requirement is that the node serving
`getblocktemplate` must have a completed local MuSig2 session so it can place
the v0x03 oracle bundle in the coinbase.

The current committed code prevents passive non-oracle template nodes from
finishing that MuSig2 session, so they return coinbase-only templates.

## WHAT IS HAPPENING

The modified mainnet PRE chain is active and mining valid blocks. DigiDollar and
MuSig2 are active at height 600. The problem is that mined blocks are missing
the oracle bundle, so `bundle_count` stays at zero and `getoracleprice` stays
zero.

Live PRE evidence from local RPC ports 14046-14049:

```text
status=active
activation_height=599
oracle_activation_height=600
musig2_format_activation_height=600
musig2_session.state=nonces_complete
musig2_session.nonce_count=18
musig2_session.partial_sig_count=0
getblocktemplate.coinbasetxn=false
getblocktemplate.default_oracle_commitment=false
```

The logs show the real failure:

```text
Rejected MuSig2 partial sig ... context mismatch ... session=000000...
Buffered partial sig ... (session not SIGNING yet)
TickEpochSession ... state=2 is_oracle=0
```

State `2` is `NONCES_COMPLETE`. The node has received remote nonces and partial
signatures, but it never selected the signed context and therefore never entered
`SIGNING` or `COMPLETE`.

## WHY GBT IS MISSING ORACLE DATA

`getblocktemplate` does not create oracle data by itself. It only reports oracle
fields when the locally assembled candidate block already has an oracle
coinbase output.

Code path:

- `getblocktemplate` calls `BlockAssembler::CreateNewBlock()`:
  `src/rpc/mining.cpp:952`
- `CreateNewBlock()` calls `OracleBundleManager::AddOracleBundleToBlock()` after
  DigiDollar activation:
  `src/node/miner.cpp:523-529`
- `AddOracleBundleToBlock()` asks the local signing orchestrator for a completed
  session:
  `src/oracle/bundle_manager.cpp:830-841`
- `GetCompletedSession()` returns false unless the local session is `COMPLETE`:
  `src/oracle/signing_orchestrator.cpp:1428-1440`
- GBT only returns `coinbasetxn` and `default_oracle_commitment` if the coinbase
  contains `OP_RETURN OP_ORACLE`:
  `src/rpc/mining.cpp:1098-1118` and `src/rpc/mining.cpp:1162-1168`

So if the GBT node is stuck at `NONCES_COMPLETE`, the template is correctly
coinbase-only.

## WHY TESTNET LOOKED DIFFERENT

The old testnet harness did not exercise this topology.

`test_multi_oracle_testnet.sh` starts a local mini-testnet with `-easypow`.
That flag does two things on testnet:

- enables easy PoW
- switches to a local deterministic oracle roster

The harness then starts 24 local oracle slots across the same 8 Qt nodes that
are used for local mining/template assembly. Bob mines with `generatetoaddress`,
and Bob is also an oracle-hosting node.

Relevant paths:

- `test_multi_oracle_testnet.sh:10-18`
- `test_multi_oracle_testnet.sh:756-790`
- `src/kernel/chainparams.cpp:650-724`

The modified mainnet PRE harness is different:

- 5 mainnet-identity Qt nodes
- real PoW
- remote 35-slot mainnet oracle topology
- only DigiSwarm is local on Bob
- external CPU miners request templates from local RPC nodes

Relevant paths:

- `test_oracle_mainnet.sh:16-35`
- `test_oracle_mainnet.sh:457-482`
- `test_oracle_mainnet.sh:700-727`

That means the PRE template nodes must work as passive non-oracle aggregators.
The committed code does not let them do that.

## ROOT CAUSE

The intended design says non-oracle nodes collect nonce/context/partial-signature
messages and aggregate the final MuSig2 signature:

- `REPO_MAP_DIGIDOLLAR.md:531-535`

But the committed code gates context selection behind `is_oracle`:

```cpp
if (is_oracle && state == MuSig2SessionState::NONCES_COMPLETE) {
    ...
    SelectReadyContextProposal(...)
    ...
}
```

That is in `HEAD:src/oracle/signing_orchestrator.cpp:1214`.

Because of that gate, a passive GBT node can:

- receive remote nonces
- reach `NONCES_COMPLETE`
- store remote context proposals
- receive remote partial signatures

But it cannot:

- select the authenticated remote context
- enter `SIGNING`
- replay buffered partial signatures
- aggregate the final signature
- expose a completed session to `getblocktemplate`

So the current committed mainnet-PRE code effectively makes oracle-bundled block
templates depend on the template-serving node being an oracle node.

## WHY THIS FIXES GENERATETOADDRESS AND GETBLOCKTEMPLATE

Both mining paths use the same block-template assembly code.

`generatetoaddress`:

- calls `generateBlocks()`
- `generateBlocks()` calls `BlockAssembler::CreateNewBlock()`
- then Core solves and submits that block locally

Relevant path:

- `src/rpc/mining.cpp:218-227`

`getblocktemplate`:

- directly calls `BlockAssembler::CreateNewBlock()`
- returns the assembled template to external mining software

Relevant path:

- `src/rpc/mining.cpp:946-954`

So the fix belongs in the MuSig2 session state feeding
`BlockAssembler::CreateNewBlock()`, not in `cpuminer`, not in GBT response
formatting, and not in `generatetoaddress` itself.

## WHAT THIS IS NOT

This is not a `cpuminer` bug. `cpuminer` only consumes the template returned by
RPC.

This is not a PoW validity bug. The chain is mining valid blocks.

This is not a consensus rule requiring oracles to mine. A non-oracle external
miner can mine an oracle-bundled block if it points at a GBT node that already
has a completed MuSig2 session.

This is not a testnet/mainnet validation mismatch. Mainnet PRE exposed a
topology that local `-easypow` testnet did not cover.

## PROPOSED FIX

Allow passive non-oracle nodes to perform MuSig2 context convergence.

Concretely:

1. Run the `NONCES_COMPLETE` context-selection block for all nodes, not only
   oracle nodes.
2. Keep local context proposal creation behind `is_oracle`.
3. Keep local partial-signature creation and broadcast behind `is_oracle`.
4. Let passive nodes select the authenticated remote context, enter `SIGNING`,
   replay buffered remote partial signatures, aggregate the final signature, and
   serve that completed session to GBT.

The safe shape is:

```cpp
if (state == MuSig2SessionState::NONCES_COMPLETE) {
    chosen_context = SelectReadyContextProposal(...);

    if (!chosen_context && is_oracle) {
        local_context = BuildLocalContextProposal(...);
        BroadcastMusigContext(...);
        return;
    }

    if (chosen_context) {
        session->SetSignedValues(...);
        session->TrimNoncesToParticipants(...);
        session->SetKeyAggCache(...);
        session->AggregateNonces(...);

        if (is_oracle) {
            CreatePartialSignature(...);
            BroadcastMusigPartialSig(...);
        }
    }
}

if (state == SIGNING) {
    DrainPendingPartialSigsForEpoch(...);
}
```

This keeps private-key actions oracle-only while making aggregation work on
ordinary template-serving nodes.

## REQUIRED TEST

Add a focused unit regression:

1. Create a fresh `OracleSigningOrchestrator` with no local oracle keys.
2. Feed it valid remote MuSig2 nonce messages.
3. Feed it a valid signed remote context proposal.
4. Feed it valid remote partial signatures.
5. Call `OnBlockConnected()`.
6. Assert:
   - session moves from `NONCES_COMPLETE` to `COMPLETE`
   - `GetCompletedSession()` returns true
   - aggregate signature is 64 bytes
   - signed price/timestamp match the context

This test fails against committed `HEAD=e382064d6a` because the passive node
never enters the context-selection block. It passes with the proposed fix.

## ACCEPTANCE CRITERIA

After rebuilding and restarting the PRE Qt nodes:

```text
getdigidollardeploymentinfo.musig2_session.state == complete
getdigidollardeploymentinfo.musig2_session.partial_sig_count >= 7
getblocktemplate has coinbasetxn == true
getblocktemplate has default_oracle_commitment == true
new mined blocks contain one OP_RETURN OP_ORACLE v0x03 output
getoracleprice returns non-zero after a bundled block connects
```

The fix should not change consensus rules. It only lets non-oracle nodes finish
the already-authenticated MuSig2 aggregation path they were designed to support.
