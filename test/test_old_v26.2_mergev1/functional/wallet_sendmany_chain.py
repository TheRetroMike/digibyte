#!/usr/bin/env python3
# Copyright (c) 2023 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the wallet's sendmany implementation with multiple recipients that ensures no "too-long-mempool-chain" errors."""

from decimal import Decimal
from test_framework.blocktools import COINBASE_MATURITY_2
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class WalletSendmanyChainTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-whitelist=noban@127.0.0.1", "-dandelion=0"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        # Generate blocks to get spendable outputs
        self.generatetoaddress(self.nodes[0], COINBASE_MATURITY_2 + 20, self.nodes[0].getnewaddress())
        
        # Verify we have a good balance to start
        initial_balance = self.nodes[0].getbalance()
        self.log.info(f"Starting wallet balance: {initial_balance}")
        assert_greater_than(initial_balance, 100)  # Ensure we have enough funds
        
        # Helper function to create multiple recipient dictionary
        def create_recipients(count, amount_per_recipient):
            recipients = {}
            for _ in range(count):
                recipients[self.nodes[0].getnewaddress()] = amount_per_recipient
            return recipients

        # Test 1: Single sendmany with multiple (5) recipients
        self.log.info("Test 1: Single sendmany with 5 recipients")
        recipients1 = create_recipients(5, 1)
        txid1 = self.nodes[0].sendmany("", recipients1)
        self.log.info(f"Transaction sent: {txid1}")
        self.generate(self.nodes[0], 1)
        
        # Get the transaction details and verify everything succeeded
        tx1 = self.nodes[0].gettransaction(txid1)
        assert_equal(tx1["confirmations"], 1)
        
        # Test 2: Chain of 3 sendmany transactions without generating blocks in between
        # This would have triggered the "too-long-mempool-chain" error before the fix
        self.log.info("Test 2: Chain of 3 sendmany transactions without blocks")
        
        # First transaction in the chain
        recipients2a = create_recipients(5, 0.5)
        txid2a = self.nodes[0].sendmany("", recipients2a)
        self.log.info(f"First chained transaction sent: {txid2a}")
        
        # Second transaction in the chain
        recipients2b = create_recipients(5, 0.5)
        txid2b = self.nodes[0].sendmany("", recipients2b)
        self.log.info(f"Second chained transaction sent: {txid2b}")
        
        # Third transaction in the chain 
        recipients2c = create_recipients(5, 0.5)
        txid2c = self.nodes[0].sendmany("", recipients2c)
        self.log.info(f"Third chained transaction sent: {txid2c}")
        
        # Generate block to confirm all transactions
        self.generate(self.nodes[0], 1)
        
        # Verify all transactions confirmed
        tx2a = self.nodes[0].gettransaction(txid2a)
        tx2b = self.nodes[0].gettransaction(txid2b)
        tx2c = self.nodes[0].gettransaction(txid2c)
        
        assert_equal(tx2a["confirmations"], 1)
        assert_equal(tx2b["confirmations"], 1)
        assert_equal(tx2c["confirmations"], 1)
        
        # Test 3: Chain of 5 sendmany with many recipients each (10) 
        self.log.info("Test 3: Chain of 5 sendmany with 10 recipients each")
        
        txids = []
        for i in range(5):
            recipients = create_recipients(10, 0.2)
            txid = self.nodes[0].sendmany("", recipients)
            self.log.info(f"Chain transaction {i+1} sent: {txid}")
            txids.append(txid)
        
        # Generate block to confirm all transactions
        self.generate(self.nodes[0], 1)
        
        # Verify all transactions confirmed
        for i, txid in enumerate(txids):
            tx = self.nodes[0].gettransaction(txid)
            assert_equal(tx["confirmations"], 1)
            
        # Test 4: Extreme case - Single sendmany with 30 recipients
        # This specifically tests the case reported in the issue
        self.log.info("Test 4: Extreme case - Single sendmany with 30 recipients")
        recipients_extreme = create_recipients(30, 0.1)
        txid_extreme = self.nodes[0].sendmany("", recipients_extreme)
        self.log.info(f"Extreme transaction sent: {txid_extreme}")
        self.generate(self.nodes[0], 1)
        
        # Verify transaction confirmed
        tx_extreme = self.nodes[0].gettransaction(txid_extreme)
        assert_equal(tx_extreme["confirmations"], 1)
        
        # Test 5: Multiple chained extreme sendmany operations (3 chained sendmany with 30 recipients each)
        # This is a stress test combining both the chaining and large recipient count
        self.log.info("Test 5: Multiple chained extreme sendmany operations (3x30 recipients)")
        
        extreme_txids = []
        for i in range(3):
            recipients_extreme_chain = create_recipients(30, 0.05)
            txid = self.nodes[0].sendmany("", recipients_extreme_chain)
            self.log.info(f"Extreme chain transaction {i+1} sent: {txid}")
            extreme_txids.append(txid)
        
        # Generate block to confirm all transactions
        self.generate(self.nodes[0], 1)
        
        # Verify all transactions confirmed
        for i, txid in enumerate(extreme_txids):
            tx = self.nodes[0].gettransaction(txid)
            assert_equal(tx["confirmations"], 1)
            
        # Test 6: Long chain of transactions with 20 recipients each
        # This tests a longer chain than previous tests - creates a sequence of
        # 7 consecutive transactions without generating a block
        self.log.info("Test 6: Long chain of transactions (7x20 recipients)")
        
        long_chain_txids = []
        for i in range(7):
            recipients_long_chain = create_recipients(20, 0.01)
            txid = self.nodes[0].sendmany("", recipients_long_chain)
            self.log.info(f"Long chain transaction {i+1} sent: {txid}")
            long_chain_txids.append(txid)
        
        # Generate block to confirm all transactions
        self.generate(self.nodes[0], 1)
        
        # Verify all transactions confirmed
        for i, txid in enumerate(long_chain_txids):
            tx = self.nodes[0].gettransaction(txid)
            assert_equal(tx["confirmations"], 1)
        
        # Verify we have a non-zero balance left
        final_balance = self.nodes[0].getbalance()
        self.log.info(f"Final wallet balance: {final_balance}")
        assert_greater_than(final_balance, 0)
        
        self.log.info("All sendmany chain tests completed successfully")


if __name__ == '__main__':
    WalletSendmanyChainTest().main()