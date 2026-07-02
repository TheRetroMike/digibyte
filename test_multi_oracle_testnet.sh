#!/bin/bash
# DigiDollar Qt GUI TestNet Test with Live Oracle
# VERSION 18 (RC44): 7-of-35 MULTI-ORACLE + TESTNET26 RESET + ALL-TIER + TRANSFER CHAIN + WALLET PERSISTENCE
# Tests the full DigiDollar cycle on TestNet with real-time exchange price data
# Opens 8 SEPARATE Qt wallet instances hosting 24 active test oracles.
# The 7-of-35 chainparams threshold is used while slots 0-23 produce
# signed messages.
#
# ============================================================================
# LOCAL MINI-TESTNET MODE
# ============================================================================
# This script now starts every testnet node with -easypow.
# On testnet26 that debug flag does two things for local harnesses only:
#   1. Enables easy PoW so generatetoaddress can reach BIP9 height 600 quickly
#   2. Switches CTestNetParams to the local oracle key/node set on localhost
#
# Production testnet defaults remain unchanged unless -easypow is explicitly
# passed, so there is nothing manual to uncomment before running this script.
# ============================================================================
#
# TEST PLAN:
# - 8 wallet nodes (Bob, Alice, Charlie, Dave, Eve, Frank, Grace, Heidi)
# - 24 active oracles distributed across 8 nodes (slots 0-23)
# - 7-of-35 consensus threshold; 24 of 35 reserved slots actively sign
# - Bob mints $100 at tier 0, then $110 at tier 0 and tiers 1-9 = 11 mints total
# - Mine past tier 0 lock (240 blocks)
# - Bob redeems 2x tier 0 mints successfully
# - Test partial redemption (should FAIL)
# - Alice mints $100 at tier 3 (180 days) and tier 5 (2 years)
# - Charlie mints $100 at tier 7 (5 years) and tier 8 (7 years)
# - Comprehensive transfer chain: Bob->Alice($55), Alice->Charlie($22), Charlie->Bob($10), Bob->Charlie($5)
# - Full balance verification at EVERY step
# - Transaction confirmation verification
# - Network-wide DD supply and collateral tracking
#
# MULTI-ORACLE CONSENSUS TESTS (Step 27A-E):
# - 27A: 7/35 reserved oracle slots agree on price -> consensus PASSES
# - 27B: Verify manual sendoracleprice injection remains removed
# - 27C: Check live exchange outlier-filter evidence when an outlier occurs
# - 27D: Verify continued oracle consensus after live price refresh
# - 27E: Verify on-chain v0x03 oracle bundle byte size and prefix in coinbase
#
# WALLET PERSISTENCE TESTS:
# - Step 28: WALLET RESTART - Stop Bob's Qt, restart, verify DD balances persist
# - Step 29: WALLET BACKUP/RESTORE - Backup wallet, restore, verify DD balances
# - Step 30: REINDEX TEST - Stop node, restart with -reindex, verify DD rebuilt
# - Step 31-33: Alice rescan / reindex / export-reimport tests
# - Step 34: Bob export-reimport test (full DD tx history restoration)

set -e

# Create log file with timestamp
LOG_DIR="/tmp/digidollar_debug_logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/test_run_$(date +%Y%m%d_%H%M%S).log"
echo "=========================================="
echo "DigiDollar Qt TestNet Automated Test"
echo "VERSION 18 (RC44) - TESTNET26 RESET + 7-of-35 MULTI-ORACLE + ALL-TIER + TRANSFER CHAIN + WALLET PERSISTENCE"
echo "=========================================="
echo "Log file: $LOG_FILE"
echo ""

# Tee output to both console and log file
exec > >(tee -a "$LOG_FILE") 2>&1

echo "=========================================="
echo "DigiDollar Qt TestNet Automated Test"
echo "With 8 SEPARATE Qt GUI Instances (24 active oracles across 8 nodes)"
echo "Using LIVE Oracle Price Data"
echo "VERSION 18 (RC44): TESTNET26 RESET + 7-of-35 MULTI-ORACLE + ALL-TIER + TRANSFER + WALLET PERSISTENCE"
echo "=========================================="
echo "Test started: $(date)"
echo ""

# ============================================================================
# Configuration - Multi-Oracle Keys (7-of-35 threshold, RC44)
# ============================================================================
# Deterministic keys derived from SHA256("digibyte_testnet_oracle_N"), N=0..23.
# The x-only pubkeys corresponding to these privkeys are in the
# "FOR LOCAL MINI-TESTNET TESTING" block of src/kernel/chainparams.cpp
# (selected automatically when -easypow local mini-testnet mode is used).
ORACLE_KEY_0="952f219b8442ac40e5d356c0dbf7a76d81904d196e859a75b63dfb02346501fe"
ORACLE_KEY_1="7ede2d9b28569bcca3576e7982ce778cd3be4a6a0f1c99472c057e81cdebb0d8"
ORACLE_KEY_2="dc025fb2dab1cb3bbb3a6763e53b32e7d6fa8c39c16465e3aae80dcaf684adfe"
ORACLE_KEY_3="2e2e060fdcd5e26f9c5dc1ba422d9b1a7b33356c156fd128da453719e466e7ac"
ORACLE_KEY_4="8d347622f08b18341a8edb94e6420e2c916fbacc43f7325219026734849a1d28"
ORACLE_KEY_5="f97bf972b029ebbecf6bb80cc484dc9aa99675301d6fa8d73415d7f5a0c46b2b"
ORACLE_KEY_6="fad9aa44fe6b6203637c21f72ef74364bb1aefeace9fc1ef78182a7ce48daac0"
ORACLE_KEY_7="1ce799b137d7b6fb95393d2b71f5595ba3b37a805cb9e52ec6475f12011b9700"
ORACLE_KEY_8="767219a4c4d33be59322ecaa0b6593db2256b9c0151597f04d08a75b1b469eee"
ORACLE_KEY_9="6f4fdc8ef95905436175cd9048c182aa0d1dc797711ff262916de0c31420bd66"
ORACLE_KEY_10="058ef136fd30f2946697ca8f0c9a44fa4edec180a660eb7bb49e8548d91df5f9"
ORACLE_KEY_11="f823e53ca05fd336758746da3a66cbe02ecb36bba91f1f5b10cdfae3e4e5977e"
ORACLE_KEY_12="72cbee89ae2e896589bdaa982b1afa318c043bfbbe13a4f6a554f96eb39187fb"
ORACLE_KEY_13="49e45a508a4efd9610a717fd7d6baa7e34716f984f6464550bd09cfbb9f2ead5"
ORACLE_KEY_14="5fcb239f8cd649f8b5a681f86a7a69aca8ecacb76798658fbd64b36115916c33"
ORACLE_KEY_15="a080aacffc0b681952cc7d9f0a698ae8e0ac8604e92abd8c1e1392d94fa7e20f"
ORACLE_KEY_16="e2cf94f4a32b332b7c852ba5e00e0caf41e793cabfc048b5b0d3eed4366c585f"
ORACLE_KEY_17="3dceefc82c19a97fa44944d51d02ec6ef95f97e40565e99a23f4f13780811423"
ORACLE_KEY_18="aeeacbbe1b857d3e836ac9afac3a239c99f5621d7b15a5b4815d26a7a1838ad6"
ORACLE_KEY_19="713dc5f2c88fea142e95a64a28c9efa8446a25e00bd0fcabc1b8595e45541f4a"
ORACLE_KEY_20="0f01648fcde31421e050a59f81c04381c5d1f7d97a3966e136893ce8f2351a8b"
ORACLE_KEY_21="d066295c428f196ec9b4a337c6cda850bc1579dddff6386f60d499ce21db4d27"
ORACLE_KEY_22="83459560aadc6921bb822a7123e79b8f449df685ea9a92fdaece8ad1a6842f34"
ORACLE_KEY_23="67afbf7a5b9f2874c77d297ec3d01d500a75aae1f49c0454214bba9c4e0dc474"

# ============================================================================
# Mini Testnet ports (8 nodes hosting 24 active oracles — 7-of-35 consensus, RC44)
# ============================================================================
# Oracle distribution:
#   Bob     : oracles 0, 1, 16, 18  (4 oracles)
#   Alice   : oracles 2, 3, 17, 19  (4 oracles)
#   Charlie : oracles 4, 5, 20      (3 oracles)
#   Dave    : oracles 6, 7, 21  (3 oracles)
#   Eve     : oracles 8, 9, 22  (3 oracles)
#   Frank   : oracles 10, 11, 23 (3 oracles)
#   Grace   : oracles 12, 13    (2 oracles)
#   Heidi   : oracles 14, 15    (2 oracles)
# Total: 24 active oracles across 8 nodes signing 24 of 35 slots; the 7-of-35
# chainparams threshold is used. MuSig2 nonces flow over real P2P.
BOB_PORT=12027
BOB_RPC=14027
ALICE_PORT=12029
ALICE_RPC=14029
CHARLIE_PORT=12030
CHARLIE_RPC=14030
DAVE_PORT=12034
DAVE_RPC=14034
# Keep the local mini-testnet off the public testnet26 default port (12033) so
# this harness can run while a normal testnet node is open.
EVE_PORT=12036
EVE_RPC=14033
FRANK_PORT=12035
FRANK_RPC=14035
GRACE_PORT=12037
GRACE_RPC=14037
HEIDI_PORT=12039
HEIDI_RPC=14039

# Data directories
BOB_DATADIR="/tmp/bob_minitestnet"
ALICE_DATADIR="/tmp/alice_minitestnet"
CHARLIE_DATADIR="/tmp/charlie_minitestnet"
DAVE_DATADIR="/tmp/dave_minitestnet"
EVE_DATADIR="/tmp/eve_minitestnet"
FRANK_DATADIR="/tmp/frank_minitestnet"
GRACE_DATADIR="/tmp/grace_minitestnet"
HEIDI_DATADIR="/tmp/heidi_minitestnet"
TESTNET_SUBDIR="testnet26"

# CLI commands
BOB_CLI="./src/digibyte-cli -testnet -datadir=$BOB_DATADIR -rpcport=$BOB_RPC"
ALICE_CLI="./src/digibyte-cli -testnet -datadir=$ALICE_DATADIR -rpcport=$ALICE_RPC"
CHARLIE_CLI="./src/digibyte-cli -testnet -datadir=$CHARLIE_DATADIR -rpcport=$CHARLIE_RPC"
DAVE_CLI="./src/digibyte-cli -testnet -datadir=$DAVE_DATADIR -rpcport=$DAVE_RPC"
EVE_CLI="./src/digibyte-cli -testnet -datadir=$EVE_DATADIR -rpcport=$EVE_RPC"
FRANK_CLI="./src/digibyte-cli -testnet -datadir=$FRANK_DATADIR -rpcport=$FRANK_RPC"
GRACE_CLI="./src/digibyte-cli -testnet -datadir=$GRACE_DATADIR -rpcport=$GRACE_RPC"
HEIDI_CLI="./src/digibyte-cli -testnet -datadir=$HEIDI_DATADIR -rpcport=$HEIDI_RPC"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

# EXPECTED DD balances (updated after each operation)
EXPECT_BOB_DD=0
EXPECT_ALICE_DD=0
EXPECT_CHARLIE_DD=0
EXPECT_DAVE_DD=0
EXPECT_EVE_DD=0
EXPECT_NETWORK_DD=0
ALLOW_DD_DISTRIBUTION_DRIFT=0

# Track mints for redemption
declare -A BOB_MINTS     # txid -> dd_amount
declare -A BOB_COLLATERAL # txid -> collateral_dgb
BOB_TIER0_MINT1=""
BOB_TIER0_MINT2=""
BOB_STRESS_MINTS=()
STRESS_MINT_COUNT=10
STRESS_DD_CENTS=10000
STRESS_DD_TOTAL=$((STRESS_DD_CENTS * STRESS_MINT_COUNT))

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
WARN_TESTS=0
ORACLE_CACHE_REBUILT=false

print_status() {
    local status=$1
    local message=$2
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [ "$status" = "ok" ]; then
        echo -e "${GREEN}[OK]${NC} $message"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    elif [ "$status" = "fail" ]; then
        echo -e "${RED}[FAIL]${NC} $message"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    elif [ "$status" = "warn" ]; then
        echo -e "${YELLOW}[WARN]${NC} $message"
        WARN_TESTS=$((WARN_TESTS + 1))
        TOTAL_TESTS=$((TOTAL_TESTS - 1))
    elif [ "$status" = "info" ]; then
        echo -e "${CYAN}[INFO]${NC} $message"
        TOTAL_TESTS=$((TOTAL_TESTS - 1))  # Don't count info as test
    else
        echo "[INFO] $message"
        TOTAL_TESTS=$((TOTAL_TESTS - 1))
    fi
}

print_header() {
    echo ""
    echo -e "${BLUE}==========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}==========================================${NC}"
}

print_subheader() {
    echo ""
    echo -e "${CYAN}--- $1 ---${NC}"
}

wait_for_rpc() {
    local cli=$1
    local name=$2
    local max=${3:-180}
    local last_error=""
    for i in $(seq 1 $max); do
        if last_error=$($cli getblockchaininfo 2>&1 >/dev/null); then
            return 0
        fi
        if [ $((i % 15)) -eq 0 ]; then
            echo "  Waiting for $name RPC warmup ($i/$max): $(echo "$last_error" | head -c 120)"
        fi
        sleep 2
    done
    echo "  $name RPC did not become ready. Last response: $(echo "$last_error" | head -c 200)"
    return 1
}

require_rpc_ready() {
    local cli=$1
    local name=$2
    local ready_message=$3
    local fail_message=${4:-"$name Qt failed to start"}

    if wait_for_rpc "$cli" "$name"; then
        print_status "ok" "$ready_message"
    else
        print_status "fail" "$fail_message"
        exit 1
    fi
}

port_is_listening() {
    local port=$1
    python3 - "$port" <<'PY'
import socket
import sys

port = int(sys.argv[1])
for host, family in (("127.0.0.1", socket.AF_INET), ("::1", socket.AF_INET6)):
    with socket.socket(family, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.2)
        if sock.connect_ex((host, port)) == 0:
            sys.exit(0)

sys.exit(1)
PY
}

check_minitestnet_ports() {
    local labels=(
        "Bob P2P" "Bob RPC"
        "Alice P2P" "Alice RPC"
        "Charlie P2P" "Charlie RPC"
        "Dave P2P" "Dave RPC"
        "Eve P2P" "Eve RPC"
        "Frank P2P" "Frank RPC"
        "Grace P2P" "Grace RPC"
        "Heidi P2P" "Heidi RPC"
    )
    local ports=(
        "$BOB_PORT" "$BOB_RPC"
        "$ALICE_PORT" "$ALICE_RPC"
        "$CHARLIE_PORT" "$CHARLIE_RPC"
        "$DAVE_PORT" "$DAVE_RPC"
        "$EVE_PORT" "$EVE_RPC"
        "$FRANK_PORT" "$FRANK_RPC"
        "$GRACE_PORT" "$GRACE_RPC"
        "$HEIDI_PORT" "$HEIDI_RPC"
    )
    local failed=0

    for i in "${!ports[@]}"; do
        if port_is_listening "${ports[$i]}"; then
            print_status "fail" "${labels[$i]} port ${ports[$i]} is already in use"
            failed=1
        fi
    done

    if [ "$failed" -ne 0 ]; then
        echo "Mini-testnet ports must be free before this harness starts."
        return 1
    fi

    print_status "ok" "Mini-testnet TCP ports are free"
}

# Get balances safely
get_dgb_balance() {
    local cli=$1
    local wallet=$2
    $cli -rpcwallet=$wallet getbalance 2>/dev/null || echo "0"
}

get_dd_balance() {
    local cli=$1
    local wallet=$2
    local raw
    raw=$($cli -rpcwallet=$wallet getdigidollarbalance 2>/dev/null || true)
    if [ -z "$raw" ]; then
        echo "0"
        return
    fi
    echo "$raw" | jq -r '.total // .confirmed // 0' 2>/dev/null || echo "0"
}

get_immature_balance() {
    local cli=$1
    local wallet=$2
    $cli -rpcwallet=$wallet getbalances 2>/dev/null | jq -r '.mine.immature // 0' 2>/dev/null || echo "0"
}

get_network_collateral() {
    $BOB_CLI getdigidollarstats 2>/dev/null | jq -r '.total_collateral_dgb // "0"' 2>/dev/null || echo "0"
}

get_network_dd_supply() {
    $BOB_CLI getdigidollarstats 2>/dev/null | jq -r '.total_dd_supply // 0' 2>/dev/null || echo "0"
}

wait_for_tx_confirmed() {
    local cli=$1
    local wallet=$2
    local txid=$3
    local miner_cli=$4
    local miner_addr=$5
    local label=$6
    local max_blocks=${7:-80}

    for i in $(seq 1 "$max_blocks"); do
        local confs
        confs=$($cli -rpcwallet=$wallet gettransaction "$txid" 2>/dev/null | jq -r '.confirmations // 0' 2>/dev/null || echo "0")
        if [ "$confs" -gt 0 ] 2>/dev/null; then
            $cli syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
            print_status "ok" "$label confirmed after $confs block(s)"
            return 0
        fi

        echo "  $label not confirmed yet; mining live-oracle block $i/$max_blocks..."
        $miner_cli generatetoaddress 1 "$miner_addr" 2000000000 "sha256d" >/dev/null 2>&1 || true
        $cli syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        sleep 2
    done

    print_status "fail" "$label tx $txid never confirmed after $max_blocks live-oracle blocks"
    echo "Mempool entry:"
    $cli getmempoolentry "$txid" 2>/dev/null || true
    echo "Wallet tx:"
    $cli -rpcwallet=$wallet gettransaction "$txid" 2>/dev/null || true
    echo "Recent DD miner/oracle log lines:"
    grep -E "CreateNewBlock\(\): skipping DD tx|Added MuSig2 v0x03 bundle|Added cached MuSig2 v0x03 bundle|No price available in cache|MuSig2.*COMPLETE" "$BOB_DATADIR/$TESTNET_SUBDIR/debug.log" 2>/dev/null | tail -40 || true
    return 1
}

wait_for_dd_position_active() {
    local cli=$1
    local wallet=$2
    local txid=$3
    local expected_amount=$4
    local label=$5
    local max_wait=${6:-45}

    for i in $(seq 1 "$max_wait"); do
        $cli syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        local pos status confs amount
        pos=$($cli -rpcwallet=$wallet listdigidollarpositions false 2>/dev/null \
            | jq -r --arg txid "$txid" '.[] | select(.position_id == $txid) | "\(.status) \(.confirmations) \(.dd_minted)"' 2>/dev/null \
            | head -1)
        status=$(echo "$pos" | awk '{print $1}')
        confs=$(echo "$pos" | awk '{print $2}')
        amount=$(echo "$pos" | awk '{print $3}')

        if [ -n "$status" ] && [ "$status" != "pending" ] && [ "${confs:-0}" -gt 0 ] 2>/dev/null && [ "$amount" = "$expected_amount" ]; then
            print_status "ok" "$label DD position active ($amount cents, $confs confirmation(s))"
            return 0
        fi

        echo "  Waiting for $label DD position: status=${status:-missing} confs=${confs:-0} amount=${amount:-0} ($i/$max_wait)"
        sleep 1
    done

    print_status "fail" "$label DD position stayed pending/missing"
    $cli -rpcwallet=$wallet listdigidollarpositions false 2>/dev/null | jq . || true
    return 1
}

confirm_dd_mint() {
    local cli=$1
    local wallet=$2
    local txid=$3
    local amount=$4
    local label=$5
    local miner_cli=$6
    local miner_addr=$7

    wait_for_tx_confirmed "$cli" "$wallet" "$txid" "$miner_cli" "$miner_addr" "$label" 80 || exit 1
    wait_for_dd_position_active "$cli" "$wallet" "$txid" "$amount" "$label" 45 || exit 1
}

assert_wallet_tx_clean() {
    local cli=$1
    local wallet=$2
    local txid=$3
    local label=$4
    local txjson confs abandoned conflicts

    txjson=$($cli -rpcwallet=$wallet gettransaction "$txid" 2>/dev/null || echo "{}")
    confs=$(echo "$txjson" | jq -r '.confirmations // 0' 2>/dev/null || echo "0")
    abandoned=$(echo "$txjson" | jq -r '.abandoned // false' 2>/dev/null || echo "false")
    conflicts=$(echo "$txjson" | jq -r '(.walletconflicts // []) | length' 2>/dev/null || echo "0")

    if [ "$confs" -gt 0 ] 2>/dev/null && [ "$abandoned" = "false" ] && [ "$conflicts" = "0" ]; then
        print_status "ok" "$label clean wallet state (confirmed=$confs, conflicts=0, abandoned=false)"
        return 0
    fi

    print_status "fail" "$label dirty wallet state (confirmed=$confs, conflicts=$conflicts, abandoned=$abandoned)"
    echo "$txjson" | jq . 2>/dev/null || echo "$txjson"
    return 1
}

assert_positions_not_redeemed() {
    local cli=$1
    local wallet=$2
    local positions=$3
    local label=$4
    local redeemed=0
    local txid status

    for txid in $positions; do
        status=$($cli -rpcwallet=$wallet listdigidollarpositions false 2>/dev/null \
            | jq -r --arg txid "$txid" '.[] | select(.position_id == $txid) | .status' 2>/dev/null \
            | head -1)
        if [ "$status" = "redeemed" ]; then
            redeemed=$((redeemed + 1))
        fi
    done

    if [ "$redeemed" -eq 0 ]; then
        print_status "ok" "$label did not mark unrelated vaults as redeemed"
        return 0
    fi

    print_status "fail" "$redeemed unrelated vault(s) showed redeemed after $label"
    return 1
}

assert_no_pending_positions() {
    local cli=$1
    local wallet=$2
    local name=$3
    local pending
    pending=$($cli -rpcwallet=$wallet listdigidollarpositions false 2>/dev/null \
        | jq '[.[] | select(.status == "pending")] | length' 2>/dev/null || echo "0")
    if [ "$pending" != "0" ]; then
        print_status "fail" "$name has $pending pending DD position(s)"
        $cli -rpcwallet=$wallet listdigidollarpositions false 2>/dev/null | jq . || true
        exit 1
    fi
    print_status "ok" "$name has no pending DD positions"
}

# VERIFY DD BALANCE with expected value
verify_dd_balance() {
    local name=$1
    local cli=$2
    local wallet=$3
    local expected=$4

    # Wallet/DD indexes can lag right after fast block generation.
    # Flush validation queue and retry briefly before declaring failure.
    local actual="0"
    local attempts=0
    local max_attempts=20
    while [ $attempts -lt $max_attempts ]; do
        $cli syncwithvalidationinterfacequeue > /dev/null 2>&1 || true
        actual=$(get_dd_balance "$cli" "$wallet")
        if [ "$actual" = "$expected" ]; then
            break
        fi
        attempts=$((attempts + 1))
        sleep 1
    done

    if [ "$actual" = "$expected" ]; then
        echo -e "  ${GREEN}[OK]${NC} $name DD: $actual cents (expected: $expected)"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
        return 0
    else
        echo -e "  ${RED}[FAIL]${NC} $name DD: $actual cents (EXPECTED: $expected)"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
        return 1
    fi
}

# COMPREHENSIVE BALANCE VERIFICATION at each step
verify_all_balances() {
    local step_desc=$1
    local block_height=$($BOB_CLI getblockcount 2>/dev/null || echo "0")

    print_header "VERIFICATION: $step_desc (Block $block_height)"

    echo ""
    echo "========== DIGIBYTE (DGB) BALANCES =========="

    # Get all DGB balances
    local bob_dgb=$(get_dgb_balance "$BOB_CLI" "bob")
    local bob_immature=$(get_immature_balance "$BOB_CLI" "bob")
    local alice_dgb=$(get_dgb_balance "$ALICE_CLI" "alice")
    local alice_immature=$(get_immature_balance "$ALICE_CLI" "alice")
    local charlie_dgb=$(get_dgb_balance "$CHARLIE_CLI" "charlie")
    local charlie_immature=$(get_immature_balance "$CHARLIE_CLI" "charlie")

    echo "  BOB:     $bob_dgb DGB (immature: $bob_immature)"
    echo "  ALICE:   $alice_dgb DGB (immature: $alice_immature)"
    echo "  CHARLIE: $charlie_dgb DGB (immature: $charlie_immature)"

    echo ""
    echo "========== DIGIDOLLAR (DD) BALANCES =========="

    # Verify each DD balance against expected
    verify_dd_balance "BOB" "$BOB_CLI" "bob" "$EXPECT_BOB_DD"
    verify_dd_balance "ALICE" "$ALICE_CLI" "alice" "$EXPECT_ALICE_DD"
    verify_dd_balance "CHARLIE" "$CHARLIE_CLI" "charlie" "$EXPECT_CHARLIE_DD"

    local total_expected_dd=$((EXPECT_BOB_DD + EXPECT_ALICE_DD + EXPECT_CHARLIE_DD))
    local expected_network_dd=${EXPECT_NETWORK_DD:-$total_expected_dd}
    echo ""
    echo "  Total Expected DD: $total_expected_dd cents (\$$(echo "scale=2; $total_expected_dd / 100" | bc 2>/dev/null || echo "0"))"
    echo "  Expected Network DD (mint-redeem invariant): $expected_network_dd cents"

    echo ""
    echo "========== NETWORK STATS =========="

    # IMPORTANT: Sync validation interface queue before checking network stats
    # The DigiDollar stats index is updated asynchronously, so we need to wait
    # for it to process all pending block notifications. Without this, the stats
    # may lag behind by 1-2 blocks, causing supply mismatches.
    $BOB_CLI syncwithvalidationinterfacequeue 2>/dev/null
    sleep 1

    local network_dd=$(get_network_dd_supply)
    local network_collateral=$(get_network_collateral)
    local oracle_price=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.price_usd // "N/A"')

    echo "  Oracle Price: \$$oracle_price per DGB"
    echo "  Network DD Supply: $network_dd cents"
    echo "  Network Collateral: $network_collateral DGB"

    # Verify network DD matches mint-redeem conservation invariant
    if [ "$network_dd" = "$expected_network_dd" ]; then
        echo -e "  ${GREEN}[OK]${NC} Network DD supply matches invariant ($network_dd = $expected_network_dd)"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
    else
        echo -e "  ${RED}[FAIL]${NC} Network DD supply mismatch: $network_dd != invariant $expected_network_dd"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
    fi

    # Optional distribution check for tracked wallets only.
    if [ "$network_dd" != "$total_expected_dd" ]; then
        local drift=$((network_dd - total_expected_dd))
        if [ "$ALLOW_DD_DISTRIBUTION_DRIFT" -eq 1 ]; then
            echo -e "  ${YELLOW}[WARN]${NC} Tracked wallet distribution differs from network by $drift cents (expected once restored wallets participate)"
        else
            echo -e "  ${RED}[FAIL]${NC} Tracked wallet distribution mismatch: tracked=$total_expected_dd network=$network_dd (drift=$drift)"
            FAILED_TESTS=$((FAILED_TESTS + 1))
            TOTAL_TESTS=$((TOTAL_TESTS + 1))
        fi
    fi

    echo "=========================================="
}

