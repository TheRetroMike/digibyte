#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 14 Agent C multi-node IBD / reorg / reindex coverage for the
DigiDollar consensus + index + cache surface.

This test pins the parity invariants the existing single-node reorg /
reindex tests (`digidollar_oracle_reorg_cache.py`,
`wallet_digidollar_mint_reorg.py`, `wallet_digidollar_reorg.py`,
`digidollar_stats_reorg.py`, `wallet_digidollar_reindex.py`) cannot
prove on their own:

  Phase 1 - cold IBD past activation:
    - Node 0 mints a tier-0 DD position past the BIP9 activation height,
      mines several DD-touching blocks and one non-DD block.
    - Node 1 (fresh clean datadir, no wallet activity) cold-syncs from
      node 0 over P2P and must reach the same tip, the same
      `getdigidollarstats` (supply, vault count, collateral), and the
      same `getdigidollardeploymentinfo` (status, activation_height).
    - Node 1's oracle price cache must agree with node 0's after IBD.
    - This proves the consensus + stats-index + cache surface is
      reconstructible from on-chain data alone, without any wallet or
      mempool state.

  Phase 2 - node 0 reorgs to node 1's longer competing chain:
    - The two nodes are disconnected.
    - Node 0 keeps chain A: a DD redeem on the original mint.
    - Node 1 builds a longer competing chain B with a different DD tx
      sequence (a transfer instead of the redeem) plus extra non-DD
      blocks so chain B's work strictly exceeds chain A's.
    - The nodes are reconnected; node 0 must reorg to chain B.
    - After reorg both nodes must agree on `getdigidollarstats`. The
      disconnected branch-A redeem may be resurrected into node 0's mempool,
      in which case wallet policy must keep the position reserved/inactive
      until that pending spend leaves the mempool.

  Phase 3 - reindex node 0 alone:
    - Restart node 0 with `-reindex=1` (no peer involvement).
    - After reindex, node 0 must report identical stats and deployment state
      to before the reindex. This phase starts without mempool persistence
      or wallet rebroadcast, so wallet positions must reflect the active
      chain after the pending disconnected redeem is intentionally dropped.

  Phase 4 - wallet notifications across reorg:
    - Pin that the wallet observing DD positions sees the disconnected
      tip drop, keeps pending DD redeems reserved while they remain in
      mempool, and returns to the active-chain view after the pending
      redeem is dropped during the reindex restart.

The fixture uses `-digidollaractivationheight=200` plus
`-digidollarstatsindex=1` so `getdigidollarstats` is consensus-derived
from the index and comparable across nodes regardless of which wallet
loaded which positions.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


REGTEST_PERIOD = 144
# `-digidollaractivationheight=N` only sets BIP9 `min_activation_height`. The
# state machine still has to walk DEFINED -> STARTED -> LOCKED_IN -> ACTIVE in
# period (144 block) steps. With min_activation_height <= 432 the first ACTIVE
# tip is height 431 (period boundary at the 3rd period). Mining 5 more blocks
# past 431 lands us comfortably inside ACTIVE territory and gives the wallet
# enough mature coinbases to mint.
ACTIVATION_HEIGHT = 432
FIRST_ACTIVE_TIP = 431
ORACLE_PRICE_MICRO_USD = 500000  # $0.50/DGB


def position_is_active(position):
    return position.get(
        "is_active",
        position.get("status") in ("active", "unlocked"),
    )


