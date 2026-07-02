#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for redeeming an active DD position after descriptor restore."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_greater_than

ORACLE_PRICE_MICRO_USD = 500000


class WalletDigiDollarActiveRestoreRedeemTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-digidollar=1",
            "-txindex=1",
            "-dandelion=0",
        ]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def run_test(self):
        node = self.nodes[0]
        amount = 100000

        self.log.info("Mining spendable DGB and setting mock oracle price")
        self.generate(node, 200)
        result = node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        assert_equal(result["price_micro_usd"], ORACLE_PRICE_MICRO_USD)

        self.log.info("Minting an active tier-0 DD position")
        mint = node.mintdigidollar(amount, 0)
        position_id = mint["position_id"]
        unlock_height = mint["unlock_height"]
        self.generate(node, 1)

        assert_equal(node.getdigidollarbalance()["total"], amount)
        assert_equal(len(node.listdigidollarpositions()), 1)

        self.log.info("Exporting private descriptors and restoring into a blank wallet")
        descriptors = node.listdescriptors(True)["descriptors"]
        node.createwallet(
            wallet_name="active_dd_restore",
            disable_private_keys=False,
            blank=True,
            passphrase="",
            avoid_reuse=False,
            descriptors=True,
        )
        restored = node.get_wallet_rpc("active_dd_restore")

        imports = []
        for desc in descriptors:
            req = {
                "desc": desc["desc"],
                "timestamp": 0,
                "active": desc.get("active", False),
                "internal": desc.get("internal", False),
            }
            if "range" in desc:
                req["range"] = desc["range"]
            imports.append(req)

        result = restored.importdescriptors(imports)
        assert_equal(sum(1 for item in result if item.get("success", False)), len(imports))

        self.log.info("Verifying restored active DD state")
        assert_equal(restored.getdigidollarbalance()["total"], amount)
        restored_positions = restored.listdigidollarpositions()
        assert_equal(len(restored_positions), 1)
        assert_equal(restored_positions[0]["position_id"], position_id)
        assert_equal(restored_positions[0]["spendable"], True)

        current_height = node.getblockcount()
        if current_height <= unlock_height:
            self.log.info("Advancing to the tier-0 unlock height")
            self.generate(node, unlock_height - current_height + 1)

        self.log.info("Redeeming from the restored wallet")
        result = node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        assert_equal(result["price_micro_usd"], ORACLE_PRICE_MICRO_USD)
        redeem = restored.redeemdigidollar(position_id, amount)
        assert "txid" in redeem
        self.generate(node, 1)

        assert_equal(restored.getdigidollarbalance()["total"], 0)
        positions_after = restored.listdigidollarpositions(False)
        assert_greater_than(len(positions_after), 0)
        assert_equal(positions_after[0]["status"], "redeemed")


if __name__ == "__main__":
    WalletDigiDollarActiveRestoreRedeemTest().main()
