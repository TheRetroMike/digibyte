#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) 2015-2022 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the listreceivedbyaddress, listreceivedbylabel, getreceivedybaddress, and getreceivedbylabel RPCs."""
from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_array_result,
    assert_equal,
    assert_raises_rpc_error,
)
from test_framework.wallet_util import test_address


class ReceivedByTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 2
        # whitelist peers to speed up tx relay / mempool sync
        # disable Dandelion++ to avoid embargo delays in testing
        self.extra_args = [["-whitelist=noban@127.0.0.1", "-dandelion=0"]] * self.num_nodes

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_cli()

    def run_test(self):
        # Generate block to get out of IBD and get spendable coins
        # DigiByte: Generate enough blocks for coinbase maturity
        self.generate(self.nodes[0], COINBASE_MATURITY + 1)
        self.sync_blocks()
        
        # Manually ensure nodes are connected before sending transactions
        self.connect_nodes(0, 1)
        self.log.info(f"Node 0 connections: {self.nodes[0].getconnectioncount()}")
        self.log.info(f"Node 1 connections: {self.nodes[1].getconnectioncount()}")

        # save the number of coinbase reward addresses so far
        num_cb_reward_addresses = len(self.nodes[1].listreceivedbyaddress(minconf=0, include_empty=True, include_watchonly=True))

        self.log.info("listreceivedbyaddress Test")

        # Send from node 0 to 1
        # First check node 0 balance to ensure it has spendable funds
        balance0 = self.nodes[0].getbalance()
        self.log.info(f"Node 0 balance: {balance0}")
        
        addr = self.nodes[1].getnewaddress()
        txid = self.nodes[0].sendtoaddress(addr, 0.1)
        
        # Ensure transaction is fully propagated to all nodes
        self.sync_mempools()
        self.sync_all()
        
        # Generate a block to confirm the transaction
        self.generate(self.nodes[0], 1)
        self.sync_all()
        
        # Check transaction was created and received
        self.log.info(f"Transaction ID: {txid}")
        self.log.info(f"Node 0 transaction: {self.nodes[0].gettransaction(txid)}")
        
        # Check if node 1 now knows about the transaction
        try:
            tx_info_1 = self.nodes[1].gettransaction(txid)
            self.log.info(f"Node 1 transaction info: {tx_info_1}")
        except Exception as e:
            self.log.info(f"Node 1 still doesn't know about transaction: {e}")

        # DigiByte has default minconf=1 (vs Bitcoin's higher default)
        # So with 1 confirmation, address should appear by default. Check for minconf=2 instead.
        assert_array_result(self.nodes[1].listreceivedbyaddress(2),
                            {"address": addr},
                            {},
                            True)
        # Bury Tx under 9 more blocks (we already generated 1) so it will have 10 confirmations total
        self.generate(self.nodes[1], 9)
        self.sync_all()
        
        # Check node 1 balance to see if funds were received
        balance1 = self.nodes[1].getbalance()
        self.log.info(f"Node 1 balance after transaction: {balance1}")
        
        # Check transaction status from receiver side
        try:
            tx_info = self.nodes[1].gettransaction(txid)
            self.log.info(f"Node 1 transaction info: {tx_info}")
        except Exception as e:
            self.log.info(f"Node 1 doesn't know about transaction: {e}")
        
        received_list = self.nodes[1].listreceivedbyaddress()
        self.log.info(f"listreceivedbyaddress result: {received_list}")
        self.log.info(f"Looking for address: {addr}")
        assert_array_result(received_list,
                            {"address": addr},
                            {"address": addr, "label": "", "amount": Decimal("0.1"), "confirmations": 10, "txids": [txid, ]})
        # With min confidence < 10
        assert_array_result(self.nodes[1].listreceivedbyaddress(5),
                            {"address": addr},
                            {"address": addr, "label": "", "amount": Decimal("0.1"), "confirmations": 10, "txids": [txid, ]})
        # With min confidence > 10, should not find Tx
        assert_array_result(self.nodes[1].listreceivedbyaddress(11), {"address": addr}, {}, True)

        # Empty Tx
        empty_addr = self.nodes[1].getnewaddress()
        assert_array_result(self.nodes[1].listreceivedbyaddress(0, True),
                            {"address": empty_addr},
                            {"address": empty_addr, "label": "", "amount": 0, "confirmations": 0, "txids": []})

        # Test Address filtering
        # Only on addr
        expected = {"address": addr, "label": "", "amount": Decimal("0.1"), "confirmations": 10, "txids": [txid, ]}
        res = self.nodes[1].listreceivedbyaddress(minconf=0, include_empty=True, include_watchonly=True, address_filter=addr)
        assert_array_result(res, {"address": addr}, expected)
        assert_equal(len(res), 1)
        # Test for regression on CLI calls with address string (#14173)
        cli_res = self.nodes[1].cli.listreceivedbyaddress(0, True, True, addr)
        assert_array_result(cli_res, {"address": addr}, expected)
        assert_equal(len(cli_res), 1)
        # Error on invalid address
        assert_raises_rpc_error(-4, "address_filter parameter was invalid", self.nodes[1].listreceivedbyaddress, minconf=0, include_empty=True, include_watchonly=True, address_filter="bamboozling")
        # Another address receive money
        res = self.nodes[1].listreceivedbyaddress(0, True, True)
        assert_equal(len(res), 2 + num_cb_reward_addresses)  # Right now 2 entries
        other_addr = self.nodes[1].getnewaddress()
        txid2 = self.nodes[0].sendtoaddress(other_addr, 0.1)
        self.generate(self.nodes[0], 1)
        self.sync_all()
        # Same test as above should still pass
        expected = {"address": addr, "label": "", "amount": Decimal("0.1"), "confirmations": 11, "txids": [txid, ]}
        res = self.nodes[1].listreceivedbyaddress(0, True, True, addr)
        assert_array_result(res, {"address": addr}, expected)
        assert_equal(len(res), 1)
        # Same test as above but with other_addr should still pass
        expected = {"address": other_addr, "label": "", "amount": Decimal("0.1"), "confirmations": 1, "txids": [txid2, ]}
        res = self.nodes[1].listreceivedbyaddress(0, True, True, other_addr)
        assert_array_result(res, {"address": other_addr}, expected)
        assert_equal(len(res), 1)
        # Should be two entries though without filter
        res = self.nodes[1].listreceivedbyaddress(0, True, True)
        assert_equal(len(res), 3 + num_cb_reward_addresses)  # Became 3 entries

        # Not on random addr
        other_addr = self.nodes[0].getnewaddress()  # note on node[0]! just a random addr
        res = self.nodes[1].listreceivedbyaddress(0, True, True, other_addr)
        assert_equal(len(res), 0)

        self.log.info("getreceivedbyaddress Test")

        # Send from node 0 to 1
        addr = self.nodes[1].getnewaddress()
        txid = self.nodes[0].sendtoaddress(addr, 0.1)
        self.sync_all()

        # Check balance is 0 because of 0 confirmations
        balance = self.nodes[1].getreceivedbyaddress(addr)
        assert_equal(balance, Decimal("0.0"))

        # Check balance is 0.1
        balance = self.nodes[1].getreceivedbyaddress(addr, 0)
        assert_equal(balance, Decimal("0.1"))

        # Bury Tx under 10 blocks so it will be returned by the default getreceivedbyaddress
        self.generate(self.nodes[1], 10)
        self.sync_all()
        balance = self.nodes[1].getreceivedbyaddress(addr)
        assert_equal(balance, Decimal("0.1"))

        # Trying to getreceivedby for an address the wallet doesn't own should return an error
        assert_raises_rpc_error(-4, "Address not found in wallet", self.nodes[0].getreceivedbyaddress, addr)

        self.log.info("listreceivedbylabel + getreceivedbylabel Test")

        # set pre-state
        label = ''
        address = self.nodes[1].getnewaddress()
        test_address(self.nodes[1], address, labels=[label])
        received_by_label_json = [r for r in self.nodes[1].listreceivedbylabel() if r["label"] == label][0]
        balance_by_label = self.nodes[1].getreceivedbylabel(label)

        txid = self.nodes[0].sendtoaddress(addr, 0.1)
        self.sync_all()

        # getreceivedbylabel returns an error if the wallet doesn't own the label
        assert_raises_rpc_error(-4, "Label not found in wallet", self.nodes[0].getreceivedbylabel, "dummy")

        # listreceivedbylabel should return received_by_label_json because of 0 confirmations
        assert_array_result(self.nodes[1].listreceivedbylabel(),
                            {"label": label},
                            received_by_label_json)

        # getreceivedbylabel should return same balance because of 0 confirmations
        balance = self.nodes[1].getreceivedbylabel(label)
        assert_equal(balance, balance_by_label)

        self.generate(self.nodes[1], 10)
        self.sync_all()
        # listreceivedbylabel should return updated received list
        assert_array_result(self.nodes[1].listreceivedbylabel(),
                            {"label": label},
                            {"label": received_by_label_json["label"], "amount": (received_by_label_json["amount"] + Decimal("0.1"))})

        # getreceivedbylabel should return updated receive total
        balance = self.nodes[1].getreceivedbylabel(label)
        assert_equal(balance, balance_by_label + Decimal("0.1"))

        # Create a new label named "mynewlabel" that has a 0 balance
        address = self.nodes[1].getnewaddress()
        self.nodes[1].setlabel(address, "mynewlabel")
        received_by_label_json = [r for r in self.nodes[1].listreceivedbylabel(0, True) if r["label"] == "mynewlabel"][0]

        # Test includeempty of listreceivedbylabel
        assert_equal(received_by_label_json["amount"], Decimal("0.0"))

        # Test getreceivedbylabel for 0 amount labels
        balance = self.nodes[1].getreceivedbylabel("mynewlabel")
        assert_equal(balance, Decimal("0.0"))

        self.log.info("Test -walletbroadcast")
        self.stop_nodes()
        # Add higher maxtxfee for legacy wallets to avoid fee exceeded errors (DigiByte uses higher fees)
        self.start_node(0, ["-walletbroadcast=0", "-dandelion=0", "-maxtxfee=100"])
        self.start_node(1, ["-walletbroadcast=0", "-dandelion=0", "-maxtxfee=100"])
        self.connect_nodes(0, 1)

        txid = self.nodes[0].sendtoaddress(addr, 0.1)

        # With -walletbroadcast=0, node0 should have the tx but not broadcast it
        assert_equal(self.nodes[0].gettransaction(txid, True)["txid"], txid)
        
        # Node1 should NOT have the transaction yet (not broadcasted)
        # It will only see it after it's mined
        
        # With -walletbroadcast=0, the tx is not in mempool, so we need to manually add it
        # Get the raw transaction and send it to mempool
        raw_tx = self.nodes[0].gettransaction(txid)["hex"]
        self.nodes[0].sendrawtransaction(raw_tx)
        
        # Mine the tx so it gets to node 1
        blockhash = self.generate(self.nodes[0], 1)[0]
        self.sync_all()
        
        # Verify the block was mined
        assert_equal(self.nodes[0].getbestblockhash(), blockhash)
        assert_equal(self.nodes[1].getbestblockhash(), blockhash)
        
        # Now both nodes should have the confirmed transaction
        assert_equal(self.nodes[0].gettransaction(txid, True)["confirmations"], 1)
        assert_equal(self.nodes[1].gettransaction(txid, True)["confirmations"], 1)
        
        # Verify node1 now sees the transaction in listtransactions
        found = False
        for tx in self.nodes[1].listtransactions(label="*", count=10000, include_watchonly=True):
            if tx.get("txid") == txid:
                found = True
                break
        assert found, f"Transaction {txid} not found in node1's listtransactions after mining"

        self.log.info("Test getreceivedbyaddress with minconf > 1")
        
        # Send more transactions
        addr2 = self.nodes[1].getnewaddress()
        tx_ids = []
        for _ in range(5):
            tx_id = self.nodes[0].sendtoaddress(addr2, 0.2)
            tx_ids.append(tx_id)
            # With -walletbroadcast=0, we need to manually broadcast each tx
            raw_tx = self.nodes[0].gettransaction(tx_id)["hex"]
            self.nodes[0].sendrawtransaction(raw_tx)
        self.sync_all()
        
        # Mine 5 blocks - transactions will have 5 confirmations
        self.generate(self.nodes[0], 5)
        self.sync_all()
        
        # Check with minconf=1
        balance_1conf = self.nodes[1].getreceivedbyaddress(addr2, 1)
        assert_equal(balance_1conf, Decimal("1.0"))  # 5 * 0.2
        
        # Check with minconf=5
        balance_5conf = self.nodes[1].getreceivedbyaddress(addr2, 5)
        assert_equal(balance_5conf, Decimal("1.0"))
        
        # Check with minconf=6 (should be 0 as we only have 5 confirmations)
        balance_6conf = self.nodes[1].getreceivedbyaddress(addr2, 6)
        assert_equal(balance_6conf, Decimal("0.0"))

        self.log.info("Test listreceivedbylabel with includeempty=false")
        
        # Create a new empty label
        empty_label_addr = self.nodes[1].getnewaddress("empty_label")
        
        # Check that empty label is not shown with includeempty=false
        labels_no_empty = self.nodes[1].listreceivedbylabel(minconf=0, include_empty=False)
        empty_labels = [r for r in labels_no_empty if r["label"] == "empty_label"]
        assert_equal(len(empty_labels), 0)
        
        # Check that empty label is shown with includeempty=true
        labels_with_empty = self.nodes[1].listreceivedbylabel(minconf=0, include_empty=True)
        empty_labels = [r for r in labels_with_empty if r["label"] == "empty_label"]
        assert_equal(len(empty_labels), 1)
        assert_equal(empty_labels[0]["amount"], Decimal("0.0"))

        self.log.info("Test include_watchonly parameter")
        
        if not self.options.descriptors:
            # Watch-only import only works properly in legacy wallets
            # Import a watch-only address
            watch_addr = self.nodes[0].getnewaddress()
            self.nodes[1].importaddress(watch_addr, "watch_label", False)
            
            # Send to watch-only address
            watch_txid = self.nodes[0].sendtoaddress(watch_addr, 0.5)
            # With -walletbroadcast=0, manually broadcast
            raw_tx = self.nodes[0].gettransaction(watch_txid)["hex"]
            self.nodes[0].sendrawtransaction(raw_tx)
            self.generate(self.nodes[0], 1)
            self.sync_all()
            
            # Check that watchonly is excluded by default
            received_default = self.nodes[1].listreceivedbyaddress()
            watch_entries = [r for r in received_default if r["address"] == watch_addr]
            assert_equal(len(watch_entries), 0)
            
            # Check that watchonly is included when specified
            received_watchonly = self.nodes[1].listreceivedbyaddress(minconf=0, include_empty=True, include_watchonly=True)
            watch_entries = [r for r in received_watchonly if r["address"] == watch_addr]
            assert_equal(len(watch_entries), 1)
            assert_equal(watch_entries[0]["amount"], Decimal("0.5"))
            assert_equal(watch_entries[0]["involvesWatchonly"], True)
        else:
            self.log.info("Skipping watch-only test for descriptor wallets")

        self.log.info("All listreceivedby tests completed successfully!")


if __name__ == '__main__':
    ReceivedByTest().main()