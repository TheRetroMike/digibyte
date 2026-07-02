#!/usr/bin/env python3
# Copyright (c) 2014-2025 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test DigiDollar data in wallet backup/restore."""

import os
import stat
import tempfile
from decimal import Decimal

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)

ORACLE_PRICE_HALF_USD = 500000  # micro-USD: $0.50 per DGB
TIER_30_DAYS = 1
TIER_90_DAYS = 2
TIER_180_DAYS = 3
TIER_365_DAYS = 4
TIER_3_YEARS = 5
TIER_5_YEARS = 6


class DigiDollarBackupTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-digidollar=1",
            "-txindex=1",
            "-dandelion=0",
            "-whitelist=noban@127.0.0.1",
        ]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing DigiDollar wallet backup/restore...")
        self.setup_digidollar_test()

        self.test_backup_includes_dd_positions()
        self.test_backup_includes_dd_balance()
        self.test_backup_multiple_positions()
        self.test_restore_to_different_wallet_name()
        self.test_backup_file_permissions()
        self.test_encrypted_backup_restores_dd_keys_after_unlock()

        self.log.info("All DigiDollar backup tests passed!")

    def setup_digidollar_test(self):
        self.log.info("Setting up DigiDollar test environment...")
        self.generate(self.nodes[0], 110)
        self.nodes[0].setmockoracleprice(ORACLE_PRICE_HALF_USD)

        balance = self.nodes[0].getbalance()
        self.log.info(f"Node 0 DGB balance: {balance}")
        assert_greater_than(balance, 10000)

    def test_backup_includes_dd_positions(self):
        self.log.info("Testing backup includes DD positions...")

        node = self.nodes[0]
        restored_wallet_name = "restored_positions"

        mint_amount_cents = 10000
        mint_result = node.mintdigidollar(mint_amount_cents, TIER_365_DAYS)
        mint_txid = mint_result['txid']
        self.log.info(f"Created DD position with txid: {mint_txid}")

        self.generate(node, 1)

        positions_before = node.listdigidollarpositions()
        assert_greater_than(len(positions_before), 0)
        self.log.info(f"Positions before backup: {len(positions_before)}")

        with tempfile.TemporaryDirectory() as temp_dir:
            backup_path = os.path.join(temp_dir, "backup_positions.dat")

            try:
                node.backupwallet(backup_path)
                assert os.path.exists(backup_path)
                self.log.info(f"Wallet backed up to: {backup_path}")

                node.restorewallet(restored_wallet_name, backup_path)

                restored_wallet = node.get_wallet_rpc(restored_wallet_name)

                positions_after = restored_wallet.listdigidollarpositions()
                assert_equal(len(positions_before), len(positions_after))

                for pos_before in positions_before:
                    matching_pos = None
                    for pos_after in positions_after:
                        if self._positions_match(pos_before, pos_after):
                            matching_pos = pos_after
                            break

                    assert matching_pos is not None, \
                        f"Position not found after restore: {pos_before}"

                self.log.info("DD positions successfully restored!")

            finally:
                try:
                    node.unloadwallet(restored_wallet_name)
                except Exception:
                    pass

    def test_backup_includes_dd_balance(self):
        self.log.info("Testing backup includes DD balance...")

        node = self.nodes[0]
        restored_wallet_name = "restored_balance"

        mint_amount_cents = 25000
        mint_result = node.mintdigidollar(mint_amount_cents, TIER_90_DAYS)
        self.log.info(f"Minted DD: {mint_result}")

        self.generate(node, 1)

        balance_info_before = node.getdigidollarbalance()
        balance_before = balance_info_before['total'] if isinstance(balance_info_before, dict) else balance_info_before
        self.log.info(f"DD balance before backup: {balance_before}")
        assert_greater_than(balance_before, 0)

        with tempfile.TemporaryDirectory() as temp_dir:
            backup_path = os.path.join(temp_dir, "backup_balance.dat")

            try:
                node.backupwallet(backup_path)
                assert os.path.exists(backup_path)

                node.restorewallet(restored_wallet_name, backup_path)

                restored_wallet = node.get_wallet_rpc(restored_wallet_name)

                balance_info_after = restored_wallet.getdigidollarbalance()
                balance_after = balance_info_after['total'] if isinstance(balance_info_after, dict) else balance_info_after

                assert_equal(balance_before, balance_after)
                self.log.info(f"DD balance successfully restored: {balance_after}")

            finally:
                try:
                    node.unloadwallet(restored_wallet_name)
                except Exception:
                    pass

    def test_backup_multiple_positions(self):
        self.log.info("Testing backup with multiple positions (different tiers)...")

        node = self.nodes[0]
        restored_wallet_name = "restored_multiple"

        positions_to_create = [
            {"amount": 15000, "tier": TIER_30_DAYS},
            {"amount": 30000, "tier": TIER_180_DAYS},
            {"amount": 50000, "tier": TIER_3_YEARS},
        ]

        created_txids = []
        for pos_data in positions_to_create:
            result = node.mintdigidollar(pos_data["amount"], pos_data["tier"])
            created_txids.append(result['txid'])
            self.log.info(f"Created position tier {pos_data['tier']}: txid={result['txid']}")

        self.generate(node, 3)

        positions_before = node.listdigidollarpositions()
        num_positions_before = len(positions_before)
        self.log.info(f"Total positions before backup: {num_positions_before}")
        assert_greater_than(num_positions_before, 2)

        balance_info_before = node.getdigidollarbalance()
        total_before = balance_info_before['total'] if isinstance(balance_info_before, dict) else balance_info_before

        with tempfile.TemporaryDirectory() as temp_dir:
            backup_path = os.path.join(temp_dir, "backup_multiple.dat")

            try:
                node.backupwallet(backup_path)
                assert os.path.exists(backup_path)

                node.restorewallet(restored_wallet_name, backup_path)

                restored_wallet = node.get_wallet_rpc(restored_wallet_name)

                positions_after = restored_wallet.listdigidollarpositions()
                assert_equal(num_positions_before, len(positions_after))

                balance_info_after = restored_wallet.getdigidollarbalance()
                total_after = balance_info_after['total'] if isinstance(balance_info_after, dict) else balance_info_after
                assert_equal(total_before, total_after)

                self.log.info(f"All {len(positions_after)} positions successfully restored!")
                self.log.info(f"Total DD balance preserved: {total_after}")

            finally:
                try:
                    node.unloadwallet(restored_wallet_name)
                except Exception:
                    pass

    def test_restore_to_different_wallet_name(self):
        self.log.info("Testing restore to different wallet name...")

        node = self.nodes[0]
        new_wallet_name = "wallet2_restored"

        mint_amount_cents = 20000
        mint_result = node.mintdigidollar(mint_amount_cents, TIER_5_YEARS)
        original_txid = mint_result['txid']
        self.log.info(f"Created position with txid: {original_txid}")

        self.generate(node, 1)

        positions_before = node.listdigidollarpositions()
        balance_info_before = node.getdigidollarbalance()
        balance_before = balance_info_before['total'] if isinstance(balance_info_before, dict) else balance_info_before

        with tempfile.TemporaryDirectory() as temp_dir:
            backup_path = os.path.join(temp_dir, "wallet1_backup.dat")

            try:
                node.backupwallet(backup_path)
                assert os.path.exists(backup_path)
                self.log.info(f"Backed up as wallet1 to: {backup_path}")

                node.restorewallet(new_wallet_name, backup_path)
                self.log.info(f"Restored as: {new_wallet_name}")

                wallet2 = node.get_wallet_rpc(new_wallet_name)

                positions_in_wallet2 = wallet2.listdigidollarpositions()
                balance_info_wallet2 = wallet2.getdigidollarbalance()
                balance_wallet2 = balance_info_wallet2['total'] if isinstance(balance_info_wallet2, dict) else balance_info_wallet2

                assert_equal(len(positions_before), len(positions_in_wallet2))
                assert_equal(balance_before, balance_wallet2)

                dd_address = wallet2.getdigidollaraddress()
                assert isinstance(dd_address, str)
                assert len(dd_address) > 0

                self.log.info(f"DD data accessible in renamed wallet: {new_wallet_name}")
                self.log.info(f"  Positions: {len(positions_in_wallet2)}")
                self.log.info(f"  Balance: {balance_wallet2}")
                self.log.info(f"  Can generate addresses: {dd_address[:20]}...")

            finally:
                try:
                    node.unloadwallet(new_wallet_name)
                except Exception:
                    pass

    def test_backup_file_permissions(self):
        self.log.info("Testing backup file permissions...")

        node = self.nodes[0]

        with tempfile.TemporaryDirectory() as temp_dir:
            backup_path = os.path.join(temp_dir, "backup_perms.dat")

            node.backupwallet(backup_path)

            assert os.path.exists(backup_path)

            file_stat = os.stat(backup_path)
            file_mode = file_stat.st_mode

            assert file_mode & stat.S_IRUSR, "Backup file should be readable by owner"

            is_world_writable = file_mode & stat.S_IWOTH
            if is_world_writable:
                self.log.warning("Backup file is world-writable - potential security issue")

            file_size = file_stat.st_size
            assert_greater_than(file_size, 1000)
            self.log.info(f"Backup file size: {file_size} bytes")

            test_wallet_name = "perms_test_restore"
            try:
                node.restorewallet(test_wallet_name, backup_path)
                self.log.info("Backup file is valid and can be restored")
                node.unloadwallet(test_wallet_name)
            except Exception as e:
                self.log.error(f"Backup file appears invalid: {e}")
                raise AssertionError("Backup file should be restorable")

            self.log.info("Backup file permissions test passed!")

    def test_encrypted_backup_restores_dd_keys_after_unlock(self):
        self.log.info("Testing encrypted backup restores DD keys after unlock...")

        node = self.nodes[0]
        restored_wallet_name = "restored_encrypted_dd"
        passphrase = "DigiDollarEncryptedBackupPass123!"
        mint_amount_cents = 7500

        self.log.info("Encrypting wallet that already contains DD state...")
        node.encryptwallet(passphrase)
        self.restart_node(0)
        node = self.nodes[0]
        node.setmockoracleprice(ORACLE_PRICE_HALF_USD)
        node.walletpassphrase(passphrase, 300)

        mint_result = node.mintdigidollar(mint_amount_cents, 0)
        position_id = mint_result["position_id"]
        unlock_height = mint_result["unlock_height"]
        self.generate(node, 1)

        balance_before = node.getdigidollarbalance()["total"]
        positions_before = node.listdigidollarpositions(False)
        assert any(p.get("position_id") == position_id for p in positions_before)

        node.walletlock()

        with tempfile.TemporaryDirectory() as temp_dir:
            backup_path = os.path.join(temp_dir, "encrypted_dd_backup.dat")

            try:
                node.backupwallet(backup_path)
                assert os.path.exists(backup_path)

                node.restorewallet(restored_wallet_name, backup_path)
                restored_wallet = node.get_wallet_rpc(restored_wallet_name)

                assert_equal(restored_wallet.getdigidollarbalance()["total"], balance_before)
                restored_positions = restored_wallet.listdigidollarpositions(False)
                assert any(p.get("position_id") == position_id for p in restored_positions)

                assert_raises_rpc_error(
                    -13,
                    "Please enter the wallet passphrase with walletpassphrase first",
                    restored_wallet.redeemdigidollar,
                    position_id,
                    mint_amount_cents,
                )

                restored_wallet.walletpassphrase(passphrase, 300)

                current_height = node.getblockcount()
                if current_height <= unlock_height:
                    self.generate(node, unlock_height - current_height + 1)

                node.setmockoracleprice(ORACLE_PRICE_HALF_USD)
                redeem_result = restored_wallet.redeemdigidollar(position_id, mint_amount_cents)
                assert "txid" in redeem_result
                self.generate(node, 1)

                assert_equal(
                    restored_wallet.getdigidollarbalance()["total"],
                    balance_before - mint_amount_cents,
                )
                positions_after = restored_wallet.listdigidollarpositions(False)
                restored_position = next(
                    p for p in positions_after if p.get("position_id") == position_id
                )
                assert_equal(restored_position["status"], "redeemed")

                self.log.info("Encrypted DD backup restored and redeemed successfully")

            finally:
                try:
                    node.unloadwallet(restored_wallet_name)
                except Exception:
                    pass

    def _positions_match(self, pos1, pos2):
        key_fields = [
            ('dd_minted', 'dd_minted'),
            ('amount', 'amount'),
            ('unlock_height', 'unlock_height'),
            ('lock_height', 'lock_height'),
            ('dgb_collateral', 'dgb_collateral'),
            ('collateral_locked', 'collateral_locked'),
        ]

        matches = 0
        for field1, field2 in key_fields:
            if field1 in pos1 and field2 in pos2:
                if pos1[field1] == pos2[field2]:
                    matches += 1

        return matches >= 2


if __name__ == '__main__':
    DigiDollarBackupTest().main()
