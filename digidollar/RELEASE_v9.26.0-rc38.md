# DigiByte Core v9.26.0-rc38 Release Notes

**WARNING: RC38 is testnet-only until Jared reviews, tags, releases, and deploys it.**

RC38 is a DigiDollar oracle liveness and operator-visibility release on top of RC37. It keeps `testnet24`, keeps the 9-of-17 V1 quorum, and keeps the on-chain v0x03 oracle bundle format.

The headline:

> RC38 makes the off-chain MuSig2 signing context attempt-aware and evidence-bound, then adds signed oracle version heartbeats so operators can see who is online and what version they are running.

Development branch: `feature/digidollar-v1`

Developer chat: https://app.gitter.im/#/room/#digidollar:gitter.im

---

## Read This First

RC38 does not reset the public DigiDollar testnet.

It stays on `testnet24`. It does not change activation heights, oracle quorum, epoch length, DigiDollar economics, wallet database format, oracle roster, or the v0x03 bundle committed in blocks.

What changed from RC37:

- MuSig2 nonce, context, and partial-signature messages now carry an `attempt_id`.
- MuSig2 session context IDs now commit to the attempt ID.
- Context proposals now carry signed nonce evidence and signed price evidence.
- Context proposal hashes/signatures commit to the nonce set hash, quote set hash, nonce evidence, and price evidence.
- Nodes validate a context proposal from the included evidence before signing it.
- Local signing no longer immediately self-signs a just-built context in the same tick; it relays the proposal first and signs only after the context path is selected.
- Live P2P relay now feeds MuSig2 nonce and partial-signature messages into the signing orchestrator only, avoiding the old epoch-only bundle-manager session path.
- Oracle nodes now broadcast signed version heartbeats.
- `getoracles` now reports heartbeat status and reported client/P2P/oracle/MuSig2 context versions for each oracle.
- `listoracle` now reports the local node's version/protocol fields and last heartbeat time.
- `ORACLE_BUNDLE_EXPLAINER.md` and the oracle testing guide were updated for RC38.

What did not change:

- No mainnet activation is included in RC38.
- No testnet reset is included in RC38.
- The V1 quorum remains 9-of-17.
- The on-chain v0x03 bundle format is unchanged.
- The oracle roster, activation heights, epoch length, and DigiDollar economic rules are unchanged.
- No production mock or fallback price path was added.
- Validators still verify the final bundle from the block.

---

## Testnet24 Network Details

These values are unchanged from RC37.

| Item | RC38 value |
| --- | --- |
| Testnet name | `testnet24` |
| Data directory | `testnet24` |
| Genesis hash | `0xe42636c490059fafe7e0278acc6fb451b901b6a316b31e10d7ccff565baf23df` |
| Merkle root | `0x502bf477644933ced36281bbfdcc6755895b3d9f75262eb148d2c1c2c21d7e73` |
| Genesis time | `2026-05-11 13:53:00 UTC` |
| Genesis nonce | `57535` |
| Network magic | `fe c4 b7 e5` |
| Default P2P port | `12031` |
| Default RPC port | `14026` |
| DigiDollar activation height | `600` |
| Oracle activation height | `600` |
| Oracle epoch length | `40` blocks |
| Oracle quorum | `9-of-17` |
| Oracle bundle format | `v0x03` MuSig2 aggregate bundle |

---

## RC38 In Plain English

RC37 made oracle nodes agree on a signed context before they sent partial signatures. That was the right direction, but live behavior still showed a real-world risk: if nodes restart, receive messages in different orders, or see an older signing attempt next to a newer one, they can still fail to converge cleanly.

RC38 tightens the off-chain transcript.

Every MuSig2 signing message now says which attempt it belongs to. A context proposal now carries the actual signed evidence used to build the context:

- The selected signers' nonce messages.
- The selected signers' price messages.
- A hash of the nonce set.
- A hash of the price quote set.
- The final price and timestamp computed from that evidence.

That means an oracle does not need to guess what the proposer saw. It can check the evidence directly before signing.

RC38 also adds operator heartbeats. These are signed status messages that say, "oracle ID X is online and running this client/protocol/context version." They do not set the price and they are not mined. They are there so operators can quickly see who is upgraded and who is missing.

---

## Architecture Record

### 1. MuSig2 messages are attempt-aware

The issue:

MuSig2 secret nonces are single-use. If a signing round stalls and a new round starts inside the same epoch, messages from the old round must not mix with messages from the new round.

