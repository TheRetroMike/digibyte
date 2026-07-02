DigiByte Core version 9.26.3
============================

DigiByte Core v9.26.3 is a patch release on top of v9.26.2. It fixes a
fresh-node header synchronization regression and makes the transaction index
on by default so DigiDollar nodes start cleanly. **All v9.26.2 users — and
anyone setting up a new node — should upgrade.** It contains no consensus rule
changes; v9.26.2's Groestl algolock and the DigiDollar BIP9 deployment are
carried forward unchanged.

How to Upgrade
==============

Shut down DigiByte Core, replace the binaries, and restart. A reindex is not
required for this release.

Notable changes
===============

Fresh-node header sync fixed (nMinimumChainWork)
------------------------------------------------

A fresh v9.26.2 node could pre-synchronize block headers to ~99.98% and then
reset back to a lower height, looping forever with `headers=0` and never leaving
initial block download.

Root cause: the v9.26.2 release metadata refresh set mainnet
`consensus.nMinimumChainWork` to a value derived from the node's *real* chainwork
(~98.68% of the tip). DigiByte's header pre-synchronization, however, measures
work from a contextless `CBlockIndex` (no height or previous-block context), which
in DigiByte's multi-algorithm work model only accumulates to roughly 28% of the
real chainwork. The pre-sync work total therefore could never cross the threshold,
so the node aborted and restarted pre-sync indefinitely.

Fix: mainnet `nMinimumChainWork` is reverted to `0x00` — the value mainnet used
for its entire history before the v9.26.2 refresh, and a value the pre-sync always
reaches. This is a chain-metadata fix only; it does not change consensus, chain
selection, or anti-DoS posture relative to mainnet's prior behavior. Testnet,
signet and regtest are unaffected (their values were already pre-sync-reachable).

Background and a full validation plan are in `HEADERS_SYNC_FIX_PLAN.md`.

Transaction index on by default
-------------------------------

`-txindex` now defaults to **on** (`DEFAULT_TXINDEX = true`). DigiDollar requires a
full transaction index (mint/transfer/redeem scanning, collateral lookups,
`getrawtransaction`), and the node already refuses to start with DigiDollar active
and `-txindex` off. Defaulting it on lets DigiDollar nodes start out of the box.
Operators who do not want a transaction index can still set `-txindex=0` on
non-DigiDollar configurations.

Note: the `-digidollar` startup flag is not required and is not enabled by
default. DigiDollar consensus, RPC, and P2P all follow the BIP9 deployment
automatically (`IsDigiDollarEnabled()`); the flag only affects regtest.

Credits
=======

Thanks to everyone who diagnosed the fresh-node sync issue against live nodes.
