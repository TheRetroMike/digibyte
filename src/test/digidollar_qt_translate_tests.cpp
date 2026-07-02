// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// DD-FA-FUNC-032 (Wave 19 Agent C): the Qt mint flow surfaces consensus
// reject reasons returned by m_node.broadcastTransaction verbatim. Bare
// strings like "minting-blocked-during-err" or "bad-tx-no-musig2-quote"
// confuse end users and provide no remediation hint. The widget therefore
// pipes the reason through a translator that maps known DigiDollar/oracle
// consensus tags to a plain-English explanation before display.
//
// This unit test pins the translator contract so the table can grow
// without Qt being part of the build.

#include <test/util/setup_common.h>

#include <qt/digidollar_qt_translate.h>

#include <boost/test/unit_test.hpp>

#include <string>

BOOST_FIXTURE_TEST_SUITE(digidollar_qt_translate_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(translate_minting_blocked_during_err)
{
    const std::string consensus = "minting-blocked-during-err";
    const std::string user = TranslateMintRejectReasonForUser(consensus);
    BOOST_CHECK_NE(user, consensus);
    BOOST_CHECK(user.find("Emergency Redemption") != std::string::npos);
    BOOST_CHECK(user.find("paused") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(translate_no_musig2_quote)
{
    const std::string consensus = "bad-tx-no-musig2-quote";
    const std::string user = TranslateMintRejectReasonForUser(consensus);
    BOOST_CHECK_NE(user, consensus);
    BOOST_CHECK(user.find("oracle quote") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(translate_volatility_freeze)
{
    const std::string consensus = "volatility-freeze";
    const std::string user = TranslateMintRejectReasonForUser(consensus);
    BOOST_CHECK_NE(user, consensus);
    BOOST_CHECK(user.find("volatility") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(translate_current_volatility_reject_tokens)
{
    const std::vector<std::string> consensus_reasons{
        "minting-frozen-volatility",
        "all-operations-frozen",
        "minting-frozen-volatility-candidate",
    };

    for (const std::string& consensus : consensus_reasons) {
        const std::string user = TranslateMintRejectReasonForUser(consensus);
        BOOST_CHECK_NE(user, consensus);
        BOOST_CHECK(user.find("volatility") != std::string::npos ||
                    user.find("frozen") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(translate_bad_lock_tier_duration)
{
    const std::string consensus = "bad-mint-lock-tier-duration";
    const std::string user = TranslateMintRejectReasonForUser(consensus);
    BOOST_CHECK_NE(user, consensus);
    BOOST_CHECK(user.find("lock") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(translate_unknown_passes_through)
{
    // Unknown reasons must pass through unchanged so we never
    // hide forensic detail from operators or RPC clients during
    // Qt regression debugging.
    const std::string consensus = "totally-unknown-reason-xyz";
    const std::string user = TranslateMintRejectReasonForUser(consensus);
    BOOST_CHECK_EQUAL(user, consensus);
}

BOOST_AUTO_TEST_CASE(translate_substring_match)
{
    // Mempool wraps reject reasons in surrounding context; the translator
    // must still recognise the canonical token inside a longer string.
    const std::string consensus = "mempool rejected (reason: minting-blocked-during-err)";
    const std::string user = TranslateMintRejectReasonForUser(consensus);
    BOOST_CHECK_NE(user, consensus);
    BOOST_CHECK(user.find("Emergency Redemption") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(translate_empty_input)
{
    BOOST_CHECK_EQUAL(TranslateMintRejectReasonForUser(""), "");
}

BOOST_AUTO_TEST_SUITE_END()
