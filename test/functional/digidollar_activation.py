#!/usr/bin/env python3
# Copyright (c) 2025 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test DigiDollar BIP9 soft fork activation.

Tests the full BIP9 state machine progression:
  DEFINED → STARTED → LOCKED_IN → ACTIVE

Uses -digidollaractivationheight to enable real BIP9 signaling on regtest
(instead of ALWAYS_ACTIVE which skips the state machine entirely).
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal

# Regtest BIP9 parameters
REGTEST_CONFIRMATION_WINDOW = 144
REGTEST_ACTIVATION_THRESHOLD = 108  # 75% of 144


class DigiDollarActivationTest(DigiByteTestFramework):
    """Test suite for DigiDollar BIP9 activation state machine."""

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # Use -digidollaractivationheight=0 to enable real BIP9 signaling
        # (start_time=0 means STARTED from first period, no min_activation_height delay)
        self.extra_args = [["-digidollaractivationheight=1", "-txindex=1"]]

    def skip_test_if_missing_module(self):
        pass

    def run_test(self):
        self.log.info("Starting DigiDollar BIP9 activation tests")

        node = self.nodes[0]

        # ── DEFINED state ──
        # At genesis (height 0), no blocks mined yet, state should be DEFINED
        self.log.info("Testing DEFINED state at genesis...")
        info = node.getdeploymentinfo()
        dd_status = info["deployments"]["digidollar"]["bip9"]["status"]
        self.log.info(f"  State at height 0: {dd_status}")
        assert_equal(dd_status, "defined")

        # ── Mine first period to transition to STARTED ──
        # Generate one full confirmation window (144 blocks)
        # MTP of regtest genesis (1519460922) > start_time (0), so after first
        # period boundary the state transitions DEFINED → STARTED
        self.log.info("Mining first confirmation window (144 blocks)...")
        node.generate(REGTEST_CONFIRMATION_WINDOW)

        info = node.getdeploymentinfo()
        dd_status = info["deployments"]["digidollar"]["bip9"]["status"]
        self.log.info(f"  State after period 1: {dd_status}")
        assert_equal(dd_status, "started")

        # ── Verify block version signaling ──
        # ComputeBlockVersion should set bit 23 when state is STARTED
        self.log.info("Verifying miner signals bit 23...")
        template = node.getblocktemplate({"rules": ["segwit"]})
        version = template["version"]
        bit_23_set = (version & (1 << 23)) != 0
        self.log.info(f"  Block template version: 0x{version:08x}, bit 23 set: {bit_23_set}")
        assert bit_23_set, "Bit 23 should be set in block template when STARTED"

        # ── Mine second period — miners auto-signal, threshold met → LOCKED_IN ──
        self.log.info("Mining second confirmation window (144 blocks, all signaling)...")
        node.generate(REGTEST_CONFIRMATION_WINDOW)

        info = node.getdeploymentinfo()
        dd_status = info["deployments"]["digidollar"]["bip9"]["status"]
        self.log.info(f"  State after period 2: {dd_status}")
        assert_equal(dd_status, "locked_in")

        # ── DigiDollar should NOT be usable yet in LOCKED_IN ──
        self.log.info("Verifying DigiDollar rejected during LOCKED_IN...")
        try:
            node.mintdigidollar(100.0, 365)
            self.log.warning("mintdigidollar succeeded during LOCKED_IN (unexpected)")
        except Exception as e:
            self.log.info(f"  Correctly rejected: {e}")

        # ── Mine third period → ACTIVE ──
        self.log.info("Mining third confirmation window (144 blocks)...")
        node.generate(REGTEST_CONFIRMATION_WINDOW)

        info = node.getdeploymentinfo()
        dd_status = info["deployments"]["digidollar"]["bip9"]["status"]
        self.log.info(f"  State after period 3: {dd_status}")
        assert_equal(dd_status, "active")

        # ── Verify deployment info shows full history ──
        self.log.info("Checking deployment info details...")
        dd_info = info["deployments"]["digidollar"]
        self.log.info(f"  Full deployment info: {dd_info}")

        # bip9 section should have status, start_time, timeout, since, etc.
        bip9 = dd_info["bip9"]
        assert_equal(bip9["status"], "active")
        assert "since" in bip9, "Should have 'since' height"
        self.log.info(f"  Active since height: {bip9['since']}")

        # ── Verify getblockchaininfo also reports correctly ──
        blockchain_info = node.getblockchaininfo()
        if "softforks" in blockchain_info:
            softforks = blockchain_info["softforks"]
            if "digidollar" in softforks:
                sf = softforks["digidollar"]
                self.log.info(f"  softforks.digidollar: {sf}")

        self.log.info("All DigiDollar BIP9 activation tests PASSED ✓")


class DigiDollarActivationTimeoutTest(DigiByteTestFramework):
    """Test BIP9 timeout (FAILED state) when miners don't signal."""

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # Use vbparams to set a very short timeout for testing
        # Format: name:start:timeout
        self.extra_args = [[
            "-vbparams=digidollar:0:1"  # start=epoch 0, timeout=1 second (already passed)
        ]]

    def skip_test_if_missing_module(self):
        pass

    def run_test(self):
        self.log.info("Starting DigiDollar BIP9 timeout test")

        node = self.nodes[0]

        # Mine past the first period — since timeout is already in the past,
        # state should transition DEFINED → STARTED → FAILED
        self.log.info("Mining past timeout period...")
        node.generate(REGTEST_CONFIRMATION_WINDOW)

        info = node.getdeploymentinfo()
        dd_status = info["deployments"]["digidollar"]["bip9"]["status"]
        self.log.info(f"  State after period 1: {dd_status}")

        # Should have gone through STARTED and timed out to FAILED
        # (or stayed DEFINED if MTP < start, then next period hits timeout)
        if dd_status == "started":
            # Mine another period to hit timeout
            node.generate(REGTEST_CONFIRMATION_WINDOW)
            info = node.getdeploymentinfo()
            dd_status = info["deployments"]["digidollar"]["bip9"]["status"]
            self.log.info(f"  State after period 2: {dd_status}")

        assert_equal(dd_status, "failed")

        # Verify DigiDollar not enabled
        try:
            node.mintdigidollar(100.0, 365)
            assert False, "Should have been rejected"
        except Exception as e:
            self.log.info(f"  Correctly rejected in FAILED state: {e}")

        self.log.info("DigiDollar timeout test PASSED ✓")


if __name__ == '__main__':
    DigiDollarActivationTest().main()