# List DD positions for a wallet
list_dd_positions() {
    local cli=$1
    local wallet=$2
    local name=$3

    print_subheader "$name's DD Positions"

    local positions=$($cli -rpcwallet=$wallet listdigidollarpositions false 2>/dev/null || echo "[]")

    if [ "$positions" = "[]" ] || [ -z "$positions" ]; then
        echo "  No DD positions found"
        return
    fi

    local count=$(echo "$positions" | jq 'length')
    echo "  Total positions: $count"

    echo "$positions" | jq -r '.[] | "    [\(.status)] \(.dd_minted) cents - \(.dgb_collateral) DGB - tier \(.lock_tier) - \(.position_id[0:12])..."' 2>/dev/null || echo "  Error parsing positions"
}

refresh_local_p2p_links() {
    # The local mini-testnet intentionally disables discovery, so refreshed
    # manual links keep restart/reindex tests from depending on reconnect timing.
    $BOB_CLI     addnode "127.0.0.1:$ALICE_PORT"   onetry >/dev/null 2>&1 || true
    $BOB_CLI     addnode "127.0.0.1:$CHARLIE_PORT" onetry >/dev/null 2>&1 || true
    $BOB_CLI     addnode "127.0.0.1:$DAVE_PORT"    onetry >/dev/null 2>&1 || true
    $BOB_CLI     addnode "127.0.0.1:$EVE_PORT"     onetry >/dev/null 2>&1 || true
    $BOB_CLI     addnode "127.0.0.1:$FRANK_PORT"   onetry >/dev/null 2>&1 || true
    $BOB_CLI     addnode "127.0.0.1:$GRACE_PORT"   onetry >/dev/null 2>&1 || true
    $BOB_CLI     addnode "127.0.0.1:$HEIDI_PORT"   onetry >/dev/null 2>&1 || true

    $ALICE_CLI   addnode "127.0.0.1:$BOB_PORT" onetry >/dev/null 2>&1 || true
    $CHARLIE_CLI addnode "127.0.0.1:$BOB_PORT" onetry >/dev/null 2>&1 || true
    $DAVE_CLI    addnode "127.0.0.1:$BOB_PORT" onetry >/dev/null 2>&1 || true
    $EVE_CLI     addnode "127.0.0.1:$BOB_PORT" onetry >/dev/null 2>&1 || true
    $FRANK_CLI   addnode "127.0.0.1:$BOB_PORT" onetry >/dev/null 2>&1 || true
    $GRACE_CLI   addnode "127.0.0.1:$BOB_PORT" onetry >/dev/null 2>&1 || true
    $HEIDI_CLI   addnode "127.0.0.1:$BOB_PORT" onetry >/dev/null 2>&1 || true
}

# Sync all nodes to the same height (8 nodes total for RC44)
sync_all_nodes() {
    echo "Syncing all 8 nodes to a common tip..."

    for i in {1..180}; do
        $BOB_CLI     syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        $ALICE_CLI   syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        $CHARLIE_CLI syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        $DAVE_CLI    syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        $EVE_CLI     syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        $FRANK_CLI   syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        $GRACE_CLI   syncwithvalidationinterfacequeue >/dev/null 2>&1 || true
        $HEIDI_CLI   syncwithvalidationinterfacequeue >/dev/null 2>&1 || true

        local bob_height=$($BOB_CLI getblockcount 2>/dev/null || echo "0")
        local alice_height=$($ALICE_CLI getblockcount 2>/dev/null || echo "0")
        local charlie_height=$($CHARLIE_CLI getblockcount 2>/dev/null || echo "0")
        local dave_height=$($DAVE_CLI getblockcount 2>/dev/null || echo "0")
        local eve_height=$($EVE_CLI getblockcount 2>/dev/null || echo "0")
        local frank_height=$($FRANK_CLI getblockcount 2>/dev/null || echo "0")
        local grace_height=$($GRACE_CLI getblockcount 2>/dev/null || echo "0")
        local heidi_height=$($HEIDI_CLI getblockcount 2>/dev/null || echo "0")
        local bob_hash=$($BOB_CLI getbestblockhash 2>/dev/null || echo "")
        local alice_hash=$($ALICE_CLI getbestblockhash 2>/dev/null || echo "")
        local charlie_hash=$($CHARLIE_CLI getbestblockhash 2>/dev/null || echo "")
        local dave_hash=$($DAVE_CLI getbestblockhash 2>/dev/null || echo "")
        local eve_hash=$($EVE_CLI getbestblockhash 2>/dev/null || echo "")
        local frank_hash=$($FRANK_CLI getbestblockhash 2>/dev/null || echo "")
        local grace_hash=$($GRACE_CLI getbestblockhash 2>/dev/null || echo "")
        local heidi_hash=$($HEIDI_CLI getbestblockhash 2>/dev/null || echo "")

        if [ "$bob_height" = "$alice_height" ] && \
           [ "$bob_height" = "$charlie_height" ] && \
           [ "$bob_height" = "$dave_height" ] && \
           [ "$bob_height" = "$eve_height" ] && \
           [ "$bob_height" = "$frank_height" ] && \
           [ "$bob_height" = "$grace_height" ] && \
           [ "$bob_height" = "$heidi_height" ] && \
           [ "$bob_hash" = "$alice_hash" ] && \
           [ "$bob_hash" = "$charlie_hash" ] && \
           [ "$bob_hash" = "$dave_hash" ] && \
           [ "$bob_hash" = "$eve_hash" ] && \
           [ "$bob_hash" = "$frank_hash" ] && \
           [ "$bob_hash" = "$grace_hash" ] && \
           [ "$bob_hash" = "$heidi_hash" ] && \
           [ -n "$bob_hash" ]; then
            echo "[SYNC OK] All nodes at height $bob_height (${bob_hash:0:16}...)"
            return 0
        fi

        if [ $((i % 10)) -eq 0 ]; then
            echo "  Sync wait $i/180 | bob=$bob_height alice=$alice_height charlie=$charlie_height dave=$dave_height eve=$eve_height frank=$frank_height grace=$grace_height heidi=$heidi_height"
        fi
        if [ $((i % 30)) -eq 0 ]; then
            echo "  Refreshing local P2P links during sync wait..."
            refresh_local_p2p_links
        fi
        sleep 2
    done

    echo "Warning: Nodes did not converge to a common tip"
    echo "  Final heights: bob=$bob_height alice=$alice_height charlie=$charlie_height dave=$dave_height eve=$eve_height frank=$frank_height grace=$grace_height heidi=$heidi_height"
    echo "  Final hashes: bob=${bob_hash:0:16} alice=${alice_hash:0:16} charlie=${charlie_hash:0:16} dave=${dave_hash:0:16} eve=${eve_hash:0:16} frank=${frank_hash:0:16} grace=${grace_hash:0:16} heidi=${heidi_hash:0:16}"
    return 1
}

# Oracle distribution across nodes (RC44: 24 active oracles, 7-of-35 consensus):
#   Bob     : oracles 0, 1, 16, 18  (4 oracles)
#   Alice   : oracles 2, 3, 17, 19  (4 oracles)
#   Charlie : oracles 4, 5, 20      (3 oracles)
#   Dave    : oracles 6, 7, 21  (3 oracles)
#   Eve     : oracles 8, 9, 22  (3 oracles)
#   Frank   : oracles 10, 11, 23 (3 oracles)
#   Grace   : oracles 12, 13    (2 oracles)
#   Heidi   : oracles 14, 15    (2 oracles)
# Total: 24 active oracles across 8 nodes (24 of 35 slots can sign; 7 required).

refresh_oracle_prices() {
    # Oracle prices come EXCLUSIVELY from live exchange aggregation.
    # No fake price injection — oracles fetch from Binance, KuCoin, Gate.io,
    # Crypto.com, etc. via MultiExchangeAggregator.
    # Just mine blocks so oracle price threads broadcast and bundles form.
    $BOB_CLI generatetoaddress 6 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
    sleep 3  # Give oracle price threads time to fetch + broadcast
}

start_oracle_checked() {
    local cli=$1
    local oracle_id=$2
    local oracle_key=$3
    local host=$4
    local result exit_code

    set +e
    result=$($cli startoracle "$oracle_id" "$oracle_key" 2>&1)
    exit_code=$?
    set -e

    if [ $exit_code -eq 0 ] && echo "$result" | jq -e '.success == true and (.status == "running" or .was_already_running == true)' >/dev/null 2>&1; then
        print_status "ok" "Oracle slot $oracle_id started on $host"
        return 0
    fi

    print_status "fail" "Oracle slot $oracle_id failed to start on $host: $(echo "$result" | head -c 220)"
    return 1
}

start_all_oracles() {
    # Distribute 24 active oracles across all 8 nodes (7-of-35 threshold, RC44).
    # Bob: oracles 0, 1, 16, 18
    start_oracle_checked "$BOB_CLI"     0  "$ORACLE_KEY_0"  "Bob" || exit 1
    start_oracle_checked "$BOB_CLI"     1  "$ORACLE_KEY_1"  "Bob" || exit 1
    # Alice: oracles 2, 3, 17, 19
    start_oracle_checked "$ALICE_CLI"   2  "$ORACLE_KEY_2"  "Alice" || exit 1
    start_oracle_checked "$ALICE_CLI"   3  "$ORACLE_KEY_3"  "Alice" || exit 1
    # Charlie: oracles 4, 5, 20
    start_oracle_checked "$CHARLIE_CLI" 4  "$ORACLE_KEY_4"  "Charlie" || exit 1
    start_oracle_checked "$CHARLIE_CLI" 5  "$ORACLE_KEY_5"  "Charlie" || exit 1
    # Dave: oracles 6, 7, 21
    start_oracle_checked "$DAVE_CLI"    6  "$ORACLE_KEY_6"  "Dave" || exit 1
    start_oracle_checked "$DAVE_CLI"    7  "$ORACLE_KEY_7"  "Dave" || exit 1
    # Eve: oracles 8, 9, 22
    start_oracle_checked "$EVE_CLI"     8  "$ORACLE_KEY_8"  "Eve" || exit 1
    start_oracle_checked "$EVE_CLI"     9  "$ORACLE_KEY_9"  "Eve" || exit 1
    # Frank: oracles 10, 11, 23
    start_oracle_checked "$FRANK_CLI"   10 "$ORACLE_KEY_10" "Frank" || exit 1
    start_oracle_checked "$FRANK_CLI"   11 "$ORACLE_KEY_11" "Frank" || exit 1
    # Grace: oracles 12, 13
    start_oracle_checked "$GRACE_CLI"   12 "$ORACLE_KEY_12" "Grace" || exit 1
    start_oracle_checked "$GRACE_CLI"   13 "$ORACLE_KEY_13" "Grace" || exit 1
    # Heidi: oracles 14, 15
    start_oracle_checked "$HEIDI_CLI"   14 "$ORACLE_KEY_14" "Heidi" || exit 1
    start_oracle_checked "$HEIDI_CLI"   15 "$ORACLE_KEY_15" "Heidi" || exit 1
    start_oracle_checked "$BOB_CLI"     16 "$ORACLE_KEY_16" "Bob" || exit 1
    start_oracle_checked "$ALICE_CLI"   17 "$ORACLE_KEY_17" "Alice" || exit 1
    start_oracle_checked "$BOB_CLI"     18 "$ORACLE_KEY_18" "Bob" || exit 1
    start_oracle_checked "$ALICE_CLI"   19 "$ORACLE_KEY_19" "Alice" || exit 1
    start_oracle_checked "$CHARLIE_CLI" 20 "$ORACLE_KEY_20" "Charlie" || exit 1
    start_oracle_checked "$DAVE_CLI"    21 "$ORACLE_KEY_21" "Dave" || exit 1
    start_oracle_checked "$EVE_CLI"     22 "$ORACLE_KEY_22" "Eve" || exit 1
    start_oracle_checked "$FRANK_CLI"   23 "$ORACLE_KEY_23" "Frank" || exit 1
}

stop_qt_node() {
    local node_name="$1"
    local pid="$2"
    local cli_cmd="$3"
    local context="$4"

    echo "Requesting ${node_name}'s Qt shutdown via RPC stop${context:+ for $context}..."
    set +e
    local stop_result
    stop_result=$($cli_cmd stop 2>&1)
    local stop_exit=$?
    set -e

    if [ $stop_exit -ne 0 ]; then
        echo "RPC stop was not accepted for ${node_name}: $stop_result"
        echo "Sending SIGTERM to ${node_name}'s Qt (PID: $pid)..."
        kill -TERM "$pid" 2>/dev/null || true
    fi

    echo "Waiting for ${node_name}'s Qt to shut down cleanly (60 seconds max)..."
    for i in {1..60}; do
        if ! ps -p "$pid" > /dev/null 2>&1; then
            set +e
            wait "$pid" 2>/dev/null
            local wait_exit=$?
            set -e
            if [ $wait_exit -eq 0 ]; then
                print_status "ok" "${node_name}'s Qt shut down cleanly${context:+ for $context} after $i seconds"
                return 0
            fi
            print_status "fail" "${node_name}'s Qt exited abnormally${context:+ for $context} (wait status $wait_exit)"
            return 1
        fi
        sleep 1
    done

    print_status "warn" "${node_name}'s Qt did not stop cleanly${context:+ for $context}; force killing"
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    sleep 2
    return 1
}

cleanup_qt_nodes() {
    if [ "${KEEP_QT_OPEN:-0}" = "1" ]; then
        return 0
    fi

    local pids=(
        "${BOB_PID:-}" "${ALICE_PID:-}" "${CHARLIE_PID:-}" "${DAVE_PID:-}"
        "${EVE_PID:-}" "${FRANK_PID:-}" "${GRACE_PID:-}" "${HEIDI_PID:-}"
    )
    local cli_cmds=(
        "$BOB_CLI" "$ALICE_CLI" "$CHARLIE_CLI" "$DAVE_CLI"
        "$EVE_CLI" "$FRANK_CLI" "$GRACE_CLI" "$HEIDI_CLI"
    )
    local names=(
        "Bob" "Alice" "Charlie" "Dave"
        "Eve" "Frank" "Grace" "Heidi"
    )

    for i in "${!pids[@]}"; do
        local pid="${pids[$i]}"
        if [[ "$pid" =~ ^[0-9]+$ ]] && ps -p "$pid" >/dev/null 2>&1; then
            echo "Requesting ${names[$i]}'s Qt shutdown..."
            ${cli_cmds[$i]} stop >/dev/null 2>&1 || kill -TERM "$pid" 2>/dev/null || true

            for _ in {1..30}; do
                if ! ps -p "$pid" >/dev/null 2>&1; then
                    break
                fi
                sleep 1
            done

            if ps -p "$pid" >/dev/null 2>&1; then
                kill -TERM "$pid" 2>/dev/null || true
                sleep 2
            fi

            if ps -p "$pid" >/dev/null 2>&1; then
                kill -9 "$pid" 2>/dev/null || true
            fi

            wait "$pid" 2>/dev/null || true
        fi
    done
    echo "All tracked Qt testnet processes closed."
}

stop_existing_harness_processes() {
    local datadirs=(
        "$BOB_DATADIR" "$ALICE_DATADIR" "$CHARLIE_DATADIR" "$DAVE_DATADIR"
        "$EVE_DATADIR" "$FRANK_DATADIR" "$GRACE_DATADIR" "$HEIDI_DATADIR"
    )

    echo "Stopping existing local mini-testnet processes owned by this harness..."
    for datadir in "${datadirs[@]}"; do
        pkill -TERM -f "digibyte-qt.*-datadir=$datadir" 2>/dev/null || true
        pkill -TERM -f "digibyted.*-datadir=$datadir" 2>/dev/null || true
    done
    sleep 2

    for datadir in "${datadirs[@]}"; do
        pkill -9 -f "digibyte-qt.*-datadir=$datadir" 2>/dev/null || true
        pkill -9 -f "digibyted.*-datadir=$datadir" 2>/dev/null || true
    done
    sleep 1
}

trap cleanup_qt_nodes EXIT

# Tier descriptions (10 tiers: 0-9)
get_tier_description() {
    local tier=$1
    # Must match consensus/digidollar.h collateralRatios
    case $tier in
        0) echo "1 hour (240 blocks)" ;;
        1) echo "30 days" ;;
        2) echo "90 days" ;;
        3) echo "180 days" ;;
        4) echo "1 year" ;;
        5) echo "2 years" ;;
        6) echo "3 years" ;;
        7) echo "5 years" ;;
        8) echo "7 years" ;;
        9) echo "10 years" ;;
        *) echo "unknown" ;;
    esac
}

# ============================================================================
# MAIN TEST EXECUTION
# ============================================================================

# Step 1: Clean environment
print_header "Step 1: Cleaning environment"

# Kill only prior local mini-testnet processes that used this harness's
# temporary datadirs. Do not stop unrelated public testnet26 operators.
stop_existing_harness_processes
check_minitestnet_ports

# Clean up ALL test data directories to prevent stale wallet data issues
echo "Removing old test data directories (8 nodes)..."
rm -rf "$BOB_DATADIR" "$ALICE_DATADIR" "$CHARLIE_DATADIR" "$DAVE_DATADIR" "$EVE_DATADIR" \
       "$FRANK_DATADIR" "$GRACE_DATADIR" "$HEIDI_DATADIR"
rm -rf /tmp/bob_testnet*.log /tmp/alice_testnet*.log /tmp/charlie_testnet*.log \
       /tmp/dave_testnet*.log /tmp/eve_testnet*.log /tmp/frank_testnet*.log \
       /tmp/grace_testnet*.log /tmp/heidi_testnet*.log
rm -rf /tmp/bob_descriptors.json /tmp/alice_descriptors.json
rm -rf /tmp/bob_import_request.json /tmp/alice_import_request.json

# Verify directories are actually removed
if [ -d "$BOB_DATADIR" ]   || [ -d "$ALICE_DATADIR" ] || [ -d "$CHARLIE_DATADIR" ] || \
   [ -d "$DAVE_DATADIR" ]  || [ -d "$EVE_DATADIR" ]   || [ -d "$FRANK_DATADIR" ]   || \
   [ -d "$GRACE_DATADIR" ] || [ -d "$HEIDI_DATADIR" ]; then
    echo -e "${RED}WARNING: Failed to remove data directories. Retrying...${NC}"
    sleep 2
    rm -rf "$BOB_DATADIR" "$ALICE_DATADIR" "$CHARLIE_DATADIR" "$DAVE_DATADIR" "$EVE_DATADIR" \
           "$FRANK_DATADIR" "$GRACE_DATADIR" "$HEIDI_DATADIR"
fi

# Create fresh directories
mkdir -p "$BOB_DATADIR" "$ALICE_DATADIR" "$CHARLIE_DATADIR" "$DAVE_DATADIR" "$EVE_DATADIR" \
         "$FRANK_DATADIR" "$GRACE_DATADIR" "$HEIDI_DATADIR"
print_status "ok" "Clean environment ready (all stale data removed, 8 fresh datadirs)"

# Step 2: Start Bob's Qt node
print_header "Step 2: Starting Bob's Qt node"
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$BOB_DATADIR \
    -port=$BOB_PORT \
    -rpcport=$BOB_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    > /tmp/bob_testnet.log 2>&1 &
BOB_PID=$!
echo "Bob's Qt started (PID: $BOB_PID)"

require_rpc_ready "$BOB_CLI" "Bob" "Bob's Qt RPC is ready" "Bob's Qt failed to start"

# Step 3: Setup Bob's wallet and generate enough mature DGB for normal mints
# plus the rapid 20-mint stress batch. The live oracle price can move, so keep
# this intentionally high enough to test DD state handling instead of funding.
print_header "Step 3: Setting up Bob's wallet with sufficient DGB"
$BOB_CLI createwallet "bob" 2>/dev/null || true
BOB_ADDR=$($BOB_CLI -rpcwallet=bob getnewaddress "mining" "bech32")
echo "Bob's mining address: $BOB_ADDR"

# Bob needs DGB for the scripted tier mints and an additional 10x $100 tier-0
# rapid stress batch. Mining 3000 blocks gives the harness enough confirmed
# coinbase outputs and collateral headroom when live DGB prices are low.
echo "Mining 3000 blocks for Bob's coinbase maturity and DGB..."
$BOB_CLI generatetoaddress 3000 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
HEIGHT=$($BOB_CLI getblockcount)
print_status "ok" "Mined to height $HEIGHT"

BOB_BALANCE=$($BOB_CLI -rpcwallet=bob getbalance)
echo "Bob's DGB balance: $BOB_BALANCE DGB"

# Step 4: Oracle startup deferred to Step 8B (after BIP9 activation at height 600)
print_header "Step 4: Oracle startup deferred (BIP9 activates at height 600)"
echo "Oracles will be started after all nodes are synced past activation height."
print_status "ok" "Oracle startup deferred to Step 8B"

# Step 5: Start Alice's Qt node
print_header "Step 5: Starting Alice's Qt node"
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$ALICE_DATADIR \
    -port=$ALICE_PORT \
    -rpcport=$ALICE_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$BOB_PORT \
    > /tmp/alice_testnet.log 2>&1 &
ALICE_PID=$!
echo "Alice's Qt started (PID: $ALICE_PID)"

require_rpc_ready "$ALICE_CLI" "Alice" "Alice's Qt RPC is ready" "Alice's Qt failed to start"

$ALICE_CLI createwallet "alice" 2>/dev/null || true
ALICE_ADDR=$($ALICE_CLI -rpcwallet=alice getnewaddress "receive" "bech32")
echo "Alice's address: $ALICE_ADDR"

# Step 6: Start Charlie's Qt node
print_header "Step 6: Starting Charlie's Qt node"
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$CHARLIE_DATADIR \
    -port=$CHARLIE_PORT \
    -rpcport=$CHARLIE_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$BOB_PORT \
    > /tmp/charlie_testnet.log 2>&1 &
CHARLIE_PID=$!
echo "Charlie's Qt started (PID: $CHARLIE_PID)"

require_rpc_ready "$CHARLIE_CLI" "Charlie" "Charlie's Qt RPC is ready" "Charlie's Qt failed to start"

$CHARLIE_CLI createwallet "charlie" 2>/dev/null || true
CHARLIE_ADDR=$($CHARLIE_CLI -rpcwallet=charlie getnewaddress "receive" "bech32")
echo "Charlie's address: $CHARLIE_ADDR"

# Step 6B: Start Dave's Qt node (hosts oracles 6, 7)
print_header "Step 6B: Starting Dave's Qt node (hosts oracles 6, 7)"
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$DAVE_DATADIR \
    -port=$DAVE_PORT \
    -rpcport=$DAVE_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$BOB_PORT \
    > /tmp/dave_testnet.log 2>&1 &
DAVE_PID=$!
echo "Dave's Qt started (PID: $DAVE_PID)"

require_rpc_ready "$DAVE_CLI" "Dave" "Dave's Qt RPC is ready" "Dave's Qt failed to start"

$DAVE_CLI createwallet "dave" 2>/dev/null || true
DAVE_ADDR=$($DAVE_CLI -rpcwallet=dave getnewaddress "receive" "bech32")
echo "Dave's address: $DAVE_ADDR"

# Step 6C: Start Eve's Qt node (hosts oracles 8, 9)
print_header "Step 6C: Starting Eve's Qt node (hosts oracles 8, 9)"
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$EVE_DATADIR \
    -port=$EVE_PORT \
    -rpcport=$EVE_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$BOB_PORT \
    > /tmp/eve_testnet.log 2>&1 &
EVE_PID=$!
echo "Eve's Qt started (PID: $EVE_PID)"

require_rpc_ready "$EVE_CLI" "Eve" "Eve's Qt RPC is ready" "Eve's Qt failed to start"

$EVE_CLI createwallet "eve" 2>/dev/null || true
EVE_ADDR=$($EVE_CLI -rpcwallet=eve getnewaddress "receive" "bech32")
echo "Eve's address: $EVE_ADDR"

# Step 6D: Start Frank's Qt node (hosts oracles 10, 11)
print_header "Step 6D: Starting Frank's Qt node (hosts oracles 10, 11)"
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$FRANK_DATADIR \
    -port=$FRANK_PORT \
    -rpcport=$FRANK_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$BOB_PORT \
    > /tmp/frank_testnet.log 2>&1 &
FRANK_PID=$!
echo "Frank's Qt started (PID: $FRANK_PID)"

require_rpc_ready "$FRANK_CLI" "Frank" "Frank's Qt RPC is ready" "Frank's Qt failed to start"

$FRANK_CLI createwallet "frank" 2>/dev/null || true
FRANK_ADDR=$($FRANK_CLI -rpcwallet=frank getnewaddress "receive" "bech32")
echo "Frank's address: $FRANK_ADDR"

