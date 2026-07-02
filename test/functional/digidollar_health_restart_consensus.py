#!/usr/bin/env python3
"""Regression for DD-FINAL-003 (AR-CONSENSUS-1): DigiDollar system health must
survive a node restart.

Consensus DCA/ERR health is derived from SystemHealthMonitor::s_currentMetrics
(total DD supply + total collateral). Before the fix those metrics were only
accumulated incrementally while the process ran and were NEVER reconstructed at
startup, so a freshly-restarted node saw total_dd_supply == 0 and treated the
system as maximally healthy (300%) regardless of the true on-chain state. A
continuously-running node held the true (stressed) health. The two then
disagreed on ERR minting blocks / DCA collateral for the same block -> split.

Observable: when true system health < 100%, ERR blocks new minting
(ERR::ShouldBlockMinting reads the same cached metrics consensus uses, via the
mint RPC). A node that forgets its DD supply on restart wrongly UNBLOCKS
minting. We stress the system below 100% health, let the volatility-freeze
window (1h, time-based) age out so ERR is the sole mint blocker, and assert
that a mint blocked before a restart stays blocked after the restart.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_raises_rpc_error


class DigiDollarHealthRestartConsensusTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def advance(self, seconds, blocks=2):
        """Advance mock time and mine a few blocks so the price history records
        the current (stable) oracle price at the new timestamps."""
        self.t += seconds
        self.nodes[0].setmocktime(self.t)
        self.nodes[0].generate(blocks)

    def stabilize_and_assert_blocked(self, price, label):
        """With a stable low price and a settled 1h volatility window, ERR must
        be the reason minting is blocked (not the volatility freeze)."""
        node = self.nodes[0]
        node.setmockoracleprice(price)
        # Age the big price drop out of the 1-hour volatility window and fill
        # the window with the stable low price.
        for _ in range(8):
            self.advance(1200, blocks=2)  # +20 min * 8 = +160 min of stable price
        self.log.info(f"{label}: expecting ERR (emergency state) to block minting")
        assert_raises_rpc_error(-1, "emergency state", node.mintdigidollar, 10000, 0)

    def run_test(self):
        node = self.nodes[0]
        self.t = 1700000000
        node.setmocktime(self.t)
        base_price = 50000  # ~$0.50 / DGB

        # --- Build real on-chain DD supply at a healthy price ---
        node.generate(150)  # past coinbase maturity + DD activation height
        node.setmockoracleprice(base_price)
        self.advance(60, blocks=2)
        for _ in range(3):
            res = node.mintdigidollar(100000, 4)  # $1000 each, tier 4
            node.generate(1)
            assert res["txid"] in node.getblock(node.getbestblockhash())["tx"]
            self.advance(60, blocks=1)

        # --- Stress below 100% health, settle volatility, confirm ERR blocks ---
        err_price = 9000  # ~$0.09 / DGB -> locked collateral devalued -> < 100%
        self.stabilize_and_assert_blocked(err_price, "Pre-restart")

        # --- Restart the node (no reindex) ---
        self.log.info("Restarting node (no reindex) ...")
        self.restart_node(0, extra_args=self.extra_args[0])
        node.setmocktime(self.t)

        # --- Regression: minting must STILL be blocked after restart ---
        # Pre-fix the restarted node forgot its DD supply (total_dd_supply == 0
        # -> ShouldBlockMinting returns false), so this mint would SUCCEED and
        # the assertion would fail. With startup health reconstruction the
        # supply is restored and ERR still blocks the mint.
        self.stabilize_and_assert_blocked(err_price, "Post-restart")

        self.log.info("PASS: DigiDollar system health survives restart; "
                      "no DCA/ERR consensus divergence across restart.")


if __name__ == '__main__':
    DigiDollarHealthRestartConsensusTest().main()
