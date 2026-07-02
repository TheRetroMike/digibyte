# Fresh-Node Headers Sync — Validated Root Cause & KISS Fix Plan

*Independent audit (2026-06-30) of `FRESH_NODE_HEADERS_PRESYNC_FIX.md`, validated against
code, the live mainnet node, and the failing fresh node `192.168.1.143`.*

**Branch note:** this was investigated on `groestl-mining-modded`, but the affected code
(`src/kernel/chainparams.cpp`) is identical on `develop`. **The fix must land on `develop`
(the v9.26.x release line), not on the Groestl mining branch.** This plan file travels with it.

---

## TL;DR

Fresh v9.26.2 nodes can't sync because the **2026-06-26 release commit `47b9ea3481`
("refresh mainnet chain metadata") set `nMinimumChainWork` from `0x00` to a value equal to
~98.68% of the *real* tip chainwork** — but DigiByte's headers **pre-sync measures
"contextless" work that only ever reaches ~28% of the real chainwork.** So pre-sync tops out
around 28% and can never cross the 98.68% gate → it resets forever (the 99.98% → 76% loop).

**KISS fix: revert mainnet `nMinimumChainWork` to `0x00`** (one line). That is exactly how
mainnet ran for its entire history; it restores fresh sync immediately, with zero risk and no
regression. The complex "contextual presync cursor" the other team proposed is **not needed**
for this bug and is risky in a consensus-adjacent path.

---

## Evidence

### Live fresh node (192.168.1.143, v9.26.2, fresh datadir)
```
blocks=0 headers=0 ibd=True
Pre-synchronizing blockheaders, height: 23740000 (~99.90%)
Pre-synchronizing blockheaders, height: 23760000 (~99.98%)
Pre-synchronizing blockheaders, height: 18220000 (~76.72%)   <-- reset
```
RPC shows **zero headers accepted** — presync never reaches the work threshold, so nothing is
committed to the block index.

### The numbers (measured against the synced mainnet node)
| Quantity | Value | As % of real tip |
|---|---|---|
| Real tip chainwork | `0x…1d10a64f…` | 100% |
| `nMinimumChainWork` (set 2026-06-26) | `0x…1cae290e…` | **98.68%** |
| What contextless pre-sync actually accumulates | measured | **~28%** |

Contextless/real work ratio measured over the last 500 / 2000 / 5000 blocks: **0.258 / 0.286 /
0.280**. Fresh sync requires `contextless% > nMinimumChainWork%` → **28% > 98.68% is
impossible.**

### Git history (the "why now")
```
47b9ea3481  2026-06-26  "release: refresh mainnet chain metadata"
-   consensus.nMinimumChainWork = uint256S("0x00");
+   consensus.nMinimumChainWork = uint256S("0x…1cae290ed41eb2efd4804c");
```
Mainnet `nMinimumChainWork` was **`0x00` for its entire prior history** (no work gate → fresh
sync always worked). v9.26.2 introduced the gate, computed from the **real** chainwork, which
the contextless pre-sync can't reach.

---

## Why the contextless work is only ~28% of real (validated in code)

Pre-sync sums work from a **dummy index** with no context
(`src/headerssync.cpp:208,244`):
```cpp
m_current_chain_work   += GetBlockProof(CBlockIndex(current));
m_redownload_chain_work += GetBlockProof(CBlockIndex(header));
```
`CBlockIndex(const CBlockHeader&)` leaves `nHeight = 0` and `pprev = nullptr`
(`src/chain.cpp:28`). DigiByte's `GetBlockProof()` is **context-sensitive** (`src/chain.cpp`):
- `nHeight >= workComputationChangeTarget` (real headers): geometric mean of
  `GetNextWorkRequired(block.pprev, …)` across all active algos, then **`<< 7` (×128)**.
- `nHeight == 0` (the dummy): falls into the legacy branch
  `GetBlockProofBase(nBits) * GetAlgoWorkFactor(0, algo)`, and
  `GetAlgoWorkFactor(0, …) == 1` for every algo. So the dummy returns only the block's own
  single-algo `nBits` work — **no `<<7`, no multi-algo averaging.**

That structural difference is why the dummy sum is ~28% of the real multi-algo chainwork. This
is inherited from upstream Bitcoin, where `GetBlockProof()` depends **only** on `nBits`, so the
contextless dummy equals the real work. In DigiByte it does not.

The pre-sync threshold for a fresh node is `nMinimumChainWork` (confirmed:
`GetAntiDoSWorkThreshold()` = `max(near_chaintip_work, MinimumChainWork())`, and for a fresh
node `near_chaintip_work = 0`, `src/net_processing.cpp:2890`).

---

