#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Pin listoracle RPC schema when a local oracle is running."""

import hashlib

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class DigiDollarListOracleSchemaTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("oracle_schema")
        wallet = node.get_wallet_rpc("oracle_schema")

        regtest_privkey = hashlib.sha256(b"digibyte_regtest_oracle_0").hexdigest()
        start_result = wallet.startoracle(0, regtest_privkey)
        assert_equal(start_result["success"], True)
        assert_equal(start_result["status"], "running")

        result = node.listoracle()
        assert_equal(result["running"], True)
        assert_equal(result["oracle_id"], 0)
        assert_equal(result["price_source"], "none")

        node.stoporacle(0)


if __name__ == "__main__":
    DigiDollarListOracleSchemaTest().main()
