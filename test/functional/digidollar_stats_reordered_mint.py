#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Regression test for DD-RH-059: DigiDollar wallet/index state must account for a
consensus-valid mint when ordinary DGB change appears before the mint vault
outputs and DD OP_RETURN.
"""

from test_framework.messages import CTxWitness, tx_from_hex
from test_framework.script import OP_RETURN
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class DigiDollarStatsReorderedMintTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex=1", "-dandelion=0", "-digidollarstatsindex=1", "-walletbroadcast=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def reorder_change_before_collateral(self, raw_hex):
        tx = tx_from_hex(raw_hex)
        opreturn_index = next(
            i for i, txout in enumerate(tx.vout)
            if txout.scriptPubKey and txout.scriptPubKey[0] == OP_RETURN
        )
        change_index = next(
            i for i, txout in enumerate(tx.vout)
            if i not in (0, 1, opreturn_index)
            and txout.nValue > 0
            and not (txout.scriptPubKey and txout.scriptPubKey[0] == OP_RETURN)
        )

        original_outputs = tx.vout
        tx.vout = [
            original_outputs[change_index],
            original_outputs[0],
            original_outputs[1],
            original_outputs[opreturn_index],
        ] + [
            txout for i, txout in enumerate(original_outputs)
            if i not in (0, 1, change_index, opreturn_index)
        ]

        tx.wit = CTxWitness()
        tx.sha256 = None
        tx.hash = None
        return tx.serialize().hex()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mining spendable funds and activating DigiDollar")
        node.generate(200)
        node.setmockoracleprice(500000)

        self.log.info("Creating a signed mint template")
        dd_amount = 100
        mint = node.mintdigidollar(dd_amount, 0)
        raw_template = node.gettransaction(mint["txid"])["hex"]

        self.log.info("Reordering DGB change before the vault outputs and re-signing")
        reordered_unsigned = self.reorder_change_before_collateral(raw_template)
        signed = node.signrawtransactionwithwallet(reordered_unsigned)
        assert signed["complete"]

        accepted = node.testmempoolaccept([signed["hex"]], maxfeerate=0)[0]
        assert accepted["allowed"], accepted
        reordered_txid = node.sendrawtransaction(signed["hex"], maxfeerate=0)

        self.log.info("Mining the consensus-valid reordered mint")
        block_hash = node.generate(1)[0]
        assert reordered_txid in node.getblock(block_hash)["tx"]

        stats = node.getdigidollarstats()
        assert_equal(stats["total_dd_supply"], dd_amount)
        assert_equal(stats["active_positions"], 1)

        positions = node.listdigidollarpositions(False)
        assert_equal(len([p for p in positions if p["position_id"] == reordered_txid]), 1)


if __name__ == "__main__":
    DigiDollarStatsReorderedMintTest().main()
