#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Regression test for Bug #15: getoracles / getalloracleprices consistency.

Validates that both RPCs report identical prices, statuses, and data for
the same oracles when called with the same scan depth.

Before the fix:
- getoracles hardcoded 20-block scan depth; getalloracleprices parameterised it
- getoracles checked local runtime + pending; getalloracleprices only checked on-chain + pending (no stale filter)
- Status strings differed (reporting/stopped/no_data vs reporting/no_data/outlier)
- Prices could diverge for the same oracle between the two RPCs

After the fix:
- Both use ScanOracleDataFromChain() shared helper
- getoracles now accepts an optional 'blocks' parameter
- Both filter stale pending messages, both check local runtime
- Status strings are unified: reporting/no_data/outlier
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class OracleRPCConsistencyTest(DigiByteTestFramework):
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

        self.log.info("Generating initial blocks past activation...")
        self.generate(node, 110)

        # Set mock oracle price
        node.setmockoracleprice(6500)
        node.mintdigidollar(1000, 0)

        # Mine a few more blocks so the DD-touching block embeds a v0x03
        # oracle bundle and later RPC scans see on-chain participants.
        self.generate(node, 5)

        self.test_same_scan_depth_same_data(node)
        self.test_blocks_param_accepted(node)
        self.test_status_strings_consistent(node)
        self.test_price_consistency(node)
        self.test_v03_onchain_signature_status(node)

        self.log.info("=== Bug #15 regression tests passed! ===")

    def test_same_scan_depth_same_data(self, node):
        """Both RPCs with same scan depth must report same oracle prices."""
        self.log.info("Test: same scan depth produces same prices")

        oracles_result = node.getoracles(False, 20)  # active_only=False, blocks=20
        prices_result = node.getalloracleprices(20)   # blocks=20

        # Build price map from getoracles
        oracles_prices = {}
        for o in oracles_result:
            oracles_prices[o["oracle_id"]] = o["last_price_micro_usd"]

        # Build price map from getalloracleprices
        allprices_prices = {}
        for o in prices_result["oracles"]:
            allprices_prices[o["oracle_id"]] = o["price_micro_usd"]

        # Every oracle in getalloracleprices must have the same price in getoracles
        for oid, price in allprices_prices.items():
            if oid in oracles_prices:
                assert oracles_prices[oid] == price, (
                    f"Oracle {oid} price mismatch: getoracles={oracles_prices[oid]} vs getalloracleprices={price}"
                )

    def test_blocks_param_accepted(self, node):
        """getoracles should accept blocks parameter without error."""
        self.log.info("Test: getoracles accepts blocks parameter")

        # These should not throw
        result_5 = node.getoracles(False, 5)
        result_50 = node.getoracles(False, 50)

        # Both should return oracle arrays
        assert_greater_than(len(result_5), 0)
        assert_greater_than(len(result_50), 0)

    def test_status_strings_consistent(self, node):
        """Both RPCs must use the same status vocabulary."""
        self.log.info("Test: status strings are from the same set")

        valid_statuses = {"reporting", "no_data", "outlier"}

        oracles_result = node.getoracles()
        for o in oracles_result:
            assert o["status"] in valid_statuses, \
                f"getoracles: unexpected status '{o['status']}' for oracle {o['oracle_id']}"

        prices_result = node.getalloracleprices()
        for o in prices_result["oracles"]:
            assert o["status"] in valid_statuses, \
                f"getalloracleprices: unexpected status '{o['status']}' for oracle {o['oracle_id']}"

    def test_price_consistency(self, node):
        """For any oracle reporting in both RPCs, prices must match."""
        self.log.info("Test: reporting oracles show identical prices in both RPCs")

        oracles_result = node.getoracles(False, 20)
        prices_result = node.getalloracleprices(20)

        # Map by oracle_id
        oracles_by_id = {o["oracle_id"]: o for o in oracles_result}
        prices_by_id = {o["oracle_id"]: o for o in prices_result["oracles"]}

        for oid in oracles_by_id:
            if oid not in prices_by_id:
                continue
            o_status = oracles_by_id[oid]["status"]
            p_status = prices_by_id[oid]["status"]

            # Status must match
            assert o_status == p_status, (
                f"Oracle {oid} status mismatch: getoracles='{o_status}' vs getalloracleprices='{p_status}'"
            )

            # If reporting, price must match
            if o_status == "reporting":
                o_price = oracles_by_id[oid]["last_price_micro_usd"]
                p_price = prices_by_id[oid]["price_micro_usd"]
                assert o_price == p_price, (
                    f"Oracle {oid} price mismatch: getoracles={o_price} vs getalloracleprices={p_price}"
                )

    def test_v03_onchain_signature_status(self, node):
        """v0x03 on-chain participants should report aggregate signature validity."""
        self.log.info("Test: v0x03 on-chain participants report valid aggregate signature status")

        prices_result = node.getalloracleprices(20)
        onchain_reporting = [
            o for o in prices_result["oracles"]
            if o["price_source"] == "on-chain" and o["status"] == "reporting"
        ]
        assert_greater_than(len(onchain_reporting), 0)
        for oracle in onchain_reporting:
            assert_equal(oracle["signature_valid"], True)


if __name__ == '__main__':
    OracleRPCConsistencyTest().main()
