#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 21 (DD-FA-FUNC-034): DoS / resource exhaustion bound checks.

This test pins the bounded-result contract for the DigiDollar/oracle RPCs
that walk wallet- or chain-sized collections without an existing per-call
limit. The Wave 21 audit charter (Agent C) requires that:

* `listdigidollarpositions` accept a paging window so a wallet with a
  large keypool / many redeemed mints cannot blow up RPC clients with an
  unbounded result body.
* `listdigidollartxs` must continue to enforce the documented
  ``count <= 1000`` clamp and reject negative ``skip`` values.
* `getalloracleprices` must continue to clamp ``blocks`` to the
  documented [1, 1000] window even for adversarial inputs.

Pre-fix this script fails at the first ``listdigidollarpositions``
named-arg paging call because the RPC schema has no ``count`` parameter.
The Wave 21 fix adds ``count`` / ``skip`` paging to
``listdigidollarpositions`` while preserving the documented default
behaviour (return all matching positions when paging is omitted).
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class DigiDollarWave21DoSPagingTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-digidollar=1",
            "-txindex=1",
            "-mocktime=0",
            "-dandelion=0",
        ]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Preparing active DigiDollar regtest chain")
        self.generate(node, 110)
        node.setmockoracleprice(500000)

        self.log.info("Minting positions across distinct tiers/amounts")
        # Use distinct tier/amount pairs so we get real position diversity
        # without piling thousands of mints into a single regtest run.
        mint_specs = [
            (10000, 0),  # $100  tier 0 (1h)
            (15000, 0),
            (20000, 0),
            (25000, 0),
            (30000, 1),  # $300  tier 1 (30d)
            (35000, 1),
            (40000, 1),
            (45000, 2),  # $450  tier 2 (90d)
            (50000, 2),
        ]
        position_ids = []
        for amount, tier in mint_specs:
            result = node.mintdigidollar(amount, tier)
            position_ids.append(result["position_id"])
            self.generate(node, 1)
        assert_equal(len(position_ids), len(mint_specs))

        self.log.info("Bound check: listdigidollarpositions accepts a paging window")
        # Documented default behaviour (no paging args) must keep returning
        # every active position so existing scripts remain compatible.
        all_active = node.listdigidollarpositions()
        assert_equal(len(all_active), len(mint_specs))

        # Wave 21 contract: positional `count` (5th arg) clamps the result
        # body length so an oversize wallet cannot DoS the RPC client with
        # an unbounded array.
        first_three = node.listdigidollarpositions(False, None, 0, 3, 0)
        assert_equal(len(first_three), 3)

        # Named paging (count + skip) must work identically to positional.
        next_three = node.listdigidollarpositions(
            active_only=False,
            count=3,
            skip=3,
        )
        assert_equal(len(next_three), 3)

        # Pages must not overlap.
        first_ids = {p["position_id"] for p in first_three}
        next_ids = {p["position_id"] for p in next_three}
        assert_equal(len(first_ids & next_ids), 0)

        # Skipping past the end returns an empty list rather than failing.
        empty = node.listdigidollarpositions(False, None, 0, 10, 100)
        assert_equal(empty, [])

        # Wave 21 contract: the count clamp must reject obviously hostile
        # values rather than silently truncating to a different cap.
        assert_raises_rpc_error(
            -8,
            "Count must be between 0 and 1000",
            node.listdigidollarpositions,
            False, None, 0, -1, 0,
        )
        assert_raises_rpc_error(
            -8,
            "Count must be between 0 and 1000",
            node.listdigidollarpositions,
            False, None, 0, 1001, 0,
        )
        assert_raises_rpc_error(
            -8,
            "Skip must be non-negative",
            node.listdigidollarpositions,
            False, None, 0, 10, -1,
        )

        self.log.info("listdigidollartxs paging clamp / negative-skip regression")
        # listdigidollartxs has carried this clamp for several waves; pin
        # it here so the Wave 21 RPC paging contract stays uniform across
        # the wallet-context list RPCs.
        assert_raises_rpc_error(
            -8,
            "Count must be between 0 and 1000",
            node.listdigidollartxs,
            1001, 0,
        )
        assert_raises_rpc_error(
            -8,
            "Skip must be non-negative",
            node.listdigidollartxs,
            10, -1,
        )
        # Non-paging defaults still return a list of dicts.
        default_txs = node.listdigidollartxs()
        assert isinstance(default_txs, list)

        self.log.info("getalloracleprices clamps `blocks` to documented [1, 1000] window")
        # The RPC silently clamps adversarial inputs (no JSON-RPC error)
        # so the response time stays bounded.
        clamped_low = node.getalloracleprices(0)
        assert "oracles" in clamped_low
        clamped_high = node.getalloracleprices(10_000)
        assert "oracles" in clamped_high
        # Per CLAUDE.md the regtest oracle roster is exactly 7 slots.
        assert_equal(len(clamped_high["oracles"]), 7)


if __name__ == "__main__":
    DigiDollarWave21DoSPagingTest().main()
