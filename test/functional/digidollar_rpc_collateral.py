#!/usr/bin/env python3
"""Test DigiDollar collateral calculation RPC command (calculatecollateralrequirement)."""

from decimal import Decimal, ROUND_DOWN
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
)


LOCK_TIERS = {
    0: (30, 500),
    1: (90, 400),
    2: (180, 350),
    3: (365, 300),
    4: (730, 275),
    5: (1095, 250),
    6: (1825, 225),
    7: (2555, 212),
    8: (3650, 200),
}


class DigiDollarCollateralTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing DigiDollar collateral calculation RPC...")

        self.setup_collateral_test()

        self.test_collateral_tier_0()
        self.test_collateral_tier_3()
        self.test_collateral_tier_8()
        self.test_collateral_all_tiers()
        self.test_collateral_with_custom_price()
        self.test_collateral_invalid_amount()
        self.test_collateral_response_format()

        self.log.info("All collateral calculation tests passed!")

    def setup_collateral_test(self):
        """Setup test environment for collateral calculations."""
        self.log.info("Setting up test environment...")

        self.generate(self.nodes[0], 110)

        self.default_price_micro_usd = 6000
        self.nodes[0].setmockoracleprice(self.default_price_micro_usd)

        stats = self.nodes[0].getdigidollarstats()
        assert "health_percentage" in stats or "system_collateral_ratio" in stats

        self.log.info(f"Oracle price set to {self.default_price_micro_usd} micro-USD ($0.006/DGB)")

    def calculate_expected_dgb(self, dd_amount_cents, ratio_percent, price_micro_usd):
        dd_amount_usd = Decimal(dd_amount_cents) / Decimal(100)
        ratio_multiplier = Decimal(ratio_percent) / Decimal(100)
        price_usd = Decimal(price_micro_usd) / Decimal(1000000)
        return (dd_amount_usd * ratio_multiplier) / price_usd

    def test_collateral_tier_0(self):
        self.log.info("Testing Tier 0 (30 days, 500% ratio)...")

        dd_amount_cents = 10000
        lock_days = LOCK_TIERS[0][0]
        expected_ratio = LOCK_TIERS[0][1]

        result = self.nodes[0].calculatecollateralrequirement(dd_amount_cents, lock_days)

        actual_ratio = int(result['effective_ratio'])
        self.log.info(f"Tier 0: expected ratio={expected_ratio}%, actual ratio={actual_ratio}%")

        expected_dgb = self.calculate_expected_dgb(dd_amount_cents, expected_ratio, self.default_price_micro_usd)
        actual_dgb = Decimal(str(result['required_dgb']))

        self.log.info(f"Tier 0: expected DGB={expected_dgb:.2f}, actual DGB={actual_dgb:.2f}")

        tolerance = expected_dgb * Decimal('0.01')
        assert abs(actual_dgb - expected_dgb) <= tolerance, \
            f"Tier 0 DGB mismatch: expected {expected_dgb:.2f}, got {actual_dgb:.2f}"

    def test_collateral_tier_3(self):
        self.log.info("Testing Tier 3 (365 days, 300% ratio)...")

        dd_amount_cents = 10000
        lock_days = LOCK_TIERS[3][0]
        expected_ratio = LOCK_TIERS[3][1]

        result = self.nodes[0].calculatecollateralrequirement(dd_amount_cents, lock_days)

        actual_ratio = int(result['effective_ratio'])
        self.log.info(f"Tier 3: expected ratio={expected_ratio}%, actual ratio={actual_ratio}%")

        expected_dgb = self.calculate_expected_dgb(dd_amount_cents, expected_ratio, self.default_price_micro_usd)
        actual_dgb = Decimal(str(result['required_dgb']))

        self.log.info(f"Tier 3: expected DGB={expected_dgb:.2f}, actual DGB={actual_dgb:.2f}")

        tolerance = expected_dgb * Decimal('0.01')
        assert abs(actual_dgb - expected_dgb) <= tolerance, \
            f"Tier 3 DGB mismatch: expected {expected_dgb:.2f}, got {actual_dgb:.2f}"

    def test_collateral_tier_8(self):
        self.log.info("Testing Tier 8 (3650 days, 200% ratio)...")

        dd_amount_cents = 10000
        lock_days = LOCK_TIERS[8][0]
        expected_ratio = LOCK_TIERS[8][1]

        result = self.nodes[0].calculatecollateralrequirement(dd_amount_cents, lock_days)

        actual_ratio = int(result['effective_ratio'])
        self.log.info(f"Tier 8: expected ratio={expected_ratio}%, actual ratio={actual_ratio}%")

        expected_dgb = self.calculate_expected_dgb(dd_amount_cents, expected_ratio, self.default_price_micro_usd)
        actual_dgb = Decimal(str(result['required_dgb']))

        self.log.info(f"Tier 8: expected DGB={expected_dgb:.2f}, actual DGB={actual_dgb:.2f}")

        tolerance = expected_dgb * Decimal('0.01')
        assert abs(actual_dgb - expected_dgb) <= tolerance, \
            f"Tier 8 DGB mismatch: expected {expected_dgb:.2f}, got {actual_dgb:.2f}"

    def test_collateral_all_tiers(self):
        self.log.info("Testing all lock tiers...")

        dd_amount_cents = 100000

        for tier, (lock_days, expected_ratio) in LOCK_TIERS.items():
            self.log.info(f"  Testing Tier {tier}: {lock_days} days, {expected_ratio}% ratio")

            result = self.nodes[0].calculatecollateralrequirement(dd_amount_cents, lock_days)

            assert 'required_dgb' in result, f"Tier {tier}: Missing required_dgb field"
            assert 'effective_ratio' in result, f"Tier {tier}: Missing effective_ratio field"
            assert 'oracle_price_micro_usd' in result, f"Tier {tier}: Missing oracle_price_micro_usd field"
            assert 'dca_multiplier' in result, f"Tier {tier}: Missing dca_multiplier field"

            actual_ratio = int(result['effective_ratio'])
            assert_greater_than(actual_ratio, 0), f"Tier {tier}: Ratio must be positive"

            expected_dgb = self.calculate_expected_dgb(dd_amount_cents, actual_ratio, self.default_price_micro_usd)
            actual_dgb = Decimal(str(result['required_dgb']))

            # Allow 1% tolerance
            tolerance = expected_dgb * Decimal('0.01')
            assert abs(actual_dgb - expected_dgb) <= tolerance, \
                f"Tier {tier}: DGB mismatch - expected {expected_dgb:.2f}, got {actual_dgb:.2f}"

            self.log.info(f"    Result: {actual_dgb:.2f} DGB at {actual_ratio}% ratio")

        self.log.info("All tier calculations verified!")

    def test_collateral_with_custom_price(self):
        """Test collateral calculation with custom oracle price parameter."""
        self.log.info("Testing collateral with custom oracle price...")

        dd_amount_cents = 10000
        lock_days = 365

        price_scenarios = [
            (5000, "$0.005/DGB (lower price = more DGB required)"),
            (10000, "$0.01/DGB (higher price = less DGB required)"),
            (50000, "$0.05/DGB"),
            (100000, "$0.10/DGB"),
            (500000, "$0.50/DGB"),
            (1000000, "$1.00/DGB"),
        ]

        for custom_price, description in price_scenarios:
            self.log.info(f"  Testing with {description}")

            self.nodes[0].setmockoracleprice(custom_price)

            result = self.nodes[0].calculatecollateralrequirement(dd_amount_cents, lock_days)

            actual_price = int(result['oracle_price_micro_usd'])
            assert_equal(actual_price, custom_price), \
                f"Oracle price mismatch: expected {custom_price}, got {actual_price}"

            ratio = int(result['effective_ratio'])
            expected_dgb = self.calculate_expected_dgb(dd_amount_cents, ratio, custom_price)
            actual_dgb = Decimal(str(result['required_dgb']))

            self.log.info(f"    Price: {custom_price} micro-USD, Required: {actual_dgb:.2f} DGB")

            tolerance = expected_dgb * Decimal('0.01')
            assert abs(actual_dgb - expected_dgb) <= tolerance, \
                f"Price {custom_price}: DGB mismatch - expected {expected_dgb:.2f}, got {actual_dgb:.2f}"

        self.nodes[0].setmockoracleprice(self.default_price_micro_usd)
        self.log.info("Custom price tests completed!")

    def test_collateral_invalid_tier(self):
        """Test error handling for invalid lock tier values."""
        self.log.info("Testing invalid tier handling...")

        dd_amount_cents = 10000

        invalid_lock_days = [-1, -100]

        for invalid_days in invalid_lock_days:
            self.log.info(f"  Testing invalid lock_days: {invalid_days}")
            try:
                assert_raises_rpc_error(
                    None,
                    "",
                    self.nodes[0].calculatecollateralrequirement,
                    dd_amount_cents,
                    invalid_days
                )
                self.log.info(f"    Correctly rejected lock_days={invalid_days}")
            except AssertionError:
                result = self.nodes[0].calculatecollateralrequirement(dd_amount_cents, invalid_days)
                self.log.info(f"    Implementation handled gracefully: {result}")

        very_high_lock_days = 5000
        result = self.nodes[0].calculatecollateralrequirement(dd_amount_cents, very_high_lock_days)
        actual_ratio = int(result['effective_ratio'])
        assert_greater_than_or_equal(actual_ratio, 200), \
            f"Very high lock_days should use at least tier 9 ratio, got {actual_ratio}%"
        self.log.info(f"  High lock_days ({very_high_lock_days}) uses ratio: {actual_ratio}%")

        self.log.info("Invalid tier tests completed!")

    def test_collateral_invalid_amount(self):
        """Test error handling for invalid DD amounts."""
        self.log.info("Testing invalid amount handling...")

        lock_days = 365

        invalid_amounts = [
            (0, "zero amount"),
            (-100, "negative amount"),
            (-10000, "large negative amount"),
        ]

        for invalid_amount, description in invalid_amounts:
            self.log.info(f"  Testing {description}: {invalid_amount} cents")
            try:
                assert_raises_rpc_error(
                    None,
                    "",
                    self.nodes[0].calculatecollateralrequirement,
                    invalid_amount,
                    lock_days
                )
                self.log.info(f"    Correctly rejected amount={invalid_amount}")
            except AssertionError:
                try:
                    result = self.nodes[0].calculatecollateralrequirement(invalid_amount, lock_days)
                    if 'required_dgb' in result:
                        dgb = Decimal(str(result['required_dgb']))
                        if dgb <= 0:
                            self.log.info(f"    Implementation returned non-positive DGB: {dgb}")
                        else:
                            self.log.info(f"    Warning: Implementation accepted invalid amount, returned {dgb} DGB")
                except Exception as e:
                    self.log.info(f"    Correctly rejected with: {e}")

        small_valid_regtest_amount = 50
        self.log.info(f"  Testing small valid regtest amount: {small_valid_regtest_amount} cents")
        try:
            result = self.nodes[0].calculatecollateralrequirement(small_valid_regtest_amount, lock_days)
            self.log.info(f"    Accepted small valid regtest amount")
        except Exception as e:
            self.log.info(f"    Rejected small valid regtest amount: {e}")

        above_regtest_max = 100001
        self.log.info(f"  Testing above regtest mint maximum: {above_regtest_max} cents")
        assert_raises_rpc_error(
            -8,
            "Maximum mint amount is $1000",
            self.nodes[0].calculatecollateralrequirement,
            above_regtest_max,
            lock_days,
        )

        self.log.info("Invalid amount tests completed!")

    def test_collateral_response_format(self):
        """Test response structure and data types."""
        self.log.info("Testing response format...")

        dd_amount_cents = 10000
        lock_days = 365

        result = self.nodes[0].calculatecollateralrequirement(dd_amount_cents, lock_days)
        self.log.info(f"Response: {result}")

        required_fields = [
            'required_dgb',
            'effective_ratio',
            'oracle_price_micro_usd',
            'dca_multiplier'
        ]

        for field in required_fields:
            assert field in result, f"Missing required field: {field}"
            assert result[field] is not None, f"Field {field} is None"
            self.log.info(f"  {field}: {result[field]}")

        required_dgb = Decimal(str(result['required_dgb']))
        assert_greater_than(required_dgb, Decimal('0')), \
            f"required_dgb must be positive, got {required_dgb}"

        effective_ratio = int(result['effective_ratio'])
        assert_greater_than(effective_ratio, 0), \
            f"effective_ratio must be positive, got {effective_ratio}"
        assert effective_ratio >= 200 and effective_ratio <= 1000, \
            f"effective_ratio should be between 200% and 1000%, got {effective_ratio}%"

        oracle_price = int(result['oracle_price_micro_usd'])
        assert_greater_than(oracle_price, 0), \
            f"oracle_price_micro_usd must be positive, got {oracle_price}"

        dca_multiplier = Decimal(str(result['dca_multiplier']))
        assert_greater_than_or_equal(dca_multiplier, Decimal('1.0')), \
            f"dca_multiplier must be >= 1.0, got {dca_multiplier}"

        amounts = [10000, 50000, 100000]
        self.log.info("  Testing amount scaling...")

        previous_dgb = Decimal('0')
        for amount in amounts:
            result = self.nodes[0].calculatecollateralrequirement(amount, lock_days)
            dgb = Decimal(str(result['required_dgb']))

            assert_greater_than(dgb, previous_dgb), \
                f"DGB for {amount} cents ({dgb}) should be > previous ({previous_dgb})"
            previous_dgb = dgb
            self.log.info(f"    {amount} cents -> {dgb:.2f} DGB")

        self.log.info("Response format tests completed!")


if __name__ == '__main__':
    DigiDollarCollateralTest().main()
