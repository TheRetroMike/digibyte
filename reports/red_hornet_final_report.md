# Red Hornet DigiDollar Hardening Final Report

Campaign: DigiDollar / oracle hardening review on `feature/digidollar-v1`

Ledger: `reports/red_hornet_ledger.md`

Final status: 20 waves completed. Confirmed production bugs were fixed and committed except architecture items that require Jared's approval before implementation. The configured full test suite passed after fixes. Extra DigiDollar/oracle scripts omitted by `test_runner.py` were run directly and passed after test coverage repairs.

## Executive Summary

The campaign found and fixed reachable bugs across DigiDollar consensus validation, collateral math, wallet accounting, RPC validation, Qt display safety, oracle feed/P2P handling, and MuSig2 lifecycle handling. The most important fixed bug classes were DD transfer inflation/spoofing, redemption/wallet-state corruption, reorg/accounting drift, stale or malformed oracle bundle handling, MuSig2 bitmap/session mistakes, and resource-growth edges.

Open items are intentionally limited to protocol, consensus, wallet-storage, or UX-schema decisions that should not be silently changed in a hardening pass.

## Severity Table

| Severity | IDs |
|---|---|
| Critical | `DD-RH-007` |
| High | `DD-RH-002` open architecture, `DD-RH-005`, `DD-RH-011`, `DD-RH-034` open architecture, `DD-RH-037` open architecture |
| Medium | `DD-RH-001`, `DD-RH-008` through `DD-RH-010`, `DD-RH-012` through `DD-RH-033`, `DD-RH-035`, `DD-RH-036`, `DD-RH-038` through `DD-RH-050` |
| Low / Coverage | `DD-RH-003`, `DD-RH-004`, `DD-RH-006`, `DD-RH-051` |

## Fixed Bugs

| Commit | Bugs fixed |
|---|---|
| `4740af7559` | `DD-RH-003` DCA fractional multiplier rounding |
| `c3b61a3546` | `DD-RH-004` collateral satoshi rounding |
| `a35cdb7783` | `DD-RH-001`, `DD-RH-005`, `DD-RH-007`, `DD-RH-010`, `DD-RH-011` consensus/validation fixes |
| `74a0f07df2` | `DD-RH-012` through `DD-RH-020` wallet accounting/recovery fixes |
| `19a2df1db3` | `DD-RH-006`, `DD-RH-008`, `DD-RH-009`, `DD-RH-021` through `DD-RH-028`, `DD-RH-036`, `DD-RH-042` RPC fixes |
| `d8b9d3be5f` | `DD-RH-029` through `DD-RH-033` Qt safety display fixes |
| `b0dc484897` | `DD-RH-035`, `DD-RH-038` through `DD-RH-049` oracle/MuSig2/P2P fixes |
| `b1b70b5b30` | Fuzz coverage expansion for Red Hornet DD/oracle targets |
| `305dd806ca` | Oracle block-validation test alignment for `DD-RH-010` |
| `584b8372aa` | Wallet test fixture alignment for `DD-RH-012` |
| `87051c5999` | `DD-RH-050` redemption input amount and failed-broadcast state mutation fix |
| `85c93a4fdc` | `DD-RH-051` extra functional coverage repair |

## Open Architecture Items

| Item | Decision needed |
|---|---|
| `ARCH-RH-001` | Strict post-activation `OP_ORACLE` rejection versus liveness escape hatches for missing/unparseable oracle data. |
| `ARCH-RH-002` / `DD-RH-002` | Whether `skipOracleValidation` during IBD/catch-up is acceptable consensus behavior. |
| `ARCH-RH-003` | Final watch-only DigiDollar address storage/rescan/listing model. |
| `DD-RH-034` | Qt mint owner-key generation/storage timing and HD wallet recovery design. |
| `DD-RH-037` | Mainnet active oracle roster size versus 17-slot bitmap/protocol capacity. |

## Final Test Evidence

| Command | Result |
|---|---|
| `make -j2 check` | Passed after final production fixes. |
| `python3 test/functional/test_runner.py` | Passed: 316/316 configured tests, 17 expected skips, runtime 609s. |
| `python3 test/functional/digidollar_transactions.py` | Passed after `DD-RH-050`. |
| Four repaired extra scripts | Passed: `digidollar_rpc_redemption.py`, `digidollar_rpc_estimate.py`, `digidollar_watchonly_rescan.py`, `digidollar_oracle_consistency.py`. |
| Scoped extra DigiDollar/oracle direct scripts | Passed: 34/34. |
| DD/oracle/MuSig2 fuzz smoke | Passed: 29 compiled targets, four stdin sizes each. |

Two unlisted generic scripts, `feature_assumeutxo.py` and `feature_assumevalid.py`, fail when run directly. They are outside DigiDollar/oracle scope and are not part of the configured functional runner.

## Remaining Risks

- `getredemptioninfo(position, amount)` still has a UX/schema mismatch with exact-only redemption: it can report a capped `redeemable_dd` even though `redeemdigidollar` requires the full vault amount. This should be reviewed before public launch.
- Some fuzz source files are not compiled into the current standalone fuzz binary; coverage exists for 29 compiled DD/oracle/MuSig2 targets, but not every source-level target.
- The architecture items above are the main launch blockers if Jared wants protocol behavior finalized before public exposure.

## Confirmation

No pushes were performed. Local commits are separated by individual bug where practical and by tightly coupled subsystem where one shared patch closed multiple related IDs. The final ledger and this report are the authoritative walk-through package for Jared.
