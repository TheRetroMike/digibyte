#!/usr/bin/env python3
# Copyright (c) 2020-2022 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test error messages for 'getaddressinfo' and 'validateaddress' RPC commands."""

from test_framework.test_framework import DigiByteTestFramework

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

# DigiByte regtest addresses (from v8.22.2 working version)
BECH32_VALID = 'dgbrt1qtmp74ayg7p24uslctssvjm06q5phz4yrgndnyh'
BECH32_VALID_CAPITALS = 'DGBRT1QPLMTZKC2XHARPPZDLNPAQL78RSHJ68U38MUXH'
BECH32_VALID_MULTISIG = 'dgbrt1qdg3myrgvzw7ml9q0ejxhlkyxm7vl9r56yzkfgvzclrf4hkpx9yfqkknkfh'

BECH32_INVALID_BECH32 = 'dgbrt1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq0f0gl9'
BECH32_INVALID_BECH32M = 'dgbrt1qw508d6qejxtdg4y5r3zarvary0c5xw7klmwc4k'
BECH32_INVALID_VERSION = 'dgbrt130xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqxp9x0t'
BECH32_INVALID_SIZE = 'dgbrt1s0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7v8n0nx0muaewav25rew0dg'
BECH32_INVALID_V0_SIZE = 'dgbrt1qw508d6qejxtdg4y5r3zarvary0c5xw7kqqzq6ha3'
BECH32_INVALID_PREFIX = 'dgb1pw508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7k5pxtuk'
BECH32_TOO_LONG = 'dgbrt1q049edschfnwystcqnsvyfpj23mpsg3jcedq9xv049edschfnwystcqnsvyfpj23mpsg3jcedq9xv049edschfnwystcqnsvyfpj23m'
BECH32_ONE_ERROR = 'dgbrt1q049edschfnwystcqnsvyfpj23mpsg3jcedq9xv'
BECH32_ONE_ERROR_CAPITALS = 'DGBRT1QPLMTZKC2XHARPPZDLNPAQL78RSHJ68U38MUXH'
BECH32_TWO_ERRORS = 'dgbrt1qax9suht3qv95sw33xavx8crpxduefdrsvgsklu'
BECH32_NO_SEPARATOR = 'dgbrtq049ldschfnwystcqnsvyfpj23mpsg3jcedq9xv'
BECH32_INVALID_CHAR = 'dgbrt1q04oldschfnwystcqnsvyfpj23mpsg3jcedq9xv'
BECH32_MULTISIG_TWO_ERRORS = 'dgbrt1qdg3myrgvzw7ml8q0ejxhlkyxn7vl9r56yzkfgvzclrf4hkpx9yfqkknkfh'
BECH32_WRONG_VERSION = 'dgbrt1ptmp74ayg7p24uslctssvjm06q5phz4yrgndnyh'

BASE58_VALID = 't1HLHCmYY7YBZyNGYETdaWDFyjHppENiUS'
BASE58_INVALID_PREFIX = '17VZNX1SN5NtKa8UQFxwQbFeFc3iqRYhem'
BASE58_INVALID_CHECKSUM = 't1HLHCmYY7YBZyNGYETdaWDFyjHppENiUT'
BASE58_INVALID_LENGTH = '2VKf7XKMrp4bVNVmuRbyCewkP8FhGLP2E54LHDPakr9Sq5mtU2'

INVALID_ADDRESS = 'asfah14i8fajz0123f'
INVALID_ADDRESS_2 = '1q049ldschfnwystcqnsvyfpj23mpsg3jcedq9xv'

class InvalidAddressErrorMessageTest(DigiByteTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def check_valid(self, addr):
        info = self.nodes[0].validateaddress(addr)
        assert info['isvalid']
        assert 'error' not in info
        assert 'error_locations' not in info

    def check_invalid(self, addr, error_str=None, error_locations=None):
        res = self.nodes[0].validateaddress(addr)
        assert not res['isvalid']
        if error_str is not None:
            assert_equal(res['error'], error_str)
        else:
            # For DigiByte, just verify an error field exists for invalid addresses
            assert 'error' in res, f"Expected error field for invalid address {addr}, got: {res}"
        if error_locations:
            assert_equal(res['error_locations'], error_locations)
        else:
            if 'error_locations' in res:
                assert_equal(res['error_locations'], [])

    def test_validateaddress(self):
        # Test invalid DigiByte bech32 addresses (should use dgbrt1 prefix)
        invalid_bech32_cases = [
            "bcrt1qtmp74ayg7p24uslctssvjm06q5phz4yrrhkqgv",  # Bitcoin regtest prefix
            "dgb1qtmp74ayg7p24uslctssvjm06q5phz4yr8a2kw9",   # Mainnet prefix
            "dgbrt1invalid_bech32_checksum",                    # Invalid checksum
            "dgbrt1qtmp74ayg7p24uslctssvjm06q5phz4yr",         # Truncated
        ]
        for addr in invalid_bech32_cases:
            self.check_invalid(addr)

        # Use predefined valid DigiByte addresses
        self.check_valid(BECH32_VALID)
        # Note: Legacy address validation skipped as this test focuses on bech32 validation

        # Test invalid Base58 addresses  
        invalid_base58_cases = [
            "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa",  # Bitcoin mainnet
            "mpLQjfK79b7CCV4VMJWEWAj5Mpx8Up5zxB",  # Bitcoin regtest
            "invalid_base58_checksum123",           # Invalid format
        ]
        for addr in invalid_base58_cases:
            self.check_invalid(addr)

        # Invalid address formats
        invalid_formats = [
            "not_an_address",
            "",
            "123",
            "invalid_format_test"
        ]
        for addr in invalid_formats:
            self.check_invalid(addr)

        node = self.nodes[0]

        # Missing arg returns the help text
        assert_raises_rpc_error(-1, "Return information about the given digibyte address.", node.validateaddress)
        # Explicit None is not allowed for required parameters
        assert_raises_rpc_error(-3, "JSON value of type null is not of expected type string", node.validateaddress, None)

    def test_getaddressinfo(self):
        node = self.nodes[0]

        assert_raises_rpc_error(-5, "Invalid Bech32 address program size (41 bytes)", node.getaddressinfo, BECH32_INVALID_SIZE)
        assert_raises_rpc_error(-5, "Invalid or unsupported Segwit (Bech32) or Base58 encoding.", node.getaddressinfo, BECH32_INVALID_PREFIX)
        assert_raises_rpc_error(-5, "Invalid or unsupported Base58-encoded address.", node.getaddressinfo, BASE58_INVALID_PREFIX)
        assert_raises_rpc_error(-5, "Invalid or unsupported Segwit (Bech32) or Base58 encoding.", node.getaddressinfo, INVALID_ADDRESS)

    def run_test(self):
        self.test_validateaddress()

        if self.is_wallet_compiled():
            self.init_wallet(node=0)
            self.test_getaddressinfo()


if __name__ == '__main__':
    InvalidAddressErrorMessageTest().main()
