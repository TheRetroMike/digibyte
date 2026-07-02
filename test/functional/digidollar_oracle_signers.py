#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test getoraclesigners exposes decoded MuSig2 bundle participants.

Oracle bundle validation already decodes the v0x03 participation bitmap to
verify which oracle IDs signed the aggregate signature. Operators need the same
information through RPC so wallets and monitoring tools can show the actual
bundle signers, not only the selected epoch roster or aggregate counts.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
)


class DigiDollarOracleSignersTest(DigiByteTestFramework):
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

        self.log.info("Activate DigiDollar/oracle on regtest")
        self.generate(node, 650)

        self.log.info("Mine a regtest MuSig2 oracle bundle")
        result = node.setmockoracleprice(6500)
        assert_equal(result["price_micro_usd"], 6500)
        node.mintdigidollar(1000, 0)
        self.generate(node, 1)

        self.log.info("getoraclesigners returns decoded bundle signer IDs")
        signers = node.getoraclesigners(20)
        assert_greater_than_or_equal(signers["chain_height"], 651)
        assert_equal(signers["scan_blocks"], 20)
        assert_equal(signers["required_signers"], 4)
        assert_equal(signers["total_oracle_slots"], 7)
        assert_equal(signers["active_oracle_slots"], 7)
        assert_greater_than(signers["bundle_count"], 0)

        latest = signers["bundles"][0]
        assert_equal(latest["version"], 3)
        assert_equal(latest["price_micro_usd"], 6500)
        assert_equal(latest["signer_count"], 4)
        assert_equal(latest["signer_ids"], [0, 1, 2, 3])
        assert_equal(latest["bitmap_valid"], True)
        assert latest["participation_bitmap"] != ""

        signer_rows = latest["signers"]
        assert_equal(len(signer_rows), 4)
        for expected_id, signer in enumerate(signer_rows):
            assert_equal(signer["oracle_id"], expected_id)
            assert_equal(signer["in_consensus"], True)
            assert_equal(signer["configured"], True)
            assert signer["pubkey"] != ""
            assert signer["endpoint"] != ""


if __name__ == "__main__":
    DigiDollarOracleSignersTest().main()
