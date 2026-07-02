# DigiByte Core v9.26.0-rc40 Release Notes

RC40 is the DigiDollar final hardening and launch-readiness release candidate on top of RC39.

This release focuses on real security findings from the final Red Hornet review: activation guards, mint validation, sendmany safety, redemption collateral release, MuSig2 attempt isolation, wallet locked-state behavior, Qt display safety, and RPC coverage.

Development branch: `feature/digidollar-v1`

Release: https://github.com/DigiByte-Core/digibyte/releases/tag/v9.26.0-rc40

---

## Summary

RC40 does not reset the public DigiDollar testnet.

It keeps:

- Testnet: `testnet24`
- DigiDollar activation height: `600`
- Oracle activation height: `600`
- Oracle quorum: `9-of-17`
- Oracle bundle format: `v0x03` MuSig2 aggregate bundles
- Existing DigiDollar economic rules

RC40 is a security-hardening and readiness release. It does not activate mainnet by itself.

---

## What Changed

### DigiDollar RPC activation guards

RC40 fixes and proves the pre-activation RPC gate.

Operational DigiDollar and oracle RPC commands now reject before DigiDollar BIP9 activation. The only intentional exception is `getdigidollardeploymentinfo`, which must remain available so users can monitor deployment status before activation.

The final recheck covered the live node and wallet RPC tables:

- 32 registered DigiDollar/oracle RPC commands.
- 31 operational commands requiring BIP9 activation.
- 31/31 operational commands verified as BIP9-gated.
- `getdigidollardeploymentinfo` remains ungated by design.

### Mint amount validation

RC40 tightens mint input parsing so malformed or unsafe mint amounts cannot slip through RPC parsing into transaction construction.

This protects the mint path before wallet code builds collateral and token outputs.

### DigiDollar sendmany duplicate-recipient rejection

RC40 rejects duplicate recipients in `sendmanydigidollar`.

This prevents user-facing confusion where the same recipient could appear more than once in a batch and make the intended send amount unclear.

### Redemption collateral release validation

RC40 strengthens redemption checks so collateral release must match the correct DigiDollar position and burn requirement.

The important rule remains simple: redemption cannot release locked collateral unless the right position is redeemed with the required DigiDollar burn.

### MuSig2 attempt isolation

RC40 fixes a MuSig2 attempt-preemption issue.

Oracle signing attempts are isolated so stale or lower-priority attempt data cannot replace the active attempt in a way that breaks signing convergence.

### MuSig2 context proposal bounds

RC40 adds bounds checking for context proposal handling.

This prevents oversized or malformed context proposal data from becoming a practical resource or validation problem.

### Wallet locked-state and spendability safety

RC40 fixes DigiDollar wallet RPC spendability reporting for locked and encrypted wallet states.

Locked wallets must not make DigiDollar outputs look safely spendable when signing cannot actually proceed.

### Qt mint unlock-height display

RC40 fixes the Qt mint unlock-height display.

The UI now reports the intended unlock height consistently with the actual lock tier behavior, reducing the risk of users misunderstanding when collateral can be redeemed.

### Test isolation fixes

RC40 includes two test-only fixes for ERR state isolation.

These do not change product behavior. They make the test suite deterministic so Red Hornet validation results are reliable.

---

## What Did Not Change

RC40 does not change:

- Mainnet activation status.
- Testnet network identity.
- Testnet genesis.
- Default testnet ports.
- Oracle roster.
- Oracle quorum.
- Oracle epoch length.
- On-chain oracle bundle format.
- DigiDollar economics.
- Wallet database format.
- RPC schema in a breaking way.
- P2P message formats in a breaking way.

RC40 is not a consensus redesign. It is a focused hardening release.

---

## Testnet24 Network Details

| Item | RC40 value |
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

Older operator notes that mention `testnet23` or P2P port `12030` are stale for RC40. Use the values above.

---

## Validation Status

Final Red Hornet validation completed on May 19, 2026 from `feature/digidollar-v1`.