# Step 6E: Start Grace's Qt node (hosts oracles 12, 13)
print_header "Step 6E: Starting Grace's Qt node (hosts oracles 12, 13)"
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$GRACE_DATADIR \
    -port=$GRACE_PORT \
    -rpcport=$GRACE_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$BOB_PORT \
    > /tmp/grace_testnet.log 2>&1 &
GRACE_PID=$!
echo "Grace's Qt started (PID: $GRACE_PID)"

require_rpc_ready "$GRACE_CLI" "Grace" "Grace's Qt RPC is ready" "Grace's Qt failed to start"

$GRACE_CLI createwallet "grace" 2>/dev/null || true
GRACE_ADDR=$($GRACE_CLI -rpcwallet=grace getnewaddress "receive" "bech32")
echo "Grace's address: $GRACE_ADDR"

# Step 6F: Start Heidi's Qt node (hosts oracles 14, 15)
print_header "Step 6F: Starting Heidi's Qt node (hosts oracles 14, 15)"
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$HEIDI_DATADIR \
    -port=$HEIDI_PORT \
    -rpcport=$HEIDI_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$BOB_PORT \
    > /tmp/heidi_testnet.log 2>&1 &
HEIDI_PID=$!
echo "Heidi's Qt started (PID: $HEIDI_PID)"

require_rpc_ready "$HEIDI_CLI" "Heidi" "Heidi's Qt RPC is ready" "Heidi's Qt failed to start"

$HEIDI_CLI createwallet "heidi" 2>/dev/null || true
HEIDI_ADDR=$($HEIDI_CLI -rpcwallet=heidi getnewaddress "receive" "bech32")
echo "Heidi's address: $HEIDI_ADDR"

# Step 6G: Slots 16 and 17 are active in the local oracle roster and are
# started by start_all_oracles after BIP9 activation.

# Step 7: Fund non-Bob wallet nodes with DGB (so every oracle operator has
# spendable coins to exercise DD mint/redeem if needed). Bob retains the bulk
# of the funds because he drives most mints in Steps 10+.
print_header "Step 7: Funding Alice, Charlie, Dave, Eve, Frank, Grace, Heidi with DGB"
echo 'Alice needs DGB for 2 mints ($200 worth of collateral)'
echo 'Charlie needs DGB for 2 mints ($200 worth of collateral)'
echo "Dave, Eve, Frank, Grace, Heidi just need enough DGB to relay"
echo "oracle price txs and pay fees (they do not mint by default)."

# Mine blocks to Alice (100 blocks = 7.2M DGB)
echo "Mining 300 blocks to Alice..."
$BOB_CLI generatetoaddress 300 "$ALICE_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
print_status "ok" "Alice funded: 300 blocks mined"

# Mine blocks to Charlie (100 blocks = 7.2M DGB)
echo "Mining 300 blocks to Charlie..."
$BOB_CLI generatetoaddress 300 "$CHARLIE_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
print_status "ok" "Charlie funded: 300 blocks mined"

# Mine blocks to Dave (50 blocks = 3.6M DGB)
echo "Mining 50 blocks to Dave..."
$BOB_CLI generatetoaddress 50 "$DAVE_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
print_status "ok" "Dave funded: 50 blocks mined"

# Mine blocks to Eve (50 blocks = 3.6M DGB)
echo "Mining 50 blocks to Eve..."
$BOB_CLI generatetoaddress 50 "$EVE_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
print_status "ok" "Eve funded: 50 blocks mined"

# Mine 10 blocks each to the RC44 oracle hosts
# (plenty to pay fees; they do not mint DD).
for name in FRANK GRACE HEIDI; do
    addr_var="${name}_ADDR"
    echo "Mining 10 blocks to ${name}..."
    $BOB_CLI generatetoaddress 10 "${!addr_var}" 2000000000 "sha256d" > /dev/null 2>&1
    print_status "ok" "${name} funded: 10 blocks mined"
done

# Step 8: Sync chains
print_header "Step 8: Syncing chains"
$BOB_CLI generatetoaddress 5 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
sleep 5

sync_all_nodes
print_status "ok" "All nodes synced"

# Step 8B: Start oracles NOW (BIP9 is active, height > 600)
print_header "Step 8B: Starting 24 Live Oracles (BIP9 now active - 7-of-35 RC44)"
HEIGHT_8B=$($BOB_CLI getblockcount)
echo "Current height: $HEIGHT_8B (BIP9 activates at 600)"

# Verify BIP9 is active
DD_STATUS=$($BOB_CLI getdigidollardeploymentinfo 2>/dev/null | jq -r '.status // .deployments.digidollar.status // "unknown"' 2>/dev/null || echo "unknown")
echo "DigiDollar BIP9 status: $DD_STATUS"

start_all_oracles
sleep 2

# RC44: prove signed version heartbeats are flowing before relying on price
# consensus. Heartbeats are off-chain monitoring data, but they tell operators
# which oracle slots are live and upgraded.
echo "Waiting for signed oracle version heartbeats..."
HEARTBEAT_COUNT=0
for i in {1..20}; do
    HEARTBEAT_COUNT=$($BOB_CLI getoracles true 2>/dev/null \
        | jq '[.[] | select(.heartbeat_status == "fresh")] | length' 2>/dev/null || echo "0")
    if [ "$HEARTBEAT_COUNT" -eq 24 ] 2>/dev/null; then
        break
    fi
    echo "  Fresh oracle heartbeats seen: $HEARTBEAT_COUNT/24 ($i/20)"
    sleep 3
done

if [ "$HEARTBEAT_COUNT" -eq 24 ] 2>/dev/null; then
    print_status "ok" "Signed oracle version heartbeats visible from all 24 active slots"
else
    print_status "fail" "Expected all 24 active oracle heartbeats after startup, saw $HEARTBEAT_COUNT/24"
    $BOB_CLI getoracles true 2>/dev/null | jq '[.[] | {oracle_id, heartbeat_status, software_version, client_version, musig2_context_version}]' || true
    exit 1
fi

# Oracle prices come from LIVE exchange feeds (Binance, KuCoin, Gate.io, etc.)
# Mine blocks so oracle price threads broadcast and bundles form on-chain
echo "Waiting for live oracle price data from exchanges..."
refresh_oracle_prices

# Wait for oracle consensus
ORACLE_ACTIVE=false
for i in {1..20}; do
    ORACLE_PRICE_CHECK=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.price_usd // "0"')
    if [ "$ORACLE_PRICE_CHECK" != "0" ] && [ "$ORACLE_PRICE_CHECK" != "N/A" ]; then
        print_status "ok" "24 Live Oracles are active (7-of-35 threshold met)"
        ORACLE_ACTIVE=true
        break
    fi
    # Mine more blocks to give oracle price threads time to broadcast
    refresh_oracle_prices
    sleep 3
done

ORACLE_PRICE=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.price_usd // "N/A"')
echo "LIVE Oracle Price: \$$ORACLE_PRICE per DGB (from 7-of-35 oracle consensus)"

if [ "$ORACLE_ACTIVE" = "false" ]; then
    print_status "fail" "Oracle price still $0 after 20 attempts — oracle system not working"
    echo "Check debug log: "$BOB_DATADIR/$TESTNET_SUBDIR/debug.log""
    echo "Last oracle lines:"
    grep -i "oracle" "$BOB_DATADIR/$TESTNET_SUBDIR/debug.log" | tail -10
    exit 1
fi

sync_all_nodes

# ====================================================================================
# MuSig2 NONCE EXCHANGE TEST: Mine blocks slowly so P2P nonces can propagate
# ====================================================================================
print_header "Step 8C: MuSig2 Phase 3 Nonce Exchange"
echo "Mining blocks one-at-a-time to allow MuSig2 P2P nonce exchange..."
MUSIG_SUCCESS=false
MUSIG_START_HEIGHT=$($BOB_CLI getblockcount 2>/dev/null || echo 0)
MUSIG_LOG_MARK=$(wc -l < "$BOB_DATADIR/$TESTNET_SUBDIR/debug.log" 2>/dev/null || echo 0)

check_current_tip_for_musig_bundle() {
    MUSIG_BLOCK_HASH=$($BOB_CLI getbestblockhash 2>/dev/null || echo "")
    MUSIG_BLOCK_JSON=""
    MUSIG_ORACLE_HEX=""
    MUSIG_ORACLE_LEN_BYTES=0
    MUSIG_BLOCK_HEIGHT="unknown"

    if [ -n "$MUSIG_BLOCK_HASH" ]; then
        MUSIG_BLOCK_JSON=$($BOB_CLI getblock "$MUSIG_BLOCK_HASH" 2 2>/dev/null || echo "")
        MUSIG_BLOCK_HEIGHT=$(echo "$MUSIG_BLOCK_JSON" | jq -r '.height // "unknown"' 2>/dev/null || echo "unknown")
        MUSIG_ORACLE_HEX=$(echo "$MUSIG_BLOCK_JSON" | jq -r '[.tx[].vout[].scriptPubKey.hex // empty | select(startswith("6abf"))][0] // ""' 2>/dev/null || echo "")
        if [ -n "$MUSIG_ORACLE_HEX" ]; then
            MUSIG_ORACLE_LEN_BYTES=$(( ${#MUSIG_ORACLE_HEX} / 2 ))
        fi
    fi

    [[ "$MUSIG_ORACLE_HEX" == 6abf0103* && "$MUSIG_ORACLE_LEN_BYTES" -gt 60 ]]
}

if check_current_tip_for_musig_bundle; then
    MUSIG_SUCCESS=true
    print_status "ok" "Fresh MuSig2 v0x03 bundle already on current tip $MUSIG_BLOCK_HEIGHT (size=$MUSIG_ORACLE_LEN_BYTES bytes)"
fi

for i in {1..15}; do
    if [ "$MUSIG_SUCCESS" = "true" ]; then
        break
    fi

    $BOB_CLI generatetoaddress 1 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
    sleep 3  # Give P2P time to propagate nonces between nodes

    # Require a fresh on-chain v0x03 bundle, not a stale debug.log line from
    # earlier setup mining.
    if check_current_tip_for_musig_bundle; then
        MUSIG_SUCCESS=true
        print_status "ok" "Fresh MuSig2 v0x03 bundle mined at height $MUSIG_BLOCK_HEIGHT (size=$MUSIG_ORACLE_LEN_BYTES bytes)"
        break
    fi

    NEW_MUSIG_LOGS=$(tail -n +"$((MUSIG_LOG_MARK + 1))" "$BOB_DATADIR/$TESTNET_SUBDIR/debug.log" 2>/dev/null | grep "Added MuSig2 v0x03 bundle to block\|Added cached MuSig2 v0x03 bundle to block" | tail -1 || true)
    if [ -n "$NEW_MUSIG_LOGS" ]; then
        echo "  Fresh MuSig2 log seen but no v0x03 OP_ORACLE found on best block yet: $NEW_MUSIG_LOGS"
    fi
    echo "  Block $i mined, waiting for nonce exchange..."
done
if [ "$MUSIG_SUCCESS" = "false" ]; then
    print_status "fail" "No fresh on-chain MuSig2 v0x03 oracle bundle mined after 15 blocks from height $MUSIG_START_HEIGHT"
    echo "MuSig2 v0x03 bundle not yet produced; V1 does not accept legacy fallback bundles"
    echo "Nonce/context/signature status:"
    grep "Ingested remote nonce\|Stored MuSig2 context\|Broadcast MuSig2 context\|context mismatch\|Rejected MuSig2 context\|Step 2.*recomputed\|auto-aggregated\|MuSig2 COMPLETE" "$BOB_DATADIR/$TESTNET_SUBDIR/debug.log" 2>/dev/null | tail -40 || true
    exit 1
fi

sync_all_nodes

# ====================================================================================
# INITIAL STATE - All balances should be 0
# ====================================================================================
print_header "Step 9: Initial DigiDollar State"

# Expected: All DD balances = 0
EXPECT_BOB_DD=0
EXPECT_ALICE_DD=0
EXPECT_CHARLIE_DD=0
verify_all_balances "Initial State (No DD Minted)"

# ====================================================================================
# Step 10: BOB MINTS DD AT EVERY TIER (0-9) - First mint $100, rest $110 for DD change test
# ====================================================================================
print_header "Step 10: Bob Mints DD at ALL Collateral Tiers (0-9)"
echo ""
echo "Bob will mint \$100 (first tier 0) and \$110 (all others) to test DD change."
echo "Total: \$100 + 10x\$110 = \$1200 (120000 cents)"
echo ""

ORACLE_PRICE=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.price_usd')
echo "Current LIVE Oracle Price: \$$ORACLE_PRICE per DGB"
echo ""

# Mint $100 at tier 0 TWICE (for redemption testing later)
print_subheader "Tier 0 - First Mint (for redemption test)"
echo "Refreshing oracle prices before mint..."
refresh_oracle_prices
echo "Minting \$100 DD (10000 cents) with tier 0 [$(get_tier_description 0)]..."
MINT_RESULT=$($BOB_CLI -rpcwallet=bob mintdigidollar 10000 0 2>&1)

if echo "$MINT_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    BOB_TIER0_MINT1=$(echo "$MINT_RESULT" | jq -r '.txid')
    COLLATERAL=$(echo "$MINT_RESULT" | jq -r '.dgb_collateral')
    confirm_dd_mint "$BOB_CLI" "bob" "$BOB_TIER0_MINT1" 10000 "Bob tier 0 mint #1" "$BOB_CLI" "$BOB_ADDR"
    print_status "ok" "Tier 0 Mint #1: TX ${BOB_TIER0_MINT1:0:12}... Collateral: $COLLATERAL DGB"
    EXPECT_BOB_DD=$((EXPECT_BOB_DD + 10000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD + 10000))
else
    print_status "fail" "Tier 0 Mint #1 failed: $MINT_RESULT"
    exit 1
fi

print_subheader "Tier 0 - Second Mint (larger amount for DD change test)"
echo "Refreshing oracle prices before mint..."
refresh_oracle_prices
echo "Minting \$110 DD (11000 cents) with tier 0 [$(get_tier_description 0)]..."
MINT_RESULT=$($BOB_CLI -rpcwallet=bob mintdigidollar 11000 0 2>&1)

if echo "$MINT_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    BOB_TIER0_MINT2=$(echo "$MINT_RESULT" | jq -r '.txid')
    COLLATERAL=$(echo "$MINT_RESULT" | jq -r '.dgb_collateral')
    confirm_dd_mint "$BOB_CLI" "bob" "$BOB_TIER0_MINT2" 11000 "Bob tier 0 mint #2" "$BOB_CLI" "$BOB_ADDR"
    print_status "ok" "Tier 0 Mint #2: TX ${BOB_TIER0_MINT2:0:12}... Collateral: $COLLATERAL DGB"
    EXPECT_BOB_DD=$((EXPECT_BOB_DD + 11000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD + 11000))
else
    print_status "fail" "Tier 0 Mint #2 failed: $MINT_RESULT"
    exit 1
fi

# Now mint at tiers 1-9 (10 tiers total: 0-9) - Using $110 to test DD change scenario
for tier in 1 2 3 4 5 6 7 8 9; do
    print_subheader "Tier $tier - $(get_tier_description $tier)"
    echo "Refreshing oracle prices before mint..."
    refresh_oracle_prices
    echo "Minting \$110 DD (11000 cents) with tier $tier..."

    MINT_RESULT=$($BOB_CLI -rpcwallet=bob mintdigidollar 11000 $tier 2>&1)

    if echo "$MINT_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
        TXID=$(echo "$MINT_RESULT" | jq -r '.txid')
        COLLATERAL=$(echo "$MINT_RESULT" | jq -r '.dgb_collateral')
        confirm_dd_mint "$BOB_CLI" "bob" "$TXID" 11000 "Bob tier $tier mint" "$BOB_CLI" "$BOB_ADDR"
        print_status "ok" "Tier $tier Mint: TX ${TXID:0:12}... Collateral: $COLLATERAL DGB"
        EXPECT_BOB_DD=$((EXPECT_BOB_DD + 11000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD + 11000))
    else
        print_status "fail" "Tier $tier Mint failed: $MINT_RESULT"
        exit 1
    fi

done

# Sync and verify after all mints
sync_all_nodes
assert_no_pending_positions "$BOB_CLI" "bob" "Bob"

# Bob should have 1 x 10000 + 10 x 11000 = 120000 DD
echo ""
echo "Bob completed 11 mints (tier0 \$100, tier0 \$110, tiers 1-9 \$110 each)"
echo "Expected Bob DD: $EXPECT_BOB_DD cents (\$$(echo "scale=2; $EXPECT_BOB_DD / 100" | bc))"
verify_all_balances "After Bob's 11 Mints (\$1200 total)"
list_dd_positions "$BOB_CLI" "bob" "Bob"

# ====================================================================================
# Step 10B: RAPID BATCH MINT STRESS - tier 0 mints without mining between RPCs
# ====================================================================================
print_header "Step 10B: Rapid Batch Mint Stress (${STRESS_MINT_COUNT}x \$100 tier 0)"
echo "Submitting $STRESS_MINT_COUNT mintdigidollar RPCs back-to-back, then confirming them together."
echo "This verifies rapid mint input reservation does not create local conflicts."

refresh_oracle_prices
BOB_STRESS_MINTS=()
for i in $(seq 1 "$STRESS_MINT_COUNT"); do
    echo "  Rapid mint $i/$STRESS_MINT_COUNT..."
    set +e
    MINT_RESULT=$($BOB_CLI -rpcwallet=bob mintdigidollar "$STRESS_DD_CENTS" 0 2>&1)
    MINT_EXIT=$?
    set -e
    if [ $MINT_EXIT -eq 0 ] && echo "$MINT_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
        TXID=$(echo "$MINT_RESULT" | jq -r '.txid')
        BOB_STRESS_MINTS+=("$TXID")
        echo "    accepted: ${TXID:0:16}..."
    else
        print_status "fail" "Rapid mint $i failed: $(echo "$MINT_RESULT" | head -c 180)"
        exit 1
    fi
done

echo "Mining one live-oracle block to confirm the rapid mint batch..."
$BOB_CLI generatetoaddress 1 "$BOB_ADDR" 2000000000 "sha256d" >/dev/null 2>&1
sync_all_nodes

for i in "${!BOB_STRESS_MINTS[@]}"; do
    TXID="${BOB_STRESS_MINTS[$i]}"
    LABEL="Rapid mint $((i + 1))/$STRESS_MINT_COUNT"
    wait_for_tx_confirmed "$BOB_CLI" "bob" "$TXID" "$BOB_CLI" "$BOB_ADDR" "$LABEL" 80 || exit 1
    wait_for_dd_position_active "$BOB_CLI" "bob" "$TXID" "$STRESS_DD_CENTS" "$LABEL" 45 || exit 1
    assert_wallet_tx_clean "$BOB_CLI" "bob" "$TXID" "$LABEL" || exit 1
done

EXPECT_BOB_DD=$((EXPECT_BOB_DD + STRESS_DD_TOTAL))
EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD + STRESS_DD_TOTAL))
assert_no_pending_positions "$BOB_CLI" "bob" "Bob after rapid mint batch"
verify_all_balances "After rapid ${STRESS_MINT_COUNT}x \$100 mint batch"

# ====================================================================================
# Step 11: Early redemption test (should FAIL - tier 0 still locked)
# ====================================================================================
print_header "Step 11: Early Redemption Test (should FAIL)"
echo "Bob's tier 0 vaults are still locked (need 240 blocks)..."
echo "Attempting to redeem BEFORE lock expires..."

CURRENT_HEIGHT=$($BOB_CLI getblockcount)
echo "Current height: $CURRENT_HEIGHT"

set +e
EARLY_REDEEM=$($BOB_CLI -rpcwallet=bob redeemdigidollar "$BOB_TIER0_MINT1" 10000 2>&1)
EARLY_EXIT=$?
set -e

if [ $EARLY_EXIT -ne 0 ] || echo "$EARLY_REDEEM" | grep -qi "error\|lock"; then
    print_status "ok" "Early redemption correctly REJECTED (position still locked)"
    echo "   Response: $(echo $EARLY_REDEEM | head -c 100)..."
else
    print_status "fail" "Early redemption unexpectedly succeeded"
    echo "   Response: $EARLY_REDEEM"
fi

# ====================================================================================
# Step 12: Mine past tier 0 lock period (240 blocks)
# ====================================================================================
print_header "Step 12: Mining Past Tier 0 Lock Period"
echo "Tier 0 lock period is 240 blocks (~1 hour at 15s/block on testnet)"
echo "Mining until all tier 0 unlock heights are reached..."

TIER0_UNLOCKED=0
for i in {1..120}; do
    TIER0_POS_JSON=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null)
    CURRENT_HEIGHT=$($BOB_CLI getblockcount)

    MAX_TIER0_UNLOCK=$(echo "$TIER0_POS_JSON" | jq -r '[.[] | select(.lock_tier == 0 and .status != "redeemed") | .unlock_height] | max // 0' 2>/dev/null)
    [ -z "$MAX_TIER0_UNLOCK" ] && MAX_TIER0_UNLOCK=0

    if [ "$CURRENT_HEIGHT" -ge "$MAX_TIER0_UNLOCK" ]; then
        TIER0_UNLOCKED=1
        break
    fi

    BLOCKS_NEEDED=$((MAX_TIER0_UNLOCK - CURRENT_HEIGHT))
    [ "$BLOCKS_NEEDED" -gt 20 ] && BLOCKS_NEEDED=20
    [ "$BLOCKS_NEEDED" -lt 1 ] && BLOCKS_NEEDED=1

    echo "  Tier0 still locked (height $CURRENT_HEIGHT < unlock $MAX_TIER0_UNLOCK); mining $BLOCKS_NEEDED block(s)..."
    $BOB_CLI generatetoaddress "$BLOCKS_NEEDED" "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
    sleep 2
done

sync_all_nodes
NEW_HEIGHT=$($BOB_CLI getblockcount)
echo "Current height: $NEW_HEIGHT"

if [ "$TIER0_UNLOCKED" -ne 1 ]; then
    print_status "fail" "Tier 0 lock window did not clear within expected mining budget"
    exit 1
fi

LOCKED_TIER0_COUNT=$(echo "$TIER0_POS_JSON" | jq --argjson h "$NEW_HEIGHT" '[.[] | select(.lock_tier == 0 and .status != "redeemed" and .unlock_height > $h)] | length' 2>/dev/null)
if [ -z "$LOCKED_TIER0_COUNT" ] || [ "$LOCKED_TIER0_COUNT" -ne 0 ]; then
    print_status "fail" "Tier 0 positions still locked after unlock-wait phase"
    echo "$TIER0_POS_JSON" | jq -r '.[] | select(.lock_tier == 0) | "  [\(.status)] \(.dd_minted) cents - unlock: \(.unlock_height)"' 2>/dev/null || true
    exit 1
fi

print_status "ok" "Tier 0 unlock heights reached; redemption window is open"

# Check Bob's positions status
echo ""
echo "Bob's tier 0 positions status:"
echo "$TIER0_POS_JSON" | jq -r '.[] | select(.lock_tier == 0) | "  [\(.status)] \(.dd_minted) cents - unlock: \(.unlock_height)"' 2>/dev/null || echo "  Error reading positions"

# ====================================================================================
# Step 12A: RAPID REDEEM STRESS - redemptions without mining between RPCs
# ====================================================================================
print_header "Step 12A: Rapid Redeem Stress (${STRESS_MINT_COUNT}x \$100 tier 0)"
echo "Submitting $STRESS_MINT_COUNT redeemdigidollar RPCs back-to-back against the isolated stress mints."
echo "This verifies rapid redemptions do not leave local conflicts or stale pending state."

RAPID_REDEEM_TXS=()
for i in "${!BOB_STRESS_MINTS[@]}"; do
    POSITION_ID="${BOB_STRESS_MINTS[$i]}"
    echo "  Rapid redeem $((i + 1))/$STRESS_MINT_COUNT: ${POSITION_ID:0:16}..."
    set +e
    REDEEM_RESULT=$($BOB_CLI -rpcwallet=bob redeemdigidollar "$POSITION_ID" "$STRESS_DD_CENTS" 2>&1)
    REDEEM_EXIT=$?
    set -e
    if [ $REDEEM_EXIT -eq 0 ] && echo "$REDEEM_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
        TXID=$(echo "$REDEEM_RESULT" | jq -r '.txid')
        RAPID_REDEEM_TXS+=("$TXID")
        echo "    accepted: ${TXID:0:16}..."
    else
        print_status "fail" "Rapid redeem $((i + 1)) failed: $(echo "$REDEEM_RESULT" | head -c 180)"
        exit 1
    fi
done

echo "Mining one live-oracle block to confirm the rapid redeem batch..."
$BOB_CLI generatetoaddress 1 "$BOB_ADDR" 2000000000 "sha256d" >/dev/null 2>&1
sync_all_nodes

for i in "${!RAPID_REDEEM_TXS[@]}"; do
    TXID="${RAPID_REDEEM_TXS[$i]}"
    LABEL="Rapid redeem $((i + 1))/$STRESS_MINT_COUNT"
    wait_for_tx_confirmed "$BOB_CLI" "bob" "$TXID" "$BOB_CLI" "$BOB_ADDR" "$LABEL" 80 || exit 1
    assert_wallet_tx_clean "$BOB_CLI" "bob" "$TXID" "$LABEL" || exit 1
done

for POSITION_ID in "${BOB_STRESS_MINTS[@]}"; do
    STATUS=$($BOB_CLI -rpcwallet=bob listdigidollarpositions false 2>/dev/null \
        | jq -r --arg txid "$POSITION_ID" '.[] | select(.position_id == $txid) | .status' 2>/dev/null \
        | head -1)
    if [ "$STATUS" != "redeemed" ]; then
        print_status "fail" "Stress vault ${POSITION_ID:0:16} status is ${STATUS:-missing}, expected redeemed after confirmed redeem"
        exit 1
    fi
done
print_status "ok" "All $STRESS_MINT_COUNT stress vaults show redeemed only after confirmed redemptions"

