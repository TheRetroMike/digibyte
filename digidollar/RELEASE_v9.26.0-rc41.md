# DigiByte Core v9.26.0-rc41 Release Notes

RC41 is a DigiDollar launch-parameter, testnet-reset, hardening, and usability release candidate on top of RC40.

This release resets public DigiDollar testnet to `testnet25`, moves the testnet P2P port to `12032`, reserves 35 oracle slots for mainnet/testnet, sets the mainnet DigiDollar activation floor to block `23,627,520`, and keeps the RC40 hardening fixes for mint validation, wallet state, no-wallet builds, Dandelion cleanup, and Qt usability/theme cleanup.

Development branch: `feature/digidollar-v1`

Release: https://github.com/DigiByte-Core/digibyte/releases/tag/v9.26.0-rc41

---

## Read This First

RC41 starts a fresh DigiDollar testnet: `testnet25`.

RC40 used `testnet24`. RC41 does not. Old `testnet24` blocks, chainstate, peer addresses, oracle messages, and DigiDollar positions are not RC41 proof. Back up wallet and oracle key material before wiping old data, then start clean on `testnet25`.

Why the reset matters:

- A new genesis makes it obvious which nodes are actually testing RC41.
- New network magic bytes prevent accidental peer mixing with `testnet24`.
- The P2P port moves from `12031` to `12032`, so firewalls, seeders, addnodes, and operator configs need to be updated.
- Oracle signatures and MuSig2 context are chain-bound, so old testnet24 oracle messages cannot satisfy testnet25.
- The 35-slot oracle reserve needs a fresh public proving network before mainnet launch decisions.

What changed from RC40 for testnet operators:

- Data directory: `testnet24` -> `testnet25`
- P2P port: `12031` -> `12032`
- Network magic: `fe c4 b7 e5` -> `fe c5 b8 e6`
- Genesis block: new RC41 genesis
- Oracle roster shape: 17 active slots inside a 35-slot reserved roster
- Oracle quorum: 9 signatures from the active configured MuSig2 keyset

What did not change:

- The default testnet RPC port remains `14026`.
- DigiDollar activation height remains `600` on testnet.
- Oracle activation height remains `600` on testnet.
- The on-chain oracle bundle format remains `v0x03`.
- RC41 does not activate DigiDollar on mainnet by itself.

Oracle operator note:

Private key format did not change, but the network did. Operators must run RC41, use the `testnet25` datadir, open P2P port `12032`, and make sure their oracle slot/key matches the RC41 chainparams roster. Any old signed oracle messages, cached nonces, peer state, or bundle attempts from `testnet24` are intentionally invalid on `testnet25` because the signing context is bound to the new chain.

Mainnet oracle note:

The mainnet roster keeps placeholder/reserve slots for launch planning. Mainnet oracle operators still need to provide their own mainnet oracle keys before those slots can be made active in a later release.

---

## Summary

RC41 resets the public DigiDollar testnet.

It sets:

- Testnet: `testnet25`
- DigiDollar activation height: `600`
- Oracle activation height: `600`
- Oracle roster: 35 reserved slots, slots 0-16 active at launch
- Oracle quorum: 9 signatures from the configured active MuSig2 keyset
- Oracle bundle format: `v0x03` MuSig2 aggregate bundles
- Mainnet DigiDollar activation floor: block `23,627,520`
- Existing DigiDollar economic rules

RC41 is not an economic redesign. It is a focused launch-configuration, correctness, wallet-state, documentation, and Qt display/theme cleanup release.

---

## What Changed

### Mainnet BIP9 activation window

RC41 sets the mainnet DigiDollar BIP9 signaling window to June 1, 2026 through June 1, 2027.

The threshold remains 70% over a 40,320-block mainnet signaling window. Mainnet activation status does not change in this release; activation still requires miner signaling and the BIP9 state machine.

### Mainnet activation floor

RC41 sets the mainnet DigiDollar `min_activation_height`, `nDDActivationHeight`, and `nOracleActivationHeight` to block `23,627,520`.

This aligns the direct height gate with the planned June 2026 release window while preserving BIP9 miner signaling. There is no fixed activation timestamp after release; mainnet activation still depends on miners reaching the 70% signaling threshold and then the chain reaching the minimum activation height.

### Testnet25 reset

RC41 resets public DigiDollar testnet to `testnet25`.

The new public testnet uses P2P port `12032`, data directory `~/.digibyte/testnet25/`, network magic `fe c5 b8 e6`, and a fresh genesis block. RPC remains on `14026`.

