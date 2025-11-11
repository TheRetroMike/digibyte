// Copyright (c) 2014-2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <consensus/amount.h>
#include <policy/fees.h>

#include <boost/test/unit_test.hpp>

#include <set>

BOOST_AUTO_TEST_SUITE(policy_fee_tests)

BOOST_AUTO_TEST_CASE(FeeRounder)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    FeeFilterRounder fee_rounder{CFeeRate{100000}, rng};

    // check that 1000 rounds to 0 or 50000 (DigiByte values)
    std::set<CAmount> results;
    while (results.size() < 2) {
        results.emplace(fee_rounder.round(1000));
    }
    BOOST_CHECK_EQUAL(*results.begin(), 0);
    BOOST_CHECK_EQUAL(*++results.begin(), 50000);

    // check that negative amounts rounds to 0
    BOOST_CHECK_EQUAL(fee_rounder.round(-0), 0);
    BOOST_CHECK_EQUAL(fee_rounder.round(-1), 0);

    // check that MAX_MONEY rounds to 9936506125 (DigiByte: 21 billion vs Bitcoin: 21 million)
    BOOST_CHECK_EQUAL(fee_rounder.round(MAX_MONEY), 9936506125);
}

BOOST_AUTO_TEST_SUITE_END()