EXPECT_BOB_DD=$((EXPECT_BOB_DD - STRESS_DD_TOTAL))
EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD - STRESS_DD_TOTAL))
assert_no_pending_positions "$BOB_CLI" "bob" "Bob after rapid redeem batch"
verify_all_balances "After rapid ${STRESS_MINT_COUNT}x redeem batch"

# ====================================================================================
# Step 12B: DD CHANGE TEST - Create mixed-size UTXOs via transfers
# ====================================================================================
print_header "Step 12B: DD Change Bug Test Setup"
echo "Creating mixed-size DD UTXOs via transfers to test DD change during redemption..."
echo ""
echo "SCENARIO:"
echo "  1. Bob sends \$50 to Alice  -> Bob has \$50 change UTXO"
echo "  2. Alice sends \$25 to Charlie -> Alice has \$25 change UTXO"
echo "  3. Charlie sends \$10 to Bob -> Bob gets \$10 UTXO"
echo ""
echo "Result: Bob has mixed UTXOs (\$50 change + \$10 received + remaining \$100 UTXOs)"
echo "When redeeming \$100 vault, SelectDDCoins may select >100 and generate DD change"
echo ""

# Get DD addresses for Alice and Charlie
ALICE_DD_ADDR_EARLY=$($ALICE_CLI -rpcwallet=alice getdigidollaraddress 2>/dev/null)
CHARLIE_DD_ADDR_EARLY=$($CHARLIE_CLI -rpcwallet=charlie getdigidollaraddress 2>/dev/null)
BOB_DD_ADDR_EARLY=$($BOB_CLI -rpcwallet=bob getdigidollaraddress 2>/dev/null)

echo "DD Addresses:"
echo "  Alice: $ALICE_DD_ADDR_EARLY"
echo "  Charlie: $CHARLIE_DD_ADDR_EARLY"
echo "  Bob: $BOB_DD_ADDR_EARLY"
echo ""

BOB_DD_START=$(get_dd_balance "$BOB_CLI" "bob")
echo "Bob's DD before transfers: $BOB_DD_START cents"

# Transfer 1: Bob sends $50 to Alice
echo ""
echo "Transfer 1: Bob sends \$50 (5000 cents) to Alice..."
set +e
XFER1=$($BOB_CLI -rpcwallet=bob senddigidollar "$ALICE_DD_ADDR_EARLY" 5000 2>&1)
XFER1_EXIT=$?
set -e

if [ $XFER1_EXIT -eq 0 ] && echo "$XFER1" | jq -e '.txid' > /dev/null 2>&1; then
    TX1=$(echo "$XFER1" | jq -r '.txid')
    wait_for_tx_confirmed "$BOB_CLI" "bob" "$TX1" "$BOB_CLI" "$BOB_ADDR" "Bob->Alice $50 transfer" 80 || exit 1
    print_status "ok" "Bob->Alice \$50: ${TX1:0:16}..."
    EXPECT_BOB_DD=$((EXPECT_BOB_DD - 5000))
    EXPECT_ALICE_DD=$((EXPECT_ALICE_DD + 5000))
else
    print_status "fail" "Transfer 1 failed: $XFER1"
    exit 1
fi

# Transfer 2: Alice sends $25 to Charlie
echo "Transfer 2: Alice sends \$25 (2500 cents) to Charlie..."
set +e
XFER2=$($ALICE_CLI -rpcwallet=alice senddigidollar "$CHARLIE_DD_ADDR_EARLY" 2500 2>&1)
XFER2_EXIT=$?
set -e

if [ $XFER2_EXIT -eq 0 ] && echo "$XFER2" | jq -e '.txid' > /dev/null 2>&1; then
    TX2=$(echo "$XFER2" | jq -r '.txid')
    wait_for_tx_confirmed "$ALICE_CLI" "alice" "$TX2" "$BOB_CLI" "$BOB_ADDR" "Alice->Charlie $25 transfer" 80 || exit 1
    print_status "ok" "Alice->Charlie \$25: ${TX2:0:16}..."
    EXPECT_ALICE_DD=$((EXPECT_ALICE_DD - 2500))
    EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD + 2500))
else
    print_status "fail" "Transfer 2 failed: $XFER2"
    exit 1
fi

# Give Charlie wallet/index time to see the incoming DD
sync_all_nodes
sleep 2

# Check Charlie's balance before trying to send
CHARLIE_DD_CHECK=$(get_dd_balance "$CHARLIE_CLI" "charlie")
echo "Charlie's DD balance after receiving from Alice: $CHARLIE_DD_CHECK cents"

# Transfer 3: Charlie sends $10 to Bob
echo "Transfer 3: Charlie sends \$10 (1000 cents) to Bob..."
set +e
XFER3=$($CHARLIE_CLI -rpcwallet=charlie senddigidollar "$BOB_DD_ADDR_EARLY" 1000 2>&1)
XFER3_EXIT=$?
set -e

if [ $XFER3_EXIT -eq 0 ] && echo "$XFER3" | jq -e '.txid' > /dev/null 2>&1; then
    TX3=$(echo "$XFER3" | jq -r '.txid')
    wait_for_tx_confirmed "$CHARLIE_CLI" "charlie" "$TX3" "$BOB_CLI" "$BOB_ADDR" "Charlie->Bob $10 transfer" 80 || exit 1
    print_status "ok" "Charlie->Bob \$10: ${TX3:0:16}..."
    EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD - 1000))
    EXPECT_BOB_DD=$((EXPECT_BOB_DD + 1000))
else
    print_status "fail" "Transfer 3 failed: $XFER3"
    exit 1
fi

# Give wallet time to process incoming DD
sync_all_nodes
sleep 2

# Force Bob's wallet to rescan for any missed DD UTXOs
$BOB_CLI -rpcwallet=bob rescanblockchain > /dev/null 2>&1 || true
sleep 2

BOB_DD_AFTER=$(get_dd_balance "$BOB_CLI" "bob")
echo ""
echo "Bob's DD after transfers: $BOB_DD_AFTER cents"
echo "Expected: $EXPECT_BOB_DD cents"
echo ""
echo "Bob now has mixed-size DD UTXOs. Proceeding with redemption tests..."
echo ""

# ====================================================================================
# Step 13: Partial Redemption Test (should FAIL - exact amount required)
# ====================================================================================
print_header "Step 13: Partial Redemption Test (should FAIL)"
echo "Testing that partial redemption is NOT allowed..."
echo "Bob's tier 0 vault #1 has 10000 cents - trying to redeem only 5000 cents..."

set +e
PARTIAL_REDEEM=$($BOB_CLI -rpcwallet=bob redeemdigidollar "$BOB_TIER0_MINT1" 5000 2>&1)
PARTIAL_EXIT=$?
set -e

if [ $PARTIAL_EXIT -ne 0 ] || echo "$PARTIAL_REDEEM" | grep -qi "error\|exact\|must\|full"; then
    print_status "ok" "Partial redemption correctly REJECTED (exact amount required)"
    echo "   Response: $(echo $PARTIAL_REDEEM | head -c 120)..."
else
    print_status "fail" "Partial redemption unexpectedly succeeded (should require exact amount)"
    echo "   Response: $PARTIAL_REDEEM"
fi

# ====================================================================================
# Step 14: Bob's First Successful Redemption (tier 0 mint #1)
# ====================================================================================
print_header "Step 14: Bob Redeems Tier 0 Vault #1 (\$100)"
echo "Bob's tier 0 position is now unlocked - redeeming EXACT amount of 10000 cents..."

BOB_DGB_BEFORE=$($BOB_CLI -rpcwallet=bob getbalance 2>/dev/null || echo "0")
BOB_DD_BEFORE=$(get_dd_balance "$BOB_CLI" "bob")
echo "Before redemption: Bob has $BOB_DD_BEFORE DD cents and $BOB_DGB_BEFORE DGB"

set +e
REDEEM_RESULT=$($BOB_CLI -rpcwallet=bob redeemdigidollar "$BOB_TIER0_MINT1" 10000 2>&1)
REDEEM_EXIT=$?
set -e

if [ $REDEEM_EXIT -eq 0 ] && echo "$REDEEM_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    REDEEM_TXID=$(echo "$REDEEM_RESULT" | jq -r '.txid')
    DGB_RETURNED=$(echo "$REDEEM_RESULT" | jq -r '.dgb_returned // .collateral_returned // "unknown"')
    print_status "ok" "REDEMPTION #1 SUCCESSFUL! TX: ${REDEEM_TXID:0:16}..."
    echo "  DD Burned: 10000 cents (\$100)"
    echo "  DGB Returned: $DGB_RETURNED DGB"
    EXPECT_BOB_DD=$((EXPECT_BOB_DD - 10000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD - 10000))
else
    print_status "fail" "Redemption #1 failed: $(echo $REDEEM_RESULT | head -c 150)..."
    exit 1
fi

wait_for_tx_confirmed "$BOB_CLI" "bob" "$REDEEM_TXID" "$BOB_CLI" "$BOB_ADDR" "Bob redemption #1" 80 || exit 1
sync_all_nodes

# ====================================================================================
# CRITICAL DD CHANGE VERIFICATION
# ====================================================================================
echo ""
echo "${CYAN}=== DD CHANGE BUG VERIFICATION ===${NC}"
BOB_DD_AFTER=$(get_dd_balance "$BOB_CLI" "bob")
EXPECTED_DD_AFTER=$((BOB_DD_BEFORE - 10000))  # Should be before - $100 burned

echo "  DD BEFORE redemption:  $BOB_DD_BEFORE cents"
echo "  DD burned:             10000 cents (\$100)"
echo "  EXPECTED DD after:     $EXPECTED_DD_AFTER cents"
echo "  ACTUAL DD after:       $BOB_DD_AFTER cents"

# Check if DD change was properly tracked
if [ "$BOB_DD_AFTER" -eq "$EXPECTED_DD_AFTER" ]; then
    print_status "ok" "DD CHANGE CORRECTLY TRACKED! Balance is exactly as expected."
elif [ "$BOB_DD_AFTER" -gt "$EXPECTED_DD_AFTER" ]; then
    print_status "fail" "DD balance higher than expected (possible duplicate UTXO tracking)"
    exit 1
else
    DD_LOSS=$((EXPECTED_DD_AFTER - BOB_DD_AFTER))
    print_status "fail" "DD CHANGE BUG DETECTED! Lost $DD_LOSS cents of DD change!"
    echo "  This indicates the DD change output was not properly tracked."
    echo "  The wallet burned more DD than necessary without returning change."
    exit 1
fi
echo ""

verify_all_balances "After Bob's First Redemption"

# ====================================================================================
# Step 15: Bob's Second Successful Redemption (tier 0 mint #2)
# ====================================================================================
print_header "Step 15: Bob Redeems Tier 0 Vault #2 (\$110)"
echo "Redeeming Bob's second tier 0 position (the \$110 vault)..."

BOB_DGB_BEFORE=$($BOB_CLI -rpcwallet=bob getbalance 2>/dev/null || echo "0")
BOB_DD_BEFORE=$(get_dd_balance "$BOB_CLI" "bob")
echo "Before redemption: Bob has $BOB_DD_BEFORE DD cents and $BOB_DGB_BEFORE DGB"

set +e
REDEEM_RESULT=$($BOB_CLI -rpcwallet=bob redeemdigidollar "$BOB_TIER0_MINT2" 11000 2>&1)
REDEEM_EXIT=$?
set -e

if [ $REDEEM_EXIT -eq 0 ] && echo "$REDEEM_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    REDEEM_TXID=$(echo "$REDEEM_RESULT" | jq -r '.txid')
    DGB_RETURNED=$(echo "$REDEEM_RESULT" | jq -r '.dgb_returned // .collateral_returned // "unknown"')
    print_status "ok" "REDEMPTION #2 SUCCESSFUL! TX: ${REDEEM_TXID:0:16}..."
    echo "  DD Burned: 11000 cents (\$110)"
    echo "  DGB Returned: $DGB_RETURNED DGB"
    EXPECT_BOB_DD=$((EXPECT_BOB_DD - 11000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD - 11000))
else
    print_status "fail" "Redemption #2 failed: $(echo $REDEEM_RESULT | head -c 150)..."
    exit 1
fi

wait_for_tx_confirmed "$BOB_CLI" "bob" "$REDEEM_TXID" "$BOB_CLI" "$BOB_ADDR" "Bob redemption #2" 80 || exit 1
sync_all_nodes

verify_all_balances "After Bob's Second Redemption"
list_dd_positions "$BOB_CLI" "bob" "Bob"

# ====================================================================================
# Step 15B: RAPID SEND STRESS - 20 sends without mining between RPCs
# ====================================================================================
print_header "Step 15B: Rapid DD Send Stress"
echo "Submitting 20x 5 DD sends without mining between RPCs."
echo "This verifies DD sends stay clean and do not mark unrelated vaults redeemed."

RAPID_SEND_TXS=()
CHARLIE_RAPID_DD_ADDR=$($CHARLIE_CLI -rpcwallet=charlie getdigidollaraddress 2>/dev/null)
echo "Rapid send destination (Charlie): $CHARLIE_RAPID_DD_ADDR"

UNREDEEMED_BEFORE_SEND=$($BOB_CLI -rpcwallet=bob listdigidollarpositions false 2>/dev/null \
    | jq -r '.[] | select(.status != "redeemed") | .position_id' 2>/dev/null)

echo "Creating 20 confirmed 5 DD self-send fragments for deterministic rapid send inputs..."
BOB_DD_BEFORE_FRAGMENT=$(get_dd_balance "$BOB_CLI" "bob")
SELF_SEND_AMOUNTS="{"
for i in $(seq 1 20); do
    SELF_ADDR=$($BOB_CLI -rpcwallet=bob getdigidollaraddress "rapid-send-fragment-$i" 2>/dev/null)
    if [ "$i" -gt 1 ]; then
        SELF_SEND_AMOUNTS+=","
    fi
    SELF_SEND_AMOUNTS+="\"$SELF_ADDR\":500"
done
SELF_SEND_AMOUNTS+="}"

set +e
FRAGMENT_RESULT=$($BOB_CLI -rpcwallet=bob sendmanydigidollar "" "$SELF_SEND_AMOUNTS" "rapid send input fragmentation" 2>&1)
FRAGMENT_EXIT=$?
set -e
if [ $FRAGMENT_EXIT -eq 0 ] && echo "$FRAGMENT_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    FRAGMENT_TXID=$(echo "$FRAGMENT_RESULT" | jq -r '.txid')
    echo "  self-fragment tx accepted: ${FRAGMENT_TXID:0:16}..."
else
    print_status "fail" "Rapid send self-fragmentation failed: $(echo "$FRAGMENT_RESULT" | head -c 180)"
    exit 1
fi

echo "Mining one live-oracle block to confirm rapid send input fragments..."
$BOB_CLI generatetoaddress 1 "$BOB_ADDR" 2000000000 "sha256d" >/dev/null 2>&1
sync_all_nodes
wait_for_tx_confirmed "$BOB_CLI" "bob" "$FRAGMENT_TXID" "$BOB_CLI" "$BOB_ADDR" "Rapid send self-fragmentation" 80 || exit 1
assert_wallet_tx_clean "$BOB_CLI" "bob" "$FRAGMENT_TXID" "Rapid send self-fragmentation" || exit 1
assert_positions_not_redeemed "$BOB_CLI" "bob" "$UNREDEEMED_BEFORE_SEND" "DD self-fragmentation" || exit 1

BOB_DD_AFTER_FRAGMENT=$(get_dd_balance "$BOB_CLI" "bob")
if [ "$BOB_DD_AFTER_FRAGMENT" -ne "$BOB_DD_BEFORE_FRAGMENT" ]; then
    print_status "fail" "Self-fragmentation changed Bob DD balance ($BOB_DD_BEFORE_FRAGMENT -> $BOB_DD_AFTER_FRAGMENT)"
    exit 1
fi
print_status "ok" "Self-fragmentation preserved Bob DD balance at $BOB_DD_AFTER_FRAGMENT cents"

for i in $(seq 1 20); do
    echo "  Rapid send $i/20: Bob -> Charlie 500 cents..."
    set +e
    SEND_RESULT=$($BOB_CLI -rpcwallet=bob senddigidollar "$CHARLIE_RAPID_DD_ADDR" 500 2>&1)
    SEND_EXIT=$?
    set -e
    if [ $SEND_EXIT -eq 0 ] && echo "$SEND_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
        TXID=$(echo "$SEND_RESULT" | jq -r '.txid')
        RAPID_SEND_TXS+=("$TXID")
        echo "    accepted: ${TXID:0:16}..."
    else
        print_status "fail" "Rapid DD send $i failed: $(echo "$SEND_RESULT" | head -c 180)"
        exit 1
    fi
done

assert_positions_not_redeemed "$BOB_CLI" "bob" "$UNREDEEMED_BEFORE_SEND" "rapid DD sends" || exit 1

echo "Mining one live-oracle block to confirm the rapid send batch..."
$BOB_CLI generatetoaddress 1 "$BOB_ADDR" 2000000000 "sha256d" >/dev/null 2>&1
sync_all_nodes

for i in "${!RAPID_SEND_TXS[@]}"; do
    TXID="${RAPID_SEND_TXS[$i]}"
    LABEL="Rapid send $((i + 1))/20"
    wait_for_tx_confirmed "$BOB_CLI" "bob" "$TXID" "$BOB_CLI" "$BOB_ADDR" "$LABEL" 80 || exit 1
    assert_wallet_tx_clean "$BOB_CLI" "bob" "$TXID" "$LABEL" || exit 1
done

EXPECT_BOB_DD=$((EXPECT_BOB_DD - 10000))
EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD + 10000))
verify_all_balances "After rapid 20x 5 DD send batch"

# ====================================================================================
# Step 16: Alice Mints $100 at Tier 3 (180 days)
# ====================================================================================
print_header "Step 16: Alice Mints \$100 at Tier 3 (180 days)"

ALICE_DGB=$($ALICE_CLI -rpcwallet=alice getbalance 2>/dev/null || echo "0")
echo "Alice's DGB balance: $ALICE_DGB DGB"

echo "Refreshing oracle prices before Alice's mint..."
refresh_oracle_prices
echo "Alice minting \$100 DD (10000 cents) with tier 3 [$(get_tier_description 3)]..."
set +e
ALICE_MINT=$($ALICE_CLI -rpcwallet=alice mintdigidollar 10000 3 2>&1)
ALICE_MINT_EXIT=$?
set -e

if [ $ALICE_MINT_EXIT -eq 0 ] && echo "$ALICE_MINT" | jq -e '.txid' > /dev/null 2>&1; then
    ALICE_TIER3_TX=$(echo "$ALICE_MINT" | jq -r '.txid')
    COLLATERAL=$(echo "$ALICE_MINT" | jq -r '.dgb_collateral')
    confirm_dd_mint "$ALICE_CLI" "alice" "$ALICE_TIER3_TX" 10000 "Alice tier 3 mint" "$ALICE_CLI" "$ALICE_ADDR"
    print_status "ok" "Alice Tier 3 Mint: TX ${ALICE_TIER3_TX:0:12}... Collateral: $COLLATERAL DGB"
    EXPECT_ALICE_DD=$((EXPECT_ALICE_DD + 10000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD + 10000))
else
    print_status "fail" "Alice Tier 3 Mint failed: $ALICE_MINT"
    exit 1
fi

sync_all_nodes

# ====================================================================================
# Step 17: Alice Mints $100 at Tier 5 (2 years)
# ====================================================================================
print_header "Step 17: Alice Mints \$100 at Tier 5 (2 years)"

ALICE_DGB=$($ALICE_CLI -rpcwallet=alice getbalance 2>/dev/null || echo "0")
echo "Alice's DGB balance: $ALICE_DGB DGB"

# Top up Alice if her first mint consumed most spendable collateral. Live
# exchange prices can move during the run, so keep enough headroom for the
# second mint at low DGB prices.
ALICE_BAL_INT=$(echo "$ALICE_DGB" | cut -d. -f1)
if [ "$ALICE_BAL_INT" -lt 200000 ] 2>/dev/null; then
    echo "Alice balance low ($ALICE_DGB DGB) — sending 200000 DGB from Bob..."
    ALICE_TOP=$($ALICE_CLI -rpcwallet=alice getnewaddress "" "legacy" 2>/dev/null)
    $BOB_CLI -rpcwallet=bob sendtoaddress "$ALICE_TOP" 200000 > /dev/null 2>&1
    $BOB_CLI generatetoaddress 1 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
    sync_all_nodes
    ALICE_DGB=$($ALICE_CLI -rpcwallet=alice getbalance 2>/dev/null || echo "0")
    echo "Alice new balance: $ALICE_DGB DGB"
fi

echo "Refreshing oracle prices before Alice's mint..."
refresh_oracle_prices
echo "Alice minting \$100 DD (10000 cents) with tier 5 [$(get_tier_description 5)]..."
set +e
ALICE_MINT=$($ALICE_CLI -rpcwallet=alice mintdigidollar 10000 5 2>&1)
ALICE_MINT_EXIT=$?
set -e

if [ $ALICE_MINT_EXIT -eq 0 ] && echo "$ALICE_MINT" | jq -e '.txid' > /dev/null 2>&1; then
    ALICE_TIER5_TX=$(echo "$ALICE_MINT" | jq -r '.txid')
    COLLATERAL=$(echo "$ALICE_MINT" | jq -r '.dgb_collateral')
    confirm_dd_mint "$ALICE_CLI" "alice" "$ALICE_TIER5_TX" 10000 "Alice tier 5 mint" "$ALICE_CLI" "$ALICE_ADDR"
    print_status "ok" "Alice Tier 5 Mint: TX ${ALICE_TIER5_TX:0:12}... Collateral: $COLLATERAL DGB"
    EXPECT_ALICE_DD=$((EXPECT_ALICE_DD + 10000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD + 10000))
else
    print_status "fail" "Alice Tier 5 Mint failed: $ALICE_MINT"
    exit 1
fi

sync_all_nodes
assert_no_pending_positions "$ALICE_CLI" "alice" "Alice"

verify_all_balances "After Alice's 2 Mints (Tier 3 + Tier 5)"
list_dd_positions "$ALICE_CLI" "alice" "Alice"

# ====================================================================================
# Step 18: Charlie Mints $100 at Tier 7 (5 years)
# ====================================================================================
print_header "Step 18: Charlie Mints \$100 at Tier 7 (5 years)"

CHARLIE_DGB=$($CHARLIE_CLI -rpcwallet=charlie getbalance 2>/dev/null || echo "0")
echo "Charlie's DGB balance: $CHARLIE_DGB DGB"

echo "Refreshing oracle prices before Charlie's mint..."
refresh_oracle_prices
echo "Charlie minting \$100 DD (10000 cents) with tier 7 [$(get_tier_description 7)]..."
set +e
CHARLIE_MINT=$($CHARLIE_CLI -rpcwallet=charlie mintdigidollar 10000 7 2>&1)
CHARLIE_MINT_EXIT=$?
set -e

if [ $CHARLIE_MINT_EXIT -eq 0 ] && echo "$CHARLIE_MINT" | jq -e '.txid' > /dev/null 2>&1; then
    CHARLIE_TIER7_TX=$(echo "$CHARLIE_MINT" | jq -r '.txid')
    COLLATERAL=$(echo "$CHARLIE_MINT" | jq -r '.dgb_collateral')
    confirm_dd_mint "$CHARLIE_CLI" "charlie" "$CHARLIE_TIER7_TX" 10000 "Charlie tier 7 mint" "$CHARLIE_CLI" "$CHARLIE_ADDR"
    print_status "ok" "Charlie Tier 7 Mint: TX ${CHARLIE_TIER7_TX:0:12}... Collateral: $COLLATERAL DGB"
    EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD + 10000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD + 10000))
else
    print_status "fail" "Charlie Tier 7 Mint failed: $CHARLIE_MINT"
    exit 1
fi

sync_all_nodes

# ====================================================================================
# Step 19: Charlie Mints $100 at Tier 8 (7 years)
# ====================================================================================
print_header "Step 19: Charlie Mints \$100 at Tier 8 (7 years)"

CHARLIE_DGB=$($CHARLIE_CLI -rpcwallet=charlie getbalance 2>/dev/null || echo "0")
echo "Charlie's DGB balance: $CHARLIE_DGB DGB"

# Top up Charlie if balance is low (collateral at low oracle EMA price can be huge)
CHARLIE_BAL_INT=$(echo "$CHARLIE_DGB" | cut -d. -f1)
if [ "$CHARLIE_BAL_INT" -lt 200000 ] 2>/dev/null; then
    echo "Charlie balance low ($CHARLIE_DGB DGB) — sending 200000 DGB from Bob..."
    CHARLIE_TOP=$($CHARLIE_CLI -rpcwallet=charlie getnewaddress "" "legacy" 2>/dev/null)
    $BOB_CLI -rpcwallet=bob sendtoaddress "$CHARLIE_TOP" 200000 > /dev/null 2>&1
    $BOB_CLI generatetoaddress 1 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
    sync_all_nodes
    CHARLIE_DGB=$($CHARLIE_CLI -rpcwallet=charlie getbalance 2>/dev/null || echo "0")
    echo "Charlie new balance: $CHARLIE_DGB DGB"
fi

echo "Refreshing oracle prices before Charlie's mint..."
refresh_oracle_prices
echo "Charlie minting \$100 DD (10000 cents) with tier 8 [$(get_tier_description 8)]..."
set +e
CHARLIE_MINT=$($CHARLIE_CLI -rpcwallet=charlie mintdigidollar 10000 8 2>&1)
CHARLIE_MINT_EXIT=$?
set -e

