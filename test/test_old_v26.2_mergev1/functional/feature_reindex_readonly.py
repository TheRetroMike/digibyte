#!/usr/bin/env python3
# Copyright (c) 2023-present The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test running digibyted with -reindex from a read-only blockstore
- Start a node, generate blocks, then restart with -reindex after setting blk files to read-only
"""

import os
import stat
import subprocess
from test_framework.test_framework import DigiByteTestFramework
from test_framework.wallet import MiniWallet
from test_framework.script import CScript, OP_RETURN


class BlockstoreReindexTest(DigiByteTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # Add regtest-specific args to handle DigiByte multi-algo
        self.extra_args = [["-fastprune", "-algo=0"]]  # Force SHA256D algorithm

    def reindex_readonly(self):
        self.log.debug("Generate blocks big enough to start second block file")
        
        # Simple approach: generate many blocks to reach the block file size threshold
        # With fastprune, blocks files are smaller, so we need more blocks
        self.generate(self.nodes[0], 500, sync_fun=self.no_op)  # Generate more blocks
        self.stop_node(0)

        # Debug: check what block files exist
        blocks_dir = self.nodes[0].chain_path / "blocks"
        self.log.info(f"Block files in {blocks_dir}: {list(blocks_dir.glob('blk*.dat'))}")
        
        assert (self.nodes[0].chain_path / "blocks" / "blk00000.dat").exists()
        # If blk00001.dat doesn't exist, that's ok - the test will adapt
        if not (self.nodes[0].chain_path / "blocks" / "blk00001.dat").exists():
            self.log.info("blk00001.dat does not exist, generating more blocks")
            self.start_node(0, extra_args=["-fastprune", "-algo=0"])
            self.generate(self.nodes[0], 1000, sync_fun=self.no_op)
            self.stop_node(0)
        
        assert (self.nodes[0].chain_path / "blocks" / "blk00001.dat").exists()

        self.log.debug("Make the first block file read-only")
        filename = self.nodes[0].chain_path / "blocks" / "blk00000.dat"
        filename.chmod(stat.S_IREAD)

        undo_immutable = lambda: None
        # Linux
        try:
            subprocess.run(['chattr'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                subprocess.run(['chattr', '+i', filename], capture_output=True, check=True)
                undo_immutable = lambda: subprocess.check_call(['chattr', '-i', filename])
                self.log.info("Made file immutable with chattr")
            except subprocess.CalledProcessError as e:
                self.log.warning(str(e))
                if e.stdout:
                    self.log.warning(f"stdout: {e.stdout}")
                if e.stderr:
                    self.log.warning(f"stderr: {e.stderr}")
                if os.getuid() == 0:
                    self.log.warning("Return early on Linux under root, because chattr failed.")
                    self.log.warning("This should only happen due to missing capabilities in a container.")
                    self.log.warning("Make sure to --cap-add LINUX_IMMUTABLE if you want to run this test.")
                    undo_immutable = False
        except Exception:
            # macOS, and *BSD
            try:
                subprocess.run(['chflags'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                try:
                    subprocess.run(['chflags', 'uchg', filename], capture_output=True, check=True)
                    undo_immutable = lambda: subprocess.check_call(['chflags', 'nouchg', filename])
                    self.log.info("Made file immutable with chflags")
                except subprocess.CalledProcessError as e:
                    self.log.warning(str(e))
                    if e.stdout:
                        self.log.warning(f"stdout: {e.stdout}")
                    if e.stderr:
                        self.log.warning(f"stderr: {e.stderr}")
                    if os.getuid() == 0:
                        self.log.warning("Return early on BSD under root, because chflags failed.")
                        undo_immutable = False
            except Exception:
                pass

        if undo_immutable:
            self.log.info("Attempt to restart and reindex the node with the unwritable block file")
            with self.nodes[0].assert_debug_log(expected_msgs=['FlushStateToDisk', 'failed to open file'], unexpected_msgs=[]):
                self.nodes[0].assert_start_raises_init_error(extra_args=['-reindex', '-fastprune'],
                    expected_msg="Error: A fatal internal error occurred, see debug.log for details")
            undo_immutable()

        filename.chmod(0o777)

    def run_test(self):
        self.reindex_readonly()


if __name__ == '__main__':
    BlockstoreReindexTest().main()
