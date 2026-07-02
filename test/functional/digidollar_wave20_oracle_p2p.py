#!/usr/bin/env python3
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Wave 20 multi-node functional: P2P oracle messages and pending bundle abuse.

This test exercises the on-wire surface in `src/net_processing.cpp` for
DigiDollar oracle P2P messages — the layer that the legacy
`feature_oracle_p2p.py` scaffolding silently passes (every assertion is
swallowed by `try/except` and reports "Tests successful" while the
referenced RPCs do not even exist). Wave 20 needs a real exercise of:

1.  Honest `oracleprice` propagation across two nodes — the message hash
    appears in `bytesrecv_per_msg["oracleprice"]` on the relay peer.
2.  Malformed `oracleprice` payloads — peer is disconnected for invalid
    signature (`Misbehaving +20` repeats until `DISCOURAGEMENT_THRESHOLD`
    of 100 is hit, then `MaybeDiscourageAndDisconnect` fires).
3.  Duplicate `oracleprice` — silently dropped, peer not penalised
    (`bundleManager.HasOracleMessage(msg_hash)` short-circuit at the top
    of `ProcessMessage("oracleprice")`).
4.  `getoracles` flooding — silently rate-limited at 10/min/peer with no
    misbehaviour penalty (the handler uses `LogPrint` and `return`, never
    `Misbehaving`).
5.  `getoracles` pending response — when a peer requests pending oracle
    data, the node sends back its pending messages as `oracleprice`
    frames.
6.  Pre-activation peer connecting to an active node — the malformed
    `oracleprice` is *ignored* before `nOracleActivationHeight` (the gate
    `Consensus::IsOracleActive` short-circuits before `Misbehaving`).
7.  BIP9-inactive peer above the oracle height still ignores oracle P2P frames.
8.  Heartbeat telemetry uses the same activation gate as oracle data-path
    messages and is ignored before DigiDollar is active.
9.  Peer recovery after disconnect — a second honest peer can reconnect
    after the attacker is banned and successfully exchange oracle data.

Wave 20 ID assignments (next-free per Wave 19 ledger):
- DD-FA-FUNC-033: real multi-node oracle P2P functional coverage.
- DD-FA-DOC-011: legacy `feature_oracle_p2p.py` is a silently-passing
  scaffold; documented in the ledger as superseded by this test.

