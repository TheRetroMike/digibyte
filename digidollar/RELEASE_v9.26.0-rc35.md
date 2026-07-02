# DigiByte Core v9.26.0-rc35 Release Notes

**WARNING: RC35 is testnet-only. Do not use it on mainnet.**

RC35 is a focused DigiDollar V1 release candidate on top of RC34. It does not reset the public testnet again. It keeps the RC34 `testnet24` network and fixes two things that matter before mainnet proving:

- The MuSig2 oracle committee must not get stuck waiting for fixed low-numbered oracle IDs.
- Wallet, RPC, and Qt paths must tell the truth when oracle data is missing, wallets are fragmented, or rapid mint/redeem flows are used.

Development branch: `feature/digidollar-v1`

Developer chat: https://app.gitter.im/#/room/#digidollar:gitter.im

---

## Read This First

RC35 stays on the RC34 DigiDollar testnet: `testnet24`.

There is no RC35 testnet reset. RC34 already moved DigiDollar proving from `testnet23` to `testnet24`. RC35 keeps that chain so the oracle and wallet fixes can be tested on the same public proving network.

What changed from RC34:

- Oracle signing committee selection now rotates correctly and uses nonce-submitting oracle IDs instead of the first sequential IDs.
- Exact-size oracle rosters are still epoch-sorted, so threshold callers do not quietly fall back to chainparams order.
- Regtest mock oracle prices are only used when the mock oracle is explicitly enabled.
- "No oracle price" is reported as oracle unavailable, not as emergency redemption state.
- Fragmented-wallet auto-consolidation is safer and proves mempool acceptance before the wallet records success.
- New regression tests cover the RC33 wallet bugs: fragmented large mints, 22 rapid mints, rapid redemptions, no-oracle reporting, and empty DigiDollar UI tooltips.

What did not change:

- No mainnet deployment is included in RC35.
- No new testnet genesis is included in RC35.
- The V1 oracle model remains 9-of-17 MuSig2.
- The v0x03 bundle format is unchanged.
- The oracle roster, quorum, bitmap format, epoch length, activation height, and chainparams are unchanged from RC34.
- No production mock price path was added.
- Jared still reviews, tags, releases, and deploys.

---

## Testnet24 Network Details

These values are unchanged from RC34.

| Item | RC35 value |
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
- `src/kernel/chainparams.cpp` still contains the RC34 `testnet24` genesis, magic bytes, port, activation height, oracle epoch, and quorum values.
- RC35 does not change those chain parameters.

---

## RC35 In Plain English

RC34 made DigiDollar strict. RC35 makes a few parts less brittle.

The biggest bug was in oracle signing. The system could have 9 valid online oracles, but still fail to mint because the MuSig2 session was waiting for the first sequential IDs, such as 0 through 8. If IDs 6, 7, and 8 were offline, valid online oracles like 10, 14, and 15 did not count. That is not a quorum failure. That is bad committee selection.

RC35 fixes that by scoring every oracle that submitted a nonce for the epoch and taking the best 9 by deterministic epoch hash. Any 9 valid online oracles can complete the session. The selected committee rotates by epoch instead of being permanently tied to the lowest oracle IDs.

RC35 also fixes the wallet and status paths that were hiding truth:

- If no oracle price exists, the system says that clearly.
- Minting still fails closed when price data is unavailable.
- ERR is not shown as active just because the oracle is missing.
- Regtest mock oracle prices cannot silently stand in for real oracle data unless the test explicitly enables the mock oracle.
- Fragmented wallets no longer record a consolidation as successful before the mempool accepts it.

---

## Architecture Record

### Oracle committee selection now follows the V1 rotation design

RC35 changes MuSig2 signing session participant selection from fixed sequential IDs to epoch-hash selection over collected nonces.

Before RC35:

- `GetRequiredParticipants()` chose the first `m_min_signers` sequential IDs.
- With threshold 9, that usually meant IDs `0,1,2,3,4,5,6,7,8`.
- If enough non-sequential oracles were online, the session could still wait forever because those online IDs were not in the fixed required set.
- `SelectOraclesForEpoch()` also returned exact-size rosters in chainparams order, which meant a 17-of-17 testnet roster did not get epoch-shuffled before threshold callers took a subset.