This is a deliberate network reset like the RC34 move to `testnet24`. Nodes on RC40/`testnet24` will not follow RC41/`testnet25`, and RC41 nodes should not be pointed at old testnet24 chainstate.

Operators must update:

- Data directory references from `testnet24` to `testnet25`.
- Firewall and hosting rules from P2P port `12031` to `12032`.
- Static `addnode` entries to use RC41 peers on port `12032`.
- DNS seeders to the new genesis, magic bytes, and port.
- Oracle service configs so `startoracle` runs against the RC41 chainparams roster and not stale testnet24 peer/key state.

### 35-slot oracle reserve

RC41 reserves 35 oracle slots on mainnet and testnet.

Slots 0-16 remain the configured active MuSig2 keyset at launch, and slots 17-34 are inactive reserve slots. A reserve slot does not sign or count toward consensus until a later release adds the operator's key to `consensus.vOraclePublicKeys` and marks that slot active in chainparams. The threshold remains 9 signatures.

### Mainnet minimum mint enforcement

RC41 fixes the mainnet DigiDollar mint-floor parameter.

Wallet and RPC paths already enforced the documented $100 minimum mint amount. Mainnet consensus parameters now enforce the same floor for raw DigiDollar mint transactions as soon as DigiDollar activates on mainnet.

### Stricter mint lock-height validation

RC41 rejects malformed mint OP_RETURN metadata with a missing, non-minimal, zero, or negative lock height.

Before this fix, validation could normalize a bad lock height into a default testing value. The mint path now fails closed instead of inventing consensus-critical metadata.

### Stale redemption metadata repair

RC41 includes the post-RC40 wallet repair for stale DigiDollar mint metadata before redemption.

The wallet now uses the original mint transaction as the authority for amount, collateral, lock tier, and unlock height before reconciliation, redeem construction, and signing. If mint metadata cannot be verified, redemption fails closed and tells the user to rescan or restore the wallet.

### No-wallet build boundary

RC41 fixes `--disable-wallet` builds.

Node-level DigiDollar RPC code remains available, while wallet-only DigiDollar RPC helpers compile only when wallet support is enabled. Wallet aggregate health has a no-wallet stub instead of linking against missing wallet symbols.

### DigiDollar receive-request amount validation

RC41 fixes malformed amount handling in the Qt DigiDollar Receive panel.

Malformed amounts such as `12.bad` are rejected before address generation. Valid request amounts are parsed as fixed-point DigiDollar cents, stored consistently, and emitted in payment URIs in canonical form.

### Qt overview blockchain totals

RC41 updates the DigiDollar overview panel to use Blockchain wording for chain-wide status and totals. The System Health row now reads values such as `592% Collateralized`, and the health bar now reads values such as `592.0% Collateralization`.

The blockchain DD supply and DGB locked cards were widened and regression-tested for launch-scale values, including at least `$999,000,000.00 DD`, a stress value of `$11,000,000,000.00 DD`, and `21,000,000,000.00 DGB`.

### Dandelion final-reject wallet cleanup

RC41 fixes a wallet-state bug when a Dandelion stem transaction is accepted locally but later rejected during final mempool promotion.

Rejected stem transactions are now removed from the stempool and wallet-owned DigiDollar transactions are marked abandoned when they are no longer confirmed or in mempool. This prevents failed mints or sends from staying visible as live pending transactions.

### DigiDollar transaction sorting

RC41 preserves the user's selected sort order in the DigiDollar Transactions table.

The first load still defaults to Date descending, but later refreshes no longer force the table back to Date descending every few seconds.

### Mint collateral ratio bar

RC41 changes the Qt mint collateral-ratio progress bar to show the practical 200%-500% safety band.

The exact collateral ratio text is unchanged. Ratios above 500% still display their real value in the label, while the visual bar clamps to full so it does not imply that 500% is only a partial safety level.

### Pending DigiDollar balance styling

RC41 adds explicit light and dark theme rules for the Pending DD row on the Overview page.

The pending balance calculation did not change. The row now uses the same spacing, alignment, font sizing, and theme colors as the adjacent balance rows.

### DigiDollar green section theme

RC41 gives the DigiDollar area of the Qt wallet a distinct green theme so users can visually tell when they are inside the DigiDollar section instead of the regular DigiByte wallet pages.

The top DigiDollar navigation button, DigiDollar subtabs, DigiDollar page backgrounds, card borders, table surfaces, progress bars, and DigiDollar action buttons now use scoped green theme rules. The regular DigiByte pages keep the existing blue wallet theme.

