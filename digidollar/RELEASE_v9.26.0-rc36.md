# DigiByte Core v9.26.0-rc36 Release Notes

**WARNING: RC36 is testnet-only until Jared reviews, tags, releases, and deploys it.**

RC36 is a DigiDollar oracle reliability release on top of RC35. It keeps the RC34/RC35 `testnet24` chain and focuses on the last thing that was still too fragile: live multi-oracle MuSig2 signing under real timing.

The headline:

> RC36 binds every MuSig2 partial signature to the exact signing context, so oracle nodes only aggregate partial signatures for the same price, timestamp, signer set, nonce set, epoch, and chain.

That fixes the liveness failure seen in live debug logs where prices and nonces were present, but only a few partial signatures were accepted because nodes had signed different local contexts.

Development branch: `feature/digidollar-v1`

Developer chat: https://app.gitter.im/#/room/#digidollar:gitter.im

---

## Read This First

RC36 does not reset the public DigiDollar testnet.

It stays on `testnet24`, keeps the v0x03 oracle bundle format, and keeps the V1 9-of-17 oracle model. The change is in the off-chain MuSig2 signing messages and session handling.

What changed from RC35:

- MuSig2 partial signature P2P messages now carry a session context version and session context ID.
- The partial-signature authentication hash commits to that context.
- Pending partial signatures are buffered by epoch and context, not just epoch.
- Pending partial-signature buffers are capped per context and per epoch, so random context IDs cannot create unbounded memory growth.
- Nodes reject partial signatures that do not match the local session context.
- The price/timestamp signed by MuSig2 is computed from the selected signer IDs for that context.
- Completed sessions no longer guess missing signed values from the live price pool after aggregation.
- A new root `ORACLE_BUNDLE_EXPLAINER.md` explains the full oracle bundle design for technical and non-technical readers.
- RC36 also includes the existing local RC36 release bump and oracle endpoint/name cleanup commits already on this branch.

What did not change:

- No mainnet deployment is included in RC36.
- No testnet reset is included in RC36.
- The V1 quorum remains 9-of-17.
- The on-chain v0x03 bundle format is unchanged.
- The oracle roster, activation heights, epoch length, and DigiDollar economic rules are unchanged.
- No production mock or fallback price path was added.
- Validators still verify the bundle from the block.

---

## Testnet24 Network Details

These values are unchanged from RC35.

| Item | RC36 value |
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

Code check:

- `src/chainparamsbase.cpp` still selects `testnet24`.
- `src/kernel/chainparams.cpp` still contains the RC34 `testnet24` genesis, magic bytes, ports, activation height, oracle epoch, and quorum values.
- RC36 does not change those chain parameters.

---

## RC36 In Plain English

RC35 fixed which oracles get selected. RC36 fixes what exactly they are signing together.

MuSig2 is strict. A partial signature only works if everyone signs the exact same message with the exact same signer set and nonce set. If one node signs price A and another signs price B, or one node uses a slightly different nonce set, those partial signatures cannot be combined.

That is good for safety. It means bad signatures do not sneak through.

But it was bad for liveness because the P2P partial signature message did not say enough about the signing context. Nodes could honestly reach different local contexts inside the same epoch and then reject each other's partial signatures. The live logs showed the pattern: prices were available, nonces were available, signing started, but only a few partial signatures were accepted and no valid bundle arrived.

RC36 fixes that by making the context explicit.

Every partial signature now says, in effect:

```text
I am oracle X.
I am signing epoch E.
I am signing context C.
Context C means this chain, this price/timestamp message,
this signer bitmap, and this public nonce set.
```

If the context does not match, the node ignores that partial signature. If the context matches, the partial can be verified and aggregated.

This keeps the system decentralized. No coordinator chooses the bundle. Every node can compute the same context from public data, and every validator still checks the final on-chain v0x03 bundle.

---

## Architecture Record

### 1. Partial signatures are now bound to a MuSig2 session context

The issue:

Live oracle debug logs showed the oracle network had prices and nonces, but the signing round stalled with too few valid partial signatures. That pointed to a deeper MuSig2 transcript problem, not a missing-price problem.

Before RC36:

