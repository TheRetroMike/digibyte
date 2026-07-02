#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional guards for collateral-vault spend enforcement.

This test pins the wallet/RPC/mempool/block boundary for spends of a real
wallet-created DigiDollar collateral vault. Unit tests cover the validator's
synthetic fixtures; this functional case proves the same reject reasons surface
through user-facing raw transaction RPCs and submitblock.
"""

from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    tx_from_hex,
)
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal
from test_framework.address import address_to_scriptpubkey


REGTEST_CONFIRMATION_WINDOW = 144
ACTIVATION_HEIGHT = 200
ORACLE_PRICE_MICRO_USD = 500_000
DD_TX_TRANSFER_VERSION = (2 << 24) | 0x0770
DD_TX_REDEEM_VERSION = (3 << 24) | 0x0770


def dgb_to_sat(value):
    return int(Decimal(str(value)) * COIN)


class DigiDollarCollateralSpendGuardsTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            f"-digidollaractivationheight={ACTIVATION_HEIGHT}",
            "-dandelion=0",
            "-txindex=1",
        ]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def activate_digidollar(self, node):
        while True:
            info = node.getdeploymentinfo()
            if info["deployments"]["digidollar"]["bip9"]["status"] == "active":
                return
            current = node.getblockcount()
            remaining = REGTEST_CONFIRMATION_WINDOW - (current % REGTEST_CONFIRMATION_WINDOW)
            node.generate(remaining or REGTEST_CONFIRMATION_WINDOW)

    def make_plain_collateral_spend(self, node, mint_txid, collateral_sats):
        dest = node.getnewaddress()
        raw = node.createrawtransaction(
            [{"txid": mint_txid, "vout": 0, "sequence": 0xfffffffe}],
            {dest: Decimal(collateral_sats - 100_000) / COIN},
            node.getblockcount(),
        )
        tx = tx_from_hex(raw)
        assert_equal(tx.nVersion, 2)
        return tx.serialize().hex()

    def with_version(self, raw_hex, version):
        tx = tx_from_hex(raw_hex)
        tx.nVersion = version
        tx.rehash()
        return tx.serialize().hex()

    def make_partial_burn_redeem(self, node, mint_txid, mint_tx, collateral_sats, dd_amount):
        dd_change = dd_amount // 2
        tx = CTransaction()
        tx.nVersion = DD_TX_REDEEM_VERSION
        tx.nLockTime = node.getblockcount()
        tx.vin = [
            CTxIn(COutPoint(int(mint_txid, 16), 0), b"", 0xfffffffe),
            CTxIn(COutPoint(int(mint_txid, 16), 1), b"", 0xfffffffe),
        ]
        tx.vout = [
            CTxOut(collateral_sats - 100_000, address_to_scriptpubkey(node.getnewaddress())),
            CTxOut(0, bytes.fromhex(mint_tx["vout"][1]["scriptPubKey"]["hex"])),
            CTxOut(0, CScript([OP_RETURN, b"DD", b"\x03", dd_change])),
        ]
        tx.rehash()
        return tx.serialize().hex()

    def assert_mempool_reject(self, node, raw_hex, reason):
        result = node.testmempoolaccept([raw_hex], maxfeerate=0)[0]
        assert_equal(result["allowed"], False)
        assert_equal(result["reject-reason"], reason)
        try:
            node.sendrawtransaction(raw_hex, maxfeerate=0)
            raise AssertionError(f"sendrawtransaction unexpectedly accepted tx expected to fail with {reason}")
        except JSONRPCException as exc:
            assert reason in str(exc), f"expected {reason} in sendrawtransaction error, got {exc}"

    def submit_bad_block(self, node, raw_tx, expected_reason):
        prev_hash_hex = node.getbestblockhash()
        prev_hash = int(prev_hash_hex, 16)
        block_time = node.getblock(prev_hash_hex)["time"] + 1
        next_height = node.getblockcount() + 1
        coinbase = create_coinbase(next_height)
        coinbase.rehash()
        block = create_block(prev_hash, coinbase, block_time)
        block.vtx.append(tx_from_hex(raw_tx))
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()

        baseline_hash = node.getbestblockhash()
        baseline_height = node.getblockcount()
        assert_equal(node.submitblock(block.serialize().hex()), expected_reason)
        assert_equal(node.getbestblockhash(), baseline_hash)
        assert_equal(node.getblockcount(), baseline_height)

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Activate DigiDollar and prepare a mature wallet")
        node.generate(160)
        self.activate_digidollar(node)
        node.generate(120)
        node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)

        self.log.info("Mint and mature a real collateral vault")
        dd_amount = 100_000
        mint = node.mintdigidollar(dd_amount, 0)
        mint_txid = mint["txid"]
        node.generate(1)
        mint_tx = node.getrawtransaction(mint_txid, True)
        collateral_sats = dgb_to_sat(mint_tx["vout"][0]["value"])
        info = node.getredemptioninfo(mint_txid)
        if node.getblockcount() < info["unlock_height"]:
            node.generate(info["unlock_height"] - node.getblockcount())
        node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)

        self.log.info("Ordinary DGB spend of collateral vault is rejected by RPC/mempool")
        plain_spend = self.make_plain_collateral_spend(node, mint_txid, collateral_sats)
        self.assert_mempool_reject(
            node,
            plain_spend,
            "bad-collateral-spend-missing-dd-burn",
        )

        self.log.info("Block containing ordinary collateral spend is rejected by ConnectBlock")
        self.submit_bad_block(
            node,
            plain_spend,
            "bad-collateral-spend-missing-dd-burn",
        )

        self.log.info("DD-marked non-redeem spend of collateral vault is rejected")
        transfer_spend = self.with_version(plain_spend, DD_TX_TRANSFER_VERSION)
        self.assert_mempool_reject(
            node,
            transfer_spend,
            "bad-collateral-spend-missing-dd-burn",
        )

        self.log.info("Redeem missing DD burn is rejected before script checks")
        missing_burn = self.with_version(plain_spend, DD_TX_REDEEM_VERSION)
        self.assert_mempool_reject(
            node,
            missing_burn,
            "bad-redeem-insufficient-inputs",
        )

        self.log.info("Redeem with partial DD burn cannot release collateral")
        partial_burn = self.make_partial_burn_redeem(
            node,
            mint_txid,
            mint_tx,
            collateral_sats,
            dd_amount,
        )
        self.assert_mempool_reject(
            node,
            partial_burn,
            "bad-collateral-release-partial-burn",
        )


if __name__ == "__main__":
    DigiDollarCollateralSpendGuardsTest().main()
