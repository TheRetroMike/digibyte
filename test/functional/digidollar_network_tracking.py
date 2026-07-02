#!/usr/bin/env python3
"""Test DigiDollar network-wide tracking.

CRITICAL TEST: Verify that all nodes see identical DigiDollar stats
regardless of which wallets are loaded, by scanning the UTXO set.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
)
from decimal import Decimal


class DigiDollarNetworkTrackingTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # Disable Dandelion for testing (DD transfers fail with Dandelion++)
        self.extra_args = [
            ["-digidollar=1", "-debug=digidollar", "-txindex=1", "-dandelion=0"],
            ["-digidollar=1", "-debug=digidollar", "-txindex=1", "-dandelion=0"]
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("=" * 80)
        self.log.info("TESTING NETWORK-WIDE DIGIDOLLAR TRACKING (CRITICAL)")
        self.log.info("=" * 80)

        # Setup: Generate blocks and set oracle price
        # Need lots of blocks because oracle price is $0.01 per DGB
        # Minting $175 DD requires ~57,500 DGB collateral
        self.log.info("Setting up test environment...")
        self.nodes[0].generate(700)  # Matches Qt test setup
        self.sync_all()

        # CRITICAL FIX: Consolidate UTXOs after maturity to avoid huge mint transactions
        # With 700 tiny coinbase outputs, mint transactions can have 28+ inputs
        # which creates transactions >700 vB that fail min relay fee checks
        self.log.info("Consolidating UTXOs to reduce mint transaction sizes...")
        balance = self.nodes[0].getbalance()
        addr = self.nodes[0].getnewaddress()
        # Send 90% of balance to self in one transaction, leave 10% for fees
        self.nodes[0].sendtoaddress(addr, balance * Decimal('0.9'), "", "", False)
        self.nodes[0].generate(10)  # Mature the consolidated UTXO
        self.sync_all()

        # Set oracle price on both nodes (using $0.01 per DGB to match Qt test)
        # Oracle price is in micro-USD: 1,000,000 micro-USD = $1.00
        # So $0.01/DGB = 10,000 micro-USD
        for node in self.nodes:
            node.setmockoracleprice(10000)  # 10000 micro-USD = $0.01 per DGB

        # Phase 1: Bob mints 3 DigiDollars (matching Qt test structure)
        self.log.info("\n--- Phase 1: Bob (node 0) mints 3 DigiDollars ---")

        # Use the current DD mint minimum rate so the test does not rely on
        # mintdigidollar's low-fee floor.
        fee_rate = 35000000

        # Mint #1: $100.00 DD, tier 4 (365 days)
        bob_mint1 = self.nodes[0].mintdigidollar(10000, 4, fee_rate)
        self.log.info(f"Mint #1: $100.00 DD, txid: {bob_mint1['txid']}, collateral: {bob_mint1['dgb_collateral']} DGB")

        # Mint #2: $50.00 DD, tier 3 (180 days)
        bob_mint2 = self.nodes[0].mintdigidollar(5000, 3, fee_rate)
        self.log.info(f"Mint #2: $50.00 DD, txid: {bob_mint2['txid']}, collateral: {bob_mint2['dgb_collateral']} DGB")

        # Mint #3: $25.00 DD, tier 2 (90 days)
        bob_mint3 = self.nodes[0].mintdigidollar(2500, 2, fee_rate)
        self.log.info(f"Mint #3: $25.00 DD, txid: {bob_mint3['txid']}, collateral: {bob_mint3['dgb_collateral']} DGB")

        # Track actual collateral from mint results
        total_collateral = Decimal(bob_mint1['dgb_collateral']) + Decimal(bob_mint2['dgb_collateral']) + Decimal(bob_mint3['dgb_collateral'])
        self.log.info(f"Bob's total minted: $175.00 DD (17500 cents), collateral: {total_collateral} DGB")

        # Sync mempools to ensure all nodes see the transactions
        # (Dandelion is disabled via -dandelion=0, so no embargo wait needed)
        self.log.info("Syncing mempools across all nodes...")
        self.sync_mempools()

        # Mine blocks to confirm
        self.log.info("Mining blocks to confirm all transactions...")
        self.nodes[0].generate(10)
        self.sync_all()

        # Phase 2: Network-wide tracking test
        self.log.info("\n--- Phase 2: CRITICAL TEST - Network-wide stats ---")

        bob_health = self.nodes[0].getdigidollarstats()
        alice_health = self.nodes[1].getdigidollarstats()

        # Calculate expected health from actual collateral
        # health = (collateral_dgb * price_per_dgb) / dd_supply_usd * 100
        # price = $0.01/DGB, supply = $175.00
        expected_health = int(total_collateral * Decimal('0.01') / Decimal('1.75'))

        self.log.info(f"\nBob (node 0) sees:")
        self.log.info(f"  Total DD Supply: {bob_health['total_dd_supply']} cents (expected: 17500)")
        self.log.info(f"  Total Collateral: {bob_health['total_collateral_locked']} DGB (expected: {total_collateral})")
        self.log.info(f"  System Health: {bob_health['health_percentage']}% (expected: {expected_health}%)")

        self.log.info(f"\nAlice (node 1) sees:")
        self.log.info(f"  Total DD Supply: {alice_health['total_dd_supply']} cents (expected: 17500)")
        self.log.info(f"  Total Collateral: {alice_health['total_collateral_locked']} DGB (expected: {total_collateral})")
        self.log.info(f"  System Health: {alice_health['health_percentage']}% (expected: {expected_health}%)")

        # CRITICAL ASSERTION: Both nodes MUST see identical stats
        self.log.info("\n--- Verifying network-wide consistency ---")

        # Both nodes MUST see identical DD supply (scanning UTXO set)
        assert_equal(bob_health['total_dd_supply'], alice_health['total_dd_supply'])
        assert_equal(bob_health['total_dd_supply'], 17500)

        # Both nodes MUST see identical collateral (scanning UTXO set)
        assert_equal(bob_health['total_collateral_locked'], alice_health['total_collateral_locked'])

        # Both nodes MUST see identical system health percentage
        assert_equal(bob_health['health_percentage'], alice_health['health_percentage'])

        # Verify health is within expected range (collateral may vary slightly due to fees)
        assert_equal(bob_health['health_percentage'], expected_health)

        self.log.info("✓ SUCCESS: Both nodes see identical network stats!")
        self.log.info("✓ UTXO-based tracking is working correctly!")
        self.log.info(f"✓ System health correctly calculated: {expected_health}%")

        self.log.info("\n" + "=" * 80)
        self.log.info("ALL TESTS PASSED - NETWORK-WIDE TRACKING WORKS!")
        self.log.info("=" * 80)
        self.log.info("\nVerified:")
        self.log.info("- Both nodes scan the UTXO set for DigiDollar vaults")
        self.log.info("- Both nodes report identical DD supply and collateral")
        self.log.info("- System health tracking is blockchain-wide, not wallet-specific")


if __name__ == '__main__':
    DigiDollarNetworkTrackingTest().main()