## Audit of `FRESH_NODE_HEADERS_PRESYNC_FIX.md`

| Their claim | Verdict |
|---|---|
| `GetBlockProof()` is context-sensitive; presync uses a contextless dummy | ✅ **Correct** — validated in code |
| The dummy's `nHeight=0/pprev=null` yields wrong work for modern headers | ✅ **Correct** (it's the legacy branch, ~28% of real) |
| The presync abort at the tip height is the observed failure | ✅ **Correct** — reproduced on 192.168.1.143 |
| "Do **not** lower `nMinimumChainWork` as the primary fix" | ❌ **Wrong** — the over-tight `nMinimumChainWork` set on 2026-06-26 is the proximate root cause; correcting it is the right KISS fix |
| The fix must be a contextual `HeadersSyncWorkCursor` (temp CBlockIndex state) | ❌ **Over-engineered** for this bug — complex, risky, and unnecessary to restore fresh sync |

They diagnosed the *mechanism* correctly but inverted the *fix*: they treated a latent,
long-tolerated quirk (contextless presync) as the thing to rewrite, and dismissed the actual
trigger (a one-line metadata value set 4 days ago).

---

## The fix

### Recommended (KISS, zero-risk): revert `nMinimumChainWork` to `0x00`
`src/kernel/chainparams.cpp` (mainnet, ~line 191):
```cpp
// FRESH-SYNC FIX: revert the 2026-06-26 metadata refresh. DigiByte's headers pre-sync
// measures contextless work (~28% of real chainwork), so a real-chainwork-derived
// nMinimumChainWork is unreachable and breaks fresh sync. Mainnet ran with 0x00 for its
// entire history.
consensus.nMinimumChainWork = uint256S("0x00");
```
- **Works:** restores the exact behavior mainnet had for years (fresh sync succeeds).
- **No regression:** it is the historical value; nothing that worked breaks.
- **Anti-DoS:** unchanged from mainnet's entire history (which never had a presync work gate).
  Header pre-sync still uses commitment-based redownload + peer management.
- **Does not touch consensus, Groestl rules, the algolock, BIP9, Qt, or validation.**

Also verify the other networks in the same file (testnet `0x…01ad46be4862`, signet, regtest)
and revert/zero any whose `nMinimumChainWork` exceeds ~25% of that network's real tip work by
the same test.

### Optional — if a real anti-DoS floor is wanted (do NOT ship as the emergency fix)
A non-zero `nMinimumChainWork` is only safe if it is **below what contextless pre-sync can
reach** (~25% of real tip work, with margin). Two ways to get there:
1. **Pick a validated low value** (e.g. ≤15–20% of real tip work) and **prove it on a fresh
   node** before release. Risk: the exact whole-chain contextless total isn't trivially
   computable, so it must be tested, not guessed.
2. **Make pre-sync contextual (the other team's `HeadersSyncWorkCursor`)** so it measures real
   work; then `nMinimumChainWork` can be set from real chainwork the normal way. This is the
   architecturally complete fix but is **complex, touches the DoS-protection path, and carries
   real regression risk** — schedule it deliberately, not as the emergency patch.

**Recommendation:** ship the `0x00` revert now to unblock fresh nodes; treat the contextual
pre-sync as a separate, carefully-reviewed improvement only if a presync work gate is actually
desired.

---

## Validation plan for the fix

1. Build the patched binary; fresh mainnet datadir; `-debug=net`.
2. Connect to current v9.26.2 peers.
3. Confirm the log shows headers being accepted (no more
   `Pre-synchronizing … (99.98%) → … (76%)` reset loop).
4. `getblockchaininfo`: `headers` climbs above 0 and tracks `blocks`; `initialblockdownload`
   eventually false.
5. Confirm block download proceeds and the node reaches tip.
6. Confirm **no** `bad-algo` / `InvalidChainFound` while syncing the grandfathered Groestl
   history (independent of this fix; the algolock only activates at height 23,808,000).
7. Sanity: a synced node still follows the most-work chain (the `0x00` change does not affect
   chain selection).

---

## Bottom line

- **Root cause:** v9.26.2's 2026-06-26 `nMinimumChainWork` refresh (`0x00` → 98.68% of real
  tip) made the header pre-sync work gate unreachable, because DigiByte pre-sync measures
  contextless work (~28% of real).
- **KISS fix:** revert mainnet `nMinimumChainWork` to `0x00` (one line). Restores years-proven
  behavior, zero risk, no consensus impact.
- **The other team's contextual-cursor rewrite is correct in theory but the wrong call for this
  bug** — unnecessary, complex, and risky. Keep it on the shelf as an optional future
  enhancement if a presync work floor is ever wanted.
