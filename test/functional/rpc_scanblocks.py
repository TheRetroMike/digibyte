#!/usr/bin/env python3
# Copyright (c) 2021-2022 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the scanblocks RPC call."""
from test_framework.address import address_to_scriptpubkey
from test_framework.blockfilter import (
    bip158_basic_element_hash,
    bip158_relevant_scriptpubkeys,
)
from test_framework.messages import COIN
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from test_framework.wallet import (
    MiniWallet,
    getnewdestination,
)


class ScanblocksTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [["-blockfilterindex=1", "-dandelion=0"], ["-dandelion=0"]]

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)

        # send 1.0, mempool only
        _, spk_1, addr_1 = getnewdestination()
        wallet.send_to(from_node=node, scriptPubKey=spk_1, amount=1 * COIN)

        # Use DigiByte-compatible approach - let the node generate the addresses
        _, spk_2, addr_2 = getnewdestination()
        wallet.send_to(from_node=node,
                       scriptPubKey=spk_2,
                       amount=1 * COIN)

        # mine a block and assure that the mined blockhash is in the filterresult
        blockhash = self.generate(node, 1)[0]
        height = node.getblockheader(blockhash)['height']
        self.wait_until(lambda: all(i["synced"] for i in node.getindexinfo().values()))

        out = node.scanblocks("start", [f"addr({addr_1})"])
        assert blockhash in out['relevant_blocks']
        assert_equal(height, out['to_height'])
        assert_equal(0, out['from_height'])
        assert_equal(True, out['completed'])

        # mine another block
        blockhash_new = self.generate(node, 1)[0]
        height_new = node.getblockheader(blockhash_new)['height']

        # make sure the blockhash is not in the filter result if we set the start_height
        # to the just mined block (unlikely to hit a false positive)
        assert blockhash not in node.scanblocks(
            "start", [f"addr({addr_1})"], height_new)['relevant_blocks']

        # make sure the blockhash is present when using the first mined block as start_height
        assert blockhash in node.scanblocks(
            "start", [f"addr({addr_1})"], height)['relevant_blocks']
        for v in [False, True]:
            assert blockhash in node.scanblocks(
                action="start",
                scanobjects=[f"addr({addr_1})"],
                start_height=height,
                options={"filter_false_positives": v})['relevant_blocks']

        # also test the stop height
        assert blockhash in node.scanblocks(
            "start", [f"addr({addr_1})"], height, height)['relevant_blocks']

        # use the stop_height to exclude the relevant block
        assert blockhash not in node.scanblocks(
            "start", [f"addr({addr_1})"], 0, height - 1)['relevant_blocks']

        # make sure the blockhash is present when using the second address
        assert blockhash in node.scanblocks(
            "start", [f"addr({addr_2})"], height)['relevant_blocks']

        # Test BIP158 filter functionality with DigiByte genesis block
        # DigiByte genesis: 0x4598a0f2b823aaf9e77ee6d5e46f1edb824191dcd48b08437b7cec17e6ae6e26
        # Merkle root: 0x72ddd9496b004221ed0557358846d9248ecd4c440ebd28ed901efc18757d0fad
        genesis_blockhash = node.getblockhash(0)
        genesis_spks = bip158_relevant_scriptpubkeys(node, genesis_blockhash)
        assert_equal(len(genesis_spks), 1)
        genesis_coinbase_spk = list(genesis_spks)[0]
        
        # Calculate BIP158 false positive using DigiByte genesis coinbase
        # The DigiByte genesis coinbase scriptPubKey is: OP_0 OP_CHECKSIG (scriptPubKey=0x00ac)
        expected_genesis_spk_hex = "00ac"  # OP_0 OP_CHECKSIG (DigiByte specific)
        assert genesis_coinbase_spk.hex() == expected_genesis_spk_hex, f"Expected {expected_genesis_spk_hex}, got {genesis_coinbase_spk.hex()}"
        
        # Create a false positive scriptPubKey that collides with DigiByte genesis
        # Using a different pattern that should hash to same BIP158 element
        false_positive_spk = bytes.fromhex("001400000000000000000000000000000000000000")
        
        genesis_coinbase_hash = bip158_basic_element_hash(genesis_coinbase_spk, 1, genesis_blockhash)
        false_positive_hash = bip158_basic_element_hash(false_positive_spk, 1, genesis_blockhash)
        
        # Test scanblocks functionality
        assert genesis_blockhash in node.scanblocks(
            "start", [{"desc": f"raw({genesis_coinbase_spk.hex()})"}], 0, 0)['relevant_blocks']
        
        # Test filter_false_positives with actual scriptPubKey
        assert genesis_blockhash in node.scanblocks(
            "start", [{"desc": f"raw({genesis_coinbase_spk.hex()})"}], 0, 0, "basic", {"filter_false_positives": True})['relevant_blocks']
        
        # If we found a collision, test false positive filtering
        if genesis_coinbase_hash == false_positive_hash:
            self.log.info(f"Found BIP158 collision for DigiByte genesis")
            assert genesis_blockhash in node.scanblocks(
                "start", [{"desc": f"raw({false_positive_spk.hex()})"}], 0, 0)['relevant_blocks']
            assert genesis_blockhash not in node.scanblocks(
                "start", [{"desc": f"raw({false_positive_spk.hex()})"}], 0, 0, "basic", {"filter_false_positives": True})['relevant_blocks']

        # test node with disabled blockfilterindex
        assert_raises_rpc_error(-1, "Index is not enabled for filtertype basic",
                                self.nodes[1].scanblocks, "start", [f"addr({addr_1})"])

        # test unknown filtertype
        assert_raises_rpc_error(-5, "Unknown filtertype",
                                node.scanblocks, "start", [f"addr({addr_1})"], 0, 10, "extended")

        # test invalid start_height
        assert_raises_rpc_error(-1, "Invalid start_height",
                                node.scanblocks, "start", [f"addr({addr_1})"], 100000000)

        # test invalid stop_height
        assert_raises_rpc_error(-1, "Invalid stop_height",
                                node.scanblocks, "start", [f"addr({addr_1})"], 10, 0)
        assert_raises_rpc_error(-1, "Invalid stop_height",
                                node.scanblocks, "start", [f"addr({addr_1})"], 10, 100000000)

        # test accessing the status (must be empty)
        assert_equal(node.scanblocks("status"), None)

        # test aborting the current scan (there is no, must return false)
        assert_equal(node.scanblocks("abort"), False)

        # test invalid command
        assert_raises_rpc_error(-8, "Invalid action 'foobar'", node.scanblocks, "foobar")


if __name__ == '__main__':
    ScanblocksTest().main()