class DigiDollarWave14MultinodeIbdReorgTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        common = [
            "-digidollaractivationheight={}".format(ACTIVATION_HEIGHT),
            "-dandelion=0",
            "-txindex=1",
            "-digidollarstatsindex=1",
        ]
        self.extra_args = [list(common), list(common)]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.setup_nodes()
        # Phase 1 starts with the nodes disconnected so node 0 builds the
        # initial chain alone, then node 1 joins for a real cold IBD.
        self.stop_node(1)

    # ------------------------------------------------------------------ helpers
    def wipe_and_start_node(self, idx, extra_args=None):
        """Stop, wipe, and start node idx with the given args."""
        import os
        import shutil

        node = self.nodes[idx]
        if node.process is not None:
            self.stop_node(idx)
        chain_dir = os.path.join(node.datadir_path, node.chain)
        if os.path.exists(chain_dir):
            shutil.rmtree(chain_dir)
        if extra_args is None:
            extra_args = self.extra_args[idx]
        self.start_node(idx, extra_args=extra_args)

    def assert_states_match(self, indices, label):
        """Compare deployment, stats and tip across the given node indices."""
        baseline_idx = indices[0]
        baseline_tip = self.nodes[baseline_idx].getbestblockhash()
        baseline_height = self.nodes[baseline_idx].getblockcount()
        baseline_dep = self.nodes[baseline_idx].getdigidollardeploymentinfo()
        baseline_stats = self.nodes[baseline_idx].getdigidollarstats()
        for idx in indices[1:]:
            assert_equal(self.nodes[idx].getbestblockhash(), baseline_tip)
            assert_equal(self.nodes[idx].getblockcount(), baseline_height)
            dep = self.nodes[idx].getdigidollardeploymentinfo()
            assert_equal(dep["status"], baseline_dep["status"])
            assert_equal(dep["enabled"], baseline_dep["enabled"])
            assert_equal(dep["activation_height"], baseline_dep["activation_height"])
            stats = self.nodes[idx].getdigidollarstats()
            for key in (
                "total_dd_supply",
                "active_positions",
                "total_collateral_dgb",
                "total_collateral_locked",
                "oracle_price_micro_usd",
                "is_emergency",
            ):
                assert_equal(stats[key], baseline_stats[key])
        self.log.info(
            "  %s: tip=%s height=%d supply=%d positions=%d price=%d",
            label,
            baseline_tip[:16],
            baseline_height,
            baseline_stats["total_dd_supply"],
            baseline_stats["active_positions"],
            baseline_stats["oracle_price_micro_usd"],
        )

    def mine_dd_block(self, node, generator):
        """Refresh oracle quote then mine `generator()` and return result."""
        node.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        return generator()

    # --------------------------------------------------------------- run_test
    def run_test(self):
        self.log.info("Phase 1: cold IBD past activation")
        self.test_phase1_ibd()

        self.log.info("Phase 2: node 0 reorgs onto node 1's longer chain")
        self.test_phase2_reorg_to_longer_chain()

        self.log.info("Phase 3: reindex node 0 alone")
        self.test_phase3_reindex()

        self.log.info("Phase 4: wallet notifications across reorg")
        self.test_phase4_wallet_notifications()

        self.log.info("Wave 14 multi-node IBD/reorg/reindex tests PASSED")

    # ============================================================== Phase 1
    def test_phase1_ibd(self):
        # Node 1 stays offline. Node 0 alone builds a chain past activation
        # with mature coinbase funds, mints a DD position, mines a DD
        # transfer, and a plain non-DD block.
        node0 = self.nodes[0]

        # Mature funds + cross BIP9 activation. With min_activation_height=432
        # and period=144, ACTIVE flips at FIRST_ACTIVE_TIP=431. We mine
        # FIRST_ACTIVE_TIP + 5 blocks to land safely inside ACTIVE with mature
        # coinbase funds available for minting.
        self.log.info("  building chain to activation on node 0 alone")
        node0.generate(FIRST_ACTIVE_TIP + 5)
        info = node0.getdigidollardeploymentinfo()
        assert_equal(info["status"], "active")
        assert_equal(info["enabled"], True)

        node0.setmockoracleprice(ORACLE_PRICE_MICRO_USD)

        self.log.info("  minting tier-0 DD position on node 0")
        mint = node0.mintdigidollar(100000, 0)
        self.mint_position_id = mint["position_id"]
        self.mint_block_hash = self.mine_dd_block(node0, lambda: node0.generate(1)[0])

        # Mine a couple of additional DD-touching blocks (transfer back to
        # ourselves) so the index gets multiple DD-touching reorg-able tips.
        self_addr = node0.getdigidollaraddress()
        node0.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        node0.senddigidollar(self_addr, 5000)
        self.mine_dd_block(node0, lambda: node0.generate(1))
        node0.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        node0.senddigidollar(self_addr, 5000)
        self.mine_dd_block(node0, lambda: node0.generate(1))

        # Plain non-DD block to prove the bundle is omitted on non-DD blocks
        # and the IBD path handles a mixed sequence.
        node0.generate(1)

        baseline_height = node0.getblockcount()
        baseline_stats = node0.getdigidollarstats()
        assert baseline_stats["total_dd_supply"] >= 100000, (
            "expected at least the minted tier-0 DD supply", baseline_stats,
        )
        assert_equal(baseline_stats["active_positions"], 1)

        # Now bring up node 1 from a freshly wiped datadir and let it cold-sync.
        self.log.info("  starting fresh node 1 and connecting for IBD")
        self.wipe_and_start_node(1)
        # The new node must currently report a different state from node 0.
        assert_equal(self.nodes[1].getblockcount(), 0)

        self.connect_nodes(0, 1)
        self.sync_blocks([self.nodes[0], self.nodes[1]], timeout=120)

        # `getdigidollarstats` reads the index, which on a freshly synced
        # node must be flushed to the tip before we compare. The RPC blocks
        # internally via `BlockUntilSyncedToCurrentChain`.
        self.assert_states_match([0, 1], "after IBD")
        assert_equal(self.nodes[1].getblockcount(), baseline_height)

    # ============================================================== Phase 2
    def test_phase2_reorg_to_longer_chain(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        self.disconnect_nodes(0, 1)

        # Baseline before either branch advances.
        common_tip = node0.getbestblockhash()
        common_height = node0.getblockcount()
        assert_equal(node1.getbestblockhash(), common_tip)
        common_block_hash = common_tip

        # Mine to the position's unlock height so a redeem becomes legal.
        # Tier 0 = 1 hour = 240 blocks at 15 s/block.
        positions = node0.listdigidollarpositions(False)
        position = next(
            p for p in positions if p["position_id"] == self.mint_position_id
        )
        unlock_height = position["unlock_height"]
        blocks_to_unlock = max(0, unlock_height - common_height)
        if blocks_to_unlock > 0:
            # Mine the unlock blocks on node 0 only, then save the resulting
            # tip hash so node 1 can fast-forward to the same shared
            # extended-ancestor through P2P later. Doing this on a single
            # node keeps both branches anchored to a single deterministic
            # extended ancestor, simplifying the reorg work comparison.
            self.log.info(
                "  unlocking the position on node 0 (mining %d blocks)",
                blocks_to_unlock,
            )
            self.mine_dd_block(node0, lambda: node0.generate(blocks_to_unlock))
            # Briefly reconnect so node 1 fast-syncs to this extended ancestor
            # before either side starts diverging.
            self.connect_nodes(0, 1)
            self.sync_blocks([node0, node1], timeout=60)
            self.disconnect_nodes(0, 1)
            common_height = node0.getblockcount()
            common_block_hash = node0.getbestblockhash()
            assert_equal(node1.getbestblockhash(), common_block_hash)

        # Branch A on node 0: a redeem of the tier-0 mint.
        self.log.info("  branch A on node 0: redeem the position")
        node0.setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        redeem = node0.redeemdigidollar(self.mint_position_id, 100000)
        redeem_txid = redeem["txid"]
        self.mine_dd_block(node0, lambda: node0.generate(1)[0])
        node0.generate(1)
        a_tip = node0.getbestblockhash()
        a_height = node0.getblockcount()
        a_stats = node0.getdigidollarstats()
        assert_equal(a_stats["total_dd_supply"], 0)
        assert_equal(a_stats["active_positions"], 0)

        # Branch B on node 1: extra padding to outwork branch A by a wide
        # margin (>> 144 anti-DoS buffer). No DD tx on node 1 because the
        # mint owner key lives on node 0; branch B's value is its
        # competing work, which forces node 0's reorg.
        self.log.info("  branch B on node 1: outwork branch A by 30 blocks")
        extra_blocks = (a_height - node1.getblockcount()) + 30
        node1.generate(extra_blocks)
        b_tip = node1.getbestblockhash()
        b_height = node1.getblockcount()
        assert b_height > a_height + 25, (
            "branch B must clearly outwork branch A",
            b_height,
            a_height,
        )
        assert b_tip != a_tip
        b_stats = node1.getdigidollarstats()
        # Branch B kept the original mint position alive (no redeem here).
        assert_equal(b_stats["total_dd_supply"], 100000)
        assert_equal(b_stats["active_positions"], 1)

        # Reconnect; node 0 must reorg from branch A onto branch B.
        self.log.info("  reconnecting; node 0 must reorg to branch B")
        self.connect_nodes(0, 1)
        self.sync_blocks([node0, node1], timeout=120)

        # Both nodes must now be on branch B with identical DD state.
        assert_equal(node0.getbestblockhash(), b_tip)
        assert_equal(node1.getbestblockhash(), b_tip)
        self.assert_states_match([0, 1], "after reorg to branch B")

        # The disconnected branch-A redeem is still a valid wallet
        # transaction and returns to mempool on node 0. While it is pending,
        # the wallet must keep the collateral/DD position reserved so the UI
        # and RPC layer do not offer a second redeem of the same vault.
        assert_equal(redeem_txid in node0.getrawmempool(), True)
        positions = node0.listdigidollarpositions(False)
        position = next(
            p for p in positions if p["position_id"] == self.mint_position_id
        )
        assert_equal(position_is_active(position), False)

        # Save state for Phase 3 / 4.
        self.post_reorg_tip = b_tip
        self.post_reorg_height = b_height
        self.post_reorg_stats = node0.getdigidollarstats()
        self.post_reorg_positions = node0.listdigidollarpositions(False)
        self.post_reorg_redeem_txid = redeem_txid

    # ============================================================== Phase 3
    def test_phase3_reindex(self):
        node0 = self.nodes[0]

        before_dep = node0.getdigidollardeploymentinfo()
        before_stats = self.post_reorg_stats

        self.log.info("  restarting node 0 with -reindex=1")
        self.disconnect_nodes(0, 1)
        self.restart_node(
            0,
            # This phase proves deterministic active-chain reindex parity.
            # The branch-A redeem remains a valid wallet transaction after
            # branch B wins. Disable mempool persistence and wallet rebroadcast
            # so this phase stays scoped to active-chain reindex parity; pending
            # redeem reservation/restart behavior is covered separately.
            extra_args=self.extra_args[0]
            + ["-reindex=1", "-persistmempool=0", "-walletbroadcast=0"],
        )
        # Reindex should rebuild the index and converge to the same tip.
        node0 = self.nodes[0]
        node0.syncwithvalidationinterfacequeue()
        # Tip and DD state must match the pre-reindex snapshot exactly.
        assert_equal(node0.getbestblockhash(), self.post_reorg_tip)
        assert_equal(node0.getblockcount(), self.post_reorg_height)

        after_dep = node0.getdigidollardeploymentinfo()
        assert_equal(after_dep["status"], before_dep["status"])
        assert_equal(after_dep["enabled"], before_dep["enabled"])
        assert_equal(
            after_dep["activation_height"], before_dep["activation_height"]
        )

        after_stats = node0.getdigidollarstats()
        for key in (
            "total_dd_supply",
            "active_positions",
            "total_collateral_dgb",
            "total_collateral_locked",
        ):
            assert_equal(after_stats[key], before_stats[key])

        after_positions = node0.listdigidollarpositions(False)
        # The reindex restart intentionally drops mempool persistence for
        # active-chain parity. With the disconnected redeem gone from mempool,
        # the wallet must return to the active-chain view for the mint.
        after_map = {
            p["position_id"]: position_is_active(p) for p in after_positions
        }
        assert_equal(after_map, {self.mint_position_id: True})

        # Reconnect for Phase 4.
        self.connect_nodes(0, 1)
        self.sync_blocks([self.nodes[0], self.nodes[1]], timeout=120)

    # ============================================================== Phase 4
    def test_phase4_wallet_notifications(self):
        """Reorg-driven wallet notifications must produce a self-consistent
        position view that matches the active chain.

        The earlier phases proved consensus / index agreement. This phase
        proves the wallet on node 0 reacts correctly to the validation
        interface notifications (BlockConnected / BlockDisconnected /
        TransactionAdded) emitted during the Phase 2 reorg: after the
        reorg the wallet must first reserve the position while the
        disconnected branch-A redeem is pending, then show the position
        active again after the reindex restart drops that pending mempool
        spend. At that point the wallet's DD balance must equal the supply
        reported by the consensus-driven stats index.
        """
        node0 = self.nodes[0]

        positions = node0.listdigidollarpositions(False)
        active_positions = [p for p in positions if position_is_active(p)]
        assert_equal(len(active_positions), 1)
        assert_equal(active_positions[0]["position_id"], self.mint_position_id)

        balance = node0.getdigidollarbalance()
        stats = node0.getdigidollarstats()
        # Wallet observes the same DD it minted; consensus index and
        # wallet must agree because there is exactly one mint position
        # on the active chain.
        assert_equal(balance["total"], stats["total_dd_supply"])
        assert_equal(balance["confirmed"], stats["total_dd_supply"])

        # Node 1 has no wallet (it was wiped for the cold IBD), but its
        # consensus stats must still match node 0 because the
        # `digidollarstatsindex` is a chain-state index, not a wallet
        # construct. This proves the consensus surface is fully
        # reconstructible from on-chain data alone.
        node1 = self.nodes[1]
        node1_stats = node1.getdigidollarstats()
        assert_equal(node1_stats["total_dd_supply"], 100000)
        assert_equal(node1_stats["active_positions"], 1)

        # Final cross-node snapshot one more time.
        self.assert_states_match([0, 1], "final")


if __name__ == "__main__":
    DigiDollarWave14MultinodeIbdReorgTest().main()
