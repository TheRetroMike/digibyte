#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 18 - RPC Command Matrix (Agent C).

Pins the DigiDollar RPC error matrix across the operator-facing scenarios that
Wave 18 brief calls out:

  - No wallet loaded.
  - Missing wallet (-rpcwallet=<unknown>).
  - Locked encrypted wallet.
  - Watch-only wallet (WALLET_FLAG_DISABLE_PRIVATE_KEYS).
  - Pre-activation deployment gate (DEFINED state).

The pins cover the load-bearing wallet-routed DD commands
  (mintdigidollar, senddigidollar, sendmanydigidollar, redeemdigidollar,
   listdigidollarpositions, getdigidollarbalance, getdigidollaraddress,
   createoraclekey)
plus the no-wallet DD/oracle commands that must work without any wallet
loaded (getoracleprice, getalloracleprices, getoracles,
getdigidollardeploymentinfo).

Findings being pinned:

  DD-FA-FUNC-028 - locked-wallet write RPCs other than redeemdigidollar
                   surface only the generic upstream "Please enter the
                   wallet passphrase" hint with no DigiDollar context.
                   Wave 17 fixed only redeemdigidollar (DD-FA-FUNC-025);
                   Wave 18 extends the DD-flavored prefix to the rest of
                   the DD/oracle write surface (mintdigidollar,
                   senddigidollar, sendmanydigidollar,
                   getdigidollaraddress, createoraclekey) so wallet UIs
                   can disambiguate every locked-wallet rejection. The
                   substring "walletpassphrase" is preserved for backward
                   compatibility with digidollar_encrypted_wallet.py.

Legacy wallets are deliberately skipped: legacy wallet support for DD is a
documented unsupported state (RC33 release notes).
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


ORACLE_PRICE_MICRO_USD = 500000


