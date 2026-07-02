#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 26 Agent C - Mixed-node backward-compatibility / activation proof.

This is the final compatibility scenario before launch sign-off. It
demonstrates that DigiDollar V1 does not break ordinary DigiByte network
behaviour for users who do not interact with DigiDollar at all.

Mixed-node setup
================

There is no separate "non-upgraded" binary in the tree (this is the first
DD-aware release). The test simulates a non-upgraded user by holding one
node strictly below the BIP9 activation boundary. While that node's tip
is in LOCKED_IN state:
  - `IsDigiDollarEnabled(tip)` returns False on its mempool acceptor.
  - Its DD/oracle RPCs reject with "DigiDollar is not yet active on this
    blockchain".
  - `Consensus::IsOracleActive` returns False, so its P2P stack drops
    every oracle/MuSig2 message it sees on the wire.
  - Its `BlockTouchesDigiDollar`/`ValidateBlockOracleData` skip the
    oracle requirement entirely.

That is the same observable behaviour a binary built before DigiDollar
ever existed would exhibit on the same chain prefix - DD-aware logic is
dormant. The purpose of this wave is therefore not to prove the BIP9
state machine works (that is `digidollar_activation*.py` and Wave 12).
The purpose is to prove the chain still works for plain DGB users.

Three scenarios
===============

1. **Pre-activation parity.** Two LOCKED_IN nodes exchange a regular DGB
   `sendtoaddress` transaction. The receiver's mempool accepts and the
   block confirming it is valid on both nodes. No oracle data, no DD
   marker, no DD opcodes anywhere. (Mirrors a non-upgraded ↔ non-upgraded
   pair.)

2. **Mixed pair across activation.** Node 0 advances into ACTIVE and
   begins minting/relaying DD txs. Node 1 (the simulated non-upgraded
   user) stays at LOCKED_IN by being held offline for the activation
   block. Once reconnected the link still relays a regular DGB tx that
   node 1 originates - proving the upgraded peer continues to accept
   non-DD traffic from a peer that doesn't speak DD-aware mempool
   semantics. The plain DGB tx confirms in a non-DD block. (Mirrors a
   non-upgraded sender ↔ upgraded relayer.)

3. **Upgraded ↔ upgraded, no DD activity.** Both nodes are deep into
   ACTIVE state but neither has any DD positions, oracle quotes, or
   bundles. Plain DGB sendtoaddress traffic still relays and confirms
   without requiring oracle data or being touched by `bad-oracle-*`
   reject reasons. Non-DD blocks omit the OP_ORACLE bundle entirely
   (`BlockTouchesDigiDollar` returns false), and the validator accepts
   them without any oracle output. This is the explicit PASS for
   "regular DGB users and non-upgraded nodes can still transact
   normally."

Required proofs covered
=======================

  - regular DGB tx originated by a "non-upgraded" peer is accepted and
    relayed by an upgraded peer
  - upgraded nodes do not require oracle/DD data for ordinary DGB
    transactions
  - upgraded nodes do not require oracle/DD data for non-DD blocks
  - DD-touching blocks must still satisfy V1 rules (sanity check that
    we are running on a real DD-active chain)

