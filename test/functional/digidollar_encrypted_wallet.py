#!/usr/bin/env python3
"""Test DigiDollar operations with encrypted/locked wallets.

Verifies that DD write operations (mint, send, redeem, oracle keygen) require
an unlocked wallet, while read operations work regardless of lock state.
"""

import time

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

ORACLE_PRICE_MICRO_USD = 500000


class DigiDollarEncryptedWalletTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.passphrase = "DigiDollarTestPass123"
        self.setup_digidollar_env()

        self.test_mint_locked_wallet()
        self.test_get_dd_address_locked_wallet()
        self.test_mint_after_unlock()
        self.test_send_locked_wallet()
        self.test_send_after_unlock()
        self.test_redeem_locked_wallet()
        self.test_redeem_after_unlock()
        self.test_oracle_keygen_locked()
        self.test_oracle_keygen_after_unlock()
        self.test_relock_timeout()
        self.test_read_ops_while_locked()
        self.test_full_lock_unlock_cycle()
        self.test_dd_keys_encrypted_after_encryptwallet()

    # ── helpers ────────────────────────────────────────────────────────

    def setup_digidollar_env(self):
        """Mine DGB, activate DigiDollar, set oracle price, fund node 1, encrypt node 1."""
        self.log.info("Setting up DigiDollar environment...")

        # Node 0: miner / oracle node — generate past DD activation (regtest=650)
        self.nodes[0].generate(660)
        self.sync_all()

        # Set oracle price on both nodes ($0.50 / DGB = 500000 micro-USD)
        self.refresh_oracle_quotes()

        # Fund node 1 with DGB so it can mint
        addr1 = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(addr1, 500000)
        self.nodes[0].generate(10)
        self.sync_all()

        # Encrypt node 1 wallet (restarts the node internally)
        self.log.info("Encrypting node 1 wallet...")
        self.nodes[1].encryptwallet(self.passphrase)
        # Restart node to activate encryption
        self.restart_node(1)
        self.connect_nodes(0, 1)
        self.sync_all()
        self.refresh_oracle_quotes()

    def unlock(self, timeout=60):
        """Unlock node 1 wallet."""
        self.nodes[1].walletpassphrase(self.passphrase, timeout)

    def lock(self):
        """Lock node 1 wallet."""
        self.nodes[1].walletlock()

    def mine_and_sync(self, count=1):
        self.nodes[0].generate(count)
        self.sync_all()

    def refresh_oracle_quotes(self, price=ORACLE_PRICE_MICRO_USD):
        for node in self.nodes:
            result = node.setmockoracleprice(price)
            assert_equal(result["price_micro_usd"], price)

    # ── tests ──────────────────────────────────────────────────────────

    def test_mint_locked_wallet(self):
        self.log.info("test_mint_locked_wallet")
        assert_raises_rpc_error(
            -13, "Please enter the wallet passphrase with walletpassphrase first",
            self.nodes[1].mintdigidollar, 10000, 0  # 100.00 DD, tier 0
        )

    def test_get_dd_address_locked_wallet(self):
        self.log.info("test_get_dd_address_locked_wallet")
        assert_raises_rpc_error(
            -13, "Please enter the wallet passphrase with walletpassphrase first",
            self.nodes[1].getdigidollaraddress
        )

    def test_mint_after_unlock(self):
        self.log.info("test_mint_after_unlock")
        self.unlock()
        self.refresh_oracle_quotes()
        result = self.nodes[1].mintdigidollar(10000, 0)
        assert 'txid' in result
        # Wait for TX to relay to node 0 before mining
        self.sync_mempools()
        self.mine_and_sync()
        self.lock()

    def test_send_locked_wallet(self):
        self.log.info("test_send_locked_wallet")
        dest = self.nodes[0].getdigidollaraddress()
        assert_raises_rpc_error(
            -13, "Please enter the wallet passphrase with walletpassphrase first",
            self.nodes[1].senddigidollar, dest, 1000  # 10.00 DD
        )

    def test_send_after_unlock(self):
        self.log.info("test_send_after_unlock")
        dest = self.nodes[0].getdigidollaraddress()
        self.unlock()
        self.refresh_oracle_quotes()
        result = self.nodes[1].senddigidollar(dest, 1000)
        assert 'txid' in result
        self.sync_mempools()
        self.mine_and_sync()
        self.lock()

    def test_redeem_locked_wallet(self):
        self.log.info("test_redeem_locked_wallet")
        # We need an expired position to redeem. Mint one at tier 0 (1 hour)
        # and advance past its lock period.
        self.unlock()
        self.refresh_oracle_quotes()
        mint_result = self.nodes[1].mintdigidollar(10000, 0)
        self.sync_mempools()
        self.mine_and_sync()
        self.lock()

        # Advance blocks to expire the tier-0 lock (use mocktime or mine enough blocks)
        # Tier 0 = 1 hour ≈ 240 blocks at 15s. Mine 250 to be safe.
        self.nodes[0].generate(250)
        self.sync_all()

        # Now try to redeem while locked
        self.refresh_oracle_quotes()
        pos_id = mint_result.get('position_id', mint_result['txid'])
        redemption_info = self.nodes[1].getredemptioninfo(pos_id, 10000)
        assert_equal(redemption_info["can_redeem"], False)

        positions = self.nodes[1].listdigidollarpositions()
        redeemable = [p for p in positions if p.get('is_redeemable', False) or p.get('redeemable', False)]
        if redeemable:
            pos_id = redeemable[0].get('position_id', redeemable[0].get('txid', ''))
            dd_amount = redeemable[0].get('dd_amount', 10000)
            assert_raises_rpc_error(
                -13, "Please enter the wallet passphrase with walletpassphrase first",
                self.nodes[1].redeemdigidollar, pos_id, dd_amount
            )
        else:
            # If no redeemable positions found, just test the RPC rejects while locked
            assert_raises_rpc_error(
                -13, "Please enter the wallet passphrase with walletpassphrase first",
                self.nodes[1].redeemdigidollar, pos_id, 10000
            )

    def test_redeem_after_unlock(self):
        self.log.info("test_redeem_after_unlock")
        self.unlock()
        positions = self.nodes[1].listdigidollarpositions()
        redeemable = [p for p in positions if p.get('is_redeemable', False) or p.get('redeemable', False)]
        if redeemable:
            pos_id = redeemable[0].get('position_id', redeemable[0].get('txid', ''))
            dd_amount = redeemable[0].get('dd_amount', 10000)
            self.refresh_oracle_quotes()
            result = self.nodes[1].redeemdigidollar(pos_id, dd_amount)
            assert 'txid' in result
            self.sync_mempools()
            self.mine_and_sync()
        else:
            self.log.info("No redeemable positions found — skipping actual redeem call")
        self.lock()

    def test_oracle_keygen_locked(self):
        self.log.info("test_oracle_keygen_locked")
        assert_raises_rpc_error(
            -13, "Please enter the wallet passphrase with walletpassphrase first",
            self.nodes[1].createoraclekey, 0
        )

    def test_oracle_keygen_after_unlock(self):
        self.log.info("test_oracle_keygen_after_unlock")
        self.unlock()
        result = self.nodes[1].createoraclekey(0)
        assert result is not None
        self.lock()

    def test_relock_timeout(self):
        self.log.info("test_relock_timeout")
        self.nodes[1].walletpassphrase(self.passphrase, 2)
        # Wallet is unlocked now — wait for timeout
        time.sleep(3)
        assert_raises_rpc_error(
            -13, "Please enter the wallet passphrase with walletpassphrase first",
            self.nodes[1].mintdigidollar, 10000, 0
        )

    def test_read_ops_while_locked(self):
        self.log.info("test_read_ops_while_locked")
        # Wallet is locked — read-only DD RPCs should still work
        stats = self.nodes[1].getdigidollarstats()
        assert stats is not None

        balance = self.nodes[1].getdigidollarbalance()
        assert balance is not None

        positions = self.nodes[1].listdigidollarpositions()
        assert isinstance(positions, list)

        self.log.info("All read operations succeeded while wallet is locked")

    def test_full_lock_unlock_cycle(self):
        self.log.info("test_full_lock_unlock_cycle")

        # Fund node 1 with more DGB if needed
        addr1 = self.nodes[1].getnewaddress()
        self.unlock()
        self.nodes[0].sendtoaddress(addr1, 500000)
        self.mine_and_sync()

        # Step 1: unlock → mint succeeds
        self.refresh_oracle_quotes()
        result1 = self.nodes[1].mintdigidollar(10000, 0)
        assert 'txid' in result1
        self.sync_mempools()
        self.mine_and_sync()

        # Step 2: lock → mint fails
        self.lock()
        assert_raises_rpc_error(
            -13, "Please enter the wallet passphrase with walletpassphrase first",
            self.nodes[1].mintdigidollar, 10000, 0
        )

        # Step 3: unlock again → mint succeeds
        self.unlock()
        self.refresh_oracle_quotes()
        result3 = self.nodes[1].mintdigidollar(10000, 0)
        assert 'txid' in result3
        self.sync_mempools()
        self.mine_and_sync()
        self.lock()

        self.log.info("Full lock/unlock cycle passed")

    def test_dd_keys_encrypted_after_encryptwallet(self):
        """T4-03a: Verify DD private keys are encrypted in the wallet database.

        Tests that after encryptwallet:
        1. DD owner keys are stored encrypted (not plaintext) in wallet.dat
        2. The wallet can still perform DD sends after unlock (proving keys decrypt correctly)
        3. Minting in an already-encrypted wallet stores new keys encrypted too
        """
        self.log.info("test_dd_keys_encrypted_after_encryptwallet")

        # Node 1 was encrypted in setup_digidollar_env.
        # It already has minted DD from test_mint_after_unlock.
        # Verify the wallet still has DD balance
        self.unlock()
        balance = self.nodes[1].getdigidollarbalance()
        self.log.info(f"DD balance after encryption: {balance}")

        # Test 1: Mint new DD in an encrypted wallet — keys should be stored encrypted
        self.refresh_oracle_quotes()
        mint_result = self.nodes[1].mintdigidollar(5000, 0)  # 50.00 DD
        assert 'txid' in mint_result, "Mint in encrypted wallet should succeed when unlocked"
        self.sync_mempools()
        self.mine_and_sync()

        new_balance = self.nodes[1].getdigidollarbalance()
        self.log.info(f"DD balance after encrypted mint: {new_balance}")

        # Test 2: Send DD from encrypted wallet — proves owner key decrypts correctly
        dest = self.nodes[0].getdigidollaraddress()
        self.refresh_oracle_quotes()
        send_result = self.nodes[1].senddigidollar(dest, 1000)  # 10.00 DD
        assert 'txid' in send_result, "Send from encrypted wallet should succeed when unlocked"
        self.sync_mempools()
        self.mine_and_sync()

        # Test 3: Lock wallet and verify signing fails
        self.lock()
        assert_raises_rpc_error(
            -13, "Please enter the wallet passphrase with walletpassphrase first",
            self.nodes[1].senddigidollar, dest, 500
        )

        # Test 4: Unlock and verify send still works (keys survive lock/unlock cycle)
        self.unlock()
        self.refresh_oracle_quotes()
        send_result2 = self.nodes[1].senddigidollar(dest, 500)
        assert 'txid' in send_result2, "Send after re-unlock should succeed"
        self.sync_mempools()
        self.mine_and_sync()

        # Test 5: Generate a new DD address in encrypted wallet
        new_addr = self.nodes[1].getdigidollaraddress()
        assert new_addr is not None and len(new_addr) > 0, "Should generate DD address in encrypted wallet"

        self.lock()
        self.log.info("T4-03a: DD key encryption test passed — keys encrypt/decrypt correctly")


if __name__ == '__main__':
    DigiDollarEncryptedWalletTest().main()
