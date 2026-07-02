#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Wave 10 (Agent C): assert getdigidollardeploymentinfo surfaces the
current MuSig2 signing session state so operators can diagnose stuck
sessions, sub-quorum stalls, or post-restart liveness.

The orchestrator's `m_signing_sessions` map is private. Without an RPC
field, an operator running a 9-of-17 oracle network has no way to tell:

  - is the current epoch's session in CREATED / NONCES_COLLECTING /
    NONCES_COMPLETE / SIGNING / COMPLETE / FAILED?
  - how many nonces and partial signatures has the orchestrator
    received from peers so far?

This test pins that `getdigidollardeploymentinfo` returns a
`musig2_session` object containing at minimum:

  - `epoch`              — current epoch (computed from tip height)
  - `state`              — string: created / nonces_collecting /
                           nonces_complete / signing / complete /
                           failed / none
  - `nonce_count`        — number of pubnonces collected
  - `partial_sig_count`  — number of partial sigs collected

The presence of this field is the contract; exact transitions during
the session lifecycle are covered by the C++ orchestration tests.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


VALID_STATES = {
    "none",
    "created",
    "nonces_collecting",
    "nonces_complete",
    "signing",
    "complete",
    "failed",
}


class MuSig2SessionStatusTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Activate DigiDollar/oracle on regtest")
        self.generate(node, 650)

        deployment = node.getdigidollardeploymentinfo()
        assert "musig2_session" in deployment, \
            "getdigidollardeploymentinfo missing 'musig2_session' field — operator cannot diagnose stuck MuSig2 sessions"

        session = deployment["musig2_session"]
        for key in ("epoch", "state", "nonce_count", "partial_sig_count"):
            assert key in session, "musig2_session missing required field '%s'" % key

        assert isinstance(session["epoch"], int), \
            "musig2_session.epoch must be int, got %s" % type(session["epoch"])
        assert isinstance(session["state"], str), \
            "musig2_session.state must be string, got %s" % type(session["state"])
        assert session["state"] in VALID_STATES, \
            "musig2_session.state '%s' not in valid set %s" % (session["state"], VALID_STATES)
        assert isinstance(session["nonce_count"], int), \
            "musig2_session.nonce_count must be int"
        assert isinstance(session["partial_sig_count"], int), \
            "musig2_session.partial_sig_count must be int"

        # On regtest with no oracle peers configured, the orchestrator may
        # have a session in CREATED / NONCES_COLLECTING (if the local
        # node booted and ticked once) or "none" (if it never ticked). All
        # are acceptable — the contract is just that the field exists and
        # is well-typed.
        self.log.info(
            "musig2_session state=%s nonce_count=%d partial_sig_count=%d epoch=%d" %
            (session["state"], session["nonce_count"], session["partial_sig_count"], session["epoch"]))

        # epoch must be >= 0 and >= block-height / nDDOracleEpochBlocks.
        # RC34 pins regtest to the same 40-block (~10 minute) signing epoch
        # used by mainnet/testnet.
        height = node.getblockcount()
        epoch_length = 40
        expected_min_epoch = (height - 1) // epoch_length
        assert session["epoch"] >= expected_min_epoch - 1, \
            "musig2_session.epoch=%d unexpectedly small for height %d" % (session["epoch"], height)


if __name__ == "__main__":
    MuSig2SessionStatusTest().main()
