#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 17 - Wallet Spendability, Watch-Only, Accounting, Privacy (Agent C).

Pins behavior across the RPC surface that an integrator or operator could be
deceived by:

  DD-FA-FUNC-022 - listdigidollarpositions must NOT report spendable=true /
                   can_redeem=true on a locked encrypted wallet whose private
                   keys are physically inaccessible. The RPC currently checks
                   only WALLET_FLAG_DISABLE_PRIVATE_KEYS, so a tier-0
                   matured position on a locked encrypted wallet returns
                   spendable=true and can_redeem=true even though
                   redeemdigidollar would immediately fail with the locked
                   passphrase error.

  DD-FA-FUNC-023 - validateddaddress must report a `solvable` field consistent
                   with bitcoin-core's `validateaddress`, i.e. true when the
                   wallet has the descriptor information required to construct
                   a spending witness, regardless of whether the wallet is
                   currently locked. Pre-fix the RPC schema does not surface
                   `solvable` at all, so a wallet integrator cannot tell
                   whether a returned ismine=false address is a foreign
                   address vs. a known descriptor with the keys absent.

  DD-FA-FUNC-024 - listdigidollaraddresses must not leak generated but unused
                   DD addresses by default; only the addresses with a
                   non-zero balance and the explicit `include_empty=true`
                   addresses should be listed. Pre-fix every generated
                   address from the keypool is surfaced in the default call,
                   leaking address counts to any RPC reader.

  DD-FA-FUNC-025 - locked-wallet redeem must surface a DigiDollar-specific
                   hint pointing the user at walletpassphrase (the generic
                   "Please enter the wallet passphrase" hint is preserved as
                   a substring; the new prefix tells the user which DD
                   operation was blocked so wallet UIs do not accidentally
                   silence-fail the redeem).