The fix:

RC38 adds `attempt_id` to:

- `OracleMusigNonceMsg`
- `OracleMusigContextMsg`
- `OracleMusigPartialSigMsg`

The message hashes and authentication hashes include `attempt_id`. Session context IDs also include `attempt_id`.

Why it matters:

A nonce, context proposal, or partial signature from attempt `0` cannot be accepted into attempt `1`. That protects liveness and avoids dangerous nonce reuse confusion.

Code/tests touched:

- `src/oracle/musig2_messages.h`
- `src/protocol.{h,cpp}`
- `src/oracle/musig2_session.{h,cpp}`
- `src/oracle/signing_orchestrator.{h,cpp}`
- `src/test/musig2_p2p_message_tests.cpp`
- `src/test/musig2_session_tests.cpp`

### 2. Context proposals now carry evidence

The issue:

The proposer announces a signing context, but other nodes need to know exactly which nonce and price inputs produced that context. Without evidence, honest nodes can disagree because their local pending pools are not identical at the same instant.

The fix:

RC38 extends `OracleMusigContextMsg` with:

- `nonce_set_hash`
- `quote_set_hash`
- `nonce_evidence`
- `price_evidence`

The proposer signs the whole context message. Nodes verify the included nonce signatures and price signatures, recompute the price/timestamp, recompute the nonce set hash, and then recompute the session context ID.

Why it matters:

The context is no longer "trust my local pending pool looked like this." It is "here is the signed evidence; verify it yourself."

Code/tests touched:

- `src/oracle/musig2_messages.h`
- `src/protocol.cpp`
- `src/oracle/signing_orchestrator.cpp`
- `src/test/musig2_p2p_message_tests.cpp`

### 3. Signing waits for the selected context path

The issue:

A node that builds a context and immediately signs it can race ahead of peers that have not accepted the context yet. In a live network, that can produce split partial signatures even when the context is valid.

The fix:

RC38 broadcasts the local context proposal and returns from that tick. On a later tick, the normal context selection/validation path chooses the context and then selected local oracles sign it.

Why it matters:

Nodes get a chance to converge on the context before partial signatures hit the network.

Code touched:

- `src/oracle/signing_orchestrator.cpp`

### 4. The signing orchestrator is the live MuSig2 session owner

The issue:

There were two live ingestion paths for MuSig2 nonce and partial-signature P2P messages: the newer signing orchestrator and an older bundle-manager session path. The older path was epoch-only and did not understand the RC38 attempt/context evidence model.

The fix:

Live P2P relay now bypasses the old bundle-manager MuSig2 ingestion calls and feeds nonce/partial-signature messages into the signing orchestrator.

Why it matters:

One owner means one attempt-aware view of the epoch session. That reduces split-brain behavior inside the node.

Code touched:

- `src/net_processing.cpp`

### 5. Signed version heartbeats show who is running what

The issue:

When oracle consensus stalls, operators need to know whether peers are offline, isolated, or running old code. Before RC38, `getoracles` could show prices, but it could not reliably show each oracle's software/protocol version.

The fix:

RC38 adds `OracleVersionHeartbeatMsg` and the `oraclehb` P2P message. Each heartbeat is signed by the oracle key and reports:

- Oracle ID.
- Client version.
- P2P protocol version.
- Oracle protocol version.
- MuSig2 context version.
- Software version/subversion.
- Timestamp and nonce.

Heartbeats are stored in `OracleBundleManager`, relayed over P2P, included in `GETORACLES` catch-up responses, and displayed by RPC.

Why it matters:

Operators can now answer, "which oracle IDs are live and upgraded?" without guessing from price logs.

Code/tests touched:

- `src/protocol.{h,cpp}`
- `src/oracle/node.{h,cpp}`
- `src/oracle/bundle_manager.{h,cpp}`
- `src/net_processing.cpp`
- `src/rpc/digidollar.cpp`
- `src/test/digidollar_p2p_tests.cpp`
- `src/test/oracle_bundle_manager_tests.cpp`
- `src/test/fuzz/oracle_p2p_wire_messages.cpp`

### 6. Documentation was brought up to the current protocol

The issue:

Some operator docs were still describing old single-oracle or pre-RC38 behavior. That is dangerous because it sends testers looking in the wrong place.

The fix:

RC38 updates the root oracle bundle explainer and replaces the stale oracle testing guide with current V1 guidance.

