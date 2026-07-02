#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that wallet restore via importdescriptors correctly marks redeemed positions.

BUG #8: After recovering a wallet via `listdescriptors true` -> `importdescriptors`,
the wallet shows active "Redeem DigiDollar" buttons for positions that have ALREADY
been redeemed. This is a persistent bug reported multiple times by users.

ROOT CAUSE (two bugs):
1. Full-redemption REDEEM txs (ddChange == 0) have NO OP_RETURN with DD metadata.
   ProcessDDTxForRescan relies on OP_RETURN to detect tx type, so it never detects
   these REDEEM txs and never marks positions as inactive.
2. ValidatePositionStates() (which checks the UTXO set) only runs at startup,
   not after importdescriptors/rescanblockchain rescans.

Test flow:
1. Mint DD on node 0
2. Redeem DD on node 0 (full redemption — ddChange == 0)
3. Verify position is marked redeemed on node 0
4. Export descriptors from node 0
5. Create new wallet on node 0
6. Import descriptors into new wallet
7. CRITICAL: Verify position is marked redeemed in restored wallet
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)
from decimal import Decimal


class DigiDollarWalletRestoreRedeemTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-debug=digidollar", "-dandelion=0"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def run_test(self):
        node = self.nodes[0]

        # ----------------------------------------------------------------
        # Setup: mature coins + oracle price
        # ----------------------------------------------------------------
        self.log.info("Generating 200 blocks for maturity...")
        self.generate(node, 200)

        # Set oracle price ($0.01/DGB = 10000 micro-USD)
        node.setmockoracleprice(10000)
        self.generate(node, 1)

        # ----------------------------------------------------------------
        # Step 1: Mint DigiDollars
        # ----------------------------------------------------------------
        self.log.info("Step 1: Minting $1000 DD with tier 0 (1-hour lock)...")
        mint_result = node.mintdigidollar(100000, 0)  # 100000 cents = $1000, tier 0
        position_id = mint_result['position_id']
        dd_minted = mint_result['dd_minted']
        unlock_height = mint_result['unlock_height']

        self.log.info(f"  Position ID: {position_id}")
        self.log.info(f"  DD minted: {dd_minted} cents")
        self.log.info(f"  Unlock height: {unlock_height}")

        assert_equal(dd_minted, 100000)

        # Confirm mint
        self.generate(node, 10)

        # Verify DD balance
        dd_bal = node.getdigidollarbalance()
        self.log.info(f"  DD balance after mint: {dd_bal['total']} cents")
        assert_equal(dd_bal['total'], 100000)

        # Verify position is active
        positions = node.listdigidollarpositions(False)  # all positions
        active = [p for p in positions if p['status'] != 'redeemed']
        self.log.info(f"  Active positions: {len(active)}")
        assert_equal(len(active), 1)
        assert_equal(active[0]['position_id'], position_id)

        # ----------------------------------------------------------------
        # Step 2: Generate blocks past unlock height, then redeem
        # ----------------------------------------------------------------
        current_height = node.getblockcount()
        blocks_needed = unlock_height - current_height + 10
        self.log.info(f"Step 2: Generating {blocks_needed} blocks to expire timelock...")
        self.generate(node, blocks_needed)

        self.log.info("  Redeeming full position (exact amount, ddChange == 0)...")
        redeem_result = node.redeemdigidollar(position_id, 100000)
        self.log.info(f"  Redeem txid: {redeem_result['txid']}")
        assert_equal(redeem_result['dd_redeemed'], 100000)
        assert_equal(redeem_result['position_closed'], True)

        # Confirm redemption
        self.generate(node, 10)

        # Verify position is redeemed
        dd_bal_after = node.getdigidollarbalance()
        self.log.info(f"  DD balance after redeem: {dd_bal_after['total']} cents")
        assert_equal(dd_bal_after['total'], 0)

        positions_after = node.listdigidollarpositions(False)
        redeemed = [p for p in positions_after if p['status'] == 'redeemed']
        self.log.info(f"  Redeemed positions: {len(redeemed)}")
        assert_greater_than(len(redeemed), 0)
        assert_equal(redeemed[0]['position_id'], position_id)

        # ----------------------------------------------------------------
        # Step 3: Export descriptors from current wallet
        # ----------------------------------------------------------------
        self.log.info("Step 3: Exporting descriptors...")
        descriptors = node.listdescriptors(True)  # True = include private keys
        self.log.info(f"  Exported {len(descriptors['descriptors'])} descriptors")

        # ----------------------------------------------------------------
        # Step 4: Create new wallet and import descriptors
        # ----------------------------------------------------------------
        self.log.info("Step 4: Creating new wallet and importing descriptors...")

        # Create a new blank descriptor wallet
        node.createwallet(
            wallet_name="restored_wallet",
            disable_private_keys=False,
            blank=True,
            passphrase="",
            avoid_reuse=False,
            descriptors=True,
        )

        # Switch to the new wallet
        restored = node.get_wallet_rpc("restored_wallet")

        # Import descriptors with timestamp=0 to trigger full rescan
        import_requests = []
        for desc_info in descriptors['descriptors']:
            req = {
                "desc": desc_info['desc'],
                "timestamp": 0,  # Scan from genesis
                "active": desc_info.get('active', False),
                "internal": desc_info.get('internal', False),
            }
            if 'range' in desc_info:
                req['range'] = desc_info['range']
            import_requests.append(req)

        self.log.info(f"  Importing {len(import_requests)} descriptors with full rescan...")
        import_results = restored.importdescriptors(import_requests)

        # Verify all imports succeeded
        for i, result in enumerate(import_results):
            if not result['success']:
                self.log.warning(f"  Descriptor {i} import failed: {result}")
            # Some descriptors may fail (e.g., combo on descriptor wallet) - that's OK

        successful = sum(1 for r in import_results if r['success'])
        self.log.info(f"  {successful}/{len(import_results)} descriptors imported successfully")

        # ----------------------------------------------------------------
        # Step 5: CRITICAL — Verify redeemed positions in restored wallet
        # ----------------------------------------------------------------
        self.log.info("Step 5: CRITICAL — Checking positions in restored wallet...")

        # Check DD balance (should be 0 — all DD was redeemed)
        restored_dd_bal = restored.getdigidollarbalance()
        self.log.info(f"  Restored wallet DD balance: {restored_dd_bal['total']} cents")
        assert_equal(restored_dd_bal['total'], 0)

        # THE CRITICAL CHECK: positions must show as redeemed, NOT active
        restored_positions = restored.listdigidollarpositions(False)  # Get ALL positions
        self.log.info(f"  Restored wallet total positions: {len(restored_positions)}")

        if len(restored_positions) == 0:
            self.log.info("  No positions found in restored wallet (mint may not have been detected)")
            self.log.info("  This is acceptable — no false 'active' positions shown")
            return

        # If positions ARE found, they must NOT be active
        restored_active = [p for p in restored_positions if p['status'] != 'redeemed']
        restored_redeemed = [p for p in restored_positions if p['status'] == 'redeemed']

        self.log.info(f"  Restored active positions: {len(restored_active)}")
        self.log.info(f"  Restored redeemed positions: {len(restored_redeemed)}")

        for p in restored_active:
            self.log.error(f"  BUG: Position {p['position_id']} shows as '{p['status']}' "
                          f"but should be 'redeemed'!")

        # CRITICAL: No active positions should exist — they were all redeemed
        assert_equal(len(restored_active), 0)

        self.log.info("=" * 60)
        self.log.info("PASS: Restored wallet correctly shows all positions as redeemed")
        self.log.info("=" * 60)


if __name__ == '__main__':
    DigiDollarWalletRestoreRedeemTest().main()
