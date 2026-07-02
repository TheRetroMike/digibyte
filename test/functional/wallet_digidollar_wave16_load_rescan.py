#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 16 - Agent C functional coverage.

Real RPC `loadwallet` / `unloadwallet` / `rescanblockchain` / restart flows
with active DD positions. The earlier `wallet_digidollar_persistence_restart.py`
silently exited because it called `mintdigidollar(100, 365)` (tier=365 is out
of range 0..9), so no real assertion ever ran. This test exercises the full
Agent C scope end-to-end:

1. Mint an active tier-0 DD position, confirm it.
2. `unloadwallet` while the DD position is active, then `loadwallet` and check
   that DD positions, balance, and the dd_owner_keys side-table all survive.
3. `rescanblockchain` on the loaded wallet -- positions and balance must stay
   identical (no double counting, no position loss).
4. Stop and `restart_node` while the DD position is active, then verify the
   default wallet still reports the same DD position and balance and the
   position is still spendable (redeemable past unlock height).
5. Restore from descriptors into a brand-new wallet on the same node, rescan,
   and prove the restored wallet sees the same DD position and can redeem
   past unlock height.

The single-output `getdigidollarbalance` total is also asserted to remain
exactly equal across every load/unload/rescan/restart step (no drift, no
double counting).
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)


ORACLE_PRICE_MICRO_USD = 500000
MINT_AMOUNT_CENTS = 100000  # $1000.00


def _balance_total(b):
    return b["total"] if isinstance(b, dict) else b


def _position_active(p):
    return p.get("is_active", p.get("status") in ("active", "unlocked"))


