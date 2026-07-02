#!/usr/bin/env python3
# Copyright (c) 2024 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
DigiDollar Redemption and Network Statistics Test

Tests:
1. Mint DigiDollars with 1-hour lock (240 blocks in regtest)
2. Verify early redemption is rejected
3. Generate blocks past lock expiry
4. Redeem successfully
5. Verify network-wide statistics are consistent across 3 nodes
6. Verify DD supply and collateral decrease after redemption
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal

COINBASE_MATURITY = 8
# Oracle price in micro-USD: 1,000,000 micro-USD = $1.00
# For $0.01/DGB (1 cent per DGB), use 10,000 micro-USD
ORACLE_PRICE_MICRO_USD = 10000  # 10,000 micro-USD = $0.01 per DGB
TIER_0_RATIO = 1000  # 1000% collateral for 1-hour lock
TIER_0_BLOCKS = 240  # 1 hour in regtest (15 seconds per block)

class DigiDollarRedeemStatsTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-mocktime=0", "-dandelion=0", "-txindex=1"],
            ["-digidollar=1", "-mocktime=0", "-dandelion=0", "-txindex=1"],
            ["-digidollar=1", "-mocktime=0", "-dandelion=0", "-txindex=1"]
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        alice = self.nodes[0]
        bob = self.nodes[1]
        charlie = self.nodes[2]

        self.log.info("=== Step 1: Setup - Generate blocks and sync ===")

        # Generate blocks on Alice to mature coinbase
        # Test framework auto-creates wallets when using --legacy-wallet
        alice.generate(COINBASE_MATURITY + 100)
        self.sync_blocks()

        # Verify Alice has balance
        alice_balance = alice.getbalance()
        self.log.info(f"✓ Alice balance: {alice_balance} DGB")

        # Set oracle price on all nodes
        for node in [alice, bob, charlie]:
            node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)

        self.log.info(f"✓ All nodes synced at height {alice.getblockcount()}")
        self.log.info(f"✓ Oracle price set to {ORACLE_PRICE_MICRO_USD} micro-USD ($0.01/DGB)")

        self.log.info("=== Step 2: Alice mints $10 DD with 1-hour lock ===")

        # Mint parameters
        dd_amount = Decimal('10.00')
        dd_cents = int(dd_amount * 100)  # Convert to cents: 1000 cents = $10
        lock_tier = 0  # Tier 0 = 1 hour testing lock

        # Mint with 1-hour lock (tier 0)
        # RPC signature: mintdigidollar dd_amount lock_tier ( fee_rate )
        # dd_amount is in CENTS, not dollars!
        # fee_rate is in sat/kB; mintdigidollar floors lower values to the
        # current DD minimum rate.
        fee_rate_sat_kb = 35000000
        mint_result = alice.mintdigidollar(dd_cents, lock_tier, fee_rate_sat_kb)

        mint_txid = mint_result['txid']
        collateral_dgb = mint_result['dgb_collateral']
        unlock_height_from_mint = mint_result['unlock_height']

        self.log.info(f"✓ Mint txid: {mint_txid[:16]}...")
        self.log.info(f"✓ DD Amount: {dd_cents} cents (${dd_amount})")
        self.log.info(f"✓ Collateral: {collateral_dgb} DGB")
        self.log.info(f"✓ Lock tier: {lock_tier} (1 hour)")
        self.log.info(f"✓ Unlock height: {unlock_height_from_mint}")

        # CRITICAL: Verify mint transaction outputs BEFORE mining blocks
        self.log.info("=== Verifying mint transaction structure ===")
        mint_raw_tx = alice.getrawtransaction(mint_txid, True)

        # Mint transaction should have 2 outputs:
        # Output 0: Collateral (large amount, P2TR with MAST tree)
        # Output 1: DD tokens (small amount, P2TR with MAST tree)
        self.log.info(f"Mint transaction has {len(mint_raw_tx['vout'])} outputs:")

        # Verify output 0 is the collateral
        collateral_output = mint_raw_tx['vout'][0]
        collateral_output_value = Decimal(str(collateral_output['value']))
        self.log.info(f"  Output 0 (Collateral): {collateral_output_value} DGB")

        assert collateral_output_value == Decimal(str(collateral_dgb)), \
            f"CRITICAL BUG: Mint collateral output value ({collateral_output_value}) doesn't match expected ({collateral_dgb})!"

        # Verify output 1 is the DD tokens
        dd_output = mint_raw_tx['vout'][1]
        dd_output_value = Decimal(str(dd_output['value']))
        self.log.info(f"  Output 1 (DD tokens): {dd_output_value} DGB")

        self.log.info("✅ Mint transaction structure verified")
        self.log.info(f"✅ Collateral locked: {collateral_output_value} DGB (will verify this exact amount returns on redemption)")

        # Generate blocks to confirm mint
        alice.generate(10)
        self.sync_blocks()

        current_height = alice.getblockcount()
        unlock_height = unlock_height_from_mint  # Use the unlock height from the mint result
        self.log.info(f"✓ Mint confirmed at height {current_height}")
        self.log.info(f"✓ Will unlock at height {unlock_height}")

        self.log.info("=== Step 3: Verify network stats with mint (all 3 nodes) ===")

        # Check stats on all 3 nodes
        stats = {}
        for i, (name, node) in enumerate([('Alice', alice), ('Bob', bob), ('Charlie', charlie)]):
            stats[name] = node.getdigidollarstats()
            self.log.info(f"\n{name}'s view:")
            self.log.info(f"  Total DD Supply: {stats[name]['total_dd_supply']} cents")
            self.log.info(f"  Total Collateral: {stats[name]['total_collateral_dgb']} DGB")
            self.log.info(f"  Health: {stats[name]['health_percentage']}%")

        # Verify all nodes see the same network stats (matching each other)
        assert_equal(stats['Alice']['total_dd_supply'], stats['Bob']['total_dd_supply'])
        assert_equal(stats['Bob']['total_dd_supply'], stats['Charlie']['total_dd_supply'])
        assert_equal(stats['Alice']['total_collateral_dgb'], stats['Bob']['total_collateral_dgb'])
        assert_equal(stats['Bob']['total_collateral_dgb'], stats['Charlie']['total_collateral_dgb'])

        # CRITICAL: Verify stats match the actual mint amount (not mock data!)
        expected_dd_supply = dd_cents  # 1000 cents minted
        expected_collateral = float(collateral_dgb)  # 10000 DGB locked

        assert stats['Alice']['total_dd_supply'] == expected_dd_supply, \
            f"Alice sees wrong DD supply: expected {expected_dd_supply}, got {stats['Alice']['total_dd_supply']}"
        assert stats['Alice']['total_collateral_dgb'] == expected_collateral, \
            f"Alice sees wrong collateral: expected {expected_collateral}, got {stats['Alice']['total_collateral_dgb']}"

        self.log.info(f"✅ All 3 nodes see identical network stats!")
        self.log.info(f"✅ Stats match actual mint: {dd_cents} cents DD, {collateral_dgb} DGB collateral")

        self.log.info("=== Step 4: Try early redemption (should FAIL) ===")

        # Generate halfway through lock period
        current_height = alice.getblockcount()
        blocks_to_unlock = unlock_height - current_height
        halfway_blocks = blocks_to_unlock // 2

        alice.generate(halfway_blocks)
        self.sync_blocks()

        current_height = alice.getblockcount()
        blocks_remaining = unlock_height - current_height
        self.log.info(f"Current height: {current_height}")
        self.log.info(f"Blocks until unlock: {blocks_remaining}")

        # Attempt early redemption - should fail
        # RPC signature: redeemdigidollar "position_id" dd_amount ( "redemption_address" )
        try:
            alice.redeemdigidollar(mint_txid, dd_cents)
            raise AssertionError("Early redemption should have been rejected!")
        except Exception as e:
            if "locked until block" in str(e).lower() or "position locked" in str(e).lower():
                self.log.info(f"✅ Early redemption correctly rejected: {str(e)[:80]}...")
            else:
                raise

        self.log.info("=== Step 5: Generate blocks past lock expiry ===")

        # Generate past unlock height
        blocks_to_generate = blocks_remaining + 10
        alice.generate(blocks_to_generate)
        self.sync_blocks()

        current_height = alice.getblockcount()
        self.log.info(f"✓ Current height: {current_height}")
        self.log.info(f"✓ Unlock height: {unlock_height}")
        self.log.info(f"✓ Lock period EXPIRED")

        self.log.info("=== Step 6: Redeem after lock expires (should SUCCEED) ===")

        # Redeem the vault
        # RPC signature: redeemdigidollar "position_id" dd_amount ( "redemption_address" )
        redeem_result = alice.redeemdigidollar(mint_txid, dd_cents)
        redeem_txid = redeem_result['txid']
        dgb_unlocked = redeem_result['dgb_unlocked']

        self.log.info(f"✓ Redemption txid: {redeem_txid[:16]}...")
        self.log.info(f"✓ DD Redeemed: {dd_cents} cents (${dd_amount})")
        self.log.info(f"✓ Collateral returned: {dgb_unlocked} DGB")

        # CRITICAL: Verify redemption transaction outputs BEFORE mining
        self.log.info("=== Verifying redemption transaction structure ===")
        redeem_raw_tx = alice.getrawtransaction(redeem_txid, True)

        # Log all outputs for debugging
        self.log.info(f"Redemption transaction has {len(redeem_raw_tx['vout'])} outputs:")
        total_output_value = Decimal('0')
        for idx, vout in enumerate(redeem_raw_tx['vout']):
            value = Decimal(str(vout['value']))
            total_output_value += value
            self.log.info(f"  Output {idx}: {value} DGB")
            if 'scriptPubKey' in vout and 'type' in vout['scriptPubKey']:
                self.log.info(f"    Type: {vout['scriptPubKey']['type']}")

        self.log.info(f"Total output value: {total_output_value} DGB")

        # CRITICAL VERIFICATION: Total outputs should equal collateral locked (minus reasonable fee)
        # During mint, we locked exactly collateral_dgb (10000 DGB)
        # During redemption, we should get back ~10000 DGB (minus small fee)

        # DigiDollar transactions require a minimum fee of 0.1 DGB (10,000,000 satoshis)
        # to ensure network relay. With the 35M sat/kB fee rate on a ~300-400 byte tx,
        # the expected fee is around 0.105-0.14 DGB
        max_reasonable_fee = Decimal('0.15')  # 0.15 DGB maximum (accounts for fee rate overhead)
        expected_min_return = Decimal(str(collateral_dgb)) - max_reasonable_fee

        assert total_output_value >= expected_min_return, \
            f"CRITICAL BUG: Redemption outputs ({total_output_value} DGB) much less than collateral locked ({collateral_dgb} DGB)!"

        # Also verify output 0 is EXACTLY the collateral amount (no haircut)
        output_0_value = Decimal(str(redeem_raw_tx['vout'][0]['value']))
        assert output_0_value == Decimal(str(collateral_dgb)), \
            f"CRITICAL BUG: Output 0 ({output_0_value} DGB) doesn't match collateral locked ({collateral_dgb} DGB)!"

        self.log.info(f"✅ Output 0 EXACTLY matches collateral: {output_0_value} DGB")

        # Verify fee is reasonable (change should be close to input - fee)
        if len(redeem_raw_tx['vout']) > 1:
            change_value = Decimal(str(redeem_raw_tx['vout'][1]['value']))
            # Fee = (total inputs - total outputs)
            # We need to get input values to calculate actual fee
            total_input_value = Decimal('0')
            for vin in redeem_raw_tx['vin']:
                # Get the value of each input by looking up the prev transaction
                prev_tx = alice.getrawtransaction(vin['txid'], True)
                input_value = Decimal(str(prev_tx['vout'][vin['vout']]['value']))
                total_input_value += input_value

            actual_fee = total_input_value - total_output_value
            self.log.info(f"Total inputs: {total_input_value} DGB")
            self.log.info(f"Actual fee: {actual_fee} DGB")

            assert actual_fee <= max_reasonable_fee, \
                f"CRITICAL BUG: Fee ({actual_fee} DGB) is INSANELY high! Should be < {max_reasonable_fee} DGB"

            self.log.info(f"✅ Fee is reasonable: {actual_fee} DGB (< {max_reasonable_fee} DGB)")


        # Also verify the inputs are correct
        self.log.info(f"Redemption transaction has {len(redeem_raw_tx['vin'])} inputs:")
        for idx, vin in enumerate(redeem_raw_tx['vin']):
            self.log.info(f"  Input {idx}: {vin['txid']}:{vin['vout']}")
            # Input 0 should be the collateral UTXO from mint
            if idx == 0:
                assert vin['txid'] == mint_txid, \
                    f"CRITICAL BUG: First input is not the collateral UTXO! Expected {mint_txid}, got {vin['txid']}"
                # Verify we're spending the collateral output (output 0 from mint)
                assert vin['vout'] == 0, \
                    f"CRITICAL BUG: First input is not spending collateral output 0! Got output {vin['vout']}"
                self.log.info(f"  ✓ Input 0 correctly spends collateral from mint tx")

        self.log.info("✅ Redemption transaction structure verified")

        # Generate blocks to confirm redemption
        alice.generate(10)
        self.sync_blocks()

        self.log.info(f"✓ Redemption confirmed at height {alice.getblockcount()}")

        self.log.info("=== Step 7: Verify redemption transaction in wallet ===")

        # Get redemption transaction using gettransaction (wallet method)
        redeem_tx = alice.gettransaction(redeem_txid)

        self.log.info(f"Redemption transaction in wallet:")
        self.log.info(f"  Transaction ID: {redeem_txid}")
        self.log.info(f"  Amount: {redeem_tx.get('amount', 0)} DGB")
        self.log.info(f"  Fee: {redeem_tx.get('fee', 0)} DGB")
        self.log.info("✅ Redemption transaction found in wallet")

        self.log.info("=== Step 8: Verify network stats AFTER redemption (all 3 nodes) ===")

        # Check stats on all 3 nodes after redemption
        stats_after = {}
        for name, node in [('Alice', alice), ('Bob', bob), ('Charlie', charlie)]:
            stats_after[name] = node.getdigidollarstats()
            self.log.info(f"\n{name}'s view (after redemption):")
            self.log.info(f"  Total DD Supply: {stats_after[name]['total_dd_supply']} cents")
            self.log.info(f"  Total Collateral: {stats_after[name]['total_collateral_dgb']} DGB")
            self.log.info(f"  Health: {stats_after[name]['health_percentage']}%")

        # CRITICAL: After redemption, vault UTXOs are spent, so stats should return to 0
        expected_dd_after = 0  # All DD burned
        expected_collateral_after = 0.0  # All collateral released

        # Verify all nodes see stats decreased to 0
        for name in ['Alice', 'Bob', 'Charlie']:
            assert stats_after[name]['total_dd_supply'] == expected_dd_after, \
                f"{name} sees wrong DD supply after redemption: expected {expected_dd_after}, got {stats_after[name]['total_dd_supply']}"
            assert stats_after[name]['total_collateral_dgb'] == expected_collateral_after, \
                f"{name} sees wrong collateral after redemption: expected {expected_collateral_after}, got {stats_after[name]['total_collateral_dgb']}"

        self.log.info("✅ All 3 nodes see DD supply decreased to 0")
        self.log.info("✅ All 3 nodes see collateral decreased to 0")
        self.log.info("✅ Stats accurately reflect on-chain state (not mock data)")

        self.log.info("=== SUCCESS: Redemption flow complete ===")
        self.log.info("✅ Mint transaction created and confirmed")
        self.log.info("✅ Early redemption correctly rejected (timelock)")
        self.log.info("✅ Redemption after expiry successful")
        self.log.info("✅ All network consensus checks passed")

        # Test complete - listdigidollartxs is intentionally skipped because
        # this flow validates redemption through balance, supply, and collateral
        # state instead of transaction-history RPC help output.
        self.log.info("\n" + "="*60)
        self.log.info("TEST PASSED: DigiDollar Redemption & Network Stats")
        self.log.info("="*60)
        self.log.info("\nVerified:")
        self.log.info("  ✓ 1-hour lock mint successful")
        self.log.info("  ✓ Early redemption correctly rejected")
        self.log.info("  ✓ Redemption successful after lock expiry")
        self.log.info("  ✓ Transaction confirmed in blockchain")
        self.log.info("  ✓ Network stats consistent across 3 nodes (before)")
        self.log.info("  ✓ Network stats consistent across 3 nodes (after)")
        self.log.info("  ✓ DD supply decreased correctly")
        self.log.info("  ✓ Collateral released correctly")

if __name__ == '__main__':
    DigiDollarRedeemStatsTest().main()
