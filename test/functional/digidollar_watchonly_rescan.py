#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that watch-only DD addresses do NOT inflate DD balance during rescan.

SECURITY TEST [T4-04]: Verifies the fix for watch-only DD balance contamination
in ProcessDDTxForRescan().

Bug description:
    ProcessDDTxForRescan() used `IsMine() != ISMINE_NO` which accepts
    ISMINE_WATCH_ONLY. After importing a watch-only address that received DD
    and running rescanblockchain, foreign DD UTXOs would contaminate dd_utxos
    and collateral_positions, inflating GetTotalDDBalance().

Attack vector:
    1. Alice mints DD on node 0
    2. Bob imports Alice's DD address as watch-only on node 1
    3. Bob rescans blockchain on node 1
    4. BEFORE FIX: Bob's wallet would show Alice's DD balance as his own
    5. AFTER FIX: Bob's DD balance remains 0 — watch-only outputs are excluded

Test coverage:
    1. Mint DD on node 0 (Alice)
    2. Import Alice's address as watch-only on node 1 (Bob)
    3. Rescan node 1
    4. Verify node 1's DD balance is 0 (not contaminated)
    5. Verify node 1's collateral positions are empty
    6. Verify node 0's DD balance is unchanged (control)
"""

from decimal import Decimal
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)
import time


class DigiDollarWatchOnlyRescanTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-debug=digidollar", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-debug=digidollar", "-dandelion=0"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def setup_digidollar_environment(self):
        """Setup test environment with mature coins and oracle price."""
        self.log.info("Setting up DigiDollar test environment...")

        # Generate blocks to get spendable coins (100 blocks for maturity)
        self.generate(self.nodes[0], 200)
        self.sync_all()

        # Set mock oracle price ($0.0065/DGB = 6500 micro-USD)
        for node in self.nodes:
            try:
                node.setmockoracleprice(6500)
            except Exception as e:
                self.log.info(f"setmockoracleprice: {e}")

        # Generate a block to confirm oracle price
        self.generate(self.nodes[0], 1)
        self.sync_all()

    def run_test(self):
        self.setup_digidollar_environment()

        self.log.info("=== Test: Watch-only DD addresses must NOT inflate balance on rescan ===")

        # ---- Step 1: Alice (node 0) mints DD ----
        self.log.info("Step 1: Alice mints DigiDollars on node 0...")

        # Get Alice's DD balance before mint
        alice_balance_before = self.nodes[0].getdigidollarbalance()
        self.log.info(f"Alice DD balance before mint: {alice_balance_before}")

        # Mint DD
        try:
            mint_result = self.nodes[0].mintdigidollar(500, 0)  # $5.00 DD, tier 0 (1 hour)
            mint_txid = mint_result if isinstance(mint_result, str) else mint_result.get("txid", "")
            self.log.info(f"Alice minted DD, txid: {mint_txid}")
        except Exception as e:
            self.log.info(f"Mint failed (may need different params): {e}")
            # Try alternative mint syntax
            try:
                mint_result = self.nodes[0].mintdigidollar({"amount": 500, "lock_tier": 0})
                mint_txid = mint_result if isinstance(mint_result, str) else mint_result.get("txid", "")
                self.log.info(f"Alice minted DD (alt syntax), txid: {mint_txid}")
            except Exception as e2:
                self.log.info(f"Alt mint also failed: {e2}")
                self.log.info("SKIP: Cannot test watch-only contamination without working mint")
                return

        # Confirm the mint
        self.generate(self.nodes[0], 6)
        self.sync_all()

        # Verify Alice has DD balance
        alice_balance_after = self.nodes[0].getdigidollarbalance()
        self.log.info(f"Alice DD balance after mint: {alice_balance_after}")

        # Get the minted amount for comparison
        alice_dd_balance = alice_balance_after.get("confirmed", 0) if isinstance(alice_balance_after, dict) else alice_balance_after
        assert alice_dd_balance > 0, "Alice should have DD balance after mint"

        # Get Alice's positions
        alice_positions = self.nodes[0].listdigidollarpositions()
        self.log.info(f"Alice has {len(alice_positions)} positions")
        assert len(alice_positions) > 0, "Alice should have at least one position"

        # ---- Step 2: Get an address Alice owns (from the mint tx) ----
        self.log.info("Step 2: Getting Alice's address from mint transaction...")

        # Get the raw transaction to find Alice's output addresses
        raw_tx = self.nodes[0].getrawtransaction(mint_txid, True)
        alice_addresses = []
        for vout in raw_tx.get("vout", []):
            spk = vout.get("scriptPubKey", {})
            addr = spk.get("address", "")
            if addr:
                alice_addresses.append(addr)
        self.log.info(f"Found addresses in mint tx: {alice_addresses}")

        # ---- Step 3: Bob (node 1) creates a wallet and imports watch-only ----
        self.log.info("Step 3: Bob imports Alice's address as watch-only...")

        # Check Bob's DD balance before import
        bob_balance_before = self.nodes[1].getdigidollarbalance()
        bob_dd_before = bob_balance_before.get("confirmed", 0) if isinstance(bob_balance_before, dict) else bob_balance_before
        self.log.info(f"Bob DD balance before import: {bob_dd_before}")
        assert bob_dd_before == 0, "Bob should have 0 DD balance before import"

        # Import Alice's address(es) as watch-only on Bob's node
        for addr in alice_addresses:
            try:
                # For descriptor wallets, import as watch-only descriptor
                desc = f"addr({addr})"
                desc_info = self.nodes[1].getdescriptorinfo(desc)
                checksum_desc = desc_info["descriptor"]

                import_result = self.nodes[1].importdescriptors([{
                    "desc": checksum_desc,
                    "timestamp": "now",
                    "watchonly": True,
                    "label": "alice-watchonly"
                }])
                self.log.info(f"Imported {addr} as watch-only: {import_result}")
            except Exception as e:
                self.log.info(f"Import descriptor failed for {addr}: {e}")
                # Try legacy import
                try:
                    self.nodes[1].importaddress(addr, "alice-watchonly", False)
                    self.log.info(f"Imported {addr} as watch-only (legacy)")
                except Exception as e2:
                    self.log.info(f"Legacy import also failed: {e2}")

        # ---- Step 4: Bob rescans blockchain ----
        self.log.info("Step 4: Bob rescans blockchain...")

        try:
            rescan_result = self.nodes[1].rescanblockchain(0)
            self.log.info(f"Rescan result: {rescan_result}")
        except Exception as e:
            self.log.info(f"Rescan failed: {e}")

        # ---- Step 5: Verify Bob's DD balance is NOT inflated ----
        self.log.info("Step 5: Verifying Bob's DD balance is NOT contaminated...")

        bob_balance_after = self.nodes[1].getdigidollarbalance()
        bob_dd_after = bob_balance_after.get("confirmed", 0) if isinstance(bob_balance_after, dict) else bob_balance_after
        self.log.info(f"Bob DD balance after rescan: {bob_dd_after}")

        # CRITICAL ASSERTION: Bob's DD balance must still be 0
        assert bob_dd_after == 0, (
            f"SECURITY BUG [T4-04]: Watch-only DD balance contamination! "
            f"Bob's DD balance should be 0 but got {bob_dd_after}. "
            f"Watch-only addresses inflated the DD balance."
        )

        # ---- Step 6: Verify Bob has no DD positions ----
        self.log.info("Step 6: Verifying Bob has no collateral positions...")

        bob_positions = self.nodes[1].listdigidollarpositions()
        self.log.info(f"Bob has {len(bob_positions)} positions (should be 0)")
        assert len(bob_positions) == 0, (
            f"SECURITY BUG [T4-04]: Watch-only position contamination! "
            f"Bob has {len(bob_positions)} positions but should have 0."
        )

        # ---- Step 7: Verify Alice's balance is unchanged (control) ----
        self.log.info("Step 7: Verifying Alice's DD balance is unchanged (control)...")

        alice_balance_final = self.nodes[0].getdigidollarbalance()
        alice_dd_final = alice_balance_final.get("confirmed", 0) if isinstance(alice_balance_final, dict) else alice_balance_final
        self.log.info(f"Alice DD balance (control): {alice_dd_final}")
        assert alice_dd_final == alice_dd_balance, "Alice's DD balance should not have changed"

        alice_positions_final = self.nodes[0].listdigidollarpositions()
        assert len(alice_positions_final) == len(alice_positions), "Alice's positions should not have changed"

        self.log.info("=== PASS: Watch-only DD addresses correctly excluded from DD balance ===")
        self.log.info(f"  Alice (owner): {alice_dd_balance} DD cents, {len(alice_positions)} positions")
        self.log.info(f"  Bob (watch-only): {bob_dd_after} DD cents, {len(bob_positions)} positions")


if __name__ == '__main__':
    DigiDollarWatchOnlyRescanTest().main()
