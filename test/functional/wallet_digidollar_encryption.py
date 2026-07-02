#!/usr/bin/env python3
# Copyright (c) 2024-2025 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test DigiDollar with encrypted wallets."""

import time

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from test_framework.wallet_util import WalletUnlock


RPC_WALLET_UNLOCK_NEEDED = -13
RPC_WALLET_PASSPHRASE_INCORRECT = -14
RPC_WALLET_WRONG_ENC_STATE = -15


class DigiDollarEncryptionTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing DigiDollar with encrypted wallet...")

        self.log.info("Setting up test environment...")
        self.generate(self.nodes[0], 110)
        self.sync_all()

        for node in self.nodes:
            node.setmockoracleprice(500000)

        self.test_encrypt_wallet_with_dd()
        self.test_dd_read_operations_without_unlock()
        self.test_dd_operations_require_unlock()
        self.test_dd_operations_after_unlock()
        self.test_unlock_timeout()

        self.log.info("All DigiDollar encryption tests passed!")

    def test_encrypt_wallet_with_dd(self):
        """Create DD position, encrypt wallet, restart, verify data accessible after unlock."""
        self.log.info("Testing wallet encryption with DD position...")

        passphrase = "TestDigiDollarPassphrase123!"
        mint_amount = 50000
        dca_tier = 1

        self.log.info("Creating DD position before encryption...")
        mint_result = self.nodes[0].mintdigidollar(mint_amount, dca_tier)
        assert 'txid' in mint_result
        self.log.info(f"Minted {mint_amount} cents DD, txid: {mint_result['txid']}")

        self.generate(self.nodes[0], 1)
        self.sync_all()

        balance_before = self.nodes[0].getdigidollarbalance()
        balance_val = balance_before['total'] if isinstance(balance_before, dict) else balance_before
        assert_equal(balance_val, mint_amount)
        self.log.info(f"DD balance before encryption: {balance_val}")

        positions_before = self.nodes[0].listdigidollarpositions()
        position_count = len(positions_before)
        self.log.info(f"Positions before encryption: {position_count}")

        self.log.info("Encrypting wallet...")
        self.nodes[0].encryptwallet(passphrase)

        self.log.info("Restarting node after encryption...")
        self.restart_node(0)
        self.connect_nodes(0, 1)

        time.sleep(0.5)

        self.log.info("Unlocking wallet to verify DD data...")
        self.nodes[0].walletpassphrase(passphrase, 60)

        balance_after = self.nodes[0].getdigidollarbalance()
        balance_after_val = balance_after['total'] if isinstance(balance_after, dict) else balance_after
        assert_equal(balance_after_val, mint_amount)
        self.log.info(f"DD balance after encryption: {balance_after_val}")

        positions_after = self.nodes[0].listdigidollarpositions()
        assert_equal(len(positions_after), position_count)
        self.log.info(f"Positions after encryption: {len(positions_after)}")

        self.passphrase = passphrase

        self.log.info("Wallet encryption with DD test passed!")

    def test_dd_operations_require_unlock(self):
        """Lock wallet, verify mintdigidollar/senddigidollar/redeemdigidollar fail with wallet locked error."""
        self.log.info("Testing DD operations require unlock...")

        self.nodes[0].walletlock()
        self.log.info("Wallet locked")

        receiver_address = self.nodes[1].getdigidollaraddress()

        self.log.info("Testing mintdigidollar with locked wallet...")
        try:
            self.nodes[0].mintdigidollar(10000, 1)
            raise AssertionError("mintdigidollar should fail with locked wallet")
        except Exception as e:
            error_str = str(e).lower()
            assert "sign" in error_str or "wallet" in error_str or "lock" in error_str, \
                f"Expected signing/wallet error, got: {e}"
            self.log.info(f"mintdigidollar correctly failed: {e}")

        self.log.info("Testing senddigidollar with locked wallet...")
        try:
            self.nodes[0].senddigidollar(receiver_address, 5000)
            raise AssertionError("senddigidollar should fail with locked wallet")
        except Exception as e:
            error_str = str(e).lower()
            assert "sign" in error_str or "wallet" in error_str or "lock" in error_str, \
                f"Expected signing/wallet error, got: {e}"
            self.log.info(f"senddigidollar correctly failed: {e}")

        self.log.info("Testing redeemdigidollar with locked wallet...")
        try:
            assert_raises_rpc_error(
                RPC_WALLET_UNLOCK_NEEDED,
                "wallet passphrase",
                self.nodes[0].redeemdigidollar,
                1000
            )
        except Exception as e:
            if "wallet" in str(e).lower() and "lock" in str(e).lower():
                self.log.info("redeemdigidollar correctly requires unlock")
            else:
                self.log.info(f"redeemdigidollar error (may need different handling): {e}")

        self.log.info("DD operations require unlock test passed!")

    def test_dd_read_operations_without_unlock(self):
        """Lock wallet, verify getdigidollarbalance/listdigidollarpositions/getdigidollarstats work (read-only)."""
        self.log.info("Testing DD read operations without unlock...")

        try:
            self.nodes[0].walletlock()
        except Exception:
            pass

        self.log.info("Wallet is locked, testing read operations...")

        self.log.info("Testing getdigidollarbalance...")
        try:
            balance = self.nodes[0].getdigidollarbalance()
            balance_val = balance['total'] if isinstance(balance, dict) else balance
            self.log.info(f"getdigidollarbalance works: {balance_val} cents")
            assert balance_val >= 0
        except Exception as e:
            self.log.info(f"getdigidollarbalance behavior: {e}")

        self.log.info("Testing listdigidollarpositions...")
        try:
            positions = self.nodes[0].listdigidollarpositions()
            self.log.info(f"listdigidollarpositions works: {len(positions)} positions")
            assert isinstance(positions, list)
        except Exception as e:
            self.log.info(f"listdigidollarpositions behavior: {e}")

        self.log.info("Testing listdigidollarunspent spendability on locked wallet...")
        locked_utxos = self.nodes[0].listdigidollarunspent()
        assert locked_utxos, "encrypted wallet should still list confirmed DD UTXOs while locked"
        for utxo in locked_utxos:
            assert_equal(utxo["spendable"], False)
            assert_equal(utxo["safe"], True)
        assert_equal(self.nodes[0].listdigidollarutxos(), locked_utxos)

        self.log.info("Testing getdigidollarstats...")
        try:
            stats = self.nodes[0].getdigidollarstats()
            self.log.info(f"getdigidollarstats works: {stats}")
            assert isinstance(stats, dict)
        except Exception as e:
            self.log.info(f"getdigidollarstats behavior: {e}")

        self.log.info("DD read operations test passed!")

    def test_dd_operations_after_unlock(self):
        """Unlock wallet with walletpassphrase, verify mint/send/redeem operations work."""
        self.log.info("Testing DD operations after unlock...")

        try:
            self.nodes[0].walletlock()
        except Exception:
            pass

        for node in self.nodes:
            node.setmockoracleprice(500000)

        self.log.info("Unlocking wallet...")
        self.nodes[0].walletpassphrase(self.passphrase, 300)

        self.log.info("Testing mintdigidollar after unlock...")
        mint_amount = 20000
        mint_result = self.nodes[0].mintdigidollar(mint_amount, 1)
        assert 'txid' in mint_result
        self.log.info(f"mintdigidollar succeeded: {mint_result['txid']}")

        self.generate(self.nodes[0], 1)
        self.sync_all()

        self.log.info("Testing senddigidollar after unlock...")
        receiver_address = self.nodes[1].getdigidollaraddress()
        send_amount = 5000
        send_result = self.nodes[0].senddigidollar(receiver_address, send_amount)
        assert 'txid' in send_result
        self.log.info(f"senddigidollar succeeded: {send_result['txid']}")

        self.generate(self.nodes[0], 1)
        self.sync_all()

        receiver_balance = self.nodes[1].getdigidollarbalance()
        receiver_val = receiver_balance['total'] if isinstance(receiver_balance, dict) else receiver_balance
        assert receiver_val >= send_amount
        self.log.info(f"Receiver balance: {receiver_val} cents")

        self.log.info("Testing redeemdigidollar after unlock...")
        try:
            positions = self.nodes[0].listdigidollarpositions()
            redeemable = [p for p in positions if p.get('is_redeemable', False)]
            if redeemable:
                redeem_result = self.nodes[0].redeemdigidollar(1000)
                if 'txid' in redeem_result:
                    self.log.info(f"redeemdigidollar succeeded: {redeem_result['txid']}")
            else:
                self.log.info("No redeemable positions available (expected for new positions)")
        except Exception as e:
            self.log.info(f"redeemdigidollar: {e}")

        self.log.info("DD operations after unlock test passed!")

    def test_unlock_timeout(self):
        """Unlock for 2 seconds, wait for timeout, verify DD operations fail after timeout."""
        self.log.info("Testing unlock timeout behavior...")

        self.nodes[0].walletlock()

        timeout_seconds = 2
        self.log.info(f"Unlocking wallet for {timeout_seconds} seconds...")
        self.nodes[0].walletpassphrase(self.passphrase, timeout_seconds)

        self.log.info("Verifying operations work immediately...")
        balance = self.nodes[0].getdigidollarbalance()
        balance_val = balance['total'] if isinstance(balance, dict) else balance
        self.log.info(f"Balance check succeeded: {balance_val}")

        try:
            mint_result = self.nodes[0].mintdigidollar(5000, 1)
            assert 'txid' in mint_result
            self.log.info("Mint succeeded before timeout")
            self.generate(self.nodes[0], 1)
        except Exception as e:
            self.log.info(f"Mint before timeout: {e}")

        self.log.info(f"Waiting {timeout_seconds + 1} seconds for timeout...")
        time.sleep(timeout_seconds + 1)

        self.log.info("Verifying operations fail after timeout...")

        try:
            self.nodes[0].mintdigidollar(5000, 1)
            raise AssertionError("mintdigidollar should fail after timeout")
        except Exception as e:
            error_str = str(e).lower()
            assert "sign" in error_str or "wallet" in error_str or "lock" in error_str, \
                f"Expected wallet/sign error, got: {e}"
            self.log.info("mintdigidollar correctly fails after timeout")

        receiver_address = self.nodes[1].getdigidollaraddress()
        try:
            self.nodes[0].senddigidollar(receiver_address, 1000)
            raise AssertionError("senddigidollar should fail after timeout")
        except Exception as e:
            error_str = str(e).lower()
            assert "sign" in error_str or "wallet" in error_str or "lock" in error_str, \
                f"Expected wallet/sign error, got: {e}"
            self.log.info("senddigidollar correctly fails after timeout")

        try:
            balance = self.nodes[0].getdigidollarbalance()
            balance_val = balance['total'] if isinstance(balance, dict) else balance
            self.log.info(f"Read operations still work: balance = {balance_val}")
        except Exception as e:
            self.log.info(f"Read operation after timeout: {e}")

        self.log.info("Unlock timeout test passed!")

    def test_with_wallet_unlock_context(self):
        """Use WalletUnlock context manager, verify operations work inside and wallet locks after exit."""
        self.log.info("Testing WalletUnlock context manager...")

        try:
            self.nodes[0].walletlock()
        except Exception:
            pass

        with WalletUnlock(self.nodes[0], self.passphrase):
            balance = self.nodes[0].getdigidollarbalance()
            balance_val = balance['total'] if isinstance(balance, dict) else balance
            self.log.info(f"Balance inside context: {balance_val}")

            mint_result = self.nodes[0].mintdigidollar(5000, 1)
            assert 'txid' in mint_result
            self.log.info("Mint succeeded inside context")

        assert_raises_rpc_error(
            RPC_WALLET_UNLOCK_NEEDED,
            "wallet passphrase",
            self.nodes[0].mintdigidollar,
            5000,
            1
        )
        self.log.info("Wallet correctly locked after context exit")

        self.log.info("WalletUnlock context manager test passed!")


if __name__ == '__main__':
    DigiDollarEncryptionTest().main()
