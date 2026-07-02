#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Regression test for DD-RH-066 under V1 oracle rules.

Oracle validation/cache rollback must scan the whole coinbase for OP_ORACLE
data, not only vout[1]. V1 accepts only MuSig2 v0x03 oracle bundles.
"""

import struct

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CTxOut
from test_framework.script import CScript, CScriptOp, OP_RETURN, OP_TRUE
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


OP_ORACLE = CScriptOp(0xBF)
REGTEST_CONFIRMATION_WINDOW = 144


def oracle_script(price_micro_usd, timestamp, oracle_id=0):
    compact = bytes([oracle_id])
    compact += struct.pack("<Q", price_micro_usd)
    compact += struct.pack("<q", timestamp)
    return CScript([OP_RETURN, OP_ORACLE, b"\x01", compact])


class DigiDollarOracleReorgCacheTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-digidollaractivationheight=200",
            "-dandelion=0",
            "-txindex=1",
        ]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def oracle_vout_indexes(self, block_hash):
        coinbase = self.nodes[0].getblock(block_hash, 2)["tx"][0]
        return [
            i for i, vout in enumerate(coinbase["vout"])
            if vout["scriptPubKey"].get("hex", "").startswith("6abf")
        ]

    def mine_digidollar_mint(self, amount_cents, tier, expected_price):
        node = self.nodes[0]
        mint = node.mintdigidollar(amount_cents, tier)
        block_hash = node.generate(1)[0]
        block = node.getblock(block_hash)
        assert mint["txid"] in block["tx"]

        self.assert_musig2_oracle_output(block_hash)
        assert_equal(node.getmockoracleprice()["price_micro_usd"], expected_price)
        return block_hash

    def mine_digidollar_transfer(self, amount_cents, expected_price):
        node = self.nodes[0]
        transfer = node.senddigidollar(node.getdigidollaraddress(), amount_cents)
        block_hash = node.generate(1)[0]
        block = node.getblock(block_hash)
        assert transfer["txid"] in block["tx"]

        self.assert_musig2_oracle_output(block_hash)
        assert_equal(node.getmockoracleprice()["price_micro_usd"], expected_price)
        return block_hash

    def assert_musig2_oracle_output(self, block_hash):
        oracle_indexes = self.oracle_vout_indexes(block_hash)
        assert_equal(len(oracle_indexes), 1)
        assert oracle_indexes[0] > 1, "MuSig2 oracle bundle should be after the witness commitment"

    def activate_digidollar(self):
        node = self.nodes[0]
        while True:
            info = node.getdeploymentinfo()
            status = info["deployments"]["digidollar"]["bip9"]["status"]
            if status == "active":
                return
            current = node.getblockcount()
            remaining = REGTEST_CONFIRMATION_WINDOW - (current % REGTEST_CONFIRMATION_WINDOW)
            if remaining == 0:
                remaining = REGTEST_CONFIRMATION_WINDOW
            node.generate(remaining)

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Activate DigiDollar")
        node.generate(150)
        self.activate_digidollar()

        baseline_price = 500000
        attacker_price = 777777

        self.log.info("Mine a non-DD block without an oracle bundle")
        non_dd_hash = node.generate(1)[0]
        assert_equal(self.oracle_vout_indexes(non_dd_hash), [])

        node.setmockoracleprice(baseline_price)

        self.log.info("Mine an honest baseline DD block with a MuSig2 oracle bundle")
        baseline_hash = self.mine_digidollar_mint(100000, 0, baseline_price)
        baseline_height = node.getblockcount()

        self.log.info("Reject legacy v0x01 oracle data even when it appears at vout[2]")
        prev_hash = int(node.getbestblockhash(), 16)
        next_height = baseline_height + 1
        block_time = node.getblock(baseline_hash)["time"] + 1
        coinbase = create_coinbase(next_height)
        coinbase.vout.append(CTxOut(0, CScript([OP_TRUE])))
        coinbase.vout.append(CTxOut(0, oracle_script(attacker_price, block_time)))
        coinbase.rehash()

        block = create_block(prev_hash, coinbase, block_time)
        block.solve()
        assert_equal(node.submitblock(block.serialize().hex()), "bad-oracle-malformed")
        assert_equal(node.getblockcount(), baseline_height)
        assert_equal(node.getmockoracleprice()["price_micro_usd"], baseline_price)

        self.log.info("Mine a valid MuSig2 DD block on branch A")
        node.setmockoracleprice(attacker_price)
        attacker_hash = self.mine_digidollar_transfer(10000, attacker_price)
        assert_equal(node.getblockcount(), next_height)

        self.log.info("Invalidate branch A and require oracle cache/mock price to roll back")
        node.invalidateblock(attacker_hash)
        assert_equal(node.getblockcount(), baseline_height)
        assert_equal(node.getmockoracleprice()["price_micro_usd"], baseline_price)


if __name__ == '__main__':
    DigiDollarOracleReorgCacheTest().main()
