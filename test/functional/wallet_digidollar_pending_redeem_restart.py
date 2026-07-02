#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test DigiDollar wallet state when a pending redeem leaves the mempool.

Redeem creation marks the position inactive so users cannot submit another
redeem while the spend is pending. If that unconfirmed redeem is not in mempool
after restart, the active chain still contains the collateral UTXO and the
wallet must make the position active/redeemable again.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal

ORACLE_PRICE_MICRO_USD = 500000


def position_is_active(position):
    return position.get("is_active", position.get("status") in ("active", "unlocked"))


class WalletDigiDollarPendingRedeemRestartTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex=1"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining spendable funds")
        node.generate(200)
        result = node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        assert_equal(result["price_micro_usd"], ORACLE_PRICE_MICRO_USD)

        self.log.info("Minting and confirming tier-0 DigiDollar")
        mint = node.mintdigidollar(100000, 0)
        position_id = mint["position_id"]
        node.generate(1)

        position = next(p for p in node.listdigidollarpositions(False) if p["position_id"] == position_id)
        blocks_needed = max(0, position["unlock_height"] - node.getblockcount())
        if blocks_needed:
            node.generate(blocks_needed)

        self.log.info("Creating a pending redeem")
        result = node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        assert_equal(result["price_micro_usd"], ORACLE_PRICE_MICRO_USD)
        redeem = node.redeemdigidollar(position_id, 100000)
        redeem_txid = redeem["txid"]
        assert redeem_txid in node.getrawmempool()

        history_rows = [tx for tx in node.listdigidollartxs(20, 0) if tx["txid"] == redeem_txid and tx["category"] == "redeem"]
        assert_equal(len(history_rows), 1)
        assert_equal(history_rows[0]["wallet_state"], "pending")
        assert_equal(history_rows[0]["in_mempool"], True)
        assert_equal(history_rows[0]["abandoned"], False)

        position = next(p for p in node.listdigidollarpositions(False) if p["position_id"] == position_id)
        assert_equal(position_is_active(position), False)
        assert_equal(position["status"], "pending_redeem")
        redemption_info = node.getredemptioninfo(position_id)
        assert_equal(redemption_info["status"], "pending_redeem")
        assert_equal(redemption_info["can_redeem"], False)

        self.log.info("Restarting without mempool persistence or wallet rebroadcast")
        self.restart_node(0, extra_args=["-txindex=1", "-persistmempool=0", "-walletbroadcast=0"])
        node = self.nodes[0]
        node.syncwithvalidationinterfacequeue()
        assert redeem_txid not in node.getrawmempool()

        position = next(p for p in node.listdigidollarpositions(False) if p["position_id"] == position_id)
        assert_equal(position_is_active(position), True)

        balance = node.getdigidollarbalance()
        assert_equal(balance["total"], 100000)

        self.restart_node(0, extra_args=["-txindex=1"])
        node = self.nodes[0]
        node.syncwithvalidationinterfacequeue()

        result = node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        assert_equal(result["price_micro_usd"], ORACLE_PRICE_MICRO_USD)
        retry = node.redeemdigidollar(position_id, 100000)
        assert "txid" in retry
        retry_txid = retry["txid"]
        assert retry_txid in node.getrawmempool()

        history_rows = [tx for tx in node.listdigidollartxs(20, 0) if tx["txid"] == retry_txid and tx["category"] == "redeem"]
        assert_equal(len(history_rows), 1)
        assert_equal(history_rows[0]["wallet_state"], "pending")
        assert_equal(history_rows[0]["in_mempool"], True)

        node.generate(1)
        node.syncwithvalidationinterfacequeue()

        history_rows = [tx for tx in node.listdigidollartxs(20, 0) if tx["txid"] == retry_txid and tx["category"] == "redeem"]
        assert_equal(len(history_rows), 1)
        assert_equal(history_rows[0]["wallet_state"], "confirmed")
        assert_equal(history_rows[0]["abandoned"], False)

        position = next(p for p in node.listdigidollarpositions(False) if p["position_id"] == position_id)
        assert_equal(position["status"], "redeemed")
        redemption_info = node.getredemptioninfo(position_id)
        assert_equal(redemption_info["status"], "redeemed")
        assert_equal(redemption_info["can_redeem"], False)


if __name__ == "__main__":
    WalletDigiDollarPendingRedeemRestartTest().main()
