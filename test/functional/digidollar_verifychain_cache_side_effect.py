#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for verifychain DD/oracle cache side effects.

`verifychain` checklevel 3 performs a memory-only DisconnectBlock of recent
tip blocks. That verification path must not mutate live DigiDollar health,
volatility, or oracle caches because RPC verification is a read-only probe of
the active chain.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


REGTEST_CONFIRMATION_WINDOW = 144
ACTIVATION_HEIGHT = 432
BASELINE_PRICE = 500000
TIP_PRICE = 777777


class DigiDollarVerifychainCacheSideEffectTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            f"-digidollaractivationheight={ACTIVATION_HEIGHT}",
            "-dandelion=0",
            "-txindex=1",
            "-digidollarstatsindex=1",
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
            if remaining == 0:
                remaining = REGTEST_CONFIRMATION_WINDOW
            node.generate(remaining)

    def assert_cached_oracle_price(self, expected_price):
        price = self.nodes[0].getoracleprice()
        assert_equal(price["price_micro_usd"], expected_price)
        return price

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Activate DigiDollar and create a DD block with baseline price")
        node.generate(150)
        self.activate_digidollar()

        node.setmockoracleprice(BASELINE_PRICE)
        mint = node.mintdigidollar(100000, 0)
        mint_hash = node.generate(1)[0]
        assert mint["txid"] in node.getblock(mint_hash)["tx"]

        self.log.info("Mine a second DD block at a distinct tip oracle price")
        node.setmockoracleprice(TIP_PRICE)
        transfer = node.senddigidollar(node.getdigidollaraddress(), 10000)
        transfer_hash = node.generate(1)[0]
        assert transfer["txid"] in node.getblock(transfer_hash)["tx"]

        self.log.info("Disable mock fallback so getoracleprice reads the on-chain cache")
        node.enablemockoracle(False)
        before = self.assert_cached_oracle_price(TIP_PRICE)
        stats_before = node.getdigidollarstats()
        assert_equal(stats_before["oracle_price_micro_usd"], TIP_PRICE)

        for checklevel, nblocks in ((3, 1), (4, 2)):
            self.log.info(
                "verifychain level %d over %d block(s) must be read-only for live DD/oracle caches",
                checklevel,
                nblocks,
            )
            assert_equal(node.verifychain(checklevel, nblocks), True)
            after = self.assert_cached_oracle_price(TIP_PRICE)
            assert_equal(after["last_update_height"], before["last_update_height"])
            assert_equal(after["last_update_time"], before["last_update_time"])

        stats_after = node.getdigidollarstats()
        assert_equal(stats_after["oracle_price_micro_usd"], stats_before["oracle_price_micro_usd"])


if __name__ == "__main__":
    DigiDollarVerifychainCacheSideEffectTest().main()
