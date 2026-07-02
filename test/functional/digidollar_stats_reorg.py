#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Regression test for DD-RH-068: digidollarstatsindex rewind must restore
vault metadata needed to account for a redeem after reorg.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class DigiDollarStatsReorgTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex=1", "-dandelion=0", "-digidollarstatsindex=1"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def assert_stats(self, supply, positions):
        stats = self.nodes[0].getdigidollarstats()
        assert_equal(stats["total_dd_supply"], supply)
        assert_equal(stats["active_positions"], positions)

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining spendable funds")
        node.generate(200)
        node.setmockoracleprice(500000)

        self.log.info("Minting and confirming a tier-0 DigiDollar vault")
        mint = node.mintdigidollar(100000, 0)
        position_id = mint["position_id"]
        node.generate(1)
        self.assert_stats(100000, 1)

        position = next(p for p in node.listdigidollarpositions(False) if p["position_id"] == position_id)
        blocks_needed = max(0, position["unlock_height"] - node.getblockcount())
        if blocks_needed:
            node.generate(blocks_needed)
        self.assert_stats(100000, 1)

        self.log.info("Redeeming the vault on branch A")
        redeem = node.redeemdigidollar(position_id, 100000)
        redeem_txid = redeem["txid"]
        redeem_block_a = node.generate(1)[0]
        assert redeem_txid in node.getblock(redeem_block_a)["tx"]
        self.assert_stats(0, 0)

        self.log.info("Rewinding branch A and mining the same redeem again on branch B")
        node.invalidateblock(redeem_block_a)
        assert redeem_txid in node.getrawmempool()
        self.assert_stats(100000, 1)

        redeem_block_b = node.generatetoaddress(1, node.getnewaddress(), invalid_call=False)[0]
        assert redeem_txid in node.getblock(redeem_block_b)["tx"]
        self.assert_stats(0, 0)


if __name__ == "__main__":
    DigiDollarStatsReorgTest().main()
