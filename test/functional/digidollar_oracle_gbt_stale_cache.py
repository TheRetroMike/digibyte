#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""getblocktemplate must not keep serving stale oracle-bearing templates."""

import time

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


ACTIVATION_HEIGHT = 200
REGTEST_CONFIRMATION_WINDOW = 144
ORACLE_MAX_AGE_SECONDS = 3600
ORACLE_PRICE_MICRO_USD = 500_000
DD_AWARE_GBT_REQUEST_PARAMS = {"rules": ["segwit", "digidollar-oracle"]}


class DigiDollarOracleGBTStaleCacheTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            f"-digidollaractivationheight={ACTIVATION_HEIGHT}",
            "-dandelion=0",
            "-txindex=1",
        ]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def activate_digidollar(self):
        node = self.nodes[0]
        while True:
            info = node.getdeploymentinfo()
            status = info["deployments"]["digidollar"]["bip9"]["status"]
            if status == "active":
                return
            current = node.getblockcount()
            remaining = REGTEST_CONFIRMATION_WINDOW - (current % REGTEST_CONFIRMATION_WINDOW)
            if remaining == 0:
                remaining = REGTEST_CONFIRMATION_WINDOW
            node.generate(remaining)

    @staticmethod
    def template_txids(template):
        return {tx["txid"] for tx in template["transactions"]}

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Activate DigiDollar and publish a fresh oracle quote")
        node.generate(150)
        self.activate_digidollar()
        assert_equal(node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)["price_micro_usd"], ORACLE_PRICE_MICRO_USD)

        self.log.info("Create a DD mint so the first GBT has a required oracle bundle")
        mint = node.mintdigidollar(100_000, 0)
        dd_txid = mint["txid"]

        fresh_template = node.getblocktemplate(DD_AWARE_GBT_REQUEST_PARAMS)
        assert dd_txid in self.template_txids(fresh_template)
        assert "default_oracle_commitment" in fresh_template
        assert "coinbasetxn" not in fresh_template

        self.log.info("Age the cached template past the oracle freshness window without changing tip or mempool")
        future_time = int(time.time()) + ORACLE_MAX_AGE_SECONDS + 120
        node.enablemockoracle(False)
        node.setmocktime(future_time)

        stale_template = node.getblocktemplate(DD_AWARE_GBT_REQUEST_PARAMS)
        assert_equal(stale_template["previousblockhash"], fresh_template["previousblockhash"])
        assert_greater_than(stale_template["curtime"] - fresh_template["curtime"], ORACLE_MAX_AGE_SECONDS)

        assert dd_txid not in self.template_txids(stale_template), (
            "GBT must rebuild stale oracle-bearing templates and strip price-dependent "
            "DD transactions when no fresh bundle is available"
        )
        assert "default_oracle_commitment" not in stale_template, (
            "GBT must not advertise a stale oracle commitment after template time advances"
        )
        assert "coinbasetxn" not in stale_template, (
            "GBT must not return a prebuilt coinbase that still carries stale oracle data"
        )


if __name__ == "__main__":
    DigiDollarOracleGBTStaleCacheTest().main()