After RC35:

- `GetOracleEpochSelectionHash(epoch, oracle_id)` provides one shared deterministic scoring function.
- `SelectOraclesForEpoch()` applies epoch-hash sorting for every roster size, including exactly 17 active oracles.
- `MuSig2SigningSession::GetRequiredParticipants()` scores the oracle IDs that actually submitted nonces and takes the first threshold set.
- Nonce arrival order does not change the selected committee.
- Offline low IDs no longer block a valid 9-oracle signing set when 9 other valid nonce submitters are present.

Why it matters:

The oracle system is supposed to be 9-of-17, not "IDs 0 through 8 or nothing." RC35 restores that liveness property while keeping the existing deterministic rules.

What did not change:

- The quorum is still 9-of-17.
- The roster is unchanged.
- The bitmap still records the actual signer IDs.
- The v0x03 bundle format is unchanged.
- Aggregate signature validation is unchanged.
- Chainparams and activation heights are unchanged.

### Mock oracle prices are test tools, not hidden fallback truth

`MockOracleManager` is a regtest-only test helper. It lets unit and functional tests create a price without running live oracle nodes.

Before RC35, some regtest paths checked only "are we on regtest?" before reading the mock price. That meant a disabled mock oracle could still leak a stored/default price into a path that was supposed to be proving "no oracle price available."

After RC35:

- Regtest mock oracle fallbacks require both regtest and `MockOracleManager::IsEnabled()`.
- Disabled mock oracle now means no mock price.
- Production testnet and mainnet do not use this path.
- No production fallback price was added.

Why it matters:

Tests should not accidentally pass because a fake price was still sitting in memory. If the oracle is unavailable, the code should see that and fail closed.

### No-oracle and ERR are separate states

ERR means the DigiDollar system is undercollateralized and redemptions require more DD burn. Missing oracle data means the node cannot evaluate the system health.

Those are not the same problem.

RC35 separates them:

- Mint validation still rejects mints with no oracle price.
- The reject reason is now the clear fail-closed oracle reason instead of being masked as ERR.
- `getdigidollarstats` now reports `oracle_available`, `oracle_status`, and `minting_restricted_reason`.
- `getprotectionstatus` now includes an `oracle` section and an ERR `evaluation_status`.
- If no oracle price exists, ERR is reported as not evaluated rather than active.

Why it matters:

Operators need to know whether they have an oracle outage or a real emergency redemption condition. Treating both as the same thing makes debugging and incident response worse.

### Fragmented-wallet minting now proves the consolidation transaction first

Large fragmented wallets can have hundreds of small DGB UTXOs. A DigiDollar mint may need those UTXOs consolidated before the mint can be built cleanly.

Before RC35:

- RPC and Qt auto-consolidation could commit a consolidation transaction to the wallet before proving the mempool accepted it.
- That could make the wallet look like it had successfully prepared the mint path when the network had not accepted the consolidation transaction.
- UTXOs were selected in wallet/list order, which was not ideal for reducing fragmentation quickly.

After RC35:

- Auto-consolidation fails clearly if wallet transaction broadcasting is disabled.
- Consolidation inputs are sorted largest-first.
- The consolidation transaction is broadcast and accepted by the mempool before the wallet records it as committed.
- UTXOs are refreshed after consolidation before retrying the mint.
- Qt mirrors the RPC consolidation behavior.

Why it matters:

A wallet should not tell the user a mint path succeeded when the prerequisite consolidation transaction was never accepted. RC35 makes the wallet prove the network step before reporting success.

### Legacy wallet transfer/redeem paths no longer guess a price

Some legacy wallet helper paths still had a hardcoded `6500` micro-USD fallback price when no oracle price was available.

RC35 removes that fallback:

- Transfer builders use the real oracle cache first.
- Regtest may use the mock oracle only when it is explicitly enabled.
- If no price is available, transfer/redeem construction fails instead of guessing.

Why it matters:

No DigiDollar path should quietly invent a price. If price data is required and unavailable, failing closed is the correct behavior.

### Qt now explains empty or unavailable states better

RC35 cleans up small but visible Qt status issues:

