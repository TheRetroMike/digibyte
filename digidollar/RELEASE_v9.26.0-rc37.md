# DigiByte Core v9.26.0-rc37 Release Notes

**WARNING: RC37 is testnet-only until Jared reviews, tags, releases, and deploys it.**

RC37 is a DigiDollar oracle convergence release on top of RC36. It keeps `testnet24`, keeps the 9-of-17 V1 quorum, and keeps the on-chain v0x03 bundle format. The change is how oracle nodes choose the 9 signers and agree on the exact MuSig2 signing transcript before partial signatures are sent.

The headline:

> RC37 uses a blockchain-seeded oracle ranking and a signed context proposal so honest oracle nodes converge on the same 9 signers, price, timestamp, nonce set, and session context before they sign.

Development branch: `feature/digidollar-v1`

Developer chat: https://app.gitter.im/#/room/#digidollar:gitter.im

---

## Read This First

RC37 does not reset the public DigiDollar testnet.

It stays on `testnet24`. It does not change activation heights, oracle quorum, epoch length, DigiDollar economics, wallet database format, or the v0x03 bundle committed in blocks.

What changed from RC36:

- Oracle signer ranking now uses `GetOracleEpochSelectionHash(epoch, oracle_id, epoch_selection_seed)`.
- The epoch selection seed comes from the local blockchain at the epoch boundary.
- Future epoch nonce collection can prestart, but context signing waits until the real seed block is known.
- A new signed `OracleMusigContextMsg` P2P message announces the exact signer set, price, timestamp, seed, and context ID.
- Context proposers are chosen by the same chain-seeded oracle ranking. If the first proposer is offline, later ranked proposers become eligible.
- Nodes reject context proposals that do not match the local chain seed, selected signers, nonce set, or selected-signer price calculation.
- Remote peers are not allowed to define the local epoch seed.
- Partial signatures still bind to the accepted `session_context_id`.
- Unit and fuzz coverage now checks seeded selection, arrival-order convergence, context message auth, and remote-seed rejection.
- `ORACLE_BUNDLE_EXPLAINER.md` was updated as the plain-English and technical blueprint for RC37 oracle bundles.

What did not change:

- No mainnet activation is included in RC37.
- No testnet reset is included in RC37.
- The V1 quorum remains 9-of-17.
- The on-chain v0x03 bundle format is unchanged.
- The oracle roster, activation heights, epoch length, and DigiDollar economic rules are unchanged.
- No production mock or fallback price path was added.
- Validators still verify the final bundle from the block.

---

## Testnet24 Network Details

These values are unchanged from RC36.

| Item | RC37 value |
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

## RC37 In Plain English

RC36 made partial signatures say which context they belonged to. That protected the signing round from mixing mismatched partials.

RC37 fixes the next problem: how the oracle network agrees on that context in the first place.

Every epoch now works like this:

1. The chain provides a seed from the block immediately before the epoch starts.
2. Every oracle that is online broadcasts a public MuSig2 nonce.
3. Every node scores nonce submitters with the same seed.
4. The 9 lowest-scored nonce submitters become the signer set.
5. The highest-priority eligible proposer broadcasts a signed context proposal.
6. Nodes verify the proposal locally.
7. Selected oracles sign only that context.
8. Any miner can include the finished v0x03 bundle.
9. Every full node verifies the final bundle from the block.

There is no central coordinator. The proposer does not get to choose an arbitrary bundle. The proposer only announces a transcript that everyone else can recompute and reject if it is wrong.

---

## Architecture Record

### 1. Oracle selection is now chain-seeded

The issue:

RC35/RC36 selection was deterministic, but it was still based on epoch scoring without an explicit blockchain seed in the signing session. That made the rotation too predictable and left room for nodes to diverge when they had different local nonce timing.

The fix:

RC37 adds an epoch selection seed to `GetOracleEpochSelectionHash()`. The seed is:

```text
seed_height = max(0, epoch_start_height - 1)
epoch_seed  = block_hash(seed_height)
```

The first epoch uses the genesis block hash. Later epochs use the block immediately before the epoch starts.

Why it matters:

