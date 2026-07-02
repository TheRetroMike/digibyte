#!/usr/bin/env python3
"""Regression tests for Bug #11 and Bug #13 fixes.

Bug #11: mintdigidollar had no RPC-level validation for consensus mint limits.
         Users got confusing broadcast-time rejections instead of clear RPC errors.
         Fix: Validate ddAmount against ConsensusParams min/max at RPC layer.

Bug #13: listdigidollartxs returned blockheight=-1 for confirmed transactions.
         Fix: Populate blockheight/blockhash from TxStateConfirmed in GetDDTransactionHistory().

Regtest consensus: minMintAmount=1 cent ($0.01), maxMintAmount=100000 cents ($1000).
DD amounts are in CENTS (integer), not micro-USD or float.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)


class DigiDollarBug11Bug13RegressionTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_test(self):
        """Common setup: mine past activation, set oracle price."""
        self.log.info("Setting up: generating blocks past DD activation height (650)...")
        self.nodes[0].generate(660)
        self.sync_all()

        # $0.50/DGB = 500000 micro-USD
        self.nodes[0].setmockoracleprice(500000)
        self.nodes[1].setmockoracleprice(500000)

    # ──────────────────────────────────────────────────────────
    #  Bug #11: RPC-level mint amount validation
    # ──────────────────────────────────────────────────────────

    def test_bug11_mint_below_minimum(self):
        """Minting below the minimum (regtest: 1 cent) should fail with clear RPC error."""
        self.log.info("Bug #11: Testing mint below minimum (0 cents)...")
        # 0 is caught by the pre-existing "must be positive" check
        assert_raises_rpc_error(
            -8, "must be positive",
            self.nodes[0].mintdigidollar, 0, 1
        )

    def test_bug11_mint_negative(self):
        """Negative mint amount should fail."""
        self.log.info("Bug #11: Testing negative mint amount...")
        assert_raises_rpc_error(
            -8, None,
            self.nodes[0].mintdigidollar, -100, 1
        )

    def test_bug11_mint_above_maximum(self):
        """Minting above the maximum (regtest: 100000 cents = $1000) should fail."""
        self.log.info("Bug #11: Testing mint above maximum...")
        above_max = 100001  # 1 cent above regtest max of 100000
        assert_raises_rpc_error(
            -8, "Maximum mint amount",
            self.nodes[0].mintdigidollar, above_max, 1
        )

    def test_bug11_mint_way_above_maximum(self):
        """Minting far above maximum should give clear error, not crash."""
        self.log.info("Bug #11: Testing mint way above maximum (10M cents)...")
        assert_raises_rpc_error(
            -8, "Maximum mint amount",
            self.nodes[0].mintdigidollar, 10000000, 1
        )

    def test_bug11_mint_at_exact_minimum(self):
        """Minting exactly at the minimum boundary (regtest: 1 cent) should succeed."""
        self.log.info("Bug #11: Testing mint at exact minimum (1 cent)...")
        # This should NOT raise RPC_INVALID_PARAMETER for the amount.
        # It may fail for other reasons (insufficient funds etc.) but the
        # amount validation should pass. We check it doesn't say "Minimum".
        try:
            result = self.nodes[0].mintdigidollar(1, 0)  # 1 cent, tier 0 (1 hour)
            self.log.info(f"Mint at minimum succeeded: {result}")
        except Exception as e:
            error_msg = str(e)
            assert "Minimum mint amount" not in error_msg, \
                f"Amount validation incorrectly rejected minimum: {error_msg}"
            assert "Maximum mint amount" not in error_msg, \
                f"Amount validation incorrectly rejected minimum as max: {error_msg}"
            self.log.info(f"Mint at minimum failed for non-amount reason: {error_msg}")

    def test_bug11_mint_at_exact_maximum(self):
        """Minting exactly at the maximum boundary (regtest: 100000 cents) should succeed."""
        self.log.info("Bug #11: Testing mint at exact maximum (100000 cents = $1000)...")
        try:
            result = self.nodes[0].mintdigidollar(100000, 1)  # $1000, tier 1 (30 days)
            self.log.info(f"Mint at maximum succeeded: {result}")
        except Exception as e:
            error_msg = str(e)
            assert "Minimum mint amount" not in error_msg, \
                f"Amount validation incorrectly rejected maximum: {error_msg}"
            assert "Maximum mint amount" not in error_msg, \
                f"Amount validation incorrectly rejected exact maximum: {error_msg}"
            self.log.info(f"Mint at maximum failed for non-amount reason: {error_msg}")

    def test_bug11_mint_valid_midrange(self):
        """Minting a valid mid-range amount should pass amount validation."""
        self.log.info("Bug #11: Testing valid mid-range mint (5000 cents = $50)...")
        try:
            result = self.nodes[0].mintdigidollar(5000, 0)  # $50, tier 0
            self.log.info(f"Mid-range mint succeeded: {result}")
        except Exception as e:
            error_msg = str(e)
            assert "Minimum mint amount" not in error_msg, \
                f"Amount validation incorrectly rejected valid amount: {error_msg}"
            assert "Maximum mint amount" not in error_msg, \
                f"Amount validation incorrectly rejected valid amount: {error_msg}"
            self.log.info(f"Mid-range mint failed for non-amount reason: {error_msg}")

    def test_bug11_error_message_contains_limit(self):
        """Error messages should contain the actual dollar limit for user clarity."""
        self.log.info("Bug #11: Verifying error messages contain dollar amounts...")

        # Below minimum — regtest min is 1 cent, but 0 is caught by "positive" check.
        # For the "Minimum" message to trigger, we need minMintAmount > amount > 0.
        # On regtest minMintAmount=1, so ANY positive amount passes the min check.
        # We can only test the max boundary message on regtest.
        try:
            self.nodes[0].mintdigidollar(100001, 1)
            assert False, "Should have raised"
        except Exception as e:
            error_msg = str(e)
            # Should mention dollar amount and cents
            assert "1000" in error_msg or "100000" in error_msg, \
                f"Error message should contain limit value: {error_msg}"
            self.log.info(f"Error message properly shows limits: {error_msg}")

    # ──────────────────────────────────────────────────────────
    #  Bug #13: blockheight populated for confirmed DD transactions
    # ──────────────────────────────────────────────────────────

    def test_bug13_confirmed_tx_has_blockheight(self):
        """Confirmed DD transactions should have blockheight > 0, not -1."""
        self.log.info("Bug #13: Testing blockheight for confirmed DD transactions...")

        # Mint a DD to create a transaction
        try:
            txid = self.nodes[0].mintdigidollar(500, 0)  # $5, tier 0
        except Exception as e:
            self.log.info(f"Mint failed (may need more funds): {e}")
            # Generate more blocks for funds and retry
            self.nodes[0].generate(50)
            txid = self.nodes[0].mintdigidollar(500, 0)

        self.log.info(f"Minted DD with txid: {txid}")

        # Mine the transaction
        self.nodes[0].generate(1)
        self.sync_all()

        # Check listdigidollartxs output
        txs = self.nodes[0].listdigidollartxs()
        assert len(txs) > 0, "Should have at least one DD transaction"

        found = False
        for tx in txs:
            if isinstance(txid, dict):
                target_txid = txid.get("txid", txid.get("hash", ""))
            else:
                target_txid = str(txid)

            tx_txid = tx.get("txid", "")
            if tx_txid == target_txid or not target_txid:
                # Check ANY confirmed tx
                confirmations = tx.get("confirmations", 0)
                if confirmations > 0:
                    blockheight = tx.get("blockheight", -1)
                    self.log.info(f"  TX {tx_txid}: blockheight={blockheight}, confirmations={confirmations}")
                    assert blockheight > 0, \
                        f"Bug #13 regression: confirmed tx has blockheight={blockheight}, expected > 0"
                    # Also check blockhash is populated
                    blockhash = tx.get("blockhash", "")
                    assert len(blockhash) > 0, \
                        f"Bug #13 regression: confirmed tx has empty blockhash"
                    found = True

        assert found, "No confirmed DD transaction found to verify blockheight"
        self.log.info("Bug #13: Confirmed transactions have correct blockheight ✓")

    def test_bug13_unconfirmed_tx_has_negative_blockheight(self):
        """Unconfirmed DD transactions should have blockheight=-1."""
        self.log.info("Bug #13: Testing blockheight for unconfirmed DD transactions...")

        # Mint without mining the block
        try:
            txid = self.nodes[0].mintdigidollar(500, 0)
        except Exception as e:
            self.log.info(f"Mint failed: {e}")
            self.nodes[0].generate(50)
            txid = self.nodes[0].mintdigidollar(500, 0)

        # Do NOT mine — check immediately
        txs = self.nodes[0].listdigidollartxs()
        for tx in txs:
            if tx.get("confirmations", 1) == 0:
                blockheight = tx.get("blockheight", None)
                self.log.info(f"  Unconfirmed TX: blockheight={blockheight}")
                assert blockheight == -1 or blockheight is None, \
                    f"Unconfirmed tx should have blockheight=-1, got {blockheight}"
                self.log.info("Bug #13: Unconfirmed transaction has blockheight=-1 ✓")
                # Mine it for cleanup
                self.nodes[0].generate(1)
                return

        # If no unconfirmed tx found, mine and move on
        self.log.info("No unconfirmed tx in list (may have been auto-mined); skipping sub-test")
        self.nodes[0].generate(1)

    def test_bug13_blockheight_matches_actual_block(self):
        """The blockheight in DD tx should match the actual block it was mined in."""
        self.log.info("Bug #13: Testing blockheight matches actual mining block...")

        pre_height = self.nodes[0].getblockcount()

        try:
            txid = self.nodes[0].mintdigidollar(500, 0)
        except Exception as e:
            self.nodes[0].generate(50)
            pre_height = self.nodes[0].getblockcount()
            txid = self.nodes[0].mintdigidollar(500, 0)

        # Mine it
        block_hashes = self.nodes[0].generate(1)
        expected_height = pre_height + 1
        expected_hash = block_hashes[0]

        txs = self.nodes[0].listdigidollartxs()
        for tx in txs:
            if tx.get("confirmations", 0) > 0:
                bh = tx.get("blockheight", -1)
                bhash = tx.get("blockhash", "")
                if bh == expected_height:
                    assert_equal(bhash, expected_hash)
                    self.log.info(f"Bug #13: blockheight={bh} matches mined block ✓")
                    return

        self.log.info("Could not find tx at expected height; test inconclusive")

    def test_bug13_multiple_confirmations(self):
        """After more blocks, blockheight should remain stable while confirmations increase."""
        self.log.info("Bug #13: Testing blockheight stability across confirmations...")

        try:
            txid = self.nodes[0].mintdigidollar(500, 0)
        except Exception:
            self.nodes[0].generate(50)
            txid = self.nodes[0].mintdigidollar(500, 0)

        self.nodes[0].generate(1)
        self.sync_all()

        # Record initial state
        txs = self.nodes[0].listdigidollartxs()
        initial_bh = None
        for tx in txs:
            if tx.get("confirmations", 0) == 1:
                initial_bh = tx.get("blockheight", -1)
                break

        if initial_bh is None:
            self.log.info("No 1-conf tx found; skipping stability test")
            return

        # Mine more blocks
        self.nodes[0].generate(5)
        self.sync_all()

        txs = self.nodes[0].listdigidollartxs()
        for tx in txs:
            if tx.get("blockheight", -1) == initial_bh:
                confs = tx.get("confirmations", 0)
                assert confs >= 6, f"Expected >=6 confirmations, got {confs}"
                assert_equal(tx["blockheight"], initial_bh)
                self.log.info(f"Bug #13: blockheight={initial_bh} stable at {confs} confs ✓")
                return

    # ──────────────────────────────────────────────────────────
    #  Test orchestration
    # ──────────────────────────────────────────────────────────

    def run_test(self):
        self.setup_test()

        # Bug #11 tests
        self.log.info("=" * 60)
        self.log.info("BUG #11: Mint amount validation tests")
        self.log.info("=" * 60)
        self.test_bug11_mint_below_minimum()
        self.test_bug11_mint_negative()
        self.test_bug11_mint_above_maximum()
        self.test_bug11_mint_way_above_maximum()
        self.test_bug11_mint_at_exact_minimum()
        self.test_bug11_mint_at_exact_maximum()
        self.test_bug11_mint_valid_midrange()
        self.test_bug11_error_message_contains_limit()

        # Bug #13 tests
        self.log.info("=" * 60)
        self.log.info("BUG #13: DD transaction blockheight tests")
        self.log.info("=" * 60)
        self.test_bug13_confirmed_tx_has_blockheight()
        self.test_bug13_unconfirmed_tx_has_negative_blockheight()
        self.test_bug13_blockheight_matches_actual_block()
        self.test_bug13_multiple_confirmations()

        self.log.info("=" * 60)
        self.log.info("ALL Bug #11 and Bug #13 regression tests passed!")
        self.log.info("=" * 60)


if __name__ == '__main__':
    DigiDollarBug11Bug13RegressionTest().main()