- Empty transaction notes show `No note` as the tooltip.
- Empty receive dates show `No date`.
- Empty receive addresses show `No address available`.
- Mint UI says `Oracle unavailable` and `Oracle price unavailable - minting is paused` instead of letting the state look like a generic balance problem.
- Overview UI shows `Oracle Unavailable`, DCA `Paused`, and ERR `Not Evaluated` when there is no price.

Why it matters:

The UI should not make users guess. If minting is paused because the oracle is unavailable, the wallet should say that.

---

## Issues Fixed And Why They Matter

### 1. Oracle committee could get stuck on low IDs

The issue:

The MuSig2 signing session required the first sequential oracle IDs instead of the best threshold set from actual nonce submitters. With low IDs offline and enough higher IDs online, the oracle network could have quorum but still never complete a signing session.

The fix:

RC35 scores collected nonce submitters by `hash(epoch, oracle_id)` and selects the threshold committee from that scored set.

Details:

- Added `GetOracleEpochSelectionHash()` in `src/primitives/oracle.cpp`.
- `SelectOraclesForEpoch()` now epoch-sorts active oracles even when the active roster size is less than or equal to `ORACLE_ACTIVE_COUNT`.
- `MuSig2SigningSession::GetRequiredParticipants()` now selects from `m_pubnonces`, not from hardcoded sequential IDs.
- Tests prove selection is not `[0..8]` by default, rotates across epochs, completes when low IDs are offline, and is independent of nonce arrival order.

Why it matters:

This was a real liveness bug. The oracle system could be alive and still not mint because the wrong IDs were being waited on.

Code/tests touched:

- `src/oracle/musig2_session.cpp`
- `src/oracle/musig2_session.h`
- `src/primitives/oracle.cpp`
- `src/primitives/oracle.h`
- `src/test/digidollar_oracle_tests.cpp`
- `src/test/musig2_session_tests.cpp`
- `src/test/musig2_p2p_network_attacks_tests.cpp`
- `src/test/rh57_musig2_trim_aggregate_toctou_tests.cpp`

### 2. Exact-size rosters were not shuffled

The issue:

When the active oracle count was exactly the configured active count, the selection function returned the roster in chainparams order. That quietly disabled epoch rotation for the current 17-oracle testnet roster.

The fix:

RC35 removes that early return. All active rosters are sorted by epoch score. If there are 17 active oracles, all 17 are still returned, but in epoch-scored order.

Why it matters:

The current testnet roster is exactly the size where the old early return mattered. Without this fix, threshold callers could keep selecting a fixed low-ID committee.

Code/tests touched:

- `src/primitives/oracle.cpp`
- `src/test/digidollar_oracle_tests.cpp`

### 3. Missing oracle price was being confused with ERR

The issue:

When no oracle price was available, some paths reported or behaved as if emergency redemption was active. That hid the actual problem: the node did not have a valid price.

The fix:

RC35 keeps minting fail-closed when the oracle is missing, but reports the reason as oracle unavailable. ERR is only evaluated when a price exists.

Details:

- Mint validation no longer runs the ERR precheck when the oracle price is zero.
- The mint still fails later with `bad-oracle-price`.
- `getdigidollarstats` exposes `oracle_available`, `oracle_status`, and `minting_restricted_reason`.
- `getprotectionstatus` exposes an oracle status object and `err.evaluation_status`.
- Functional tests disable the mock oracle and assert that ERR is not falsely active.

Why it matters:

An oracle outage and a real emergency are different operational events. RC35 makes the node say which one is happening.

Code/tests touched:

- `src/digidollar/validation.cpp`
- `src/rpc/digidollar.cpp`
- `src/test/digidollar_skip_oracle_tests.cpp`
- `src/test/digidollar_rpc_tests.cpp`
- `test/functional/digidollar_rpc_protection.py`

### 4. Regtest mock prices could hide no-oracle bugs

The issue:

Some regtest code used `MockOracleManager::GetCurrentPrice()` whenever the chain was regtest. It did not also check whether the mock oracle was enabled.

The fix:

RC35 requires `MockOracleManager::IsEnabled()` before using the mock price.

Details:

- `OracleIntegration::GetCurrentOraclePrice()`
- `OracleIntegration::GetCurrentOraclePriceMicroUSD()`
- `OracleIntegration::GetOraclePriceForHeight()`
- ERR fallback price lookup
- DigiDollar RPC price lookup paths
- Wallet transfer/redeem fallback paths

