#!/usr/bin/env python3
"""Test Bug #13 fix: listdigidollartxs fee field must be non-zero for sends.

Previously, listdigidollartxs always showed fee: 0.00000000 for send
transactions because DDTransaction::fee was never populated.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_greater_than
from decimal import Decimal


class DigiDollarBug13ListDDTxsFeeTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Bug #13: listdigidollartxs fee must be non-zero for sends")

        # Setup: mine blocks past maturity, set oracle price
        self.nodes[0].generate(110)
        self.sync_all()

        # Oracle price $0.50/DGB = 500000 micro-USD
        self.nodes[0].setmockoracleprice(500000)
        self.nodes[1].setmockoracleprice(500000)

        # Mint DigiDollars on node 0 (amount in cents, lock_tier 0 = 1h testing)
        self.log.info("Minting DigiDollars on node 0...")
        mint_result = self.nodes[0].mintdigidollar(500, 0)  # 500 cents = $5.00, tier 0
        mint_txid = mint_result["txid"]
        self.log.info(f"Mint txid: {mint_txid}")
        self.nodes[0].generate(1)
        self.sync_all()

        # Send DD from node 0 to node 1
        self.log.info("Sending DigiDollars from node 0 to node 1...")
        dd_addr1 = self.nodes[1].getdigidollaraddress()
        send_result = self.nodes[0].senddigidollar(dd_addr1, 100)  # 100 cents = $1.00
        send_txid = send_result["txid"]
        self.log.info(f"Send txid: {send_txid}")
        self.nodes[0].generate(1)
        self.sync_all()

        # Check listdigidollartxs on node 0
        self.log.info("Checking listdigidollartxs on node 0...")
        txs = self.nodes[0].listdigidollartxs()

        # Find the send transaction (category = "send")
        send_tx = None
        for tx in txs:
            if tx["txid"] == send_txid and tx.get("category") == "send":
                send_tx = tx
                break

        # If no "send" category, try finding by txid alone
        if send_tx is None:
            for tx in txs:
                if tx["txid"] == send_txid:
                    send_tx = tx
                    self.log.info(f"Found tx with category: {tx.get('category')}")
                    break

        assert send_tx is not None, f"Send tx {send_txid} not found in listdigidollartxs. All txids: {[t['txid'] for t in txs]}"

        fee = Decimal(str(send_tx["fee"]))
        self.log.info(f"Send tx fee: {fee}")
        assert_greater_than(fee, Decimal("0"))

        self.log.info("Bug #13 fix verified: send transaction fee is non-zero")


if __name__ == "__main__":
    DigiDollarBug13ListDDTxsFeeTest().main()
