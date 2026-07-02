# DigiByte v9.26.0-rc32 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ✅ SAME TESTNET — NO RESET

**RC32 uses the same `testnet23` chain as RC31.** No reset, no new genesis, no network-magic change, no port change. Existing chain data, wallets, DD positions, and oracle keys carry forward.

Oracle operators running RC31 can drop in RC32 binaries and resume. There are **no oracle key rotations in RC32**.

---

## What's New in RC32

RC32 is a **DigiDollar wallet + mining reliability release** on top of RC31. It removes unconfirmed DigiDollar transfer chaining, adds a `sendmanydigidollar` RPC so apps can pay multiple DD recipients in one transaction, updates RC32 wallet branding, and fixes a miner block-template hang exposed by the new confirmed-only DD policy.

## RC32 fix summary since RC31

- **Unconfirmed DigiDollar chaining removed** — DD transfer outputs, including trusted wallet-created change, are no longer spendable until confirmed. Wallet balance, DD coin selection, Qt available balance, and consensus validation now enforce the same confirmed-only rule.
- **Consensus rejects unconfirmed DD inputs consistently** — transfer and redemption validation now refuse DD inputs at `MEMPOOL_HEIGHT` with `dd-input-amounts-unknown`. The old mempool DD amount fallback was removed so local metadata cannot create peer-inconsistent acceptance.
- **`sendmanydigidollar` RPC added** — wallets and apps can send to multiple DigiDollar addresses in one confirmed-input transaction instead of trying to chain unconfirmed DD change through consecutive `senddigidollar` calls.
- **Qt DigiDollar send panel updated** — available DD now means confirmed spendable DD only. Pending DD remains visible as pending but cannot be selected for another send until confirmed.
- **Miner `CreateNewBlock` hang fixed** — when a DD transaction fails block-inclusion validation while selected from `mapModifiedTx`, the miner now erases the failed modified entry instead of selecting it forever. This prevents runaway debug logs and stuck block-template generation.
- **RC32 branding** — version bumped to `v9.26.0-rc32`; Qt wallet image text updated to `v9.26.0-rc32 DigiDollar`.
- **Release-note archive cleanup** — older RC28–RC30 release notes moved under `doc/release-notes/` so the repository root only carries the active/recent RC notes.

---

## 1. Confirmed-only DigiDollar transfers

RC31 still allowed wallet-trusted unconfirmed DD change to appear spendable in some paths. That made rapid consecutive sends depend on unconfirmed DD parents and created the exact behavior we do **not** want for v1: local mempool chains with DD amounts that peers cannot independently resolve from confirmed history.

RC32 changes the rule to the simple v1 policy:

> A DigiDollar output must confirm before it can be spent again.

That applies to:

- DD balance shown as spendable by the wallet.
- DD UTXO selection for `senddigidollar` and `sendmanydigidollar`.
- Qt send-screen available balance.
- Transfer validation.
- Redemption validation.

Pending DD, including the change output from your own transfer, remains tracked as pending. It simply cannot be spent until a block confirms it.

### User-facing behavior

If a wallet sends DD and immediately tries to spend the resulting DD change before confirmation, RC32 rejects the second spend with a normal wallet/validation error. Wait for the next confirmation and retry.

This is intentional. It prevents unconfirmed DD chains and keeps mempool behavior consistent across peers.

---

## 2. `sendmanydigidollar`

RC32 adds:

```bash
digibyte-cli -testnet -rpcwallet=<wallet> sendmanydigidollar "" '{"TD...addr1":5000,"TD...addr2":1250}'
```

Arguments:

1. `dummy` — must be `""`, matching the Bitcoin/DigiByte `sendmany` style.
2. `amounts` — JSON object mapping DigiDollar addresses to amounts.
3. `comment` — optional wallet comment.

Amounts follow the existing DD RPC convention:

- Integer values are cents: `5000` = `$50.00` DD.
- Decimal values are dollars: `50.25` = `5025` cents.

Example result:

```json
{
  "txid": "...",
  "amounts": {
    "TD...addr1": 5000,
    "TD...addr2": 1250
  },
  "total_amount": 6250,
  "status": "success"
}
```

### Why this matters

For payment apps and PayDesk-style flows, the clean v1 pattern is now:

- gather confirmed DD inputs,
- create one transfer transaction,
- pay many recipients,
- receive optional DD change,
- wait for confirmation before spending that change again.

That replaces the unsafe pattern of sending recipient #1, then immediately chaining the unconfirmed change into recipient #2.

---

## 3. Miner block-template hang fix

The confirmed-only DD policy exposed a miner edge case in `CreateNewBlock`.

When the mempool contained an accepted DD parent plus unconfirmed DD descendants, the descendants correctly failed block inclusion with `dd-input-amounts-unknown`. But if the failed transaction was being considered from `mapModifiedTx`, the miner did not erase that modified entry after the DD failure. The outer package-selection loop then selected the same failing entry forever.

Observed failure mode:

- `getblocktemplate` / mining appears stuck.
- `debug.log` grows explosively with repeated DD rejection lines.
- Disk can fill if left running.

RC32 handles DD validation failure like the existing package-test failure paths: if a modified candidate fails and is not added to the block, erase it from `mapModifiedTx` and continue. This fixes the hang for unconfirmed DD chains and for any other DD validation failure encountered during block assembly.

---

## 4. Validation results