Why it matters:

Tests that disable the mock oracle should actually test no-oracle behavior. Disabled fake data should not keep the system running.

Code/tests touched:

- `src/oracle/bundle_manager.cpp`
- `src/consensus/err.cpp`
- `src/rpc/digidollar.cpp`
- `src/wallet/digidollarwallet.cpp`
- `test/functional/digidollar_rpc_protection.py`

### 5. Fragmented wallet auto-consolidation could report success too early

The issue:

When minting from a wallet with many small UTXOs, auto-consolidation could commit the consolidation transaction locally before proving the transaction was accepted by the mempool.

The fix:

RC35 broadcasts consolidation transactions first and commits them only after mempool acceptance.

Details:

- RPC mint auto-consolidation now checks wallet broadcasting is enabled.
- Qt mint auto-consolidation uses the node broadcast path before wallet commit.
- Inputs are sorted largest-first before consolidation.
- UTXOs are refreshed and resorted after each consolidation pass.
- A new functional regression test creates 450 small UTXOs, mints, mines, restarts, and confirms both the consolidation and mint survived.

Why it matters:

The wallet should not create a local story that the network rejects. RC35 makes consolidation success mean actual mempool acceptance.

Code/tests touched:

- `src/rpc/digidollar.cpp`
- `src/qt/walletmodel.cpp`
- `test/functional/wallet_digidollar_rc33_regressions.py`

### 6. Rapid mint and redeem regressions are now covered

The issue:

RC33 bug reports included rapid mint and redeem flows that could leave wallet state wrong after mining or restart.

The fix:

RC35 adds functional coverage that exercises those flows against a real node.

Details:

- One test performs 22 rapid mints, mines them, restarts, and verifies every transaction and position is confirmed and active.
- One test mints 8 positions, waits through unlock height, rapidly redeems them, mines, restarts, and verifies redeemed positions are no longer active.
- The test is registered in the default functional runner.

Why it matters:

These are not just API calls. They prove wallet persistence, transaction tracking, position state, mempool behavior, and restart recovery together.

Code/tests touched:

- `test/functional/wallet_digidollar_rc33_regressions.py`
- `test/functional/test_runner.py`

### 7. Qt empty-field and oracle-unavailable display bugs were cleaned up

The issue:

Some DigiDollar Qt rows had empty tooltips or unclear status text. The mint and overview screens also did not explain no-oracle state clearly enough.

The fix:

RC35 gives empty fields readable fallback text and makes no-oracle status explicit in mint and overview widgets.

Why it matters:

Users should not have to inspect debug logs to learn that the oracle is unavailable or that a note/date/address is empty.

Code/tests touched:

- `src/qt/digidollartransactionswidget.cpp`
- `src/qt/digidollarreceivewidget.cpp`
- `src/qt/digidollarmintwidget.cpp`
- `src/qt/digidollaroverviewwidget.cpp`
- `src/qt/test/digidollarwidgettests.cpp`

### 8. Release metadata moved to RC35

The issue:

The build needed to identify itself as RC35 after the oracle and wallet fixes landed.

The fix:

RC35 updates the release candidate number in `configure.ac`.

Details:

- `_CLIENT_VERSION_RC` changed from `34` to `35`.
- `src/qt/res/icons/digibyte_wallet.png` was refreshed in the same release bump commit as a Qt wallet asset update.

Why it matters:

Operators and testers need the node to report the correct RC label while proving the RC35 code.

Code/assets touched:

- `configure.ac`
- `src/qt/res/icons/digibyte_wallet.png`

---

## Validation Evidence

Recorded RC35 validation from the commit stack:

- Build passed: `make -j"$(nproc)"`
- Unit tests passed: `./src/test/test_digibyte --show_progress`
- Default functional tests passed: `test/functional/test_runner.py --jobs=4`
- Targeted DigiDollar unit tests passed:
  - `./src/test/test_digibyte --run_test=digidollar_skip_oracle_tests --catch_system_errors=no`
  - `./src/test/test_digibyte --run_test=digidollar_rpc_tests --catch_system_errors=no`
  - `./src/test/test_digibyte --run_test=digidollar_txbuilder_tests --catch_system_errors=no`
