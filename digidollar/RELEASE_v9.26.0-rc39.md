# DigiByte Core v9.26.0-rc39 Release Notes

RC39 is a DigiDollar wallet parity and UI/UX release for the public DigiDollar testnet.

This release focuses on making DigiDollar feel more like a normal first-class wallet feature inside DigiByte Core. The main work is wallet behavior, RPC visibility, safer send paths, and Qt UI improvements requested during testnet feedback.

Development branch: `feature/digidollar-v1`

Release: https://github.com/DigiByte-Core/digibyte/releases/tag/v9.26.0-rc39

---

## Summary

RC39 improves the DigiDollar wallet experience without changing the public testnet network.

It keeps:

- Testnet: `testnet24`
- DigiDollar activation height: `600`
- Oracle activation height: `600`
- Oracle quorum: `9-of-17`
- Oracle bundle format: `v0x03` MuSig2 aggregate bundles
- Existing DigiDollar economic rules

There is no testnet reset in RC39.

---

## What Changed

### DigiDollar list-unspent RPCs

RC39 adds DGB-style wallet visibility for DigiDollar spendable outputs.

New/updated RPC behavior includes:

- Listing real spendable DigiDollar UTXOs.
- Reporting txid, vout, amount, confirmations, spendability, safety, scriptPubKey, and address when available.
- Filtering by confirmation range and address.
- Hiding already spent DigiDollar outputs.

This gives operators and wallet tooling a clearer way to inspect available DigiDollar funds before sending or consolidating.

### DigiDollar selected-input send planning

RC39 adds selected-input transfer planning for DigiDollar sends.

This is the backend needed for coin-control style behavior:

- Validate explicitly selected DigiDollar inputs.
- Reject duplicate, unknown, spent, unconfirmed, or insufficient selections.
- Reuse the normal DigiDollar transfer build path instead of creating a separate send implementation.
- Support selected-input behavior through `senddigidollar` and `sendmanydigidollar`.

### DigiDollar Send UI parity

The Qt DigiDollar Send screen now behaves more like the normal DGB Send workflow.

RC39 adds:

- Selected DigiDollar input count.
- Selected DigiDollar input amount.
- Warning when selected DigiDollar inputs are below the requested send amount.
- Clearer confirmation behavior before sending.
- Better parity with normal DGB wallet coin-control expectations.

### DigiDollar Receive request editing

RC39 improves DigiDollar receive request handling in Qt.

Receive requests can now be edited and removed more like normal DGB receive requests:

- Edit label, message, and amount.
- Persist edited DigiDollar receive requests.
- Remove DigiDollar receive requests from wallet storage.
- Keep DigiDollar and DGB receive request behavior separated correctly.

### Safer sendmany behavior

RC39 adds preflight checks for large DigiDollar batch sends.

The wallet now checks transaction capacity before attempting to build oversized transfers, producing clearer errors before broadcast.

### Wallet metadata and locked-key safety

RC39 preserves DigiDollar redeem metadata and locked-key handling during wallet flows.

This helps protect wallet state when DigiDollar operations interact with redemption and wallet bookkeeping paths.

### Amount display cleanup

RC39 standardizes DigiDollar amount precision in Qt display paths.

This makes DigiDollar values easier to read consistently across the wallet UI.

### Mint UI validation cleanup

RC39 updates Qt DigiDollar mint validation so wallet limits come from chain parameters and mint confirmation checks are revalidated correctly.

---

## What Did Not Change

RC39 does not change:

- Mainnet activation status.
- Testnet network identity.
- Testnet genesis.
- Oracle roster.
- Oracle quorum.
- Oracle epoch length.
- On-chain oracle bundle format.
- DigiDollar economics.
- Existing v0x03 bundle validation rules.

RC39 is a wallet/RPC/UI release, not a consensus reset.

---

## Testnet24 Network Details

| Item | RC39 value |
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

## Commit Summary Since RC38

- `3c58167a2a` qt: derive DigiDollar mint limits from chain params
- `53b0c66afe` qt: revalidate DigiDollar mint confirmations
- `0de1625353` qt: standardize DigiDollar amount display precision
- `f248fcb406` wallet: preserve DigiDollar redeem metadata and locked keys
- `4fcf45c652` wallet: preflight DigiDollar sendmany transaction capacity
- `5669a2af82` release: bump version to v9.26.0-rc39
- `5ab5403cf4` rpc: add DigiDollar list unspent wallet RPCs
- `ca3ac0e48a` wallet: add DigiDollar selected-input transfer planning
- `33d91023a1` qt: add DigiDollar send coin-control parity
- `b422716d61` qt: edit DigiDollar receive requests
- `02c13f9d15` doc: add RC39 release notes

---

## Notes For Testers

Please focus RC39 testing on wallet behavior:

- DigiDollar Send and Receive flows.
- DigiDollar receive request edit/remove behavior.
- DigiDollar selected-input and coin-control behavior.
- `listdigidollarunspent` / `listdigidollarutxos` output.
- `senddigidollar` and `sendmanydigidollar` with and without selected inputs.
- Batch send capacity errors.
- Normal DGB wallet behavior, to make sure RC39 did not regress existing wallet flows.

Oracle operators should continue running testnet24 and keep their assigned oracle slots online.

Thank you to everyone testing, reporting issues, suggesting wallet improvements, and helping make DigiDollar easier to use inside DigiByte Core.

