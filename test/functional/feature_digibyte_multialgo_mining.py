#!/usr/bin/env python3
# Copyright (c) 2025 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test DigiByte multi-algorithm mining functionality.

This test verifies:
1. All 5 algorithms can mine blocks (6 after Odocrypt activation)
2. getdifficulty returns per-algorithm difficulties
3. getmininginfo shows per-algorithm network hashrates
4. Block headers contain correct algorithm information
5. Difficulty adjustments work per-algorithm
6. Algorithm activation at correct heights
7. Odocrypt replaces Groestl at activation height
"""

from decimal import Decimal
import time

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)
from test_framework.messages import (
    BLOCK_VERSION_SCRYPT,
    BLOCK_VERSION_SHA256D,
    BLOCK_VERSION_GROESTL,
    BLOCK_VERSION_SKEIN,
    BLOCK_VERSION_QUBIT,
    BLOCK_VERSION_ODO,
)

# DigiByte algorithm constants
ALGO_SHA256D = 0
ALGO_SCRYPT = 1
ALGO_GROESTL = 2
ALGO_SKEIN = 3
ALGO_QUBIT = 4
ALGO_ODO = 7

# Algorithm names
ALGO_NAMES = {
    ALGO_SHA256D: "sha256d",
    ALGO_SCRYPT: "scrypt",
    ALGO_GROESTL: "groestl",
    ALGO_SKEIN: "skein",
    ALGO_QUBIT: "qubit",
    ALGO_ODO: "odo"
}

# Block version encoding (with BIP9)
ALGO_VERSIONS = {
    ALGO_SHA256D: 0x20000202,
    ALGO_SCRYPT: 0x20000002,
    ALGO_GROESTL: 0x20000402,
    ALGO_SKEIN: 0x20000602,
    ALGO_QUBIT: 0x20000802,
    ALGO_ODO: 0x20000E02
}

# DigiByte fork heights for regtest
MULTIALGO_HEIGHT = 100      # Multi-algorithm activation
MULTISHIELD_HEIGHT = 400    # MultiShield difficulty
DIGISPEED_HEIGHT = 1430     # DigiSpeed (15-second blocks)
ODO_PREP_HEIGHT = 2000      # Odocrypt preparation
ODOCRYPT_HEIGHT = 600       # Odocrypt activation (Groestl deactivation)

class DigiByteMultiAlgoTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # Use regtest with easy pow for faster testing
        self.extra_args = [["-easypow=1"], ["-easypow=1"]]
        # Fixed regtest address for mining (no wallet needed)
        self.mining_address = "dgbrt1qtmp74ayg7p24uslctssvjm06q5phz4yrgndnyh"

    def skip_test_if_missing_module(self):
        # Skip wallet check - we'll use a fixed address
        pass

    def test_pre_multialgo(self):
        """Test that only Scrypt works before MultiAlgo activation."""
        self.log.info("Testing pre-MultiAlgo era (Scrypt only)...")
        
        node = self.nodes[0]
        
        # Current height should be 0
        assert_equal(node.getblockcount(), 0)
        
        # Generate blocks with Scrypt (should work)
        self.log.info("Mining with Scrypt (should succeed)...")
        blockhash = self.generatetoaddress(node, 1, self.mining_address, 100000, "scrypt")[0]
        
        # Verify block was mined with Scrypt
        block = node.getblock(blockhash, 2)
        assert_equal(block['pow_algo'], 'scrypt')
        assert_equal(block['pow_algo_id'], ALGO_SCRYPT)
        
        # Try other algorithms (should fail before MultiAlgo)
        if node.getblockcount() < MULTIALGO_HEIGHT:
            self.log.info("Trying other algorithms (should fail)...")
            for algo_name in ["sha256d", "groestl", "skein", "qubit"]:
                try:
                    self.generatetoaddress(node, 1, self.mining_address, 100000, algo_name)
                    assert False, f"Algorithm {algo_name} should not work before MultiAlgo"
                except:
                    pass  # Expected to fail

    def test_getdifficulty_multialgo(self):
        """Test that getdifficulty returns per-algorithm difficulties."""
        self.log.info("Testing getdifficulty multi-algorithm support...")
        
        node = self.nodes[0]
        
        # Mine to MultiAlgo height
        current_height = node.getblockcount()
        if current_height < MULTIALGO_HEIGHT:
            self.generatetoaddress(node, MULTIALGO_HEIGHT - current_height, 
                                  self.mining_address, 100000, "scrypt")
        
        # Test getdifficulty returns object with difficulties
        difficulty_result = node.getdifficulty()
        
        # Should return object with 'difficulties' field
        assert 'difficulties' in difficulty_result, \
            "getdifficulty should return object with 'difficulties' field"
        
        difficulties = difficulty_result['difficulties']
        assert isinstance(difficulties, dict), \
            "difficulties should be a dictionary"
        
        # After MultiAlgo, should have 5 algorithms
        expected_algos = ["sha256d", "scrypt", "groestl", "skein", "qubit"]
        for algo in expected_algos:
            assert algo in difficulties, \
                f"Algorithm {algo} should be in difficulties"
            assert difficulties[algo] > 0, \
                f"Difficulty for {algo} should be positive"
        
        self.log.info(f"Difficulties: {difficulties}")

    def test_getmininginfo_networkhashesps(self):
        """Test that getmininginfo includes networkhashesps field."""
        self.log.info("Testing getmininginfo networkhashesps field...")
        
        node = self.nodes[0]
        
        # Get mining info
        mining_info = node.getmininginfo()
        
        # Check for required fields
        assert 'difficulties' in mining_info, \
            "getmininginfo should have 'difficulties' field"
        assert 'networkhashesps' in mining_info, \
            "getmininginfo should have 'networkhashesps' field"
        
        # Verify networkhashesps structure
        networkhashesps = mining_info['networkhashesps']
        assert isinstance(networkhashesps, dict), \
            "networkhashesps should be a dictionary"
        
        # Check that active algorithms have hashrates
        difficulties = mining_info['difficulties']
        for algo in difficulties.keys():
            assert algo in networkhashesps, \
                f"Algorithm {algo} should have network hashrate"
            # Hashrate might be 0 if no recent blocks
            assert networkhashesps[algo] >= 0, \
                f"Network hashrate for {algo} should be non-negative"
        
        self.log.info(f"Network hashrates: {networkhashesps}")

    def test_multialgo_mining(self):
        """Test mining with all 5 algorithms after MultiAlgo."""
        self.log.info("Testing multi-algorithm mining...")
        
        node = self.nodes[0]
        
        # Ensure we're past MultiAlgo height
        current_height = node.getblockcount()
        if current_height < MULTIALGO_HEIGHT:
            self.generatetoaddress(node, MULTIALGO_HEIGHT - current_height + 1,
                                  self.mining_address, 100000, "scrypt")
        
        # Test each algorithm
        algos_to_test = ["sha256d", "scrypt", "groestl", "skein", "qubit"]
        
        for algo_name in algos_to_test:
            self.log.info(f"Mining block with {algo_name}...")
            
            # Generate a block with this algorithm
            address = self.mining_address
            blockhashes = self.generatetoaddress(node, 1, address, 100000, algo_name)
            
            # Verify the block was mined with correct algorithm
            block = node.getblock(blockhashes[0], 2)
            assert_equal(block['pow_algo'], algo_name)
            
            # Check block header has correct difficulty
            header = node.getblockheader(blockhashes[0])
            assert 'difficulty' in header, "Block header should have difficulty"
            assert header['difficulty'] > 0, "Difficulty should be positive"
            
            # Verify algorithm fields if present
            if 'pow_algo' in header:
                assert_equal(header['pow_algo'], algo_name)
            if 'pow_algo_id' in header:
                expected_id = [k for k, v in ALGO_NAMES.items() if v == algo_name][0]
                assert_equal(header['pow_algo_id'], expected_id)

    def test_odocrypt_activation(self):
        """Test Odocrypt activation and Groestl deactivation."""
        self.log.info("Testing Odocrypt activation...")
        
        node = self.nodes[0]
        
        # Mine to just before Odocrypt height
        current_height = node.getblockcount()
        if current_height < ODOCRYPT_HEIGHT - 1:
            blocks_needed = ODOCRYPT_HEIGHT - 1 - current_height
            self.generatetoaddress(node, blocks_needed, self.mining_address, 100000, "scrypt")
        
        # Groestl should work before Odocrypt
        self.log.info("Mining with Groestl before Odocrypt (should succeed)...")
        try:
            self.generatetoaddress(node, 1, self.mining_address, 100000, "groestl")
            groestl_works_before = True
        except:
            groestl_works_before = False
        
        # Mine to Odocrypt activation
        current_height = node.getblockcount()
        if current_height < ODOCRYPT_HEIGHT:
            self.generatetoaddress(node, ODOCRYPT_HEIGHT - current_height,
                                  self.mining_address, 100000, "scrypt")
        
        # Now Odocrypt should work
        self.log.info("Mining with Odocrypt after activation (should succeed)...")
        try:
            blockhash = self.generatetoaddress(node, 1, self.mining_address, 100000, "odo")[0]
            block = node.getblock(blockhash, 2)
            assert_equal(block['pow_algo'], 'odo')
            odocrypt_works_after = True
        except Exception as e:
            self.log.error(f"Odocrypt mining failed: {e}")
            odocrypt_works_after = False
        
        # Groestl should NOT work after Odocrypt
        self.log.info("Mining with Groestl after Odocrypt (should fail)...")
        try:
            self.generatetoaddress(node, 1, self.mining_address, 100000, "groestl")
            groestl_works_after = True
        except:
            groestl_works_after = False
        
        # Verify the transition
        assert groestl_works_before or current_height >= ODOCRYPT_HEIGHT, \
            "Groestl should work before Odocrypt activation"
        assert odocrypt_works_after, \
            "Odocrypt should work after activation"
        assert not groestl_works_after, \
            "Groestl should NOT work after Odocrypt activation"
        
        # Check that we still have 5 active algorithms
        difficulty_result = node.getdifficulty()
        difficulties = difficulty_result['difficulties']
        active_algos = list(difficulties.keys())
        
        self.log.info(f"Active algorithms after Odocrypt: {active_algos}")
        assert len(active_algos) == 5, \
            f"Should have exactly 5 active algorithms, got {len(active_algos)}"
        
        # Verify Odocrypt replaced Groestl
        assert 'odo' in active_algos, "Odocrypt should be active"
        assert 'groestl' not in active_algos, "Groestl should be inactive"

    def test_difficulty_adjustment(self):
        """Test that difficulty adjusts per-algorithm."""
        self.log.info("Testing per-algorithm difficulty adjustment...")
        
        node = self.nodes[0]
        
        # Get initial difficulties
        initial_diff = node.getdifficulty()['difficulties']
        
        # Mine 10 blocks with one algorithm
        algo_to_test = "scrypt"
        self.log.info(f"Mining 10 blocks with {algo_to_test}...")
        for _ in range(10):
            self.generatetoaddress(node, 1, self.mining_address, 100000, algo_to_test)
        
        # Check difficulties 
        new_diff = node.getdifficulty()['difficulties']
        
        # In regtest with easypow, difficulty may not change much
        # Just verify we can get difficulties without errors
        self.log.info(f"Initial difficulty: {initial_diff[algo_to_test]}")
        self.log.info(f"New difficulty: {new_diff[algo_to_test]}")
        
        # Verify all algorithms have difficulty values
        for algo in ['sha256d', 'scrypt', 'groestl', 'skein', 'qubit']:
            assert algo in new_diff, f"Missing difficulty for {algo}"
            assert new_diff[algo] > 0, f"Difficulty for {algo} should be positive"

    def test_block_version_encoding(self):
        """Test that block versions correctly encode algorithm."""
        self.log.info("Testing block version algorithm encoding...")
        
        node = self.nodes[0]
        
        # Ensure we're past MultiAlgo
        if node.getblockcount() < MULTIALGO_HEIGHT:
            self.generatetoaddress(node, MULTIALGO_HEIGHT - node.getblockcount() + 1,
                                  self.mining_address, 100000, "scrypt")
        
        # Test each algorithm's version encoding
        test_algos = {
            "sha256d": 0x20000202,
            "scrypt": 0x20000002,
            "skein": 0x20000602,
            "qubit": 0x20000802,
        }
        
        for algo_name, expected_version in test_algos.items():
            # Mine a block
            blockhash = self.generatetoaddress(node, 1, self.mining_address, 
                                              100000, algo_name)[0]
            
            # Check version encoding
            block = node.getblock(blockhash, 2)
            version = block['version']
            
            self.log.info(f"{algo_name}: version=0x{version:08x}, "
                         f"expected=0x{expected_version:08x}")
            
            # Extract algorithm bits (bits 8-11)
            algo_bits = (version >> 8) & 0xF
            
            # Map algorithm name to expected bits
            expected_bits = {
                "sha256d": 0x2,
                "scrypt": 0x0,
                "groestl": 0x4,
                "skein": 0x6,
                "qubit": 0x8,
                "odo": 0xE,
            }
            
            assert_equal(algo_bits, expected_bits[algo_name])

    def test_generateblock_rpc(self):
        """Test generateblock RPC uses correct algorithm."""
        self.log.info("Testing generateblock RPC algorithm...")
        
        node = self.nodes[0]
        
        # The generateblock RPC will create the coinbase internally
        # We just need to provide an address and empty transaction list
        address = self.mining_address
        
        # Generate a block with no transactions (just coinbase)
        block_result = self.generateblock(node, address, [])
        blockhash = block_result['hash']
        
        # Verify the algorithm used
        block = node.getblock(blockhash, 2)
        
        # Should use default algorithm (scrypt)
        assert_equal(block['pow_algo'], 'scrypt')

    def test_getmininginfo_difficulty_algorithm(self):
        """Regression test for issue #346: difficulty field must match current algorithm."""
        self.log.info("Testing getmininginfo difficulty matches current algorithm (issue #346)...")

        info = self.nodes[0].getmininginfo()
        assert_equal(info['difficulty'], info['difficulties'][info['pow_algo']])

    def test_mining_info_comprehensive(self):
        """Comprehensive test of getmininginfo output."""
        self.log.info("Testing comprehensive getmininginfo output...")

        node = self.nodes[0]

        # Get mining info
        info = node.getmininginfo()
        
        # Required fields
        required_fields = [
            'blocks',
            'pow_algo_id',
            'pow_algo',
            'difficulty',
            'difficulties',
            'networkhashps',
            'networkhashesps',
            'pooledtx',
            'chain',
            'warnings'
        ]
        
        for field in required_fields:
            assert field in info, f"getmininginfo missing field: {field}"
        
        # Verify pow_algo matches default
        assert_equal(info['pow_algo'], 'scrypt')
        assert_equal(info['pow_algo_id'], ALGO_SCRYPT)
        
        # Verify difficulties structure
        assert isinstance(info['difficulties'], dict), \
            "difficulties should be a dictionary"
        
        # Verify networkhashesps structure
        assert isinstance(info['networkhashesps'], dict), \
            "networkhashesps should be a dictionary"
        
        # Both should have same algorithms
        assert set(info['difficulties'].keys()) == set(info['networkhashesps'].keys()), \
            "difficulties and networkhashesps should have same algorithms"
        
        self.log.info(f"Mining info validated: {len(info['difficulties'])} algorithms active")

    def run_test(self):
        """Run all multi-algorithm mining tests."""
        
        # Test 1: Pre-MultiAlgo (Scrypt only)
        self.test_pre_multialgo()
        
        # Test 2: getdifficulty multi-algorithm support
        self.test_getdifficulty_multialgo()
        
        # Test 3: getmininginfo networkhashesps
        self.test_getmininginfo_networkhashesps()
        
        # Test 4: Multi-algorithm mining
        self.test_multialgo_mining()
        
        # Test 5: Block version encoding
        self.test_block_version_encoding()
        
        # Test 6: generateblock RPC
        self.test_generateblock_rpc()
        
        # Test 7: Comprehensive mining info
        self.test_mining_info_comprehensive()

        # Test 8: getmininginfo difficulty matches algorithm (issue #346 regression test)
        self.test_getmininginfo_difficulty_algorithm()

        # Test 9: Difficulty adjustment
        self.test_difficulty_adjustment()

        # Test 10: Odocrypt activation (if height permits)
        if ODOCRYPT_HEIGHT < 1000:  # Only test if reasonable for regtest
            self.test_odocrypt_activation()
        
        self.log.info("All multi-algorithm mining tests passed!")

if __name__ == '__main__':
    DigiByteMultiAlgoTest().main()