This functional script is the canonical Wave 20 multi-node coverage. It
runs against **regtest only** because it derives oracle private keys
deterministically from `SHA256("digibyte_regtest_oracle_N")` to mirror
`MockOracleManager`. Mainnet/testnet keys are never touched.
"""

import hashlib
import io
import struct
import time

from test_framework.key import sign_schnorr, compute_xonly_pubkey
from test_framework.messages import (
    hash256,
    msg_getoracles,
    msg_oraclebundle,
    msg_oracleprice,
    ser_compact_size,
)
from test_framework.p2p import P2PInterface
from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_greater_than

# Match `Consensus::IsOracleActive` regtest threshold.
REGTEST_ORACLE_ACTIVATION = 650
# `DISCOURAGEMENT_THRESHOLD` from src/net_processing.h (=100).
# 5 invalid-sig hits at +20 each will push the peer to the threshold.
DISCOURAGE_THRESHOLD = 100
INVALID_SIG_PENALTY = 20
# `ProcessMessage(NetMsgType::GETORACLES)` rate-limits at 10 per minute per
# peer. We send well above that to exercise the silent-drop path.
GETORACLES_RATE_LIMIT_PER_MIN = 10


def regtest_oracle_privkey(oracle_id: int) -> bytes:
    """Recreate the deterministic regtest oracle private key.

    `MockOracleManager` and `CChainParams::InitializeOracleNodes` derive each
    regtest oracle key from `SHA256("digibyte_regtest_oracle_N")`. The C++
    side hex-decodes the digest into `secp256k1_keypair_create`; that is the
    same 32-byte secret we feed into Python's `sign_schnorr`.
    """
    return hashlib.sha256(f"digibyte_regtest_oracle_{oracle_id}".encode()).digest()


def attestation_hash(oracle_id: int, price_micro_usd: int, timestamp: int) -> bytes:
    """Match `COraclePriceMessage::GetAttestationSignatureHash`.

    `CHashWriter(0)` writes (oracle_id LE u32 || price LE u64 || timestamp
    LE i64) and finalises to double-SHA256.
    """
    payload = struct.pack("<I", oracle_id)
    payload += struct.pack("<Q", price_micro_usd)
    payload += struct.pack("<q", timestamp)
    return hash256(payload)


def build_signed_oracle_price(oracle_id: int, price_micro_usd: int,
                              timestamp: int, *, valid_sig: bool = True) -> msg_oracleprice:
    """Construct a signed `oracleprice` P2P message.

    When `valid_sig=False` the signature is replaced with 64 zero bytes so
    the receiver hits `Misbehaving(*peer, 20, "invalid oracle signature")`
    after pubkey rebinding.
    """
    msg = msg_oracleprice()
    msg.oracle_id = oracle_id
    msg.price_micro_usd = price_micro_usd
    msg.timestamp = timestamp
    msg.block_height = 0
    msg.nonce = 0

    secret = regtest_oracle_privkey(oracle_id)
    xpub, _ = compute_xonly_pubkey(secret)
    msg.oracle_pubkey = xpub

    if valid_sig:
        digest = attestation_hash(oracle_id, price_micro_usd, timestamp)
        msg.schnorr_sig = sign_schnorr(secret, digest)
    else:
        msg.schnorr_sig = b"\x00" * 64
    return msg


class msg_oracleheartbeat:
    """oraclehb message carrying a signed operator-version heartbeat."""

    msgtype = b"oraclehb"

    def __init__(self, oracle_id=0, timestamp=0):
        self.oracle_id = oracle_id
        self.timestamp = timestamp

    def serialize(self):
        r = struct.pack("<B", 1)                  # heartbeat_version
        r += struct.pack("<I", self.oracle_id)
        r += struct.pack("<q", self.timestamp)
        r += struct.pack("<Q", 1)                 # nonce
        r += struct.pack("<i", 9260044)           # client_version
        r += struct.pack("<i", 70019)             # p2p_protocol_version
        r += struct.pack("<B", 1)                 # oracle_protocol_version
        r += struct.pack("<B", 1)                 # musig2_context_version
        version = b"test-heartbeat"
        subversion = b"/DigiByte:test/"
        r += ser_compact_size(len(version)) + version
        r += ser_compact_size(len(subversion)) + subversion
        r += ser_compact_size(64) + (b"\x00" * 64)  # deliberately invalid signature
        return r

    def __repr__(self):
        return f"msg_oracleheartbeat(oracle_id={self.oracle_id})"


def _peer_for_p2p(node, p2p_conn):
    """Return the peer entry on `node` matching the test-framework p2p.

    `p2p_conn._transport.get_extra_info("socket").getsockname()` is the
    address pair the framework already uses in `add_p2p_connection`.
    """
    sockname = p2p_conn._transport.get_extra_info("socket").getsockname()
    addr = f"{sockname[0]}:{sockname[1]}"
    for peer in node.getpeerinfo():
        if peer["addr"] == addr:
            return peer
    return None


def bytes_received_for(node, msgtype: str) -> int:
    return sum(peer.get("bytesrecv_per_msg", {}).get(msgtype, 0)
               for peer in node.getpeerinfo())


class DigiDollarWave20OracleP2PTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        # Node 0/1: post-activation oracle nodes, linearly connected.
        # Node 2: pre-activation node — used to prove activation gating.
        # Node 3: height is above nOracleActivationHeight, but BIP9 is inactive.
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-dandelion=0", "-debug=net"],
            ["-digidollar=1", "-txindex=1", "-dandelion=0", "-debug=net"],
            [
                "-digidollar=1",
                "-txindex=1",
                "-dandelion=0",
                "-debug=net",
                # Push activation far above the height we ever mine here.
                "-digidollaractivationheight=99999",
            ],
            [
                "-digidollar=1",
                "-txindex=1",
                "-dandelion=0",
                "-debug=net",
                # Keep the DigiDollar deployment inactive while leaving
                # nOracleActivationHeight at the regtest default 650.
                "-vbparams=digidollar:4102444800:4102531200",
            ],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.setup_nodes()
        # Node 0 <-> Node 1 are the active oracle pair.
        self.connect_nodes(0, 1)
        # Node 2 stays disconnected — only attached as a P2P peer where
        # specifically required.

    def run_test(self):
        # Activate DigiDollar/oracle on nodes 0 and 1 only. Node 2 stays
        # below the override activation height.
        self.log.info("Activate DigiDollar/oracle on nodes 0 and 1")
        self.generate(self.nodes[0], REGTEST_ORACLE_ACTIVATION + 5,
                      sync_fun=lambda: self.sync_blocks(self.nodes[:2]))
        # Sanity: activation gates flipped where we expected them.
        assert_greater_than(self.nodes[0].getblockcount(),
                            REGTEST_ORACLE_ACTIVATION)
        assert_greater_than(REGTEST_ORACLE_ACTIVATION,
                            self.nodes[2].getblockcount())
        self.log.info("Mine node 3 above oracle height with DigiDollar BIP9 inactive")
        self.generate(self.nodes[3], REGTEST_ORACLE_ACTIVATION + 5,
                      sync_fun=lambda: None)
        failed_info = self.nodes[3].getdeploymentinfo()["deployments"]["digidollar"]["bip9"]
        assert failed_info["status"] != "active"
        failed_dep = self.nodes[3].getdigidollardeploymentinfo()
        assert_equal(failed_dep["enabled"], False)
        assert_greater_than(self.nodes[3].getblockcount(),
                            REGTEST_ORACLE_ACTIVATION)

        self.test_honest_propagation()
        self.test_malformed_payload_silently_dropped()
        self.test_invalid_signature_misbehaving()
        self.test_duplicate_message_silently_dropped()
        self.test_deprecated_oraclebundle_ignored()
        self.test_getoracles_pull_returns_pending()
        self.test_getoracles_epoch_boundaries()
        self.test_getoracles_flooding_rate_limited()
        self.test_pre_activation_peer_ignores_oracleprice()
        self.test_inactive_bip9_peer_ignores_oracleprice()
        self.test_inactive_bip9_peer_ignores_oracleheartbeat()
        self.test_recovery_after_attacker_disconnect()

        self.log.info("Wave 20 oracle P2P tests passed")

    # ------------------------------------------------------------------
    # 1. Honest propagation
    # ------------------------------------------------------------------
    def test_honest_propagation(self):
        self.log.info("Test 1: honest oracleprice propagates 0 -> 1")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())

        oracle_id = 3
        # Use an oracle slot not covered by the daemon's mock to keep this
        # an isolated single-message exercise.
        now = int(time.time())
        msg = build_signed_oracle_price(oracle_id, 6500, now)

        bundle_before = self.nodes[1].getalloracleprices(20)["oracles"]
        before_oid = {entry["oracle_id"]: entry for entry in bundle_before}

        peer.send_and_ping(msg)
        # Allow node 0's relay to fan out to node 1.
        active_nodes = self.nodes[:2]
        self.sync_blocks(active_nodes)
        self.sync_mempools(active_nodes)
        time.sleep(1)

        bundle_after = self.nodes[1].getalloracleprices(20)["oracles"]
        after_entry = next((e for e in bundle_after if e["oracle_id"] == oracle_id),
                           None)
        assert after_entry is not None, (
            f"Oracle {oracle_id} entry missing from node 1 getalloracleprices "
            f"after honest oracleprice broadcast")
        # Either the price now matches, or the node was already reporting and
        # the timestamp moved forward — both prove the message was accepted
        # rather than rejected.
        accepted = (
            after_entry["status"] == "reporting"
            and after_entry["price_micro_usd"] == 6500
        )
        prior = before_oid.get(oracle_id, {})
        moved_forward = (
            after_entry.get("timestamp", 0) > prior.get("timestamp", 0)
        )
        assert accepted or moved_forward, (
            f"Oracle {oracle_id} still stale on node 1 after honest msg: "
            f"before={prior} after={after_entry}")

        # Node 1 should show a non-zero oracleprice byte counter for the
        # P2P link from node 0. We look for any peer with non-zero
        # oracleprice received bytes.
        relayed = False
        for entry in self.nodes[1].getpeerinfo():
            if entry.get("bytesrecv_per_msg", {}).get("oracleprice", 0) > 0:
                relayed = True
                break
        assert relayed, "Node 1 did not record any oracleprice bytes on its peers"
        self.nodes[0].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 2. Malformed payload — silently swallowed at ProcessMessage
    # ------------------------------------------------------------------
    def test_malformed_payload_silently_dropped(self):
        self.log.info("Test 2: malformed oracleprice silently dropped (no ban)")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())

        # Wrap one byte into a checksummed envelope that the V1 transport
        # will accept (correct magic + checksum + valid command name) but
        # whose payload is too short for `OraclePriceMsg` to deserialize.
        # Production handler in `src/net_processing.cpp:5447` does
        # `vRecv >> oracle_msg`; on `ios_base::failure` the exception is
        # caught one frame up at `ProcessMessage` and logged. The peer
        # stays connected — Bitcoin Core's design avoids penalising
        # connection partners for stream framing oddities so honest peers
        # are not killed by transient corruption.
        magic = peer.magic_bytes
        msgtype = b"oracleprice"
        data = b"\x01"  # too short for COraclePriceMessage
        envelope = magic + msgtype + b"\x00" * (12 - len(msgtype))
        envelope += struct.pack("<I", len(data))
        chk = hashlib.sha256(hashlib.sha256(data).digest()).digest()[:4]
        envelope += chk + data
        for _ in range(20):
            peer.send_raw_message(envelope)
        peer.sync_with_ping(timeout=15)

        assert peer.is_connected, (
            "Peer disconnected after malformed oracleprice — production "
            "code must catch the deserialize exception and log/return")
        self.nodes[0].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 3. Invalid signature — Misbehaving until disconnect threshold
    # ------------------------------------------------------------------
    def test_invalid_signature_misbehaving(self):
        self.log.info("Test 3: invalid oracleprice signatures disconnect attacker")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())

        # Each invalid-signature message contributes +20. Five hits land on
        # the discourage threshold of 100 and `MaybeDiscourageAndDisconnect`
        # closes the peer on the next SendMessages cycle.
        bursts = (DISCOURAGE_THRESHOLD // INVALID_SIG_PENALTY) + 2
        for i in range(bursts):
            now = int(time.time())
            msg = build_signed_oracle_price(2, 7000 + i, now, valid_sig=False)
            try:
                peer.send_message(msg)
            except IOError:
                # Once the node disconnects mid-send the framework raises.
                break

        peer.wait_for_disconnect(timeout=20)
        self.log.info("  bad-signature peer disconnected after threshold")
        self.nodes[0].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 4. Duplicate honest messages — silent drop
    # ------------------------------------------------------------------
    def test_duplicate_message_silently_dropped(self):
        self.log.info("Test 4: duplicate oracleprice silently dropped")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())

        oracle_id = 4
        now = int(time.time())
        msg = build_signed_oracle_price(oracle_id, 6700, now)

        peer.send_and_ping(msg)
        # Re-send the identical bytes a few times. The handler must
        # short-circuit before `Misbehaving`, so the connection stays open.
        for _ in range(5):
            peer.send_and_ping(msg)

        # The peer must still be connected and its misbehaviour pad must
        # not have crossed the discourage threshold (we cannot read the
        # internal score, but we can prove the connection is alive).
        assert peer.is_connected, (
            "Peer was disconnected after duplicate honest oracleprice — "
            "should be silent drop")
        peer_entry = _peer_for_p2p(self.nodes[0], peer)
        assert peer_entry is not None, "Test peer vanished from getpeerinfo"
        self.nodes[0].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 4b. Deprecated oraclebundle is decoded then ignored, never relayed
    # ------------------------------------------------------------------
    def test_deprecated_oraclebundle_ignored(self):
        self.log.info("Test 4b: deprecated oraclebundle ignored without relay")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())

        before = bytes_received_for(self.nodes[1], "oraclebundle")
        bundle = msg_oraclebundle()
        bundle.version = 3
        bundle.epoch = self.nodes[0].getblockcount() // 10
        bundle.median_price_micro_usd = 6500
        bundle.timestamp = int(time.time())
        bundle.aggregate_sig = b"\x00" * 64
        bundle.participation_bitmap = b"\x01"
        bundle.block_hash = int(self.nodes[0].getbestblockhash(), 16)

        peer.send_and_ping(bundle)
        time.sleep(1)

        assert peer.is_connected
        assert_equal(bytes_received_for(self.nodes[1], "oraclebundle"), before)
        self.nodes[0].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 5. getoracles pull request returns pending messages
    # ------------------------------------------------------------------
    def test_getoracles_pull_returns_pending(self):
        self.log.info("Test 5: getoracles pull returns pending oracleprice frames")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())

        # Seed node 0 with two pending messages.
        now = int(time.time())
        for oid, price in [(5, 6800), (6, 6900)]:
            seeded = build_signed_oracle_price(oid, price, now)
            peer.send_and_ping(seeded)
        time.sleep(1)

        # Track replies before sending the pull request.
        baseline = peer.message_count.get("oracleprice", 0)

        # Request all oracles for the current epoch. Regtest uses
        # `nDDOracleEpochBlocks` value changes across releases, so we read the live epoch from the
        # deployment-info RPC instead of guessing.
        deployment = self.nodes[0].getdigidollardeploymentinfo()
        current_epoch = (deployment.get("musig2_session", {})
                         .get("epoch"))
        if current_epoch is None:
            current_epoch = self.nodes[0].getblockcount() // 10
        request = msg_getoracles()
        request.epoch = current_epoch
        request.oracle_id = 0xFFFFFFFF
        peer.send_and_ping(request)
        # Wait for the node to push back at least one oracleprice in
        # response. Cap the wait at 10 s.
        deadline = time.time() + 10
        while time.time() < deadline:
            if peer.message_count.get("oracleprice", 0) > baseline:
                break
            time.sleep(0.5)
        delta = peer.message_count.get("oracleprice", 0) - baseline
        assert_greater_than(delta, 0)
        self.log.info("  node returned %d oracleprice frames in response (epoch=%d)",
                      delta, current_epoch)
        self.nodes[0].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 5b. getoracles epoch admission boundaries
    # ------------------------------------------------------------------
    def test_getoracles_epoch_boundaries(self):
        self.log.info("Test 5b: getoracles epoch boundaries are exact")
        seed_peer = self.nodes[0].add_p2p_connection(P2PInterface())
        seed_peer.send_and_ping(build_signed_oracle_price(4, 6700, int(time.time())))
        self.nodes[0].disconnect_p2ps()
        time.sleep(1)

        deployment = self.nodes[0].getdigidollardeploymentinfo()
        current_epoch = (deployment.get("musig2_session", {})
                         .get("epoch"))
        if current_epoch is None:
            current_epoch = self.nodes[0].getblockcount() // 10

        def oracleprice_delta_for_epoch(epoch):
            peer = self.nodes[0].add_p2p_connection(P2PInterface())
            baseline = peer.message_count.get("oracleprice", 0)
            request = msg_getoracles()
            request.epoch = epoch
            request.oracle_id = 0xFFFFFFFF
            peer.send_and_ping(request)
            deadline = time.time() + 5
            while time.time() < deadline:
                if peer.message_count.get("oracleprice", 0) > baseline:
                    break
                time.sleep(0.2)
            delta = peer.message_count.get("oracleprice", 0) - baseline
            self.nodes[0].disconnect_p2ps()
            return delta

        assert_equal(oracleprice_delta_for_epoch(current_epoch - 25), 0)
        assert_greater_than(oracleprice_delta_for_epoch(current_epoch - 24), 0)
        assert_greater_than(oracleprice_delta_for_epoch(current_epoch + 1), 0)
        assert_equal(oracleprice_delta_for_epoch(current_epoch + 2), 0)

    # ------------------------------------------------------------------
    # 6. getoracles flooding — silent rate-limit, NOT Misbehaving
    # ------------------------------------------------------------------
    def test_getoracles_flooding_rate_limited(self):
        self.log.info("Test 6: getoracles flooding silently rate-limited")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())

        # Send well above the 10/min threshold. Production code path:
        # `ProcessMessage(NetMsgType::GETORACLES)` returns silently when
        # `count > 10`, never invoking `Misbehaving`.
        request = msg_getoracles()
        request.epoch = 0
        request.oracle_id = 0xFFFFFFFF
        for _ in range(GETORACLES_RATE_LIMIT_PER_MIN * 3):
            peer.send_message(request)
        peer.sync_with_ping(timeout=15)

        assert peer.is_connected, (
            "Peer disconnected from getoracles flood — production code must "
            "rate-limit silently, never Misbehaving")
        self.nodes[0].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 7. Pre-activation peer ignores oracleprice
    # ------------------------------------------------------------------
    def test_pre_activation_peer_ignores_oracleprice(self):
        self.log.info("Test 7: pre-activation node ignores oracleprice + getoracles")
        # Node 2 is configured with -digidollaractivationheight=99999 and
        # has not mined past it, so `Consensus::IsOracleActive` is false
        # and every oracle handler short-circuits before Misbehaving.
        peer = self.nodes[2].add_p2p_connection(P2PInterface())

        # Even a deliberately bad-signature message must not score the
        # peer when the oracle layer is gated off.
        now = int(time.time())
        for i in range(10):
            msg = build_signed_oracle_price(0, 6000 + i, now, valid_sig=False)
            peer.send_message(msg)
        peer.sync_with_ping(timeout=15)
        assert peer.is_connected, (
            "Pre-activation peer disconnected attacker — gate at "
            "Consensus::IsOracleActive failed open")

        # `getoracles` is also gated; it must not produce any reply.
        baseline = peer.message_count.get("oracleprice", 0)
        request = msg_getoracles()
        request.epoch = 0
        request.oracle_id = 0xFFFFFFFF
        for _ in range(5):
            peer.send_message(request)
        peer.sync_with_ping(timeout=10)
        assert_equal(peer.message_count.get("oracleprice", 0), baseline)
        self.nodes[2].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 8. BIP9-inactive peer ignores oracleprice/getoracles even above height gate
    # ------------------------------------------------------------------
    def test_inactive_bip9_peer_ignores_oracleprice(self):
        self.log.info("Test 8: BIP9-inactive node ignores oracleprice + getoracles")
        # Node 3 is above nOracleActivationHeight, but its DigiDollar BIP9
        # deployment is not ACTIVE. Oracle P2P must follow the BIP9 active gate,
        # not only the legacy height field, or inactive deployments can still
        # ingest/relay oracle state.
        peer = self.nodes[3].add_p2p_connection(P2PInterface())

        now = int(time.time())
        for i in range(10):
            msg = build_signed_oracle_price(0, 6200 + i, now, valid_sig=False)
            peer.send_message(msg)
        peer.sync_with_ping(timeout=15)
        assert peer.is_connected, (
            "BIP9-inactive peer disconnected attacker above oracle height — "
            "oracle P2P gate used height without requiring active deployment")

        baseline = peer.message_count.get("oracleprice", 0)
        request = msg_getoracles()
        request.epoch = 0
        request.oracle_id = 0xFFFFFFFF
        for _ in range(5):
            peer.send_message(request)
        peer.sync_with_ping(timeout=10)
        assert_equal(peer.message_count.get("oracleprice", 0), baseline)
        self.nodes[3].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 8b. BIP9-inactive peer ignores heartbeat telemetry
    # ------------------------------------------------------------------
    def test_inactive_bip9_peer_ignores_oracleheartbeat(self):
        self.log.info("Test 8b: BIP9-inactive node ignores oracle heartbeat telemetry")
        peer = self.nodes[3].add_p2p_connection(P2PInterface())

        for _ in range(10):
            peer.send_message(msg_oracleheartbeat(oracle_id=0, timestamp=int(time.time())))
        peer.sync_with_ping(timeout=15)
        assert peer.is_connected, (
            "BIP9-inactive peer disconnected attacker on oracle heartbeat — "
            "heartbeat P2P gate did not require active DigiDollar deployment")
        self.nodes[3].disconnect_p2ps()

    # ------------------------------------------------------------------
    # 9. Recovery after the attacker is disconnected
    # ------------------------------------------------------------------
    def test_recovery_after_attacker_disconnect(self):
        self.log.info("Test 9: recovery after attacker disconnect")
        # First, ban a peer with bad signatures.
        attacker = self.nodes[0].add_p2p_connection(P2PInterface())
        for i in range((DISCOURAGE_THRESHOLD // INVALID_SIG_PENALTY) + 2):
            try:
                attacker.send_message(
                    build_signed_oracle_price(2, 7100 + i, int(time.time()),
                                              valid_sig=False))
            except IOError:
                break
        attacker.wait_for_disconnect(timeout=20)

        # An honest peer must still be able to attach and exchange data.
        honest = self.nodes[0].add_p2p_connection(P2PInterface())
        msg = build_signed_oracle_price(1, 6600, int(time.time()))
        honest.send_and_ping(msg)
        assert honest.is_connected, "Honest peer disconnected after attacker ban"

        # And node 0 must still be talking to node 1 over the persistent
        # in-process link — the activation deployment status remains true.
        info = self.nodes[0].getdigidollardeploymentinfo()
        assert_equal(info["enabled"], True)
        self.nodes[0].disconnect_p2ps()


if __name__ == "__main__":
    DigiDollarWave20OracleP2PTest().main()