- Targeted functional tests passed:
  - `test/functional/test_runner.py wallet_digidollar_rc33_regressions.py`
  - `test/functional/test_runner.py digidollar_rpc_protection.py`
  - `test/functional/test_runner.py digidollar_oracle_rpc_staleness.py`
  - `test/functional/test_runner.py digidollar_mempool_miner_parity.py`
- Qt widget test passed:
  - `QT_QPA_PLATFORM=offscreen ./src/qt/test/test_digibyte-qt --run_test=DigiDollarWidgetTests/transactionsWidgetShowsRpcHistorySignsAndFields`
- The oracle committee fix commit records a live multi-oracle run:
  - `KEEP_QT_OPEN=1 ./test_multi_oracle_testnet.sh`

Important release note:

The final RC35 tag candidate should still rerun the full final release gates after this release-notes commit, including the live multi-oracle script last. The release notes are documentation only, but the project rule is that the final ecosystem proof comes after all changes are in place.

---

## Upgrade Instructions

### Everyone

1. Back up wallets and oracle key material.
2. Stop old RC34 or pre-RC35 nodes.
3. Keep using `testnet24`; do not go back to `testnet23`.
4. Start the RC35 node and confirm the version reports RC35.
5. Reconnect only to RC35-compatible peers when testing DigiDollar V1.

### Oracle Operators

1. Keep your assigned oracle slot unless Jared tells you otherwise.
2. Confirm the node is on `testnet24`.
3. Confirm live exchange prices are being fetched.
4. Confirm nonce and partial-signature messages are flowing.
5. Confirm MuSig2 sessions complete even when the online set is not simply IDs `0..8`.
6. Confirm v0x03 oracle bundles are mined after activation.

Useful checks:

```bash
digibyte-cli -testnet getblockchaininfo
digibyte-cli -testnet getnetworkinfo
digibyte-cli -testnet listoraclekeys
digibyte-cli -testnet getoracleinfo
digibyte-cli -testnet getdigidollarinfo
```

### Wallet Testers

Test the user path that matters:

1. Create or load a wallet.
2. Mine or receive testnet DGB collateral.
3. Mint DigiDollar after activation.
4. Try a fragmented-wallet mint if you have many small UTXOs.
5. Transfer DigiDollar.
6. Restart and confirm balances and positions persist.
7. Rescan or reindex and confirm positions return.
8. Redeem DigiDollar and confirm collateral behavior.
9. Confirm the UI reports oracle unavailable clearly if the oracle path is down.

---

## Example Configuration

Minimal RC35 testnet configuration:

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

Prebuilt binaries, when published, should use the RC35 version label:

- `digibyte-9.26.0-rc35-x86_64-linux-gnu.tar.gz`
- `digibyte-9.26.0-rc35-aarch64-linux-gnu.tar.gz`
- `digibyte-9.26.0-rc35-win64-setup.exe`
- `digibyte-9.26.0-rc35-osx.dmg`

Verify checksums and signatures from the official release location before running binaries.

---

## Commits Since RC34

- `oracle: rotate MuSig2 signer selection`
- `release: bump version to v9.26.0-rc35`
- `wallet: harden DigiDollar RC33 regression paths`

Each commit is meant to be reviewable on its own: what changed, why it changed, how it was tested, and what risk remains.

---

## Review Notes

- RC35 is a testnet release candidate, not a mainnet release.
- RC35 keeps `testnet24`; there is no new testnet reset.
- Mainnet deployment still requires Jared's review, tag, release, and deploy decision.
- Production-style DigiDollar testing must use live oracle prices and MuSig2 multi-oracle bundles.
- Regtest mock oracle behavior is stricter: tests must explicitly enable the mock oracle before relying on its price.
- The oracle rotation fix is a liveness/selection fix inside the existing V1 design, not a quorum or bundle-format change.
- Nothing was pushed externally by this release-note update.

---

## Feedback

When reporting a problem, include the exact command, log path, node version, network, wallet state, and whether the node was on `testnet24`.

Best channels:

- Gitter: https://app.gitter.im/#/room/#digidollar:gitter.im
- GitHub issues or pull requests against the DigiByte Core repository
