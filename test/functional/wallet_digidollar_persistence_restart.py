#!/usr/bin/env python3
# Copyright (c) 2024-2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test DigiDollar position persistence across wallet restart.

Earlier versions of this test silently exited on every run because they
passed `mintdigidollar(100, 365)` -- the second argument is a tier in
0..9, not a day count, so the mint always errored and the test bailed
out before any persistence assertion ran. This rewrite:

- Mints a real tier-0 DD position with the correct cents/tier units.
- Confirms the mint, asserts wallet sees it, then `stop`/`start_node`.
- Re-asserts that `listdigidollarpositions` and `getdigidollarbalance`
  match exactly across the restart.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


ORACLE_PRICE_MICRO_USD = 500000
MINT_AMOUNT_CENTS = 100000  # $1000.00


class DigiDollarPersistenceRestartTest(DigiByteTestFramework):
    def add_options(self, parser):
        # DigiDollar mint requires HD descriptor wallet support; legacy
        # wallets are explicitly unsupported in V1.
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-digidollar=1",
            "-txindex=1",
            "-dandelion=0",
        ]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        # DigiDollar mint requires an HD descriptor wallet (V1 does not
        # support legacy BDB wallets for DD owner-key derivation).
        self.skip_if_no_sqlite()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining funds and minting an active DD position")
        self.generate(node, 200)
        result = node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        assert_equal(result["price_micro_usd"], ORACLE_PRICE_MICRO_USD)

        mint = node.mintdigidollar(MINT_AMOUNT_CENTS, 0)  # tier 0 (1h)
        position_id = mint["position_id"]
        self.generate(node, 1)

        positions_before = node.listdigidollarpositions()
        assert_equal(len(positions_before), 1)
        assert_equal(positions_before[0]["position_id"], position_id)

        balance_before = node.getdigidollarbalance()
        assert_equal(balance_before["total"], MINT_AMOUNT_CENTS)

        self.log.info("Stopping and restarting node")
        self.stop_node(0)
        self.start_node(0, extra_args=self.extra_args[0])
        node = self.nodes[0]
        node.syncwithvalidationinterfacequeue()

        positions_after = node.listdigidollarpositions()
        assert_equal(len(positions_after), 1)
        assert_equal(positions_after[0]["position_id"], position_id)

        balance_after = node.getdigidollarbalance()
        assert_equal(balance_after["total"], balance_before["total"])

        self.log.info("DigiDollar position persisted across wallet restart")


if __name__ == "__main__":
    DigiDollarPersistenceRestartTest().main()