| Category | Result | Status |
|----------|--------|--------|
| Full unit suite | `./src/test/test_digibyte --show_progress` — 2936 test cases | ✅ |
| Functional test runner | `test/functional/test_runner.py --jobs=4` | ✅ |
| DD wallet/RPC/redteam targeted tests | 519 cases, 3361 assertions | ✅ |
| `digidollar_transfer.py` | Covers `sendmanydigidollar` and confirmed-only DD change | ✅ |
| `digidollar_rpc_gating.py` | Confirms `sendmanydigidollar` is activation-gated | ✅ |
| Fuzz smoke targets | `mini_miner`, `mini_miner_selection`, `dd_consensus_rules`, `dd_amount_validation`, `dd_collateral_math`, `dd_mint_params`, `dd_txbuilder_mint`, `dd_txbuilder_transfer`, `dd_validate_mint`, `validation_load_mempool` | ✅ |
| Local Qt testnet startup | Clean `testnet23` debug log, synced to height 6128, oracle messages received and cached | ✅ |
| Scrypt mining template | `getblocktemplate` succeeds for scrypt with empty template tx set | ✅ |

---

## Upgrade Path

RC32 is a drop-in binary replacement on `testnet23`.

1. Stop the RC31 node or Qt wallet.
2. Install the RC32 binary.
3. Start with the same data directory and wallet.
4. Keep `txindex=1` enabled for DD nodes.
5. Oracle operators: load/unlock the same oracle wallet and run `startoracle <your_oracle_id>` if auto-start does not resume.

No chain reset or wallet migration is required.

Manual oracle start if needed:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

Encrypted wallet operators should unlock first:

```bash
digibyte-cli -testnet -rpcwallet=oracle walletpassphrase "your passphrase" 600
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

---

## Oracle Operators (RC32)

No oracle slot changes from RC31.

| ID | Operator | Status |
|----|----------|--------|
| 0 | Oracle slot 0 | ✅ Active |
| 1 | Oracle slot 1 | ✅ Active |
| 2 | Oracle slot 2 | ✅ Active |
| 3 | Oracle slot 3 | ✅ Active |
| 4 | Oracle slot 4 | ✅ Active |
| 5 | Oracle slot 5 | ✅ Active |
| 6 | Oracle slot 6 | ✅ Active |
| 7 | Oracle slot 7 | ✅ Active |
| 8 | Oracle slot 8 | ✅ Active |
| 9 | Oracle slot 9 | ✅ Active |
| 10 | Oracle slot 10 | ✅ Active |
| 11 | Oracle slot 11 | ✅ Active |
| 12 | Oracle slot 12 | ✅ Active |
| 13 | Oracle slot 13 | ✅ Active |
| 14 | Oracle slot 14 | ✅ Active |
| 15 | DigiSwarm | ✅ Active |
| 16 | Oracle slot 16 | ✅ Active |
---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (`testnet23`) |
| Genesis Hash | `0xa19e809bb060f7f50c05a9bec7fdefedd8497aa0bd6ccca6f55c86090963e4ca` |
| Network Magic | `fd d2 b9 e4` |
| Default P2P Port | **12030** |
| Default RPC Port | **14026** |
| Oracle Consensus | **9-of-17** |
| Oracle Bundle Format | **MuSig2 aggregate signing (`v0x03`, 84 bytes)** |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

---

## Configuration

```ini
testnet=1

[test]
digidollar=1
txindex=1
algo=scrypt
addnode=oracle1.digibyte.io
```

> **Note:** `txindex=1` is enforced at startup for DD-enabled nodes. Make sure it is in the correct section (`[test]` for testnet, `[main]` for mainnet). Global placement above all sections also works.

### Scrypt CPU mining against local Qt/node

For local testnet scrypt mining, both the node and miner must agree on scrypt:

```ini
[test]
algo=scrypt
```

Start the miner with the scrypt flag explicitly:

```bash
cd /home/jared/Code/cpuminer
./minerd -c dgb-solo-mining.json -a scrypt -t 12
```

Healthy mining output includes:

- `12 miner threads started, using 'scrypt' algorithm.`
- `Long-polling activated for http://127.0.0.1:14026/`
- no repeated HTTP 401/500 errors.

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc32-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc32-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc32-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc32-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc32-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc32-aarch64-linux-gnu.tar.gz` |

Binaries attached to the GitHub release once Guix builds complete and are verified.

---

## Troubleshooting

### "My DD change disappeared after sending"

It did not disappear. In RC32, unconfirmed DD change is pending, not spendable. Wait for the transfer to confirm, then it becomes available for the next DD spend.

### "senddigidollar says insufficient DD balance right after I sent DD"

That is expected if your only remaining DD is unconfirmed change. Wait for one confirmation or use `sendmanydigidollar` to pay multiple recipients in the original transaction.

### "Previous DigiDollar transfer has not confirmed yet"

RC32 intentionally blocks DD chains through unconfirmed parents. Wait for confirmation and retry.

### "getblocktemplate hangs or debug.log grows rapidly"

This was the miner bug fixed in RC32. Upgrade to RC32. If you are still testing older binaries, stop the node, clear stale mempool state, and restart with a clean mempool.

### "My oracle did not start automatically"

Load and unlock the oracle wallet, then start manually:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle walletpassphrase "your passphrase" 600
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### "Node refuses to start: oracle roster alignment mismatch"

That is the RH-50 guard from RC31 working as intended. It means `consensus.vOraclePublicKeys` and `vOracleNodes` disagree in your local chainparams. Run an official RC32 binary and do not hand-edit chainparams.

---

## Commits Since RC31

```
0b4959f563 wallet: remove unconfirmed DigiDollar chaining
9143bed9b9 rpc: add sendmanydigidollar
14c08a9f1b release: bump DigiDollar RC32 wallet branding
6b5ff516c3 miner: fix CreateNewBlock hang when DD tx fails in mapModifiedTx
a7c0c97521 Doc Cleanup
```

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
