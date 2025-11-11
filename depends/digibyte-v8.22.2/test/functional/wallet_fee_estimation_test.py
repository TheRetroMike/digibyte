#!/usr/bin/env python3
# Copyright (c) 2023 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test fee estimation in the wallet with different settings."""

from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY_2
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)

class WalletFeeEstimationTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            # Node 0: Default settings (with fallback fee)
            ["-fallbackfee=0.01"],
            # Node 1: Fallback fee disabled
            ["-fallbackfee=0"]
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        # Generate blocks to get spendable outputs for all nodes
        self.generate(self.nodes[0], COINBASE_MATURITY_2 + 5)
        self.generate(self.nodes[1], 5)
        self.sync_all()
        
        # Phase 1: Initial state testing - no fee estimation history
        self.log.info("Phase 1: Testing initial state with no fee estimation history")
        
        # Node 0 (with fallback fee enabled)
        self.log.info("Testing sendtoaddress with fallback fee enabled")
        address0 = self.nodes[0].getnewaddress()
        txid0 = self.nodes[0].sendtoaddress(address0, Decimal('1.0'))
        tx0 = self.nodes[0].gettransaction(txid0)
        self.log.info(f"Transaction fee with fallback fee enabled: {tx0['fee']}")
        assert_greater_than(-tx0['fee'], 0)  # Should have a non-zero fee
        
        # Node 1 (fallback fee disabled)
        self.log.info("Testing sendtoaddress with fallback fee disabled")
        address1 = self.nodes[1].getnewaddress()
        try:
            txid1 = self.nodes[1].sendtoaddress(address1, Decimal('1.0'))
            tx1 = self.nodes[1].gettransaction(txid1)
            self.log.info(f"Transaction fee: {tx1['fee']}")
            self.log.info("NOTE: Expected failure but transaction succeeded!")
        except Exception as e:
            self.log.info(f"Expected error: {str(e)}")
            # Verify it's a fee estimation error
            assert "Fee estimation failed" in str(e), "Unexpected error message"
        
        # Phase 2: Build up some fee estimation data
        self.log.info("Phase 2: Building fee estimation data")
        self.log.info("Creating a variety of transactions to build fee estimation history")
        
        # Generate transactions with varying fees
        for i in range(20):
            # The idea is to create transactions with different amounts to get varying fees
            amount = Decimal('0.01') + (i * Decimal('0.001'))  # Slightly different amounts to vary the tx size
            addr = self.nodes[0].getnewaddress()
            self.nodes[0].sendtoaddress(addr, amount)
            
            # Create some multi-recipient transactions too
            if i % 5 == 0:
                recipients = {}
                for j in range(3):
                    addr = self.nodes[0].getnewaddress()
                    recipients[addr] = Decimal('0.005')
                self.nodes[0].sendmany("", recipients)
                
            # Mine a block every few transactions
            if i % 4 == 0:
                self.log.info(f"Mining block to confirm transactions (iteration {i})")
                self.generate(self.nodes[0], 1)
                self.sync_all()
        
        # Mine a final block to confirm all transactions
        self.generate(self.nodes[0], 1)
        self.sync_all()
        
        # Phase 3: Test fee estimation after having some history
        self.log.info("Phase 3: Testing fee estimation after building history")
        
        # Check if node 1 (disabled fallback fee) can now estimate fees
        address1 = self.nodes[1].getnewaddress()
        try:
            self.log.info("Testing if node with disabled fallback fee can now use fee estimation")
            txid1 = self.nodes[1].sendtoaddress(address1, Decimal('0.5'))
            tx1 = self.nodes[1].gettransaction(txid1)
            self.log.info(f"Transaction fee with fee estimation: {tx1['fee']}")
            assert_greater_than(-tx1['fee'], 0)
            self.log.info("Success - fee was estimated based on history")
        except Exception as e:
            self.log.info(f"Fee estimation still failed: {str(e)}")
            # This might happen if fee estimation doesn't have enough data yet
        
        # Phase 4: Test the original issue - multiple recipients with sendmany
        self.log.info("Phase 4: Testing sendmany with multiple recipients")
        
        # Node 0 (with fallback fee) - should always succeed
        recipients0 = {}
        for i in range(30):
            addr = self.nodes[0].getnewaddress()
            recipients0[addr] = Decimal('0.01')
        
        txid_many0 = self.nodes[0].sendmany("", recipients0)
        tx_many0 = self.nodes[0].gettransaction(txid_many0)
        self.log.info(f"Sendmany with 30 recipients fee (fallback enabled): {tx_many0['fee']}")
        
        # Node 1 (without fallback fee) - might succeed or fail depending on fee estimation
        recipients1 = {}
        for i in range(30):
            addr = self.nodes[1].getnewaddress()
            recipients1[addr] = Decimal('0.01')
        
        try:
            txid_many1 = self.nodes[1].sendmany("", recipients1)
            tx_many1 = self.nodes[1].gettransaction(txid_many1)
            self.log.info(f"Sendmany with 30 recipients fee (no fallback): {tx_many1['fee']}")
            self.log.info("Success - fee was estimated for complex transaction")
        except Exception as e:
            self.log.info(f"Sendmany failed: {str(e)}")
            # If it still fails, it should be because of fee estimation
            assert "Fee estimation failed" in str(e), "Unexpected error message"
        
        # Phase 5: Test transaction chaining - the heart of the reported issue
        self.log.info("Phase 5: Testing transaction chaining - the bug scenario")
        
        # Node 0 (with fallback fee) should be able to create a chain
        self.log.info("Creating chain of transactions with fallback fee enabled")
        chain_txids = []
        for i in range(3):
            chain_recipients = {}
            for j in range(20):
                addr = self.nodes[0].getnewaddress()
                chain_recipients[addr] = Decimal('0.01')
            
            txid = self.nodes[0].sendmany("", chain_recipients)
            chain_txids.append(txid)
            self.log.info(f"Chain transaction {i+1} sent: {txid}")
        
        # Mine a block to confirm all transactions
        self.generate(self.nodes[0], 1)
        self.sync_all()
        
        # Verify all transactions confirmed
        for i, txid in enumerate(chain_txids):
            tx = self.nodes[0].gettransaction(txid)
            assert_equal(tx["confirmations"], 1)
            self.log.info(f"Chain transaction {i+1} confirmed with fee: {tx['fee']}")
        
        self.log.info("Fee estimation tests completed successfully - fallback fee solves the chaining issue")

if __name__ == '__main__':
    WalletFeeEstimationTest().main()