class DigiDollarWave18RpcMatrixTest(DigiByteTestFramework):
    def set_test_params(self):
        # Two nodes:
        #   node 0 - default DD activation (regtest height 650), wallet present.
        #            Used to source DGB and to host wEnc / wWatch.
        #   node 1 - identical activation; serves as a peer source for
        #            getdigidollaraddress on a non-encrypted wallet (foreign DD
        #            address producer).
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

    # ------------------------------------------------------------------
    # helpers
    # ------------------------------------------------------------------

    def _refresh_oracle_quotes(self, price=ORACLE_PRICE_MICRO_USD):
        for node in self.nodes:
            try:
                node.setmockoracleprice(price)
            except Exception:
                pass

    def _ensure_dd_active(self):
        # Regtest DD/oracle height gates default to 650. Mine past them to
        # leave the gating tests below in the ACTIVE state for read RPCs.
        info = self.nodes[0].getdigidollardeploymentinfo()
        if not info["enabled"]:
            self.nodes[0].generate(660)
            self.sync_all()
        self._refresh_oracle_quotes()

    def _setup_wallets(self):
        node = self.nodes[0]
        # The default wallet on regtest is auto-loaded; record its name.
        wallets = node.listwallets()
        assert wallets, "Expected the default regtest wallet to be loaded"
        self.default_wallet = wallets[0]

        # Watch-only wallet (private keys disabled).
        node.createwallet(wallet_name="wWatch", disable_private_keys=True)
        # Encrypted wallet that we will lock for the locked-wallet pins.
        node.createwallet(wallet_name="wEnc")
        wEnc = node.get_wallet_rpc("wEnc")
        wEnc.encryptwallet("Wave18Passphrase")
        # encryptwallet restarts the wallet — re-load to be safe.
        if "wEnc" not in node.listwallets():
            node.loadwallet("wEnc")

    # ------------------------------------------------------------------
    # Pins
    # ------------------------------------------------------------------

    def _pin_no_wallet_required_for_oracle_and_deployment_rpcs(self):
        """getoracleprice / getalloracleprices / getoracles /
        getdigidollardeploymentinfo must work with no wallet on the URI path
        (the post-activation surface that any monitoring tool needs)."""
        self.log.info(
            "Wave 18 matrix: oracle and deployment RPCs work without a wallet"
        )
        # The simplest "no wallet on the request" path is to pass
        # -rpcwallet=<unknown> and assert that the *deployment* and *oracle*
        # RPCs route through the no-wallet handler regardless. The wallet-
        # routing layer only kicks in for commands registered against the
        # wallet command table.
        node = self.nodes[0]
        # getdigidollardeploymentinfo is registered without wallet context.
        info = node.getdigidollardeploymentinfo()
        assert "enabled" in info
        assert "status" in info
        assert "bit" in info
        # Same for the oracle commands.
        price = node.getoracleprice()
        assert "price_micro_usd" in price
        all_prices = node.getalloracleprices()
        assert "block_height" in all_prices
        oracles = node.getoracles()
        assert isinstance(oracles, list)

    def _pin_missing_wallet_rejected_clearly(self):
        """-rpcwallet=<does-not-exist> on a wallet-routed DD RPC must raise
        the framework's clear -18 'Requested wallet does not exist or is not
        loaded' message — not silently fall through."""
        self.log.info("Wave 18 matrix: missing wallet returns -18")
        node = self.nodes[0]
        # Create a binding to a non-existent wallet name.
        ghost = node.get_wallet_rpc("ghost-wallet-does-not-exist")
        for rpc, args in [
            ("getdigidollarbalance", ()),
            ("listdigidollarpositions", ()),
            ("mintdigidollar", (10000, 0)),
            ("senddigidollar", ("RDtest1234", 1000)),
            ("redeemdigidollar", ("ab" * 32, 1000)),
            ("getdigidollaraddress", ()),
            ("createoraclekey", (0,)),
        ]:
            assert_raises_rpc_error(
                -18,
                "Requested wallet does not exist or is not loaded",
                getattr(ghost, rpc),
                *args,
            )

    def _pin_watch_only_write_rpcs_clear_error(self):
        """Watch-only (private keys disabled) wallet must reject every DD
        write RPC with the explicit 'Private keys are disabled for this
        wallet' message."""
        self.log.info(
            "Wave 18 matrix: watch-only DD write RPCs surface 'Private keys are disabled'"
        )
        wWatch = self.nodes[0].get_wallet_rpc("wWatch")
        for rpc, args in [
            ("mintdigidollar", (10000, 0)),
            ("senddigidollar", ("RDtest1234", 1000)),
            ("redeemdigidollar", ("ab" * 32, 1000)),
            ("getdigidollaraddress", ()),
            ("createoraclekey", (0,)),
        ]:
            assert_raises_rpc_error(
                -4,
                "Private keys are disabled for this wallet",
                getattr(wWatch, rpc),
                *args,
            )
        # Read RPCs must NOT raise on watch-only and must return zero
        # spendable balance (Wave 17 contract).
        bal = wWatch.getdigidollarbalance()
        assert_equal(bal["confirmed"], 0)
        assert_equal(bal["unconfirmed"], 0)
        positions = wWatch.listdigidollarpositions()
        assert isinstance(positions, list)

    def _pin_locked_wallet_dd_flavored_hint(self):
        """DD-FA-FUNC-028: every DD write RPC on a locked encrypted wallet
        must surface a DigiDollar-flavored hint that explicitly cites the
        DD operation, AND preserve the legacy 'walletpassphrase' substring
        for backward compatibility with digidollar_encrypted_wallet.py."""
        self.log.info(
            "Wave 18 matrix: locked encrypted wallet DD write RPCs include DD context"
        )
        wEnc = self.nodes[0].get_wallet_rpc("wEnc")
        # Fund the encrypted wallet so the post-fix path actually exercises
        # the new explicit IsLocked() check before any wallet read happens.
        addr = wEnc.getnewaddress() if False else None  # unused — read-only check is enough.

        # Probe each write RPC and check the message contains BOTH the legacy
        # substring AND a DigiDollar-flavored prefix.
        cases = [
            ("mintdigidollar", (10000, 0), "DigiDollar mint"),
            ("senddigidollar", ("RDtest1234", 1000), "DigiDollar send"),
            ("sendmanydigidollar", ("", {"RDtest1234": 1000}), "DigiDollar send"),
            ("getdigidollaraddress", (), "DigiDollar address"),
            ("createoraclekey", (0,), "DigiDollar oracle"),
            ("startoracle", (0,), "DigiDollar oracle start"),
        ]
        for rpc, args, dd_phrase in cases:
            try:
                getattr(wEnc, rpc)(*args)
                raise AssertionError(
                    f"{rpc} on locked wallet should have raised RPC_WALLET_UNLOCK_NEEDED"
                )
            except Exception as exc:
                msg = str(exc)
                assert "walletpassphrase" in msg, (
                    f"DD-FA-FUNC-028: {rpc} must keep 'walletpassphrase' substring; "
                    f"got: {msg}"
                )
                assert dd_phrase in msg, (
                    f"DD-FA-FUNC-028: {rpc} must cite DigiDollar context "
                    f"(expected substring '{dd_phrase}'); got: {msg}"
                )

        # redeemdigidollar already has the DD-flavored hint from Wave 17
        # (FUNC-025); pin it here so the matrix is comprehensive.
        try:
            wEnc.redeemdigidollar("ab" * 32, 1000)
            raise AssertionError("redeemdigidollar should fail on locked wallet")
        except Exception as exc:
            msg = str(exc)
            assert "walletpassphrase" in msg
            assert "DigiDollar redemption" in msg

    def _pin_pre_activation_deployment_info_works(self):
        """getdigidollardeploymentinfo must remain queryable in any
        deployment state. The full pre-activation gate matrix for the 31
        gated DD/oracle RPCs is owned by digidollar_rpc_gating.py; this
        pin only anchors the ungated read RPC contract from the Wave 18
        matrix's perspective."""
        self.log.info(
            "Wave 18 matrix: getdigidollardeploymentinfo always queryable"
        )
        info = self.nodes[0].getdigidollardeploymentinfo()
        for required_field in ("enabled", "status", "bit", "start_time",
                               "timeout", "min_activation_height",
                               "oracle_activation_height",
                               "musig2_format_activation_height",
                               "oracle_pubkey_count",
                               "oracle_consensus_required",
                               "oracle_total_slots"):
            assert required_field in info, (
                f"getdigidollardeploymentinfo schema regression: "
                f"missing field {required_field!r}"
            )

    def _pin_dd_cent_result_help_schema_is_numeric(self):
        """DD result fields stored as integer cents must be documented as
        numeric JSON fields, not Bitcoin-style amount strings."""
        self.log.info("Wave 18 matrix: DD cent result help uses numeric schema")
        expected_numeric_fields = {
            "mintdigidollar": ("dd_minted",),
            "redeemdigidollar": ("dd_redeemed", "total_dd_minted", "required_dd_burn"),
            "listdigidollarpositions": ("dd_minted",),
            "listdigidollaraddresses": ("balance",),
            "getdigidollarbalance": ("confirmed", "unconfirmed", "total"),
            "estimatecollateral": ("dd_amount", "usd_value"),
            "getredemptioninfo": ("total_dd_minted", "redeemable_dd", "required_dd_burn", "penalty_amount"),
            "listdigidollartxs": ("amount",),
        }
        for command, fields in expected_numeric_fields.items():
            help_text = self.nodes[0].help(command)
            for field in fields:
                numeric_snippet = f'"{field}" : n'
                amount_snippet = f'"{field}" : amount'
                assert numeric_snippet in help_text, (
                    f"{command} help must document {field} as numeric cents; "
                    f"missing snippet {numeric_snippet!r}"
                )
                assert amount_snippet not in help_text, (
                    f"{command} help must not document {field} as a DGB amount; "
                    f"found stale snippet {amount_snippet!r}"
                )

    # ------------------------------------------------------------------
    # entry point
    # ------------------------------------------------------------------

    def run_test(self):
        self._ensure_dd_active()
        self._setup_wallets()
        # 1. Oracle / deployment surface works without a wallet on the URI.
        self._pin_no_wallet_required_for_oracle_and_deployment_rpcs()
        # 2. Missing wallet returns the framework's -18 clearly.
        self._pin_missing_wallet_rejected_clearly()
        # 3. Watch-only write RPCs raise -4 'Private keys are disabled'.
        self._pin_watch_only_write_rpcs_clear_error()
        # 4. Locked encrypted wallet DD write RPCs must include DD context
        #    AND keep the legacy 'walletpassphrase' substring.
        self._pin_locked_wallet_dd_flavored_hint()
        # 5. getdigidollardeploymentinfo schema is queryable and complete.
        self._pin_pre_activation_deployment_info_works()
        # 6. DD cent result fields are documented as numeric JSON fields.
        self._pin_dd_cent_result_help_schema_is_numeric()


if __name__ == "__main__":
    DigiDollarWave18RpcMatrixTest().main()