if [ $CHARLIE_MINT_EXIT -eq 0 ] && echo "$CHARLIE_MINT" | jq -e '.txid' > /dev/null 2>&1; then
    CHARLIE_TIER8_TX=$(echo "$CHARLIE_MINT" | jq -r '.txid')
    COLLATERAL=$(echo "$CHARLIE_MINT" | jq -r '.dgb_collateral')
    confirm_dd_mint "$CHARLIE_CLI" "charlie" "$CHARLIE_TIER8_TX" 10000 "Charlie tier 8 mint" "$CHARLIE_CLI" "$CHARLIE_ADDR"
    print_status "ok" "Charlie Tier 8 Mint: TX ${CHARLIE_TIER8_TX:0:12}... Collateral: $COLLATERAL DGB"
    EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD + 10000))
    EXPECT_NETWORK_DD=$((EXPECT_NETWORK_DD + 10000))
else
    print_status "fail" "Charlie Tier 8 Mint failed: $CHARLIE_MINT"
    exit 1
fi

sync_all_nodes
assert_no_pending_positions "$CHARLIE_CLI" "charlie" "Charlie"

verify_all_balances "After Charlie's 2 Mints (Tier 7 + Tier 8)"
list_dd_positions "$CHARLIE_CLI" "charlie" "Charlie"

# ====================================================================================
# Step 20: DD Transfer Tests (Bob sends to Alice and Charlie)
# ====================================================================================
print_header "Step 20: DD Transfer Tests"

# Get DD addresses
ALICE_DD_ADDR=$($ALICE_CLI -rpcwallet=alice getdigidollaraddress 2>/dev/null)
CHARLIE_DD_ADDR=$($CHARLIE_CLI -rpcwallet=charlie getdigidollaraddress 2>/dev/null)
echo "Alice's DD address: $ALICE_DD_ADDR"
echo "Charlie's DD address: $CHARLIE_DD_ADDR"

# Bob sends $50 DD to Alice
print_subheader "Bob sends \$50 DD (5000 cents) to Alice"
set +e
SEND_RESULT=$($BOB_CLI -rpcwallet=bob senddigidollar "$ALICE_DD_ADDR" 5000 2>&1)
SEND_EXIT=$?
set -e

if [ $SEND_EXIT -eq 0 ] && echo "$SEND_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    print_status "ok" "Bob sent 5000 cents to Alice"
    EXPECT_BOB_DD=$((EXPECT_BOB_DD - 5000))
    EXPECT_ALICE_DD=$((EXPECT_ALICE_DD + 5000))
else
    print_status "fail" "Transfer failed: $SEND_RESULT"
fi

$BOB_CLI generatetoaddress 2 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
sleep 3

# Bob sends $30 DD to Charlie
print_subheader "Bob sends \$30 DD (3000 cents) to Charlie"
set +e
SEND_RESULT=$($BOB_CLI -rpcwallet=bob senddigidollar "$CHARLIE_DD_ADDR" 3000 2>&1)
SEND_EXIT=$?
set -e

if [ $SEND_EXIT -eq 0 ] && echo "$SEND_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    print_status "ok" "Bob sent 3000 cents to Charlie"
    EXPECT_BOB_DD=$((EXPECT_BOB_DD - 3000))
    EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD + 3000))
else
    print_status "fail" "Transfer failed: $SEND_RESULT"
fi

$BOB_CLI generatetoaddress 2 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
sleep 3
sync_all_nodes

verify_all_balances "After DD Transfers (Bob -> Alice/Charlie)"

# ====================================================================================
# Step 21: Alice's Early Redemption Test (Tier 3 - should FAIL)
# ====================================================================================
print_header "Step 21: Alice's Early Redemption Test (should FAIL)"
echo "Alice's tier 3 vault is locked for 180 days - should be rejected..."

CURRENT_HEIGHT=$($BOB_CLI getblockcount)
echo "Current height: $CURRENT_HEIGHT"

# Get Alice's tier 3 position details
ALICE_POS=$($ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null)
echo "Alice's positions:"
echo "$ALICE_POS" | jq -r '.[] | "  [\(.status)] tier \(.lock_tier) - \(.dd_minted) cents - unlock: \(.unlock_height)"' 2>/dev/null

if [ -n "$ALICE_TIER3_TX" ]; then
    set +e
    ALICE_EARLY=$($ALICE_CLI -rpcwallet=alice redeemdigidollar "$ALICE_TIER3_TX" 10000 2>&1)
    ALICE_EARLY_EXIT=$?
    set -e

    if [ $ALICE_EARLY_EXIT -ne 0 ] || echo "$ALICE_EARLY" | grep -qi "error\|lock"; then
        print_status "ok" "Alice's tier 3 early redemption correctly REJECTED (still locked)"
        echo "   Response: $(echo $ALICE_EARLY | head -c 100)..."
    else
        print_status "fail" "Unexpected early redemption result for Alice tier-3 position: $ALICE_EARLY"
        exit 1
    fi
else
    echo "  Skipping - Alice's tier 3 mint txid not available"
fi

# ====================================================================================
# Step 22: Charlie's Early Redemption Test (Tier 8 - should FAIL)
# ====================================================================================
print_header "Step 22: Charlie's Early Redemption Test (should FAIL)"
echo "Charlie's tier 8 vault is locked for 7 years - should be rejected..."

if [ -n "$CHARLIE_TIER8_TX" ]; then
    set +e
    CHARLIE_EARLY=$($CHARLIE_CLI -rpcwallet=charlie redeemdigidollar "$CHARLIE_TIER8_TX" 10000 2>&1)
    CHARLIE_EARLY_EXIT=$?
    set -e

    if [ $CHARLIE_EARLY_EXIT -ne 0 ] || echo "$CHARLIE_EARLY" | grep -qi "error\|lock"; then
        print_status "ok" "Charlie's tier 8 early redemption correctly REJECTED (still locked)"
        echo "   Response: $(echo $CHARLIE_EARLY | head -c 100)..."
    else
        print_status "fail" "Unexpected early redemption result for Charlie tier-8 position: $CHARLIE_EARLY"
        exit 1
    fi
else
    echo "  Skipping - Charlie's tier 8 mint txid not available"
fi

# ====================================================================================
# Step 23: Comprehensive DD Transfer Chain - Bob sends $55 to Alice
# ====================================================================================
print_header "Step 23: Bob sends \$55 DD (5500 cents) to Alice"

# Get Bob's DD address for later (Charlie will send to Bob)
BOB_DD_ADDR=$($BOB_CLI -rpcwallet=bob getdigidollaraddress 2>/dev/null)
echo "Bob's DD address: $BOB_DD_ADDR"
echo ""

echo "Current balances before transfer:"
echo "  Bob:     $EXPECT_BOB_DD cents"
echo "  Alice:   $EXPECT_ALICE_DD cents"
echo "  Charlie: $EXPECT_CHARLIE_DD cents"
echo ""

# Record DGB balances before transfer
BOB_DGB_BEFORE=$(get_dgb_balance "$BOB_CLI" "bob")
ALICE_DGB_BEFORE=$(get_dgb_balance "$ALICE_CLI" "alice")
CHARLIE_DGB_BEFORE=$(get_dgb_balance "$CHARLIE_CLI" "charlie")
echo "DGB balances before:"
echo "  Bob:     $BOB_DGB_BEFORE DGB"
echo "  Alice:   $ALICE_DGB_BEFORE DGB"
echo "  Charlie: $CHARLIE_DGB_BEFORE DGB"
echo ""

set +e
SEND_RESULT=$($BOB_CLI -rpcwallet=bob senddigidollar "$ALICE_DD_ADDR" 5500 2>&1)
SEND_EXIT=$?
set -e

if [ $SEND_EXIT -eq 0 ] && echo "$SEND_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    SEND_TXID=$(echo "$SEND_RESULT" | jq -r '.txid')
    wait_for_tx_confirmed "$BOB_CLI" "bob" "$SEND_TXID" "$BOB_CLI" "$BOB_ADDR" "Bob->Alice $55 transfer" 80 || exit 1
    print_status "ok" "Bob sent 5500 cents (\$55) to Alice - TX: ${SEND_TXID:0:16}..."
    EXPECT_BOB_DD=$((EXPECT_BOB_DD - 5500))
    EXPECT_ALICE_DD=$((EXPECT_ALICE_DD + 5500))
else
    print_status "fail" "Transfer failed: $SEND_RESULT"
    exit 1
fi

sync_all_nodes

verify_all_balances "After Bob->Alice \$55 Transfer"

# ====================================================================================
# Step 24: Alice sends $22 DD to Charlie
# ====================================================================================
print_header "Step 24: Alice sends \$22 DD (2200 cents) to Charlie"

echo "Current balances before transfer:"
echo "  Bob:     $EXPECT_BOB_DD cents"
echo "  Alice:   $EXPECT_ALICE_DD cents"
echo "  Charlie: $EXPECT_CHARLIE_DD cents"
echo ""

set +e
SEND_RESULT=$($ALICE_CLI -rpcwallet=alice senddigidollar "$CHARLIE_DD_ADDR" 2200 2>&1)
SEND_EXIT=$?
set -e

if [ $SEND_EXIT -eq 0 ] && echo "$SEND_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    SEND_TXID=$(echo "$SEND_RESULT" | jq -r '.txid')
    wait_for_tx_confirmed "$ALICE_CLI" "alice" "$SEND_TXID" "$BOB_CLI" "$BOB_ADDR" "Alice->Charlie $22 transfer" 80 || exit 1
    print_status "ok" "Alice sent 2200 cents (\$22) to Charlie - TX: ${SEND_TXID:0:16}..."
    EXPECT_ALICE_DD=$((EXPECT_ALICE_DD - 2200))
    EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD + 2200))
else
    print_status "fail" "Transfer failed: $SEND_RESULT"
    exit 1
fi

sync_all_nodes

verify_all_balances "After Alice->Charlie \$22 Transfer"

# ====================================================================================
# Step 25: Charlie sends $10 DD to Bob
# ====================================================================================
print_header "Step 25: Charlie sends \$10 DD (1000 cents) to Bob"

echo "Current balances before transfer:"
echo "  Bob:     $EXPECT_BOB_DD cents"
echo "  Alice:   $EXPECT_ALICE_DD cents"
echo "  Charlie: $EXPECT_CHARLIE_DD cents"
echo ""

set +e
SEND_RESULT=$($CHARLIE_CLI -rpcwallet=charlie senddigidollar "$BOB_DD_ADDR" 1000 2>&1)
SEND_EXIT=$?
set -e

if [ $SEND_EXIT -eq 0 ] && echo "$SEND_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    SEND_TXID=$(echo "$SEND_RESULT" | jq -r '.txid')
    wait_for_tx_confirmed "$CHARLIE_CLI" "charlie" "$SEND_TXID" "$BOB_CLI" "$BOB_ADDR" "Charlie->Bob $10 transfer" 80 || exit 1
    print_status "ok" "Charlie sent 1000 cents (\$10) to Bob - TX: ${SEND_TXID:0:16}..."
    EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD - 1000))
    EXPECT_BOB_DD=$((EXPECT_BOB_DD + 1000))
else
    print_status "fail" "Transfer failed: $SEND_RESULT"
    exit 1
fi

sync_all_nodes

verify_all_balances "After Charlie->Bob \$10 Transfer"

# ====================================================================================
# Step 26: Bob sends $5 DD to Charlie
# ====================================================================================
print_header "Step 26: Bob sends \$5 DD (500 cents) to Charlie"

echo "Current balances before transfer:"
echo "  Bob:     $EXPECT_BOB_DD cents"
echo "  Alice:   $EXPECT_ALICE_DD cents"
echo "  Charlie: $EXPECT_CHARLIE_DD cents"
echo ""

set +e
SEND_RESULT=$($BOB_CLI -rpcwallet=bob senddigidollar "$CHARLIE_DD_ADDR" 500 2>&1)
SEND_EXIT=$?
set -e

if [ $SEND_EXIT -eq 0 ] && echo "$SEND_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
    SEND_TXID=$(echo "$SEND_RESULT" | jq -r '.txid')
    wait_for_tx_confirmed "$BOB_CLI" "bob" "$SEND_TXID" "$BOB_CLI" "$BOB_ADDR" "Bob->Charlie $5 transfer" 80 || exit 1
    print_status "ok" "Bob sent 500 cents (\$5) to Charlie - TX: ${SEND_TXID:0:16}..."
    EXPECT_BOB_DD=$((EXPECT_BOB_DD - 500))
    EXPECT_CHARLIE_DD=$((EXPECT_CHARLIE_DD + 500))
else
    print_status "fail" "Transfer failed: $SEND_RESULT"
    exit 1
fi

sync_all_nodes

verify_all_balances "After Bob->Charlie \$5 Transfer"

# Final DGB balance check
print_subheader "Final DGB Balance Check"
BOB_DGB_AFTER=$(get_dgb_balance "$BOB_CLI" "bob")
ALICE_DGB_AFTER=$(get_dgb_balance "$ALICE_CLI" "alice")
CHARLIE_DGB_AFTER=$(get_dgb_balance "$CHARLIE_CLI" "charlie")
echo "DGB balances after all transfers:"
echo "  Bob:     $BOB_DGB_AFTER DGB"
echo "  Alice:   $ALICE_DGB_AFTER DGB"
echo "  Charlie: $CHARLIE_DGB_AFTER DGB"
print_status "ok" "DGB balances checked after transfer chain"

# ====================================================================================
# FINAL STATE
# ====================================================================================
print_header "Step 27: Final DigiDollar Network State"

echo ""
echo "==================== FINAL SUMMARY ===================="
echo ""
echo "MINT SUMMARY:"
echo "  Bob:     11 mints (tier0 \$100, tier0 \$110, tiers 1-9 \$110 each) = \$1200"
echo "  Alice:   2 mints (tier 3 + tier 5) = \$200"
echo "  Charlie: 2 mints (tier 7 + tier 8) = \$200"
echo "  TOTAL MINTED: \$1600 (160000 cents)"
echo ""
echo "REDEMPTION SUMMARY:"
echo "  Bob:     2 tier 0 redemptions = \$210 burned (\$100 + \$110)"
echo "  Alice:   0 (locked)"
echo "  Charlie: 0 (locked)"
echo "  TOTAL REDEEMED: \$210 (21000 cents)"
echo ""
echo "TRANSFER SUMMARY:"
echo "  Initial:"
echo "    Bob -> Alice: \$50"
echo "    Bob -> Charlie: \$30"
echo "  Transfer Chain:"
echo "    Bob -> Alice: \$55 (Step 23)"
echo "    Alice -> Charlie: \$22 (Step 24)"
echo "    Charlie -> Bob: \$10 (Step 25)"
echo "    Bob -> Charlie: \$5 (Step 26)"
echo ""
echo "Expected DD Balances:"
echo "  Bob:     $EXPECT_BOB_DD cents (\$$(echo "scale=2; $EXPECT_BOB_DD / 100" | bc))"
echo "  Alice:   $EXPECT_ALICE_DD cents (\$$(echo "scale=2; $EXPECT_ALICE_DD / 100" | bc))"
echo "  Charlie: $EXPECT_CHARLIE_DD cents (\$$(echo "scale=2; $EXPECT_CHARLIE_DD / 100" | bc))"
echo "  TOTAL:   $((EXPECT_BOB_DD + EXPECT_ALICE_DD + EXPECT_CHARLIE_DD)) cents"
echo ""

echo "Actual DD Balances (from RPC):"
echo "  Bob:     $(get_dd_balance "$BOB_CLI" "bob") cents"
echo "  Alice:   $(get_dd_balance "$ALICE_CLI" "alice") cents"
echo "  Charlie: $(get_dd_balance "$CHARLIE_CLI" "charlie") cents"
echo ""

echo "Network Stats:"
echo "  Total DD Supply: $(get_network_dd_supply) cents"
echo "  Total Collateral: $(get_network_collateral) DGB"
echo ""

echo "ALL DD POSITIONS:"
list_dd_positions "$BOB_CLI" "bob" "Bob"
list_dd_positions "$ALICE_CLI" "alice" "Alice"
list_dd_positions "$CHARLIE_CLI" "charlie" "Charlie"

# ====================================================================================
# Step 27A: 7-of-35 Oracle Consensus Verification (RC44)
# ====================================================================================
# All 24 active oracles are running and reporting the live exchange price. This step verifies that the network
# actually converges on a non-zero consensus price, meaning the 7-of-35
# threshold is being met on-chain by the 24 active signing slots. (Oracle
# prices are not forged — they are the real exchange-aggregator median, so we
# only assert the price is > 0.)
print_header "Step 27A: 7-of-35 Oracle Consensus Verification (RC44)"
echo ""
echo "Verifying that at least 7 of 35 reserved oracle slots agree on the live exchange price."
echo "Mining blocks so oracle price threads broadcast and bundles form, then"
echo "asserting getoracleprice returns a non-zero, non-N/A consensus value."
echo ""

refresh_oracle_prices

ORACLE_PRICE_27A=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.price_usd // "N/A"')
echo "Oracle price after 7-of-35 consensus: \$$ORACLE_PRICE_27A"

# Accept any non-zero, non-N/A value as proof that 7-of-35 consensus fired.
if [ "$ORACLE_PRICE_27A" != "N/A" ] && [ "$ORACLE_PRICE_27A" != "0" ] && \
   [ "$ORACLE_PRICE_27A" != "0.00000000" ] && [ -n "$ORACLE_PRICE_27A" ]; then
    print_status "ok" "7-of-35 oracle consensus verified: live price = \$$ORACLE_PRICE_27A"
else
    print_status "fail" "7-of-35 consensus NOT reached (price: \$$ORACLE_PRICE_27A)"
fi

# ====================================================================================
# Step 27B: Manual Oracle Injection Removal Check
# ====================================================================================
# RC44 keeps sendoracleprice/fake price injection removed. That is intentional security
# hardening: oracle prices must come from live exchange aggregation only.
print_header "Step 27B: Manual Oracle Injection Removed"
echo ""
echo "Verifying the insecure sendoracleprice RPC is unavailable on RC44."
echo ""

SENDORACLE_HELP=$($BOB_CLI help sendoracleprice 2>&1 || true)
if echo "$SENDORACLE_HELP" | grep -qi "unknown command"; then
    print_status "ok" "sendoracleprice RPC is removed; oracle prices are live-exchange only"
else
    print_status "fail" "sendoracleprice RPC still exists — fake oracle injection is still possible"
    echo "$SENDORACLE_HELP"
    exit 1
fi

PRICE_AFTER_27B=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.price_usd // "N/A"')
echo "Price after injection-removal check: \$$PRICE_AFTER_27B"

# ====================================================================================
# Step 27C: Live Exchange Outlier Filter Evidence
# ====================================================================================
# Fake oracle injection is gone, so this step records live exchange outlier
# filtering when an outlier naturally occurs. Absence of an outlier is a warning
# and must not be summarized as coverage of a forced disagreement scenario.
print_header "Step 27C: Live Exchange Outlier Filter Evidence"
echo ""
echo "Refreshing live oracle feeds and checking debug.log for exchange outlier filtering."
echo ""

refresh_oracle_prices
PRICE_AFTER_27C=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.price_usd // "N/A"')
echo "Price after live refresh: \$$PRICE_AFTER_27C"
ORACLE_OUTLIER_OBSERVED=false

if grep -qi "Filtered outlier" "$BOB_DATADIR/$TESTNET_SUBDIR/debug.log" 2>/dev/null; then
    ORACLE_OUTLIER_OBSERVED=true
    print_status "ok" "Live exchange outlier filter triggered"
    grep -i "Filtered outlier" "$BOB_DATADIR/$TESTNET_SUBDIR/debug.log" | tail -3
else
    print_status "warn" "Live exchange outlier filter observation is optional; no outlier was observed in this run"
fi

# ====================================================================================
# Step 27D: Oracle Continued Consensus
# ====================================================================================
print_header "Step 27D: Oracle Continued Consensus Test"
echo ""
echo "All 24 active oracles continue broadcasting live exchange prices after"
echo "the manual-injection removal and optional outlier-observation checks."
echo ""

refresh_oracle_prices

PRICE_AFTER_27D=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.price_usd // "N/A"')
echo "Price after continued-consensus refresh: \$$PRICE_AFTER_27D"

if [ "$PRICE_AFTER_27D" != "N/A" ] && [ "$PRICE_AFTER_27D" != "0" ] && \
   [ "$PRICE_AFTER_27D" != "0.00000000" ] && [ -n "$PRICE_AFTER_27D" ]; then
    print_status "ok" "Oracle continued consensus successful: 7-of-35 threshold active at \$$PRICE_AFTER_27D"
else
    print_status "fail" "Oracle consensus failed after live refresh — price: \$$PRICE_AFTER_27D"
fi

sync_all_nodes

# ====================================================================================
# Step 27E: On-chain oracle bundle size + version verification
# ====================================================================================
# After Step 27D confirms continued oracle consensus, mine a fresh block and inspect
# the coinbase OP_RETURN scriptPubKey to confirm an on-chain OP_ORACLE bundle
# landed. V1 requires a v0x03 MuSig2 bundle; v0x02 fallback bundles are not
# accepted as production validation fallbacks.
#
# Script layout:
#   OP_RETURN (0x6a) | OP_ORACLE (0xbf) | push(1) | version(1) | ...payload...
# So the hex prefix is 6abf01XX for single-byte version pushes.
#
# We assert:
#   (a) scriptPubKey hex begins with 6abf01
#   (b) extracted version byte is 03
#   (c) total scriptPubKey length is > 60 bytes (rules out compact v0x01)
print_header "Step 27E: On-chain Oracle Bundle Size & Version Check"
echo ""
echo "Mining up to 12 blocks and scanning all tx outputs for v0x03 MuSig2 bundle..."
echo ""

ORACLE_HEX=""
ORACLE_LEN_BYTES=0
ORACLE_VERSION_HEX=""
BUNDLE_BLOCK_HASH=""
BUNDLE_HEIGHT=""

