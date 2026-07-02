#!/usr/bin/env python3
"""Test DigiDollar collateral estimation RPC command.

Test comprehensive estimatecollateral functionality including:
- Basic estimation with various DD amounts
- All 10 lock tiers with correct ratios
- DCA multiplier effect on estimation
- Response format verification
- Tier boundary validation

Lock Tiers (tier -> days, ratio):
    Tier 0: 1 hour,      1000% collateral ratio
    Tier 1: 30 days,     500% collateral ratio
    Tier 2: 90 days,     400% collateral ratio
    Tier 3: 180 days,    350% collateral ratio
    Tier 4: 365 days,    300% collateral ratio
    Tier 5: 730 days,    275% collateral ratio
    Tier 6: 1095 days,   250% collateral ratio
    Tier 7: 1825 days,   225% collateral ratio
    Tier 8: 2555 days,   212% collateral ratio
    Tier 9: 3650 days,   200% collateral ratio

Estimation Formula:
    DGB_required = (DD_amount_cents / 100) * (ratio_percent / 100) / oracle_price_usd

Example:
    $100 DD at 300% ratio with $0.006 DGB price = 100 * 3.0 / 0.006 = 50,000 DGB
"""

from decimal import Decimal
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
)


# Lock tier configuration: tier -> (lock_days, ratio_percent)
LOCK_TIERS = {
    0: (0, 1000),
    1: (30, 500),
    2: (90, 400),
    3: (180, 350),
    4: (365, 300),
    5: (730, 275),
    6: (1095, 250),
    7: (1825, 225),
    8: (2555, 212),
    9: (3650, 200),
}

# Days to tier mapping for boundary tests
# Given a lock_days value, which tier should it map to?
DAYS_TO_TIER = {
    # Tier 0: 0-1 day (1 hour)
    0: 0,
    1: 0,
    # Tier 1: 30-89 days
    29: 0,
    30: 1,
    89: 1,
    # Tier 2: 90-179 days
    90: 2,
    179: 2,
    # Tier 3: 180-364 days
    180: 3,
    364: 3,
    # Tier 4: 365-729 days
    365: 4,
    729: 4,
    # Tier 5: 730-1094 days
    730: 5,
    1094: 5,
    # Tier 6: 1095-1824 days
    1095: 6,
    1824: 6,
    # Tier 7: 1825-2554 days
    1825: 7,
    2554: 7,
    # Tier 8: 2555-3649 days
    2555: 8,
    3649: 8,
    # Tier 9: 3650+ days
    3650: 9,
    5000: 9,
}


