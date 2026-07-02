#!/usr/bin/env python3
# Copyright (c) 2025-2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test DigiDollar deployment info RPC (getdigidollardeploymentinfo)."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_greater_than_or_equal


class DigiDollarRPCDeploymentTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing DigiDollar deployment info RPC...")
        node = self.nodes[0]

        self.log.info("Generating initial blocks for test setup...")
        self.generate(node, 110)

        self.test_deployment_info_basic()
        self.test_deployment_info_fields()
        self.test_deployment_status_values()
        self.test_deployment_after_activation()
        self.test_deployment_info_oracle_activation_fields()

        self.log.info("All deployment info tests passed!")

    def test_deployment_info_basic(self):
        self.log.info("Testing basic deployment info response...")
        node = self.nodes[0]
        
        result = node.getdigidollardeploymentinfo()
        
        assert 'enabled' in result, "Missing 'enabled' field"
        assert 'status' in result, "Missing 'status' field"
        assert 'bit' in result, "Missing 'bit' field"
        assert 'start_time' in result, "Missing 'start_time' field"
        assert 'timeout' in result, "Missing 'timeout' field"
        
        self.log.info(f"Deployment enabled: {result['enabled']}")
        self.log.info(f"Deployment status: {result['status']}")
        self.log.info(f"Version bit: {result['bit']}")

    def test_deployment_info_fields(self):
        self.log.info("Testing deployment info field types...")
        node = self.nodes[0]
        
        result = node.getdigidollardeploymentinfo()
        
        assert isinstance(result['enabled'], bool), "enabled should be boolean"
        assert isinstance(result['status'], str), "status should be string"
        assert isinstance(result['bit'], int), "bit should be integer"
        assert isinstance(result['start_time'], int), "start_time should be integer"
        assert isinstance(result['timeout'], int), "timeout should be integer"
        assert isinstance(result['min_activation_height'], int), "min_activation_height should be integer"
        
        assert_greater_than_or_equal(result['bit'], 0)
        assert_greater_than_or_equal(result['min_activation_height'], 0)
        
        self.log.info("All field types verified correctly")

    def test_deployment_status_values(self):
        self.log.info("Testing deployment status is valid...")
        node = self.nodes[0]
        
        result = node.getdigidollardeploymentinfo()
        
        valid_statuses = ['defined', 'started', 'locked_in', 'active', 'failed']
        assert result['status'] in valid_statuses, f"Invalid status: {result['status']}"
        
        self.log.info(f"Status '{result['status']}' is valid")
        
        if result['status'] == 'active':
            assert result['enabled'] == True, "If active, enabled should be True"
            if 'activation_height' in result:
                assert isinstance(result['activation_height'], int)
                assert_greater_than_or_equal(result['activation_height'], 0)
                self.log.info(f"Activation height: {result['activation_height']}")
        
        if result['status'] in ['started', 'locked_in']:
            assert 'signaling_blocks' in result, "Missing signaling_blocks for started/locked_in"
            assert 'threshold' in result, "Missing threshold for started/locked_in"
            assert 'period_blocks' in result, "Missing period_blocks for started/locked_in"
            self.log.info(f"Signaling: {result.get('signaling_blocks', 0)}/{result.get('threshold', 0)}")

    def test_deployment_info_oracle_activation_fields(self):
        # Wave 9 (Agent C): operators need a single RPC that exposes the
        # MuSig2 oracle activation height and the active roster shape so
        # they can correlate `nDigiDollarMuSig2Height`, `nOracleConsensusRequired`,
        # and `nOraclePubkeyCount` with the BIP9 deployment status. Before
        # this fix the RPC only exposed BIP9 fields; operators had to read
        # chainparams source to discover the oracle quorum.
        self.log.info("Testing deployment info exposes oracle activation fields...")
        node = self.nodes[0]
        result = node.getdigidollardeploymentinfo()

        for field in (
                "oracle_activation_height",
                "musig2_format_activation_height",
                "oracle_pubkey_count",
                "oracle_consensus_required",
                "oracle_total_slots"):
            assert field in result, f"Missing '{field}' field — operator cannot see oracle roster shape"
            assert isinstance(result[field], int), f"'{field}' should be integer"

        # Default regtest keeps oracle/DD height gates at 650, but DigiDollar
        # BIP9 is ALWAYS_ACTIVE from height 0. MuSig2 follows that effective
        # DigiDollar boundary so v0x03 quotes are valid whenever DD is active.
        assert_equal(result["oracle_activation_height"], 650)
        assert_equal(result["musig2_format_activation_height"], 0)
        # Regtest 4-of-7 quorum.
        assert_equal(result["oracle_pubkey_count"], 7)
        assert_equal(result["oracle_consensus_required"], 4)
        # Regtest configures 7 oracle slots.
        assert_equal(result["oracle_total_slots"], 7)

        self.log.info(
            "Oracle activation fields verified: oracle_height=%d, musig2_height=%d, consensus=%d-of-%d, slots=%d" % (
                result["oracle_activation_height"],
                result["musig2_format_activation_height"],
                result["oracle_consensus_required"],
                result["oracle_pubkey_count"],
                result["oracle_total_slots"]))

    def test_deployment_after_activation(self):
        self.log.info("Testing deployment info consistency...")
        node = self.nodes[0]
        
        result1 = node.getdigidollardeploymentinfo()
        
        self.generate(node, 10)
        
        result2 = node.getdigidollardeploymentinfo()
        
        assert_equal(result1['bit'], result2['bit'])
        assert_equal(result1['start_time'], result2['start_time'])
        assert_equal(result1['timeout'], result2['timeout'])
        assert_equal(result1['min_activation_height'], result2['min_activation_height'])
        
        if result1['status'] == 'active':
            assert_equal(result1['enabled'], result2['enabled'])
        
        self.log.info("Deployment info is consistent across blocks")


if __name__ == '__main__':
    DigiDollarRPCDeploymentTest().main()