Why it matters:

The docs now describe the actual system: 9-of-17, chain-seeded signer ranking, attempt-aware context proposals, signed evidence, heartbeats, and fail-closed price behavior.

Docs touched:

- `ORACLE_BUNDLE_EXPLAINER.md`
- `doc/DIGIDOLLAR_ORACLE_TESTING_GUIDE.md`

---

## Operator Upgrade Notes

### Everyone

1. Back up wallet and oracle key material.
2. Stop old RC37 or earlier nodes.
3. Keep using `testnet24`; do not wipe into a new testnet for RC38.
4. Start RC38 and confirm the client version reports RC38.
5. Reconnect to upgraded peers.

### Oracle Operators

1. Keep your assigned oracle slot unless Jared tells you otherwise.
2. Start your oracle with the assigned private key.
3. Confirm live price fetching works.
4. Confirm `getoracles` shows your heartbeat as fresh.
5. Confirm your `musig2_context_version` matches RC38.
6. Confirm nonce, context, and partial-signature messages appear in debug logs.
7. Confirm a v0x03 bundle appears in blocks once at least 9 upgraded oracles are online.

Useful commands:

```bash
digibyte-cli -testnet getblockchaininfo
digibyte-cli -testnet getnetworkinfo
digibyte-cli -testnet listoracle
digibyte-cli -testnet getoracles
digibyte-cli -testnet getoracleprice
```

Useful log filter:

```bash
tail -n 500 ~/.digibyte/testnet24/debug.log | rg -i 'oracle|heartbeat|musig|context|partial|bundle'
```

---

## Validation Status

Final RC38 validation completed on 2026-05-14 from `feature/digidollar-v1`.

| Gate | Status |
| --- | --- |
| Final build | PASS |
| Unit tests: `./src/test/test_digibyte --show_progress` | PASS |
| Functional tests: `test/functional/test_runner.py --jobs=4` | PASS |
| Fuzz all-target corpus replay | PASS |
| Multi-oracle testnet script: `./test_multi_oracle_testnet.sh` | PASS |

Validation logs:

- Build: `/tmp/rc38_final/build.log`
- Unit tests: `/tmp/rc38_final/unit_test_digibyte.log`
- Functional tests: `/tmp/rc38_final/functional_test_runner.log`
- Fuzz target list: `/tmp/rc38_final/fuzz_target_list.txt`
- Fuzz run: `/tmp/rc38_final/fuzz_test_runner.log`
- Multi-oracle script: `/tmp/rc38_final/test_multi_oracle_testnet_outer.log`

Fuzz mode used:

- Corpus replay mode with the public `qa-assets` corpus cloned to `/tmp/rc38_qa_assets/fuzz_corpora`.
- The local build is not a libFuzzer build, and `clang`/`clang++` are not installed on this machine, so the `--empty_min_time=30` libFuzzer path was not available here.
- Public `qa-assets` did not include seed directories for every DigiByte/DigiDollar target, so one minimal seed file was added locally for each missing registered target directory under `/tmp/rc38_qa_assets/fuzz_corpora`.
- Result: all 247 registered fuzz targets were exercised and passed.

Multi-oracle script result:

- 16 active oracle slots started across 8 local Qt nodes; slot 16 remained intentionally unrun.
- 16 fresh signed version heartbeats were visible before bundle testing continued.
- A live exchange-derived oracle price was reached through 9-of-17 consensus.
- A fresh on-chain v0x03 MuSig2 bundle was mined.
- Mint, redeem, transfer-chain, wallet restart, wallet backup/restore, reindex, rescan, and descriptor restore checks all passed.
- Final script result: 223 tests passed, 0 failed.

---

## Known Risks

- Mixed RC37/RC38 oracle operators may still gossip prices, but they should not be expected to reliably complete RC38 signing contexts. Threshold upgraded participation is required.
- Heartbeats are monitoring data. They help identify stale or old oracle nodes, but they do not make a price valid.
- If fewer than 9 valid oracle operators are online and fresh, the correct behavior is no new price bundle. That is fail-closed behavior.

---

## Bottom Line

RC38 does not change what validators see on-chain. It changes how oracle operators reach the off-chain MuSig2 transcript that produces that on-chain proof.

The goal is simple: enough upgraded oracles should be able to restart, reconnect, agree on the same evidence-backed context, sign it once, and get a valid price bundle mined without a coordinator and without fallback prices.
