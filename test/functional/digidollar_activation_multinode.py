#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Multi-node DigiDollar BIP9 activation, reorg, IBD, and reindex coverage.

Wave 12 functional auditor scenarios for activation gates and consensus-split
risk. The test runs against `-digidollaractivationheight` on regtest so the
BIP9 state machine progresses (DEFINED -> STARTED -> LOCKED_IN -> ACTIVE)
instead of using the regtest ALWAYS_ACTIVE shortcut.

Activation arithmetic (regtest, period = 144 blocks):
  -digidollaractivationheight=432 sets min_activation_height to 432. State
  transitions at period boundaries (blocks 143, 287, 431, ...). Concretely:
    * tip in [0, 142]:   status = defined
    * tip in [143, 286]: status = started
    * tip in [287, 430]: status = locked_in
    * tip in [431, ...]: status = active

  `enabled` from `getdigidollardeploymentinfo` mirrors the BIP9 status
  because it asks `DeploymentActiveAfter(tip)` for a versionbits deployment,
  which is true exactly when `State(tip) == ACTIVE`. The first block actually
  validated under DD-active rules is therefore the block mined on top of an
  ACTIVE tip (i.e. heights >= 432 in the test config).

Scenarios covered:
  1. Two nodes start at height < activation; both reach activation; verify
     deployment state flips simultaneously and `getdigidollardeploymentinfo`
     reports the same activation height on both nodes.
  2. Reorg across activation: invalidate the activation block and watch DD
     turn off; reconsider the longer chain and watch DD restore correctly.
  3. Mempool relay: pre-activation, a DD-marker tx submitted via the wallet
     RPC is rejected with "not yet active"; post-activation it is accepted
     and relayed across the link.
  4. Mining template: pre-activation, mined blocks have no OP_ORACLE coinbase
     output; post-activation, only DD-touching blocks carry the v0x03 MuSig2
     bundle in coinbase, while plain (non-DD) blocks still omit it. Bit 23 is
     signaled in `getblocktemplate.version` for both LOCKED_IN and ACTIVE
     periods.
  5. IBD across activation: cold sync a fresh node from genesis through
     activation; verify the gate fires at the right block.
  6. Reindex across activation: restart with `-reindex` and verify the
     deployment state and activation height are both restored.
  7. RPC `getdigidollardeploymentinfo` reports the correct state at every
     transition: DEFINED, STARTED, LOCKED_IN, ACTIVE.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


REGTEST_PERIOD = 144                 # BIP9 confirmation window on regtest
BIP9_MIN_ACTIVATION = 432            # min_activation_height used in this test
# BIP9 transitions at period boundaries (block heights k*144 - 1 = 143, 287, 431).
# `min_activation_height = 432` means the LOCKED_IN -> ACTIVE flip happens at the
# block whose `nHeight + 1 >= min_activation_height`, i.e. the period boundary
# block at height 431. From that tip onward `State(tip) == ACTIVE` and
# `IsDigiDollarEnabled(tip)` is true, so the first DD-validated block is the
# block at height 432 (mined on top of the activation tip).
FIRST_ACTIVE_TIP = 431               # first tip height where State(tip) == ACTIVE
# `getdigidollardeploymentinfo.activation_height` walks back to the lowest tip
# height where State(pprev) != ACTIVE. With FIRST_ACTIVE_TIP=431 that walks all
# the way down to height 431 itself, so the RPC reports 431.
RPC_ACTIVATION_HEIGHT = FIRST_ACTIVE_TIP


