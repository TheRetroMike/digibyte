#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test DigiDollar wallet balance after a confirmed mint block is disconnected.

A disconnected mint that remains valid under the current oracle quote returns
to the mempool. The wallet must drop confirmed DD balance while still reporting
the pending DigiDollar output as unconfirmed.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class WalletDigiDollarMintReorgTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex=1"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining spendable funds")
        node.generate(200)
        node.setmockoracleprice(500000)

        self.log.info("Minting and confirming DigiDollar")
        mint = node.mintdigidollar(100000, 0)
        mint_txid = mint["txid"]
        mint_block = node.generate(1)[0]
        node.syncwithvalidationinterfacequeue()

        balance = node.getdigidollarbalance()
        assert_equal(balance["confirmed"], 100000)
        assert_equal(balance["total"], 100000)

        self.log.info("Disconnecting the mint block")
        node.invalidateblock(mint_block)
        node.syncwithvalidationinterfacequeue()

        assert mint_txid in node.getrawmempool()

        confirmed_only = node.getdigidollarbalance()
        assert_equal(confirmed_only["confirmed"], 0)
        assert_equal(confirmed_only["unconfirmed"], 0)
        assert_equal(confirmed_only["total"], 0)

        balance = node.getdigidollarbalance("", 0)
        assert_equal(balance["confirmed"], 0)
        assert_equal(balance["unconfirmed"], 100000)
        assert_equal(balance["total"], 100000)


if __name__ == "__main__":
    WalletDigiDollarMintReorgTest().main()
