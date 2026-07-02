# DigiDollar Protection Functional Test Scenarios

This is a plain-English summary of the DigiDollar functional tests that exercise DCA, ERR, volatility protection, oracle availability, and related safety behavior. These are functional tests, meaning they run real test nodes and RPC flows, not just isolated math functions.

## Short Summary

The current functional suite has **20 unique scripts** that touch DCA, ERR, or volatility protection. Broken down by topic, there are **12 DCA-related functional scripts**, **10 ERR-related functional scripts**, and **12 volatility-related functional scripts**. Some scripts test more than one protection system, so these categories overlap.

The main goal is simple: if DigiDollar becomes stressed by price movement, oracle outage, restart, reorg, or abnormal user behavior, the system should either require more collateral, block new risky minting, keep redemptions safe, or fail closed until oracle data is valid again.

## DCA: Dynamic Collateral Adjustment

DCA is tested as the system that raises collateral requirements when system health gets worse. The tests verify that DCA never makes minting cheaper during stress.

Functional scenarios tested:

- **Normal healthy system:** DCA returns a `1.0x` multiplier when health is healthy.
- **Warning health:** health from `120%` to `149%` returns a `1.25x` multiplier.
- **Critical health:** health from `110%` to `119%` returns a `1.5x` multiplier.
- **Emergency health:** health from `0%` to `109%` returns a `2.0x` multiplier.
- **Boundary values:** exact boundary values like `150`, `149`, `120`, `119`, `110`, `109`, `100`, `99`, and `0` are tested so off-by-one mistakes do not silently change collateral rules.
- **Extreme health values:** very low and very high health values are tested to make sure the multiplier stays bounded between `1.0x` and `2.0x`.
- **Custom health input:** `getdcamultiplier` is tested with explicit health values so wallets, dashboards, and RPC users see the expected tier and multiplier.
- **Collateral estimate impact:** `estimatecollateral` / collateral requirement RPC tests verify the returned `effective_ratio` equals the base lock-tier ratio multiplied by the DCA multiplier.
- **Stress pricing:** tests lower the oracle price to simulate collateral stress and verify DCA stays valid, increases collateral when required, or blocks unsafe activity.
- **Recovery path:** tests improve the price step by step and verify the DCA multiplier moves back toward `1.0x` when the system recovers.

In plain terms: the functional tests prove that DCA acts like a safety brake. If system health falls, new mints require more collateral instead of less.

## ERR: Emergency Redemption Ratio

ERR is tested as the emergency mode for unhealthy system conditions. The important behavior is that ERR must block unsafe new minting and keep redemption behavior controlled when system health falls too far.

Functional scenarios tested:

- **ERR status reporting:** `getprotectionstatus` must include ERR fields such as `active`, `threshold`, `current_ratio`, and `status`.
- **ERR follows real system health:** ERR current ratio is checked against the same health data exposed by `getdigidollarstats`.
- **No false ERR with no oracle:** when the oracle is unavailable, the system reports `oracle_unavailable`, not fake ERR. Minting is restricted for the correct reason.
- **Gradual price decline:** tests lower price over several steps to simulate a system moving toward emergency conditions.
- **ERR activation detection:** when health crosses emergency conditions, protection status must show ERR active.
- **Mint blocking during ERR:** when true system health is below `100%`, minting must fail with an emergency-state error.
- **Emergency redemption path:** redemption behavior is tested under emergency conditions so the system does not pretend everything is normal during a crisis.
- **Exact redemption rules still apply:** redemption tests also check that partial or excessive redemption attempts are rejected.
- **Recovery check:** after price improves, tests verify whether ERR remains active or clears according to the current protection state.
- **Stress redemption behavior:** tests attempt rapid or large redemptions under stress and verify the system handles or rejects them safely.

In plain terms: ERR is tested as the last-resort safety mode. If the system is unhealthy, new minting should stop, and redemption logic should remain strict and predictable.

## Volatility Protection

Volatility protection is tested because people are rightly worried about fast price moves. The tests try rapid price increases, rapid price decreases, and oscillating prices to make sure the system does not blindly mint through unstable oracle conditions.

Functional scenarios tested:

