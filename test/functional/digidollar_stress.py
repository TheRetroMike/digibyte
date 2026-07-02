#!/usr/bin/env python3
# Copyright (c) 2025-2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""DigiDollar stress smoke test.

This test intentionally stays on current, registered DigiDollar RPCs. It is not
a synthetic RED-phase placeholder: every operation below must either complete or
raise a real test failure.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
)


ORACLE_PRICE_MICRO_USD = 500000


class DigiDollarStressTest(DigiByteTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [[
            "-digidollar=1",
            "-txindex=1",
            "-mocktime=0",
            "-dandelion=0",
        ] for _ in range(self.num_nodes)]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Preparing active DigiDollar regtest chain")
        self.generate(self.nodes[0], 120)
        self.sync_all()

        for node in self.nodes:
            result = node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
            assert_equal(result["price_micro_usd"], ORACLE_PRICE_MICRO_USD)

        self.exercise_protection_rpcs()
        self.exercise_mint_send_volume()
        self.exercise_oracle_updates_and_network_reconnect()
        self.exercise_short_stability_loop()

    def exercise_protection_rpcs(self):
        self.log.info("Checking protection and oracle status RPCs under repeated load")
        node = self.nodes[0]

        for _ in range(5):
            stats = node.getdigidollarstats()
            assert "health_percentage" in stats
            assert "oracle_available" in stats

            protection = node.getprotectionstatus()
            assert "oracle" in protection
            assert "dca" in protection
            assert "err" in protection

            dca = node.getdcamultiplier()
            assert_greater_than_or_equal(float(dca["multiplier"]), 1.0)

            oracle = node.getoracleprice()
            assert_equal(oracle["price_micro_usd"], ORACLE_PRICE_MICRO_USD)

    def exercise_mint_send_volume(self):
        self.log.info("Minting several DD positions and sending confirmed DD outputs")
        miner = self.nodes[0]
        recipients = [self.nodes[1], self.nodes[2]]

        mint_specs = [
            (2000, 4),
            (2500, 4),
            (3000, 5),
            (3500, 5),
        ]
        expected_sender_balance = 0
        for amount_cents, tier in mint_specs:
            result = miner.mintdigidollar(amount_cents, tier)
            assert "txid" in result
            assert_equal(result["dd_minted"], amount_cents)
            expected_sender_balance += amount_cents
            self.generate(miner, 1)
            self.sync_all()

        sender_balance = miner.getdigidollarbalance()["total"]
        assert_equal(sender_balance, expected_sender_balance)

        sends = [
            (recipients[0], 250),
            (recipients[1], 300),
            (recipients[0], 150),
            (recipients[1], 100),
        ]
        sent_total = 0
        received_totals = {1: 0, 2: 0}

        for recipient, amount_cents in sends:
            address = recipient.getdigidollaraddress()
            send_result = miner.senddigidollar(address, amount_cents)
            assert "txid" in send_result
            assert_equal(send_result["amount"], amount_cents)
            sent_total += amount_cents
            received_totals[self.nodes.index(recipient)] += amount_cents
            self.generate(miner, 1)
            self.sync_all()

        assert_equal(miner.getdigidollarbalance()["total"], expected_sender_balance - sent_total)
        assert_equal(self.nodes[1].getdigidollarbalance()["total"], received_totals[1])
        assert_equal(self.nodes[2].getdigidollarbalance()["total"], received_totals[2])

    def exercise_oracle_updates_and_network_reconnect(self):
        self.log.info("Updating mock oracle prices and reconnecting peers")
        prices = [450000, 550000, ORACLE_PRICE_MICRO_USD]
        for price in prices:
            for node in self.nodes:
                result = node.setmockoracleprice(price)
                assert_equal(result["price_micro_usd"], price)
            self.generate(self.nodes[0], 1)
            self.sync_all()
            assert_equal(self.nodes[0].getoracleprice()["price_micro_usd"], price)

        self.disconnect_nodes(0, 1)
        self.connect_nodes(0, 1)
        self.sync_all()
        heights = [node.getblockcount() for node in self.nodes]
        assert_equal(len(set(heights)), 1)

    def exercise_short_stability_loop(self):
        self.log.info("Polling hot RPCs after stress operations")
        node = self.nodes[0]

        for _ in range(10):
            chain_info = node.getblockchaininfo()
            assert_greater_than(chain_info["blocks"], 0)

            balance = node.getdigidollarbalance()
            assert_greater_than_or_equal(balance["total"], 0)

            stats = node.getdigidollarstats()
            assert_greater_than_or_equal(stats["total_dd_supply"], 0)


if __name__ == '__main__':
    DigiDollarStressTest().main()