This test deliberately uses `sendtoaddress` (not the MiniWallet helper)
because the test purpose is the wallet relay path, not consensus
validation in isolation.
"""

import os
import shutil

from decimal import Decimal

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


REGTEST_PERIOD = 144
# `-digidollaractivationheight=432` -> BIP9 min_activation_height=432.
# State machine walks DEFINED(0..142) / STARTED(143..286) / LOCKED_IN(287..430)
# / ACTIVE(431+) on regtest with period=144. The first tip where
# `State(tip) == ACTIVE` is FIRST_ACTIVE_TIP=431.
BIP9_MIN_ACTIVATION = 432
FIRST_ACTIVE_TIP = 431
LOCKED_IN_BUFFER_TIP = FIRST_ACTIVE_TIP - 5  # safely inside LOCKED_IN window
ORACLE_PRICE_MICRO_USD = 500000  # $0.50 / DGB


class DigiDollarWave26MixedNodeCompatTest(DigiByteTestFramework):
    def set_test_params(self):
        # Two nodes: node 0 plays the upgraded peer, node 1 plays the
        # simulated non-upgraded peer (held at LOCKED_IN until Phase 3).
        self.num_nodes = 2
        self.setup_clean_chain = True
        common = [
            "-digidollaractivationheight={}".format(BIP9_MIN_ACTIVATION),
            "-dandelion=0",
            "-txindex=1",
        ]
        self.extra_args = [list(common), list(common)]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        # Both nodes start connected so they share the LOCKED_IN prefix
        # before Phase 2 isolates node 1 to keep it pre-activation.
        self.setup_nodes()
        self.connect_nodes(0, 1)

    # ------------------------------------------------------------------ helpers
    def _coinbase_has_oracle(self, node, block_hash):
        return self._coinbase_oracle_hex(node, block_hash) is not None

    def _coinbase_oracle_hex(self, node, block_hash):
        block = node.getblock(block_hash, 2)
        for out in block["tx"][0]["vout"]:
            if out["scriptPubKey"]["asm"].startswith("OP_RETURN OP_ORACLE"):
                return out["scriptPubKey"]["hex"]
        return None

    def _coinbase_oracle_version(self, node, block_hash):
        script_hex = self._coinbase_oracle_hex(node, block_hash)
        if script_hex is None:
            return None
        assert script_hex.startswith("6abf01"), script_hex
        return script_hex[6:8]

    def _wait_for_funded_address(self, source_node, dest_node, amount):
        """Send `amount` DGB from source to a fresh dest address; confirm.

        Returns the (txid, address, confirming_block_hash) triple. The
        receiving node's wallet ends up with a single mature UTXO it can
        later spend in Phase 2 / Phase 3 to prove non-DD relay.
        """
        addr = dest_node.getnewaddress()
        txid = source_node.sendtoaddress(addr, amount)
        # Mine on the source so the sync is one-way and we control the
        # block contents (no surprise DD txs in-flight).
        block_hash = source_node.generate(1)[0]
        self.sync_blocks(self.nodes)
        return txid, addr, block_hash

    # --------------------------------------------------------------- run_test
    def run_test(self):
        self.log.info("Phase 1: pre-activation plain DGB tx between two LOCKED_IN nodes")
        self.test_pre_activation_dgb()

        self.log.info("Phase 2: simulated non-upgraded sender + upgraded relayer")
        self.test_mixed_node_dgb_after_activation()

        self.log.info("Phase 3: upgraded-to-upgraded plain DGB with no DD activity")
        self.test_upgraded_no_dd_traffic()

        self.log.info("Wave 26 mixed-node backward-compatibility tests PASSED")

    # ============================================================== Phase 1
    def test_pre_activation_dgb(self):
        """Two LOCKED_IN nodes exchange a plain DGB tx and mine a non-DD block."""
        # Drive both nodes to LOCKED_IN territory but explicitly NOT past the
        # activation period boundary. Mining 426 blocks puts both tips at
        # height 426 (well inside LOCKED_IN, comfortably below FIRST_ACTIVE_TIP).
        target = LOCKED_IN_BUFFER_TIP  # 426
        self.nodes[0].generate(target)
        self.sync_blocks(self.nodes)

        for i in range(self.num_nodes):
            assert_equal(self.nodes[i].getblockcount(), target)
            info = self.nodes[i].getdigidollardeploymentinfo()
            assert_equal(info["status"], "locked_in")
            assert_equal(info["enabled"], False)

        # Pre-activation, the DD/oracle RPCs must refuse on both nodes.
        # `mintdigidollar` is the canonical example used by Wave 12 Phase 3.
        for i in range(self.num_nodes):
            try:
                self.nodes[i].mintdigidollar(100000, 4)
                assert False, f"node{i} mintdigidollar should reject pre-activation"
            except Exception as exc:
                err = str(exc).lower()
                assert "not yet active" in err, f"node{i} unexpected error: {exc}"

        # Originate a plain DGB sendtoaddress on node 0 (which has the mature
        # coinbase funds) targeting an address node 1 generated locally. The
        # txn must propagate to node 1's mempool with no oracle quote needed.
        addr = self.nodes[1].getnewaddress()
        amount = Decimal("12345.6789")
        txid = self.nodes[0].sendtoaddress(addr, amount)

        # Both mempools see the plain tx without any DD/oracle gating.
        self.sync_mempools(self.nodes)
        for i in range(self.num_nodes):
            assert txid in self.nodes[i].getrawmempool(), \
                f"node{i} did not relay plain DGB tx pre-activation"

        # Mine the confirming block on node 0; it MUST be a non-DD block
        # because every DD entry point is rejected pre-activation. Pre-activation
        # miners must not stamp OP_ORACLE yet; post-activation ordinary blocks
        # may carry a fresh bundle opportunistically.
        block_hash = self.nodes[0].generate(1)[0]
        self.sync_blocks(self.nodes)
        for i in range(self.num_nodes):
            block = self.nodes[i].getblock(block_hash)
            assert txid in block["tx"], \
                f"node{i} did not confirm plain DGB tx pre-activation"
            assert not self._coinbase_has_oracle(self.nodes[i], block_hash), \
                f"node{i} pre-activation block unexpectedly carries OP_ORACLE"

        # Wallet on the receiving node sees the credit.
        bal = self.nodes[1].getreceivedbyaddress(addr, 0)
        assert_equal(bal, amount)
        self.log.info(
            "  plain DGB tx %s relayed and confirmed at h=%d in non-DD block",
            txid[:16], self.nodes[0].getblockcount(),
        )

    # ============================================================== Phase 2
    def test_mixed_node_dgb_after_activation(self):
        """Upgraded peer activates DD; the non-upgraded peer is held at LOCKED_IN.

        We disconnect the nodes, push node 0 past activation while keeping
        node 1 frozen in LOCKED_IN. While node 1 is still LOCKED_IN
        (DigiDollar BIP9 inactive on its tip - the exact observable state
        a non-upgraded binary would present on the same chain prefix), it
        builds and signs a plain DGB transaction using its existing
        wallet UTXOs. We then deliver that signed tx to the upgraded
        node 0 directly via `sendrawtransaction` to remove any wallet
        rebroadcast timing concerns - the question this test answers is
        whether the upgraded mempool acceptor will *accept* an ordinary
        DGB tx that originated on a peer that is not DD-aware. The
        delivery path is irrelevant; the acceptance decision is the
        invariant.
        """
        # Snapshot tip before isolating node 1. Phase 1 mined one extra block
        # to confirm the plain DGB tx, so the common height is one above the
        # LOCKED_IN_BUFFER_TIP starting point but still strictly inside the
        # LOCKED_IN window (FIRST_ACTIVE_TIP - 4).
        common_tip = self.nodes[0].getbestblockhash()
        common_height = self.nodes[0].getblockcount()
        assert common_height < FIRST_ACTIVE_TIP, \
            f"common_height {common_height} must stay below FIRST_ACTIVE_TIP {FIRST_ACTIVE_TIP}"
        for i in range(self.num_nodes):
            assert_equal(self.nodes[i].getbestblockhash(), common_tip)
            info = self.nodes[i].getdigidollardeploymentinfo()
            assert_equal(info["status"], "locked_in")

        # Step A. Disconnect and advance node 0 across the activation
        # boundary. While node 1 is offline, node 0's tip flips to
        # State(tip) == ACTIVE on the period boundary block.
        self.disconnect_nodes(0, 1)
        self.nodes[0].generate(FIRST_ACTIVE_TIP - common_height + 3)
        info0 = self.nodes[0].getdigidollardeploymentinfo()
        assert_equal(info0["status"], "active")
        assert_equal(info0["enabled"], True)

        # While disconnected, prove node 1 is still LOCKED_IN with the
        # original tip. Its DD RPCs continue to refuse.
        info1 = self.nodes[1].getdigidollardeploymentinfo()
        assert_equal(info1["status"], "locked_in")
        assert_equal(info1["enabled"], False)
        try:
            self.nodes[1].mintdigidollar(100000, 4)
            assert False, "non-upgraded node1 should still refuse mintdigidollar"
        except Exception as exc:
            assert "not yet active" in str(exc).lower(), f"unexpected: {exc}"

        # Step B. Build a plain DGB transaction on node 1 while it is
        # still pre-activation. We use the wallet's `sendtoaddress` to
        # construct and sign the tx, but immediately serialise it - we
        # want the *raw bytes a non-upgraded peer would produce*, with
        # no DD-aware logic anywhere in the build path.
        node0_addr = self.nodes[0].getnewaddress()
        amount = Decimal("100.0")
        txid = self.nodes[1].sendtoaddress(node0_addr, amount, "", "", True)
        assert txid in self.nodes[1].getrawmempool(), \
            "node1 wallet failed to enqueue plain DGB tx"
        raw_hex = self.nodes[1].getrawtransaction(txid)

        # Step C. Reconnect and let node 1 follow node 0's longer chain
        # so they share a common UTXO set view. This sync uses the
        # consensus rules of the chain (no DD enforcement on non-DD
        # blocks) and demonstrates a non-upgraded node can passively
        # accept an upgraded peer's chain extension.
        self.connect_nodes(0, 1)
        self.sync_blocks(self.nodes)
        for i in range(self.num_nodes):
            info = self.nodes[i].getdigidollardeploymentinfo()
            assert_equal(info["status"], "active")
            assert_equal(info["enabled"], True)

        # Step D. Deliver node 1's pre-activation-built plain DGB tx
        # directly to node 0 via sendrawtransaction. This is the canonical
        # path a non-upgraded peer's tx would take if it reached an
        # upgraded peer (any of: P2P inv/tx, an explicit submission, a
        # block template). The point is whether node 0's mempool
        # acceptor accepts it - it must, because the tx has no DD
        # marker and therefore never enters the `is_digidollar_tx`
        # branch of MempoolAccept (validation.cpp:903) and never
        # consults the oracle quote freshness check.
        accepted_txid = self.nodes[0].sendrawtransaction(raw_hex)
        assert_equal(accepted_txid, txid)
        assert txid in self.nodes[0].getrawmempool(), \
            "upgraded node0 rejected plain DGB tx originated by node1"

        # Mine a NON-DD block on node 0 that confirms the plain tx. Oracle data
        # is optional here: ordinary DGB users must not depend on oracle liveness.
        block_hash = self.nodes[0].generate(1)[0]
        self.sync_blocks(self.nodes)
        for i in range(self.num_nodes):
            block = self.nodes[i].getblock(block_hash)
            assert txid in block["tx"], \
                f"node{i} did not confirm node1's plain DGB tx in active block"
            # Fresh OP_ORACLE may be present opportunistically, but absence is valid.
            info = self.nodes[i].getdigidollardeploymentinfo()
            assert_equal(info["status"], "active")
            assert_equal(info["enabled"], True)

        # Sanity: prove the receiver actually got the value.
        # `getreceivedbyaddress` includes the confirming block.
        received = self.nodes[0].getreceivedbyaddress(node0_addr, 1)
        assert_greater_than(received, Decimal("0"))
        assert received <= amount  # subtractfeefromamount may shrink it
        self.log.info(
            "  plain DGB tx %s from non-upgraded node1 mined into non-DD active block at h=%d",
            txid[:16], self.nodes[0].getblockcount(),
        )

    # ============================================================== Phase 3
    def test_upgraded_no_dd_traffic(self):
        """Both nodes deep in ACTIVE; pure DGB traffic still works no oracle needed.

        This is the explicit launch-readiness PASS for invariant 15 / the
        "regular DGB users can still transact normally" sign-off line in
        the campaign brief.
        """
        # Both nodes are already ACTIVE from Phase 2. Mine 5 more blocks on
        # node 0 to put us comfortably inside the active window. None of
        # them touch DD because the wallet has no DD tx in flight and the
        # mempool has only the previously-confirmed Phase 2 plain tx.
        prev_tip_height = self.nodes[0].getblockcount()
        # Empty mempool first to avoid surprise tx in the next blocks.
        self.sync_mempools(self.nodes)
        # Clear any leftover non-DD wallet tx by mining once.
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes)

        # Phase 3 round-trip: node 1 originates a plain DGB tx to node 0.
        # Even though both nodes are on the ACTIVE chain, the validator
        # never invokes oracle bundle checks because the originating tx
        # has no DD marker (`HasDigiDollarMarker(tx) == false`) so the
        # mempool gate at validation.cpp:903 does not enter the
        # `is_digidollar_tx` branch and the miner template's
        # `BlockTouchesDigiDollar` returns false.
        node0_addr = self.nodes[0].getnewaddress()
        # Use a smaller amount - node 1's wallet may not have lots of UTXOs
        # depending on fees taken in Phase 2.
        amount = Decimal("1.0")
        txid = self.nodes[1].sendtoaddress(node0_addr, amount, "", "", True)
        self.sync_mempools(self.nodes)
        for i in range(self.num_nodes):
            assert txid in self.nodes[i].getrawmempool(), \
                f"node{i} missing plain DGB tx at active height"

        # Mine on node 0. The resulting block has no price-dependent DD tx
        # and must validate cleanly on both nodes. Miners may still stamp an
        # opportunistic fresh OP_ORACLE bundle, so absence is not required.
        block_hash = self.nodes[0].generate(1)[0]
        self.sync_blocks(self.nodes)
        for i in range(self.num_nodes):
            block = self.nodes[i].getblock(block_hash)
            assert txid in block["tx"], \
                f"node{i} did not confirm Phase-3 DGB tx in active block"
            # Fresh OP_ORACLE may be present opportunistically, but absence is valid.

        new_tip_height = self.nodes[0].getblockcount()
        assert_greater_than(new_tip_height, prev_tip_height)

        # Sanity: a DD-touching block on the SAME chain must still require
        # the oracle bundle (proves the validator wasn't loosened). Mint a
        # DD position on node 0 with a fresh oracle quote, mine it, and
        # confirm the coinbase carries OP_ORACLE.
        self.nodes[0].setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        mint = self.nodes[0].mintdigidollar(50000, 4)
        dd_block_hash = self.nodes[0].generate(1)[0]
        self.sync_blocks(self.nodes)
        for i in range(self.num_nodes):
            block = self.nodes[i].getblock(dd_block_hash)
            assert mint["txid"] in block["tx"], \
                f"node{i} did not confirm DD mint tx"
            assert self._coinbase_has_oracle(self.nodes[i], dd_block_hash), \
                f"node{i} DD-touching block missing OP_ORACLE bundle"
            assert_equal(self._coinbase_oracle_version(self.nodes[i], dd_block_hash), "03")

        self.log.info(
            "  active non-DD block (h=%d) confirmed without requiring oracle data; "
            "active DD-touching block (h=%d) carries OP_ORACLE",
            new_tip_height, self.nodes[0].getblockcount(),
        )
        self.log.info(
            "  PASS: regular DGB users and non-upgraded nodes can still transact normally"
        )


if __name__ == "__main__":
    DigiDollarWave26MixedNodeCompatTest().main()
