#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test pending DigiDollar mint position reporting.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class DigiDollarPendingPositionStatusTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # -walletbroadcast=0 reproduces the RC44 field-report class: mintdigidollar()
        # creates a wallet-local 0-conf transaction that is not accepted to the
        # node mempool. Reconciliation must not treat that missing mempool
        # output as a spent/redeemed or missing vault.
        self.extra_args = [["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0", "-walletbroadcast=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.generate(self.nodes[0], 110)
        result = self.nodes[0].setmockoracleprice(500000)
        assert_equal(result["price_micro_usd"], 500000)

        result = self.nodes[0].mintdigidollar(1000, 0)
        txid = result["txid"]
        assert txid not in self.nodes[0].getrawmempool(), "walletbroadcast=0 mint should remain wallet-local"

        # Regression guard: listdigidollarpositions() reconciles wallet state on
        # read. A wallet-local unconfirmed mint's collateral outpoint is not in
        # the confirmed UTXO set or mempool view, but the position is still
        # active/pending — not redeemed/inactive. The default active_only=True
        # path must keep it.
        active_positions = self.nodes[0].listdigidollarpositions()
        active_matching = [pos for pos in active_positions if pos["position_id"] == txid]
        assert_equal(len(active_matching), 1)

        positions = self.nodes[0].listdigidollarpositions(False)
        matching = [pos for pos in positions if pos["position_id"] == txid]
        assert_equal(len(matching), 1)

        for position in (active_matching[0], matching[0]):
            assert_equal(position["confirmations"], 0)
            assert_equal(position["status"], "pending")
            assert_equal(position["can_redeem"], False)


if __name__ == "__main__":
    DigiDollarPendingPositionStatusTest().main()
