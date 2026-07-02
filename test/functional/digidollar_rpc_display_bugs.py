#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test DigiDollar RPC display bugs reported in RC22.

Verifies 4 RPC reporting issues found during testnet stress testing:
1. active_positions always returns 0 in getdigidollarstats
2. price_cents rounds sub-cent prices to 1 in getoracleprice
3. usd_value off by ~100,000x in estimatecollateral
4. system_health hardcoded to 150 in estimatecollateral
5. oracle_price_age is hardcoded to 0 in getdigidollarstats

These tests should FAIL before fixes and PASS after.
"""

from decimal import Decimal
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_greater_than


class DigiDollarRPCDisplayBugsTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        # Generate initial coins for minting collateral
        self.generate(self.nodes[0], 110)
        self.sync_all()

        # Set oracle price on all nodes
        self.nodes[0].setmockoracleprice(500000)  # $0.50/DGB
        self.nodes[1].setmockoracleprice(500000)

        # Run each bug test independently, logging results
        bugs_confirmed = []

        try:
            self.test_bug5_oracle_price_age()
        except AssertionError as e:
            self.log.info(f"✗ Bug 5 CONFIRMED (oracle_price_age): {e}")
            bugs_confirmed.append("Bug 5: oracle_price_age hardcoded 0")

        try:
            self.test_bug1_active_positions()
        except AssertionError as e:
            self.log.info(f"✗ Bug 1 CONFIRMED (active_positions): {e}")
            bugs_confirmed.append("Bug 1: active_positions always 0")

        try:
            self.test_bug4_system_health_estimatecollateral()
        except AssertionError as e:
            self.log.info(f"✗ Bug 4 CONFIRMED (system_health): {e}")
            bugs_confirmed.append("Bug 4: system_health hardcoded 150")

        try:
            self.test_bug2_price_cents_subcent()
        except AssertionError as e:
            self.log.info(f"✗ Bug 2 CONFIRMED (price_cents): {e}")
            bugs_confirmed.append("Bug 2: price_cents rounds to 1")

        try:
            self.test_bug3_usd_value_estimatecollateral()
        except AssertionError as e:
            self.log.info(f"✗ Bug 3 CONFIRMED (usd_value): {e}")
            bugs_confirmed.append("Bug 3: usd_value off by 100,000x")

        self.log.info(f"\n=== BUGS CONFIRMED: {len(bugs_confirmed)} / 4 ===")
        for b in bugs_confirmed:
            self.log.info(f"  {b}")

        if bugs_confirmed:
            # Fail the test to indicate bugs exist (expected pre-fix behavior)
            raise AssertionError(f"{len(bugs_confirmed)} bugs confirmed: {', '.join(bugs_confirmed)}")

    def test_bug1_active_positions(self):
        """Bug 1: active_positions should reflect actual vault count after minting."""
        self.log.info("Testing Bug 1: active_positions in getdigidollarstats...")

        # Check initial state — should be 0 with no positions
        stats = self.nodes[0].getdigidollarstats()
        initial_positions = stats.get('active_positions', -1)
        self.log.info(f"Initial active_positions: {initial_positions}")
        assert_equal(initial_positions, 0)

        # Mint a DD position
        self.nodes[0].mintdigidollar(1000, 0)  # 1000 cents = $10, tier 0
        self.generate(self.nodes[0], 2)
        self.sync_all()

        # After minting, active_positions should be > 0
        stats_after = self.nodes[0].getdigidollarstats()
        positions_after = stats_after.get('active_positions', -1)
        self.log.info(f"After mint active_positions: {positions_after}")

        # THIS IS THE BUG: active_positions stays 0 even after minting
        # After fix, this assertion should pass:
        assert_greater_than(positions_after, 0)
        self.log.info("✓ Bug 1 FIXED: active_positions reflects real vault count")

    def test_bug5_oracle_price_age(self):
        """Bug 5: oracle_price_age should advance after the last price update."""
        self.log.info("Testing Bug 5: oracle_price_age in getdigidollarstats...")

        update = self.nodes[0].setmockoracleprice(500000)
        self.nodes[1].setmockoracleprice(500000)
        update_height = update["update_height"]
        self.generate(self.nodes[0], 3)
        self.sync_all()

        mock_price = self.nodes[0].getmockoracleprice()
        stats = self.nodes[0].getdigidollarstats()
        current_height = self.nodes[0].getblockcount()
        expected_age = current_height - update_height

        self.log.info(f"mock update_height: {update_height}")
        self.log.info(f"mock last_update_height: {mock_price['last_update_height']}")
        self.log.info(f"current height: {current_height}")
        self.log.info(f"oracle_price_age: {stats['oracle_price_age']}")

        assert_greater_than(expected_age, 0)
        assert_equal(mock_price["last_update_height"], update_height)
        assert_equal(stats["oracle_price_age"], expected_age)
        self.log.info("✓ Bug 5 FIXED: oracle_price_age tracks blocks since update")

    def test_bug2_price_cents_subcent(self):
        """Bug 2: price_cents should handle sub-cent DGB prices correctly."""
        self.log.info("Testing Bug 2: price_cents for sub-cent prices...")

        # Set DGB price to $0.00417 (4170 micro-USD) — well below 1 cent
        self.nodes[0].setmockoracleprice(4170)
        self.nodes[1].setmockoracleprice(4170)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        oracle_info = self.nodes[0].getoracleprice()
        price_micro = oracle_info.get('price_micro_usd', 0)
        price_cents = oracle_info.get('price_cents', -1)
        price_usd = oracle_info.get('price_usd', 0)

        self.log.info(f"price_micro_usd: {price_micro}")
        self.log.info(f"price_cents: {price_cents}")
        self.log.info(f"price_usd: {price_usd}")

        # 4170 micro-USD = $0.00417 = 0.417 cents
        # Before fix: price_cents = 1 (floor bumps sub-cent to 1)
        # After fix: price_cents = 0 (honest about sub-cent price)
        # price_usd should always be correct regardless
        assert_equal(price_micro, 4170)

        # THIS IS THE BUG: price_cents shows 1 instead of 0
        assert_equal(price_cents, 0)
        self.log.info("✓ Bug 2 FIXED: price_cents = 0 for sub-cent prices")

    def test_bug3_usd_value_estimatecollateral(self):
        """Bug 3: usd_value in estimatecollateral should show correct USD amount."""
        self.log.info("Testing Bug 3: usd_value in estimatecollateral...")

        # Set oracle price back to something reasonable
        self.nodes[0].setmockoracleprice(50000)  # $0.05 per DGB
        self.nodes[1].setmockoracleprice(50000)
        self.generate(self.nodes[0], 1)
        self.sync_all()

        # Estimate collateral for 10000 DD cents = $100
        estimate = self.nodes[0].estimatecollateral(10000, 0)
        usd_value = estimate.get('usd_value', -1)
        dd_amount = estimate.get('dd_amount', 0)

        self.log.info(f"dd_amount: {dd_amount} cents")
        self.log.info(f"usd_value: {usd_value}")

        # 10000 cents = $100.00
        # Before fix: usd_value = 0.00099999 (ValueFromAmount divides by COIN)
        # After fix: usd_value = 100.0 (correct cents-to-dollars conversion)
        assert_equal(dd_amount, 10000)

        # usd_value should be approximately $100 (10000 cents / 100)
        # Allow small floating point tolerance
        usd_decimal = Decimal(str(usd_value))
        assert_greater_than(usd_decimal, Decimal('50'))  # Must be way more than $0.001
        self.log.info(f"✓ Bug 3 FIXED: usd_value = ${usd_value} (expected ~$100)")

    def test_bug4_system_health_estimatecollateral(self):
        """Bug 4: system_health in estimatecollateral should match getdigidollarstats."""
        self.log.info("Testing Bug 4: system_health consistency...")

        # Reuse the position from Bug 1 so intentional price swings in later
        # display checks do not trip volatility freeze before this comparison.
        stats = self.nodes[0].getdigidollarstats()
        estimate = self.nodes[0].estimatecollateral(1000, 0)

        stats_health = stats.get('health_percentage', -1)
        estimate_health = estimate.get('system_health', -1)

        self.log.info(f"getdigidollarstats health: {stats_health}")
        self.log.info(f"estimatecollateral health: {estimate_health}")

        # Before fix: stats shows real health (e.g., 735), estimate shows 150
        # After fix: both should show the same value
        # THIS IS THE BUG: estimate_health is always 150 regardless of real health
        assert_equal(stats_health, estimate_health)
        self.log.info("✓ Bug 4 FIXED: system_health consistent across RPCs")


if __name__ == '__main__':
    DigiDollarRPCDisplayBugsTest().main()
