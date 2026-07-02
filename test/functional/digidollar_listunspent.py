#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test DigiDollar list-unspent RPC semantics."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


ORACLE_PRICE_MICRO_USD = 500000


class DigiDollarListUnspentTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[
            "-digidollar=1",
            "-txindex=1",
            "-dandelion=0",
            "-debug=digidollar",
        ]] * self.num_nodes

    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def assert_single_utxo(self, utxos, txid, amount, confirmations):
        assert_equal(len(utxos), 1)
        utxo = utxos[0]
        assert_equal(utxo["txid"], txid)
        assert_equal(utxo["vout"], 1)
        assert_equal(utxo["amount"], amount)
        assert_equal(utxo["confirmations"], confirmations)
        assert_equal(utxo["spendable"], True)
        assert_equal(utxo["safe"], True)
        assert "scriptPubKey" in utxo
        assert "address" in utxo
        return utxo

    def run_test(self):
        node, receiver = self.nodes
        self.generate(node, 650)
        self.sync_all()
        for n in self.nodes:
            n.setmockoracleprice(ORACLE_PRICE_MICRO_USD)

        self.log.info("Fresh wallet has no DigiDollar unspent outputs")
        assert_equal(node.listdigidollarunspent(), [])
        assert_equal(node.listdigidollarutxos(), [])
        assert_raises_rpc_error(-8, "Minimum confirmations must be non-negative", node.listdigidollarunspent, -1)
        assert_raises_rpc_error(-8, "Maximum confirmations must be greater or equal", node.listdigidollarunspent, 2, 1)

        self.log.info("Unconfirmed DD outputs require minconf=0 and are omitted by default")
        mint_amount = 10000
        mint = node.mintdigidollar(mint_amount, 0)
        mint_txid = mint["txid"]
        assert_equal(node.listdigidollarunspent(), [])
        unconfirmed = self.assert_single_utxo(node.listdigidollarunspent(0), mint_txid, mint_amount, 0)
        assert_equal(node.listdigidollarutxos(0), node.listdigidollarunspent(0))
        assert_equal(node.listdigidollarunspent(1), [])
        assert_equal(node.listdigidollarunspent(0, 0), [unconfirmed])
        for field in ["txid", "vout", "address", "scriptPubKey", "amount", "confirmations", "spendable", "safe"]:
            assert field in unconfirmed
        assert isinstance(unconfirmed["amount"], int)

        self.log.info("Address filter matches normal listunspent behavior")
        dd_address = unconfirmed["address"]
        assert_equal(node.listdigidollarunspent(0, 9999999, [dd_address]), [unconfirmed])
        assert_equal(node.listdigidollarunspent(0, 9999999, [node.getdigidollaraddress()]), [])
        assert_raises_rpc_error(-8, "duplicated address", node.listdigidollarunspent, 0, 9999999, [dd_address, dd_address])
        assert_raises_rpc_error(-5, "Invalid DigiDollar address", node.listdigidollarunspent, 0, 9999999, ["not-an-address"])

        self.log.info("Confirmed DD outputs obey minconf/maxconf")
        node.generate(1)
        self.sync_all()
        confirmed = self.assert_single_utxo(node.listdigidollarunspent(), mint_txid, mint_amount, 1)
        assert_equal(confirmed["address"], dd_address)
        assert_equal(node.listdigidollarunspent(2), [])
        assert_equal(node.listdigidollarunspent(0, 0), [])
        node.generate(1)
        self.sync_all()
        assert_equal(node.listdigidollarunspent(1, 1), [])
        assert_equal(node.listdigidollarunspent(2, 2)[0]["txid"], mint_txid)
        assert_equal(node.listdigidollarutxos(2, 2), node.listdigidollarunspent(2, 2))

        self.log.info("Unconfirmed incoming DD outputs are unsafe and obey include_unsafe")
        receiver_address = receiver.getdigidollaraddress()
        send = node.senddigidollar(receiver_address, 2500)
        self.sync_mempools()
        receiver_unconfirmed = [u for u in receiver.listdigidollarunspent(0) if u["txid"] == send["txid"]]
        assert_equal(len(receiver_unconfirmed), 1)
        assert_equal(receiver_unconfirmed[0]["amount"], 2500)
        assert_equal(receiver_unconfirmed[0]["confirmations"], 0)
        assert_equal(receiver_unconfirmed[0]["safe"], False)
        assert_equal([u for u in receiver.listdigidollarunspent(0, 9999999, [], False) if u["txid"] == send["txid"]], [])
        assert_equal(receiver.listdigidollarutxos(0, 9999999, [], False), receiver.listdigidollarunspent(0, 9999999, [], False))

        self.log.info("Spent DD outputs disappear from listdigidollarunspent")
        assert_equal([u for u in node.listdigidollarunspent(0) if u["txid"] == mint_txid], [])
        node.generate(1)
        self.sync_all()
        post_spend = node.listdigidollarunspent()
        assert_equal([u for u in post_spend if u["txid"] == mint_txid], [])
        assert send["txid"] in [u["txid"] for u in post_spend]
        assert_equal(node.listdigidollarutxos(), post_spend)


if __name__ == "__main__":
    DigiDollarListUnspentTest().main()
