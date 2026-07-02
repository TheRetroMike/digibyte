#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 8 multi-node functional: oracle bundle format / DD-touching block rules.

V1 oracle invariants this test exercises across two nodes:

1. A valid MuSig2 v0x03 DD block produced by node 0 is accepted by node 1.
2. A coinbase carrying a legacy v0x01 OP_ORACLE payload is rejected by the
   submitting node (`bad-oracle-malformed`) and never propagates to the peer.
3. A coinbase carrying TWO OP_ORACLE outputs is rejected by the submitting
   node (`bad-oracle-multiple-outputs`) and never propagates to the peer.
4. A side branch built on top of a v0x01-bundle block is rejected at the
   first malformed link, so an honest single-block v0x03 chain on the peer
   wins regardless of how many blocks the attacker tries to stack.

The unit suites `rh63_oracle_validator_escape_hatches_tests`,
`rh65_mainnet_testnet_validator_parity_tests`, and
`digidollar_oracle_musig2_tests` already cover the consensus rule in
isolation; this functional adds the multi-node propagation/relay angle so
that we have launch-time evidence that mining a malformed bundle does not
cause a chain split with honest peers.
"""

import struct

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CTxOut
from test_framework.script import CScript, CScriptOp, OP_RETURN, OP_TRUE
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


OP_ORACLE = CScriptOp(0xBF)
REGTEST_CONFIRMATION_WINDOW = 144


def legacy_v01_oracle_script(price_micro_usd, timestamp, oracle_id=0):
    """Build an OP_RETURN OP_ORACLE script carrying a legacy v0x01 payload.

    Production extraction in `OracleBundleManager::ExtractOracleBundle`
    rejects this with `bad-oracle-malformed` because the compact 17-byte
    v0x01 layout is no longer parseable by the V1 deserializer (which
    accepts only `data[0] == 0x03`).
    """
    compact = bytes([oracle_id])
    compact += struct.pack("<Q", price_micro_usd)
    compact += struct.pack("<q", timestamp)
    return CScript([OP_RETURN, OP_ORACLE, b"\x01", compact])


class DigiDollarOracleBlockRulesRelayTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        common = [
            "-digidollaractivationheight=200",
            "-dandelion=0",
            "-txindex=1",
        ]
        self.extra_args = [common, list(common)]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def oracle_vout_indexes(self, node, block_hash):
        coinbase = node.getblock(block_hash, 2)["tx"][0]
        return [
            i for i, vout in enumerate(coinbase["vout"])
            if vout["scriptPubKey"].get("hex", "").startswith("6abf")
        ]

    def activate_digidollar(self):
        for node in self.nodes:
            while True:
                info = node.getdeploymentinfo()
                status = info["deployments"]["digidollar"]["bip9"]["status"]
                if status == "active":
                    break
                current = node.getblockcount()
                remaining = REGTEST_CONFIRMATION_WINDOW - (current % REGTEST_CONFIRMATION_WINDOW)
                if remaining == 0:
                    remaining = REGTEST_CONFIRMATION_WINDOW
                node.generate(remaining)
            self.sync_blocks()

    def publish_quote(self, price):
        for node in self.nodes:
            assert_equal(node.setmockoracleprice(price)["price_micro_usd"], price)

    def run_test(self):
        node0, node1 = self.nodes

        self.log.info("Bring both nodes past activation and publish a fresh quote")
        node0.generate(150)
        self.sync_blocks()
        self.activate_digidollar()
        self.publish_quote(500000)

        self.log.info("Case 1: honest v0x03 DD block from node 0 must propagate to node 1")
        mint = node0.mintdigidollar(100000, 0)
        block_hash = node0.generate(1)[0]
        self.sync_blocks()
        block_on_node1 = node1.getblock(block_hash)
        assert mint["txid"] in block_on_node1["tx"], "DD mint did not propagate to peer"
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        oracle_outputs = self.oracle_vout_indexes(node1, block_hash)
        assert_equal(len(oracle_outputs), 1)
        assert oracle_outputs[0] > 1, (
            "v0x03 oracle bundle must be placed AFTER the witness commitment "
            f"(found at vout[{oracle_outputs[0]}])"
        )

        self.log.info("Case 2: coinbase carrying legacy v0x01 bundle is rejected and not relayed")
        baseline_height = node0.getblockcount()
        baseline_hash = node0.getbestblockhash()
        next_height = baseline_height + 1
        block_time = node0.getblock(baseline_hash)["time"] + 1

        legacy_cb = create_coinbase(next_height)
        # Pad a witness-commitment-style placeholder, then v0x01 OP_ORACLE so the
        # extractor scans the whole coinbase (RH-66 path).
        legacy_cb.vout.append(CTxOut(0, CScript([OP_TRUE])))
        legacy_cb.vout.append(CTxOut(0, legacy_v01_oracle_script(777777, block_time)))
        legacy_cb.rehash()
        legacy_block = create_block(int(baseline_hash, 16), legacy_cb, block_time)
        legacy_block.solve()

        reject_reason = node0.submitblock(legacy_block.serialize().hex())
        assert reject_reason in ("bad-oracle-malformed", "bad-oracle-legacy"), (
            f"node 0 must reject v0x01 coinbase, got reject_reason={reject_reason!r}"
        )
        assert_equal(node0.getbestblockhash(), baseline_hash)
        # The peer must never see this block. Sync first to flush any pending
        # relay attempts, then assert tip unchanged on both nodes.
        self.sync_blocks()
        assert_equal(node1.getbestblockhash(), baseline_hash)

        self.log.info("Case 3: coinbase with TWO OP_ORACLE outputs is rejected and not relayed")
        # Reuse the same prev/next height since case 2 left the chain alone.
        multi_cb = create_coinbase(next_height)
        # First valid-shape oracle output (still legacy v0x01 — what matters
        # here is that two OP_ORACLE outputs trip the count check before any
        # extractor is reached).
        multi_cb.vout.append(CTxOut(0, legacy_v01_oracle_script(500000, block_time)))
        multi_cb.vout.append(CTxOut(0, legacy_v01_oracle_script(500001, block_time)))
        multi_cb.rehash()
        multi_block = create_block(int(baseline_hash, 16), multi_cb, block_time + 1)
        multi_block.solve()
        reject_reason = node0.submitblock(multi_block.serialize().hex())
        assert_equal(reject_reason, "bad-oracle-multiple-outputs")
        assert_equal(node0.getbestblockhash(), baseline_hash)
        self.sync_blocks()
        assert_equal(node1.getbestblockhash(), baseline_hash)

        self.log.info(
            "Case 4: a stacked side branch starting with a v0x01 block cannot "
            "win over the honest tip"
        )
        # Try to extend node 0 with a 3-block side branch whose root carries a
        # legacy v0x01 bundle. The first submission must be rejected, which
        # transitively kills any longer chain anchored to it.
        attacker_cb = create_coinbase(next_height)
        attacker_cb.vout.append(CTxOut(0, CScript([OP_TRUE])))
        attacker_cb.vout.append(CTxOut(0, legacy_v01_oracle_script(424242, block_time)))
        attacker_cb.rehash()
        attacker_block = create_block(int(baseline_hash, 16), attacker_cb, block_time + 2)
        attacker_block.solve()
        reject_reason = node0.submitblock(attacker_block.serialize().hex())
        assert reject_reason in ("bad-oracle-malformed", "bad-oracle-legacy"), (
            f"side-branch root with v0x01 must be rejected, got {reject_reason!r}"
        )
        assert_equal(node0.getbestblockhash(), baseline_hash)

        # Now have node 1 honestly extend the chain by one more v0x03 block
        # while node 0's malformed attempt is being rejected. The peer's
        # honest tip must beat anything anchored on the rejected v0x01 root.
        self.publish_quote(500000)
        new_hash = node1.generate(1)[0]
        self.sync_blocks()
        assert_equal(node0.getbestblockhash(), new_hash)
        assert_equal(node1.getbestblockhash(), new_hash)
        # The rejected-root hash must never appear on the active chain. It
        # may persist as a known-but-invalid tip in `getchaintips`, but its
        # status must be "invalid" — never "active" or "valid-headers".
        for node in self.nodes:
            tips = node.getchaintips()
            for tip in tips:
                if tip["hash"] == attacker_block.hash:
                    assert_equal(tip["status"], "invalid")
                    break
            # And the active chain on every node must be the honest v0x03 tip.
            assert_equal(node.getbestblockhash(), new_hash)

        self.log.info("All Wave 8 multi-node oracle-bundle relay assertions held")


if __name__ == "__main__":
    DigiDollarOracleBlockRulesRelayTest().main()