The oracle order is deterministic for all honest nodes, but tied to chain data. No peer gets to secretly choose the committee.

Code/tests touched:

- `src/primitives/oracle.{h,cpp}`
- `src/oracle/musig2_session.{h,cpp}`
- `src/oracle/signing_orchestrator.{h,cpp}`
- `src/test/musig2_session_tests.cpp`

### 2. Future epoch prestart now waits for the real seed before signing

The issue:

Prestarting the next epoch is useful because nonce gossip needs time. But if signing context selection happens before the epoch seed exists, nodes can accidentally use a fallback seed.

The fix:

RC37 allows future epoch nonce collection to start early, but blocks context proposal and partial signing until the local chain has the real epoch seed.

Why it matters:

The network gets the liveness benefit of early nonce gossip without letting a placeholder seed choose the signing context.

Code/tests touched:

- `src/oracle/signing_orchestrator.cpp`
- `src/test/musig2_signing_orchestration_tests.cpp`

### 3. Context proposals are explicit P2P messages

The issue:

RC36 partial signatures were context-bound, but nodes still had to independently freeze a context from local state. Honest nodes could see nonces and prices in different orders and end up with different valid-looking local contexts.

The fix:

RC37 adds `OracleMusigContextMsg` and the `oramusigctx` P2P message. A context proposal includes:

- Epoch.
- Context version.
- Epoch selection seed.
- Proposer ID.
- Participant IDs.
- Consensus price.
- Consensus timestamp.
- Session context ID.
- Proposer auth signature.

Why it matters:

Nodes agree on the exact transcript before partial signatures are created. If the proposal does not match local data, the node refuses to sign.

Code/tests touched:

- `src/oracle/musig2_messages.h`
- `src/protocol.{h,cpp}`
- `src/net_processing.cpp`
- `src/oracle/signing_orchestrator.{h,cpp}`
- `src/test/musig2_p2p_message_tests.cpp`
- `src/test/musig2_p2p_handling_tests.cpp`
- `src/test/fuzz/oracle_musig2_auth_signature_domain.cpp`

### 4. Context proposers rotate by the same seeded ranking

The issue:

If one fixed node decides the context, the oracle system becomes centralized. If every node proposes at once, the network can split across competing contexts.

The fix:

RC37 uses the same seeded oracle order for proposal priority. The top eligible proposer gets the first chance. If it is offline or cannot build a valid context, later ranked proposers become eligible in later rounds.

Why it matters:

The system stays decentralized but still converges. The proposer announces a verifiable answer; it does not own the oracle price.

Code touched:

- `src/oracle/signing_orchestrator.cpp`

### 5. Remote context messages cannot define the local seed

The issue:

A remote oracle can sign a syntactically valid context message. That message must not be allowed to tell the local node what the epoch seed is.

The fix:

RC37 buffers early context proposals, but the local chain defines the seed. Once the seed is known locally, proposals with a different seed are rejected.

Why it matters:

This prevents a malicious or mistaken oracle from pushing peers onto a non-chain seed.

Code/tests touched:

- `src/oracle/signing_orchestrator.cpp`
- `src/test/musig2_signing_orchestration_tests.cpp`

### 6. Session context IDs now include the epoch selection seed

The issue:

The context ID must describe the whole off-chain signing transcript. Once seed-based selection exists, the seed must be part of that transcript.

The fix:

RC37 includes the epoch selection seed in the MuSig2 session context hash.

Why it matters:

A partial signature for one seeded selection round cannot be replayed into a different seeded selection round.

Code/tests touched:

- `src/oracle/musig2_session.cpp`
- `src/test/musig2_session_tests.cpp`

---

## Upgrade Instructions

### Everyone

1. Back up wallets and oracle key material.
2. Stop old RC36 or pre-RC37 nodes.
3. Keep using `testnet24`; do not go back to older testnets.
4. Start the RC37 node and confirm the version reports RC37.
5. Reconnect oracle nodes to RC37-compatible peers for live signing tests.

### Oracle Operators