for attempt in $(seq 1 12); do
    $BOB_CLI generatetoaddress 1 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
    sleep 2
    sync_all_nodes

    BUNDLE_BLOCK_HASH=$($BOB_CLI getbestblockhash 2>/dev/null)
    BUNDLE_BLOCK_JSON=$($BOB_CLI getblock "$BUNDLE_BLOCK_HASH" 2 2>/dev/null)
    BUNDLE_HEIGHT=$(echo "$BUNDLE_BLOCK_JSON" | jq -r '.height')

    ORACLE_HEX=$(echo "$BUNDLE_BLOCK_JSON" | jq -r '[.tx[].vout[].scriptPubKey.hex // empty | select(startswith("6abf"))][0] // ""')
    if [ -n "$ORACLE_HEX" ]; then
        ORACLE_LEN_BYTES=$(( ${#ORACLE_HEX} / 2 ))
        if [ ${#ORACLE_HEX} -ge 8 ]; then
            ORACLE_VERSION_HEX="${ORACLE_HEX:6:2}"
        fi
        echo "Found OP_ORACLE bundle in block $BUNDLE_HEIGHT on attempt $attempt"
        break
    fi
done

echo "Coinbase/tx OP_RETURN OP_ORACLE hex: $ORACLE_HEX"
echo "scriptPubKey size: $ORACLE_LEN_BYTES bytes"
echo "bundle version byte: ${ORACLE_VERSION_HEX:-N/A}"

if [ -z "$ORACLE_HEX" ]; then
    print_status "fail" "No OP_RETURN OP_ORACLE (6abf...) output found in 12 mined blocks"
elif [[ "$ORACLE_HEX" != 6abf01* ]]; then
    print_status "fail" "Bundle prefix wrong: hex starts with ${ORACLE_HEX:0:8}, expected 6abf01.. single-byte version push"
elif [[ "$ORACLE_VERSION_HEX" != "03" ]]; then
    print_status "fail" "Unexpected oracle bundle version byte: ${ORACLE_VERSION_HEX:-missing} (expected 03)"
elif [ "$ORACLE_LEN_BYTES" -le 60 ]; then
    print_status "fail" "Bundle too small: size=$ORACLE_LEN_BYTES bytes (expected > 60 for multi-oracle bundle)"
else
    print_status "ok" "v0x03 MuSig2 bundle present: prefix=6abf0103, size=$ORACLE_LEN_BYTES bytes (> 60)"
fi

# Restored wallets may later hold part of the distribution.
ALLOW_DD_DISTRIBUTION_DRIFT=1

# ====================================================================================
# WALLET PERSISTENCE TESTS - Testing DigiDollar survives wallet operations
# ====================================================================================

# ====================================================================================
# Step 28: WALLET RESTART TEST - Verify DD persists through wallet restart
# ====================================================================================
print_header "Step 28: WALLET RESTART TEST (Bob's Qt)"
echo ""
echo "Testing that DigiDollar balances persist through wallet restart..."
echo "This verifies wallet serialization is working correctly."
echo ""

# Record Bob's state BEFORE restart
BOB_DD_BEFORE_RESTART=$EXPECT_BOB_DD
BOB_DGB_BEFORE_RESTART=$(get_dgb_balance "$BOB_CLI" "bob")
BOB_POSITIONS_BEFORE=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq 'length')

echo "========== STATE BEFORE RESTART =========="
echo "  Bob DD Balance:    $BOB_DD_BEFORE_RESTART cents"
echo "  Bob DGB Balance:   $BOB_DGB_BEFORE_RESTART DGB"
echo "  Bob DD Positions:  $BOB_POSITIONS_BEFORE"
echo "==========================================="
echo ""

print_subheader "Stopping Bob's Qt wallet (graceful shutdown)..."
stop_qt_node "Bob" "$BOB_PID" "$BOB_CLI" "wallet restart"

echo ""
echo "Bob's Qt is stopped. Data directory preserved at: $BOB_DATADIR"
echo ""

print_subheader "Restarting Bob's Qt wallet..."
echo "Starting Bob's Qt with SAME data directory (no wipe)..."

# Restart Bob's Qt with the same datadir
setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$BOB_DATADIR \
    -port=$BOB_PORT \
    -rpcport=$BOB_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$ALICE_PORT \
    > /tmp/bob_testnet_restart.log 2>&1 &
BOB_PID=$!
echo "Bob's Qt restarted (New PID: $BOB_PID)"

# Wait for RPC to be ready
require_rpc_ready "$BOB_CLI" "Bob (restarted)" "Bob's Qt RPC is ready after restart" "Bob's Qt failed to restart"

# Small delay to ensure node is fully initialized
sleep 5

# Verify Bob's wallet is loaded - must explicitly load after restart
echo "Checking if Bob's wallet is loaded..."
LOADED_WALLETS=$($BOB_CLI listwallets 2>&1)
if echo "$LOADED_WALLETS" | jq -e '.[] | select(. == "bob")' > /dev/null 2>&1; then
    print_status "ok" "Bob's wallet 'bob' is already loaded"
else
    # Wallet not auto-loaded, must explicitly load it
    echo "Bob's wallet not auto-loaded, loading explicitly..."
    LOAD_RESULT=$($BOB_CLI loadwallet "bob" 2>&1)
    if echo "$LOAD_RESULT" | jq -e '.name' > /dev/null 2>&1; then
        print_status "ok" "Bob's wallet 'bob' loaded successfully"
    else
        print_status "fail" "Failed to load Bob's wallet: $LOAD_RESULT"
    fi
    sleep 3
fi

# Verify wallet is now accessible
WALLET_INFO=$($BOB_CLI -rpcwallet=bob getwalletinfo 2>&1)
if echo "$WALLET_INFO" | jq -e '.walletname' > /dev/null 2>&1; then
    echo "  Wallet name: $(echo "$WALLET_INFO" | jq -r '.walletname')"
else
    print_status "fail" "Bob's wallet still not accessible after load attempt"
fi

# Start all oracles on restarted node
echo "Restarting all 24 active oracles across the 8 nodes..."
refresh_local_p2p_links
start_all_oracles
sleep 2
refresh_oracle_prices

# Sync nodes
sync_all_nodes

print_subheader "Verifying Bob's DD balance after restart..."

# Get Bob's state AFTER restart
BOB_DD_AFTER_RESTART=$(get_dd_balance "$BOB_CLI" "bob")
BOB_DGB_AFTER_RESTART=$(get_dgb_balance "$BOB_CLI" "bob")
BOB_POSITIONS_AFTER=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq 'length')

echo "========== STATE AFTER RESTART =========="
echo "  Bob DD Balance:    $BOB_DD_AFTER_RESTART cents (expected: $BOB_DD_BEFORE_RESTART)"
echo "  Bob DGB Balance:   $BOB_DGB_AFTER_RESTART DGB"
echo "  Bob DD Positions:  $BOB_POSITIONS_AFTER (expected: $BOB_POSITIONS_BEFORE)"
echo "========================================="
echo ""

# Verify DD balance persisted
if [ "$BOB_DD_AFTER_RESTART" = "$BOB_DD_BEFORE_RESTART" ]; then
    print_status "ok" "DD BALANCE PERSISTED through restart! ($BOB_DD_AFTER_RESTART cents)"
else
    print_status "fail" "DD BALANCE CHANGED! Before: $BOB_DD_BEFORE_RESTART, After: $BOB_DD_AFTER_RESTART"
fi

# Verify positions count persisted
if [ "$BOB_POSITIONS_AFTER" = "$BOB_POSITIONS_BEFORE" ]; then
    print_status "ok" "DD POSITIONS PERSISTED through restart! ($BOB_POSITIONS_AFTER positions)"
else
    print_status "fail" "DD POSITIONS CHANGED! Before: $BOB_POSITIONS_BEFORE, After: $BOB_POSITIONS_AFTER"
fi

# List positions to verify details
echo ""
echo "Bob's DD Positions after restart:"
$BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq -r '.[] | "  [\(.status)] \(.dd_minted) cents - tier \(.lock_tier) - \(.position_id[0:12])..."' 2>/dev/null || echo "  Error reading positions"

verify_all_balances "After Wallet Restart Test"

# ====================================================================================
# Step 29: WALLET BACKUP/RESTORE TEST - Verify DD persists through backup/restore
# ====================================================================================
print_header "Step 29: WALLET BACKUP/RESTORE TEST (Bob's Qt)"
echo ""
echo "Testing that DigiDollar balances persist through wallet backup and restore..."
echo "This verifies wallet backup serialization includes all DD metadata."
echo ""

# Record state before backup
BOB_DD_BEFORE_BACKUP=$EXPECT_BOB_DD
BOB_DGB_BEFORE_BACKUP=$(get_dgb_balance "$BOB_CLI" "bob")
BOB_POSITIONS_BEFORE_BACKUP=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq 'length')
BACKUP_FILE="/tmp/bob_wallet_backup_$(date +%s).dat"

echo "========== STATE BEFORE BACKUP =========="
echo "  Bob DD Balance:    $BOB_DD_BEFORE_BACKUP cents"
echo "  Bob DGB Balance:   $BOB_DGB_BEFORE_BACKUP DGB"
echo "  Bob DD Positions:  $BOB_POSITIONS_BEFORE_BACKUP"
echo "  Backup file:       $BACKUP_FILE"
echo "========================================="
echo ""

print_subheader "Creating wallet backup..."

# Create backup
set +e
BACKUP_RESULT=$($BOB_CLI -rpcwallet=bob backupwallet "$BACKUP_FILE" 2>&1)
BACKUP_EXIT=$?
set -e

if [ $BACKUP_EXIT -eq 0 ] && [ -f "$BACKUP_FILE" ]; then
    BACKUP_SIZE=$(ls -la "$BACKUP_FILE" | awk '{print $5}')
    print_status "ok" "Wallet backup created: $BACKUP_FILE ($BACKUP_SIZE bytes)"
else
    print_status "fail" "Wallet backup failed: $BACKUP_RESULT"
fi

print_subheader "Stopping Bob's Qt for restore test..."
stop_qt_node "Bob" "$BOB_PID" "$BOB_CLI" "restore test"

print_subheader "Simulating wallet loss by moving the original wallet directory..."

ORIGINAL_WALLET_DIR="$BOB_DATADIR/$TESTNET_SUBDIR/wallets/bob"
ORIGINAL_WALLET_BACKUP="${ORIGINAL_WALLET_DIR}.original_backup"
if [ -e "$ORIGINAL_WALLET_DIR" ]; then
    rm -rf "$ORIGINAL_WALLET_BACKUP"
    mv "$ORIGINAL_WALLET_DIR" "$ORIGINAL_WALLET_BACKUP"
    print_status "ok" "Original wallet directory moved to simulate loss"
else
    print_status "fail" "Original wallet directory missing before restore test: $ORIGINAL_WALLET_DIR"
    exit 1
fi

print_subheader "Restarting Bob's Qt before restorewallet..."

setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$BOB_DATADIR \
    -port=$BOB_PORT \
    -rpcport=$BOB_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -connect=127.0.0.1:$ALICE_PORT \
    > /tmp/bob_testnet_restore.log 2>&1 &
BOB_PID=$!
echo "Bob's Qt restarted with restored wallet (New PID: $BOB_PID)"

require_rpc_ready "$BOB_CLI" "Bob (restored)" "Bob's Qt RPC is ready after restore" "Bob's Qt failed to start after restore"

sleep 5

print_subheader "Restoring wallet from backup via restorewallet RPC..."

set +e
RESTORE_RESULT=$($BOB_CLI restorewallet "bob" "$BACKUP_FILE" 2>&1)
RESTORE_EXIT=$?
set -e

if [ $RESTORE_EXIT -eq 0 ] && echo "$RESTORE_RESULT" | jq -e '.name == "bob"' >/dev/null 2>&1; then
    print_status "ok" "Wallet restored from backup file via restorewallet"
else
    print_status "fail" "restorewallet failed: $RESTORE_RESULT"
    exit 1
fi

sleep 2

# Restart all oracles
refresh_local_p2p_links
start_all_oracles
sleep 2
refresh_oracle_prices

sync_all_nodes

print_subheader "Verifying Bob's DD balance after restore..."

BOB_DD_AFTER_RESTORE=$(get_dd_balance "$BOB_CLI" "bob")
BOB_DGB_AFTER_RESTORE=$(get_dgb_balance "$BOB_CLI" "bob")
BOB_POSITIONS_AFTER_RESTORE=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq 'length')

echo "========== STATE AFTER RESTORE =========="
echo "  Bob DD Balance:    $BOB_DD_AFTER_RESTORE cents (expected: $BOB_DD_BEFORE_BACKUP)"
echo "  Bob DGB Balance:   $BOB_DGB_AFTER_RESTORE DGB"
echo "  Bob DD Positions:  $BOB_POSITIONS_AFTER_RESTORE (expected: $BOB_POSITIONS_BEFORE_BACKUP)"
echo "========================================="
echo ""

if [ "$BOB_DD_AFTER_RESTORE" = "$BOB_DD_BEFORE_BACKUP" ]; then
    print_status "ok" "DD BALANCE RESTORED correctly! ($BOB_DD_AFTER_RESTORE cents)"
else
    print_status "fail" "DD BALANCE MISMATCH after restore! Before: $BOB_DD_BEFORE_BACKUP, After: $BOB_DD_AFTER_RESTORE"
fi

if [ "$BOB_POSITIONS_AFTER_RESTORE" = "$BOB_POSITIONS_BEFORE_BACKUP" ]; then
    print_status "ok" "DD POSITIONS RESTORED correctly! ($BOB_POSITIONS_AFTER_RESTORE positions)"
else
    print_status "fail" "DD POSITIONS MISMATCH! Before: $BOB_POSITIONS_BEFORE_BACKUP, After: $BOB_POSITIONS_AFTER_RESTORE"
fi

verify_all_balances "After Wallet Backup/Restore Test"

# ====================================================================================
# Step 30: REINDEX TEST - Verify DD rebuilds correctly during chain reindex
# ====================================================================================
print_header "Step 30: REINDEX TEST (Bob's Qt)"
echo ""
echo "Testing that DigiDollar balances rebuild correctly during -reindex..."
echo "This verifies the DD UTXO scanner and index reconstruction work properly."
echo "NOTE: This may take a few minutes as the entire chain is rescanned."
echo ""

# Record state before reindex
BOB_DD_BEFORE_REINDEX=$EXPECT_BOB_DD
BOB_DGB_BEFORE_REINDEX=$(get_dgb_balance "$BOB_CLI" "bob")
BOB_POSITIONS_BEFORE_REINDEX=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq 'length')
CHAIN_HEIGHT_BEFORE=$($BOB_CLI getblockcount)

echo "========== STATE BEFORE REINDEX =========="
echo "  Bob DD Balance:    $BOB_DD_BEFORE_REINDEX cents"
echo "  Bob DGB Balance:   $BOB_DGB_BEFORE_REINDEX DGB"
echo "  Bob DD Positions:  $BOB_POSITIONS_BEFORE_REINDEX"
echo "  Chain Height:      $CHAIN_HEIGHT_BEFORE blocks"
echo "==========================================="
echo ""

print_subheader "Stopping Bob's Qt for reindex..."
stop_qt_node "Bob" "$BOB_PID" "$BOB_CLI" "reindex"

print_subheader "Starting Bob's Qt with -reindex flag..."
echo "This will rescan the entire blockchain and rebuild all indexes..."
echo "You should see the Qt window show 'Reindexing blocks on disk...' progress"
echo ""

setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$BOB_DATADIR \
    -port=$BOB_PORT \
    -rpcport=$BOB_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -reindex \
    -connect=127.0.0.1:$ALICE_PORT \
    > /tmp/bob_testnet_reindex.log 2>&1 &
BOB_PID=$!
echo "Bob's Qt started with -reindex (New PID: $BOB_PID)"
echo ""
echo "Waiting for reindex to complete (this may take 1-3 minutes)..."

# Wait for RPC with longer timeout for reindex
for i in {1..180}; do
    if $BOB_CLI getblockchaininfo > /dev/null 2>&1; then
        CURRENT_HEIGHT=$($BOB_CLI getblockcount 2>/dev/null || echo "0")
        if [ "$CURRENT_HEIGHT" -ge "$CHAIN_HEIGHT_BEFORE" ]; then
            echo ""
            print_status "ok" "Reindex complete! Chain at height $CURRENT_HEIGHT"
            break
        else
            # Show progress every 10 seconds
            if [ $((i % 10)) -eq 0 ]; then
                PROGRESS=$($BOB_CLI getblockchaininfo 2>/dev/null | jq -r '.verificationprogress // 0')
                echo "  Reindex progress: $(echo "$PROGRESS * 100" | bc)% (height: $CURRENT_HEIGHT / $CHAIN_HEIGHT_BEFORE)"
            fi
        fi
    fi
    sleep 1
done

# Extra wait for wallet to fully load after reindex
sleep 10

# Load wallet if needed
$BOB_CLI loadwallet "bob" 2>/dev/null || true
sleep 3

# Restart all oracles after reindex
refresh_local_p2p_links
start_all_oracles
sleep 2
refresh_oracle_prices

sync_all_nodes

print_subheader "Verifying Bob's DD balance after reindex..."

BOB_DD_AFTER_REINDEX=$(get_dd_balance "$BOB_CLI" "bob")
BOB_DGB_AFTER_REINDEX=$(get_dgb_balance "$BOB_CLI" "bob")
BOB_POSITIONS_AFTER_REINDEX=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq 'length')
CHAIN_HEIGHT_AFTER=$($BOB_CLI getblockcount)

echo "========== STATE AFTER REINDEX =========="
echo "  Bob DD Balance:    $BOB_DD_AFTER_REINDEX cents (expected: $BOB_DD_BEFORE_REINDEX)"
echo "  Bob DGB Balance:   $BOB_DGB_AFTER_REINDEX DGB"
echo "  Bob DD Positions:  $BOB_POSITIONS_AFTER_REINDEX (expected: $BOB_POSITIONS_BEFORE_REINDEX)"
echo "  Chain Height:      $CHAIN_HEIGHT_AFTER blocks"
echo "========================================="
echo ""

if [ "$BOB_DD_AFTER_REINDEX" = "$BOB_DD_BEFORE_REINDEX" ]; then
    print_status "ok" "DD BALANCE REBUILT correctly after reindex! ($BOB_DD_AFTER_REINDEX cents)"
else
    print_status "fail" "DD BALANCE MISMATCH after reindex! Before: $BOB_DD_BEFORE_REINDEX, After: $BOB_DD_AFTER_REINDEX"
fi

if [ "$BOB_POSITIONS_AFTER_REINDEX" = "$BOB_POSITIONS_BEFORE_REINDEX" ]; then
    print_status "ok" "DD POSITIONS REBUILT correctly after reindex! ($BOB_POSITIONS_AFTER_REINDEX positions)"
else
    print_status "fail" "DD POSITIONS MISMATCH! Before: $BOB_POSITIONS_BEFORE_REINDEX, After: $BOB_POSITIONS_AFTER_REINDEX"
fi

# Verify oracle price cache was rebuilt
ORACLE_STATUS=$($BOB_CLI getoracleprice 2>/dev/null | jq -r '.status // "inactive"')
if [ "$ORACLE_STATUS" = "active" ]; then
    ORACLE_CACHE_REBUILT=true
    print_status "ok" "Oracle price cache rebuilt after reindex"
else
    ORACLE_CACHE_REBUILT=false
    print_status "warn" "Oracle status after reindex: $ORACLE_STATUS"
fi

echo ""
echo "Bob's DD Positions after reindex:"
$BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq -r '.[] | "  [\(.status)] \(.dd_minted) cents - tier \(.lock_tier) - \(.position_id[0:12])..."' 2>/dev/null || echo "  Error reading positions"

verify_all_balances "After Reindex Test"

# ====================================================================================
# PERSISTENCE TEST SUMMARY
# ====================================================================================
print_header "WALLET PERSISTENCE TEST SUMMARY"
echo ""
echo "========== PERSISTENCE TEST RESULTS =========="
echo ""
echo "TEST 28 - WALLET RESTART:"
echo "  DD Balance persisted: $BOB_DD_BEFORE_RESTART -> $BOB_DD_AFTER_RESTART cents"
echo "  Positions persisted:  $BOB_POSITIONS_BEFORE -> $BOB_POSITIONS_AFTER"
echo ""
echo "TEST 29 - WALLET BACKUP/RESTORE:"
echo "  DD Balance restored:  $BOB_DD_BEFORE_BACKUP -> $BOB_DD_AFTER_RESTORE cents"
echo "  Positions restored:   $BOB_POSITIONS_BEFORE_BACKUP -> $BOB_POSITIONS_AFTER_RESTORE"
echo ""
echo "TEST 30 - CHAIN REINDEX:"
echo "  DD Balance rebuilt:   $BOB_DD_BEFORE_REINDEX -> $BOB_DD_AFTER_REINDEX cents"
echo "  Positions rebuilt:    $BOB_POSITIONS_BEFORE_REINDEX -> $BOB_POSITIONS_AFTER_REINDEX"
echo "  Chain height:         $CHAIN_HEIGHT_BEFORE -> $CHAIN_HEIGHT_AFTER"
echo ""
echo "=============================================="
echo ""

# ====================================================================================
# ALICE WALLET PERSISTENCE TESTS - Steps 31-33
# ====================================================================================

# ====================================================================================
# Step 31: ALICE RESCANBLOCKCHAIN TEST - Test wallet rescan restores DD
# ====================================================================================
print_header "Step 31: ALICE RESCANBLOCKCHAIN TEST"
echo ""
echo "Testing that DigiDollar balances are correctly restored after rescanblockchain..."
echo "This is the KEY TEST for the wallet restore fix (descriptor import workflow)."
echo ""

# Record Alice's state BEFORE rescan
ALICE_DD_BEFORE_RESCAN=$EXPECT_ALICE_DD
ALICE_DGB_BEFORE_RESCAN=$(get_dgb_balance "$ALICE_CLI" "alice")
ALICE_POSITIONS_BEFORE_RESCAN=$($ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null | jq 'length')

# Get detailed position info for verification
echo "Recording Alice's DD position details before rescan..."
ALICE_POSITIONS_DETAIL_BEFORE=$($ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null)
echo "$ALICE_POSITIONS_DETAIL_BEFORE" | jq -r '.[] | "  Position: \(.position_id[0:16])... dd_minted=\(.dd_minted) tier=\(.lock_tier)"' 2>/dev/null

echo ""
echo "========== ALICE STATE BEFORE RESCAN =========="
echo "  Alice DD Balance:    $ALICE_DD_BEFORE_RESCAN cents"
echo "  Alice DGB Balance:   $ALICE_DGB_BEFORE_RESCAN DGB"
echo "  Alice DD Positions:  $ALICE_POSITIONS_BEFORE_RESCAN"
echo "==============================================="
echo ""

print_subheader "Performing rescanblockchain on Alice's wallet..."
echo "This will clear and rebuild DD state from blockchain..."

RESCAN_START=$(date +%s)
set +e
RESCAN_RESULT=$($ALICE_CLI -rpcwallet=alice rescanblockchain 2>&1)
RESCAN_EXIT=$?
set -e
RESCAN_END=$(date +%s)
RESCAN_DURATION=$((RESCAN_END - RESCAN_START))

if [ $RESCAN_EXIT -eq 0 ]; then
    START_HEIGHT=$(echo "$RESCAN_RESULT" | jq -r '.start_height // 0')
    STOP_HEIGHT=$(echo "$RESCAN_RESULT" | jq -r '.stop_height // 0')
    print_status "ok" "Rescan completed in ${RESCAN_DURATION}s (blocks $START_HEIGHT to $STOP_HEIGHT)"
else
    print_status "fail" "Rescan failed: $RESCAN_RESULT"
fi

# Small delay to ensure wallet state is updated
sleep 5

print_subheader "Verifying Alice's DD balance after rescan..."

ALICE_DD_AFTER_RESCAN=$(get_dd_balance "$ALICE_CLI" "alice")
ALICE_DGB_AFTER_RESCAN=$(get_dgb_balance "$ALICE_CLI" "alice")
ALICE_POSITIONS_AFTER_RESCAN=$($ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null | jq 'length')

echo ""
echo "========== ALICE STATE AFTER RESCAN =========="
echo "  Alice DD Balance:    $ALICE_DD_AFTER_RESCAN cents (expected: $ALICE_DD_BEFORE_RESCAN)"
echo "  Alice DGB Balance:   $ALICE_DGB_AFTER_RESCAN DGB"
echo "  Alice DD Positions:  $ALICE_POSITIONS_AFTER_RESCAN (expected: $ALICE_POSITIONS_BEFORE_RESCAN)"
echo "=============================================="
echo ""

# Verify DD balance persisted through rescan
if [ "$ALICE_DD_AFTER_RESCAN" = "$ALICE_DD_BEFORE_RESCAN" ]; then
    print_status "ok" "ALICE DD BALANCE RESTORED after rescan! ($ALICE_DD_AFTER_RESCAN cents)"
else
    print_status "fail" "ALICE DD BALANCE MISMATCH! Before: $ALICE_DD_BEFORE_RESCAN, After: $ALICE_DD_AFTER_RESCAN"
fi

# Verify positions count
if [ "$ALICE_POSITIONS_AFTER_RESCAN" = "$ALICE_POSITIONS_BEFORE_RESCAN" ]; then
    print_status "ok" "ALICE DD POSITIONS RESTORED after rescan! ($ALICE_POSITIONS_AFTER_RESCAN positions)"
else
    print_status "fail" "ALICE POSITIONS MISMATCH! Before: $ALICE_POSITIONS_BEFORE_RESCAN, After: $ALICE_POSITIONS_AFTER_RESCAN"
fi

# Show position details after rescan
echo ""
echo "Alice's DD Positions after rescan:"
$ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null | jq -r '.[] | "  [\(.status)] \(.dd_minted) cents - tier \(.lock_tier) - \(.position_id[0:12])..."' 2>/dev/null || echo "  Error reading positions"

verify_all_balances "After Alice Rescan Test"

# ====================================================================================
# Step 32: ALICE REINDEX TEST - Test full chain reindex restores DD
# ====================================================================================
print_header "Step 32: ALICE REINDEX TEST"
echo ""
echo "Testing that DigiDollar balances rebuild correctly during -reindex for Alice..."
echo "This verifies DD state is properly reconstructed from blockchain data."
echo ""

# Record state before reindex
ALICE_DD_BEFORE_REINDEX=$EXPECT_ALICE_DD
ALICE_DGB_BEFORE_REINDEX=$(get_dgb_balance "$ALICE_CLI" "alice")
ALICE_POSITIONS_BEFORE_REINDEX=$($ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null | jq 'length')
ALICE_CHAIN_HEIGHT_BEFORE=$($ALICE_CLI getblockcount)

echo "========== ALICE STATE BEFORE REINDEX =========="
echo "  Alice DD Balance:    $ALICE_DD_BEFORE_REINDEX cents"
echo "  Alice DGB Balance:   $ALICE_DGB_BEFORE_REINDEX DGB"
echo "  Alice DD Positions:  $ALICE_POSITIONS_BEFORE_REINDEX"
echo "  Chain Height:        $ALICE_CHAIN_HEIGHT_BEFORE blocks"
echo "================================================"
echo ""

print_subheader "Stopping Alice's Qt for reindex..."
stop_qt_node "Alice" "$ALICE_PID" "$ALICE_CLI" "reindex"

print_subheader "Starting Alice's Qt with -reindex flag..."
echo "This will rescan the entire blockchain and rebuild all indexes..."

setsid env -i \
    DISPLAY="${DISPLAY}" \
    XAUTHORITY="${XAUTHORITY}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE}" \
    HOME="${HOME}" \
    USER="${USER}" \
    PATH="${PATH}" \
    ./src/qt/digibyte-qt \
    -testnet \
    -easypow \
    -datadir=$ALICE_DATADIR \
    -port=$ALICE_PORT \
    -rpcport=$ALICE_RPC \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -debug=digidollar \
    -reindex \
    -connect=127.0.0.1:$BOB_PORT \
    > /tmp/alice_testnet_reindex.log 2>&1 &
ALICE_PID=$!
echo "Alice's Qt started with -reindex (New PID: $ALICE_PID)"
echo ""
echo "Waiting for reindex to complete (this may take 1-3 minutes)..."

# Wait for RPC with longer timeout for reindex
for i in {1..180}; do
    if $ALICE_CLI getblockchaininfo > /dev/null 2>&1; then
        CURRENT_HEIGHT=$($ALICE_CLI getblockcount 2>/dev/null || echo "0")
        if [ "$CURRENT_HEIGHT" -ge "$ALICE_CHAIN_HEIGHT_BEFORE" ]; then
            echo ""
            print_status "ok" "Alice reindex complete! Chain at height $CURRENT_HEIGHT"
            break
        else
            if [ $((i % 10)) -eq 0 ]; then
                PROGRESS=$($ALICE_CLI getblockchaininfo 2>/dev/null | jq -r '.verificationprogress // 0')
                echo "  Reindex progress: $(echo "$PROGRESS * 100" | bc)% (height: $CURRENT_HEIGHT / $ALICE_CHAIN_HEIGHT_BEFORE)"
            fi
        fi
    fi
    sleep 1
done

# Extra wait for wallet to fully load after reindex
sleep 10

# Load wallet if needed
$ALICE_CLI loadwallet "alice" 2>/dev/null || true
sleep 3

refresh_local_p2p_links
sync_all_nodes

print_subheader "Verifying Alice's DD balance after reindex..."

ALICE_DD_AFTER_REINDEX=$(get_dd_balance "$ALICE_CLI" "alice")
ALICE_DGB_AFTER_REINDEX=$(get_dgb_balance "$ALICE_CLI" "alice")
ALICE_POSITIONS_AFTER_REINDEX=$($ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null | jq 'length')
ALICE_CHAIN_HEIGHT_AFTER=$($ALICE_CLI getblockcount)

echo ""
echo "========== ALICE STATE AFTER REINDEX =========="
echo "  Alice DD Balance:    $ALICE_DD_AFTER_REINDEX cents (expected: $ALICE_DD_BEFORE_REINDEX)"
echo "  Alice DGB Balance:   $ALICE_DGB_AFTER_REINDEX DGB"
echo "  Alice DD Positions:  $ALICE_POSITIONS_AFTER_REINDEX (expected: $ALICE_POSITIONS_BEFORE_REINDEX)"
echo "  Chain Height:        $ALICE_CHAIN_HEIGHT_AFTER blocks"
echo "==============================================="
echo ""

