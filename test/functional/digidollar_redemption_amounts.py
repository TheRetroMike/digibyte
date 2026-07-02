#!/usr/bin/env python3
# Copyright (c) 2024 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
DigiDollar Redemption Amount Verification Test

This test PROVES that redemption transactions return EXACTLY the DGB amount
that was originally locked as collateral.

Test Flow:
1. Mint DigiDollar with specific DGB collateral
2. Record EXACT collateral amount from mint transaction
3. Wait for timelock to expire
4. Redeem DigiDollar
5. PROVE redemption transaction output EXACTLY matches original collateral
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal
from decimal import Decimal

class DigiDollarRedemptionAmountsTest(DigiByteTestFramework):
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
        self.log.info("=" * 80)
        self.log.info("DIGIDOLLAR REDEMPTION AMOUNT VERIFICATION TEST")
        self.log.info("=" * 80)

        node = self.nodes[0]

        # Setup
        self.log.info("\n=== STEP 1: Setup - Generate blocks ===")
        node.generate(108)
        initial_balance = node.getbalance()
        self.log.info(f"Initial balance: {initial_balance} DGB")

        # Set oracle price
        # Oracle price is in micro-USD: 1,000,000 micro-USD = $1.00
        # So $0.01/DGB = 10,000 micro-USD
        node.setmockoracleprice(10000)  # 10000 micro-USD = $0.01 per DGB
        self.log.info("Oracle price: 1 cent per DGB")

        # Mint DigiDollar
        self.log.info("\n=== STEP 2: Mint $10 DigiDollar (1000 cents) ===")
        mint_result = node.mintdigidollar(1000, 0)  # 1000 cents, 1-hour lock
        mint_txid = mint_result['txid']
        self.log.info(f"Mint txid: {mint_txid}")

        # Confirm mint
        node.generate(10)

        # Analyze mint transaction to get EXACT collateral amount
        self.log.info("\n=== STEP 3: Analyze Mint Transaction ===")
        mint_tx = node.getrawtransaction(mint_txid, True)

        self.log.info(f"\nMint Transaction: {mint_txid}")
        self.log.info(f"Inputs: {len(mint_tx['vin'])}")
        self.log.info(f"Outputs: {len(mint_tx['vout'])}")

        # Output 0 should be the collateral
        collateral_vout = 0
        collateral_amount = Decimal(str(mint_tx['vout'][collateral_vout]['value']))

        self.log.info(f"\n*** COLLATERAL LOCKED IN MINT ***")
        self.log.info(f"Output {collateral_vout}: {collateral_amount} DGB")
        self.log.info(f"ScriptPubKey: {mint_tx['vout'][collateral_vout]['scriptPubKey']['hex']}")

        # Verify DD token output exists
        dd_vout = 1
        dd_amount = Decimal(str(mint_tx['vout'][dd_vout]['value']))
        self.log.info(f"\n*** DD TOKEN ***")
        self.log.info(f"Output {dd_vout}: {dd_amount} DGB (should be 0)")
        # DD token is 0-value output
        if dd_amount != Decimal('0'):
            self.log.warning(f"DD token output is {dd_amount} instead of 0")

        # Generate blocks past lock expiry
        self.log.info("\n=== STEP 4: Generate blocks past timelock ===")
        node.generate(350)
        current_height = node.getblockcount()
        self.log.info(f"Current height: {current_height}")

        # Redeem
        self.log.info("\n=== STEP 5: Redeem DigiDollar ===")
        redeem_result = node.redeemdigidollar(mint_txid, 1000)
        redeem_txid = redeem_result['txid']
        self.log.info(f"Redemption txid: {redeem_txid}")

        # Confirm redemption
        node.generate(10)

        # Analyze redemption transaction
        self.log.info("\n=== STEP 6: Analyze Redemption Transaction ===")
        redeem_tx = node.getrawtransaction(redeem_txid, True)

        self.log.info(f"\nRedemption Transaction: {redeem_txid}")
        self.log.info(f"Inputs: {len(redeem_tx['vin'])}")
        self.log.info(f"Outputs: {len(redeem_tx['vout'])}")

        # Verify inputs
        self.log.info("\n*** REDEMPTION INPUTS ***")
        collateral_input_found = False
        dd_input_found = False

        for i, vin in enumerate(redeem_tx['vin']):
            prev_txid = vin['txid']
            prev_vout = vin['vout']

            if prev_txid == mint_txid:
                if prev_vout == collateral_vout:
                    collateral_input_found = True
                    self.log.info(f"Input {i}: COLLATERAL from mint tx {prev_txid}:{prev_vout}")
                elif prev_vout == dd_vout:
                    dd_input_found = True
                    self.log.info(f"Input {i}: DD TOKEN from mint tx {prev_txid}:{prev_vout}")
            else:
                # Fee input
                prev_tx = node.getrawtransaction(prev_txid, True)
                fee_value = Decimal(str(prev_tx['vout'][prev_vout]['value']))
                self.log.info(f"Input {i}: FEE INPUT {fee_value} DGB from {prev_txid}:{prev_vout}")

        assert collateral_input_found, "Redemption must spend collateral input"
        assert dd_input_found, "Redemption must spend DD token input"

        # Verify outputs
        self.log.info("\n*** REDEMPTION OUTPUTS ***")

        # Output 0 should be the returned collateral
        returned_collateral = Decimal(str(redeem_tx['vout'][0]['value']))
        self.log.info(f"Output 0: {returned_collateral} DGB (RETURNED COLLATERAL)")
        self.log.info(f"ScriptPubKey: {redeem_tx['vout'][0]['scriptPubKey']['hex']}")

        # Output 1 should be fee change
        if len(redeem_tx['vout']) > 1:
            fee_change = Decimal(str(redeem_tx['vout'][1]['value']))
            self.log.info(f"Output 1: {fee_change} DGB (FEE CHANGE)")

        # Calculate total inputs and outputs
        total_inputs = Decimal('0')
        for vin in redeem_tx['vin']:
            prev_tx = node.getrawtransaction(vin['txid'], True)
            total_inputs += Decimal(str(prev_tx['vout'][vin['vout']]['value']))

        total_outputs = Decimal('0')
        for vout in redeem_tx['vout']:
            total_outputs += Decimal(str(vout['value']))

        fee_paid = total_inputs - total_outputs

        self.log.info(f"\nTotal inputs: {total_inputs} DGB")
        self.log.info(f"Total outputs: {total_outputs} DGB")
        self.log.info(f"Fee paid: {fee_paid} DGB")

        # THE CRITICAL TEST
        self.log.info("\n" + "=" * 80)
        self.log.info("CRITICAL VERIFICATION")
        self.log.info("=" * 80)
        self.log.info(f"Collateral LOCKED in mint:     {collateral_amount} DGB")
        self.log.info(f"Collateral RETURNED in redeem: {returned_collateral} DGB")
        self.log.info(f"Difference:                    {returned_collateral - collateral_amount} DGB")

        if returned_collateral == collateral_amount:
            self.log.info("\n✅✅✅ PASS: REDEMPTION RETURNS EXACTLY THE LOCKED COLLATERAL ✅✅✅")
        else:
            self.log.error(f"\n❌❌❌ FAIL: Redemption returned {returned_collateral} but locked {collateral_amount} ❌❌❌")
            self.log.error(f"Missing/Extra: {collateral_amount - returned_collateral} DGB")
            raise AssertionError(f"Redemption amount mismatch! Expected {collateral_amount}, got {returned_collateral}")

        # CRITICAL: Check wallet transactions to verify the DGB was actually received
        self.log.info("\n" + "=" * 80)
        self.log.info("WALLET TRANSACTION VERIFICATION")
        self.log.info("=" * 80)

        # Get wallet transaction for redemption
        wallet_tx = node.gettransaction(redeem_txid)
        self.log.info(f"\nWallet view of redemption tx:")
        self.log.info(f"Amount: {wallet_tx['amount']} DGB")
        self.log.info(f"Fee: {wallet_tx.get('fee', 0)} DGB")

        # List all transactions to see the outputs
        all_txs = node.listtransactions("*", 100)
        self.log.info(f"\n*** WALLET TRANSACTIONS (last 10) ***")
        for tx in all_txs[-10:]:
            self.log.info(f"Txid: {tx['txid'][:16]}... Amount: {tx['amount']} DGB Category: {tx['category']}")

        # Check if we received the collateral back
        received_collateral = False
        for tx in all_txs:
            if tx['txid'] == redeem_txid and tx['category'] == 'receive':
                self.log.info(f"\n✅ Found RECEIVE transaction for redemption!")
                self.log.info(f"   Amount received: {tx['amount']} DGB")
                if Decimal(str(tx['amount'])) == collateral_amount:
                    self.log.info(f"   ✅ MATCHES collateral amount!")
                    received_collateral = True
                else:
                    self.log.error(f"   ❌ DOES NOT MATCH collateral amount ({collateral_amount})")

        if not received_collateral:
            self.log.error("\n❌❌❌ CRITICAL BUG: Wallet did not receive collateral back! ❌❌❌")
            self.log.error("The redemption transaction exists but the wallet doesn't see the returned DGB!")
            raise AssertionError("Wallet did not receive returned collateral")

        self.log.info("\n" + "=" * 80)
        self.log.info("ALL TESTS PASSED - REDEMPTION AMOUNTS ARE CORRECT")
        self.log.info("=" * 80)

if __name__ == '__main__':
    DigiDollarRedemptionAmountsTest().main()