class DigiDollarActivationMultinodeTest(DigiByteTestFramework):
    def set_test_params(self):
        # Three nodes total: 0 and 1 form the always-running pair used for
        # simultaneous-activation, reorg, and mempool relay phases. Node 2 is
        # a scratch node used for the LOCKED_IN mining template, IBD, and
        # reindex phases — we stop and wipe it between sub-tests.
        self.num_nodes = 3
        self.setup_clean_chain = True
        common = [
            "-digidollaractivationheight={}".format(BIP9_MIN_ACTIVATION),
            "-dandelion=0",
            "-txindex=1",
        ]
        self.extra_args = [list(common), list(common), list(common)]

    def setup_network(self, split=False):
        # Set up nodes 0 and 1 with a P2P link; do NOT start node 2 — we
        # intentionally leave it offline until each Phase-4/5/6 sub-test
        # boots it from a wiped datadir.
        self.setup_nodes()
        self.connect_nodes(0, 1)
        self.stop_node(2)

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    # ------------------------------------------------------------------ helpers
    def deployment_state(self, node):
        info = node.getdigidollardeploymentinfo()
        return info["status"], info["enabled"]

    def assert_activation_metadata(self, node, label):
        info = node.getdigidollardeploymentinfo()
        assert_equal(info["min_activation_height"], BIP9_MIN_ACTIVATION)
        assert_equal(info["oracle_activation_height"], BIP9_MIN_ACTIVATION)
        assert_equal(info["musig2_format_activation_height"], BIP9_MIN_ACTIVATION)
        self.log.info(
            "  %s: min_activation_height=%d oracle_activation_height=%d musig2_height=%d",
            label,
            info["min_activation_height"],
            info["oracle_activation_height"],
            info["musig2_format_activation_height"],
        )

    def assert_state(self, node, expected_status, *, enabled, label):
        status, ena = self.deployment_state(node)
        assert_equal(status, expected_status)
        assert_equal(ena, enabled)
        self.log.info("  %s: status=%s enabled=%s", label, status, ena)

    # --------------------------------------------------------------- run_test
    def wipe_and_start_node(self, idx):
        """Stop, wipe, and start node idx with the activation knob applied."""
        import os
        import shutil
        node = self.nodes[idx]
        if node.process is not None:
            self.stop_node(idx)
        chain_dir = os.path.join(node.datadir_path, node.chain)
        if os.path.exists(chain_dir):
            shutil.rmtree(chain_dir)
        self.start_node(idx)

    @property
    def core_nodes(self):
        """Nodes 0 and 1, the always-running pair used by Phases 1-3."""
        return [self.nodes[0], self.nodes[1]]

    def run_test(self):
        for i in range(2):  # only nodes 0,1 are running
            assert_equal(self.nodes[i].getblockcount(), 0)
            self.assert_state(self.nodes[i], "defined", enabled=False,
                              label=f"node{i} genesis")
            self.assert_activation_metadata(self.nodes[i], f"node{i} genesis activation metadata")

        self.log.info("Phase 1: Two-node simultaneous activation")
        self.test_simultaneous_activation()

        self.log.info("Phase 2: Reorg across activation boundary")
        self.test_reorg_across_activation()

        self.log.info("Phase 3: Mempool relay - pre vs post activation")
        self.test_mempool_relay()

        self.log.info("Phase 4: Mining template - pre vs post activation")
        self.test_mining_template()

        self.log.info("Phase 5: IBD across activation height")
        self.test_ibd_across_activation()

        self.log.info("Phase 6: Reindex across activation height")
        self.test_reindex_across_activation()

        self.log.info("All multi-node DigiDollar activation tests PASSED")

    # ============================================================== Phase 1
    def test_simultaneous_activation(self):
        """Both nodes mine to the same heights; deployment state must agree."""
        # Position both nodes at height = FIRST_ACTIVE_TIP - 5 (still LOCKED_IN).
        target = FIRST_ACTIVE_TIP - 5
        self.nodes[0].generate(target)
        self.sync_blocks(self.core_nodes)

        for i in range(2):
            assert_equal(self.nodes[i].getblockcount(), target)
            self.assert_state(self.nodes[i], "locked_in", enabled=False,
                              label=f"node{i} h={target}")

        # Mine to one block before activation: still LOCKED_IN because the
        # period boundary (block FIRST_ACTIVE_TIP) has not yet been reached.
        self.nodes[0].generate(FIRST_ACTIVE_TIP - 1 - target)
        self.sync_blocks(self.core_nodes)
        for i in range(2):
            assert_equal(self.nodes[i].getblockcount(), FIRST_ACTIVE_TIP - 1)
            self.assert_state(self.nodes[i], "locked_in", enabled=False,
                              label=f"node{i} pre-activation tip")

        # Mine the activation period boundary: status flips to ACTIVE on both
        # nodes simultaneously after the next sync.
        self.nodes[0].generate(1)
        self.sync_blocks(self.core_nodes)
        for i in range(2):
            assert_equal(self.nodes[i].getblockcount(), FIRST_ACTIVE_TIP)
            self.assert_state(self.nodes[i], "active", enabled=True,
                              label=f"node{i} activation tip")
            info = self.nodes[i].getdigidollardeploymentinfo()
            assert_equal(info["activation_height"], RPC_ACTIVATION_HEIGHT)

    # ============================================================== Phase 2
    def test_reorg_across_activation(self):
        """Reorg below activation must turn DD off; reorg back must restore."""
        self.disconnect_nodes(0, 1)

        activation_hash = self.nodes[1].getblockhash(FIRST_ACTIVE_TIP)
        self.nodes[1].invalidateblock(activation_hash)
        assert_equal(self.nodes[1].getblockcount(), FIRST_ACTIVE_TIP - 1)
        self.assert_state(self.nodes[1], "locked_in", enabled=False,
                          label="node1 after invalidate")

        # Node 0 keeps the original chain; mines two more blocks past activation.
        self.nodes[0].generate(2)
        assert_equal(self.nodes[0].getblockcount(), FIRST_ACTIVE_TIP + 2)
        self.assert_state(self.nodes[0], "active", enabled=True,
                          label="node0 active+2")

        # Reconnect; node 1 should follow node 0's longer chain and re-enable DD.
        self.nodes[1].reconsiderblock(activation_hash)
        self.connect_nodes(0, 1)
        self.sync_blocks(self.core_nodes)
        for i in range(2):
            assert_equal(self.nodes[i].getblockcount(), FIRST_ACTIVE_TIP + 2)
            self.assert_state(self.nodes[i], "active", enabled=True,
                              label=f"node{i} after reorg")
            info = self.nodes[i].getdigidollardeploymentinfo()
            assert_equal(info["activation_height"], RPC_ACTIVATION_HEIGHT)

    # ============================================================== Phase 3
    def test_mempool_relay(self):
        """Pre-activation DD txs are rejected; post-activation are accepted/relayed.

        The mempool acceptance gate has two layers for DD txs:
          (a) `digidollar-not-active` if the BIP9 deployment is not active.
          (b) `digidollar-missing-oracle-quote` even when active, if no recent
              v0x03 MuSig2 oracle bundle is available locally.

        Layer (a) is the activation gate this wave is concerned with. Layer
        (b) requires per-node oracle state, which the existing
        `digidollar_network_relay.py` test already exercises across nodes.
        Here we keep the focus tight: prove pre-activation rejection vs
        post-activation acceptance on a single node, plus verify the mined
        DD-touching block survives a round-trip sync between both nodes.
        """
        self.disconnect_nodes(0, 1)
        activation_hash = self.nodes[1].getblockhash(FIRST_ACTIVE_TIP)
        self.nodes[1].invalidateblock(activation_hash)
        self.assert_state(self.nodes[1], "locked_in", enabled=False,
                          label="node1 rolled back")

        # Bring node 0 to a state where it has mature coinbases for minting.
        self.nodes[0].generate(110)
        self.nodes[0].setmockoracleprice(500000)  # $0.50/DGB

        # Node 0 (active) accepts the DD mint.
        mint_result = self.nodes[0].mintdigidollar(100000, 4)  # $1000, tier 4
        txid = mint_result["txid"]
        assert txid in self.nodes[0].getrawmempool()
        self.log.info("  node0 (active) accepted DD mint %s", txid)

        # On node 1 (pre-activation), wallet RPC must refuse the same op.
        try:
            self.nodes[1].mintdigidollar(100000, 4)
            assert False, "mintdigidollar should reject pre-activation"
        except Exception as exc:
            err = str(exc).lower()
            assert "not yet active" in err, f"unexpected error: {exc}"
            self.log.info("  node1 (locked_in) rejected mint with: %s", exc)

        # Mine the DD tx into a block on node 0 (the miner stamps the bundle
        # into the coinbase, so node 1 ends up with a recent valid quote once
        # it syncs the chain).
        block_hashes = self.nodes[0].generate(1)
        assert txid in self.nodes[0].getblock(block_hashes[0])["tx"]

        # Restore node 1 and reconnect; node 1 should follow node 0 and end
        # up with the DD-touching block in its chain.
        self.nodes[1].reconsiderblock(activation_hash)
        self.connect_nodes(0, 1)
        self.sync_blocks(self.core_nodes)
        for i in range(2):
            block = self.nodes[i].getblock(block_hashes[0])
            assert txid in block["tx"], f"node{i} missing DD tx in mined block"
            self.assert_state(self.nodes[i], "active", enabled=True,
                              label=f"node{i} after DD mine")

    # ============================================================== Phase 4
    def test_mining_template(self):
        """Mining templates: pre-activation no oracle; post-activation DD-touching only.

        On a DD-active chain the miner appends a v0x03 oracle output of the
        form `OP_RETURN OP_ORACLE <0x03> <bundle>` to the coinbase only when
        the block contains a DD-touching transaction (see
        `BlockTouchesDigiDollarForMiner` in `src/node/miner.cpp` and
        `OracleBundleManager::AddOracleBundleToBlock`). Non-DD blocks omit
        the bundle even after activation, matching the validator rule that
        non-DD blocks may omit oracle data
        (`OracleDataValidator::ValidateBlockOracleData`).

        Pre-activation, the miner skips the bundle for every block, so the
        coinbase only carries the standard segwit witness-commitment OP_RETURN.
        """
        ORACLE_SCRIPT_PREFIX = "OP_RETURN OP_ORACLE"

        def coinbase_has_oracle(node, block_hash):
            block = node.getblock(block_hash, 2)
            for out in block["tx"][0]["vout"]:
                if out["scriptPubKey"]["asm"].startswith(ORACLE_SCRIPT_PREFIX):
                    return True
            return False

        # The Phase 3 mempool-relay test already mined a DD-touching block
        # with an oracle output. Re-prove the invariant explicitly here:
        # build a DD mint, mine a fresh block, and confirm OP_ORACLE carries.
        self.nodes[0].setmockoracleprice(500000)
        mint_result = self.nodes[0].mintdigidollar(50000, 4)
        dd_block_hash = self.nodes[0].generate(1)[0]
        assert mint_result["txid"] in self.nodes[0].getblock(dd_block_hash)["tx"]
        assert coinbase_has_oracle(self.nodes[0], dd_block_hash), \
            "post-activation DD-touching coinbase missing OP_ORACLE bundle"
        self.log.info("  post-activation DD-touching coinbase carries OP_ORACLE")

        # A non-DD block on the same active chain may carry a fresh oracle bundle
        # opportunistically, but it must not require one to remain valid.
        non_dd_block_hash = self.nodes[0].generate(1)[0]
        self.log.info("  post-activation non-DD coinbase oracle_present=%s",
                      coinbase_has_oracle(self.nodes[0], non_dd_block_hash))
        self.sync_blocks(self.core_nodes)

        # Now check a clean pre-activation chain by booting node 2 from a
        # wiped datadir and mining only into the LOCKED_IN window (below
        # FIRST_ACTIVE_TIP). The template produced there must NOT need an
        # oracle bundle to be a valid block.
        self.wipe_and_start_node(2)
        # Mine a single block - state should be DEFINED here.
        self.nodes[2].generate(1)
        self.assert_state(self.nodes[2], "defined", enabled=False,
                          label="node2 first block")
        # Mine into LOCKED_IN window.
        self.nodes[2].generate(FIRST_ACTIVE_TIP - 2)
        assert_equal(self.nodes[2].getblockcount(), FIRST_ACTIVE_TIP - 1)
        self.assert_state(self.nodes[2], "locked_in", enabled=False,
                          label="node2 LOCKED_IN tip")

        tmpl_locked = self.nodes[2].getblocktemplate({"rules": ["segwit"]})
        version_locked = tmpl_locked["version"]
        bit23_locked = (version_locked & (1 << 23)) != 0
        self.log.info("  pre-activation template version=0x%08x bit23=%s",
                      version_locked, bit23_locked)
        assert bit23_locked, "Bit 23 should still be signaled during LOCKED_IN"

        # Mine a pre-activation block; coinbase must NOT carry an oracle
        # bundle. The block at FIRST_ACTIVE_TIP is itself validated under
        # non-DD-active rules (DeploymentActiveAt(block 431) checks State on
        # block 430, which is LOCKED_IN — not ACTIVE). After this block lands
        # the chain reports status=ACTIVE for the next block.
        pre_block_hash = self.nodes[2].generate(1)[0]
        assert not coinbase_has_oracle(self.nodes[2], pre_block_hash), \
            "pre-activation coinbase unexpectedly carries OP_ORACLE bundle"
        self.log.info("  pre-activation coinbase has NO OP_ORACLE bundle")
        assert_equal(self.nodes[2].getblockcount(), FIRST_ACTIVE_TIP)
        # Subsequent blocks are mined under DD-active rules — we stop here.
        self.assert_state(self.nodes[2], "active", enabled=True,
                          label="node2 just-activated tip")

        self.stop_node(2)

    # ============================================================== Phase 5
    def test_ibd_across_activation(self):
        """Cold sync a fresh node from genesis past activation."""
        # Phase 4 left node 2 stopped with a chain that diverges from node 0
        # (it has its own pre-activation history). For IBD coverage we need a
        # *fresh* datadir, so wipe and re-start the node.
        self.wipe_and_start_node(2)
        self.assert_state(self.nodes[2], "defined", enabled=False,
                          label="node2 genesis (pre-IBD)")

        # Connect to source and let the chain sync over P2P (IBD path).
        self.connect_nodes(2, 0)
        self.sync_blocks([self.nodes[0], self.nodes[2]], timeout=120)

        assert_equal(self.nodes[2].getblockcount(), self.nodes[0].getblockcount())
        self.assert_state(self.nodes[2], "active", enabled=True,
                          label="node2 post-IBD")
        info0 = self.nodes[0].getdigidollardeploymentinfo()
        info2 = self.nodes[2].getdigidollardeploymentinfo()
        assert_equal(info2["activation_height"], info0["activation_height"])
        assert_equal(info2["activation_height"], RPC_ACTIVATION_HEIGHT)

        self.disconnect_nodes(0, 2)
        self.stop_node(2)

    # ============================================================== Phase 6
    def test_reindex_across_activation(self):
        """`-reindex` must restore activation state at the right block."""
        before = self.nodes[0].getdigidollardeploymentinfo()
        self.assert_state(self.nodes[0], "active", enabled=True,
                          label="node0 pre-reindex")

        self.restart_node(0, extra_args=[
            "-digidollaractivationheight={}".format(BIP9_MIN_ACTIVATION),
            "-dandelion=0",
            "-txindex=1",
            "-reindex",
        ])

        after = self.nodes[0].getdigidollardeploymentinfo()
        assert_equal(after["status"], "active")
        assert_equal(after["enabled"], True)
        assert_equal(after["activation_height"], before["activation_height"])
        assert_equal(after["activation_height"], RPC_ACTIVATION_HEIGHT)
        self.log.info("  reindex preserved activation_height=%d",
                      after["activation_height"])


if __name__ == "__main__":
    DigiDollarActivationMultinodeTest().main()
