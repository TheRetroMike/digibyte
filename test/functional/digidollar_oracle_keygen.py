#!/usr/bin/env python3
"""Test DigiDollar oracle key generation in descriptor wallets.

Tests the createoraclekey RPC command which generates oracle keypairs
and stores them inside a descriptor wallet for use with startoracle.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
import os
import re
import tempfile


class DigiDollarOracleKeygenTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        # Generate blocks past coinbase maturity
        self.log.info("Generating initial blocks...")
        node.generate(110)

        self.log.info("Creating descriptor wallet for oracle keys...")
        node.createwallet("oracle_test")
        wallet = node.get_wallet_rpc("oracle_test")

        # --- DD-RH-028: listoracle no-running path must satisfy RPC schema ---
        self.log.info("Test: listoracle should return a clean no-oracle-running result")
        list_result = node.listoracle()
        assert_equal(list_result["running"], False)
        assert "No oracle is running" in list_result["message"]

        # --- DD-RH-026: createoraclekey must reject disabled-private-key wallets ---
        self.log.info("Test: createoraclekey should reject disabled-private-key wallets")
        node.createwallet(wallet_name="oracle_watchonly", disable_private_keys=True)
        watch_wallet = node.get_wallet_rpc("oracle_watchonly")
        assert_equal(watch_wallet.getwalletinfo()["private_keys_enabled"], False)
        assert_raises_rpc_error(-4, "Private keys are disabled", watch_wallet.createoraclekey, 0)
        assert_raises_rpc_error(-4, "Private keys are disabled", watch_wallet.importoracleprivkey, 0, "00" * 32)
        assert_raises_rpc_error(-4, "Private keys are disabled", watch_wallet.exportoracleprivkey, 0)

        # --- Test 1: createoraclekey 0 succeeds ---
        self.log.info("Test: createoraclekey 0 should succeed")
        result = wallet.createoraclekey(0)
        assert_equal(result["oracle_id"], 0)
        assert_equal(result["stored_in_wallet"], True)

        pubkey = result["pubkey"]
        pubkey_xonly = result["pubkey_xonly"]

        # --- Test 2: pubkey format (66 hex chars, starts with 02 or 03) ---
        self.log.info("Test: pubkey format validation")
        assert_equal(len(pubkey), 66)
        assert pubkey[:2] in ("02", "03"), f"pubkey must start with 02 or 03, got {pubkey[:2]}"
        assert re.fullmatch(r'[0-9a-f]{66}', pubkey), "pubkey must be lowercase hex"

        # --- Test 3: pubkey_xonly format (64 hex chars) ---
        self.log.info("Test: pubkey_xonly format validation")
        assert_equal(len(pubkey_xonly), 64)
        assert re.fullmatch(r'[0-9a-f]{64}', pubkey_xonly), "pubkey_xonly must be lowercase hex"

        # --- Test 4: pubkey_xonly == pubkey without prefix ---
        self.log.info("Test: pubkey_xonly equals pubkey without 02/03 prefix")
        assert_equal(pubkey_xonly, pubkey[2:])

        # --- Test 5: duplicate createoraclekey 0 should fail ---
        self.log.info("Test: createoraclekey 0 again should fail (key exists)")
        assert_raises_rpc_error(None, "already exists", wallet.createoraclekey, 0)

        # --- Test 5b: export/import oracle private key for wallet recovery ---
        self.log.info("Test: exportoracleprivkey/importoracleprivkey should round-trip oracle 0")
        exported = wallet.exportoracleprivkey(0)
        assert_equal(exported["oracle_id"], 0)
        assert_equal(exported["pubkey"], pubkey)
        assert_equal(exported["pubkey_xonly"], pubkey_xonly)
        assert_equal(len(exported["private_key"]), 64)
        assert re.fullmatch(r'[0-9a-f]{64}', exported["private_key"])
        assert_equal(exported["wallet_name"], "oracle_test")

        node.createwallet("oracle_imported")
        import_wallet = node.get_wallet_rpc("oracle_imported")
        imported = import_wallet.importoracleprivkey(0, exported["private_key"])
        assert_equal(imported["oracle_id"], 0)
        assert_equal(imported["stored_in_wallet"], True)
        assert_equal(imported["replaced"], False)
        assert_equal(imported["pubkey"], pubkey)
        assert_equal(imported["pubkey_xonly"], pubkey_xonly)
        assert_equal(imported["wallet_name"], "oracle_imported")

        imported_export = import_wallet.exportoracleprivkey(0)
        assert_equal(imported_export["private_key"], exported["private_key"])
        assert_equal(imported_export["pubkey"], pubkey)
        assert_raises_rpc_error(-4, "already has an oracle key", import_wallet.importoracleprivkey, 0, exported["private_key"])
        replaced = import_wallet.importoracleprivkey(0, exported["private_key"], True)
        assert_equal(replaced["replaced"], True)
        assert_equal(replaced["pubkey"], pubkey)

        # --- Test 6: createoraclekey 1 succeeds with different key ---
        self.log.info("Test: createoraclekey 1 should succeed with different pubkey")
        result1 = wallet.createoraclekey(1)
        assert_equal(result1["oracle_id"], 1)
        assert_equal(result1["stored_in_wallet"], True)
        assert result1["pubkey"] != pubkey, "oracle 1 should have different pubkey than oracle 0"

        # --- DD-RH: restored wallet should still expose configured oracle keys ---
        self.log.info("Test: wallet-configured oracle key should be visible before startoracle")
        pubkey_result = wallet.getoraclepubkey(0)
        assert_equal(pubkey_result["oracle_id"], 0)
        assert_equal(pubkey_result["pubkey_full"], pubkey)
        assert_equal(pubkey_result["pubkey"], pubkey_xonly)
        assert_equal(pubkey_result["is_running"], False)
        assert_equal(pubkey_result["configured_in_wallet"], True)
        assert_equal(pubkey_result["source"], "wallet")
        assert_equal(pubkey_result["wallet_name"], "oracle_test")
        assert "not running" in pubkey_result["message"]
        assert "private" not in pubkey_result

        list_result = wallet.listoracle()
        assert_equal(list_result["running"], False)
        assert_equal(list_result["configured"], True)
        assert_equal(list_result["oracle_id"], 0)
        assert_equal(list_result["pubkey"], pubkey)
        assert_equal(list_result["wallet_name"], "oracle_test")
        assert "not running" in list_result["message"]

        self.log.info("Test: wrong selected wallet should not claim another wallet's oracle key")
        node.createwallet("oracle_empty")
        empty_wallet = node.get_wallet_rpc("oracle_empty")
        assert_raises_rpc_error(-8, "selected wallet 'oracle_empty' has no stored oracle key for oracle ID 0", empty_wallet.getoraclepubkey, 0)
        empty_list = empty_wallet.listoracle()
        assert_equal(empty_list["running"], False)
        assert_equal(empty_list["configured"], False)
        assert_equal(empty_list["wallet_name"], "oracle_empty")
        assert "no stored oracle key for oracle ID 0" in empty_list["message"]
        wrong_start = empty_wallet.startoracle(0)
        assert_equal(wrong_start["success"], False)
        assert "selected wallet 'oracle_empty' has no stored oracle key for oracle ID 0" in wrong_start["message"]
        assert "createoraclekey" not in wrong_start["message"].lower()

        self.log.info("Test: multiple loaded wallets without wallet selection should be clear")
        assert_raises_rpc_error(-19, "Wallet file not specified", node.startoracle, 0)

        self.log.info("Test: oracle key persists across backupwallet/restorewallet")
        with tempfile.TemporaryDirectory() as temp_dir:
            backup_file = os.path.join(temp_dir, "oracle_test_backup.dat")
            wallet.backupwallet(backup_file)
            node.restorewallet("oracle_restored", backup_file)
            restored_wallet = node.get_wallet_rpc("oracle_restored")

            assert_raises_rpc_error(None, "already exists", restored_wallet.createoraclekey, 0)

            restored_pubkey = restored_wallet.getoraclepubkey(0)
            assert_equal(restored_pubkey["oracle_id"], 0)
            assert_equal(restored_pubkey["pubkey_full"], pubkey)
            assert_equal(restored_pubkey["pubkey"], pubkey_xonly)
            assert_equal(restored_pubkey["configured_in_wallet"], True)
            assert restored_pubkey["source"] in ("wallet", "running_oracle")
            assert_equal(restored_pubkey["wallet_name"], "oracle_restored")

            restored_list = restored_wallet.listoracle()
            assert_equal(restored_list["running"], False)
            assert_equal(restored_list["configured"], True)
            assert_equal(restored_list["oracle_id"], 0)
            assert_equal(restored_list["pubkey"], pubkey)
            assert_equal(restored_list["wallet_name"], "oracle_restored")

        self.log.info("Test: encrypted restored wallet exposes public key while locked")
        node.createwallet("encrypted_oracle")
        encrypted_wallet = node.get_wallet_rpc("encrypted_oracle")
        encrypted_wallet.encryptwallet("oracle-passphrase")
        encrypted_wallet.walletpassphrase("oracle-passphrase", 600)
        encrypted_result = encrypted_wallet.createoraclekey(2)
        encrypted_pubkey = encrypted_result["pubkey"]
        encrypted_xonly = encrypted_result["pubkey_xonly"]
        with tempfile.TemporaryDirectory() as temp_dir:
            encrypted_backup = os.path.join(temp_dir, "encrypted_oracle_backup.dat")
            encrypted_wallet.backupwallet(encrypted_backup)
            node.restorewallet("encrypted_oracle_restored", encrypted_backup)
            encrypted_restored = node.get_wallet_rpc("encrypted_oracle_restored")
            encrypted_restored.walletlock()

            locked_pubkey = encrypted_restored.getoraclepubkey(2)
            assert_equal(locked_pubkey["oracle_id"], 2)
            assert_equal(locked_pubkey["pubkey_full"], encrypted_pubkey)
            assert_equal(locked_pubkey["pubkey"], encrypted_xonly)
            assert_equal(locked_pubkey["configured_in_wallet"], True)
            assert_equal(locked_pubkey["source"], "wallet")
            assert_equal(locked_pubkey["wallet_name"], "encrypted_oracle_restored")
            assert "private" not in locked_pubkey

            assert_raises_rpc_error(-13, "walletpassphrase", encrypted_restored.startoracle, 2)
            encrypted_restored.walletpassphrase("oracle-passphrase", 600)
            unlocked_start = encrypted_restored.startoracle(2)
            assert_equal(unlocked_start["oracle_id"], 2)
            assert_equal(unlocked_start["success"], False)
            assert "not authorized" in unlocked_start["message"].lower() or "mismatch" in unlocked_start["message"].lower()
            assert "createoraclekey" not in unlocked_start["message"].lower()

        self.log.info("Test: encrypted wallet export/import requires unlock")
        node.createwallet("encrypted_oracle_import")
        encrypted_import = node.get_wallet_rpc("encrypted_oracle_import")
        encrypted_import.encryptwallet("oracle-import-passphrase")
        assert_raises_rpc_error(-13, "walletpassphrase", encrypted_import.importoracleprivkey, 3, exported["private_key"])
        encrypted_import.walletpassphrase("oracle-import-passphrase", 600)
        encrypted_import_result = encrypted_import.importoracleprivkey(3, exported["private_key"])
        assert_equal(encrypted_import_result["oracle_id"], 3)
        assert_equal(encrypted_import_result["stored_in_wallet"], True)
        encrypted_import.walletlock()
        assert_raises_rpc_error(-13, "walletpassphrase", encrypted_import.exportoracleprivkey, 3)
        encrypted_import.walletpassphrase("oracle-import-passphrase", 600)
        encrypted_import_export = encrypted_import.exportoracleprivkey(3)
        assert_equal(encrypted_import_export["private_key"], exported["private_key"])

        # --- Test 7: createoraclekey 35 should fail (max oracle_id is 34) ---
        self.log.info("Test: createoraclekey 35 should fail (invalid oracle_id)")
        assert_raises_rpc_error(None, None, wallet.createoraclekey, 35)

        # --- DD-FA-FUNC-029: createoraclekey parameter validation parity ---
        # createoraclekey must reject negative IDs with a signed-int error
        # message matching the other oracle CRUD RPCs (startoracle /
        # stoporacle / getoraclepubkey). Previously a negative input was cast
        # to uint32_t and surfaced as the unsigned wrap-around (e.g. -1 ->
        # 4294967295), which still rejected the request but with a
        # confusing diagnostic.
        self.log.info("Test: createoraclekey -1 should fail with signed -1 in error message")
        assert_raises_rpc_error(-8, "Invalid oracle ID -1", wallet.createoraclekey, -1)

        # createoraclekey must also refuse IDs that have no slot in the
        # active chainparams oracle roster (regtest publishes only 7
        # oracle slots: 0..6). Without this check, the wallet would
        # persist an unusable private key for slot 7..29 that startoracle
        # later refuses with "Oracle ID N not found in chain parameters".
        self.log.info("Test: createoraclekey 7 should fail on regtest (only 7 oracle slots)")
        assert_raises_rpc_error(-8, "not found in chain parameters", wallet.createoraclekey, 7)

        self.log.info("Test: stoporacle 7 should fail on regtest (only 7 oracle slots)")
        assert_raises_rpc_error(-8, "not found in chain parameters", node.stoporacle, 7)

        self.log.info("Test: getoraclepubkey 7 should fail on regtest (only 7 oracle slots)")
        assert_raises_rpc_error(-8, "not found in chain parameters", node.getoraclepubkey, 7)

        # --- Test 8: startoracle 0 without private_key loads from wallet ---
        # On regtest, chainparams oracle keys are test keys, so the wallet-generated
        # key won't match. We expect a pubkey mismatch error.
        self.log.info("Test: startoracle 0 from wallet should fail with pubkey mismatch on regtest")
        start_result = wallet.startoracle(0)
        self.log.info(f"startoracle 0 returned: {start_result}")
        assert_equal(start_result["oracle_id"], 0)
        assert_equal(start_result["success"], False)
        assert_equal(start_result["status"], "stopped")
        assert "not authorized" in start_result["message"].lower() or "mismatch" in start_result["message"].lower()
        assert "createoraclekey" not in start_result["message"].lower()

        # --- Test 9: Key persistence across wallet unload/reload ---
        self.log.info("Test: oracle key persists across wallet unload/reload")
        wallet.unloadwallet()
        node.loadwallet("oracle_test")
        wallet = node.get_wallet_rpc("oracle_test")

        # Creating key for oracle 0 again should fail (proves key persisted)
        assert_raises_rpc_error(None, "already exists", wallet.createoraclekey, 0)

        # startoracle 0 should still attempt to load the key from wallet
        # (same pubkey mismatch error proves key was loaded from wallet)
        start_result = wallet.startoracle(0)
        self.log.info(f"startoracle 0 after reload returned: {start_result}")
        assert_equal(start_result["oracle_id"], 0)
        assert_equal(start_result["success"], False)
        assert_equal(start_result["status"], "stopped")
        assert "not authorized" in start_result["message"].lower() or "mismatch" in start_result["message"].lower()
        assert "createoraclekey" not in start_result["message"].lower()

        self.log.info("All oracle keygen tests passed!")


if __name__ == '__main__':
    DigiDollarOracleKeygenTest().main()