if [ "$ALICE_DD_AFTER_REINDEX" = "$ALICE_DD_BEFORE_REINDEX" ]; then
    print_status "ok" "ALICE DD BALANCE REBUILT correctly after reindex! ($ALICE_DD_AFTER_REINDEX cents)"
else
    print_status "fail" "ALICE DD BALANCE MISMATCH after reindex! Before: $ALICE_DD_BEFORE_REINDEX, After: $ALICE_DD_AFTER_REINDEX"
fi

if [ "$ALICE_POSITIONS_AFTER_REINDEX" = "$ALICE_POSITIONS_BEFORE_REINDEX" ]; then
    print_status "ok" "ALICE DD POSITIONS REBUILT correctly after reindex! ($ALICE_POSITIONS_AFTER_REINDEX positions)"
else
    print_status "fail" "ALICE POSITIONS MISMATCH! Before: $ALICE_POSITIONS_BEFORE_REINDEX, After: $ALICE_POSITIONS_AFTER_REINDEX"
fi

echo ""
echo "Alice's DD Positions after reindex:"
$ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null | jq -r '.[] | "  [\(.status)] \(.dd_minted) cents - tier \(.lock_tier) - \(.position_id[0:12])..."' 2>/dev/null || echo "  Error reading positions"

verify_all_balances "After Alice Reindex Test"

# ====================================================================================
# Step 33: ALICE EXPORT-REIMPORT WALLET TEST (Descriptor-based wallet restore)
# ====================================================================================
print_header "Step 33: ALICE EXPORT-REIMPORT WALLET TEST"
echo ""
echo "Testing the FULL wallet restore workflow via descriptor export/import..."
echo "This is the CRITICAL TEST for the DigiDollar wallet restore fix!"
echo ""
echo "Workflow:"
echo "  1. Export Alice's wallet descriptors (listdescriptors true)"
echo "  2. Create new wallet 'alice_restored'"
echo "  3. Import descriptors into new wallet"
echo "  4. Rescan blockchain"
echo "  5. Verify DD positions and balances match original"
echo ""

# Record Alice's original state
ALICE_DD_BEFORE_EXPORT=$EXPECT_ALICE_DD
ALICE_DGB_BEFORE_EXPORT=$(get_dgb_balance "$ALICE_CLI" "alice")
ALICE_POSITIONS_BEFORE_EXPORT=$($ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null | jq 'length')

# Get detailed position info
ALICE_POSITIONS_DETAIL_BEFORE=$($ALICE_CLI -rpcwallet=alice listdigidollarpositions 2>/dev/null)

echo "========== ALICE ORIGINAL STATE =========="
echo "  Alice DD Balance:    $ALICE_DD_BEFORE_EXPORT cents"
echo "  Alice DGB Balance:   $ALICE_DGB_BEFORE_EXPORT DGB"
echo "  Alice DD Positions:  $ALICE_POSITIONS_BEFORE_EXPORT"
echo ""
echo "Position Details:"
echo "$ALICE_POSITIONS_DETAIL_BEFORE" | jq -r '.[] | "  \(.position_id[0:16])... | DD: \(.dd_minted) cents | DGB: \(.dgb_collateral) | tier: \(.lock_tier) | status: \(.status)"' 2>/dev/null
echo "==========================================="
echo ""

print_subheader "Step 33a: Exporting wallet descriptors..."

# Export descriptors with private keys
set +e
DESCRIPTORS=$($ALICE_CLI -rpcwallet=alice listdescriptors true 2>&1)
EXPORT_EXIT=$?
set -e

if [ $EXPORT_EXIT -eq 0 ] && echo "$DESCRIPTORS" | jq -e '.descriptors' > /dev/null 2>&1; then
    DESC_COUNT=$(echo "$DESCRIPTORS" | jq '.descriptors | length')
    print_status "ok" "Exported $DESC_COUNT descriptors from Alice's wallet"

    # Save to file for debugging
    echo "$DESCRIPTORS" > /tmp/alice_descriptors.json
    echo "  Descriptors saved to: /tmp/alice_descriptors.json"
else
    print_status "fail" "Failed to export descriptors: $DESCRIPTORS"
fi

print_subheader "Step 33b: Creating new wallet 'alice_restored'..."

# Create new blank wallet
set +e
CREATE_RESULT=$($ALICE_CLI createwallet "alice_restored" false true "" false true 2>&1)
CREATE_EXIT=$?
set -e

if [ $CREATE_EXIT -eq 0 ]; then
    print_status "ok" "Created new blank wallet 'alice_restored'"
else
    # Wallet might already exist, try to unload and recreate
    $ALICE_CLI unloadwallet "alice_restored" 2>/dev/null || true
    sleep 1

    # Delete old wallet directory if exists
    rm -rf "$ALICE_DATADIR/$TESTNET_SUBDIR/wallets/alice_restored" 2>/dev/null || true

    CREATE_RESULT=$($ALICE_CLI createwallet "alice_restored" false true "" false true 2>&1)
    if echo "$CREATE_RESULT" | jq -e '.name' > /dev/null 2>&1; then
        print_status "ok" "Created new blank wallet 'alice_restored' (after cleanup)"
    else
        print_status "fail" "Failed to create wallet: $CREATE_RESULT"
    fi
fi

print_subheader "Step 33c: Importing descriptors into new wallet..."

# Prepare descriptors for import (add timestamp, default missing fields to false)
# Note: Some descriptors may not have 'internal' field (single-key vs ranged descriptors)
IMPORT_DESCS=$(echo "$DESCRIPTORS" | jq '[.descriptors[] | {desc: .desc, timestamp: "now", active: (.active // false), internal: (.internal // false)}]')

# Save import request for debugging
echo "$IMPORT_DESCS" > /tmp/alice_import_request.json
echo "  Import request saved to: /tmp/alice_import_request.json"

# Import descriptors
set +e
IMPORT_RESULT=$($ALICE_CLI -rpcwallet=alice_restored importdescriptors "$IMPORT_DESCS" 2>&1)
IMPORT_EXIT=$?
set -e

if [ $IMPORT_EXIT -eq 0 ]; then
    SUCCESS_COUNT=$(echo "$IMPORT_RESULT" | jq '[.[] | select(.success == true)] | length')
    TOTAL_COUNT=$(echo "$IMPORT_RESULT" | jq 'length')
    print_status "ok" "Imported $SUCCESS_COUNT/$TOTAL_COUNT descriptors successfully"

    # Check for any failures
    FAILED=$(echo "$IMPORT_RESULT" | jq '[.[] | select(.success != true)]')
    if [ "$(echo "$FAILED" | jq 'length')" -gt 0 ]; then
        echo "  Warning: Some imports failed:"
        echo "$FAILED" | jq -r '.[] | "    - \(.error.message // "unknown error")"'
    fi
else
    print_status "fail" "Descriptor import failed: $IMPORT_RESULT"
fi

print_subheader "Step 33d: Rescanning blockchain for restored wallet..."

echo ""
echo "Testing that DigiDollar is AUTOMATICALLY restored via rescan."
echo "NO export/import needed - DD state should be reconstructed from blockchain!"
echo ""

echo "Running rescanblockchain on alice_restored..."
RESCAN_START=$(date +%s)
set +e
RESCAN_RESULT=$($ALICE_CLI -rpcwallet=alice_restored rescanblockchain 2>&1)
RESCAN_EXIT=$?
set -e
RESCAN_END=$(date +%s)
RESCAN_DURATION=$((RESCAN_END - RESCAN_START))

if [ $RESCAN_EXIT -eq 0 ]; then
    START_HEIGHT=$(echo "$RESCAN_RESULT" | jq -r '.start_height // 0')
    STOP_HEIGHT=$(echo "$RESCAN_RESULT" | jq -r '.stop_height // 0')
    print_status "ok" "Rescan completed in ${RESCAN_DURATION}s (blocks $START_HEIGHT to $STOP_HEIGHT)"
else
    print_status "fail" "Rescan failed: $RESCAN_RESULT"
fi

# Wait for wallet to fully process
sleep 5

print_subheader "Step 33e: Verifying restored wallet DD state (NO export/import)..."

# Get restored wallet state
ALICE_RESTORED_DD=$(get_dd_balance "$ALICE_CLI" "alice_restored")
ALICE_RESTORED_DGB=$(get_dgb_balance "$ALICE_CLI" "alice_restored")
ALICE_RESTORED_POSITIONS=$($ALICE_CLI -rpcwallet=alice_restored listdigidollarpositions 2>/dev/null | jq 'length')

# Get detailed position info from restored wallet
ALICE_RESTORED_POSITIONS_DETAIL=$($ALICE_CLI -rpcwallet=alice_restored listdigidollarpositions 2>/dev/null)

echo ""
echo "========== RESTORED WALLET STATE =========="
echo "  DD Balance:    $ALICE_RESTORED_DD cents (expected: $ALICE_DD_BEFORE_EXPORT)"
echo "  DGB Balance:   $ALICE_RESTORED_DGB DGB (expected: $ALICE_DGB_BEFORE_EXPORT)"
echo "  DD Positions:  $ALICE_RESTORED_POSITIONS (expected: $ALICE_POSITIONS_BEFORE_EXPORT)"
echo ""
echo "Restored Position Details:"
echo "$ALICE_RESTORED_POSITIONS_DETAIL" | jq -r '.[] | "  \(.position_id[0:16])... | DD: \(.dd_minted) cents | DGB: \(.dgb_collateral) | tier: \(.lock_tier) | status: \(.status)"' 2>/dev/null || echo "  No positions found"
echo "==========================================="
echo ""

# THE KEY VERIFICATION - DD balance must match
if [ "$ALICE_RESTORED_DD" = "$ALICE_DD_BEFORE_EXPORT" ]; then
    print_status "ok" "DD BALANCE RESTORED CORRECTLY! ($ALICE_RESTORED_DD cents)"
else
    print_status "fail" "DD BALANCE MISMATCH! Original: $ALICE_DD_BEFORE_EXPORT, Restored: $ALICE_RESTORED_DD"
fi

# Verify position count
if [ "$ALICE_RESTORED_POSITIONS" = "$ALICE_POSITIONS_BEFORE_EXPORT" ]; then
    print_status "ok" "DD POSITIONS RESTORED CORRECTLY! ($ALICE_RESTORED_POSITIONS positions)"
else
    print_status "fail" "POSITIONS MISMATCH! Original: $ALICE_POSITIONS_BEFORE_EXPORT, Restored: $ALICE_RESTORED_POSITIONS"
fi

# Verify DGB balance matches (may differ slightly due to fees)
ALICE_DGB_DIFF=$(echo "$ALICE_RESTORED_DGB - $ALICE_DGB_BEFORE_EXPORT" | bc 2>/dev/null || echo "unknown")
if [ "$ALICE_RESTORED_DGB" = "$ALICE_DGB_BEFORE_EXPORT" ]; then
    print_status "ok" "DGB BALANCE RESTORED CORRECTLY! ($ALICE_RESTORED_DGB DGB)"
else
    print_status "warn" "DGB balance differs by $ALICE_DGB_DIFF DGB (may be due to fees)"
fi

# Compare position details (sort by dd_minted for consistent comparison)
echo ""
echo "Comparing position details..."

ORIGINAL_AMOUNTS=$(echo "$ALICE_POSITIONS_DETAIL_BEFORE" | jq -r '[.[] | .dd_minted] | sort | join(",")')
RESTORED_AMOUNTS=$(echo "$ALICE_RESTORED_POSITIONS_DETAIL" | jq -r '[.[] | .dd_minted] | sort | join(",")')

if [ "$ORIGINAL_AMOUNTS" = "$RESTORED_AMOUNTS" ]; then
    print_status "ok" "Position DD amounts match! ($ORIGINAL_AMOUNTS)"
else
    print_status "fail" "Position amounts differ! Original: $ORIGINAL_AMOUNTS, Restored: $RESTORED_AMOUNTS"
fi

ORIGINAL_TIERS=$(echo "$ALICE_POSITIONS_DETAIL_BEFORE" | jq -r '[.[] | .lock_tier] | sort | join(",")')
RESTORED_TIERS=$(echo "$ALICE_RESTORED_POSITIONS_DETAIL" | jq -r '[.[] | .lock_tier] | sort | join(",")')

if [ "$ORIGINAL_TIERS" = "$RESTORED_TIERS" ]; then
    print_status "ok" "Position lock tiers match! ($ORIGINAL_TIERS)"
else
    print_status "fail" "Position tiers differ! Original: $ORIGINAL_TIERS, Restored: $RESTORED_TIERS"
fi

print_subheader "Step 33f: Testing DD operations on restored wallet..."

# Test getting a new DD address from restored wallet
echo "Testing getdigidollaraddress on restored wallet..."
set +e
RESTORED_DD_ADDR=$($ALICE_CLI -rpcwallet=alice_restored getdigidollaraddress 2>&1)
ADDR_EXIT=$?
set -e

if [ $ADDR_EXIT -eq 0 ] && [ -n "$RESTORED_DD_ADDR" ] && [ "$RESTORED_DD_ADDR" != "null" ]; then
    print_status "ok" "Can generate DD addresses from restored wallet: ${RESTORED_DD_ADDR:0:20}..."
else
    print_status "fail" "Cannot generate DD address from restored wallet: $RESTORED_DD_ADDR"
fi

# Test DD transfer from restored wallet (send back to original Alice wallet)
if [ "$ALICE_RESTORED_DD" -gt 100 ]; then
    echo "Testing senddigidollar from restored wallet (sending 100 cents back to original)..."

    ORIGINAL_ALICE_DD_ADDR=$($ALICE_CLI -rpcwallet=alice getdigidollaraddress 2>/dev/null)

    set +e
    SEND_RESULT=$($ALICE_CLI -rpcwallet=alice_restored senddigidollar "$ORIGINAL_ALICE_DD_ADDR" 100 2>&1)
    SEND_EXIT=$?
    set -e

    if [ $SEND_EXIT -eq 0 ] && echo "$SEND_RESULT" | jq -e '.txid' > /dev/null 2>&1; then
        SEND_TXID=$(echo "$SEND_RESULT" | jq -r '.txid')
        print_status "ok" "DD transfer from restored wallet SUCCESSFUL! TX: ${SEND_TXID:0:16}..."

        # Update expected balances
        ALICE_RESTORED_DD=$((ALICE_RESTORED_DD - 100))

        # Mine to confirm
        $BOB_CLI generatetoaddress 2 "$BOB_ADDR" 2000000000 "sha256d" > /dev/null 2>&1
        sleep 3
        sync_all_nodes

        # alice and alice_restored share keys; coin selection/change attribution can
        # shift visible balance as wallet indices catch up. Re-anchor EXPECT_ALICE_DD
        # from live wallet state after confirmation + sync.
        ALICE_ACTUAL_AFTER_RESTORE=""
        for i in $(seq 1 20); do
            $ALICE_CLI syncwithvalidationinterfacequeue > /dev/null 2>&1 || true
            ALICE_ACTUAL_AFTER_RESTORE=$(get_dd_balance "$ALICE_CLI" "alice")
            if [ -n "$ALICE_ACTUAL_AFTER_RESTORE" ] && [ "$ALICE_ACTUAL_AFTER_RESTORE" != "null" ] && [[ "$ALICE_ACTUAL_AFTER_RESTORE" =~ ^[0-9]+$ ]]; then
                break
            fi
            sleep 1
        done
        if [ -n "$ALICE_ACTUAL_AFTER_RESTORE" ] && [[ "$ALICE_ACTUAL_AFTER_RESTORE" =~ ^[0-9]+$ ]]; then
            EXPECT_ALICE_DD=$ALICE_ACTUAL_AFTER_RESTORE
            echo "  Re-anchored EXPECT_ALICE_DD to $EXPECT_ALICE_DD after shared-key transfer"
        else
            print_status "fail" "Unable to derive stable alice DD balance after restored-wallet transfer"
            exit 1
        fi
    else
        print_status "fail" "DD transfer test failed: $SEND_RESULT"
        exit 1
    fi
else
    echo "  Skipping transfer test - insufficient DD balance"
fi

echo ""
echo "========== EXPORT-REIMPORT TEST SUMMARY =========="
echo ""
echo "Original Wallet (alice):"
echo "  DD Balance:  $ALICE_DD_BEFORE_EXPORT cents"
echo "  Positions:   $ALICE_POSITIONS_BEFORE_EXPORT"
echo ""
echo "Restored Wallet (alice_restored):"
echo "  DD Balance:  $ALICE_RESTORED_DD cents"
echo "  Positions:   $ALICE_RESTORED_POSITIONS"
echo ""
if [ "$ALICE_RESTORED_DD" = "$ALICE_DD_BEFORE_EXPORT" ] || [ "$((ALICE_RESTORED_DD + 100))" = "$ALICE_DD_BEFORE_EXPORT" ]; then
    echo -e "${GREEN}*** WALLET RESTORE TEST PASSED! ***${NC}"
else
    echo -e "${RED}*** WALLET RESTORE TEST FAILED! ***${NC}"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
echo "=================================================="
echo ""

verify_all_balances "After Alice Export-Reimport Test"

# ====================================================================================
# Step 34: BOB EXPORT-REIMPORT WALLET TEST (Descriptor-based wallet restore)
# ====================================================================================
print_header "Step 34: BOB EXPORT-REIMPORT WALLET TEST"
echo ""
echo "Testing wallet restore workflow for BOB via descriptor export/import..."
echo "This tests that wallet recovery correctly handles:"
echo "  - DD balance restoration"
echo "  - DD position restoration (including redeemed positions marked inactive)"
echo "  - DD transaction history restoration (mint, send, receive, redeem)"
echo ""

# Record Bob's original state
BOB_DD_BEFORE_EXPORT=$EXPECT_BOB_DD
BOB_DGB_BEFORE_EXPORT=$(get_dgb_balance "$BOB_CLI" "bob")
BOB_POSITIONS_BEFORE_EXPORT=$($BOB_CLI -rpcwallet=bob listdigidollarpositions false 2>/dev/null | jq 'length')

# Get detailed position info
BOB_POSITIONS_DETAIL_BEFORE=$($BOB_CLI -rpcwallet=bob listdigidollarpositions false 2>/dev/null)

# Get DD transaction history (all types)
BOB_DD_TXS_BEFORE=$($BOB_CLI -rpcwallet=bob listdigidollartxs 100 0 2>/dev/null)
BOB_MINT_COUNT_BEFORE=$(echo "$BOB_DD_TXS_BEFORE" | jq '[.[] | select(.category == "mint")] | length' 2>/dev/null || echo "0")
BOB_SEND_COUNT_BEFORE=$(echo "$BOB_DD_TXS_BEFORE" | jq '[.[] | select(.category == "send")] | length' 2>/dev/null || echo "0")
BOB_RECEIVE_COUNT_BEFORE=$(echo "$BOB_DD_TXS_BEFORE" | jq '[.[] | select(.category == "receive")] | length' 2>/dev/null || echo "0")
BOB_REDEEM_COUNT_BEFORE=$(echo "$BOB_DD_TXS_BEFORE" | jq '[.[] | select(.category == "redeem")] | length' 2>/dev/null || echo "0")
BOB_TOTAL_TXS_BEFORE=$(echo "$BOB_DD_TXS_BEFORE" | jq 'length' 2>/dev/null || echo "0")

# Count inactive (redeemed) positions
BOB_INACTIVE_POSITIONS_BEFORE=$(echo "$BOB_POSITIONS_DETAIL_BEFORE" | jq 'def dd_active: if (.is_active | type) == "boolean" then .is_active else ((.status // "") == "active" or (.status // "") == "unlocked") end; [.[] | select(dd_active | not)] | length' 2>/dev/null || echo "0")
BOB_ACTIVE_POSITIONS_BEFORE=$(echo "$BOB_POSITIONS_DETAIL_BEFORE" | jq 'def dd_active: if (.is_active | type) == "boolean" then .is_active else ((.status // "") == "active" or (.status // "") == "unlocked") end; [.[] | select(dd_active)] | length' 2>/dev/null || echo "0")

if [ "$BOB_INACTIVE_POSITIONS_BEFORE" -le 0 ] 2>/dev/null; then
    print_status "fail" "Bob restore fixture has no redeemed positions to validate"
fi

echo "========== BOB ORIGINAL STATE =========="
echo "  DD Balance:          $BOB_DD_BEFORE_EXPORT cents"
echo "  DGB Balance:         $BOB_DGB_BEFORE_EXPORT DGB"
echo "  DD Positions:        $BOB_POSITIONS_BEFORE_EXPORT total"
echo "    Active:            $BOB_ACTIVE_POSITIONS_BEFORE"
echo "    Inactive/Redeemed: $BOB_INACTIVE_POSITIONS_BEFORE"
echo ""
echo "  DD Transactions:     $BOB_TOTAL_TXS_BEFORE total"
echo "    Mint:              $BOB_MINT_COUNT_BEFORE"
echo "    Send:              $BOB_SEND_COUNT_BEFORE"
echo "    Receive:           $BOB_RECEIVE_COUNT_BEFORE"
echo "    Redeem:            $BOB_REDEEM_COUNT_BEFORE"
echo ""
echo "Position Details:"
echo "$BOB_POSITIONS_DETAIL_BEFORE" | jq -r 'def dd_active: if (.is_active | type) == "boolean" then .is_active else ((.status // "") == "active" or (.status // "") == "unlocked") end; .[] | "  \(.position_id[0:16])... | DD: \(.dd_minted) cents | DGB: \(.dgb_collateral) | tier: \(.lock_tier) | active: \(dd_active) | status: \(.status)"' 2>/dev/null
echo "==========================================="
echo ""

print_subheader "Step 34a: Exporting Bob's wallet descriptors..."

# Export descriptors with private keys
set +e
BOB_DESCRIPTORS=$($BOB_CLI -rpcwallet=bob listdescriptors true 2>&1)
BOB_EXPORT_EXIT=$?
set -e

if [ $BOB_EXPORT_EXIT -eq 0 ] && echo "$BOB_DESCRIPTORS" | jq -e '.descriptors' > /dev/null 2>&1; then
    BOB_DESC_COUNT=$(echo "$BOB_DESCRIPTORS" | jq '.descriptors | length')
    print_status "ok" "Exported $BOB_DESC_COUNT descriptors from Bob's wallet"

    # Save to file for debugging
    echo "$BOB_DESCRIPTORS" > /tmp/bob_descriptors.json
    echo "  Descriptors saved to: /tmp/bob_descriptors.json"
else
    print_status "fail" "Failed to export descriptors: $BOB_DESCRIPTORS"
fi

print_subheader "Step 34b: Creating new wallet 'bob_restored'..."

# Create new blank wallet
set +e
BOB_CREATE_RESULT=$($BOB_CLI createwallet "bob_restored" false true "" false true 2>&1)
BOB_CREATE_EXIT=$?
set -e

if [ $BOB_CREATE_EXIT -eq 0 ]; then
    print_status "ok" "Created new blank wallet 'bob_restored'"
else
    # Wallet might already exist, try to unload and recreate
    $BOB_CLI unloadwallet "bob_restored" 2>/dev/null || true
    sleep 1

    # Delete old wallet directory if exists
    rm -rf "$BOB_DATADIR/$TESTNET_SUBDIR/wallets/bob_restored" 2>/dev/null || true

    BOB_CREATE_RESULT=$($BOB_CLI createwallet "bob_restored" false true "" false true 2>&1)
    if echo "$BOB_CREATE_RESULT" | jq -e '.name' > /dev/null 2>&1; then
        print_status "ok" "Created new blank wallet 'bob_restored' (after cleanup)"
    else
        print_status "fail" "Failed to create wallet: $BOB_CREATE_RESULT"
    fi
fi

print_subheader "Step 34c: Importing descriptors into Bob's restored wallet..."

# Prepare descriptors for import
BOB_IMPORT_DESCS=$(echo "$BOB_DESCRIPTORS" | jq '[.descriptors[] | {desc: .desc, timestamp: "now", active: (.active // false), internal: (.internal // false)}]')

# Save import request for debugging
echo "$BOB_IMPORT_DESCS" > /tmp/bob_import_request.json
echo "  Import request saved to: /tmp/bob_import_request.json"

# Import descriptors
set +e
BOB_IMPORT_RESULT=$($BOB_CLI -rpcwallet=bob_restored importdescriptors "$BOB_IMPORT_DESCS" 2>&1)
BOB_IMPORT_EXIT=$?
set -e

if [ $BOB_IMPORT_EXIT -eq 0 ]; then
    BOB_SUCCESS_COUNT=$(echo "$BOB_IMPORT_RESULT" | jq '[.[] | select(.success == true)] | length')
    BOB_IMPORT_TOTAL=$(echo "$BOB_IMPORT_RESULT" | jq 'length')
    print_status "ok" "Imported $BOB_SUCCESS_COUNT/$BOB_IMPORT_TOTAL descriptors successfully"

    # Check for any failures
    BOB_FAILED=$(echo "$BOB_IMPORT_RESULT" | jq '[.[] | select(.success != true)]')
    if [ "$(echo "$BOB_FAILED" | jq 'length')" -gt 0 ]; then
        echo "  Warning: Some imports failed:"
        echo "$BOB_FAILED" | jq -r '.[] | "    - \(.error.message // "unknown error")"'
    fi
else
    print_status "fail" "Descriptor import failed: $BOB_IMPORT_RESULT"
fi

print_subheader "Step 34d: Rescanning blockchain for Bob's restored wallet..."

echo ""
echo "Testing that DigiDollar is AUTOMATICALLY restored via rescan."
echo "NO export/import needed - DD state should be reconstructed from blockchain!"
echo ""