- `OracleMusigPartialSigMsg` carried epoch, oracle ID, partial signature, and auth signature.
- It did not identify the exact price/timestamp message.
- It did not identify the exact signer bitmap.
- It did not identify the exact public nonce set.
- Pending partial signatures were keyed by epoch, so different contexts inside the same epoch could collide.

After RC36:

- `OracleMusigPartialSigMsg` includes `context_version`.
- `OracleMusigPartialSigMsg` includes `session_context_id`.
- The message hash and auth signature hash include both fields.
- Net processing rejects partial signatures without a valid context.
- The signing orchestrator only applies partial signatures that match the local session context.
- Pending partial signatures are buffered by `epoch -> session_context_id -> oracle_id`.
- The pending buffer has both per-context and per-epoch caps, so a malicious authenticated oracle cannot bypass the cap by grinding random session context IDs.

Why it matters:

MuSig2 partial signatures are only valid for one exact transcript. RC36 makes that transcript visible in the P2P message so honest nodes do not mix different signing rounds.

Code/tests touched:

- `src/oracle/musig2_messages.h`
- `src/protocol.cpp`
- `src/net_processing.cpp`
- `src/oracle/musig2_session.cpp`
- `src/oracle/musig2_session.h`
- `src/oracle/signing_orchestrator.cpp`
- `src/oracle/signing_orchestrator.h`
- `src/test/musig2_p2p_message_tests.cpp`
- `src/test/musig2_signing_orchestration_tests.cpp`

### 2. The signing context commits to the nonce set and signer bitmap

The issue:

The final on-chain bundle has a bitmap, and validators reconstruct the aggregate public key from that bitmap. The off-chain signing round must use the same signer set and nonce set or aggregation will fail.

The fix:

RC36 computes a session context ID from:

- Domain string.
- Chain genesis hash.
- Epoch.
- Context version.
- Message hash for the signed price/timestamp.
- Raw signer bitmap.
- Hash of the serialized public nonce set.

Why it matters:

This prevents a partial signature for one signer set or nonce set from being accepted into a different signer set or nonce set.

Code/tests touched:

- `src/oracle/musig2_session.cpp`
- `src/oracle/musig2_session.h`
- `src/test/musig2_p2p_message_tests.cpp`

### 3. The signed price is computed from the selected signer set

The issue:

The old signing path used the general pending oracle pool when computing consensus values. That pool can contain non-signer oracle prices. In a live network, non-signer updates can arrive while the signer set is being frozen.

The fix:

RC36 adds `ComputeConsensusValuesForOracles()`.

The signing orchestrator asks for consensus values from the selected signer IDs only. If any selected signer lacks a fresh price, the node waits instead of signing a context that another node may not reproduce.

Why it matters:

The price/timestamp message must be stable for the exact MuSig2 context. Non-signer price messages should not make one node sign a different message from another node in the same signing round.

Code/tests touched:

- `src/oracle/bundle_manager.cpp`
- `src/oracle/bundle_manager.h`
- `src/oracle/signing_orchestrator.cpp`
- `src/test/oracle_bundle_manager_tests.cpp`

### 4. Completed sessions no longer recover missing signed values from live state

The issue:

There was a fallback path that tried to fill missing signed price/timestamp values from the current live consensus pool after aggregation.

That was not the right rule for a hardened signing context. The bundle should carry the price and timestamp that were actually signed.

The fix:

RC36 removes that recovery path. A completed session is usable only if it has a non-empty aggregate signature and recorded signed price/timestamp values.

Why it matters:

The miner must not accidentally build a bundle with values that were not the values signed by the MuSig2 context.

Code touched:

- `src/oracle/signing_orchestrator.cpp`

### 5. Oracle endpoint metadata was cleaned up

The issue:

Two oracle endpoint/name metadata issues were already fixed in the RC36 branch before the context-binding work:

- Oracle endpoint ports needed to match the intended testnet layout.
- Missing operator names made oracle status output harder to read.

The fix:

The existing RC36 commit updates chainparams and RPC metadata so operator-facing oracle information is clearer and the configured endpoints match the RC36 testnet setup.

Why it matters:

Operators need to see the right oracle identity and endpoint information while debugging live testnet behavior.

Code touched:

- `src/kernel/chainparams.cpp`
- `src/rpc/digidollar.cpp`

### 6. Release metadata moved to RC36

The issue:

