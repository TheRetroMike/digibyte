#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Regression test for DD-RH-121: if a reorg disconnects both a DD mint and a
descendant DD transfer, the transfer must not resurrect through stale txindex
data while its DD input is only available from the mempool parent.
"""

from test_framework.test_framework import DigiByteTestFramework


class WalletDigiDollarTransferAncestorReorgTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex=1", "-dandelion=0", "-persistmempool=1"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining spendable funds")
        node.generate(200)
        # Match the regtest fallback/P2P oracle price used during disconnect
        # mempool re-add, so the disconnected mint itself remains policy-valid.
        node.setmockoracleprice(6500)

        self.log.info("Minting and confirming DigiDollar")
        mint = node.mintdigidollar(100, 0)
        mint_txid = mint["txid"]
        mint_block = node.generate(1)[0]
        node.syncwithvalidationinterfacequeue()

        self.log.info("Confirming a transfer descendant of the minted DD output")
        transfer = node.senddigidollar(node.getdigidollaraddress(), 100)
        transfer_txid = transfer["txid"]
        transfer_block = node.generate(1)[0]
        assert transfer_txid in node.getblock(transfer_block)["tx"]
        node.syncwithvalidationinterfacequeue()

        self.log.info("Invalidating the mint ancestor block")
        node.invalidateblock(mint_block)
        node.syncwithvalidationinterfacequeue()

        mempool = node.getrawmempool()
        assert transfer_txid not in mempool, (
            "DD transfer descendant must not return to mempool while its DD input "
            "comes from an unconfirmed mint"
        )
        assert node.getdigidollarbalance()["total"] == 0, (
            "Wallet must not keep phantom DD balance from a temporarily re-added "
            "descendant whose confirmed ancestor was disconnected"
        )

        self.log.info("Restarting with mempool persistence must not reload the descendant")
        self.restart_node(0, extra_args=["-txindex=1", "-dandelion=0", "-persistmempool=1"])
        node = self.nodes[0]
        node.syncwithvalidationinterfacequeue()
        mempool = node.getrawmempool()
        assert transfer_txid not in mempool, "Rejected DD descendant must not survive restart"
        assert node.getdigidollarbalance()["total"] == 0


if __name__ == "__main__":
    WalletDigiDollarTransferAncestorReorgTest().main()
