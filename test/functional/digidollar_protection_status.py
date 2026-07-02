#!/usr/bin/env python3
# Copyright (c) 2024-2025 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that getprotectionstatus returns real system health (Bug #7/#9).

Verifies that getprotectionstatus computes live data from the same source
as getdigidollarstats, instead of returning hardcoded mock values.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class DigiDollarProtectionStatusTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Generate initial blocks to activate DigiDollar")
        self.generate(node, 200)

        self.log.info("Call getprotectionstatus and getdigidollarstats")
        prot = node.getprotectionstatus()
        stats = node.getdigidollarstats()

        self.log.info("Verify protection status has expected structure")
        assert "dca" in prot
        assert "err" in prot
        assert "volatility" in prot
        assert "overall" in prot

        self.log.info("Verify system_health matches between RPCs")
        assert_equal(prot["dca"]["system_health"], stats["health_percentage"])

        self.log.info("Verify DCA tier matches")
        assert_equal(prot["dca"]["tier"], stats["health_status"])

        self.log.info("Verify DCA multiplier matches")
        assert_equal(prot["dca"]["current_multiplier"], stats["dca_tier"]["multiplier"])

        self.log.info("Verify ERR current_ratio matches system health")
        assert_equal(prot["err"]["current_ratio"], stats["health_percentage"])

        self.log.info("Verify volatility is honest (0.0, not hardcoded 2.5)")
        assert_equal(prot["volatility"]["current_volatility"], 0.0)

        self.log.info("Verify values are not the old hardcoded mock (150)")
        # With no DD minted, health should be 0, not 150
        if stats["total_dd_supply"] == 0:
            assert_equal(prot["dca"]["system_health"], 0)
            self.log.info("No DD minted — health is 0 as expected (not hardcoded 150)")

        self.log.info("All protection status checks passed!")


if __name__ == "__main__":
    DigiDollarProtectionStatusTest().main()