echo "Running rescanblockchain on bob_restored..."
BOB_RESCAN_START=$(date +%s)
set +e
BOB_RESCAN_RESULT=$($BOB_CLI -rpcwallet=bob_restored rescanblockchain 2>&1)
BOB_RESCAN_EXIT=$?
set -e
BOB_RESCAN_END=$(date +%s)
BOB_RESCAN_DURATION=$((BOB_RESCAN_END - BOB_RESCAN_START))

if [ $BOB_RESCAN_EXIT -eq 0 ]; then
    BOB_START_HEIGHT=$(echo "$BOB_RESCAN_RESULT" | jq -r '.start_height // 0')
    BOB_STOP_HEIGHT=$(echo "$BOB_RESCAN_RESULT" | jq -r '.stop_height // 0')
    print_status "ok" "Rescan completed in ${BOB_RESCAN_DURATION}s (blocks $BOB_START_HEIGHT to $BOB_STOP_HEIGHT)"
else
    print_status "fail" "Rescan failed: $BOB_RESCAN_RESULT"
fi

# Wait for wallet to fully process
sleep 5

print_subheader "Step 34e: Verifying Bob's restored wallet DD state (NO export/import)..."

# Get restored wallet state
BOB_RESTORED_DD=$(get_dd_balance "$BOB_CLI" "bob_restored")
BOB_RESTORED_DGB=$(get_dgb_balance "$BOB_CLI" "bob_restored")
BOB_RESTORED_POSITIONS=$($BOB_CLI -rpcwallet=bob_restored listdigidollarpositions false 2>/dev/null | jq 'length')

# Get detailed position info from restored wallet
BOB_RESTORED_POSITIONS_DETAIL=$($BOB_CLI -rpcwallet=bob_restored listdigidollarpositions false 2>/dev/null)

# Get DD transaction history from restored wallet
BOB_RESTORED_DD_TXS=$($BOB_CLI -rpcwallet=bob_restored listdigidollartxs 100 0 2>/dev/null)
BOB_RESTORED_MINT_COUNT=$(echo "$BOB_RESTORED_DD_TXS" | jq '[.[] | select(.category == "mint")] | length' 2>/dev/null || echo "0")
BOB_RESTORED_SEND_COUNT=$(echo "$BOB_RESTORED_DD_TXS" | jq '[.[] | select(.category == "send")] | length' 2>/dev/null || echo "0")
BOB_RESTORED_RECEIVE_COUNT=$(echo "$BOB_RESTORED_DD_TXS" | jq '[.[] | select(.category == "receive")] | length' 2>/dev/null || echo "0")
BOB_RESTORED_REDEEM_COUNT=$(echo "$BOB_RESTORED_DD_TXS" | jq '[.[] | select(.category == "redeem")] | length' 2>/dev/null || echo "0")
BOB_RESTORED_TOTAL_TXS=$(echo "$BOB_RESTORED_DD_TXS" | jq 'length' 2>/dev/null || echo "0")

# Count inactive (redeemed) positions in restored wallet
BOB_RESTORED_INACTIVE_POSITIONS=$(echo "$BOB_RESTORED_POSITIONS_DETAIL" | jq 'def dd_active: if (.is_active | type) == "boolean" then .is_active else ((.status // "") == "active" or (.status // "") == "unlocked") end; [.[] | select(dd_active | not)] | length' 2>/dev/null || echo "0")
BOB_RESTORED_ACTIVE_POSITIONS=$(echo "$BOB_RESTORED_POSITIONS_DETAIL" | jq 'def dd_active: if (.is_active | type) == "boolean" then .is_active else ((.status // "") == "active" or (.status // "") == "unlocked") end; [.[] | select(dd_active)] | length' 2>/dev/null || echo "0")

echo ""
echo "========== BOB RESTORED WALLET STATE =========="
echo "  DD Balance:          $BOB_RESTORED_DD cents (expected: $BOB_DD_BEFORE_EXPORT)"
echo "  DGB Balance:         $BOB_RESTORED_DGB DGB (expected: $BOB_DGB_BEFORE_EXPORT)"
echo "  DD Positions:        $BOB_RESTORED_POSITIONS total (expected: $BOB_POSITIONS_BEFORE_EXPORT)"
echo "    Active:            $BOB_RESTORED_ACTIVE_POSITIONS (expected: $BOB_ACTIVE_POSITIONS_BEFORE)"
echo "    Inactive/Redeemed: $BOB_RESTORED_INACTIVE_POSITIONS (expected: $BOB_INACTIVE_POSITIONS_BEFORE)"
echo ""
echo "  DD Transactions:     $BOB_RESTORED_TOTAL_TXS total (expected: $BOB_TOTAL_TXS_BEFORE)"
echo "    Mint:              $BOB_RESTORED_MINT_COUNT (expected: $BOB_MINT_COUNT_BEFORE)"
echo "    Send:              $BOB_RESTORED_SEND_COUNT (expected: $BOB_SEND_COUNT_BEFORE)"
echo "    Receive:           $BOB_RESTORED_RECEIVE_COUNT (expected: $BOB_RECEIVE_COUNT_BEFORE)"
echo "    Redeem:            $BOB_RESTORED_REDEEM_COUNT (expected: $BOB_REDEEM_COUNT_BEFORE)"
echo ""
echo "Restored Position Details:"
echo "$BOB_RESTORED_POSITIONS_DETAIL" | jq -r 'def dd_active: if (.is_active | type) == "boolean" then .is_active else ((.status // "") == "active" or (.status // "") == "unlocked") end; .[] | "  \(.position_id[0:16])... | DD: \(.dd_minted) cents | DGB: \(.dgb_collateral) | tier: \(.lock_tier) | active: \(dd_active) | status: \(.status)"' 2>/dev/null || echo "  No positions found"
echo "================================================"
echo ""

# KEY VERIFICATION 1: DD balance must match
if [ "$BOB_RESTORED_DD" = "$BOB_DD_BEFORE_EXPORT" ]; then
    print_status "ok" "DD BALANCE RESTORED CORRECTLY! ($BOB_RESTORED_DD cents)"
else
    print_status "fail" "DD BALANCE MISMATCH! Original: $BOB_DD_BEFORE_EXPORT, Restored: $BOB_RESTORED_DD"
fi

# KEY VERIFICATION 2: Position count must match
if [ "$BOB_RESTORED_POSITIONS" = "$BOB_POSITIONS_BEFORE_EXPORT" ]; then
    print_status "ok" "DD POSITIONS RESTORED CORRECTLY! ($BOB_RESTORED_POSITIONS positions)"
else
    print_status "fail" "POSITIONS COUNT MISMATCH! Original: $BOB_POSITIONS_BEFORE_EXPORT, Restored: $BOB_RESTORED_POSITIONS"
fi

# KEY VERIFICATION 3: Inactive (redeemed) positions must match
if [ "$BOB_RESTORED_INACTIVE_POSITIONS" = "$BOB_INACTIVE_POSITIONS_BEFORE" ]; then
    print_status "ok" "REDEEMED POSITIONS CORRECTLY MARKED INACTIVE! ($BOB_RESTORED_INACTIVE_POSITIONS redeemed)"
else
    print_status "fail" "REDEEMED POSITIONS MISMATCH! Original inactive: $BOB_INACTIVE_POSITIONS_BEFORE, Restored inactive: $BOB_RESTORED_INACTIVE_POSITIONS"
    echo "  This is the critical bug - redeemed positions should NOT be redeemable again!"
fi

# KEY VERIFICATION 4: DD transaction history types must be present
echo ""
echo "Verifying DD transaction history categories..."

if [ "$BOB_RESTORED_MINT_COUNT" = "$BOB_MINT_COUNT_BEFORE" ]; then
    print_status "ok" "MINT transactions restored! ($BOB_RESTORED_MINT_COUNT)"
else
    print_status "fail" "MINT transaction count differs! Original: $BOB_MINT_COUNT_BEFORE, Restored: $BOB_RESTORED_MINT_COUNT"
fi

if [ "$BOB_RESTORED_SEND_COUNT" = "$BOB_SEND_COUNT_BEFORE" ]; then
    print_status "ok" "SEND transactions restored! ($BOB_RESTORED_SEND_COUNT)"
else
    print_status "fail" "SEND transaction count differs! Original: $BOB_SEND_COUNT_BEFORE, Restored: $BOB_RESTORED_SEND_COUNT"
fi

if [ "$BOB_RESTORED_RECEIVE_COUNT" = "$BOB_RECEIVE_COUNT_BEFORE" ]; then
    print_status "ok" "RECEIVE transactions restored! ($BOB_RESTORED_RECEIVE_COUNT)"
else
    print_status "fail" "RECEIVE transaction count differs! Original: $BOB_RECEIVE_COUNT_BEFORE, Restored: $BOB_RESTORED_RECEIVE_COUNT"
fi

if [ "$BOB_RESTORED_REDEEM_COUNT" = "$BOB_REDEEM_COUNT_BEFORE" ]; then
    print_status "ok" "REDEEM transactions restored! ($BOB_RESTORED_REDEEM_COUNT)"
else
    print_status "fail" "REDEEM transaction count differs! Original: $BOB_REDEEM_COUNT_BEFORE, Restored: $BOB_RESTORED_REDEEM_COUNT"
fi

# Verify DGB balance matches
BOB_DGB_DIFF=$(echo "$BOB_RESTORED_DGB - $BOB_DGB_BEFORE_EXPORT" | bc 2>/dev/null || echo "unknown")
if [ "$BOB_RESTORED_DGB" = "$BOB_DGB_BEFORE_EXPORT" ]; then
    print_status "ok" "DGB BALANCE RESTORED CORRECTLY! ($BOB_RESTORED_DGB DGB)"
else
    print_status "warn" "DGB balance differs by $BOB_DGB_DIFF DGB (may be due to fees)"
fi

print_subheader "Step 34f: Testing DD operations on Bob's restored wallet..."

# Test getting a new DD address from restored wallet
echo "Testing getdigidollaraddress on restored wallet..."
set +e
BOB_RESTORED_DD_ADDR=$($BOB_CLI -rpcwallet=bob_restored getdigidollaraddress 2>&1)
BOB_ADDR_EXIT=$?
set -e

if [ $BOB_ADDR_EXIT -eq 0 ] && [ -n "$BOB_RESTORED_DD_ADDR" ] && [ "$BOB_RESTORED_DD_ADDR" != "null" ]; then
    print_status "ok" "Can generate DD addresses from restored wallet: ${BOB_RESTORED_DD_ADDR:0:20}..."
else
    print_status "fail" "Cannot generate DD address from restored wallet: $BOB_RESTORED_DD_ADDR"
fi

# Test attempting to redeem an already-redeemed position (should fail gracefully)
if [ "$BOB_RESTORED_INACTIVE_POSITIONS" -gt 0 ]; then
    echo ""
    echo "Testing that redeemed positions cannot be redeemed again..."

    # Get an inactive position ID
    INACTIVE_POSITION_ID=$(echo "$BOB_RESTORED_POSITIONS_DETAIL" | jq -r 'def dd_active: if (.is_active | type) == "boolean" then .is_active else ((.status // "") == "active" or (.status // "") == "unlocked") end; [.[] | select(dd_active | not)][0].position_id' 2>/dev/null)

    if [ -n "$INACTIVE_POSITION_ID" ] && [ "$INACTIVE_POSITION_ID" != "null" ]; then
        echo "  Testing redemption of inactive position: ${INACTIVE_POSITION_ID:0:16}..."

        set +e
        REDEEM_RESULT=$($BOB_CLI -rpcwallet=bob_restored redeemdigidollar "$INACTIVE_POSITION_ID" 2>&1)
        REDEEM_EXIT=$?
        set -e

        # Redemption should fail for inactive position
        if [ $REDEEM_EXIT -ne 0 ] || echo "$REDEEM_RESULT" | grep -qi "error\|invalid\|cannot\|not.*active\|already.*redeemed"; then
            print_status "ok" "Correctly rejected redemption of already-redeemed position"
        else
            print_status "fail" "CRITICAL: Allowed redemption of already-redeemed position! Result: $REDEEM_RESULT"
        fi
    else
        echo "  No inactive position found to test"
    fi
fi

echo ""
echo "========== BOB EXPORT-REIMPORT TEST SUMMARY =========="
echo ""
echo "Original Wallet (bob):"
echo "  DD Balance:          $BOB_DD_BEFORE_EXPORT cents"
echo "  Positions:           $BOB_POSITIONS_BEFORE_EXPORT ($BOB_ACTIVE_POSITIONS_BEFORE active, $BOB_INACTIVE_POSITIONS_BEFORE redeemed)"
echo "  Transactions:        $BOB_TOTAL_TXS_BEFORE (mint:$BOB_MINT_COUNT_BEFORE send:$BOB_SEND_COUNT_BEFORE recv:$BOB_RECEIVE_COUNT_BEFORE redeem:$BOB_REDEEM_COUNT_BEFORE)"
echo ""
echo "Restored Wallet (bob_restored):"
echo "  DD Balance:          $BOB_RESTORED_DD cents"
echo "  Positions:           $BOB_RESTORED_POSITIONS ($BOB_RESTORED_ACTIVE_POSITIONS active, $BOB_RESTORED_INACTIVE_POSITIONS redeemed)"
echo "  Transactions:        $BOB_RESTORED_TOTAL_TXS (mint:$BOB_RESTORED_MINT_COUNT send:$BOB_RESTORED_SEND_COUNT recv:$BOB_RESTORED_RECEIVE_COUNT redeem:$BOB_RESTORED_REDEEM_COUNT)"
echo ""

# Overall test result
BOB_RESTORE_PASS=true

if [ "$BOB_RESTORED_DD" != "$BOB_DD_BEFORE_EXPORT" ]; then
    BOB_RESTORE_PASS=false
fi
if [ "$BOB_RESTORED_POSITIONS" != "$BOB_POSITIONS_BEFORE_EXPORT" ]; then
    BOB_RESTORE_PASS=false
fi
if [ "$BOB_RESTORED_INACTIVE_POSITIONS" != "$BOB_INACTIVE_POSITIONS_BEFORE" ]; then
    BOB_RESTORE_PASS=false
fi
if [ "$BOB_RESTORED_TOTAL_TXS" != "$BOB_TOTAL_TXS_BEFORE" ]; then
    BOB_RESTORE_PASS=false
fi
if [ "$BOB_RESTORED_MINT_COUNT" != "$BOB_MINT_COUNT_BEFORE" ]; then
    BOB_RESTORE_PASS=false
fi
if [ "$BOB_RESTORED_SEND_COUNT" != "$BOB_SEND_COUNT_BEFORE" ]; then
    BOB_RESTORE_PASS=false
fi
if [ "$BOB_RESTORED_RECEIVE_COUNT" != "$BOB_RECEIVE_COUNT_BEFORE" ]; then
    BOB_RESTORE_PASS=false
fi
if [ "$BOB_RESTORED_REDEEM_COUNT" != "$BOB_REDEEM_COUNT_BEFORE" ]; then
    BOB_RESTORE_PASS=false
fi

if [ "$BOB_RESTORE_PASS" = true ]; then
    echo -e "${GREEN}*** BOB WALLET RESTORE TEST PASSED! ***${NC}"
else
    echo -e "${RED}*** BOB WALLET RESTORE TEST FAILED! ***${NC}"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
echo "======================================================="
echo ""

verify_all_balances "After Bob Export-Reimport Test"

# ====================================================================================
# ALICE PERSISTENCE TEST SUMMARY
# ====================================================================================
print_header "ALICE WALLET PERSISTENCE TEST SUMMARY"
echo ""
echo "========== ALICE PERSISTENCE TEST RESULTS =========="
echo ""
echo "TEST 31 - RESCANBLOCKCHAIN:"
echo "  DD Balance:    $ALICE_DD_BEFORE_RESCAN -> $ALICE_DD_AFTER_RESCAN cents"
echo "  Positions:     $ALICE_POSITIONS_BEFORE_RESCAN -> $ALICE_POSITIONS_AFTER_RESCAN"
echo ""
echo "TEST 32 - REINDEX:"
echo "  DD Balance:    $ALICE_DD_BEFORE_REINDEX -> $ALICE_DD_AFTER_REINDEX cents"
echo "  Positions:     $ALICE_POSITIONS_BEFORE_REINDEX -> $ALICE_POSITIONS_AFTER_REINDEX"
echo "  Chain Height:  $ALICE_CHAIN_HEIGHT_BEFORE -> $ALICE_CHAIN_HEIGHT_AFTER"
echo ""
echo "TEST 33 - EXPORT-REIMPORT:"
echo "  DD Balance:    $ALICE_DD_BEFORE_EXPORT -> $ALICE_RESTORED_DD cents"
echo "  Positions:     $ALICE_POSITIONS_BEFORE_EXPORT -> $ALICE_RESTORED_POSITIONS"
echo "  DD Transfer:   Tested (100 cents from restored wallet)"
echo ""
echo "===================================================="
echo ""

verify_all_balances "FINAL STATE (After All Persistence Tests)"

# Summary
print_header "TEST RESULTS SUMMARY"
echo ""
echo "  Total Tests:  $TOTAL_TESTS"
echo "  Passed:       $PASSED_TESTS"
echo "  Failed:       $FAILED_TESTS"
echo "  Warnings:     $WARN_TESTS"
echo ""

if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}*** SOME TESTS FAILED ***${NC}"
    echo ""
    echo "Please check the balance verification output above for discrepancies."
else
    echo -e "${GREEN}*** ALL TESTS PASSED ***${NC}"
fi

echo ""
echo "TEST COVERAGE:"
echo "  [x] All collateral tiers (0-9) mint successfully"
echo "  [x] Tier 0 positions unlock after 240 blocks"
echo "  [x] Partial redemption correctly rejected"
echo "  [x] Early redemption correctly rejected"
echo "  [x] 2 successful tier 0 redemptions"
echo "  [x] DD transfers between wallets (basic)"
echo "  [x] Comprehensive transfer chain (Bob->Alice->Charlie->Bob->Charlie)"
echo "  [x] Transaction confirmation verification"
echo "  [x] DGB balance tracking through transfers"
echo "  [x] Network DD supply verification at every step"
echo "  [x] Balance verification at every step"
echo ""
echo "MULTI-ORACLE COVERAGE:"
echo "  [x] 24 active oracles started across 8 wallet nodes (7-of-35 threshold, RC44)"
echo "  [x] Oracle prices refreshed before every mint"
echo "  [x] 7-of-35 consensus verification (Step 27A)"
echo "  [x] Manual oracle injection RPC removed (Step 27B)"
if [ "${ORACLE_OUTLIER_OBSERVED:-false}" = "true" ]; then
    echo "  [x] Live exchange outlier filter observed (Step 27C)"
else
    echo "  [warn] Live exchange outlier filter observation is optional; no outlier was observed (Step 27C)"
fi
echo "  [x] Oracle continued consensus after live refresh (Step 27D)"
echo "  [x] On-chain v0x03 bundle prefix + size assertion (Step 27E)"
echo "  [x] All oracles restarted after wallet restart/reindex"
echo ""
echo "WALLET PERSISTENCE COVERAGE (BOB):"
echo "  [x] Wallet restart - DD balances persist through Qt wallet restart"
echo "  [x] Wallet backup/restore - DD balances survive backup and restore"
echo "  [x] Chain reindex - DD balances rebuild correctly with -reindex"
if [ "$ORACLE_CACHE_REBUILT" = "true" ]; then
    echo "  [x] Oracle price cache rebuilt after reindex"
else
    echo "  [warn] Oracle price cache was not active after reindex"
fi
echo "  [x] DD positions preserved through all persistence tests"
echo "  [x] Export-reimport - FULL wallet restore via descriptor export/import"
if [ "$BOB_RESTORED_INACTIVE_POSITIONS" = "$BOB_INACTIVE_POSITIONS_BEFORE" ] && [ "$BOB_INACTIVE_POSITIONS_BEFORE" -gt 0 ] 2>/dev/null; then
    echo "  [x] Redeemed positions correctly marked inactive after recovery"
else
    echo "  [warn] Redeemed-position recovery coverage was incomplete"
fi
if [ "$BOB_RESTORED_MINT_COUNT" -ge "$BOB_MINT_COUNT_BEFORE" ] 2>/dev/null \
    && [ "$BOB_RESTORED_SEND_COUNT" -ge "$BOB_SEND_COUNT_BEFORE" ] 2>/dev/null \
    && [ "$BOB_RESTORED_RECEIVE_COUNT" -ge "$BOB_RECEIVE_COUNT_BEFORE" ] 2>/dev/null \
    && [ "$BOB_RESTORED_REDEEM_COUNT" -ge "$BOB_REDEEM_COUNT_BEFORE" ] 2>/dev/null; then
    echo "  [x] DD transaction history restored (mint, send, receive, redeem)"
else
    echo "  [warn] DD transaction history restored partially; see category counts above"
fi
echo ""
echo "WALLET PERSISTENCE COVERAGE (ALICE):"
echo "  [x] Rescanblockchain - DD balances restored after wallet rescan"
echo "  [x] Chain reindex - DD balances rebuild correctly with -reindex"
echo "  [x] Export-reimport - FULL wallet restore via descriptor export/import"
echo "  [x] DD positions correctly reconstructed from blockchain"
echo "  [x] DD transfers work from restored wallet"
echo ""

print_header "DEBUG LOG LOCATIONS"
echo "  Test log:    $LOG_FILE"
echo "  Bob log:     /tmp/bob_testnet.log"
echo "  Alice log:   /tmp/alice_testnet.log"
echo "  Charlie log: /tmp/charlie_testnet.log"
echo ""
echo "BOB PERSISTENCE TEST LOGS:"
echo "  Bob restart log:   /tmp/bob_testnet_restart.log"
echo "  Bob restore log:   /tmp/bob_testnet_restore.log"
echo "  Bob reindex log:   /tmp/bob_testnet_reindex.log"
echo "  Bob descriptors:   /tmp/bob_descriptors.json"
echo "  Bob import req:    /tmp/bob_import_request.json"
echo ""
echo "ALICE PERSISTENCE TEST LOGS:"
echo "  Alice reindex log:     /tmp/alice_testnet_reindex.log"
echo "  Alice descriptors:     /tmp/alice_descriptors.json"
echo "  Alice import request:  /tmp/alice_import_request.json"
echo ""

print_header "RUNNING Qt WINDOWS"
echo "  - Bob's Qt     (PID: $BOB_PID)     — Oracles 0, 1, 16, 18"
echo "  - Alice's Qt   (PID: $ALICE_PID)   — Oracles 2, 3, 17, 19"
echo "  - Charlie's Qt (PID: $CHARLIE_PID) — Oracles 4, 5, 20"
echo "  - Dave's Qt    (PID: $DAVE_PID)    — Oracles 6, 7"
echo "  - Eve's Qt     (PID: $EVE_PID)     — Oracles 8, 9"
echo "  - Frank's Qt   (PID: $FRANK_PID)   — Oracles 10, 11"
echo "  - Grace's Qt   (PID: $GRACE_PID)   — Oracles 12, 13"
echo "  - Heidi's Qt   (PID: $HEIDI_PID)   — Oracles 14, 15"
echo ""

# ============================================================================
# RC44 MINI-TESTNET NOTE
# ============================================================================
print_header "RC44 MINI-TESTNET NOTE"
echo ""
echo "Local oracle keys are enabled only through -easypow local mini-testnet mode."
echo "Production testnet oracle keys remain the default when -easypow is absent."
echo ""

# Show current balances for manual verification
print_header "LIVE WALLET COMPARISON (bob vs bob_restored)"
echo ""
BOB_LIVE_DD=$(get_dd_balance "$BOB_CLI" "bob")
BOB_RESTORED_LIVE_DD=$(get_dd_balance "$BOB_CLI" "bob_restored")
echo "  bob DD Balance:          $BOB_LIVE_DD cents"
echo "  bob_restored DD Balance: $BOB_RESTORED_LIVE_DD cents"
echo ""
if [ "$BOB_LIVE_DD" = "$BOB_RESTORED_LIVE_DD" ]; then
    echo -e "  ${GREEN}[MATCH]${NC} Both wallets have the same DD balance!"
else
    echo -e "  ${RED}[MISMATCH]${NC} Balances differ! bob=$BOB_LIVE_DD, bob_restored=$BOB_RESTORED_LIVE_DD"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
echo ""
BOB_LIVE_POS=$($BOB_CLI -rpcwallet=bob listdigidollarpositions 2>/dev/null | jq 'length')
BOB_RESTORED_LIVE_POS=$($BOB_CLI -rpcwallet=bob_restored listdigidollarpositions 2>/dev/null | jq 'length')
echo "  bob Active Positions:          $BOB_LIVE_POS"
echo "  bob_restored Active Positions: $BOB_RESTORED_LIVE_POS"
echo ""

echo "Commands for manual testing:"
echo "  # Compare bob vs bob_restored:"
echo "  $BOB_CLI -rpcwallet=bob getdigidollarbalance"
echo "  $BOB_CLI -rpcwallet=bob_restored getdigidollarbalance"
echo ""
echo "  # List positions:"
echo "  $BOB_CLI -rpcwallet=bob listdigidollarpositions"
echo "  $BOB_CLI -rpcwallet=bob_restored listdigidollarpositions"
echo ""
echo "  # Other commands:"
echo "  $BOB_CLI getoracleprice"
echo "  $BOB_CLI getdigidollarstats"
echo ""
echo "KEEP_QT_OPEN=1 keeps Qt windows open after the run; default closes them for automation."
echo ""

if [ "${KEEP_QT_OPEN:-0}" = "1" ]; then
    trap - EXIT
    echo "KEEP_QT_OPEN=1 set; leaving all 8 Qt windows open."
    echo "Qt PIDs: $BOB_PID $ALICE_PID $CHARLIE_PID $DAVE_PID $EVE_PID $FRANK_PID $GRACE_PID $HEIDI_PID"
else
    echo "Automation mode: closing Qt windows now."
    cleanup_qt_nodes
    trap - EXIT
fi

if [ "$FAILED_TESTS" -gt 0 ]; then
    exit 1
fi
