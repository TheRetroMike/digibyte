#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test oracle RPC freshness when the last on-chain bundle expires by wall time."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal

import time

ORACLE_MAX_AGE_SECONDS = 3600


class DigiDollarOracleRPCStalenessTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Generate past DigiDollar/oracle activation")
        self.generate(node, 650)
        node.enablemockoracle(False)

        base_time = int(time.time()) + 60
        node.setmocktime(base_time)

        self.log.info("Publish a fresh regtest MuSig2 oracle bundle")
        result = node.setmockoracleprice(6500)
        assert_equal(result["price_micro_usd"], 6500)
        node.mintdigidollar(1000, 0)
        self.generate(node, 1)

        fresh = node.getoracleprice()
        assert_equal(fresh["price_micro_usd"], 6500)
        assert_equal(fresh["is_stale"], False)

        self.log.info("Advance wall time past the oracle max age without mining")
        node.setmocktime(base_time + ORACLE_MAX_AGE_SECONDS + 1)

        price = node.getoracleprice()
        assert_equal(price["price_micro_usd"], 0)
        assert_equal(price["is_stale"], True)
        assert_equal(price["oracle_count"], 0)

        all_prices = node.getalloracleprices(20)
        assert_equal(all_prices["consensus_price_micro_usd"], 0)
        assert_equal(all_prices["oracle_count"], 0)
        for oracle in all_prices["oracles"]:
            assert_equal(oracle["price_micro_usd"], 0)
            assert_equal(oracle["price_source"], "none")
            assert_equal(oracle["status"], "no_data")

        oracles = node.getoracles(False, 20)
        for oracle in oracles:
            assert_equal(oracle["last_price_micro_usd"], 0)
            assert_equal(oracle["price_source"], "none")
            assert_equal(oracle["status"], "no_data")

        self.log.info("Stale oracle must not block transfer-only DigiDollar sends")
        balance_before = node.getdigidollarbalance()["total"]
        receive_address = node.getdigidollaraddress()
        send_result = node.senddigidollar(receive_address, 100)
        assert_equal(len(send_result["txid"]), 64)
        self.generate(node, 1)
        assert_equal(node.getdigidollarbalance()["total"], balance_before)

        node.setmocktime(0)


if __name__ == "__main__":
    DigiDollarOracleRPCStalenessTest().main()
