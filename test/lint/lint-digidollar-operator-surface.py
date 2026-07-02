#!/usr/bin/env python3
#
# Copyright (c) 2026 The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Check DigiDollar operator-facing scripts/docs against current testnet/RPC surface."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

FORBIDDEN = {
    "deploy_testnet_oracle.sh": [
        "12028",
        "14028",
        "testnet5",
        "getoracleinfo",
        "ORACLE_PRIVATE_KEY",
        "<assigned_private_key_hex>",
        "0000000000000000000000000000000000000000000000000000000000000001",
    ],
    "docs/ORACLE_OPERATOR_GUIDE.md": [
        "raw_hex_private_key",
        "line 4821",
        "line 4959",
        "lines 975–976",
    ],
    "src/rpc/digidollar.cpp": [
        "Provide private_key parameter",
        "Oracle private key must be provided",
        "your_private_key_hex",
    ],
    "build_digibyte_ubuntu.sh": [
        "Testnet P2P:  12028",
        "RPC: 14028",
        "~/.digibyte/testnet5",
    ],
    "ORACLE_BUNDLE_EXPLAINER.md": [
        "getoracleinfo",
        "listoraclekeys",
        "How The 9 Oracles Are Selected",
        "If fewer than 9 valid testnet/mainnet V1 oracles are online and fresh",
    ],
    "digidollar/WALLET_MIGRATION_GUIDE.md": [
        "future testnet resets",
        "17-oracle list",
        "**rc30 (current)**",
    ],
    "docs/DIGIDOLLAR_TESTNET_ORACLE_SETUP.md": [
        "oracle=1",
        "oracleid=<your-slot-id>",
    ],
    "docs/DIGIDOLLAR_ORACLE_SETUP_COMPLETE_GUIDE.md": [
        "oracle=1",
        "oracleid=<oracle-id>",
    ],
    "DIGIDOLLAR_ORACLE_SETUP.md": [
        "startoracle <oracle_id> [private_key_hex]",
        "Oracle must be initialized via `startoracle` first",
    ],
    "digidollar/DIGIDOLLAR_TESTNET_BEGINNERS_GUIDE.md": [
        "17 configured oracle slots",
        "9-signature launch quorum",
        "Activates at Block 650",
        "activates at block 650",
        "after block 650",
        "reach block 650",
        ">= 650",
        "< 650",
    ],
    "DIGIDOLLAR_EXPLAINER.md": [
        "Testnet25 activates",
        "June 1, 2026 BIP9 start time is pending",
        "slots 0-16 consensus-active",
        "requires 9 BIP-340 Schnorr signatures",
        "require 9 MuSig2 Schnorr signatures",
        "OP_CHECKPRICE consults the live consensus price",
        "Live `g_get_oracle_consensus_price`",
        "ORACLE_CONSENSUS_REQUIRED=9",
    ],
    "DIGIDOLLAR_ACTIVATION_EXPLAINER.md": [
        "Testnet25 | `600`",
        "slots 0–16",
        "OP_CHECKPRICE` consults the live oracle consensus price",
        "`g_get_oracle_consensus_price` — fails closed",
        "oraclehb` heartbeats are the current telemetry exception",
        "Signed oracle version heartbeats are the current exception",
        "ORACLEHEARTBEAT` is not guarded",
        "`ORACLEHEARTBEAT` is authenticated/rate-limited but currently not height-gated",
    ],
    "DIGIDOLLAR_WALLET_INTEGRATION.md": [
        "wired to live consensus price",
        "no mock fallback in production",
        "currently not height-gated",
        "Local oracle public key/status after `startoracle`",
    ],
    "DIGIDOLLAR_ORACLE_EXPLAINER.md": [
        "Testnet25 | height 600",
    ],
    "digidollar/ROSTER_PLAN.md": [
        "Mainnet has 35 reserved oracle slots, 17 active pubkeys, and 9 required MuSig2 signers",
        "Testnet currently has 35 reserved oracle slots, 18 active pubkeys, and 9 required MuSig2 signers",
    ],
    "DIGIDOLLAR_ARCHITECTURE.md": [
        "Live-oracle consensus price",
        "OP_CHECKPRICE production wiring",
        "OP_CHECKPRICE wired to consensus",
        "OP_CHECKPRICE` consults the live consensus price",
        "OP_CHECKPRICE` consults the live oracle consensus price hook",
        "mainnet/testnet OP_CHECKPRICE and `ShouldBlockMinting` consult",
        "it consults live oracle consensus",
        "it requires a live oracle consensus price",
        "Wired to live consensus price via `g_get_oracle_consensus_price`",
        "Interpreter calls `g_get_oracle_consensus_price`",
        "g_get_oracle_consensus_price",
    ],
    "DIGIDOLLAR_ORACLE_ARCHITECTURE.md": [
        "g_get_oracle_consensus_price",
        "15 wallet-context DD/oracle RPCs",
        "OP_CHECKPRICE` consults the live oracle consensus price",
        "DigiDollar minting/redemption logic and `OP_CHECKPRICE` consult",
        "OP_CHECKPRICE has no mock fallback",
        "OP_CHECKPRICE` consults `g_get_oracle_consensus_price`",
        "one oracle relay handler that does not currently share",
        "heartbeat is authenticated telemetry without that height",
        "oraclehb` is signed/rate-limited telemetry without the same height gate",
        "Replaced by 9-signature mainnet/testnet MuSig2",
    ],
    "ARCHITECTURE.md": [
        "21 configured active keys",
    ],
    "CLAUDE.md": [
        "current code does not put the same height gate",
    ],
    "REPO_MAP_DIGIDOLLAR.md": [
        "Not a production fallback for `OP_CHECKPRICE`",
        "Production `OP_CHECKPRICE` reaches the live oracle consensus",
        "live oracle wired in",
        "currently has no height gate",
        "slot 0–16 ordering",
    ],
    "finish_oracle_setup.sh": [
        "getoracleinfo",
        "WALLET_NAME=\"${WALLET_NAME:-oracle_wallet}\"",
        "ORACLE_PRIVATE_KEY",
        "<assigned_private_key_hex>",
        "echo \"Oracle status:\"\n$CLI listoracle",
        "0000000000000000000000000000000000000000000000000000000000000001",
    ],
    "test_multi_oracle_testnet.sh": [
        "startoracle 0  \"$ORACLE_KEY_0\"  2>/dev/null || true",
        "mv \"$BOB_DATADIR/$TESTNET_SUBDIR/wallets/bob_original_backup\" \"$BOB_DATADIR/$TESTNET_SUBDIR/wallets/bob\"",
        "trap \"kill $BOB_PID",
        "pkill -f \"digibyte-qt.*testnet\"",
        "pkill -9 -f \"digibyte-qt.*testnet\"",
        "Below threshold rejection - 6-of-21 (Step 27B)",
        "Median filter with live exchange outlier evidence (Step 27C)",
        "Oracle recovery after disagreement (Step 27D)",
        "BOB_POSITIONS_BEFORE_EXPORT=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq 'length')",
        "BOB_RESTORED_POSITIONS=$($BOB_CLI -rpcwallet=bob_restored listdigidollarpositions 2>/dev/null | jq 'length')",
        "select(.is_active == false)",
        "select(.is_active == true)",
    ],
}

