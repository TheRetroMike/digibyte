#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wave 21: MuSig2 P2P stale-epoch DoS bounds.

The MuSig2 nonce/partial-sig net handlers intentionally allow `current + 1`
messages so the next epoch can collect nonces before the boundary. They must
not accept arbitrarily old positive epochs: each accepted authenticated stale
nonce can be relayed and can lazily create obsolete session state until the
next cleanup tick.
"""

import hashlib
import struct
import time

from test_framework.key import sign_schnorr
from test_framework.messages import (
    hash256,
    msg_oraclemusignonce,
    msg_oraclemusigpartialsig,
    ser_string,
    ser_uint256,
)
from test_framework.p2p import P2PInterface
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal

REGTEST_ORACLE_ACTIVATION = 650
MUSIG2_CONTEXT_VERSION = 2


def regtest_oracle_privkey(oracle_id: int) -> bytes:
    return hashlib.sha256(f"digibyte_regtest_oracle_{oracle_id}".encode()).digest()


def musig_nonce_signature_hash(genesis_hash_hex: str, epoch: int,
                               attempt_id: int,
                               oracle_id: int, pubnonce: bytes) -> bytes:
    payload = ser_string(b"DigiDollar/MuSig2Nonce")
    payload += ser_uint256(int(genesis_hash_hex, 16))
    payload += struct.pack("<i", epoch)
    payload += struct.pack("<B", attempt_id)
    payload += struct.pack("<B", oracle_id)
    payload += ser_string(pubnonce)
    return hash256(payload)


def build_signed_musig_nonce(genesis_hash_hex: str, epoch: int,
                             oracle_id: int, fill: int) -> msg_oraclemusignonce:
    attempt_id = 0
    pubnonce = bytes([fill]) * 66
    digest = musig_nonce_signature_hash(genesis_hash_hex, epoch, attempt_id, oracle_id, pubnonce)
    sig = sign_schnorr(regtest_oracle_privkey(oracle_id), digest)

    payload = struct.pack("<i", epoch)
    payload += struct.pack("<B", attempt_id)
    payload += struct.pack("<B", oracle_id)
    payload += ser_string(pubnonce)
    payload += ser_string(sig)
    return msg_oraclemusignonce(payload)


def musig_partialsig_signature_hash(genesis_hash_hex: str, epoch: int,
                                    attempt_id: int,
                                    context_version: int,
                                    session_context_id: int, oracle_id: int,
                                    partial_sig: bytes) -> bytes:
    payload = ser_string(b"DigiDollar/MuSig2PartialSig")
    payload += ser_uint256(int(genesis_hash_hex, 16))
    payload += struct.pack("<i", epoch)
    payload += struct.pack("<B", attempt_id)
    payload += struct.pack("<B", context_version)
    payload += ser_uint256(session_context_id)
    payload += struct.pack("<B", oracle_id)
    payload += ser_string(partial_sig)
    return hash256(payload)


def build_signed_musig_partialsig(genesis_hash_hex: str, epoch: int,
                                  oracle_id: int, fill: int) -> msg_oraclemusigpartialsig:
    attempt_id = 0
    context_version = MUSIG2_CONTEXT_VERSION
    partial_sig = bytes([fill]) * 32
    session_context_id = int.from_bytes(hashlib.sha256(
        b"functional-wave21-session-context" + struct.pack("<i", epoch) + bytes([oracle_id, fill])
    ).digest(), "little")
    digest = musig_partialsig_signature_hash(
        genesis_hash_hex, epoch, attempt_id, context_version, session_context_id, oracle_id, partial_sig)
    sig = sign_schnorr(regtest_oracle_privkey(oracle_id), digest)

    payload = struct.pack("<i", epoch)
    payload += struct.pack("<B", attempt_id)
    payload += struct.pack("<B", context_version)
    payload += ser_uint256(session_context_id)
    payload += struct.pack("<B", oracle_id)
    payload += ser_string(partial_sig)
    payload += ser_string(sig)
    return msg_oraclemusigpartialsig(payload)


def bytes_received_for(node, msgtype: str) -> int:
    return sum(peer.get("bytesrecv_per_msg", {}).get(msgtype, 0)
               for peer in node.getpeerinfo())


class DigiDollarWave21MuSig2P2PDoSTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-dandelion=0", "-debug=net"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0", "-debug=net"],
        ]

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)

    def run_test(self):
        self.log.info("Activate DigiDollar/oracle on both nodes")
        self.generate(self.nodes[0], REGTEST_ORACLE_ACTIVATION + 5)
        self.sync_blocks()

        node0, node1 = self.nodes
        current_epoch = node0.getdigidollardeploymentinfo()["musig2_session"]["epoch"]
        assert current_epoch > 3
        genesis_hash = node0.getblockhash(0)

        sender = node0.add_p2p_connection(P2PInterface())

        self.log.info("Stale MuSig2 nonce epoch must not relay")
        before = bytes_received_for(node1, "oramusnonce")
        stale = build_signed_musig_nonce(
            genesis_hash,
            epoch=current_epoch - 3,
            oracle_id=0,
            fill=0x21,
        )
        sender.send_and_ping(stale)
        time.sleep(1)
        after = bytes_received_for(node1, "oramusnonce")
        assert_equal(after, before)

        self.log.info("Current+1 MuSig2 nonce remains relayable for boundary liveness")
        allowed = build_signed_musig_nonce(
            genesis_hash,
            epoch=current_epoch + 1,
            oracle_id=0,
            fill=0x42,
        )
        sender.send_and_ping(allowed)
        self.wait_until(lambda: bytes_received_for(node1, "oramusnonce") > after,
                        timeout=5)

        self.log.info("Stale MuSig2 partial-sig epoch must not relay")
        psig_before = bytes_received_for(node1, "oramusigpsig")
        stale_psig = build_signed_musig_partialsig(
            genesis_hash,
            epoch=current_epoch - 3,
            oracle_id=0,
            fill=0x11,
        )
        sender.send_and_ping(stale_psig)
        time.sleep(1)
        psig_after = bytes_received_for(node1, "oramusigpsig")
        assert_equal(psig_after, psig_before)

        self.log.info("Current+1 MuSig2 partial-sig remains relayable")
        allowed_psig = build_signed_musig_partialsig(
            genesis_hash,
            epoch=current_epoch + 1,
            oracle_id=0,
            fill=0x12,
        )
        sender.send_and_ping(allowed_psig)
        self.wait_until(lambda: bytes_received_for(node1, "oramusigpsig") > psig_after,
                        timeout=5)


if __name__ == "__main__":
    DigiDollarWave21MuSig2P2PDoSTest().main()
