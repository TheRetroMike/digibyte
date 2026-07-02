#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Wave 8 - Oracle bundle reject reason matrix (functional, single-node).

Single-node `submitblock` coverage that mines a DD-touching block under
each of the failure modes that
`OracleDataValidator::ValidateBlockOracleData` enforces, and asserts the
exact reject reason emitted by `submitblock`. Complements the C++ unit
suite `digidollar_oracle_bundle_matrix_tests` and the multi-node
coverage owned by Agent C.

Cases:
  - DD block + no oracle output                    -> bad-oracle-missing
  - DD block + duplicated oracle output            -> bad-oracle-multiple-outputs
  - DD block + raw v0x01 oracle script             -> bad-oracle-malformed
  - DD block + raw v0x02 oracle script             -> bad-oracle-malformed
  - DD block + unknown version byte (0x04)         -> bad-oracle-malformed
  - DD block + truncated v0x03 oracle script       -> bad-oracle-malformed
"""

import struct

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import COIN, CTxOut
from test_framework.script import CScript, CScriptOp, OP_RETURN, OP_TRUE
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


OP_ORACLE = CScriptOp(0xBF)
REGTEST_CONFIRMATION_WINDOW = 144
ACTIVATION_HEIGHT = 200


def v01_oracle_script(price_micro_usd, timestamp, oracle_id=0):
    """Legacy v0x01 single-message oracle script (rejected as malformed)."""
    payload = bytes([oracle_id])
    payload += struct.pack("<Q", price_micro_usd)
    payload += struct.pack("<q", timestamp)
    return CScript([OP_RETURN, OP_ORACLE, b"\x01", payload])


def v02_oracle_script(price_micro_usd, timestamp):
    """Legacy v0x02 multi-message oracle script (rejected as malformed)."""
    # 1 message, price, timestamp, 1 oracle_id, 64-byte sig.
    payload = bytes([0x01])
    payload += struct.pack("<Q", price_micro_usd)
    payload += struct.pack("<q", timestamp)
    payload += bytes([0x00])
    payload += bytes(64)
    return CScript([OP_RETURN, OP_ORACLE, b"\x02", payload])


def unknown_version_oracle_script(version_byte):
    """Future / unknown bundle version byte."""
    payload = bytes(96)  # arbitrary fixed-size payload
    return CScript([OP_RETURN, OP_ORACLE, bytes([version_byte]), payload])


def truncated_v03_oracle_script():
    """Structurally incomplete v0x03 MuSig2 bundle."""
    return CScript([OP_RETURN, OP_ORACLE, b"\x03", b"\x01\x0f"])


class DigiDollarOracleBundleRejectMatrixTest(DigiByteTestFramework):
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

    def mine_baseline_dd_block(self, mock_price):
        node = self.nodes[0]
        node.setmockoracleprice(mock_price)
        mint = node.mintdigidollar(100000, 0)
        block_hash = node.generate(1)[0]
        block = node.getblock(block_hash)
        assert mint["txid"] in block["tx"]
        return block_hash

    def coinbase_oracle_output_count(self, block_hash):
        coinbase = self.nodes[0].getblock(block_hash, 2)["tx"][0]
        return sum(
            1
            for vout in coinbase["vout"]
            if vout["scriptPubKey"].get("hex", "").startswith("6abf")
        )

    def attempt_block_with_oracle_outputs(self, dd_txid_hex, oracle_scripts, label):
        """
        Build a block with the given DD tx (DD-touching) and supplied
        oracle scripts in the coinbase. Returns submitblock's reject reason
        string (or None if accepted).
        """
        node = self.nodes[0]

        prev_hash = int(node.getbestblockhash(), 16)
        prev_block_time = node.getblock(node.getbestblockhash())["time"]
        block_time = prev_block_time + 1
        height = node.getblockcount() + 1

        coinbase = create_coinbase(height)
        for s in oracle_scripts:
            coinbase.vout.append(CTxOut(0, s))
        coinbase.rehash()

        # Pull the DD mint tx body from the mempool to attach to the block.
        raw = node.getrawtransaction(dd_txid_hex, False)
        from test_framework.messages import CTransaction, tx_from_hex
        dd_tx = tx_from_hex(raw)

        block = create_block(prev_hash, coinbase, block_time)
        block.vtx.append(dd_tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()

        result = node.submitblock(block.serialize().hex())
        self.log.info("%s: submitblock => %r", label, result)
        return result

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Activate DigiDollar")
        node.generate(150)
        self.activate_digidollar()

        baseline_price = 500000
        node.setmockoracleprice(baseline_price)

        self.log.info("Mine an honest baseline DD block with a MuSig2 oracle bundle")
        baseline_hash = self.mine_baseline_dd_block(baseline_price)
        baseline_height = node.getblockcount()
        assert_equal(self.coinbase_oracle_output_count(baseline_hash), 1)

        # Each subsequent attempt creates a fresh DD mint tx in the
        # mempool and tries to mine it under a different oracle-bundle
        # failure mode.
        def fresh_mint():
            node.setmockoracleprice(baseline_price)
            return node.mintdigidollar(100000, 0)["txid"]

        self.log.info("DD block + no oracle output -> bad-oracle-missing")
        dd_txid = fresh_mint()
        result = self.attempt_block_with_oracle_outputs(
            dd_txid, oracle_scripts=[], label="no oracle"
        )
        assert_equal(result, "bad-oracle-missing")
        assert_equal(node.getblockcount(), baseline_height)

        self.log.info("DD block + raw v0x01 oracle -> bad-oracle-malformed")
        dd_txid = fresh_mint()
        result = self.attempt_block_with_oracle_outputs(
            dd_txid,
            oracle_scripts=[v01_oracle_script(777777, 1700000000)],
            label="v0x01 oracle",
        )
        assert_equal(result, "bad-oracle-malformed")
        assert_equal(node.getblockcount(), baseline_height)

        self.log.info("DD block + raw v0x02 oracle -> bad-oracle-malformed")
        dd_txid = fresh_mint()
        result = self.attempt_block_with_oracle_outputs(
            dd_txid,
            oracle_scripts=[v02_oracle_script(777777, 1700000000)],
            label="v0x02 oracle",
        )
        assert_equal(result, "bad-oracle-malformed")
        assert_equal(node.getblockcount(), baseline_height)

        self.log.info("DD block + unknown version 0x04 -> bad-oracle-malformed")
        dd_txid = fresh_mint()
        result = self.attempt_block_with_oracle_outputs(
            dd_txid,
            oracle_scripts=[unknown_version_oracle_script(0x04)],
            label="v0x04 unknown",
        )
        assert_equal(result, "bad-oracle-malformed")
        assert_equal(node.getblockcount(), baseline_height)

        self.log.info("DD block + truncated v0x03 oracle -> bad-oracle-malformed")
        dd_txid = fresh_mint()
        result = self.attempt_block_with_oracle_outputs(
            dd_txid,
            oracle_scripts=[truncated_v03_oracle_script()],
            label="truncated v0x03 oracle",
        )
        assert_equal(result, "bad-oracle-malformed")
        assert_equal(node.getblockcount(), baseline_height)

        self.log.info("DD block + duplicated oracle output -> bad-oracle-multiple-outputs")
        dd_txid = fresh_mint()
        # Use two raw v0x01 outputs so the first dedupe gate trips.
        oracle = v01_oracle_script(123456, 1700000000)
        result = self.attempt_block_with_oracle_outputs(
            dd_txid,
            oracle_scripts=[oracle, oracle],
            label="dup oracle outputs",
        )
        assert_equal(result, "bad-oracle-multiple-outputs")
        assert_equal(node.getblockcount(), baseline_height)


if __name__ == "__main__":
    DigiDollarOracleBundleRejectMatrixTest().main()