REQUIRED = {
    "finish_oracle_setup.sh": [
        "-testnet",
        "WALLET_NAME=\"${WALLET_NAME:-Oracle_Seed}\"",
        "listoracle",
    ],
    "build_digibyte_ubuntu.sh": [
        "Testnet P2P:  12033",
        "RPC: 14026",
        "~/.digibyte/testnet26",
    ],
    "ORACLE_BUNDLE_EXPLAINER.md": [
        "getoraclesigners",
        "listoracle",
    ],
    "digidollar/WALLET_MIGRATION_GUIDE.md": [
        "ARCHIVED RC30-ONLY",
        "Do not use this guide for RC44/testnet26",
        "DIGIDOLLAR_ORACLE_SETUP.md",
    ],
    "docs/DIGIDOLLAR_TESTNET_ORACLE_SETUP.md": [
        "digidollar=1",
        "createoraclekey <oracle_id>",
        "startoracle <oracle_id>",
    ],
    "docs/DIGIDOLLAR_ORACLE_SETUP_COMPLETE_GUIDE.md": [
        "digidollar=1",
        "Do not add `oracle` or `oracleid` config keys",
    ],
    "DIGIDOLLAR_ORACLE_SETUP.md": [
        "startoracle <oracle_id>",
        "Normal setup loads the oracle private key from the wallet",
    ],
    "digidollar/DIGIDOLLAR_TESTNET_BEGINNERS_GUIDE.md": [
        "35 active oracle slots",
        "7-signature launch quorum",
        "Activates at Block 600",
    ],
    "DIGIDOLLAR_EXPLAINER.md": [
        "testnet26",
        "mainnet activation remains gated",
        "35 active oracle slots",
        "requires 7 BIP-340 Schnorr signatures",
        "`OP_CHECKPRICE` is reserved and deterministically disabled",
        "ORACLE_CONSENSUS_REQUIRED=7",
    ],
    "DIGIDOLLAR_ACTIVATION_EXPLAINER.md": [
        "Testnet26 | `600`",
        "slots 0-34",
        "`OP_CHECKPRICE` is reserved and deterministically disabled",
        "including signed `oraclehb` heartbeats",
        "uses `IsOracleP2PActive`",
    ],
    "DIGIDOLLAR_WALLET_INTEGRATION.md": [
        "`OP_CHECKPRICE` is reserved and deterministically disabled",
        "`ORACLEHEARTBEAT` use `IsOracleP2PActive`",
    ],
    "DIGIDOLLAR_ORACLE_EXPLAINER.md": [
        "Testnet26 | height 600",
    ],
    "digidollar/ROSTER_PLAN.md": [
        "Mainnet and testnet have 35 reserved oracle slots, 35 active pubkeys, and 7 required MuSig2 signers",
    ],
    "DIGIDOLLAR_ARCHITECTURE.md": [
        "`OP_CHECKPRICE` is reserved and deterministically disabled",
    ],
    "DIGIDOLLAR_ORACLE_ARCHITECTURE.md": [
        "`OP_CHECKPRICE` is reserved and deterministically disabled",
        "17 wallet-context DD/oracle RPCs",
        "including `oraclehb`, share the `IsOracleP2PActive` gate",
        "35 active slots",
        "Replaced by 7-signature mainnet/testnet MuSig2",
    ],
    "CLAUDE.md": [
        "including `oraclehb`",
        "IsOracleP2PActive",
    ],
    "REPO_MAP_DIGIDOLLAR.md": [
        "`OP_CHECKPRICE` is reserved and deterministically disabled",
        "all use `IsOracleP2PActive`",
        "slot 0-34 ordering",
    ],
    "deploy_testnet_oracle.sh": [
        "TESTNET_NAME=\"testnet26\"",
        "TESTNET_P2P_PORT=12033",
        "TESTNET_RPC_PORT=14026",
        "TESTNET_GENESIS_HASH=\"0c9af936f28f7bd0e90c8f6235399063a026ed267bb53da398313b5d7aa55d82\"",
        "createoraclekey \"$ORACLE_ID\"",
        "install -d -m 700 \"$DATA_DIR\"",
        "chmod 600 \"$CONFIG_FILE\"",
        "ACTUAL_GENESIS=\\$(\\$CLI getblockhash 0 2>/dev/null || true)",
        "listoracle",
    ],
    "docs/ORACLE_OPERATOR_GUIDE.md": [
        "exportoracleprivkey <oracle_id>",
        "importoracleprivkey <oracle_id> <private_key_hex> [replace]",
        "walletpassphrase",
    ],
    "test_multi_oracle_testnet.sh": [
        "WARN_TESTS=0",
        "ORACLE_CACHE_REBUILT=false",
        "stop_existing_harness_processes",
        "cleanup_qt_nodes()",
        "start_oracle_checked",
        "bob_hash=$($BOB_CLI getbestblockhash",
        "[ \"$bob_hash\" = \"$alice_hash\" ]",
        "restorewallet \"bob\" \"$BACKUP_FILE\"",
        "if [ \"$HEARTBEAT_COUNT\" -eq 24 ]",
        "Manual oracle injection RPC removed (Step 27B)",
        "Live exchange outlier filter observation is optional",
        "BOB_POSITIONS_BEFORE_EXPORT=$($BOB_CLI -rpcwallet=bob listdigidollarpositions false",
        "BOB_RESTORED_POSITIONS=$($BOB_CLI -rpcwallet=bob_restored listdigidollarpositions false",
        "Bob restore fixture has no redeemed positions to validate",
        "def dd_active:",
        "Redeemed-position recovery coverage was incomplete",
        "DD transaction history restored partially; see category counts above",
    ],
}


def main() -> None:
    failures = []
    for relpath, needles in FORBIDDEN.items():
        text = (ROOT / relpath).read_text(encoding="utf8")
        for needle in needles:
            if needle in text:
                failures.append(f"{relpath}: forbidden stale operator surface `{needle}`")

    for relpath, needles in REQUIRED.items():
        text = (ROOT / relpath).read_text(encoding="utf8")
        for needle in needles:
            if needle not in text:
                failures.append(f"{relpath}: missing current operator surface `{needle}`")

    if failures:
        raise SystemExit("\n".join(failures))


if __name__ == "__main__":
    main()
