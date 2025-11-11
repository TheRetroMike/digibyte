#!/usr/bin/env python3
# Copyright (c) 2017-2021 The DigiByte Core developers
# Copyright (c) 2017-2022 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test HD Wallet keypool restore function.

Two nodes. Node1 is under test. Node0 is providing transactions and generating blocks.

- Start node1, shutdown and backup wallet.
- Generate 110 keys (enough to drain the keypool). Store key 90 (in the initial keypool) and key 110 (beyond the initial keypool). Send funds to key 90 and key 110.
- Stop node1, clear the datadir, move wallet file back into the datadir and restart node1.
- connect node1 to node0. Verify that they sync and node1 receives its funds."""
import os
import shutil

from test_framework.blocktools import COINBASE_MATURITY, COINBASE_MATURITY_2
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
)


class KeypoolRestoreTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [[], ['-keypool=100'], ['-keypool=100'], ['-keypool=100']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        wallet_path = os.path.join(self.nodes[1].datadir, self.chain, "wallets", self.default_wallet_name, self.wallet_data_filename)
        wallet_backup_path = os.path.join(self.nodes[1].datadir, "wallet.bak")
        # DigiByte: Use COINBASE_MATURITY_2 for longer maturity test
        self.generate(self.nodes[0], COINBASE_MATURITY_2 + 1)

        self.log.info("Make backup of wallet")
        self.stop_node(1)
        shutil.copyfile(wallet_path, wallet_backup_path)
        self.start_node(1, self.extra_args[1])
        self.connect_nodes(0, 1)
        self.connect_nodes(0, 2)
        self.connect_nodes(0, 3)

        for i, output_type in enumerate(["legacy", "p2sh-segwit", "bech32"]):

            self.log.info("Generate keys for wallet with address type: {}".format(output_type))
            idx = i+1
            for _ in range(90):
                addr_oldpool = self.nodes[idx].getnewaddress(address_type=output_type)
            for _ in range(20):
                addr_extpool = self.nodes[idx].getnewaddress(address_type=output_type)

            # Make sure we're creating the outputs we expect
            address_details = self.nodes[idx].validateaddress(addr_extpool)
            if i == 0:
                assert not address_details["isscript"] and not address_details["iswitness"]
            elif i == 1:
                assert address_details["isscript"] and not address_details["iswitness"]
            else:
                assert not address_details["isscript"] and address_details["iswitness"]

            self.log.info("Send funds to wallet with address type: {}".format(output_type))
            self.nodes[0].sendtoaddress(addr_oldpool, 10)
            self.nodes[0].sendtoaddress(addr_extpool, 5)
            self.generate(self.nodes[0], 1)
            self.sync_blocks()

            self.log.info("Restart node with wallet backup for wallet with address type: {}".format(output_type))
            self.stop_node(idx)
            shutil.rmtree(os.path.join(self.nodes[idx].datadir, self.chain, 'blocks'))
            shutil.rmtree(os.path.join(self.nodes[idx].datadir, self.chain, 'chainstate'))
            shutil.copyfile(
                os.path.join(self.nodes[idx].datadir, "wallet.bak"),
                os.path.join(self.nodes[idx].datadir, self.chain, "wallets", self.default_wallet_name, self.wallet_data_filename),
            )
            self.start_node(idx, self.extra_args[idx])
            self.connect_nodes(0, idx)
            self.sync_blocks()

            self.log.info("Verify keypool is restored and balance is correct")
            assert_equal(self.nodes[idx].getbalance(), 15)
            assert_equal(self.nodes[idx].listtransactions()[0]['category'], "receive")
            # Check that we have marked all keys up to the used keypool key as used
            if output_type == 'legacy':
                assert_equal(self.nodes[idx].getaddressinfo(self.nodes[idx].getnewaddress(address_type=output_type))['hdkeypath'], "m/0'/0'/110'")
            elif output_type == 'p2sh-segwit':
                assert_equal(self.nodes[idx].getaddressinfo(self.nodes[idx].getnewaddress(address_type=output_type))['hdkeypath'], "m/0'/0'/110'")
            elif output_type == 'bech32':
                assert_equal(self.nodes[idx].getaddressinfo(self.nodes[idx].getnewaddress(address_type=output_type))['hdkeypath'], "m/0'/0'/110'")

        self.log.info("Test restoring wallet with missing transactions")
        
        # Create a new wallet for this test
        self.nodes[1].createwallet(wallet_name="missing_tx_wallet")
        missing_wallet = self.nodes[1].get_wallet_rpc("missing_tx_wallet")
        
        # Generate some addresses and receive funds
        addresses = []
        for _ in range(5):
            addresses.append(missing_wallet.getnewaddress())
        
        for addr in addresses:
            self.nodes[0].sendtoaddress(addr, 1)
        self.generate(self.nodes[0], 1)
        self.sync_blocks()
        
        original_balance = missing_wallet.getbalance()
        assert_equal(original_balance, 5)
        
        # Backup the wallet
        backup_path = os.path.join(self.nodes[1].datadir, "missing_tx_wallet.bak")
        missing_wallet.backupwallet(backup_path)
        
        # Send more transactions
        for _ in range(3):
            self.nodes[0].sendtoaddress(missing_wallet.getnewaddress(), 2)
        self.generate(self.nodes[0], 1)
        self.sync_blocks()
        
        new_balance = missing_wallet.getbalance()
        assert_equal(new_balance, 11)  # 5 + 3*2
        
        # Unload wallet and restore from backup
        self.nodes[1].unloadwallet("missing_tx_wallet")
        
        # Remove the current wallet file
        wallet_file = os.path.join(self.nodes[1].datadir, self.chain, "wallets", "missing_tx_wallet", self.wallet_data_filename)
        os.remove(wallet_file)
        
        # Restore from backup
        shutil.copyfile(backup_path, wallet_file)
        
        # Load the restored wallet
        self.nodes[1].loadwallet("missing_tx_wallet")
        restored_wallet = self.nodes[1].get_wallet_rpc("missing_tx_wallet")
        
        # The restored wallet should have all transactions after rescan
        assert_equal(restored_wallet.getbalance(), 11)
        
        self.log.info("Test keypool exhaustion and automatic topup")
        
        # Create a wallet with small keypool
        self.nodes[2].createwallet(wallet_name="small_keypool", blank=False)
        small_wallet = self.nodes[2].get_wallet_rpc("small_keypool")
        
        # Set keypool size to 10
        small_wallet.keypoolrefill(10)
        
        # Generate 15 addresses (should trigger automatic topup)
        addresses = []
        for i in range(15):
            addresses.append(small_wallet.getnewaddress())
            self.log.debug(f"Generated address {i+1}: {addresses[-1]}")
        
        # Verify all addresses are valid
        for addr in addresses:
            info = small_wallet.getaddressinfo(addr)
            assert info['ismine']
            assert info['iswatchonly'] == False
        
        # Check keypool size was automatically topped up
        wallet_info = small_wallet.getwalletinfo()
        assert wallet_info['keypoolsize'] > 0
        
        self.log.info("Test importing keys doesn't interfere with keypool")
        
        # Generate a private key to import
        import_wallet = self.nodes[3].get_wallet_rpc(self.default_wallet_name)
        new_addr = self.nodes[0].getnewaddress()
        priv_key = self.nodes[0].dumpprivkey(new_addr)
        
        # Import the private key
        import_wallet.importprivkey(priv_key, "imported_key", False)
        
        # Verify keypool is still intact
        keypool_before = import_wallet.getwalletinfo()['keypoolsize']
        
        # Generate some addresses
        for _ in range(5):
            import_wallet.getnewaddress()
        
        keypool_after = import_wallet.getwalletinfo()['keypoolsize']
        assert_equal(keypool_before - keypool_after, 5)
        
        self.log.info("All keypool restore tests completed successfully!")


if __name__ == '__main__':
    KeypoolRestoreTest().main()