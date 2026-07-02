#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Regression test for DD-RH-067: a confirmed DD transfer that is later
disconnected and abandoned must release the sender's original DD UTXO.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class WalletDigiDollarTransferReorgTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex=1", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining spendable funds")
        node.generate(200)
        node.setmockoracleprice(500000)

        self.log.info("Minting and confirming DigiDollar")
        minted_cents = 100000
        mint = node.mintdigidollar(minted_cents, 0)
        node.generate(1)
        node.syncwithvalidationinterfacequeue()
        assert_equal(node.getdigidollarbalance()["total"], minted_cents)

        self.log.info("Confirming a self-transfer that spends the original DD UTXO")
        self_address = node.getdigidollaraddress()
        transfer = node.senddigidollar(self_address, 10000)
        transfer_txid = transfer["txid"]
        transfer_block = node.generate(1)[0]
        node.syncwithvalidationinterfacequeue()
        assert_equal(node.getdigidollarbalance()["total"], minted_cents)

        self.log.info("Disconnecting the transfer block")
        node.invalidateblock(transfer_block)
        node.syncwithvalidationinterfacequeue()
        assert transfer_txid in node.getrawmempool()

        self.log.info("Restarting without mempool persistence or wallet rebroadcast")
        self.restart_node(0, extra_args=["-txindex=1", "-dandelion=0", "-persistmempool=0", "-walletbroadcast=0"])
        node = self.nodes[0]
        node.syncwithvalidationinterfacequeue()
        assert transfer_txid not in node.getrawmempool()

        self.log.info("Abandoning the disconnected transfer must restore the original DD UTXO")
        node.abandontransaction(transfer_txid)
        node.syncwithvalidationinterfacequeue()
        assert_equal(node.getdigidollarbalance()["total"], minted_cents)

        retry = node.senddigidollar(node.getdigidollaraddress(), 10000)
        assert "txid" in retry


if __name__ == "__main__":
    WalletDigiDollarTransferReorgTest().main()