The node needed to identify itself as RC36 for this proving round.

The fix:

The existing RC36 release bump updates the release candidate number and wallet splash image.

Why it matters:

Operators and testers need to know which build they are running.

Code/assets touched:

- `configure.ac`
- `src/qt/res/icons/digibyte_wallet.png`

### 7. Oracle bundle documentation is now a build blueprint

The issue:

The oracle system is security-critical and hard to explain from scattered source files alone.

The fix:

RC36 adds `ORACLE_BUNDLE_EXPLAINER.md` at the repository root.

It explains:

- What an oracle bundle is.
- How epochs work.
- How the 9 signers are selected.
- How MuSig2 nonce and partial-signature rounds work.
- What the v0x03 bundle contains.
- What validators check.
- How the system fails closed and recovers.
- What RC36 hardened.
- How operators and reviewers should test it.

Why it matters:

This is the blueprint for humans and AI agents reviewing the oracle path. It records the design in plain language without pretending the future block-hash selection upgrade already exists.

Docs touched:

- `ORACLE_BUNDLE_EXPLAINER.md`

---

## How The 9 Oracles Are Selected

No central node chooses the 9 signers.

For each epoch, every node looks at the oracle IDs that submitted valid nonces. It scores each ID with the same deterministic function:

```text
score = GetOracleEpochSelectionHash(epoch, oracle_id)
```

Then it sorts by score and takes the threshold set.

For the current V1 testnet/mainnet shape, threshold means 9 signers.

This means:

- The signer set rotates by epoch.
- Higher oracle IDs can be selected.
- The system no longer waits forever for fixed IDs `0..8`.
- Every honest node can compute the same result without a coordinator.

Important precision:

This is deterministic epoch-scored rotation. It is not the future block-hash seeded selection design. A block-hash seeded design may be a good future hardening, but it must be implemented and validated as a consensus-aware change before release notes claim it.

---

## What Happens If Oracles Go Offline

RC36 keeps the fail-closed recovery model.

If fewer than threshold fresh oracles are available:

- A new valid aggregate bundle cannot be produced.
- Price-dependent DigiDollar behavior pauses.
- The node does not fake a price.
- Existing chain state remains valid.

When enough oracles come back:

- They fetch live prices.
- They broadcast fresh price messages.
- They broadcast fresh nonces for the current epoch.
- Nodes select the threshold set from nonce submitters.
- The MuSig2 context is built.
- Context-bound partial signatures aggregate.
- New v0x03 bundles can be mined again.

This is the right behavior for outages, startup delays, and oracle restarts. The system waits for real quorum instead of guessing.

---

## Validation Evidence

Final local RC36 validation:

- Build passed: `make -j"$(nproc)"`, log `/tmp/rc36_final_build_rerun.log`
- Full unit suite passed: `./src/test/test_digibyte --show_progress`, log `/tmp/rc36_final_unit_test_digibyte_rerun.log`
- Default functional suite passed: `test/functional/test_runner.py --jobs=4`, log `/tmp/rc36_final_functional_test_runner_rerun.log`
- Fuzz all-target gate passed from a clean temporary Clang 20/libFuzzer worktree: `test/fuzz/test_runner.py -l INFO --par=4 --empty_min_time=30 /tmp/rc36_fuzz_corpus`, log `/tmp/rc36_final_fuzz_test_runner.log`
- First live multi-oracle ecosystem run passed: `./test_multi_oracle_testnet.sh`, log `/tmp/rc36_test_multi_oracle_testnet_first.log`
- Final live multi-oracle ecosystem rerun passed after the RC36 commits were made: `./test_multi_oracle_testnet.sh`, log `/tmp/rc36_final_test_multi_oracle_testnet.log`

Targeted regression checks also passed:

- MuSig2 P2P message hashing/auth context binding: `/tmp/rc36_target_musig2_p2p_message.log`
- Oracle bundle manager selected-signer price consensus: `/tmp/rc36_target_oracle_bundle_manager.log`
- Signing orchestration context handling: `/tmp/rc36_target_musig2_signing_orchestration_after_cap.log`
- Pending partial-signature memory bounds: `/tmp/rc36_target_rh58_pending_partialsigs.log`
- Completed-session signed-value requirement: `/tmp/rc36_target_musig2_bundle_mining.log`
- Functional MuSig2 P2P DoS wire-format regression: `/tmp/rc36_target_digidollar_wave21_musig2_p2p_dos.log`