The theme work is scoped to DigiDollar Qt widgets and stylesheets. It does not change wallet accounting, consensus, oracle rules, RPC schemas, or P2P behavior.

### Documentation audit

RC41 includes a full documentation audit against current code.

Core navigation, DigiDollar architecture, oracle, activation, wallet integration, exchange integration, setup, and repo-map documents were updated so future developers see the current implementation instead of older plans or stale assumptions.

---

## What Did Not Change

RC41 does not change:

- Mainnet activation status.
- Mainnet signaling threshold.
- Oracle epoch length.
- On-chain oracle bundle format.
- DigiDollar address formats.
- DigiDollar wallet database format.
- ERR policy.
- DCA policy.
- Default testnet RPC port.
- P2P message formats in a breaking way.

RC41 keeps the DigiDollar economic model intact.

---

## Testnet25 Network Details

| Item | RC41 value |
| --- | --- |
| Testnet name | `testnet25` |
| Data directory | `testnet25` |
| Genesis hash | `0x901d46e44cd40764de5ce383717b0d6afd96190e2c6b931a4737ebc8cda96df4` |
| Merkle root | `0xd3ba96686218ada443cc6ad23563b0e6a5aa4990dc8d8e6c0c3ba5dd0ef7538b` |
| Genesis time | `2026-05-21 20:00:00 UTC` |
| Genesis nonce | `384415` |
| Network magic | `fe c5 b8 e6` |
| Default P2P port | `12032` |
| Default RPC port | `14026` |
| DigiDollar activation height | `600` |
| Oracle activation height | `600` |
| Oracle epoch length | `40` blocks |
| Oracle roster | 35 reserved slots, 17 active slots at launch |
| Oracle quorum | 9 signatures from the configured active MuSig2 keyset |
| Oracle bundle format | `v0x03` MuSig2 aggregate bundle |

Older operator notes that mention `testnet24` or P2P port `12031` are stale for RC41. Use the values above.

Minimum migration checklist:

1. Back up wallets and any oracle key material.
2. Install/run RC41.
3. Start with `-testnet` and the new `testnet25` datadir.
4. Open and advertise P2P port `12032`.
5. Replace old `testnet24` addnodes and seed data.
6. Confirm `getblockchaininfo` reports the RC41 genesis chain.
7. Confirm `getoracles` shows the expected 35-slot roster, with active configured operators online and reserve slots inactive.
8. Start the assigned oracle slot only after confirming the slot/key matches RC41 chainparams.

---

## Validation Status

Focused RC41 validation completed on May 21, 2026 from `feature/digidollar-v1`.

| Gate | Status |
| --- | --- |
| Build: `make -j"$(nproc)"` | PASS |
| Unit tests: `./src/test/test_digibyte --show_progress` | PASS |
| Qt tests: `./src/qt/test/test_digibyte-qt -platform offscreen` | PASS |
| Functional tests: `test/functional/test_runner.py --jobs=4` | PASS |
| Fuzz target inventory: `PRINT_ALL_FUZZ_TARGETS_AND_ABORT=1 ./src/test/fuzz/fuzz` | PASS, 247 targets |
| Fuzz smoke: `test/fuzz/test_runner.py -l INFO --par=4 --empty_min_time=30 ...` | PASS |
| Multi-oracle testnet25 script: `KEEP_QT_OPEN=1 ./test_multi_oracle_testnet.sh` | PASS, 223/223 |
| DigiDollar green-theme Qt QA | PASS, Bob wallet inspected across all DigiDollar tabs; Qt tests cover the required light/dark theme selectors |
| Whitespace check: `git diff --check` | PASS |

Validation logs:

- Build: `/tmp/rc41_final_make_after_gui_text.log`
- Unit tests: `/tmp/rc41_final_unit_after_gui_text.log`
- Qt tests: `/tmp/rc41_final_qt_after_gui_text.log`
- Functional tests: `/tmp/rc41_final_functional_after_gui_text.log`
- Fuzz target list: `/tmp/rc41_fuzz_libfuzzer_targets.txt`
- Fuzz smoke: `/tmp/rc41_libfuzzer.log`
- Multi-oracle testnet25 keep-open run: `/tmp/rc41_multi_oracle_testnet_keepopen_final.log`
- Multi-oracle internal test log: `/tmp/digidollar_debug_logs/test_run_20260521_172144.log`
- Final green-theme multi-oracle internal test log: `/tmp/digidollar_debug_logs/test_run_20260521_183859.log`
- Final green-theme Qt screenshots: `/tmp/digidollar_green_qa/bob_tabs_final2/contact.png`, `/tmp/digidollar_green_qa/bob_tabs_final2/root_tooltip_receive_generate2.png`

