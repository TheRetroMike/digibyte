#!/usr/bin/env python3
# Copyright (c) 2025-2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from decimal import Decimal
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_greater_than_or_equal


class DigiDollarRPCProtectionTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing DigiDollar protection status RPC...")
        node = self.nodes[0]
        
        self.log.info("Generating initial blocks for test setup...")
        self.generate(node, 110)

        self.test_oracle_unavailable_is_separate_from_err()

        node.setmockoracleprice(6000)
        
        self.test_protection_status_basic()
        self.test_protection_status_dca()
        self.test_protection_status_err()
        self.test_protection_status_volatility()
        self.test_protection_status_overall()
        self.test_protection_status_consistency()
        
        self.log.info("All protection status tests passed!")

    def test_oracle_unavailable_is_separate_from_err(self):
        self.log.info("Testing no-oracle state is reported separately from ERR...")
        node = self.nodes[0]
        node.enablemockoracle(False)

        stats = node.getdigidollarstats()
        assert_equal(stats["oracle_available"], False)
        assert_equal(stats["oracle_status"], "unavailable")
        assert_equal(stats["minting_restricted_reason"], "oracle_unavailable")
        assert_equal(stats["is_emergency"], False)

        protection = node.getprotectionstatus()
        assert 'oracle' in protection, "Missing 'oracle' section"
        assert_equal(protection["oracle"]["available"], False)
        assert_equal(protection["oracle"]["minting_restricted"], True)
        assert_equal(protection["oracle"]["minting_restricted_reason"], "oracle_unavailable")
        assert_equal(protection["err"]["active"], False)
        assert_equal(protection["err"]["evaluation_status"], "oracle_unavailable")
        assert "oracle_fail_closed" in protection["overall"]["active_protections"]
        node.enablemockoracle(True)

    def test_protection_status_basic(self):
        self.log.info("Testing basic protection status response...")
        node = self.nodes[0]
        
        result = node.getprotectionstatus()
        
        assert 'oracle' in result, "Missing 'oracle' section"
        assert 'dca' in result, "Missing 'dca' section"
        assert 'err' in result, "Missing 'err' section"
        assert 'volatility' in result, "Missing 'volatility' section"
        assert 'overall' in result, "Missing 'overall' section"
        
        self.log.info("All major sections present in response")

    def test_protection_status_dca(self):
        self.log.info("Testing DCA (Dynamic Collateral Adjustment) status...")
        node = self.nodes[0]
        
        result = node.getprotectionstatus()
        dca = result['dca']
        
        assert 'active' in dca, "Missing 'active' in DCA"
        assert 'current_multiplier' in dca, "Missing 'current_multiplier' in DCA"
        assert 'tier' in dca, "Missing 'tier' in DCA"
        assert 'system_health' in dca, "Missing 'system_health' in DCA"
        
        assert isinstance(dca['active'], bool), "active should be boolean"
        assert isinstance(dca['current_multiplier'], (int, float)), "current_multiplier should be numeric"
        assert isinstance(dca['tier'], str), "tier should be string"
        assert isinstance(dca['system_health'], (int, float)), "system_health should be numeric"
        
        valid_tiers = ['healthy', 'warning', 'critical', 'emergency']
        assert dca['tier'] in valid_tiers, f"Invalid DCA tier: {dca['tier']}"
        
        assert_greater_than_or_equal(dca['current_multiplier'], 1.0)
        
        self.log.info(f"DCA status: tier={dca['tier']}, multiplier={dca['current_multiplier']}")
        self.log.info(f"System health: {dca['system_health']}%")

    def test_protection_status_err(self):
        self.log.info("Testing ERR (Emergency Redemption Ratio) status...")
        node = self.nodes[0]
        
        result = node.getprotectionstatus()
        err = result['err']
        
        assert 'active' in err, "Missing 'active' in ERR"
        assert 'threshold' in err, "Missing 'threshold' in ERR"
        assert 'current_ratio' in err, "Missing 'current_ratio' in ERR"
        assert 'status' in err, "Missing 'status' in ERR"
        
        assert isinstance(err['active'], bool), "active should be boolean"
        assert isinstance(err['threshold'], (int, float)), "threshold should be numeric"
        assert isinstance(err['current_ratio'], (int, float)), "current_ratio should be numeric"
        assert isinstance(err['status'], str), "status should be string"
        
        valid_statuses = ['normal', 'warning', 'active', 'critical']
        assert err['status'] in valid_statuses, f"Invalid ERR status: {err['status']}"

        stats = node.getdigidollarstats()
        if stats['total_dd_supply'] == 0:
            assert_equal(err['active'], False)
            assert_equal(err['status'], 'normal')
            assert_equal(err['current_ratio'], stats['health_percentage'])
        
        self.log.info(f"ERR status: {err['status']}, active={err['active']}")
        self.log.info(f"ERR ratio: {err['current_ratio']}% (threshold: {err['threshold']}%)")

    def test_protection_status_volatility(self):
        self.log.info("Testing volatility protection status...")
        node = self.nodes[0]
        
        result = node.getprotectionstatus()
        vol = result['volatility']
        
        assert 'protection_active' in vol, "Missing 'protection_active' in volatility"
        assert 'current_volatility' in vol, "Missing 'current_volatility' in volatility"
        assert 'protection_threshold' in vol, "Missing 'protection_threshold' in volatility"
        assert 'minting_restricted' in vol, "Missing 'minting_restricted' in volatility"
        
        assert isinstance(vol['protection_active'], bool), "protection_active should be boolean"
        assert isinstance(vol['current_volatility'], (int, float, Decimal)), "current_volatility should be numeric"
        assert isinstance(vol['protection_threshold'], (int, float, Decimal)), "protection_threshold should be numeric"
        assert isinstance(vol['minting_restricted'], bool), "minting_restricted should be boolean"
        
        assert_greater_than_or_equal(vol['current_volatility'], 0)
        assert_greater_than_or_equal(vol['protection_threshold'], 0)
        
        self.log.info(f"Volatility: {vol['current_volatility']}% (threshold: {vol['protection_threshold']}%)")
        self.log.info(f"Minting restricted: {vol['minting_restricted']}")

    def test_protection_status_overall(self):
        self.log.info("Testing overall protection status...")
        node = self.nodes[0]
        
        result = node.getprotectionstatus()
        overall = result['overall']
        
        assert 'status' in overall, "Missing 'status' in overall"
        assert 'active_protections' in overall, "Missing 'active_protections' in overall"
        assert 'warnings' in overall, "Missing 'warnings' in overall"
        
        assert isinstance(overall['status'], str), "status should be string"
        assert isinstance(overall['active_protections'], list), "active_protections should be list"
        assert isinstance(overall['warnings'], list), "warnings should be list"
        
        valid_overall_statuses = ['secure', 'warning', 'critical', 'emergency']
        assert overall['status'] in valid_overall_statuses, f"Invalid overall status: {overall['status']}"

        stats = node.getdigidollarstats()
        if stats['total_dd_supply'] == 0:
            assert_equal(overall['status'], 'secure')
            assert 'err' not in overall['active_protections']
        
        self.log.info(f"Overall system status: {overall['status']}")
        self.log.info(f"Active protections: {overall['active_protections']}")
        if overall['warnings']:
            self.log.info(f"Warnings: {overall['warnings']}")

    def test_protection_status_consistency(self):
        self.log.info("Testing protection status consistency...")
        node = self.nodes[0]
        
        result1 = node.getprotectionstatus()
        result2 = node.getprotectionstatus()
        
        assert_equal(result1['dca']['tier'], result2['dca']['tier'])
        assert_equal(result1['err']['status'], result2['err']['status'])
        assert_equal(result1['overall']['status'], result2['overall']['status'])
        
        self.log.info("Protection status is consistent across calls")
        
        self.generate(node, 5)
        
        result3 = node.getprotectionstatus()
        
        self.log.info("Protection status updated after new blocks")
        self.log.info(f"DCA tier: {result3['dca']['tier']}")
        self.log.info(f"ERR status: {result3['err']['status']}")


if __name__ == '__main__':
    DigiDollarRPCProtectionTest().main()