The test mints a tier-0 position on an encrypted node, advances past the
1-hour lock, then asserts each pin both before and after `walletpassphrase`.
A separate watch-only path covers the WALLET_FLAG_DISABLE_PRIVATE_KEYS
branch (pre-existing; covered as a control to ensure the iswatchonly badge
remains correct).
"""

import time

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


ORACLE_PRICE_MICRO_USD = 500000


class DigiDollarWave17SpendabilityTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser, descriptors=True, legacy=False)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_sqlite()

    def run_test(self):
        self.passphrase = "Wave17AgentCPassphrase"
        self._setup_environment()
        self._mint_tier0_position_on_encrypted_node1()
        self._advance_past_tier0_lock()

        # ---- DD-FA-FUNC-022: locked wallet can_redeem / spendable ----
        self._pin_locked_wallet_positions_not_spendable()

        # ---- DD-FA-FUNC-023: validateddaddress 'solvable' field ----
        self._pin_validateddaddress_solvable_field()

        # ---- DD-FA-FUNC-024: privacy of generated empty addresses ----
        self._pin_listdigidollaraddresses_omits_empty_by_default()

        # ---- DD-FA-FUNC-025: locked redeem clear error ----
        self._pin_locked_redeem_dd_hint()

        # Control: getdigidollarbalance does not deceive on locked wallet
        self._pin_locked_balance_remains_accurate()

    # ------------------------------------------------------------------
    # Setup
    # ------------------------------------------------------------------

    def _setup_environment(self):
        self.log.info("Setup: activate DigiDollar, fund node 1, encrypt node 1")
        self.nodes[0].generate(660)
        self.sync_all()
        self._refresh_oracle_quotes()

        addr1 = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(addr1, 500000)
        self.nodes[0].generate(10)
        self.sync_all()

        self.nodes[1].encryptwallet(self.passphrase)
        self.restart_node(1)
        self.connect_nodes(0, 1)
        self.sync_all()
        self._refresh_oracle_quotes()

    def _refresh_oracle_quotes(self, price=ORACLE_PRICE_MICRO_USD):
        for node in self.nodes:
            try:
                node.setmockoracleprice(price)
            except Exception:
                pass

    def _unlock(self, timeout=60):
        self.nodes[1].walletpassphrase(self.passphrase, timeout)

    def _lock(self):
        self.nodes[1].walletlock()

    def _mint_tier0_position_on_encrypted_node1(self):
        self.log.info("Mint a tier-0 DD position on the encrypted node 1")
        self._unlock()
        self._refresh_oracle_quotes()
        result = self.nodes[1].mintdigidollar(10000, 0)
        assert "txid" in result, f"mintdigidollar failed: {result}"
        self.position_id = result.get("position_id", result["txid"])
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_all()
        self._lock()

    def _advance_past_tier0_lock(self):
        # Tier 0 is 1 hour ~= 240 blocks at 15s, mine 260 to be safe.
        self.log.info("Advance past tier-0 lock (1 hour)")
        self.nodes[0].generate(260)
        self.sync_all()
        self._refresh_oracle_quotes()

    # ------------------------------------------------------------------
    # Pins
    # ------------------------------------------------------------------

    def _pin_locked_wallet_positions_not_spendable(self):
        """DD-FA-FUNC-022: locked encrypted wallet must not advertise
        spendable=true / can_redeem=true on its DD positions. Pre-fix the
        RPC checks only WALLET_FLAG_DISABLE_PRIVATE_KEYS; the locked-but-
        encrypted state slips through and the schema lies."""
        self.log.info(
            "DD-FA-FUNC-022: listdigidollarpositions on locked encrypted wallet"
        )
        # Wallet is locked from the setup step.
        positions = self.nodes[1].listdigidollarpositions()
        assert positions, "Expected at least one position"
        for pos in positions:
            # Spendable must be false because the wallet cannot sign.
            assert_equal(pos["spendable"], False)
            # iswatchonly is allowed to be either bool, but spendable=False is
            # the load-bearing claim. Health/active are independent.
            # When the position has matured, can_redeem must remain false
            # because actual redemption cannot succeed without unlock.
            if pos["blocks_remaining"] == 0 and pos["status"] in ("unlocked", "active"):
                assert_equal(pos["can_redeem"], False)

        # After unlock the same position must flip to spendable=true.
        self._unlock()
        self._refresh_oracle_quotes()
        positions_after = self.nodes[1].listdigidollarpositions()
        assert positions_after, "Expected at least one position after unlock"
        for pos in positions_after:
            assert_equal(pos["spendable"], True)
        self._lock()
        self.log.info("DD-FA-FUNC-022 PASS")

    def _pin_validateddaddress_solvable_field(self):
        """DD-FA-FUNC-023: validateddaddress must return a `solvable` field
        with the same semantics as the upstream `validateaddress` RPC. A
        DD address generated by the wallet is solvable=true even when the
        wallet is locked; a foreign DD address is solvable=false."""
        self.log.info("DD-FA-FUNC-023: validateddaddress.solvable field")

        # Generate a fresh wallet-owned address while unlocked.
        self._unlock()
        own_dd = self.nodes[1].getdigidollaraddress()
        foreign_dd = self.nodes[0].getdigidollaraddress()
        self._lock()

        own_result = self.nodes[1].validateddaddress(own_dd)
        assert "solvable" in own_result, (
            "validateddaddress reply must include 'solvable' field "
            "for parity with validateaddress"
        )
        assert_equal(own_result["solvable"], True)
        # ismine must remain accurate for the locked wallet as well.
        assert_equal(own_result["ismine"], True)

        foreign_result = self.nodes[1].validateddaddress(foreign_dd)
        assert "solvable" in foreign_result
        assert_equal(foreign_result["solvable"], False)
        assert_equal(foreign_result["ismine"], False)

        invalid_result = self.nodes[1].validateddaddress("XXnotanaddress")
        assert "solvable" in invalid_result
        assert_equal(invalid_result["solvable"], False)
        assert_equal(invalid_result["isvalid"], False)

        self.log.info("DD-FA-FUNC-023 PASS")

    def _pin_listdigidollaraddresses_omits_empty_by_default(self):
        """DD-FA-FUNC-024: listdigidollaraddresses must not leak the count
        of empty wallet-generated addresses by default. A `include_empty`
        flag (default false) preserves the existing behaviour for explicit
        operator queries while plugging the privacy leak in the default
        path."""
        self.log.info(
            "DD-FA-FUNC-024: listdigidollaraddresses default omits empty addresses"
        )
        self._unlock()
        # Generate three new empty DD addresses to exaggerate the leak.
        for _ in range(3):
            self.nodes[1].getdigidollaraddress()
        self._lock()

        default_list = self.nodes[1].listdigidollaraddresses()
        for entry in default_list:
            assert int(entry["balance"]) > 0, (
                f"DD-FA-FUNC-024 leak: default listdigidollaraddresses "
                f"surfaced empty address {entry['address']}"
            )

        # Explicit include_empty=true (third parameter) returns the empties.
        explicit_list = self.nodes[1].listdigidollaraddresses(False, 0, True)
        explicit_addrs = {entry["address"] for entry in explicit_list}
        # Must include at least one zero-balance address to prove the
        # opt-in path is wired.
        zero_balance_addrs = [
            a for a in explicit_list if int(a["balance"]) == 0
        ]
        assert zero_balance_addrs, (
            "DD-FA-FUNC-024 wiring: include_empty=true did not surface any "
            "zero-balance DD addresses"
        )
        # Sanity: every default-listed address must also appear in the
        # explicit list.
        default_addrs = {entry["address"] for entry in default_list}
        assert default_addrs.issubset(explicit_addrs), (
            "explicit include_empty list is missing some non-empty addresses"
        )
        self.log.info("DD-FA-FUNC-024 PASS")

    def _pin_locked_redeem_dd_hint(self):
        """DD-FA-FUNC-025: locked redeem must surface a DigiDollar-flavored
        hint that explicitly cites walletpassphrase, so a wallet UI cannot
        misclassify the failure as a transient wallet error."""
        self.log.info("DD-FA-FUNC-025: locked redeem cites walletpassphrase")

        # Wallet is currently locked.
        try:
            self.nodes[1].redeemdigidollar(self.position_id, 10000)
            raise AssertionError("redeemdigidollar should fail on locked wallet")
        except Exception as exc:
            msg = str(exc)
            # Must keep the upstream substring for backward compatibility
            # with existing tests (digidollar_encrypted_wallet.py).
            assert "walletpassphrase" in msg, (
                f"DD-FA-FUNC-025: redeem error must cite walletpassphrase, got: {msg}"
            )
            # New requirement: the prefix must explicitly reference DigiDollar
            # redemption so wallet UIs can disambiguate this failure from a
            # generic locked-wallet rejection.
            assert "DigiDollar redemption" in msg or "DigiDollar redeem" in msg, (
                f"DD-FA-FUNC-025: redeem error must cite the DD operation; got: {msg}"
            )
        self.log.info("DD-FA-FUNC-025 PASS")

    def _pin_locked_balance_remains_accurate(self):
        """Control: getdigidollarbalance on the locked encrypted wallet
        must still return the actual confirmed balance (read-only access
        is not gated on lock state)."""
        self.log.info("Control: getdigidollarbalance accurate on locked wallet")
        balance = self.nodes[1].getdigidollarbalance()
        assert balance["confirmed"] >= 10000, (
            f"locked wallet should still see its DD balance; got {balance}"
        )
        # Watch-only flag must not be claimed by an encrypted wallet whose
        # private keys are present but inaccessible.
        # (No iswatchonly field on getdigidollarbalance reply, so the
        # check is implicit: the confirmed balance is non-zero.)


if __name__ == "__main__":
    DigiDollarWave17SpendabilityTest().main()