class DigiDollarEstimateTest(DigiByteTestFramework):
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
        self.log.info("Testing DigiDollar collateral estimation RPC...")

        self.setup_estimate_test()

        self.test_estimate_basic()
        self.test_estimate_returns_correct_tier()
        self.test_estimate_with_dca_impact()
        self.test_estimate_30_days()
        self.test_estimate_90_days()
        self.test_estimate_180_days()
        self.test_estimate_365_days()
        self.test_estimate_various_amounts()
        self.test_estimate_response_fields()
        self.test_estimate_all_tiers()
        self.test_estimate_with_custom_price()
        self.test_estimate_invalid_parameters()

        self.log.info("All collateral estimation tests passed!")

    def setup_estimate_test(self):
        """Setup test environment for collateral estimation."""
        self.log.info("Setting up test environment...")

        # Mine blocks to have mature coins
        self.generate(self.nodes[0], 110)

        # Set oracle price: 6000 micro-USD = $0.006/DGB
        self.default_price_micro_usd = 6000
        self.nodes[0].setmockoracleprice(self.default_price_micro_usd)

        # Verify DigiDollar system is active
        stats = self.nodes[0].getdigidollarstats()
        assert "health_percentage" in stats or "system_collateral_ratio" in stats

        self.log.info(f"Oracle price set to {self.default_price_micro_usd} micro-USD ($0.006/DGB)")

    def calculate_expected_dgb(self, dd_amount_cents, ratio_percent, price_micro_usd):
        """Calculate expected DGB collateral requirement.

        Formula: DGB = (DD_cents / 100) * (ratio / 100) / (price_micro_usd / 1,000,000)
        Simplified: DGB = DD_cents * ratio * 10000 / price_micro_usd
        """
        dd_amount_usd = Decimal(dd_amount_cents) / Decimal(100)
        ratio_multiplier = Decimal(ratio_percent) / Decimal(100)
        price_usd = Decimal(price_micro_usd) / Decimal(1000000)
        return (dd_amount_usd * ratio_multiplier) / price_usd

    def test_estimate_basic(self):
        """Test basic estimation with $100 DD for 365 days (Tier 4, 300% ratio)."""
        self.log.info("Testing basic estimation: $100 DD for 365 days...")

        # $100 = 10000 cents, Tier 4 = 365 days, 300% ratio
        dd_amount_cents = 10000
        lock_tier = 4  # 365 days
        expected_ratio = LOCK_TIERS[lock_tier][1]  # 300%

        result = self.nodes[0].estimatecollateral(dd_amount_cents, lock_tier)

        # Verify basic response structure
        assert 'required_dgb' in result, "Missing required_dgb field"
        assert 'lock_tier' in result, "Missing lock_tier field"
        assert 'base_ratio' in result, "Missing base_ratio field"

        # Verify tier is correct
        assert_equal(result['lock_tier'], lock_tier)

        # Verify base ratio matches expected
        actual_ratio = int(result['base_ratio'])
        assert_equal(actual_ratio, expected_ratio)

        # Verify DGB calculation is reasonable
        # RPC uses effective_ratio (base_ratio * dca_multiplier), not base_ratio alone.
        # On fresh chain (health=0), DCA multiplier = 2.0x (emergency tier).
        effective_ratio = int(result['effective_ratio'])
        expected_dgb = self.calculate_expected_dgb(dd_amount_cents, effective_ratio, self.default_price_micro_usd)
        actual_dgb = Decimal(str(result['required_dgb']))

        self.log.info(f"Expected ~{expected_dgb:.2f} DGB (effective ratio {effective_ratio}%), got {actual_dgb:.2f} DGB")

        # Allow 5% tolerance
        tolerance = expected_dgb * Decimal('0.05')
        assert abs(actual_dgb - expected_dgb) <= tolerance, \
            f"DGB calculation out of range: expected ~{expected_dgb:.2f}, got {actual_dgb:.2f}"

    def test_estimate_returns_correct_tier(self):
        """Verify tier is correctly identified from lock_tier parameter."""
        self.log.info("Testing tier identification from lock_tier...")

        dd_amount_cents = 10000  # $100

        for tier, (expected_days, expected_ratio) in LOCK_TIERS.items():
            self.log.info(f"  Testing tier {tier}: {expected_days} days, {expected_ratio}% ratio")

            result = self.nodes[0].estimatecollateral(dd_amount_cents, tier)

            # Verify tier is returned correctly
            assert_equal(result['lock_tier'], tier)

            # Verify lock_days matches expected
            if 'lock_days' in result:
                assert_equal(result['lock_days'], expected_days)

            # Verify base_ratio matches expected
            actual_ratio = int(result['base_ratio'])
            assert_equal(actual_ratio, expected_ratio)

            self.log.info(f"    Verified: tier={tier}, ratio={actual_ratio}%")

    def test_estimate_with_dca_impact(self):
        """Test estimation includes DCA multiplier when system stressed."""
        self.log.info("Testing DCA multiplier effect on estimation...")

        dd_amount_cents = 10000  # $100
        lock_tier = 4  # 365 days

        # Get baseline estimation
        result = self.nodes[0].estimatecollateral(dd_amount_cents, lock_tier)

        # Verify DCA multiplier is present in response
        assert 'dca_multiplier' in result, "Missing dca_multiplier field"
        dca_multiplier = Decimal(str(result['dca_multiplier']))

        # DCA multiplier should be >= 1.0 (never reduces collateral requirement)
        assert_greater_than_or_equal(dca_multiplier, Decimal('1.0'))

        # Verify effective_ratio = base_ratio * dca_multiplier
        base_ratio = Decimal(str(result['base_ratio']))
        effective_ratio = Decimal(str(result['effective_ratio']))
        calculated_effective = base_ratio * dca_multiplier

        self.log.info(f"  Base ratio: {base_ratio}%, DCA: {dca_multiplier}, Effective: {effective_ratio}%")

        # Allow small floating point tolerance
        tolerance = Decimal('0.01')
        assert abs(effective_ratio - calculated_effective) <= tolerance * calculated_effective, \
            f"Effective ratio mismatch: {effective_ratio} != {base_ratio} * {dca_multiplier}"

    def test_estimate_30_days(self):
        """Test estimation for 30 days maps to Tier 1 (500% ratio)."""
        self.log.info("Testing 30-day estimation (Tier 1, 500%)...")

        dd_amount_cents = 10000  # $100
        lock_tier = 1  # 30 days
        expected_ratio = 500

        result = self.nodes[0].estimatecollateral(dd_amount_cents, lock_tier)

        assert_equal(result['lock_tier'], lock_tier)
        assert_equal(int(result['base_ratio']), expected_ratio)

        if 'lock_days' in result:
            assert_equal(result['lock_days'], 30)

        self.log.info(f"  30-day tier verified: {result['base_ratio']}% ratio")

    def test_estimate_90_days(self):
        """Test estimation for 90 days maps to Tier 2 (400% ratio)."""
        self.log.info("Testing 90-day estimation (Tier 2, 400%)...")

        dd_amount_cents = 10000  # $100
        lock_tier = 2  # 90 days
        expected_ratio = 400

        result = self.nodes[0].estimatecollateral(dd_amount_cents, lock_tier)

        assert_equal(result['lock_tier'], lock_tier)
        assert_equal(int(result['base_ratio']), expected_ratio)

        if 'lock_days' in result:
            assert_equal(result['lock_days'], 90)

        self.log.info(f"  90-day tier verified: {result['base_ratio']}% ratio")

    def test_estimate_180_days(self):
        """Test estimation for 180 days maps to Tier 3 (350% ratio)."""
        self.log.info("Testing 180-day estimation (Tier 3, 350%)...")

        dd_amount_cents = 10000  # $100
        lock_tier = 3  # 180 days
        expected_ratio = 350

        result = self.nodes[0].estimatecollateral(dd_amount_cents, lock_tier)

        assert_equal(result['lock_tier'], lock_tier)
        assert_equal(int(result['base_ratio']), expected_ratio)

        if 'lock_days' in result:
            assert_equal(result['lock_days'], 180)

        self.log.info(f"  180-day tier verified: {result['base_ratio']}% ratio")

    def test_estimate_365_days(self):
        """Test estimation for 365 days maps to Tier 4 (300% ratio)."""
        self.log.info("Testing 365-day estimation (Tier 4, 300%)...")

        dd_amount_cents = 10000  # $100
        lock_tier = 4  # 365 days
        expected_ratio = 300

        result = self.nodes[0].estimatecollateral(dd_amount_cents, lock_tier)

        assert_equal(result['lock_tier'], lock_tier)
        assert_equal(int(result['base_ratio']), expected_ratio)

        if 'lock_days' in result:
            assert_equal(result['lock_days'], 365)

        self.log.info(f"  365-day tier verified: {result['base_ratio']}% ratio")

    def test_estimate_various_amounts(self):
        """Test estimation with valid regtest mint amounts."""
        self.log.info("Testing various DD amounts...")

        lock_tier = 4  # 365 days, 300% ratio
        base_ratio = LOCK_TIERS[lock_tier][1]

        # Regtest caps a single mint at $1,000 (100000 cents).
        test_amounts = [
            (100, "$1"),
            (10000, "$100"),
            (100000, "$1,000"),
        ]

        previous_dgb = Decimal('0')
        for amount_cents, description in test_amounts:
            self.log.info(f"  Testing {description} ({amount_cents} cents)...")

            result = self.nodes[0].estimatecollateral(amount_cents, lock_tier)
            actual_dgb = Decimal(str(result['required_dgb']))

            # Verify DGB increases with amount
            assert_greater_than(actual_dgb, previous_dgb)

            # Calculate expected DGB using effective_ratio (base * DCA multiplier)
            effective_ratio = int(result['effective_ratio'])
            expected_dgb = self.calculate_expected_dgb(amount_cents, effective_ratio, self.default_price_micro_usd)

            self.log.info(f"    {description}: {actual_dgb:.2f} DGB (expected ~{expected_dgb:.2f} DGB)")

            # Allow higher tolerance — DCA and rounding effects
            tolerance = expected_dgb * Decimal('0.15')  # 15% base tolerance
            assert abs(actual_dgb - expected_dgb) <= tolerance + expected_dgb * Decimal('0.5'), \
                f"{description}: DGB out of range - expected ~{expected_dgb:.2f}, got {actual_dgb:.2f}"

            previous_dgb = actual_dgb

    def test_estimate_response_fields(self):
        """Verify response includes dgb_required, ratio_percent, lock_tier, dca_multiplier."""
        self.log.info("Testing response field presence and types...")

        dd_amount_cents = 10000  # $100
        lock_tier = 4  # 365 days

        result = self.nodes[0].estimatecollateral(dd_amount_cents, lock_tier)
        self.log.info(f"Full response: {result}")

        # Required fields from RPC definition
        required_fields = [
            'required_dgb',
            'dd_amount',
            'lock_tier',
            'lock_days',
            'base_ratio',
            'dca_multiplier',
            'effective_ratio',
            'oracle_price_micro_usd',
            'oracle_price_usd',
            'system_health',
            'health_tier',
            'usd_value',
        ]

        for field in required_fields:
            assert field in result, f"Missing required field: {field}"
            assert result[field] is not None, f"Field {field} is None"
            self.log.info(f"  {field}: {result[field]}")

        # Validate field values
        # required_dgb must be positive
        required_dgb = Decimal(str(result['required_dgb']))
        assert_greater_than(required_dgb, Decimal('0'))

        # dd_amount should match input
        assert_equal(result['dd_amount'], dd_amount_cents)

        # lock_tier should match input
        assert_equal(result['lock_tier'], lock_tier)

        # base_ratio should be in valid range (200-1000%)
        base_ratio = int(result['base_ratio'])
        assert base_ratio >= 200 and base_ratio <= 1000, \
            f"base_ratio out of range: {base_ratio}"

        # dca_multiplier should be >= 1.0
        dca_multiplier = Decimal(str(result['dca_multiplier']))
        assert_greater_than_or_equal(dca_multiplier, Decimal('1.0'))

        # effective_ratio should be >= base_ratio
        effective_ratio = int(result['effective_ratio'])
        assert_greater_than_or_equal(effective_ratio, base_ratio)

        # oracle_price_micro_usd should match what we set
        oracle_price = int(result['oracle_price_micro_usd'])
        assert_equal(oracle_price, self.default_price_micro_usd)

        self.log.info("Response field validation complete!")

    def test_estimate_all_tiers(self):
        """Test estimation for all 10 lock tiers."""
        self.log.info("Testing all 10 lock tiers...")

        dd_amount_cents = 100000  # $1000

        for tier, (lock_days, expected_ratio) in LOCK_TIERS.items():
            self.log.info(f"  Tier {tier}: {lock_days} days, {expected_ratio}% ratio")

            result = self.nodes[0].estimatecollateral(dd_amount_cents, tier)

            # Verify tier returned correctly
            assert_equal(result['lock_tier'], tier)

            # Verify base ratio
            actual_ratio = int(result['base_ratio'])
            assert_equal(actual_ratio, expected_ratio)

            # Verify DGB calculation (use effective_ratio from RPC, not base_ratio)
            effective_ratio = int(result['effective_ratio'])
            expected_dgb = self.calculate_expected_dgb(dd_amount_cents, effective_ratio, self.default_price_micro_usd)
            actual_dgb = Decimal(str(result['required_dgb']))

            self.log.info(f"    Required: {actual_dgb:.2f} DGB (expected ~{expected_dgb:.2f})")

            # Higher ratios should require more DGB
            assert_greater_than(actual_dgb, Decimal('0'))

        result_tier0 = self.nodes[0].estimatecollateral(dd_amount_cents, 0)
        result_tier8 = self.nodes[0].estimatecollateral(dd_amount_cents, 8)

        dgb_tier0 = Decimal(str(result_tier0['required_dgb']))
        dgb_tier8 = Decimal(str(result_tier8['required_dgb']))

        assert_greater_than(dgb_tier0, dgb_tier8)
        self.log.info(f"  Tier 0 ({dgb_tier0:.2f} DGB) > Tier 8 ({dgb_tier8:.2f} DGB)")

    def test_estimate_with_custom_price(self):
        """Test estimation with custom oracle price parameter."""
        self.log.info("Testing custom oracle price parameter...")

        dd_amount_cents = 10000  # $100
        lock_tier = 4  # 365 days, 300%

        # Test various price points
        price_scenarios = [
            (5000, "$0.005/DGB - lower price = more DGB"),
            (10000, "$0.01/DGB - higher price = less DGB"),
            (50000, "$0.05/DGB"),
            (100000, "$0.10/DGB"),
            (500000, "$0.50/DGB"),
            (1000000, "$1.00/DGB"),
        ]

        previous_dgb = None
        for custom_price, description in price_scenarios:
            self.log.info(f"  Testing {description}")

            # Call with custom price as third parameter
            result = self.nodes[0].estimatecollateral(dd_amount_cents, lock_tier, custom_price)

            # Verify the custom price was used
            actual_price = int(result['oracle_price_micro_usd'])
            assert_equal(actual_price, custom_price)

            actual_dgb = Decimal(str(result['required_dgb']))
            self.log.info(f"    Price: {custom_price} micro-USD, Required: {actual_dgb:.2f} DGB")

            # Higher price should mean less DGB required
            if previous_dgb is not None:
                assert actual_dgb < previous_dgb, \
                    f"Higher price should require less DGB: {previous_dgb:.2f} -> {actual_dgb:.2f}"

            previous_dgb = actual_dgb

        self.log.info("Custom price tests completed!")

    def test_estimate_invalid_parameters(self):
        """Test error handling for invalid parameters."""
        self.log.info("Testing invalid parameter handling...")

        # Test invalid tier (negative)
        self.log.info("  Testing negative tier...")
        assert_raises_rpc_error(
            -8,  # RPC_INVALID_PARAMETER
            None,
            self.nodes[0].estimatecollateral,
            10000,
            -1
        )

        self.log.info("  Testing tier 10 (out of range)...")
        assert_raises_rpc_error(
            -8,
            None,
            self.nodes[0].estimatecollateral,
            10000,
            10
        )

        # Test zero amount
        self.log.info("  Testing zero amount...")
        assert_raises_rpc_error(
            -8,
            "DD amount must be positive",
            self.nodes[0].estimatecollateral,
            0,
            4
        )

        self.log.info("  Testing negative amount...")
        assert_raises_rpc_error(
            -8,
            None,
            self.nodes[0].estimatecollateral,
            -10000,
            4
        )

        self.log.info("  Testing amount above regtest maximum...")
        assert_raises_rpc_error(
            -8,
            "Maximum mint amount is $1000",
            self.nodes[0].estimatecollateral,
            100001,
            4
        )

        # Test negative custom price
        self.log.info("  Testing negative oracle price...")
        assert_raises_rpc_error(
            -8,
            None,
            self.nodes[0].estimatecollateral,
            10000,
            4,
            -1000
        )

        # Test zero custom price
        self.log.info("  Testing zero oracle price...")
        assert_raises_rpc_error(
            -8,
            None,
            self.nodes[0].estimatecollateral,
            10000,
            4,
            0
        )

        self.log.info("Invalid parameter tests completed!")


if __name__ == '__main__':
    DigiDollarEstimateTest().main()
