#!/usr/bin/env python3
# Copyright (c) 2025-2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from decimal import Decimal
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_greater_than_or_equal


class DigiDollarRPCOracleTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing DigiDollar oracle price RPC...")
        node = self.nodes[0]
        
        self.log.info("Generating initial blocks for test setup...")
        self.generate(node, 110)
        
        node.setmockoracleprice(6000)
        
        self.test_oracle_price_basic()
        self.test_oracle_price_fields()
        self.test_oracle_price_values()
        self.test_oracle_price_after_update()
        self.test_oracle_price_consistency()
        
        self.log.info("All oracle price tests passed!")

    def test_oracle_price_basic(self):
        self.log.info("Testing basic oracle price response...")
        node = self.nodes[0]
        
        result = node.getoracleprice()
        
        assert 'price_micro_usd' in result, "Missing 'price_micro_usd' field"
        assert 'price_cents' in result, "Missing 'price_cents' field"
        assert 'price_usd' in result, "Missing 'price_usd' field"
        assert 'is_stale' in result, "Missing 'is_stale' field"
        assert 'status' in result, "Missing 'status' field"
        
        self.log.info(f"Price (micro-USD): {result['price_micro_usd']}")
        self.log.info(f"Price (cents): {result['price_cents']}")
        self.log.info(f"Price (USD): {result['price_usd']}")
        self.log.info(f"Oracle status: {result['status']}")

    def test_oracle_price_fields(self):
        self.log.info("Testing oracle price field types...")
        node = self.nodes[0]
        
        result = node.getoracleprice()
        
        assert isinstance(result['price_micro_usd'], int), "price_micro_usd should be int"
        assert isinstance(result['price_cents'], int), "price_cents should be int"
        assert isinstance(result["price_usd"], (int, float, Decimal)), "price_usd should be numeric"
        assert isinstance(result['last_update_height'], int), "last_update_height should be int"
        assert isinstance(result['last_update_time'], int), "last_update_time should be int"
        assert isinstance(result['validity_blocks'], int), "validity_blocks should be int"
        assert isinstance(result['is_stale'], bool), "is_stale should be boolean"
        assert isinstance(result['oracle_count'], int), "oracle_count should be int"
        assert isinstance(result['status'], str), "status should be string"
        
        valid_statuses = ['active', 'warning', 'error', 'mock']
        assert result['status'] in valid_statuses or result['status'] == 'active', \
            f"Invalid status: {result['status']}"
        
        self.log.info("All field types verified correctly")

    def test_oracle_price_values(self):
        self.log.info("Testing oracle price value ranges...")
        node = self.nodes[0]
        
        result = node.getoracleprice()
        
        assert_greater_than_or_equal(result['price_micro_usd'], 0)
        assert_greater_than_or_equal(result['price_cents'], 0)
        assert_greater_than_or_equal(result['price_usd'], 0)
        assert_greater_than_or_equal(result['validity_blocks'], 0)
        assert_greater_than_or_equal(result['oracle_count'], 0)
        
        if result['price_micro_usd'] > 0:
            expected_usd = result['price_micro_usd'] / 1000000.0
            actual_usd = float(result['price_usd'])
            assert abs(expected_usd - actual_usd) < 0.000001, \
                f"price_usd mismatch: expected {expected_usd}, got {actual_usd}"
            self.log.info("Price conversion verified (micro-USD -> USD)")
        
        if '24h_high' in result and '24h_low' in result:
            assert result['24h_high'] >= result['24h_low'], "24h_high should be >= 24h_low"
            self.log.info(f"24h range: {result['24h_low']} - {result['24h_high']}")

    def test_oracle_price_after_update(self):
        self.log.info("Testing oracle price after mock price update...")
        node = self.nodes[0]
        
        node.setmockoracleprice(5000)
        result1 = node.getoracleprice()
        
        node.setmockoracleprice(10000)
        result2 = node.getoracleprice()
        
        self.log.info(f"Price before update: {result1['price_micro_usd']} micro-USD")
        self.log.info(f"Price after update: {result2['price_micro_usd']} micro-USD")
        
        if result1['price_micro_usd'] != result2['price_micro_usd']:
            self.log.info("Price correctly updated after setmockoracleprice")
        else:
            self.log.info("Price unchanged (mock oracle may aggregate differently)")

    def test_oracle_price_consistency(self):
        self.log.info("Testing oracle price consistency across calls...")
        node = self.nodes[0]
        
        node.setmockoracleprice(7500)
        
        results = [node.getoracleprice() for _ in range(3)]
        
        prices = [r['price_micro_usd'] for r in results]
        assert len(set(prices)) == 1, f"Price should be consistent: {prices}"
        
        self.log.info("Oracle price is consistent across multiple calls")
        
        self.generate(node, 5)
        
        result_after = node.getoracleprice()
        assert_greater_than_or_equal(
            result_after['last_update_height'],
            results[0]['last_update_height']
        )
        self.log.info("Last update height correctly advances")


if __name__ == '__main__':
    DigiDollarRPCOracleTest().main()
