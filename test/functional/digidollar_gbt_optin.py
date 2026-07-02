#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""DigiDollar mint/redeem mining must require explicit GBT opt-in."""

from test_framework.blocktools import NORMAL_GBT_REQUEST_PARAMS
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


ACTIVATION_HEIGHT = 200
REGTEST_CONFIRMATION_WINDOW = 144
ORACLE_PRICE_MICRO_USD = 500_000
DD_AWARE_GBT_REQUEST_PARAMS = {"rules": ["segwit", "digidollar-oracle"]}


class DigiDollarGBTOptInTest(DigiByteTestFramework):
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
            status = node.getdeploymentinfo()["deployments"]["digidollar"]["bip9"]["status"]
            if status == "active":
                return
            current = node.getblockcount()
            remaining = REGTEST_CONFIRMATION_WINDOW - (current % REGTEST_CONFIRMATION_WINDOW)
            self.generate(node, remaining or REGTEST_CONFIRMATION_WINDOW)

    @staticmethod
    def template_txids(template):
        return {tx["txid"] for tx in template["transactions"]}

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Activate DigiDollar and publish a fresh oracle quote")
        self.generate(node, 150)
        self.activate_digidollar()
        assert_equal(node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)["price_micro_usd"], ORACLE_PRICE_MICRO_USD)

        self.log.info("DD-aware GBT carries oracle commitments even for plain blocks")
        aware_plain = node.getblocktemplate(DD_AWARE_GBT_REQUEST_PARAMS)
        assert "default_oracle_commitment" in aware_plain
        assert "!digidollar-oracle" in aware_plain["rules"]

        self.log.info("Create a DD mint that requires an oracle-priced block")
        mint = node.mintdigidollar(100_000, 0)
        dd_txid = mint["txid"]

        self.log.info("Legacy GBT stays safe for non-DigiDollar-aware miners")
        legacy_template = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        assert dd_txid not in self.template_txids(legacy_template)
        assert "default_oracle_commitment" not in legacy_template
        assert "coinbasetxn" not in legacy_template
        assert "!digidollar-oracle" not in legacy_template["rules"]

        self.log.info("DigiDollar-aware GBT includes mint/redeem work with oracle data")
        aware_template = node.getblocktemplate(DD_AWARE_GBT_REQUEST_PARAMS)
        assert dd_txid in self.template_txids(aware_template)
        assert "default_oracle_commitment" in aware_template
        assert "!digidollar-oracle" in aware_template["rules"]

        self.log.info("Legacy and aware GBT templates are cached separately")
        legacy_again = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        assert dd_txid not in self.template_txids(legacy_again)
        assert "default_oracle_commitment" not in legacy_again


if __name__ == "__main__":
    DigiDollarGBTOptInTest().main()