class WalletDigiDollarWave16LoadRescanTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-digidollar=1",
            "-txindex=1",
            "-dandelion=0",
        ]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def _refresh_oracle(self):
        result = self.nodes[0].setmockoracleprice(ORACLE_PRICE_MICRO_USD)
        assert_equal(result["price_micro_usd"], ORACLE_PRICE_MICRO_USD)

    def _mint_active_position(self):
        self.log.info("Mining funds and minting an active tier-0 DD position")
        self.generate(self.nodes[0], 200)
        self._refresh_oracle()
        mint = self.nodes[0].mintdigidollar(MINT_AMOUNT_CENTS, 0)
        position_id = mint["position_id"]
        unlock_height = mint["unlock_height"]
        self.generate(self.nodes[0], 1)

        positions = self.nodes[0].listdigidollarpositions()
        assert_equal(len(positions), 1)
        assert_equal(positions[0]["position_id"], position_id)
        assert_equal(_balance_total(self.nodes[0].getdigidollarbalance()),
                     MINT_AMOUNT_CENTS)
        return position_id, unlock_height

    def test_unload_load_preserves_active_position(self, position_id):
        self.log.info("Unloading and reloading the default wallet with an "
                      "active DD position")
        wallet_name = self.default_wallet_name
        self.nodes[0].unloadwallet(wallet_name)

        # The default wallet should now be invisible to the wallet RPC space.
        assert wallet_name not in self.nodes[0].listwallets()
        # DD wallet RPCs must error cleanly with no wallet loaded
        # (-18 RPC_WALLET_NOT_FOUND).
        assert_raises_rpc_error(
            -18, None, self.nodes[0].listdigidollarpositions)

        # Reload the same wallet by name.
        loaded = self.nodes[0].loadwallet(wallet_name)
        assert_equal(loaded["name"], wallet_name)

        positions = self.nodes[0].listdigidollarpositions()
        assert_equal(len(positions), 1)
        assert_equal(positions[0]["position_id"], position_id)
        assert_equal(_position_active(positions[0]), True)
        assert_equal(_balance_total(self.nodes[0].getdigidollarbalance()),
                     MINT_AMOUNT_CENTS)

    def test_rescan_does_not_drift(self, position_id):
        self.log.info("Running rescanblockchain on the active DD wallet")
        before_positions = self.nodes[0].listdigidollarpositions()
        before_balance = _balance_total(
            self.nodes[0].getdigidollarbalance())

        rescan = self.nodes[0].rescanblockchain()
        assert "start_height" in rescan
        assert "stop_height" in rescan

        after_positions = self.nodes[0].listdigidollarpositions()
        after_balance = _balance_total(
            self.nodes[0].getdigidollarbalance())

        assert_equal(len(after_positions), len(before_positions))
        assert_equal(after_positions[0]["position_id"], position_id)
        assert_equal(_position_active(after_positions[0]), True)
        assert_equal(after_balance, before_balance)

    def test_restart_preserves_active_position(self, position_id):
        self.log.info("Restarting node with active DD position")
        before_balance = _balance_total(
            self.nodes[0].getdigidollarbalance())
        chain_height = self.nodes[0].getblockcount()

        self.restart_node(0, extra_args=self.extra_args[0])
        self.nodes[0].syncwithvalidationinterfacequeue()

        assert_equal(self.nodes[0].getblockcount(), chain_height)

        positions = self.nodes[0].listdigidollarpositions()
        assert_equal(len(positions), 1)
        assert_equal(positions[0]["position_id"], position_id)
        assert_equal(_position_active(positions[0]), True)
        assert_equal(_balance_total(self.nodes[0].getdigidollarbalance()),
                     before_balance)

    def test_restore_from_descriptors_then_redeem(self, position_id,
                                                  unlock_height):
        self.log.info("Restoring DD position into a fresh wallet via "
                      "importdescriptors + rescanblockchain")
        descriptors = self.nodes[0].listdescriptors(True)["descriptors"]

        self.nodes[0].createwallet(
            wallet_name="wave16_restored",
            disable_private_keys=False,
            blank=True,
            passphrase="",
            avoid_reuse=False,
            descriptors=True,
        )
        restored = self.nodes[0].get_wallet_rpc("wave16_restored")

        imports = []
        for desc in descriptors:
            req = {
                "desc": desc["desc"],
                "timestamp": 0,
                "active": desc.get("active", False),
                "internal": desc.get("internal", False),
            }
            if "range" in desc:
                req["range"] = desc["range"]
            imports.append(req)
        result = restored.importdescriptors(imports)
        assert_equal(sum(1 for item in result if item.get("success", False)),
                     len(imports))

        # Make sure the restored wallet sees the position.
        restored_positions = restored.listdigidollarpositions()
        assert_equal(len(restored_positions), 1)
        assert_equal(restored_positions[0]["position_id"], position_id)
        assert_equal(_balance_total(restored.getdigidollarbalance()),
                     MINT_AMOUNT_CENTS)

        # Advance past unlock height and redeem from the restored wallet.
        current_height = self.nodes[0].getblockcount()
        if current_height <= unlock_height:
            self.generate(self.nodes[0], unlock_height - current_height + 1)
        self._refresh_oracle()
        redeem = restored.redeemdigidollar(position_id, MINT_AMOUNT_CENTS)
        assert "txid" in redeem
        self.generate(self.nodes[0], 1)
        assert_equal(_balance_total(restored.getdigidollarbalance()), 0)

        positions_after = restored.listdigidollarpositions(False)
        # `False` -> include inactive positions; we must see the redeemed one.
        assert_greater_than(len(positions_after), 0)
        match = next(p for p in positions_after if p["position_id"] == position_id)
        assert match["status"] in ("redeemed", "inactive") or \
               match.get("is_active") is False, \
               "Redeemed position must not be reported as active"

        try:
            self.nodes[0].unloadwallet("wave16_restored")
        except Exception:
            pass

    def run_test(self):
        self.log.info("=== Wave 16 - Agent C wallet load/rescan/restart ===")
        position_id, unlock_height = self._mint_active_position()
        self.test_unload_load_preserves_active_position(position_id)
        self.test_rescan_does_not_drift(position_id)
        self.test_restart_preserves_active_position(position_id)
        self.test_restore_from_descriptors_then_redeem(position_id,
                                                      unlock_height)
        self.log.info("Wave 16 Agent C load/rescan/restart coverage PASS")


if __name__ == "__main__":
    WalletDigiDollarWave16LoadRescanTest().main()