The default functional suite caught one stale harness expectation after RC36 changed the partial-signature wire format. The test was still sending the old contextless 103-byte payload. The harness now builds and signs the RC36 context-bound payload, while keeping the same stale-epoch relay checks.

The final live multi-oracle rerun is the main ecosystem proof. It passed with live exchange prices, 16 active local oracles, 9-of-17 consensus, an on-chain v0x03 MuSig2 bundle, mint/redeem/transfer coverage, wallet restart, backup/restore, reindex, rescan, and descriptor export/reimport recovery. The script reported 222 total checks, 0 failed, and `ALL TESTS PASSED`.

---

## Upgrade Instructions

### Everyone

1. Back up wallets and oracle key material.
2. Stop old RC35 or pre-RC36 nodes.
3. Keep using `testnet24`; do not go back to older testnets.
4. Start the RC36 node and confirm the version reports RC36.
5. Reconnect only to RC36-compatible peers for live oracle signing tests.

### Oracle Operators

1. Keep your assigned oracle slot unless Jared tells you otherwise.
2. Confirm the node is on `testnet24`.
3. Confirm live exchange prices are being fetched.
4. Confirm nonce messages are flowing.
5. Confirm partial signature messages carry a non-null `session_context_id`.
6. Confirm MuSig2 sessions complete with at least 9 valid signers.
7. Confirm v0x03 oracle bundles are mined after activation.

Useful checks:

```bash
digibyte-cli -testnet getblockchaininfo
digibyte-cli -testnet getnetworkinfo
digibyte-cli -testnet listoraclekeys
digibyte-cli -testnet getoracleinfo
digibyte-cli -testnet getoracleprice
digibyte-cli -testnet getdigidollarstats
```

### Wallet Testers

Test the user path that matters:

1. Create or load a wallet.
2. Mine or receive testnet DGB collateral.
3. Mint DigiDollar after activation and after a valid oracle price exists.
4. Transfer DigiDollar.
5. Restart and confirm balances and positions persist.
6. Rescan or reindex and confirm positions return.
7. Redeem unlocked DigiDollar and confirm collateral behavior.
8. Confirm the wallet says oracle unavailable when there is no valid price.

---

## Example Configuration

Minimal RC36 testnet configuration:

```ini
testnet=1
server=1
txindex=1
digidollar=1
fallbackfee=0.0001
```

Oracle operators need their assigned oracle configuration. Do not use mock or fallback price paths for production-style testing.

---

## Downloads

Prebuilt binaries, when published, should use the RC36 version label:

- `digibyte-9.26.0-rc36-x86_64-linux-gnu.tar.gz`
- `digibyte-9.26.0-rc36-aarch64-linux-gnu.tar.gz`
- `digibyte-9.26.0-rc36-win64-setup.exe`
- `digibyte-9.26.0-rc36-osx.dmg`

Verify checksums and signatures from the official release location before running binaries.

---

## Commits Since RC35

At the time these notes were written, the RC36 branch contains:

- `fix: correct oracle endpoint ports and missing operator names`
- `release: bump version to v9.26.0-rc36 and update wallet splash image`
- `oracle: bind MuSig2 partials to session context`
- `docs: add RC36 oracle bundle explainer`

Jared should review the final local commit history before tagging.

---

## Review Notes

- RC36 is still testnet-only until Jared releases it.
- RC36 keeps `testnet24`; there is no new reset.
- The v0x03 on-chain bundle format is unchanged.
- The quorum remains 9-of-17.
- The context-binding change is off-chain P2P/session hardening.
- Mixed RC35/RC36 oracle nodes should not be used for the final live oracle signing test because RC36 partial signature messages carry new context fields.
- Production-style testing must use live oracle prices and MuSig2 multi-oracle bundles.
- Nothing was pushed externally by this local release work.

---

## Feedback

When reporting a problem, include the exact command, log path, node version, network, wallet state, and whether the node was on `testnet24`.

Best channels:

- Gitter: https://app.gitter.im/#/room/#digidollar:gitter.im
- GitHub issues or pull requests against the DigiByte Core repository
