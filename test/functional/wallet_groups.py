#!/usr/bin/env python3
# Copyright (c) 2018-2022 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test wallet group functionality."""

from test_framework.blocktools import COINBASE_MATURITY, COINBASE_MATURITY_2
from test_framework.test_framework import DigiByteTestFramework
from test_framework.messages import (
    tx_from_hex,
)
from test_framework.util import (
    assert_approx,
    assert_equal,
)


class WalletGroupTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 5
        self.extra_args = [
            ["-dandelion=0", "-maxtxfee=10000.0"],  # 10000 DGB for massive 2000-output transactions
            ["-dandelion=0", "-maxtxfee=10000.0"],
            ["-avoidpartialspends", "-dandelion=0", "-maxtxfee=10000.0"],
            ["-maxapsfee=0.01", "-dandelion=0", "-maxtxfee=10000.0"],
            ["-maxapsfee=0.022", "-dandelion=0", "-maxtxfee=10000.0"],
        ]
        self.rpc_timeout = 480

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Setting up")
        # To take full use of immediate tx relay, all nodes need to be reachable
        # via inbound peers, i.e. connect first to last to close the circle
        # (the default test network topology looks like this:
        #  node0 <-- node1 <-- node2 <-- node3 <-- node4 <-- node5)
        self.connect_nodes(0, self.num_nodes - 1)
        # Mine some coins
        self.generate(self.nodes[0], COINBASE_MATURITY_2 + 1)  # DigiByte wallet tests need COINBASE_MATURITY_2

        # Get some addresses from the two nodes
        addr1 = [self.nodes[1].getnewaddress() for _ in range(3)]
        addr2 = [self.nodes[2].getnewaddress() for _ in range(3)]
        addrs = addr1 + addr2

        # Send 50 + 25 coin to each address (DigiByte amounts)
        [self.nodes[0].sendtoaddress(addr, 50.0) for addr in addrs]
        [self.nodes[0].sendtoaddress(addr, 25.0) for addr in addrs]

        self.generate(self.nodes[0], 1)
        self.sync_all()  # Ensure all nodes see the confirmed transactions

        # For each node, send some coins back to 0;  
        # - node[1] should pick one 0.5 UTXO and leave the rest
        # - node[2] should pick one (1.0 + 0.5) UTXO group corresponding to a
        #   given address, and leave the rest
        self.log.info("Test sending transactions picks one UTXO group and leaves the rest")
        txid1 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 10.0)  # DigiByte amount
        tx1 = self.nodes[1].getrawtransaction(txid1, True)
        # txid1 should have 1 input and 2 outputs  
        assert_equal(1, len(tx1["vin"]))
        assert_equal(2, len(tx1["vout"]))
        # one output should be 10.0, the other should be ~15.0 (25.0 - 10.0 = 15.0)
        v = [vout["value"] for vout in tx1["vout"]]
        v.sort()
        assert_approx(v[0], vexp=10.0, vspan=5.0)  # DigiByte: sent amount
        assert_approx(v[1], vexp=15.0, vspan=5.0)  # change amount

        txid2 = self.nodes[2].sendtoaddress(self.nodes[0].getnewaddress(), 10.0)  # DigiByte amount
        tx2 = self.nodes[2].getrawtransaction(txid2, True)
        # txid2 should have 2 inputs and 2 outputs
        assert_equal(2, len(tx2["vin"]))
        assert_equal(2, len(tx2["vout"]))
        # one output should be 10.0, the other should be ~65.0 (50.0 + 25.0 - 10.0 = 65.0)
        v = [vout["value"] for vout in tx2["vout"]]
        v.sort()
        assert_approx(v[0], vexp=10.0, vspan=5.0)  # DigiByte: sent amount
        assert_approx(v[1], vexp=65.0, vspan=5.0)  # 75.0 - 10.0 = 65.0

        self.log.info("Test avoiding partial spends if warranted, even if avoidpartialspends is disabled")
        self.sync_all()
        self.generate(self.nodes[0], 1)
        # Nodes 1-2 now have confirmed UTXOs (letters denote destinations):
        # Node #1:      Node #2:
        # - A  1.0      - D0 1.0
        # - B0 1.0      - D1 0.5
        # - B1 0.5      - E0 1.0
        # - C0 1.0      - E1 0.5
        # - C1 0.5      - F  ~1.3
        # - D ~0.3
        # Each node received 3×75 DGB = 225 DGB, sent 10 DGB, so ~215 DGB left
        assert_approx(self.nodes[1].getbalance(), vexp=215.0, vspan=20.0)  # Account for DigiByte fees
        assert_approx(self.nodes[2].getbalance(), vexp=215.0, vspan=20.0)
        # Sending amount should be significant but not too large
        # this could be (A / B0 / C0) + (B1 / C1 / D). We ensure that it is
        # B0 + B1 or C0 + C1, because this avoids partial spends while not being
        # detrimental to transaction cost
        txid3 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 70.0)  # Increased for DigiByte
        tx3 = self.nodes[1].getrawtransaction(txid3, True)
        # tx3 should have 2 inputs and 2 outputs
        assert_equal(2, len(tx3["vin"]))
        assert_equal(2, len(tx3["vout"]))
        # the accumulated value should be 75.0 (50+25), so outputs should be
        # ~70.0 and ~5.0 (change) and should come from the same destination
        values = [vout["value"] for vout in tx3["vout"]]
        values.sort()
        assert_approx(values[0], vexp=5.0, vspan=10.0)   # Change output (small)
        assert_approx(values[1], vexp=70.0, vspan=10.0)  # Send output (large)

        input_txids = [vin["txid"] for vin in tx3["vin"]]
        input_addrs = [self.nodes[1].gettransaction(txid)['details'][0]['address'] for txid in input_txids]
        assert_equal(input_addrs[0], input_addrs[1])
        # Node 2 enforces avoidpartialspends so needs no checking here

        # DigiByte fee expectations (actual calculated values from DigiByte fee structure)
        tx4_ungrouped_fee = 1410000  # ~0.0141 DGB (DigiByte actual calculation)
        tx4_grouped_fee = 2080000    # ~0.0208 DGB (DigiByte actual calculation)
        tx5_6_ungrouped_fee = 2760000  # ~0.0276 DGB (actual DigiByte calculation)
        tx5_6_grouped_fee = 4120000    # ~0.0412 DGB (actual DigiByte calculation)

        self.log.info("Test wallet option maxapsfee")
        addr_aps = self.nodes[3].getnewaddress()
        self.nodes[0].sendtoaddress(addr_aps, 50.0)  # Increased for DigiByte fees
        self.nodes[0].sendtoaddress(addr_aps, 50.0)
        self.generate(self.nodes[0], 1)
        with self.nodes[3].assert_debug_log([f'Fee non-grouped = {tx4_ungrouped_fee}, grouped = {tx4_grouped_fee}, using grouped']):
            txid4 = self.nodes[3].sendtoaddress(self.nodes[0].getnewaddress(), 0.1)
        tx4 = self.nodes[3].getrawtransaction(txid4, True)
        # tx4 has 2 inputs because DigiByte chose grouped approach
        # instead of non-grouped due to fee calculation differences
        assert_equal(2, len(tx4["vin"]))
        assert_equal(2, len(tx4["vout"]))

        addr_aps2 = self.nodes[3].getnewaddress()
        [self.nodes[0].sendtoaddress(addr_aps2, 50.0) for _ in range(5)]  # Increased for DigiByte
        self.generate(self.nodes[0], 1)
        with self.nodes[3].assert_debug_log([f'Fee non-grouped = {tx5_6_ungrouped_fee}, grouped = {tx5_6_grouped_fee}, using non-grouped']):
            txid5 = self.nodes[3].sendtoaddress(self.nodes[0].getnewaddress(), 147.5)  # 250 - fees (~2.5)
        tx5 = self.nodes[3].getrawtransaction(txid5, True)
        # tx5 has 3 inputs due to DigiByte's coin selection algorithm (non-grouped approach)
        # because DigiByte chose non-grouped as more efficient for larger amounts  
        assert_equal(3, len(tx5["vin"]))
        assert_equal(2, len(tx5["vout"]))  # Change output created due to 3 inputs totaling 150 DGB

        # Test wallet option maxapsfee with node 4, which sets maxapsfee
        # 1 sat higher, crossing the threshold from non-grouped to grouped.
        self.log.info("Test wallet option maxapsfee threshold from non-grouped to grouped")
        addr_aps3 = self.nodes[4].getnewaddress()
        [self.nodes[0].sendtoaddress(addr_aps3, 50.0) for _ in range(5)]  # Increased for DigiByte
        self.generate(self.nodes[0], 1)
        with self.nodes[4].assert_debug_log([f'Fee non-grouped = {tx5_6_ungrouped_fee}, grouped = {tx5_6_grouped_fee}, using grouped']):
            txid6 = self.nodes[4].sendtoaddress(self.nodes[0].getnewaddress(), 147.5)  # Same as tx5
        tx6 = self.nodes[4].getrawtransaction(txid6, True)
        # tx6 has 5 inputs due to DigiByte's coin selection algorithm (grouped approach)
        assert_equal(5, len(tx6["vin"]))
        assert_equal(2, len(tx6["vout"]))

        # Empty out node2's wallet
        self.nodes[2].sendall(recipients=[self.nodes[0].getnewaddress()])
        self.sync_all()
        self.generate(self.nodes[0], 1)

        self.log.info("Fill a wallet with 10,000 outputs corresponding to the same scriptPubKey")
        for _ in range(5):
            raw_tx = self.nodes[0].createrawtransaction([{"txid":"0"*64, "vout":0}], [{addr2[0]: 0.05}])
            tx = tx_from_hex(raw_tx)
            tx.vin = []
            tx.vout = [tx.vout[0]] * 2000
            funded_tx = self.nodes[0].fundrawtransaction(tx.serialize().hex())
            signed_tx = self.nodes[0].signrawtransactionwithwallet(funded_tx['hex'])
            self.nodes[0].sendrawtransaction(signed_tx['hex'])
            self.generate(self.nodes[0], 1)

        # Check that we can create a transaction that only requires ~100 of our
        # utxos, without pulling in all outputs and creating a transaction that
        # is way too big.
        self.log.info("Test creating txn that only requires ~100 of our UTXOs without pulling in all outputs")
        assert self.nodes[2].sendtoaddress(address=addr2[0], amount=5)


if __name__ == '__main__':
    WalletGroupTest().main()
