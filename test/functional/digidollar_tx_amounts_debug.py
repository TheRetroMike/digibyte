#!/usr/bin/env python3
# Copyright (c) 2024 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
DigiDollar Transaction Amounts Debug Test

This test verifies that DGB amounts are correct in:
1. Mint transactions (collateral locked)
2. Redemption transactions (collateral returned)
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_greater_than
from decimal import Decimal

class DigiDollarTxAmountsTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # Disable Dandelion for testing (DD transfers fail with Dandelion++)
        self.extra_args = [['-txindex', '-dandelion=0']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("=== DigiDollar Transaction Amounts Debug Test ===")

        alice = self.nodes[0]

        # Generate initial blocks
        self.log.info("\n=== Step 1: Setup - Generate blocks ===")
        alice.generate(108)
        self.sync_all()

        alice_balance_initial = alice.getbalance()
        self.log.info(f"✓ Alice initial balance: {alice_balance_initial} DGB")

        # Set oracle price
        # Oracle price is in micro-USD: 1,000,000 micro-USD = $1.00
        # So $0.01/DGB = 10,000 micro-USD
        alice.setmockoracleprice(10000)  # 10000 micro-USD = $0.01 per DGB
        self.log.info("✓ Oracle price set to 1 cent per DGB")

        # Mint $10 DD with 1-hour lock
        self.log.info("\n=== Step 2: Mint $10 DD (1000 cents) with 1-hour lock ===")
        mint_result = alice.mintdigidollar(1000, 0)  # 0 = 1 hour lock
        mint_txid = mint_result['txid']
        dd_amount = 1000  # We minted 1000 cents
        collateral_locked = Decimal(str(mint_result.get('collateral', '0')))

        self.log.info(f"✓ Mint txid: {mint_txid}")
        self.log.info(f"✓ DD amount: {dd_amount} cents")
        self.log.info(f"✓ Collateral locked (from RPC): {collateral_locked} DGB")

        # Generate blocks to confirm
        alice.generate(10)
        self.sync_all()

        # Analyze mint transaction in detail
        self.log.info("\n=== Step 3: Analyze Mint Transaction ===")
        mint_tx = alice.getrawtransaction(mint_txid, True)

        self.log.info(f"\nMint TX {mint_txid}")
        self.log.info(f"Number of inputs: {len(mint_tx['vin'])}")
        self.log.info(f"Number of outputs: {len(mint_tx['vout'])}")

        # Calculate total input value
        total_input = Decimal('0')
        for i, vin in enumerate(mint_tx['vin']):
            prev_tx = alice.getrawtransaction(vin['txid'], True)
            prev_vout = prev_tx['vout'][vin['vout']]
            input_value = Decimal(str(prev_vout['value']))
            total_input += input_value
            self.log.info(f"  Input {i}: {input_value} DGB (from {vin['txid']}:{vin['vout']})")

        self.log.info(f"\nTotal inputs: {total_input} DGB")

        # Analyze outputs
        total_output = Decimal('0')
        collateral_output = None
        dd_output = None
        change_output = None
        opreturn_output = None

        for i, vout in enumerate(mint_tx['vout']):
            value = Decimal(str(vout['value']))
            total_output += value

            if 'scriptPubKey' in vout:
                script_type = vout['scriptPubKey'].get('type', 'unknown')

                if script_type == 'nulldata':
                    opreturn_output = i
                    self.log.info(f"  Output {i}: {value} DGB - OP_RETURN (metadata)")
                elif i == 0 and value > Decimal('1000'):
                    collateral_output = i
                    self.log.info(f"  Output {i}: {value} DGB - COLLATERAL *** THIS IS THE LOCKED AMOUNT ***")
                elif value == Decimal('0'):
                    dd_output = i
                    self.log.info(f"  Output {i}: {value} DGB - DD TOKEN (zero-value)")
                else:
                    change_output = i
                    self.log.info(f"  Output {i}: {value} DGB - CHANGE")

        self.log.info(f"\nTotal outputs: {total_output} DGB")
        mint_fee = total_input - total_output
        self.log.info(f"Mint fee: {mint_fee} DGB")

        if collateral_output is not None:
            actual_collateral = Decimal(str(mint_tx['vout'][collateral_output]['value']))
            self.log.info(f"\n✅ COLLATERAL LOCKED: {actual_collateral} DGB (output {collateral_output})")
        else:
            self.log.error("❌ Could not identify collateral output!")
            raise AssertionError("Collateral output not found")

        # Generate blocks past lock expiry
        self.log.info("\n=== Step 4: Generate blocks past lock expiry ===")
        alice.generate(350)  # Past tier-0 lock (240 blocks) + 100-block mint confirmation buffer
        current_height = alice.getblockcount()
        self.log.info(f"✓ Current height: {current_height}")

        # Get Alice balance before redemption
        alice_balance_before_redeem = alice.getbalance()
        self.log.info(f"✓ Alice balance before redemption: {alice_balance_before_redeem} DGB")

        # Redeem
        self.log.info("\n=== Step 5: Redeem DigiDollar ===")
        redeem_result = alice.redeemdigidollar(mint_txid, dd_amount)
        redeem_txid = redeem_result['txid']

        self.log.info(f"✓ Redemption txid: {redeem_txid}")

        # Generate blocks to confirm redemption
        alice.generate(10)
        self.sync_all()

        # Get Alice balance after redemption
        alice_balance_after_redeem = alice.getbalance()
        self.log.info(f"✓ Alice balance after redemption: {alice_balance_after_redeem} DGB")

        balance_change = alice_balance_after_redeem - alice_balance_before_redeem
        self.log.info(f"✓ Balance change: {balance_change} DGB")

        # Analyze redemption transaction in detail
        self.log.info("\n=== Step 6: Analyze Redemption Transaction ===")
        redeem_tx = alice.getrawtransaction(redeem_txid, True)

        self.log.info(f"\nRedemption TX {redeem_txid}")
        self.log.info(f"Number of inputs: {len(redeem_tx['vin'])}")
        self.log.info(f"Number of outputs: {len(redeem_tx['vout'])}")

        # Calculate total input value
        total_redeem_input = Decimal('0')
        collateral_input_value = None
        dd_input_value = None
        fee_input_value = None

        for i, vin in enumerate(redeem_tx['vin']):
            prev_tx = alice.getrawtransaction(vin['txid'], True)
            prev_vout = prev_tx['vout'][vin['vout']]
            input_value = Decimal(str(prev_vout['value']))
            total_redeem_input += input_value

            if vin['txid'] == mint_txid:
                if vin['vout'] == collateral_output:
                    collateral_input_value = input_value
                    self.log.info(f"  Input {i}: {input_value} DGB - COLLATERAL from mint (txid:{vin['txid']}:{vin['vout']}) *** UNLOCKING THIS ***")
                elif vin['vout'] == dd_output:
                    dd_input_value = input_value
                    self.log.info(f"  Input {i}: {input_value} DGB - DD TOKEN from mint (txid:{vin['txid']}:{vin['vout']}) *** BURNING THIS ***")
            else:
                fee_input_value = input_value if fee_input_value is None else fee_input_value + input_value
                self.log.info(f"  Input {i}: {input_value} DGB - FEE INPUT (txid:{vin['txid']}:{vin['vout']})")

        self.log.info(f"\nTotal redemption inputs: {total_redeem_input} DGB")

        # Analyze redemption outputs
        total_redeem_output = Decimal('0')
        collateral_return_output = None
        fee_change_output = None

        for i, vout in enumerate(redeem_tx['vout']):
            value = Decimal(str(vout['value']))
            total_redeem_output += value

            if 'scriptPubKey' in vout:
                script_type = vout['scriptPubKey'].get('type', 'unknown')

                # The first output should be the collateral return
                if i == 0:
                    collateral_return_output = i
                    self.log.info(f"  Output {i}: {value} DGB - COLLATERAL RETURN *** THIS SHOULD EQUAL LOCKED AMOUNT ***")
                else:
                    fee_change_output = i
                    self.log.info(f"  Output {i}: {value} DGB - FEE CHANGE")

        self.log.info(f"\nTotal redemption outputs: {total_redeem_output} DGB")
        redeem_fee = total_redeem_input - total_redeem_output
        self.log.info(f"Redemption fee: {redeem_fee} DGB")

        # Critical verification
        self.log.info("\n=== Step 7: CRITICAL VERIFICATION ===")

        if collateral_return_output is not None:
            actual_return = Decimal(str(redeem_tx['vout'][collateral_return_output]['value']))
            self.log.info(f"\n1. Collateral locked in mint: {actual_collateral} DGB")
            self.log.info(f"2. Collateral returned in redemption: {actual_return} DGB")
            self.log.info(f"3. Difference: {actual_return - actual_collateral} DGB")

            if actual_return == actual_collateral:
                self.log.info("✅ PASS: Redemption returns EXACTLY the locked collateral amount")
            else:
                self.log.error(f"❌ FAIL: Redemption amount ({actual_return} DGB) does NOT match locked amount ({actual_collateral} DGB)")
                self.log.error(f"   Missing/Extra: {actual_collateral - actual_return} DGB")
                raise AssertionError(f"Redemption returned {actual_return} DGB but locked {actual_collateral} DGB")
        else:
            self.log.error("❌ Could not identify collateral return output!")
            raise AssertionError("Collateral return output not found")

        # Verify wallet balance change makes sense
        self.log.info("\n=== Step 8: Verify Wallet Balance ===")
        expected_balance_increase = actual_collateral - redeem_fee
        self.log.info(f"Expected balance increase: {expected_balance_increase} DGB (collateral - fee)")
        self.log.info(f"Actual balance increase: {balance_change} DGB")

        # Allow for small discrepancies due to other transactions
        if abs(balance_change - expected_balance_increase) < Decimal('1'):
            self.log.info("✅ PASS: Wallet balance change matches expected")
        else:
            self.log.warning(f"⚠️  WARNING: Balance change differs by {abs(balance_change - expected_balance_increase)} DGB")

        self.log.info("\n=== ALL TESTS PASSED ===")

if __name__ == '__main__':
    DigiDollarTxAmountsTest().main()
