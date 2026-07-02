// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// MuSig2 P2P handling tests — placeholder pending Wave 3 integration
// Full tests require wired session + net_processing interaction

#include <boost/test/unit_test.hpp>
#include <protocol.h>
#include <test/util/setup_common.h>

BOOST_FIXTURE_TEST_SUITE(musig2_p2p_handling_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(test_musig2_p2p_message_types_registered)
{
    // Verify P2P message type strings are registered
    BOOST_CHECK_EQUAL(std::string(NetMsgType::ORACLEMUSIGNONCE), "oramusnonce");
    BOOST_CHECK_EQUAL(std::string(NetMsgType::ORACLEMUSIGCONTEXT), "oramusigctx");
    BOOST_CHECK_EQUAL(std::string(NetMsgType::ORACLEMUSIGPARTIALSIG), "oramusigpsig");
}

BOOST_AUTO_TEST_SUITE_END()
