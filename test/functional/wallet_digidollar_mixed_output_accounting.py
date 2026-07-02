#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for DD amount accounting when our output is not first."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class WalletDigiDollarMixedOutputAccountingTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        sender, first_recipient, second_recipient = self.nodes

        self.log.info("Activating DigiDollar and minting sender balance")
        self.generate(sender, 660)
        self.sync_all()
        for node in self.nodes:
            node.setmockoracleprice(500000)

        minted = sender.mintdigidollar(10000, 0)
        assert "txid" in minted
        self.generate(sender, 1)
        self.sync_all()
        assert_equal(sender.getdigidollarbalance()["total"], 10000)

        self.log.info("Sending to two recipients and checking the second output amount")
        first_address = first_recipient.getdigidollaraddress()
        second_address = second_recipient.getdigidollaraddress()
        sent = sender.sendmanydigidollar("", {
            first_address: 6000,
            second_address: 4000,
        })
        assert "txid" in sent
        self.sync_mempools()
        self.generate(sender, 1)
        self.sync_all()

        assert_equal(first_recipient.getdigidollarbalance()["total"], 6000)
        assert_equal(second_recipient.getdigidollarbalance()["total"], 4000)
        assert_equal(sender.getdigidollarbalance()["total"], 0)


if __name__ == "__main__":
    WalletDigiDollarMixedOutputAccountingTest().main()
