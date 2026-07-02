#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Wave 9 (Agent C): assert getoracles exposes a clear in_consensus field.

Operators reading `getoracles` need to know which slots are part of the
active MuSig2 quorum (`consensus.vOraclePublicKeys`) versus reserve slots
that exist in `vOracleNodes` but cannot vote. Without an explicit field,
operators have to cross-reference chainparams source by hand and may
misread mainnet's 30-slot table as a 30-of-30 quorum.

Regtest configures 7 oracle slots and a 4-of-7 MuSig2 roster, so every
slot is in the active quorum here. The invariant the test pins is:

    in_consensus == (oracle_id < oracle_pubkey_count)

which is exactly the predicate `OracleBundleManager::ValidateMuSig2Bundle`
applies in `src/oracle/bundle_manager.cpp` to reject signers outside the
active roster.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal


class GetOraclesConsensusFieldTest(DigiByteTestFramework):
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
        oracle_pubkey_count = deployment["oracle_pubkey_count"]
        oracle_total_slots = deployment["oracle_total_slots"]
        self.log.info(
            "Deployment: %d-of-%d MuSig2, %d slots configured" % (
                deployment["oracle_consensus_required"],
                oracle_pubkey_count,
                oracle_total_slots))

        self.log.info("Verify getoracles exposes in_consensus field per slot")
        oracles = node.getoracles(False, 20)
        assert len(oracles) == oracle_total_slots, \
            "Expected %d oracles in getoracles, got %d" % (oracle_total_slots, len(oracles))

        for oracle in oracles:
            assert "in_consensus" in oracle, \
                "Oracle %d missing 'in_consensus' field — operator cannot tell active vs reserve" % oracle["oracle_id"]
            assert isinstance(oracle["in_consensus"], bool), \
                "'in_consensus' must be boolean, got %s" % type(oracle["in_consensus"])

            expected = (oracle["oracle_id"] < oracle_pubkey_count)
            assert_equal(oracle["in_consensus"], expected)

        in_consensus = sum(1 for o in oracles if o["in_consensus"])
        assert_equal(in_consensus, oracle_pubkey_count)
        self.log.info("getoracles in_consensus field: %d/%d slots in active roster" % (
            in_consensus, len(oracles)))


if __name__ == "__main__":
    GetOraclesConsensusFieldTest().main()
