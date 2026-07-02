#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for redeeming with received DD in an encrypted wallet."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class WalletDigiDollarEncryptedReceivedRedeemTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def mine_sync(self, blocks=1):
        self.generate(self.nodes[0], blocks)
        self.sync_all()

    def run_test(self):
        miner = self.nodes[0]
        encrypted = self.nodes[1]
        passphrase = "DigiDollarReceivedRedeemPass123!"
        amount = 100000

        self.log.info("Activating DigiDollar and funding the encrypted wallet")
        self.generate(miner, 660)
        self.sync_all()
        for node in self.nodes:
            node.setmockoracleprice(500000)

        encrypted_addr = encrypted.getnewaddress()
        miner.sendtoaddress(encrypted_addr, 500000)
        self.mine_sync(10)

        self.log.info("Encrypting wallet and minting a redeemable position")
        encrypted.encryptwallet(passphrase)
        self.restart_node(1)
        self.connect_nodes(0, 1)
        self.sync_all()
        for node in self.nodes:
            node.setmockoracleprice(500000)

        encrypted = self.nodes[1]
        encrypted.walletpassphrase(passphrase, 600)
        mint = encrypted.mintdigidollar(amount, 0)
        position_id = mint["position_id"]
        unlock_height = mint["unlock_height"]
        self.sync_mempools()
        self.mine_sync()
        assert_equal(encrypted.getdigidollarbalance()["total"], amount)

        self.log.info("Moving the original minted DD away from the encrypted wallet")
        miner_receive = miner.getdigidollaraddress()
        sent_away = encrypted.senddigidollar(miner_receive, amount)
        assert "txid" in sent_away
        self.sync_mempools()
        self.mine_sync()
        assert_equal(encrypted.getdigidollarbalance()["total"], 0)
        assert_equal(miner.getdigidollarbalance()["total"], amount)

        self.log.info("Sending replacement DD back to the encrypted wallet")
        encrypted.walletpassphrase(passphrase, 600)
        encrypted_receive = encrypted.getdigidollaraddress()
        sent_back = miner.senddigidollar(encrypted_receive, amount)
        assert "txid" in sent_back
        self.sync_mempools()
        self.mine_sync()
        assert_equal(encrypted.getdigidollarbalance()["total"], amount)

        current_height = miner.getblockcount()
        if current_height <= unlock_height:
            self.log.info("Advancing to the tier-0 unlock height")
            self.mine_sync(unlock_height - current_height + 1)

        self.log.info("Redeeming the original collateral using received DD")
        encrypted.walletpassphrase(passphrase, 600)
        redeem = encrypted.redeemdigidollar(position_id, amount)
        assert "txid" in redeem
        self.sync_mempools()

        positions = encrypted.listdigidollarpositions(False)
        pending = [p for p in positions if p["position_id"] == position_id]
        assert_equal(len(pending), 1)
        assert_equal(pending[0]["status"], "pending_redeem")

        # The redeem was created by node1, while mine_sync mines on node0. Publish
        # a fresh mock MuSig2 quote on the miner for the next block height so the
        # pending DD redeem is eligible for block inclusion.
        miner.setmockoracleprice(500000)
        self.mine_sync()

        assert_equal(encrypted.getdigidollarbalance()["total"], 0)
        positions = encrypted.listdigidollarpositions(False)
        redeemed = [p for p in positions if p["position_id"] == position_id]
        assert_equal(len(redeemed), 1)
        assert_equal(redeemed[0]["status"], "redeemed")


if __name__ == "__main__":
    WalletDigiDollarEncryptedReceivedRedeemTest().main()
