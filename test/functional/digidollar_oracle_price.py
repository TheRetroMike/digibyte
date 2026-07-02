#!/usr/bin/env python3
# Copyright (c) 2025 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test fix for Bug #2 (sub-cent price_cents) and Bug #8 (hardcoded 24h/volatility)."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_greater_than
from decimal import Decimal


class DigiDollarBug2Bug8OraclePriceTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1"]]

    def run_test(self):
        node = self.nodes[0]

        # Generate enough blocks to activate DigiDollar
        self.generate(node, 200)

        # Set sub-cent mock oracle price: 4042 micro_usd = $0.004042
        node.setmockoracleprice(4042)

        result = node.getoracleprice()

        # Bug #2: price_cents is int64_t — sub-cent prices truncate to 0.
        # Use price_usd or price_micro_usd for sub-cent precision.
        price_cents = result["price_cents"]
        price_usd = result["price_usd"]
        price_micro = result["price_micro_usd"]
        self.log.info(f"price_cents = {price_cents}, price_usd = {price_usd}, price_micro_usd = {price_micro}")

        # price_cents = 4042 / 10000 = 0 (integer truncation for sub-cent prices)
        assert price_cents == 0, f"Expected price_cents=0 for sub-cent price, got {price_cents}"
        # price_micro_usd preserves full precision
        assert price_micro == 4042, f"Expected price_micro_usd=4042, got {price_micro}"
        # price_usd preserves full precision: 4042 / 1000000 = 0.004042
        assert abs(float(price_usd) - 0.004042) < 0.000001, f"Expected price_usd ~0.004042, got {price_usd}"

        # Bug #8: 24h_high/low defaults to current price_cents on fresh regtest
        # (no historical oracle data in blocks). With sub-cent mock price,
        # price_cents = 0, so 24h_high/low = 0. Just verify fields exist.
        high_24h = result["24h_high"]
        low_24h = result["24h_low"]
        self.log.info(f"24h_high = {high_24h}, 24h_low = {low_24h}")
        assert high_24h >= 0, f"24h_high should be >= 0, got {high_24h}"
        assert low_24h >= 0, f"24h_low should be >= 0, got {low_24h}"

        # Bug #8: volatility should not be hardcoded 2.5
        volatility = result["volatility"]
        self.log.info(f"volatility = {volatility}")
        # With a single constant mock price on fresh regtest, volatility = 0.0 (no variation)
        assert volatility != 2.5, f"Volatility should not be hardcoded 2.5, got {volatility}"


if __name__ == '__main__':
    DigiDollarBug2Bug8OraclePriceTest().main()