| Gate | Status |
| --- | --- |
| Build: `make -j"$(nproc)"` | PASS |
| Unit tests: `./src/test/test_digibyte --show_progress` | PASS, 3,376 test cases |
| Qt tests: `./src/qt/test/test_digibyte-qt -platform offscreen` | PASS |
| Functional tests: `test/functional/test_runner.py --jobs=4` | PASS, 354 passed and 17 expected skips |
| Extended functional tests: `test/functional/test_runner.py --jobs=4 --extended` | PASS, 358 passed and 17 expected skips |
| Fuzz target enumeration | PASS, 247 targets |
| Fuzz corpus replay | PASS, 247/247 targets |
| Post-final RPC guard recheck: `test/functional/digidollar_rpc_gating.py` | PASS, all 31/31 gated RPCs blocked before activation |

Validation logs:

- Build: `/tmp/red_hornet_final_make.log`
- Unit tests: `/tmp/red_hornet_final_unit.log`
- Qt tests: `/tmp/red_hornet_final_qt.log`
- Functional tests: `/tmp/red_hornet_final_functional.log`
- Extended functional tests: `/tmp/red_hornet_final_functional_extended.log`
- Fuzz target list: `/tmp/red_hornet_final_fuzz_targets.txt`
- Fuzz run: `/tmp/red_hornet_final_fuzz.log`
- RPC guard recheck: `/tmp/rc40_rpc_gating_recheck.log`

Fuzz mode used:

- Corpus replay using `/tmp/rc38_qa_assets/fuzz_corpora`.
- The local build was not a libFuzzer build, so the timed empty-corpus libFuzzer path was not used for the final gate.
- All registered targets selected by the local fuzz binary passed.

---

## Commit Summary Since RC39

- `634942c5bb` doc: streamline RC39 release notes
- `b47914414f` digidollar rpc: fix DD-RHF-001 pre-activation list gating
- `3187e358de` digidollar mint: fix DD-RHF-002 malformed amount validation
- `178f7116a0` Update .gitignore
- `e00a9a3c63` digidollar rpc: fix DD-RHF-003 duplicate sendmany recipients
- `4f40349043` digidollar redemption: fix DD-RHF-004 collateral return validation
- `23ddf9c94a` digidollar musig2: fix DD-RHF-005 attempt preemption
- `ba64275d93` digidollar wallet rpc: fix DD-RHF-006 locked spendability
- `c60ec6104c` digidollar qt: fix DD-RHF-007 mint unlock height
- `9bb5943f20` digidollar tests: fix W12-TG-001 ERR state isolation
- `05d9305edd` digidollar tests: fix W12-TG-002 RH44 ERR isolation
- `38a15698a8` digidollar musig2: fix DD-RHF-008 context proposal bounds
- `6c19156e9c` digidollar rpc: cover all BIP9-gated commands

---

## Notes For Testers

Please focus RC40 testing on the hardened paths:

- RPC behavior before and after DigiDollar BIP9 activation.
- Mint amount validation and rejected malformed amounts.
- `sendmanydigidollar` duplicate-recipient rejection.
- Redemption collateral release and required DigiDollar burn.
- Locked and encrypted wallet DigiDollar spendability reporting.
- Qt mint lock tier and unlock-height display.
- Oracle MuSig2 attempt behavior after restart or reconnect.
- Oracle context proposal handling under malformed or oversized input.

Oracle operators should continue using `testnet24` and keep assigned oracle slots online.

---

## Known Risks

- RC40 does not include mainnet activation. Mainnet launch still requires the explicit release and activation decision.
- If fewer than 9 valid oracle operators are online and fresh, new oracle bundles should fail closed.
- Mixed older RC oracle nodes may not reliably complete the current MuSig2 signing flow.
- Operator setup material outside these release notes should be checked for stale `testnet23` or `12030` references before public publication.

---

## Bottom Line

RC40 is the DigiDollar final hardening candidate after the Red Hornet review.

The code passed the final build, unit, Qt, functional, extended functional, fuzz, and RPC activation-guard checks. No unfixed DigiDollar code blocker remains in this candidate.
