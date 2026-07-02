#!/usr/bin/env python3
"""Test Bug #17 fix: validateddaddress ismine field.

Verifies that validateddaddress correctly reports ismine=true for
wallet-owned DD addresses and ismine=false for addresses belonging
to other wallets.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
)


class DigiDollarBug17ValidateDDAddressTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Generate blocks past maturity and activate DigiDollar...")
        self.nodes[0].generate(110)
        self.sync_all()

        # Set oracle price so DD operations work
        self.nodes[0].setmockoracleprice(500000)
        self.nodes[0].generate(1)
        self.sync_all()

        self.log.info("Get a DD address from node 0...")
        dd_addr = self.nodes[0].getdigidollaraddress()
        self.log.info(f"DD address: {dd_addr}")

        self.log.info("Test 1: validateddaddress on own address → ismine=true")
        result0 = self.nodes[0].validateddaddress(dd_addr)
        assert_equal(result0["isvalid"], True)
        assert_equal(result0["ismine"], True)

        self.log.info("Test 2: validateddaddress on node 1 (not owner) → ismine=false")
        result1 = self.nodes[1].validateddaddress(dd_addr)
        assert_equal(result1["isvalid"], True)
        assert_equal(result1["ismine"], False)

        self.log.info("Test 3: invalid address → isvalid=false")
        result_invalid = self.nodes[0].validateddaddress("notanaddress")
        assert_equal(result_invalid["isvalid"], False)

        self.log.info("Bug #17 fix verified: validateddaddress ismine works correctly!")


if __name__ == "__main__":
    DigiDollarBug17ValidateDDAddressTest().main()