- **Rapid price increase:** price moves from `50000` to `75000` to `100000` micro-USD.
- **Rapid price decrease:** price moves from `50000` to `37500` to `25000` micro-USD.
- **Oscillating market:** price moves from `50000` to `75000` to `40000` to `60000`.
- **Volatility status reporting:** `getprotectionstatus` must report `protection_active`, `current_volatility`, `protection_threshold`, and `minting_restricted`.
- **No hardcoded volatility:** tests verify volatility is not stuck at an old fake value like `2.5`.
- **Mint restriction or safer requirements:** if volatility crosses the threshold, the system may restrict minting or require safer collateral behavior.
- **Blocked blocks are acceptable under protection:** when block generation or mint-related operations are blocked during extreme volatility, the tests treat that as valid protection behavior.
- **Stable-price recovery:** after restoring stable price data, tests verify volatility protection can return to normal.
- **Read-only verification safety:** `verifychain` tests make sure verification does not mutate live health, volatility, or oracle caches.

In plain terms: volatility tests try to simulate fast market movement. The expected behavior is not "always allow minting." The expected behavior is "stay safe, restrict risky minting when needed, and do not let fake or stale volatility data drive decisions."

## Oracle Safety And Fail-Closed Behavior

DigiDollar depends on oracle price data, so the functional tests also check what happens when oracle data is missing, stale, or unavailable.

Functional scenarios tested:

- **Oracle unavailable is separate from ERR:** no oracle data should not be mislabeled as emergency redemption mode.
- **Minting restricted without oracle:** when the oracle is unavailable, minting is restricted with reason `oracle_unavailable`.
- **Protection status shows oracle state:** `getprotectionstatus` includes an `oracle` section showing availability and mint restriction reason.
- **Oracle price sanity:** tests check oracle price RPC values, sub-cent prices, and non-hardcoded volatility.
- **Stale or missing oracle bundle:** miner/mempool parity tests verify DigiDollar mint transactions are not mined unless the block has the required oracle bundle.
- **Invalid block rejection:** a block that tries to include a DigiDollar mint without the required oracle bundle is rejected with an oracle-missing error.

In plain terms: if oracle data is missing or not safely committed into the block, DigiDollar mint/redeem behavior fails closed. It should not guess.

## Restart And Fork-Risk Scenarios

One of the most important functional tests is the restart consensus test. This test exists because a node restart must not make one node think the system is healthy while another node thinks it is unhealthy.

Functional scenario tested:

- The test creates real on-chain DigiDollar supply at a healthy price.
- It then crashes the price low enough to push system health below `100%`.
- It waits long enough that volatility freeze is no longer the reason minting is blocked.
- It confirms ERR blocks minting before restart.
- It restarts the node without reindex.
- It confirms ERR still blocks minting after restart.

This matters because the old failure mode would have been dangerous: a restarted node could forget total DigiDollar supply, think health was artificially high, and allow minting that a continuously running node would reject. The functional test verifies that health is reconstructed after restart, so DCA/ERR decisions stay consistent.

## What This Means For Volatility Concerns

The functional tests do not assume perfect market conditions. They deliberately test price crashes, price spikes, oscillating prices, oracle unavailability, stale/missing oracle bundle cases, emergency health, recovery, and restart behavior.

The important safety principle is:

**When the system is uncertain or stressed, DigiDollar should become harder or impossible to mint, not easier.**

That is what the DCA, ERR, volatility, oracle, and restart functional scenarios are designed to prove.

## Main Functional Test Files Behind This Summary

- `test/functional/digidollar_rpc_dca.py`
- `test/functional/digidollar_protection.py`
- `test/functional/digidollar_rpc_protection.py`
- `test/functional/digidollar_protection_status.py`
- `test/functional/digidollar_health_restart_consensus.py`
- `test/functional/digidollar_rpc_estimate.py`
- `test/functional/digidollar_redeem.py`
- `test/functional/digidollar_transactions.py`
- `test/functional/digidollar_stress.py`
- `test/functional/digidollar_mempool_miner_parity.py`
- `test/functional/digidollar_oracle_price.py`
- `test/functional/digidollar_verifychain_cache_side_effect.py`
