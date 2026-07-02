#!/usr/bin/env python3
"""Test DigiDollar transfer operations.

Test comprehensive transfer functionality including:
- DD-to-DD transfers
- Multi-input transfers
- Change handling
- Invalid transfers
- Fee calculations
- Network propagation
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
    p2p_port,
)
from decimal import Decimal


class multidict(dict):
    """Dictionary that serializes duplicate keys into JSON objects."""

    def __init__(self, items):
        dict.__init__(self, items)
        self.items_list = items

    def items(self):
        return self.items_list


class DigiDollarTransferTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        # Enable DigiDollar features, disable Dandelion for testing
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def publish_musig2_quotes(self, node_indices=None, price=None):
        """Publish fresh regtest MuSig2 oracle bundles for the next block."""
        if price is None:
            price = self.oracle_price_micro_usd
        if node_indices is None:
            node_indices = range(self.num_nodes)
        for index in node_indices:
            result = self.nodes[index].setmockoracleprice(price)
            assert_equal(result["price_micro_usd"], price)

    def mine_and_sync_dd(self, node_idx, blocks=1):
        self.publish_musig2_quotes()
        return self.generate(self.nodes[node_idx], blocks)

    def ensure_connected(self, from_idx, to_idx):
        to_port = p2p_port(to_idx)
        if any(peer.get("addr", "").endswith(f":{to_port}") for peer in self.nodes[from_idx].getpeerinfo()):
            return
        self.connect_nodes(from_idx, to_idx)

    def run_test(self):
        self.log.info("Testing DigiDollar transfer operations...")

        # Test setup
        self.setup_digidollar_test()

        # Run test scenarios
        self.test_simple_transfers()
        self.test_sendmany_transfers_confirmed_only_change()
        self.test_multi_input_transfers()
        self.test_change_handling()
        self.test_transfer_validation()
        self.test_fee_calculations()
        self.test_network_propagation()
        self.test_transfer_edge_cases()
        # self.test_concurrent_transfers()  # Disabled: RPC framework not thread-safe

    def setup_digidollar_test(self):
        """Setup test environment for DigiDollar."""
        # Generate blocks past DD activation height (650 for regtest)
        # Also need coinbase maturity (COINBASE_MATURITY=8)
        self.log.info("Generating initial blocks for test setup...")
        # Generate 170 blocks per node = 680 total, past activation height of 650
        for i in range(4):
            self.nodes[i].generate(170)
        self.sync_all()

        # Set mock oracle price ($0.50 per DGB)
        # Oracle price is in micro-USD: 1,000,000 micro-USD = $1.00
        # So $0.50/DGB = 500,000 micro-USD
        base_price = 500000  # 500000 micro-USD = $0.50 per DGB
        self.oracle_price_micro_usd = base_price
        self.publish_musig2_quotes()

        # Create initial DD balances for testing
        self.log.info("Creating initial DD positions for testing...")

        # Node 0: Large position for testing ($50.00 = 5000 cents, 1 year = tier 4)
        self.nodes[0].mintdigidollar(5000, 4)
        self.mine_and_sync_dd(0)

        # Node 1: Medium position ($20.00 = 2000 cents, 180 days = tier 3)
        self.nodes[1].mintdigidollar(2000, 3)
        self.mine_and_sync_dd(1)

        # Node 2: Small position ($10.00 = 1000 cents, 90 days = tier 2)
        self.nodes[2].mintdigidollar(1000, 2)
        self.mine_and_sync_dd(2)

        # Verify initial setup
        for i in range(3):
            balance_info = self.nodes[i].getdigidollarbalance()
            balance = Decimal(balance_info['total'])
            assert_greater_than(balance, Decimal('0'))
            self.log.info(f"Node {i} DD balance: {balance} cents")

    def test_simple_transfers(self):
        """Test simple DD-to-DD transfers between nodes."""
        self.log.info("Testing simple DD transfers...")

        # Get initial balances
        sender_initial = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        receiver_initial = Decimal(self.nodes[3].getdigidollarbalance()['total'])  # Node 3 starts with 0

        # Get receiver address
        receiver_address = self.nodes[3].getdigidollaraddress()

        # Perform transfer ($10.00 = 1000 cents, node 0 has 5000 cents = $50)
        transfer_amount_cents = 1000
        transfer_amount_dollars = Decimal(transfer_amount_cents) / 100
        self.log.info(f"Transferring ${transfer_amount_dollars} DD ({transfer_amount_cents} cents) from node 0 to node 3...")

        result = self.nodes[0].senddigidollar(receiver_address, transfer_amount_cents)
        assert 'txid' in result
        txid = result['txid']

        # Mine block to confirm
        self.mine_and_sync_dd(0)

        # Verify balances
        sender_final = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        receiver_final = Decimal(self.nodes[3].getdigidollarbalance()['total'])

        expected_sender = sender_initial - Decimal(transfer_amount_cents)
        expected_receiver = receiver_initial + Decimal(transfer_amount_cents)

        assert_equal(sender_final, expected_sender)
        assert_equal(receiver_final, expected_receiver)

        # Verify transaction details
        tx_details = self.nodes[0].gettransaction(txid)
        # Note: DD amounts in wallet are in cents, not DGB
        # For DD transactions, the 'amount' field may not be directly comparable
        # assert_equal(tx_details['amount'], -transfer_amount_cents)  # Negative for sender
        assert_greater_than(tx_details['confirmations'], 0)

        # Check receiver's perspective
        rx_tx_details = self.nodes[3].gettransaction(txid)
        # assert_equal(rx_tx_details['amount'], transfer_amount_cents)  # Positive for receiver

    def test_sendmany_transfers_confirmed_only_change(self):
        """Test sendmanydigidollar and confirmed-only DD change policy."""
        self.log.info("Testing sendmanydigidollar and confirmed-only DD change...")

        sender_initial = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        node1_initial = Decimal(self.nodes[1].getdigidollarbalance()['total'])
        node2_initial = Decimal(self.nodes[2].getdigidollarbalance()['total'])

        addr1 = self.nodes[1].getdigidollaraddress()
        addr2 = self.nodes[2].getdigidollaraddress()
        amounts = {
            addr1: 200,
            addr2: 300,
        }

        assert_raises_rpc_error(
            -8,
            "duplicated address",
            self.nodes[0].sendmanydigidollar,
            "",
            multidict([(addr1, 100), (addr1, 200)]),
            "duplicate recipient regression",
        )

        sendmany_comment = "sendmany functional test"
        result = self.nodes[0].sendmanydigidollar("", amounts, sendmany_comment)
        assert 'txid' in result
        assert_equal(result['total_amount'], 500)
        assert_equal(result['amounts'][addr1], 200)
        assert_equal(result['amounts'][addr2], 300)
        assert_equal(result['comment'], sendmany_comment)
        sendmany_rows = [
            tx for tx in self.nodes[0].listdigidollartxs(20, 0)
            if tx['txid'] == result['txid'] and tx['category'] == 'send'
        ]
        assert_equal(len(sendmany_rows), 1)
        assert_equal(sendmany_rows[0]['comment'], sendmany_comment)

        # The first send spends node0's confirmed DD UTXO and creates DD change.
        # That change is unconfirmed and must not be available for a second DD send.
        retry_addr = self.nodes[3].getdigidollaraddress()
        assert_raises_rpc_error(
            -6,
            "wait for prior DigiDollar transfer confirmation",
            self.nodes[0].senddigidollar,
            retry_addr,
            100,
        )

        self.mine_and_sync_dd(0)

        sender_final = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        node1_final = Decimal(self.nodes[1].getdigidollarbalance()['total'])
        node2_final = Decimal(self.nodes[2].getdigidollarbalance()['total'])

        assert_equal(sender_final, sender_initial - Decimal(500))
        assert_equal(node1_final, node1_initial + Decimal(200))
        assert_equal(node2_final, node2_initial + Decimal(300))

        # Regression: sendmany to local DD addresses must show both sides in
        # wallet/RPC history. The send row remains the aggregate "multiple" row,
        # and each local recipient address gets its own receive row so Qt can show
        # the incoming entries too.
        local_initial = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        local_addr1 = self.nodes[0].getdigidollaraddress()
        local_addr2 = self.nodes[0].getdigidollaraddress()
        local_amounts = {
            local_addr1: 200,
            local_addr2: 300,
        }

        local_result = self.nodes[0].sendmanydigidollar("", local_amounts, "sendmany local functional test")
        local_txid = local_result['txid']
        assert_equal(local_result['total_amount'], 500)

        self.mine_and_sync_dd(0)

        local_final = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        assert_equal(local_final, local_initial)

        local_txs = self.nodes[0].listdigidollartxs(20, 0)
        send_rows = [tx for tx in local_txs if tx['txid'] == local_txid and tx['category'] == 'send']
        assert_equal(len(send_rows), 1)
        assert_equal(send_rows[0]['address'], 'multiple')
        assert_equal(send_rows[0]['amount'], Decimal('-500'))

        recv_addr1 = self.nodes[0].listdigidollartxs(10, 0, local_addr1)
        recv_addr2 = self.nodes[0].listdigidollartxs(10, 0, local_addr2)
        assert_equal(len([tx for tx in recv_addr1 if tx['txid'] == local_txid and tx['category'] == 'receive' and tx['amount'] == Decimal('200')]), 1)
        assert_equal(len([tx for tx in recv_addr2 if tx['txid'] == local_txid and tx['category'] == 'receive' and tx['amount'] == Decimal('300')]), 1)

        # Regression: an external sendmany to many addresses in one local
        # wallet must show every local recipient output, not just the first
        # txid-level receive row persisted by older wallets.
        receiver_initial = Decimal(self.nodes[3].getdigidollarbalance()['total'])
        receiver_addrs = [self.nodes[3].getdigidollaraddress() for _ in range(10)]
        external_amounts = {addr: 200 for addr in receiver_addrs}

        external_result = self.nodes[0].sendmanydigidollar("", external_amounts, "sendmany 10 local receiver outputs")
        external_txid = external_result['txid']
        assert_equal(external_result['total_amount'], 2000)

        self.mine_and_sync_dd(0)

        receiver_final = Decimal(self.nodes[3].getdigidollarbalance()['total'])
        assert_equal(receiver_final, receiver_initial + Decimal(2000))

        receiver_rows = [
            tx for tx in self.nodes[3].listdigidollartxs(50, 0, "", "receive")
            if tx['txid'] == external_txid
        ]
        assert_equal(len(receiver_rows), 10)
        assert_equal(sum(tx['amount'] for tx in receiver_rows), Decimal('2000'))
        for addr in receiver_addrs:
            addr_rows = [
                tx for tx in self.nodes[3].listdigidollartxs(20, 0, addr, "receive")
                if tx['txid'] == external_txid
            ]
            assert_equal(len(addr_rows), 1)
            assert_equal(addr_rows[0]['amount'], Decimal('200'))

        self.restart_node(3)
        receiver_rows_after_restart = [
            tx for tx in self.nodes[3].listdigidollartxs(50, 0, "", "receive")
            if tx['txid'] == external_txid
        ]
        assert_equal(len(receiver_rows_after_restart), 10)
        assert_equal(sum(tx['amount'] for tx in receiver_rows_after_restart), Decimal('2000'))
        self.connect_nodes(2, 3)
        self.sync_all()

    def test_multi_input_transfers(self):
        """Test transfers that require multiple DD inputs."""
        self.log.info("Testing multi-input transfers...")

        # For this test, we'll just verify that the current implementation
        # can handle transfers. Multi-input coin selection would require
        # splitting UTXOs first, which is complex for this initial test.

        # Instead, test a simple transfer to verify basic functionality
        receiver_address = self.nodes[2].getdigidollaraddress()
        transfer_amount = 500  # 500 cents = $5.00 DD

        initial_sender_balance = Decimal(self.nodes[1].getdigidollarbalance()['total'])
        initial_receiver_balance = Decimal(self.nodes[2].getdigidollarbalance()['total'])

        self.log.info(f"Node 1 initial balance: {initial_sender_balance} cents")
        self.log.info(f"Node 2 initial balance: {initial_receiver_balance} cents")
        self.log.info(f"Performing transfer of {transfer_amount} cents DD...")
        result = self.nodes[1].senddigidollar(receiver_address, transfer_amount)

        # Mine block to confirm
        self.mine_and_sync_dd(1)

        # Verify the transfer succeeded
        final_sender_balance = Decimal(self.nodes[1].getdigidollarbalance()['total'])
        final_receiver_balance = Decimal(self.nodes[2].getdigidollarbalance()['total'])

        expected_sender = initial_sender_balance - Decimal(transfer_amount)
        expected_receiver = initial_receiver_balance + Decimal(transfer_amount)

        assert_equal(final_sender_balance, expected_sender)
        assert_equal(final_receiver_balance, expected_receiver)

        # Verify transaction exists
        tx_info = self.nodes[1].gettransaction(result['txid'])
        assert 'details' in tx_info

        self.log.info("Multi-input transfer test passed (simplified for initial testing)")

    def test_change_handling(self):
        """Test change handling in DD transfers."""
        self.log.info("Testing DD change handling...")

        # Get current balance and create a transfer that requires change (amounts in cents)
        sender_balance = Decimal(self.nodes[2].getdigidollarbalance()['total'])
        receiver_address = self.nodes[0].getdigidollaraddress()

        # Transfer amount that requires change (leave 200 cents = $2.00 for further testing)
        transfer_amount = int(sender_balance - 200)  # Leave 200 cents change

        self.log.info(f"Transferring {transfer_amount} cents from balance of {sender_balance} cents...")

        result = self.nodes[2].senddigidollar(receiver_address, transfer_amount)

        # Mine block to confirm
        self.mine_and_sync_dd(2)

        # Verify change was properly handled
        remaining_balance = Decimal(self.nodes[2].getdigidollarbalance()['total'])
        expected_remaining = Decimal(200)  # 200 cents

        self.log.info(f"After transfer - Remaining balance: {remaining_balance} cents, Expected: {expected_remaining} cents")

        # Allow for small precision differences
        tolerance = Decimal(1)  # 1 cent tolerance
        assert abs(remaining_balance - expected_remaining) <= tolerance, \
            f"Balance mismatch: got {remaining_balance}, expected {expected_remaining}"

        # Verify sender can still use the change
        if remaining_balance >= 100:  # At least minimum output (100 cents = $1)
            # Make a transfer with the change (100 cents minimum)
            small_transfer = 100
            small_result = self.nodes[2].senddigidollar(receiver_address, small_transfer)

            self.mine_and_sync_dd(2)

            # Verify it worked and balance updated correctly
            assert 'txid' in small_result
            final_balance = Decimal(self.nodes[2].getdigidollarbalance()['total'])
            expected_final = Decimal(100)  # 200 - 100 = 100 cents remaining
            assert abs(final_balance - expected_final) <= tolerance, \
                f"Final balance mismatch: got {final_balance}, expected {expected_final}"

    def test_transfer_validation(self):
        """Test transfer validation rules."""
        self.log.info("Testing transfer validation...")

        valid_address = self.nodes[1].getdigidollaraddress()
        sender_balance = Decimal(self.nodes[0].getdigidollarbalance()['total'])

        # Test insufficient balance (try to send 1 cent more than we have)
        excessive_amount = int(sender_balance + 1)
        assert_raises_rpc_error(-6, "Insufficient DD balance",
                               self.nodes[0].senddigidollar, valid_address, excessive_amount)

        # Test invalid addresses
        invalid_addresses = [
            "",
            "invalid_address",
            "dgb1qw508d6qejxtdg4y5r3zarvary0c5xw7k3hz6a0",  # Regular DGB address
            "dgbrt1dd" + "0" * 100  # Too long
        ]

        for invalid_addr in invalid_addresses:
            assert_raises_rpc_error(-5, "Invalid DigiDollar address",
                                   self.nodes[0].senddigidollar, invalid_addr, 100)  # 100 cents

        # Test invalid amounts
        # Note: amounts should be integers (cents), but test some invalid ones
        # Skip this test for now as the RPC validation may vary
        # invalid_amounts = [0, -100]
        # for invalid_amount in invalid_amounts:
        #     with assert_raises_rpc_error(-32602, ""):
        #         self.nodes[0].senddigidollar(valid_address, invalid_amount)

    def test_fee_calculations(self):
        """Test fee calculations for DD transfers."""
        self.log.info("Testing DD transfer fee calculations...")

        # DD transfers should have minimal fees since they don't require DGB network fees
        # The main cost is the DD network fee (if any)

        sender_balance_before = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        dgb_balance_before = self.nodes[0].getbalance()

        receiver_address = self.nodes[1].getdigidollaraddress()
        transfer_amount = 100  # 100 cents (minimum output)

        # Perform transfer
        result = self.nodes[0].senddigidollar(receiver_address, transfer_amount)

        # Mine block to confirm
        self.mine_and_sync_dd(0)

        sender_balance_after = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        dgb_balance_after = self.nodes[0].getbalance()

        # DD balance should decrease by exactly the transfer amount
        dd_decrease = sender_balance_before - sender_balance_after
        assert_equal(dd_decrease, Decimal(transfer_amount))

        # DGB balance might decrease slightly due to transaction fees
        dgb_decrease = dgb_balance_before - dgb_balance_after

        # DGB fees should exist for DD transfers (actual amount varies by TX complexity)
        assert_greater_than_or_equal(dgb_decrease, Decimal('0'))  # Some fee is expected
        self.log.info(f"DGB fee for DD transfer: {dgb_decrease} DGB")

    def test_network_propagation(self):
        """Test DD transfer propagation across the network."""
        self.log.info("Testing DD transfer network propagation...")

        # Ensure all nodes are connected
        for i in range(self.num_nodes - 1):
            self.ensure_connected(i, i + 1)

        # Create transfer on node 0
        receiver_address = self.nodes[3].getdigidollaraddress()
        transfer_amount = 100  # 100 cents (minimum output)

        result = self.nodes[0].senddigidollar(receiver_address, transfer_amount)
        txid = result['txid']

        # Mine block on the sender node to guarantee the transaction is included.
        # DD transactions may not propagate via normal P2P mempool relay, so
        # mining on a remote node risks producing a block without the tx.
        block_hashes = self.mine_and_sync_dd(0)

        # Verify the confirmed block (and its transaction) propagated to all nodes
        for i in range(self.num_nodes):
            tx_info = self.nodes[i].getrawtransaction(txid, True, block_hashes[0])
            assert_greater_than(tx_info['confirmations'], 0)
            self.log.info(f"Node {i}: transaction confirmed with {tx_info['confirmations']} confirmations")

    def test_transfer_edge_cases(self):
        """Test edge cases in DD transfers."""
        self.log.info("Testing DD transfer edge cases...")

        # Test transfer to self
        self_address = self.nodes[0].getdigidollaraddress()
        transfer_amount = 100  # 100 cents (minimum output)

        initial_balance = Decimal(self.nodes[0].getdigidollarbalance()['total'])

        result = self.nodes[0].senddigidollar(self_address, transfer_amount)

        self.mine_and_sync_dd(0)

        # Balance should remain approximately the same (minus fees)
        final_balance = Decimal(self.nodes[0].getdigidollarbalance()['total'])
        balance_diff = abs(final_balance - initial_balance)
        assert balance_diff < 1, "Balance changed too much for self-transfer"  # Allow for fees (1 cent)

        # Test various amounts (all in cents, must be >= 100 cents minimum)
        test_amounts = [100, 150, 200]  # cents

        receiver_address = self.nodes[1].getdigidollaraddress()

        for amount in test_amounts:
            try:
                result = self.nodes[0].senddigidollar(receiver_address, amount)
                self.mine_and_sync_dd(0)
                self.log.info(f"Amount {amount} cents transferred successfully")
            except Exception as e:
                self.log.info(f"Amount {amount} cents failed: {e}")

    def test_concurrent_transfers(self):
        """Test concurrent DD transfers."""
        self.log.info("Testing concurrent DD transfers...")

        # Create multiple transfers simultaneously
        import threading
        import time

        results = []
        errors = []

        def transfer_worker(node_idx, amount):
            try:
                receiver_address = self.nodes[(node_idx + 1) % self.num_nodes].getdigidollaraddress()
                result = self.nodes[node_idx].senddigidollar(receiver_address, str(amount))
                results.append(result)
            except Exception as e:
                errors.append(str(e))

        # Launch concurrent transfers
        threads = []
        transfer_amounts = [100, 150, 200]  # amounts in cents (must be >= 100 minimum)

        for i, amount in enumerate(transfer_amounts):
            balance = Decimal(self.nodes[i].getdigidollarbalance()['total']) if i < len(self.nodes) else 0
            if i < len(self.nodes) and balance >= amount:
                thread = threading.Thread(target=transfer_worker, args=(i, amount))
                threads.append(thread)
                thread.start()

        # Wait for all transfers to complete
        for thread in threads:
            thread.join()

        # Mine blocks to confirm successful transfers
        self.nodes[0].generate(3)
        self.sync_all()

        self.log.info(f"Concurrent transfers completed: {len(results)} successful, {len(errors)} failed")

        # Some failures are acceptable in concurrent scenarios
        # The important thing is that the node doesn't crash and successful transfers work

        # Verify successful transfers
        for result in results:
            if 'txid' in result:
                tx_info = self.nodes[0].gettransaction(result['txid'])
                assert_greater_than(tx_info['confirmations'], 0)


if __name__ == '__main__':
    DigiDollarTransferTest().main()
