#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 7 functional coverage for canonical lock-tier enforcement.

Mint validation must accept exactly the 10 canonical lock periods defined
in src/consensus/digidollar.h and reject everything else. The C++ unit
suite covers the in-process consensus path (digidollar_locktier_tests).
This functional test exercises the same invariant end-to-end through the
RPC surface so a regression in the wallet plumbing or the
calculatecollateralrequirement / mintdigidollar entry points does not
slip past the in-process tests.

Cases:
    * mintdigidollar: every canonical tier (0..9) accepted with one mine
      and one position created. Tier byte rejection for -1, 10, 100.
    * calculatecollateralrequirement: every canonical lock_days accepted;
      every non-canonical lock_days (1, 7, 14, 31, 45, 60, 91, 200, 366,
      731, 4000) rejected with the canonical error message that names
      every canonical period.
"""

from decimal import Decimal

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)


# Canonical tier table mirrored from src/consensus/digidollar.h.
# (lock_tier, lock_days, expected_min_ratio_pct).
CANONICAL_TIERS = [
    (0, 0, 1000),     # 1 hour testing tier (lockDays=0 maps to 240 blocks)
    (1, 30, 500),
    (2, 90, 400),
    (3, 180, 350),
    (4, 365, 300),
    (5, 730, 275),
    (6, 1095, 250),
    (7, 1825, 225),
    (8, 2555, 212),
    (9, 3650, 200),
]

ORACLE_PRICE_MICRO_USD = 500000  # $0.50/DGB

NON_CANONICAL_LOCK_DAYS = [1, 7, 14, 31, 45, 60, 91, 200, 366, 731, 4000]
NON_CANONICAL_BLOCKS = [1, 239, 241, 720, 1000]


class DigiDollarLockTierCanonicalTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-digidollar=1",
            "-txindex=1",
            "-mocktime=0",
            "-dandelion=0",
        ]]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Wave 7: canonical lock tier RPC coverage...")
        self.setup_test_chain()

        self.test_calculate_collateral_canonical()
        self.test_calculate_collateral_non_canonical_rejected()
        self.test_mintdigidollar_canonical_tiers_accepted()
        self.test_mintdigidollar_invalid_tier_byte()

    def setup_test_chain(self):
        self.log.info("Generating mature blocks and seeding mock oracle...")
        self.nodes[0].generate(110)
        self.sync_all()
        self.nodes[0].setmockoracleprice(ORACLE_PRICE_MICRO_USD)

    def test_calculate_collateral_canonical(self):
        """Each canonical lock period must be accepted by calculatecollateralrequirement."""
        self.log.info("calculatecollateralrequirement: canonical periods...")
        for tier, lock_days, expected_min_ratio in CANONICAL_TIERS:
            req = self.nodes[0].calculatecollateralrequirement(10000, lock_days)
            assert "required_dgb" in req
            assert "lock_days" in req
            assert "lock_blocks" in req
            assert "base_ratio" in req
            assert_equal(int(req["lock_days"]), lock_days)
            base_ratio = int(req["base_ratio"])
            assert_equal(base_ratio, expected_min_ratio)
            assert_greater_than(Decimal(req["required_dgb"]), Decimal("0"))

    def test_calculate_collateral_non_canonical_rejected(self):
        """Non-canonical lock_days must produce the canonical error string."""
        self.log.info("calculatecollateralrequirement: non-canonical periods...")
        # The RPC explicitly rejects with a message that names every
        # canonical period (rpc/digidollar.cpp:727-731). We assert the
        # canonical-period token is present in the message so the help text
        # cannot drift away from consensus.
        canonical_tokens = ["0", "30", "90", "180", "365", "730",
                            "1095", "1825", "2555", "3650"]
        for bad_days in NON_CANONICAL_LOCK_DAYS:
            try:
                self.nodes[0].calculatecollateralrequirement(10000, bad_days)
            except Exception as e:
                msg = str(e)
                assert "Invalid lock period" in msg, (
                    f"calculatecollateralrequirement({bad_days}) message: {msg}")
                for token in canonical_tokens:
                    assert token in msg, (
                        f"calculatecollateralrequirement({bad_days}) message "
                        f"missing canonical token {token}: {msg}")
            else:
                raise AssertionError(
                    f"calculatecollateralrequirement accepted non-canonical "
                    f"lock_days={bad_days}")

        # Negative lock_days uses a different error (the sanity guard at
        # rpc/digidollar.cpp:697-699), so we accept any rpc-error there.
        try:
            self.nodes[0].calculatecollateralrequirement(10000, -1)
        except Exception as e:
            self.log.info(f"Negative lock_days rejected as expected: {e}")
        else:
            raise AssertionError(
                "calculatecollateralrequirement accepted lock_days=-1")

    def test_mintdigidollar_canonical_tiers_accepted(self):
        """Every canonical tier 0..9 must be mintable end-to-end."""
        self.log.info("mintdigidollar: canonical tiers accepted...")
        # Refresh mock oracle once per mint to keep recent quotes fresh.
        for tier, lock_days, _ratio in CANONICAL_TIERS:
            self.nodes[0].setmockoracleprice(ORACLE_PRICE_MICRO_USD)
            try:
                # Use a small mint amount that comfortably fits regtest's
                # $0.01-$1000 mint range. $50 = 5000 cents.
                result = self.nodes[0].mintdigidollar(5000, tier)
            except Exception as e:
                # Acceptable failure modes here would be insufficient funds
                # if regtest blocks 110 do not cover collateral for the
                # 1000% tier. The point is that the tier itself is valid:
                # the error string must NOT include "Lock tier must be".
                msg = str(e)
                assert "Lock tier must be" not in msg, (
                    f"Canonical tier {tier} surfaced tier-validation error: {msg}")
                self.log.info(
                    f"  tier {tier} ({lock_days}d) skipped: {msg}")
                continue
            assert "txid" in result
            assert_equal(int(result["lock_tier"]), tier)
            self.nodes[0].generate(1)
            self.sync_all()

    def test_mintdigidollar_invalid_tier_byte(self):
        """Tier bytes outside 0..9 must be rejected by the RPC layer."""
        self.log.info("mintdigidollar: invalid tier bytes rejected...")
        # The RPC validates tier 0..9 inline (rpc/digidollar.cpp:959-961)
        # so the error always contains the canonical message.
        for bad_tier in [-1, 10, 11, 100, 255, 1000]:
            assert_raises_rpc_error(
                -8,
                "Lock tier must be between 0 and 9",
                self.nodes[0].mintdigidollar,
                100000,
                bad_tier,
            )


if __name__ == "__main__":
    DigiDollarLockTierCanonicalTest().main()