1. Keep your assigned oracle slot unless Jared tells you otherwise.
2. Confirm the node is on `testnet24`.
3. Confirm live exchange prices are being fetched.
4. Confirm nonce messages are flowing.
5. Confirm context proposal messages are flowing.
6. Confirm partial signatures carry a non-null `session_context_id`.
7. Confirm MuSig2 sessions complete with at least 9 valid signers.
8. Confirm v0x03 oracle bundles are mined after activation.

Useful checks:

```bash
digibyte-cli -testnet getblockchaininfo
digibyte-cli -testnet getnetworkinfo
digibyte-cli -testnet listoraclekeys
digibyte-cli -testnet getoracleinfo
digibyte-cli -testnet getoracleprice
digibyte-cli -testnet getdigidollarstats
```

---

## Verification Results

These gates were run locally for this RC37 candidate on `feature/digidollar-v1`.
The final multi-oracle run was executed after the RC37 harness label cleanup, so the recorded ecosystem log matches the RC37 script text.

| Gate | Result | Log |
| --- | --- | --- |
| Build | PASS | `/tmp/rc37_final/build.log` |
| Unit tests | PASS | `/tmp/rc37_final/unit_test_digibyte.log` |
| Default functional tests | PASS, 370/370 | `/tmp/rc37_final/functional_test_runner.log` |
| Fuzz all-target gate | PASS, libFuzzer empty-corpus mode, 247/247 targets | `/tmp/rc37_final/fuzz_test_runner.log` |
| Multi-oracle testnet script | PASS, end-to-end | `/tmp/rc37_final/test_multi_oracle_testnet_outer.log` |

The multi-oracle script remains the most important ecosystem proof. In this run it:

- Started 16 active oracle slots across 8 Qt nodes, with slot 16 intentionally unrun.
- Fetched live exchange price data.
- Mined a fresh on-chain v0x03 MuSig2 oracle bundle at height 698.
- Confirmed live oracle prices before minting.
- Minted across every collateral tier.
- Rejected early and partial redemptions correctly.
- Redeemed unlocked tier 0 vaults.
- Ran DD transfer chains across Bob, Alice, and Charlie.
- Verified network DD supply and collateral invariants throughout the run.
- Verified 9-of-17 live oracle consensus and recovery.
- Verified an on-chain v0x03 bundle prefix and size again at height 1135.
- Passed wallet restart, backup/restore, reindex, rescan, and descriptor restore workflows.

Extended functional tests were not run for this RC37 gate because the requested release gate was the default functional suite.

Targeted checks added for RC37:

- Seeded committee selection changes with chain seed.
- Arrival order does not change the selected committee or context ID.
- Context ID changes when the epoch seed changes.
- Context proposal auth binds seed, participants, price, timestamp, and context ID.
- Remote context proposal cannot define the local epoch seed.
- Context proposal message type is registered and serialized.

---

## Downloads

Prebuilt binaries, when published, should use the RC37 version label:

- `digibyte-9.26.0-rc37-x86_64-linux-gnu.tar.gz`
- `digibyte-9.26.0-rc37-aarch64-linux-gnu.tar.gz`
- `digibyte-9.26.0-rc37-win64-setup.exe`
- `digibyte-9.26.0-rc37-osx.dmg`

Verify checksums and signatures from the official release location before running binaries.

---

## Review Notes

- RC37 is still testnet-only until Jared releases it.
- RC37 keeps `testnet24`; there is no new reset.
- The v0x03 on-chain bundle format is unchanged.
- The quorum remains 9-of-17.
- The changes are off-chain oracle signing, P2P context, and session-convergence hardening.
- Mixed RC36/RC37 oracle nodes should not be used for the final live oracle signing test because RC37 adds the context proposal message and seed-aware context rules.
- Production-style testing must use live oracle prices and MuSig2 multi-oracle bundles.
- Nothing was pushed externally by this local release work.

---

## Feedback

When reporting a problem, include the exact command, log path, node version, network, wallet state, current height, current epoch, and whether the node was on `testnet24`.

Best channels:

- Gitter: https://app.gitter.im/#/room/#digidollar:gitter.im
- GitHub issues or pull requests against the DigiByte Core repository
