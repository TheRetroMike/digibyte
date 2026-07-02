#!/usr/bin/env python3
"""Test DigiDollar DCA multiplier RPC command.

Test Dynamic Collateral Adjustment (DCA) functionality including:
- Default multiplier behavior based on system health
- Multiplier values at different health thresholds
- Custom health parameter testing
- Response format validation
- Integration with DigiDollar system

DCA Health Tiers:
- >= 150%: Healthy (multiplier 1.0)
- 120-149%: Warning (multiplier 1.25)
- 110-119%: Critical (multiplier 1.5)
- 0-109%: Emergency floor (multiplier 2.0)
- < 100%: Emergency (multiplier 2.0)
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
)
from decimal import Decimal


class DigiDollarDCAMultiplierTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # Enable DigiDollar features, disable Dandelion for testing
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing DigiDollar DCA multiplier RPC...")

        # Setup test environment
        self.setup_digidollar_test()

        # Run test scenarios
        self.test_dca_multiplier_default()
        self.test_dca_multiplier_healthy_system()
        self.test_dca_multiplier_warning_system()
        self.test_dca_multiplier_critical_system()
        self.test_dca_multiplier_emergency_system()
        self.test_dca_multiplier_with_custom_health()
        self.test_dca_multiplier_response_format()
        self.test_dca_multiplier_boundary_values()
        self.test_dca_multiplier_invalid_params()

        self.log.info("All DCA multiplier tests passed!")

    def setup_digidollar_test(self):
        """Setup test environment for DigiDollar."""
        self.log.info("Generating initial blocks for test setup...")
        self.generate(self.nodes[0], 110)

        # Set mock oracle price ($0.50 per DGB = 500,000 micro-USD)
        # Oracle price is in micro-USD: 1,000,000 micro-USD = $1.00
        self.log.info("Setting mock oracle price...")
        self.nodes[0].setmockoracleprice(500000)  # 500000 micro-USD = $0.50/DGB

        # Verify DigiDollar system is accessible
        stats = self.nodes[0].getdigidollarstats()
        assert "health_percentage" in stats or "system_collateral_ratio" in stats
        self.log.info("DigiDollar system initialized successfully")

    def test_dca_multiplier_default(self):
        """Test getdcamultiplier returns multiplier for current system health."""
        self.log.info("Testing DCA multiplier default behavior...")

        # Call without parameters - should use current system health
        result = self.nodes[0].getdcamultiplier()

        # Verify result is a valid response object
        assert result is not None, "getdcamultiplier should return a result"
        assert isinstance(result, dict), "Result should be a dictionary"

        # Verify multiplier is returned
        assert "multiplier" in result, "Result should contain 'multiplier' field"
        multiplier = float(result["multiplier"])

        # Multiplier must be >= 1.0 (minimum) and <= 2.0 (maximum as per current tiers)
        assert_greater_than_or_equal(multiplier, 1.0)
        assert multiplier <= 2.0, f"Multiplier should not exceed 2.0, got {multiplier}"

        self.log.info(f"Default multiplier: {multiplier}")

    def test_dca_multiplier_healthy_system(self):
        """Test multiplier is 1.0 when system health >= 150%."""
        self.log.info("Testing DCA multiplier for healthy system (>= 150%)...")

        # Test various healthy health levels
        healthy_levels = [150, 160, 175, 200, 250, 500, 1000]

        for health in healthy_levels:
            result = self.nodes[0].getdcamultiplier(health)

            multiplier = float(result["multiplier"])
            tier_status = result["tier_status"]

            assert_equal(multiplier, 1.0)
            assert_equal(tier_status, "healthy")

            self.log.info(f"Health {health}%: multiplier={multiplier}, tier={tier_status}")

    def test_dca_multiplier_warning_system(self):
        """Test multiplier increases for 120-149% health (warning tier)."""
        self.log.info("Testing DCA multiplier for warning system (120-149%)...")

        # Test warning tier health levels
        warning_levels = [120, 125, 135, 140, 149]

        for health in warning_levels:
            result = self.nodes[0].getdcamultiplier(health)

            multiplier = float(result["multiplier"])
            tier_status = result["tier_status"]

            # Warning tier should have 1.25x multiplier
            assert_equal(multiplier, 1.25)
            assert_equal(tier_status, "warning")

            self.log.info(f"Health {health}%: multiplier={multiplier}, tier={tier_status}")

    def test_dca_multiplier_critical_system(self):
        """Test multiplier at critical level (110-119% health)."""
        self.log.info("Testing DCA multiplier for critical system (110-119%)...")

        # Test critical tier health levels
        critical_levels = [110, 115, 119]

        for health in critical_levels:
            result = self.nodes[0].getdcamultiplier(health)

            multiplier = float(result["multiplier"])
            tier_status = result["tier_status"]

            # Critical tier should have 1.5x multiplier
            assert_equal(multiplier, 1.5)
            assert_equal(tier_status, "critical")

            self.log.info(f"Health {health}%: multiplier={multiplier}, tier={tier_status}")

    def test_dca_multiplier_emergency_system(self):
        """Test multiplier at maximum (2.0x) when health < 100%."""
        self.log.info("Testing DCA multiplier for emergency system (< 100%)...")

        # Test emergency tier health levels
        emergency_levels = [0, 25, 50, 75, 90, 99]

        for health in emergency_levels:
            result = self.nodes[0].getdcamultiplier(health)

            multiplier = float(result["multiplier"])
            tier_status = result["tier_status"]

            # Emergency tier should have 2.0x multiplier
            assert_equal(multiplier, 2.0)
            assert_equal(tier_status, "emergency")

            self.log.info(f"Health {health}%: multiplier={multiplier}, tier={tier_status}")

    def test_dca_multiplier_with_custom_health(self):
        """Test getdcamultiplier with explicit health parameter."""
        self.log.info("Testing DCA multiplier with custom health parameter...")

        # Test specific health values and verify expected multipliers
        test_cases = [
            # (health, expected_multiplier, expected_tier)
            (200, 1.0, "healthy"),
            (150, 1.0, "healthy"),
            (149, 1.25, "warning"),
            (120, 1.25, "warning"),
            (119, 1.5, "critical"),
            (110, 1.5, "critical"),
            (109, 2.0, "emergency"),
            (100, 2.0, "emergency"),
            (99, 2.0, "emergency"),
            (50, 2.0, "emergency"),
            (0, 2.0, "emergency"),
        ]

        for health, expected_multiplier, expected_tier in test_cases:
            result = self.nodes[0].getdcamultiplier(health)

            multiplier = float(result["multiplier"])
            tier_status = result["tier_status"]
            returned_health = result["system_health"]

            # Verify the health parameter was used
            assert_equal(returned_health, health)
            assert_equal(multiplier, expected_multiplier)
            assert_equal(tier_status, expected_tier)

            self.log.info(f"Custom health {health}%: multiplier={multiplier}, tier={tier_status}")

    def test_dca_multiplier_response_format(self):
        """Test that getdcamultiplier response includes all required fields."""
        self.log.info("Testing DCA multiplier response format...")

        result = self.nodes[0].getdcamultiplier()

        # Verify all required fields are present
        required_fields = ["multiplier", "system_health", "tier_status", "description"]

        for field in required_fields:
            assert field in result, f"Missing required field: {field}"
            assert result[field] is not None, f"Field '{field}' should not be null"

        # Verify field types
        assert isinstance(result["multiplier"], (int, float)), "multiplier should be numeric"
        assert isinstance(result["system_health"], int), "system_health should be integer"
        assert isinstance(result["tier_status"], str), "tier_status should be string"
        assert isinstance(result["description"], str), "description should be string"

        # Verify tier_status is valid
        valid_tiers = ["healthy", "warning", "critical", "emergency"]
        assert result["tier_status"] in valid_tiers, f"Invalid tier_status: {result['tier_status']}"

        # Verify description is meaningful
        assert len(result["description"]) > 0, "description should not be empty"

        self.log.info(f"Response format valid: {result}")

    def test_dca_multiplier_boundary_values(self):
        """Test DCA multiplier at tier boundary values."""
        self.log.info("Testing DCA multiplier at tier boundaries...")

        # Test exact boundary values
        boundary_tests = [
            # Lower boundary of healthy (>=150)
            (150, 1.0, "healthy"),
            (151, 1.0, "healthy"),
            # Upper boundary of warning (149) and lower (120)
            (149, 1.25, "warning"),
            (120, 1.25, "warning"),
            # Upper boundary of critical (119) and lower (100)
            (119, 1.5, "critical"),
            (110, 1.5, "critical"),
            (109, 2.0, "emergency"),
            (100, 2.0, "emergency"),
            # Upper boundary of emergency (99)
            (99, 2.0, "emergency"),
        ]

        for health, expected_multiplier, expected_tier in boundary_tests:
            result = self.nodes[0].getdcamultiplier(health)
            multiplier = float(result["multiplier"])
            tier_status = result["tier_status"]

            assert_equal(multiplier, expected_multiplier)
            assert_equal(tier_status, expected_tier)

            self.log.info(f"Boundary {health}%: multiplier={multiplier}, tier={tier_status}")

        # Test extreme values within valid range
        extreme_tests = [
            (0, 2.0, "emergency"),      # Minimum valid health
            (30000, 1.0, "healthy"),    # Maximum valid health (as per RPC validation)
        ]

        for health, expected_multiplier, expected_tier in extreme_tests:
            result = self.nodes[0].getdcamultiplier(health)
            multiplier = float(result["multiplier"])
            tier_status = result["tier_status"]

            assert_equal(multiplier, expected_multiplier)
            assert_equal(tier_status, expected_tier)

            self.log.info(f"Extreme {health}%: multiplier={multiplier}, tier={tier_status}")

    def test_dca_multiplier_invalid_params(self):
        """Test getdcamultiplier error handling for invalid parameters."""
        self.log.info("Testing DCA multiplier invalid parameter handling...")

        # Test negative health value
        assert_raises_rpc_error(
            -8,  # RPC_INVALID_PARAMETER
            "System health must be between 0 and 30000",
            self.nodes[0].getdcamultiplier,
            -10
        )

        # Test health value exceeding maximum
        assert_raises_rpc_error(
            -8,  # RPC_INVALID_PARAMETER
            "System health must be between 0 and 30000",
            self.nodes[0].getdcamultiplier,
            40000
        )

        self.log.info("Invalid parameter tests passed")


if __name__ == '__main__':
    DigiDollarDCAMultiplierTest().main()