---

## Commit Summary Since RC40

- `703a8d9b46` qt: add green DigiDollar section theme
- `378017b37b` chore: ignore local RC40 audit notes
- `742cd0af69` docs: clarify RC41 testnet25 migration notes
- `b36b1e411b` test: keep multi-oracle Qt wallets open after run
- `d25cb18c5d` docs: update RC41 launch and oracle reserve notes
- `99bc282f1a` digidollar qt: update blockchain collateralization display
- `9555ecd4a7` digidollar launch: configure testnet25 oracle reserve
- `9fc0d22947` doc: update RC41 notes for mainnet activation window
- `d9cd85461e` docs: align DigiDollar activation window with RC41
- `2261b40c54` digidollar mainnet: set BIP9 window for June 2026
- `7ae38b0c93` doc: add RC41 release notes
- `d473616a55` digidollar qt: style pending balance row
- `c8a62bc036` digidollar qt: clamp mint ratio bar scale
- `0a3a6b8434` digidollar qt: preserve transaction sort order
- `645125c040` dandelion: drop rejected stem transactions
- `eaa79030e6` release: bump version to v9.26.0-rc41
- `af69648808` digidollar qt: fix DD-RHF-013 receive amount validation
- `98563f1940` digidollar wallet: fix DD-RHF-012 no-wallet build boundary
- `3b6f61d4bb` digidollar tests: accept DD-RHF-011 metadata rejection
- `3c28172cae` digidollar mint: fix DD-RHF-011 lock height validation
- `68fe7a2836` digidollar qt: fix DD-RHF-010 amount label width
- `0607f081da` digidollar mainnet: fix DD-RHF-009 minimum mint enforcement
- `f466b7413b` docs: audit DigiByte and DigiDollar docs against current code
- `16f6b77c8e` Update .gitignore

---

## Notes For Testers

Please focus RC41 testing on:

- Fresh `testnet25` startup, peer discovery, and block production on P2P port `12032`.
- Clean migration from old `testnet24` local data to fresh `testnet25` data.
- Firewall, addnode, and DNS seed behavior after the P2P port change from `12031` to `12032`.
- Oracle operator startup with RC41 slot/key configuration and no stale testnet24 oracle messages.
- Oracle slots 0-16 active and slots 17-34 inactive/reserve on mainnet and testnet.
- Mainnet activation parameters: BIP9 June 1, 2026 to June 1, 2027 with min height `23,627,520`.
- Mainnet/testnet mint amount limits after activation.
- Raw or manually constructed mint transactions with bad OP_RETURN lock-height metadata.
- Redemption of older positions whose wallet metadata may have been stale.
- `--disable-wallet` build behavior.
- DigiDollar Receive requests with valid, blank, and malformed amount fields.
- Dandelion-enabled DigiDollar mints and sends that later leave stem relay.
- DD Transactions table sorting across refreshes.
- Mint collateral-ratio display for 200%, 500%, and 1000% tiers.
- Pending DD row appearance in light and dark themes.
- DigiDollar Overview blockchain totals display at large DD/DGB values.
- DigiDollar green section theme across all DigiDollar tabs, with regular DigiByte pages still using the existing blue theme.

Oracle operators should migrate to `testnet25`, open P2P port `12032`, and keep assigned active oracle slots online.

---

## Known Risks

- RC41 does not include mainnet activation. Mainnet launch still requires the explicit release and activation decision.
- Mainnet reserve oracle slots are placeholders until operators provide mainnet oracle keys and a later release adds those keys to chainparams.
- Public testnet25 DNS seeders must be reset to the new genesis, magic bytes, and P2P port before seed-based peer discovery is considered ready.
- If fewer than 9 valid oracle operators are online and fresh, new oracle bundles should fail closed.
- Mixed older RC oracle nodes may not reliably complete the current MuSig2 signing flow.

---

## Bottom Line

RC41 is the testnet25 and mainnet-launch-parameter release candidate after RC40.

It resets public testnet, reserves the 35-slot oracle roster, sets the mainnet activation floor to block `23,627,520`, keeps the DigiDollar economic model unchanged, carries forward the validated RC40 hardening fixes, and adds the scoped green DigiDollar Qt section theme.
