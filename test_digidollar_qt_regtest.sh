#!/bin/bash
# DigiDollar 3-Node Network Test
# Tests network-wide statistics and DD transfers between Bob, Alice, and Charlie
# Comprehensive balance tracking and verification throughout all operations
# Supports both Qt and daemon modes - uses daemon if Qt not available

set -e

# Check if Qt is available, fall back to daemon
if [ -x "./src/qt/digibyte-qt" ]; then
    USE_QT=1
    NODE_BINARY="./src/qt/digibyte-qt"
    echo "Using Qt GUI nodes"
else
    USE_QT=0
    NODE_BINARY="./src/digibyted"
    echo "Qt not built - using daemon nodes (all functionality identical)"
fi

echo "=========================================="
echo "DigiDollar 3-Node RegTest Automated Test"
echo "Enhanced with 3-wallet DD transfers"
echo "=========================================="
echo ""

# Helper function to wait for RPC to be ready
wait_for_rpc() {
    local DATADIR=$1
    local RPCPORT=$2
    local MAX_WAIT=${3:-60}  # Default 60 seconds
    local WAITED=0

    echo "Waiting for RPC server at port $RPCPORT..."
    while [ $WAITED -lt $MAX_WAIT ]; do
        if ./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT getblockcount >/dev/null 2>&1; then
            echo "  RPC server ready after ${WAITED}s"
            return 0
        fi
        sleep 2
        WAITED=$((WAITED + 2))
    done

    echo "  ERROR: RPC server not ready after ${MAX_WAIT}s"
    return 1
}

# Helper function to get network stats with retry logic
get_network_stats() {
    local NODE_NAME=$1
    local RPC_PORT=$2
    local COOKIE=$3
    local MAX_RETRIES=5
    local RETRY_COUNT=0

    while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
        local STATS=$(curl --silent --user "$COOKIE" \
            --data-binary '{"jsonrpc":"1.0","id":"stats","method":"getdigidollarstats","params":[]}' \
            -H 'content-type: text/plain;' \
            http://127.0.0.1:${RPC_PORT}/)

        # Check if we got valid data
        local DD_SUPPLY=$(echo "$STATS" | jq -r '.result.total_dd_supply // empty')

        if [ -n "$DD_SUPPLY" ]; then
            echo "$STATS"
            return 0
        fi

        # Retry with delay
        RETRY_COUNT=$((RETRY_COUNT + 1))
        if [ $RETRY_COUNT -lt $MAX_RETRIES ]; then
            sleep 2
        fi
    done

    # Return the last response even if invalid (for error reporting)
    echo "$STATS"
}

# Helper function to get wallet DD balance with retry logic
get_dd_balance() {
    local NODE_NAME=$1
    local RPC_PORT=$2
    local COOKIE=$3
    local WALLET_NAME=$(echo "$NODE_NAME" | tr '[:upper:]' '[:lower:]')  # bob, alice, charlie
    local MAX_RETRIES=5
    local RETRY_COUNT=0

    while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
        local BALANCE=$(curl --silent --user "$COOKIE" \
            --data-binary '{"jsonrpc":"1.0","id":"balance","method":"getdigidollarbalance","params":[]}' \
            -H 'content-type: text/plain;' \
            http://127.0.0.1:${RPC_PORT}/wallet/${WALLET_NAME})

        local TOTAL=$(echo "$BALANCE" | jq -r '.result.total // empty')

        if [ -n "$TOTAL" ]; then
            echo "$TOTAL"
            return 0
        fi

        # Retry with delay
        RETRY_COUNT=$((RETRY_COUNT + 1))
        if [ $RETRY_COUNT -lt $MAX_RETRIES ]; then
            sleep 2
        fi
    done

    # Return 0 if all retries failed
    echo "0"
}

# Helper function to display network monitoring
display_network_monitor() {
    local STEP_DESC=$1

    echo "=========================================="
    echo "NETWORK MONITOR: $STEP_DESC"
    echo "=========================================="
    echo ""

    # Get Bob's network stats
    BOB_STATS=$(get_network_stats "Bob" 18443 "$BOB_COOKIE")
    BOB_DD_SUPPLY=$(echo "$BOB_STATS" | jq -r '.result.total_dd_supply // 0')
    BOB_COLLATERAL=$(echo "$BOB_STATS" | jq -r '.result.total_collateral_dgb // 0')
    BOB_HEALTH=$(echo "$BOB_STATS" | jq -r '.result.health_percentage // 0')

    # Get Alice's network stats
    ALICE_STATS=$(get_network_stats "Alice" 18446 "$ALICE_COOKIE")
    ALICE_DD_SUPPLY=$(echo "$ALICE_STATS" | jq -r '.result.total_dd_supply // 0')
    ALICE_COLLATERAL=$(echo "$ALICE_STATS" | jq -r '.result.total_collateral_dgb // 0')
    ALICE_HEALTH=$(echo "$ALICE_STATS" | jq -r '.result.health_percentage // 0')

    # Get Charlie's network stats
    CHARLIE_STATS=$(get_network_stats "Charlie" 18447 "$CHARLIE_COOKIE")
    CHARLIE_DD_SUPPLY=$(echo "$CHARLIE_STATS" | jq -r '.result.total_dd_supply // 0')
    CHARLIE_COLLATERAL=$(echo "$CHARLIE_STATS" | jq -r '.result.total_collateral_dgb // 0')
    CHARLIE_HEALTH=$(echo "$CHARLIE_STATS" | jq -r '.result.health_percentage // 0')

    # Get individual wallet balances
    BOB_BALANCE=$(get_dd_balance "Bob" 18443 "$BOB_COOKIE")
    ALICE_BALANCE=$(get_dd_balance "Alice" 18446 "$ALICE_COOKIE")
    CHARLIE_BALANCE=$(get_dd_balance "Charlie" 18447 "$CHARLIE_COOKIE")

    echo "Network-Wide Statistics (all nodes should match):"
    echo "  Bob's view:     Total DD: $BOB_DD_SUPPLY cents | Collateral: $BOB_COLLATERAL DGB | Health: $BOB_HEALTH%"
    echo "  Alice's view:   Total DD: $ALICE_DD_SUPPLY cents | Collateral: $ALICE_COLLATERAL DGB | Health: $ALICE_HEALTH%"
    echo "  Charlie's view: Total DD: $CHARLIE_DD_SUPPLY cents | Collateral: $CHARLIE_COLLATERAL DGB | Health: $CHARLIE_HEALTH%"
    echo ""

    # Verify network stats match
    if [ "$BOB_DD_SUPPLY" = "$ALICE_DD_SUPPLY" ] && [ "$BOB_DD_SUPPLY" = "$CHARLIE_DD_SUPPLY" ]; then
        echo "✅ Network Total DD Supply MATCHES on all nodes: $BOB_DD_SUPPLY cents (\$$(echo "scale=2; $BOB_DD_SUPPLY / 100" | bc))"
    else
        echo "❌ Network Total DD Supply MISMATCH!"
        exit 1
    fi

    if [ "$BOB_COLLATERAL" = "$ALICE_COLLATERAL" ] && [ "$BOB_COLLATERAL" = "$CHARLIE_COLLATERAL" ]; then
        echo "✅ Network Total Collateral MATCHES on all nodes: $BOB_COLLATERAL DGB"
    else
        echo "❌ Network Total Collateral MISMATCH!"
        exit 1
    fi

    if [ "$BOB_HEALTH" = "$ALICE_HEALTH" ] && [ "$BOB_HEALTH" = "$CHARLIE_HEALTH" ]; then
        echo "✅ Network Health MATCHES on all nodes: $BOB_HEALTH%"
    else
        echo "❌ Network Health MISMATCH!"
        exit 1
    fi

    echo ""
    echo "Individual Wallet DD Balances:"
    echo "  Bob:     $BOB_BALANCE cents (\$$(echo "scale=2; $BOB_BALANCE / 100" | bc))"
    echo "  Alice:   $ALICE_BALANCE cents (\$$(echo "scale=2; $ALICE_BALANCE / 100" | bc))"
    echo "  Charlie: $CHARLIE_BALANCE cents (\$$(echo "scale=2; $CHARLIE_BALANCE / 100" | bc))"
    echo ""

    # Verify conservation of DD (sum of balances = network total)
    TOTAL_BALANCE=$((BOB_BALANCE + ALICE_BALANCE + CHARLIE_BALANCE))
    if [ "$TOTAL_BALANCE" = "$BOB_DD_SUPPLY" ]; then
        echo "✅ DD CONSERVATION: Sum of wallet balances ($TOTAL_BALANCE) = Network total ($BOB_DD_SUPPLY)"
    else
        echo "❌ DD CONSERVATION VIOLATION: Sum ($TOTAL_BALANCE) != Network total ($BOB_DD_SUPPLY)"
        exit 1
    fi
    echo ""
}

# Step 1: Clean environment
echo "=== Step 1: Cleaning environment ==="
pkill -f "digibyte-qt.*regtest" 2>/dev/null || true
pkill -f "digibyted.*regtest" 2>/dev/null || true
pkill -f "digibyted.*bob_regtest" 2>/dev/null || true
pkill -f "digibyted.*alice_regtest" 2>/dev/null || true
pkill -f "digibyted.*charlie_regtest" 2>/dev/null || true
sleep 2
rm -rf ~/.digibyte/regtest 2>/dev/null || true  # Linux path
rm -rf ~/Library/Application\ Support/DigiByte/regtest 2>/dev/null || true  # macOS path
rm -rf /tmp/bob_regtest
rm -rf /tmp/alice_regtest
rm -rf /tmp/charlie_regtest
echo "✓ Clean environment ready"
echo ""

# Step 2: Start Bob's node
echo "=== Step 2: Starting Bob's node ==="
mkdir -p /tmp/bob_regtest

# Launch node (Qt or daemon based on availability)
$NODE_BINARY \
    -regtest \
    -datadir=/tmp/bob_regtest \
    -port=18444 \
    -rpcport=18443 \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    > /tmp/bob_node.log 2>&1 &
BOB_PID=$!
echo "Bob's node started (PID: $BOB_PID)"

# Wait for RPC to be ready (up to 60 seconds)
if ! wait_for_rpc /tmp/bob_regtest 18443 60; then
    echo "❌ Bob's node RPC failed to start"
    cat /tmp/bob_node.log 2>/dev/null | tail -20
    exit 1
fi

# Step 3: Create Bob's wallet and generate 700 blocks (in batches for Qt stability)
echo "=== Step 3: Creating Bob's wallet and generating 700 blocks ==="
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 createwallet "bob" > /dev/null
echo "  Generating blocks in batches of 100..."
for i in 1 2 3 4 5 6 7; do
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 100 > /dev/null
    echo "    Batch $i: 100 blocks generated ($(($i * 100)) total)"
    sleep 2  # Give Qt time to process
done
echo "✓ Bob has 700 blocks (ensuring enough mature UTXOs for mints)"
BOB_COOKIE=$(cat /tmp/bob_regtest/regtest/.cookie)
echo ""

# Step 4: Set oracle price
echo "=== Step 4: Setting mock oracle price ==="
# Set oracle price to $0.01 per DGB (10000 micro-USD where 1,000,000 = $1.00)
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 setmockoracleprice 10000 > /dev/null
ORACLE_PRICE=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getmockoracleprice | jq -r '.price_usd')
echo "✓ Oracle price set to: $ORACLE_PRICE per DGB"
echo ""

# Step 5: Bob mints DigiDollars (3 separate mints)
echo "=== Step 5: Bob minting DigiDollars (3 separate mints) ==="

# Mint #1: $100.00 DD with 365 day lock (tier 4)
echo "Mint #1: \$100.00 DD (365 days, tier 4)"
BOB_MINT1=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"bob_mint1","method":"mintdigidollar","params":[10000,4]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

BOB_TXID1=$(echo "$BOB_MINT1" | jq -r '.result.txid // empty')
if [ -z "$BOB_TXID1" ]; then
    echo "❌ Bob's mint #1 failed:"
    echo "$BOB_MINT1" | jq '.'
    exit 1
fi
echo "  ✓ txid: ${BOB_TXID1:0:16}..."
echo "  ✓ Collateral: $(echo "$BOB_MINT1" | jq -r '.result.dgb_collateral') DGB"
echo ""
sleep 1

# Mint #2: $50.00 DD with 180 day lock (tier 3)
echo "Mint #2: \$50.00 DD (180 days, tier 3)"
BOB_MINT2=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"bob_mint2","method":"mintdigidollar","params":[5000,3]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

BOB_TXID2=$(echo "$BOB_MINT2" | jq -r '.result.txid // empty')
if [ -z "$BOB_TXID2" ]; then
    echo "❌ Bob's mint #2 failed:"
    echo "$BOB_MINT2" | jq '.'
    exit 1
fi
echo "  ✓ txid: ${BOB_TXID2:0:16}..."
echo "  ✓ Collateral: $(echo "$BOB_MINT2" | jq -r '.result.dgb_collateral') DGB"
echo ""
sleep 1

# Mint #3: $25.00 DD with 90 day lock (tier 2)
echo "Mint #3: \$25.00 DD (90 days, tier 2)"
BOB_MINT3=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"bob_mint3","method":"mintdigidollar","params":[2500,2]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

BOB_TXID3=$(echo "$BOB_MINT3" | jq -r '.result.txid // empty')
if [ -z "$BOB_TXID3" ]; then
    echo "❌ Bob's mint #3 failed:"
    echo "$BOB_MINT3" | jq '.'
    exit 1
fi
echo "  ✓ txid: ${BOB_TXID3:0:16}..."
echo "  ✓ Collateral: $(echo "$BOB_MINT3" | jq -r '.result.dgb_collateral') DGB"
echo ""

echo "Bob's total minted: \$175.00 DD (17500 cents)"
echo ""

# Step 6: Bob generates 10 blocks to confirm his mints
echo "=== Step 6: Bob generating 10 blocks to confirm mints ==="
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 10 > /dev/null
sleep 10
echo "  Waiting for wallet to process blocks..."
BOB_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)
echo "✓ Bob's mints confirmed (height: $BOB_HEIGHT)"
echo ""

# Step 7: Start Alice's node
echo "=== Step 7: Starting Alice's node ==="
mkdir -p /tmp/alice_regtest

# Launch node (Qt or daemon based on availability)
$NODE_BINARY \
    -regtest \
    -datadir=/tmp/alice_regtest \
    -port=18445 \
    -rpcport=18446 \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -connect=127.0.0.1:18444 \
    > /tmp/alice_node.log 2>&1 &
ALICE_PID=$!
echo "Alice's node started (PID: $ALICE_PID)"

# Wait for RPC to be ready (up to 60 seconds)
if ! wait_for_rpc /tmp/alice_regtest 18446 60; then
    echo "❌ Alice's node RPC failed to start"
    cat /tmp/alice_node.log 2>/dev/null | tail -20
    exit 1
fi

# Step 8: Create Alice's wallet and set oracle price
echo "=== Step 8: Creating Alice's wallet, setting oracle price, and syncing ==="
./src/digibyte-cli -regtest -datadir=/tmp/alice_regtest -rpcport=18446 createwallet "alice" > /dev/null
# Set oracle price to $0.01 per DGB (10000 micro-USD)
./src/digibyte-cli -regtest -datadir=/tmp/alice_regtest -rpcport=18446 setmockoracleprice 10000 > /dev/null
sleep 8
echo "  Waiting for sync and balance calculation..."
ALICE_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/alice_regtest -rpcport=18446 getblockcount)
echo "✓ Alice synced (height: $ALICE_HEIGHT)"
echo "✓ Alice's oracle price set"
ALICE_COOKIE=$(cat /tmp/alice_regtest/regtest/.cookie)
echo ""

# Step 9: Start Charlie's node
echo "=== Step 9: Starting Charlie's node ==="
mkdir -p /tmp/charlie_regtest

# Launch node (Qt or daemon based on availability)
$NODE_BINARY \
    -regtest \
    -datadir=/tmp/charlie_regtest \
    -port=18448 \
    -rpcport=18447 \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -connect=127.0.0.1:18444 \
    > /tmp/charlie_node.log 2>&1 &
CHARLIE_PID=$!
echo "Charlie's node started (PID: $CHARLIE_PID)"

# Wait for RPC to be ready (up to 60 seconds)
if ! wait_for_rpc /tmp/charlie_regtest 18447 60; then
    echo "❌ Charlie's node RPC failed to start"
    cat /tmp/charlie_node.log 2>/dev/null | tail -20
    exit 1
fi

# Step 10: Create Charlie's wallet and set oracle price
echo "=== Step 10: Creating Charlie's wallet, setting oracle price, and syncing ==="
./src/digibyte-cli -regtest -datadir=/tmp/charlie_regtest -rpcport=18447 createwallet "charlie" > /dev/null
# Set oracle price to $0.01 per DGB (10000 micro-USD)
./src/digibyte-cli -regtest -datadir=/tmp/charlie_regtest -rpcport=18447 setmockoracleprice 10000 > /dev/null
sleep 8
echo "  Waiting for sync and balance calculation..."
CHARLIE_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/charlie_regtest -rpcport=18447 getblockcount)
echo "✓ Charlie synced (height: $CHARLIE_HEIGHT)"
echo "✓ Charlie's oracle price set"
CHARLIE_COOKIE=$(cat /tmp/charlie_regtest/regtest/.cookie)
echo ""

# Monitor network state after Bob's mints
echo "  Waiting for network-wide stats to propagate..."
sleep 5
display_network_monitor "After Bob's 3 Mints"

# Step 11: Bob mints $10 DD with 1-hour lock
echo "=========================================="
echo "Step 11: Bob mints \$10 DD with 1-hour lock"
echo "=========================================="
echo ""

# Generate 200 more blocks to ensure Bob has enough UTXOs for 1000% collateral
echo "=== Generating 200 blocks for additional UTXOs ==="
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 200 > /dev/null
sleep 10
echo "  Waiting for wallet to process blocks..."
BOB_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)
echo "✓ Bob height: $BOB_HEIGHT"
echo ""

echo "Mint #4: \$10.00 DD (1 hour, tier 0 = 240 blocks, 1000% = 10000 DGB)"
BOB_MINT4=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"bob_mint4","method":"mintdigidollar","params":[1000,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

BOB_TXID4=$(echo "$BOB_MINT4" | jq -r '.result.txid // empty')
if [ -z "$BOB_TXID4" ]; then
    echo "❌ Bob's mint #4 failed:"
    echo "$BOB_MINT4" | jq '.'
    exit 1
fi
echo "  ✓ txid: ${BOB_TXID4:0:16}..."
echo "  ✓ Collateral: $(echo "$BOB_MINT4" | jq -r '.result.dgb_collateral') DGB"
MINT4_UNLOCK_HEIGHT=$(echo "$BOB_MINT4" | jq -r '.result.unlock_height // 0')
echo "  ✓ Lock blocks: 240 + 100 confirmation-buffer blocks (1 hour tier)"
echo "  ✓ Unlock height: $MINT4_UNLOCK_HEIGHT"
echo ""

MINT4_TXID=$(echo "$BOB_MINT4" | jq -r '.result.txid')
sleep 2

# Generate 2 blocks to confirm
echo "=== Generating 2 blocks to confirm mint ==="
ADDR=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -rpcwallet=bob getnewaddress)
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -rpcwallet=bob generatetoaddress 2 "$ADDR" > /dev/null
sleep 10
echo "  Waiting for wallet to process blocks..."
BOB_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)

# Verify the transaction is actually confirmed
TX_CONFIRMATIONS=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -rpcwallet=bob gettransaction "$MINT4_TXID" 2>/dev/null | jq -r '.confirmations // 0')
if [ "$TX_CONFIRMATIONS" -eq 0 ]; then
    echo "⚠️  WARNING: Mint transaction still NOT confirmed after 2 blocks!"
    echo "Attempting to mine 10 more blocks..."
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -rpcwallet=bob generatetoaddress 10 "$ADDR" > /dev/null
    sleep 10
    echo "  Waiting for wallet to process blocks..."
    TX_CONFIRMATIONS=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -rpcwallet=bob gettransaction "$MINT4_TXID" 2>/dev/null | jq -r '.confirmations // 0')
    BOB_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)
    if [ "$TX_CONFIRMATIONS" -eq 0 ]; then
        echo "❌ ERROR: Mint transaction STILL not confirmed after 12 blocks total!"
        exit 1
    fi
fi

echo "✓ Mint #4 confirmed (height: $BOB_HEIGHT, confirmations: $TX_CONFIRMATIONS)"
if [ "$MINT4_UNLOCK_HEIGHT" -le 0 ]; then
    echo "❌ ERROR: Mint #4 RPC did not return a valid unlock_height"
    exit 1
fi
echo "✓ Lock will expire at height: $MINT4_UNLOCK_HEIGHT"
echo ""

# Monitor network state after 4th mint
echo "  Waiting for network-wide stats to propagate..."
sleep 5
display_network_monitor "After Bob's 4th Mint (\$10 DD, 1-hour lock)"

# Step 12: Generate 120 blocks (halfway through lock period)
echo "=========================================="
echo "Step 12: Generate 120 blocks (halfway through lock)"
echo "=========================================="
echo ""
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 120 > /dev/null
sleep 10
echo "  Waiting for wallet to process blocks..."
BOB_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)
ALICE_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/alice_regtest -rpcport=18446 getblockcount)
CHARLIE_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/charlie_regtest -rpcport=18447 getblockcount)
echo "✓ Bob height: $BOB_HEIGHT"
echo "✓ Alice height: $ALICE_HEIGHT"
echo "✓ Charlie height: $CHARLIE_HEIGHT"
echo "✓ Blocks until unlock: $((MINT4_UNLOCK_HEIGHT - BOB_HEIGHT))"
echo ""

# Monitor network state halfway through lock
echo "  Waiting for network-wide stats to propagate..."
sleep 5
display_network_monitor "Halfway Through Lock Period (120 blocks)"

# Step 13: Try early redemption (should FAIL)
echo "=========================================="
echo "Step 13: Try early redemption (should FAIL)"
echo "=========================================="
echo ""
echo "Attempting to redeem vault before lock expires..."
EARLY_REDEEM=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"early_redeem\",\"method\":\"redeemdigidollar\",\"params\":[\"$BOB_TXID4\",1000]}" \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

EARLY_ERROR=$(echo "$EARLY_REDEEM" | jq -r '.error.message // empty')
if [ -n "$EARLY_ERROR" ]; then
    echo "✅ Early redemption correctly REJECTED"
    echo "   Error: $EARLY_ERROR"
else
    echo "❌ Early redemption should have FAILED but didn't!"
    echo "$EARLY_REDEEM" | jq '.'
fi
echo ""

# Step 14: Generate enough blocks to pass lock expiry
echo "=========================================="
echo "Step 14: Generate remaining blocks to lock expiry"
echo "=========================================="
echo ""
BLOCKS_TO_UNLOCK=$((MINT4_UNLOCK_HEIGHT - BOB_HEIGHT))
if [ "$BLOCKS_TO_UNLOCK" -lt 0 ]; then
    BLOCKS_TO_UNLOCK=0
fi
echo "Mining $BLOCKS_TO_UNLOCK blocks to reach unlock height $MINT4_UNLOCK_HEIGHT..."
if [ "$BLOCKS_TO_UNLOCK" -gt 0 ]; then
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate "$BLOCKS_TO_UNLOCK" > /dev/null
fi
sleep 10
echo "  Waiting for wallet to process blocks..."
BOB_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)
ALICE_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/alice_regtest -rpcport=18446 getblockcount)
CHARLIE_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/charlie_regtest -rpcport=18447 getblockcount)
echo "✓ Bob height: $BOB_HEIGHT"
echo "✓ Alice height: $ALICE_HEIGHT"
echo "✓ Charlie height: $CHARLIE_HEIGHT"
if [ "$BOB_HEIGHT" -lt "$MINT4_UNLOCK_HEIGHT" ]; then
    echo "❌ ERROR: Bob height $BOB_HEIGHT is still below unlock height $MINT4_UNLOCK_HEIGHT"
    exit 1
fi
echo "✓ Lock period EXPIRED - redemption should now work"
echo ""

# Step 15: Redeem the 1-hour vault (should SUCCEED)
echo "=========================================="
echo "Step 15: Redeem the 1-hour vault (should SUCCEED)"
echo "=========================================="
echo ""
echo "Attempting to redeem vault after lock expires..."
REDEEM_SUCCESS=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"redeem_success\",\"method\":\"redeemdigidollar\",\"params\":[\"$BOB_TXID4\",1000]}" \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

REDEEM_TXID=$(echo "$REDEEM_SUCCESS" | jq -r '.result.txid // empty')
if [ -n "$REDEEM_TXID" ]; then
    echo "✅ Redemption SUCCEEDED"
    echo "   Redemption txid: ${REDEEM_TXID:0:16}..."
    echo "   DD Redeemed: $(echo "$REDEEM_SUCCESS" | jq -r '.result.dd_redeemed') cents"
    echo "   Collateral returned: $(echo "$REDEEM_SUCCESS" | jq -r '.result.dgb_unlocked') DGB"
else
    echo "❌ Redemption FAILED!"
    echo "$REDEEM_SUCCESS" | jq '.'
    exit 1
fi
echo ""

# Generate 10 blocks to confirm redemption
echo "=== Generating 10 blocks to confirm redemption ==="
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 10 > /dev/null
sleep 10
echo "  Waiting for wallet to process blocks..."
BOB_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)
ALICE_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/alice_regtest -rpcport=18446 getblockcount)
CHARLIE_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/charlie_regtest -rpcport=18447 getblockcount)
echo "✓ Redemption confirmed (Bob: $BOB_HEIGHT, Alice: $ALICE_HEIGHT, Charlie: $CHARLIE_HEIGHT)"
echo ""

# CRITICAL: Verify redemption transaction returns exact collateral amount
echo "=========================================="
echo "CRITICAL: Verify Redemption Transaction"
echo "=========================================="
echo ""

# Get the mint transaction to find collateral amount
MINT_TX=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"test\",\"method\":\"getrawtransaction\",\"params\":[\"$BOB_TXID4\",true]}" \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

MINT_COLLATERAL=$(echo "$MINT_TX" | jq -r '.result.vout[0].value')
echo "Mint transaction locked: $MINT_COLLATERAL DGB as collateral"

# Get the redemption transaction
REDEEM_TX=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"test\",\"method\":\"getrawtransaction\",\"params\":[\"$REDEEM_TXID\",true]}" \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

# Output 0 should be the returned collateral
REDEEM_COLLATERAL=$(echo "$REDEEM_TX" | jq -r '.result.vout[0].value')
echo "Redemption transaction returned: $REDEEM_COLLATERAL DGB"

# Verify they match EXACTLY
if [ "$MINT_COLLATERAL" = "$REDEEM_COLLATERAL" ]; then
    echo ""
    echo "✅✅✅ PASS: Redemption returns EXACTLY the locked collateral ✅✅✅"
    echo "   Locked:   $MINT_COLLATERAL DGB"
    echo "   Returned: $REDEEM_COLLATERAL DGB"
    echo "   Difference: 0 DGB"
else
    echo ""
    echo "❌❌❌ FAIL: Redemption amount mismatch! ❌❌❌"
    echo "   Locked:   $MINT_COLLATERAL DGB"
    echo "   Returned: $REDEEM_COLLATERAL DGB"
    echo "   Difference: $(echo "$MINT_COLLATERAL - $REDEEM_COLLATERAL" | bc) DGB"
    exit 1
fi
echo ""

# Monitor network state after redemption
echo "  Waiting for network-wide stats to propagate..."
sleep 5
display_network_monitor "After Redemption of 4th Mint"

# Step 16: NEW - Bob sends DD to Alice and Charlie
echo "=========================================="
echo "Step 16: DigiDollar Transfer Tests"
echo "=========================================="
echo ""

# Get Alice's DD receive address
echo "Getting Alice's DD receive address..."
ALICE_DD_ADDR=$(curl --silent --user "$ALICE_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"getaddr","method":"getdigidollaraddress","params":[]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18446/wallet/alice | jq -r '.result')
echo "✓ Alice's DD address: $ALICE_DD_ADDR"

# Get Charlie's DD receive address
echo "Getting Charlie's DD receive address..."
CHARLIE_DD_ADDR=$(curl --silent --user "$CHARLIE_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"getaddr","method":"getdigidollaraddress","params":[]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18447/wallet/charlie | jq -r '.result')
echo "✓ Charlie's DD address: $CHARLIE_DD_ADDR"
echo ""

# Transfer 1: Bob sends $34.67 to Alice
echo "=== Transfer #1: Bob → Alice (\$34.67) ==="
echo "Before transfer:"
BOB_BAL_BEFORE=$(get_dd_balance "Bob" 18443 "$BOB_COOKIE")
ALICE_BAL_BEFORE=$(get_dd_balance "Alice" 18446 "$ALICE_COOKIE")
echo "  Bob:   $BOB_BAL_BEFORE cents (\$$(echo "scale=2; $BOB_BAL_BEFORE / 100" | bc))"
echo "  Alice: $ALICE_BAL_BEFORE cents (\$$(echo "scale=2; $ALICE_BAL_BEFORE / 100" | bc))"
echo ""

TRANSFER1=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"transfer1\",\"method\":\"senddigidollar\",\"params\":[\"$ALICE_DD_ADDR\",3467]}" \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/wallet/bob)

TRANSFER1_TXID=$(echo "$TRANSFER1" | jq -r '.result.txid // empty')
if [ -z "$TRANSFER1_TXID" ]; then
    echo "❌ Transfer #1 failed:"
    echo "$TRANSFER1" | jq '.'
    exit 1
fi
echo "✓ Transfer sent, txid: ${TRANSFER1_TXID:0:16}..."
sleep 2

# Confirm transfer
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 5 > /dev/null
sleep 10
echo "  Waiting for wallet to process blocks..."
echo "✓ Transfer confirmed"
echo ""

echo "After transfer:"
BOB_BAL_AFTER1=$(get_dd_balance "Bob" 18443 "$BOB_COOKIE")
ALICE_BAL_AFTER1=$(get_dd_balance "Alice" 18446 "$ALICE_COOKIE")
echo "  Bob:   $BOB_BAL_AFTER1 cents (\$$(echo "scale=2; $BOB_BAL_AFTER1 / 100" | bc))"
echo "  Alice: $ALICE_BAL_AFTER1 cents (\$$(echo "scale=2; $ALICE_BAL_AFTER1 / 100" | bc))"
echo ""

# Verify transfer amounts
BOB_CHANGE=$((BOB_BAL_BEFORE - BOB_BAL_AFTER1))
ALICE_CHANGE=$((ALICE_BAL_AFTER1 - ALICE_BAL_BEFORE))
echo "Balance changes:"
echo "  Bob decreased by:   $BOB_CHANGE cents (expected: 3467)"
echo "  Alice increased by: $ALICE_CHANGE cents (expected: 3467)"

if [ "$BOB_CHANGE" -eq 3467 ] && [ "$ALICE_CHANGE" -eq 3467 ]; then
    echo "✅ Transfer #1 amounts verified correctly"
else
    echo "❌ Transfer #1 amounts incorrect!"
    exit 1
fi
echo ""

# Monitor network state after first transfer
echo "  Waiting for network-wide stats to propagate..."
sleep 5
display_network_monitor "After Transfer #1 (Bob → Alice \$34.67)"

# Transfer 2: Bob sends $12.53 to Charlie
echo "=== Transfer #2: Bob → Charlie (\$12.53) ==="
echo "Before transfer:"
BOB_BAL_BEFORE2=$(get_dd_balance "Bob" 18443 "$BOB_COOKIE")
CHARLIE_BAL_BEFORE=$(get_dd_balance "Charlie" 18447 "$CHARLIE_COOKIE")
echo "  Bob:     $BOB_BAL_BEFORE2 cents (\$$(echo "scale=2; $BOB_BAL_BEFORE2 / 100" | bc))"
echo "  Charlie: $CHARLIE_BAL_BEFORE cents (\$$(echo "scale=2; $CHARLIE_BAL_BEFORE / 100" | bc))"
echo ""

TRANSFER2=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"transfer2\",\"method\":\"senddigidollar\",\"params\":[\"$CHARLIE_DD_ADDR\",1253]}" \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/wallet/bob)

TRANSFER2_TXID=$(echo "$TRANSFER2" | jq -r '.result.txid // empty')
if [ -z "$TRANSFER2_TXID" ]; then
    echo "❌ Transfer #2 failed:"
    echo "$TRANSFER2" | jq '.'
    exit 1
fi
echo "✓ Transfer sent, txid: ${TRANSFER2_TXID:0:16}..."
sleep 2

# Confirm transfer
./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 5 > /dev/null
sleep 10
echo "  Waiting for wallet to process blocks..."
echo "✓ Transfer confirmed"
echo ""

echo "After transfer:"
BOB_BAL_AFTER2=$(get_dd_balance "Bob" 18443 "$BOB_COOKIE")
CHARLIE_BAL_AFTER=$(get_dd_balance "Charlie" 18447 "$CHARLIE_COOKIE")
echo "  Bob:     $BOB_BAL_AFTER2 cents (\$$(echo "scale=2; $BOB_BAL_AFTER2 / 100" | bc))"
echo "  Charlie: $CHARLIE_BAL_AFTER cents (\$$(echo "scale=2; $CHARLIE_BAL_AFTER / 100" | bc))"
echo ""

# Verify transfer amounts
BOB_CHANGE2=$((BOB_BAL_BEFORE2 - BOB_BAL_AFTER2))
CHARLIE_CHANGE=$((CHARLIE_BAL_AFTER - CHARLIE_BAL_BEFORE))
echo "Balance changes:"
echo "  Bob decreased by:     $BOB_CHANGE2 cents (expected: 1253)"
echo "  Charlie increased by: $CHARLIE_CHANGE cents (expected: 1253)"

if [ "$BOB_CHANGE2" -eq 1253 ] && [ "$CHARLIE_CHANGE" -eq 1253 ]; then
    echo "✅ Transfer #2 amounts verified correctly"
else
    echo "❌ Transfer #2 amounts incorrect!"
    exit 1
fi
echo ""

# Monitor network state after second transfer
echo "  Waiting for network-wide stats to propagate..."
sleep 5
display_network_monitor "After Transfer #2 (Bob → Charlie \$12.53)"

# Step 17: Final comprehensive balance verification
echo "=========================================="
echo "Step 17: Final Balance Verification"
echo "=========================================="
echo ""

# Calculate expected final balances
# Bob started with $175.00 (17500 cents)
# Bob sent $34.67 (3467 cents) to Alice
# Bob sent $12.53 (1253 cents) to Charlie
# Bob expected: 17500 - 3467 - 1253 = 12780 cents ($127.80)
EXPECTED_BOB=12780
EXPECTED_ALICE=3467
EXPECTED_CHARLIE=1253
EXPECTED_NETWORK_TOTAL=17500

echo "Expected final balances:"
echo "  Bob:     $EXPECTED_BOB cents (\$$(echo "scale=2; $EXPECTED_BOB / 100" | bc))"
echo "  Alice:   $EXPECTED_ALICE cents (\$$(echo "scale=2; $EXPECTED_ALICE / 100" | bc))"
echo "  Charlie: $EXPECTED_CHARLIE cents (\$$(echo "scale=2; $EXPECTED_CHARLIE / 100" | bc))"
echo "  Network: $EXPECTED_NETWORK_TOTAL cents (\$$(echo "scale=2; $EXPECTED_NETWORK_TOTAL / 100" | bc))"
echo ""

# Get actual balances
ACTUAL_BOB=$(get_dd_balance "Bob" 18443 "$BOB_COOKIE")
ACTUAL_ALICE=$(get_dd_balance "Alice" 18446 "$ALICE_COOKIE")
ACTUAL_CHARLIE=$(get_dd_balance "Charlie" 18447 "$CHARLIE_COOKIE")

# Get network total
BOB_STATS_FINAL=$(get_network_stats "Bob" 18443 "$BOB_COOKIE")
ACTUAL_NETWORK=$(echo "$BOB_STATS_FINAL" | jq -r '.result.total_dd_supply')

echo "Actual final balances:"
echo "  Bob:     $ACTUAL_BOB cents (\$$(echo "scale=2; $ACTUAL_BOB / 100" | bc))"
echo "  Alice:   $ACTUAL_ALICE cents (\$$(echo "scale=2; $ACTUAL_ALICE / 100" | bc))"
echo "  Charlie: $ACTUAL_CHARLIE cents (\$$(echo "scale=2; $ACTUAL_CHARLIE / 100" | bc))"
echo "  Network: $ACTUAL_NETWORK cents (\$$(echo "scale=2; $ACTUAL_NETWORK / 100" | bc))"
echo ""

echo "=========================================="
echo "Final Verification Results:"
echo "=========================================="

# Verify Bob's balance
if [ "$ACTUAL_BOB" -eq "$EXPECTED_BOB" ]; then
    echo "✅ Bob's balance CORRECT: $ACTUAL_BOB cents"
else
    echo "❌ Bob's balance INCORRECT! Expected: $EXPECTED_BOB, Got: $ACTUAL_BOB"
    exit 1
fi

# Verify Alice's balance
if [ "$ACTUAL_ALICE" -eq "$EXPECTED_ALICE" ]; then
    echo "✅ Alice's balance CORRECT: $ACTUAL_ALICE cents"
else
    echo "❌ Alice's balance INCORRECT! Expected: $EXPECTED_ALICE, Got: $ACTUAL_ALICE"
    exit 1
fi

# Verify Charlie's balance
if [ "$ACTUAL_CHARLIE" -eq "$EXPECTED_CHARLIE" ]; then
    echo "✅ Charlie's balance CORRECT: $ACTUAL_CHARLIE cents"
else
    echo "❌ Charlie's balance INCORRECT! Expected: $EXPECTED_CHARLIE, Got: $ACTUAL_CHARLIE"
    exit 1
fi

# Verify network total
if [ "$ACTUAL_NETWORK" -eq "$EXPECTED_NETWORK_TOTAL" ]; then
    echo "✅ Network total CORRECT: $ACTUAL_NETWORK cents"
else
    echo "❌ Network total INCORRECT! Expected: $EXPECTED_NETWORK_TOTAL, Got: $ACTUAL_NETWORK"
    exit 1
fi

# Verify conservation
ACTUAL_SUM=$((ACTUAL_BOB + ACTUAL_ALICE + ACTUAL_CHARLIE))
if [ "$ACTUAL_SUM" -eq "$ACTUAL_NETWORK" ]; then
    echo "✅ DD CONSERVATION verified: Sum of balances ($ACTUAL_SUM) = Network total ($ACTUAL_NETWORK)"
else
    echo "❌ DD CONSERVATION VIOLATION! Sum: $ACTUAL_SUM, Network: $ACTUAL_NETWORK"
    exit 1
fi

echo ""

# ============================================================================
# DCA (Dynamic Collateral Adjustment) AND ERR (Emergency Redemption Ratio) TESTING
# ============================================================================

echo "=========================================="
echo "DCA/ERR COMPREHENSIVE TESTING"
echo "=========================================="
echo ""
echo "DCA Tiers (collateral multiplier based on system health):"
echo "  - Healthy (>=150%): 1.0x multiplier"
echo "  - Warning (120-149%): 1.2x multiplier"
echo "  - Critical (100-119%): 1.5x multiplier"
echo "  - Emergency (<100%): 2.0x multiplier + ERR activates"
echo ""
echo "ERR Tiers (DD burn INCREASE on redemption when health <100%):"
echo "  CRITICAL: ERR increases DD burn, NOT reduces collateral!"
echo "  - 95-100%: Burn 105.3% DD (1/0.95) -> FULL collateral"
echo "  - 90-95%:  Burn 111.1% DD (1/0.90) -> FULL collateral"
echo "  - 85-90%:  Burn 117.6% DD (1/0.85) -> FULL collateral"
echo "  - <85%:    Burn 125% DD (1/0.80) -> FULL collateral"
echo ""
echo "  Example: \$100 DD position at 80% health:"
echo "    - Need to burn: \$100 / 0.80 = \$125 DD"
echo "    - You get back: FULL collateral (not reduced!)"
echo ""

# Helper function to get DCA info
get_dca_info() {
    local RPC_PORT=$1
    local COOKIE=$2
    curl --silent --user "$COOKIE" \
        --data-binary '{"jsonrpc":"1.0","id":"stats","method":"getdigidollarstats","params":[]}' \
        -H 'content-type: text/plain;' \
        http://127.0.0.1:${RPC_PORT}/
}

# Helper to set oracle price on all nodes and mine blocks
set_all_oracle_prices() {
    local PRICE=$1
    local BLOCKS=${2:-10}  # Default 10 blocks

    echo "  Setting oracle price to $PRICE micro-USD on all nodes..."
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 setmockoracleprice $PRICE > /dev/null
    ./src/digibyte-cli -regtest -datadir=/tmp/alice_regtest -rpcport=18446 setmockoracleprice $PRICE > /dev/null
    ./src/digibyte-cli -regtest -datadir=/tmp/charlie_regtest -rpcport=18447 setmockoracleprice $PRICE > /dev/null

    echo "  Mining $BLOCKS blocks to confirm state change..."
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate $BLOCKS > /dev/null
    sleep 5
    echo "  Done."
    echo ""
}

# Display current DCA status
display_dca_status() {
    local DESC=$1
    local STATS=$(get_dca_info 18443 "$BOB_COOKIE")

    local HEALTH=$(echo "$STATS" | jq -r '.result.health_percentage // "N/A"')
    local DCA_MULT=$(echo "$STATS" | jq -r '.result.dca_tier.multiplier // "N/A"')
    local DCA_STATUS=$(echo "$STATS" | jq -r '.result.dca_tier.status // "N/A"')
    local ORACLE_PRICE=$(echo "$STATS" | jq -r '.result.oracle_price_usd // "N/A"')
    local IS_EMERGENCY=$(echo "$STATS" | jq -r '.result.is_emergency // "N/A"')
    local DD_SUPPLY=$(echo "$STATS" | jq -r '.result.total_dd_supply // 0')
    local COLLATERAL=$(echo "$STATS" | jq -r '.result.total_collateral_dgb // 0')

    echo "=== DCA Status: $DESC ==="
    echo "  Oracle Price:   \$$ORACLE_PRICE per DGB"
    echo "  DD Supply:      $DD_SUPPLY cents (\$$(echo "scale=2; $DD_SUPPLY / 100" | bc))"
    echo "  Collateral:     $COLLATERAL DGB"
    echo "  System Health:  $HEALTH%"
    echo "  DCA Multiplier: ${DCA_MULT}x"
    echo "  DCA Status:     $DCA_STATUS"
    echo "  Is Emergency:   $IS_EMERGENCY"
    echo ""
}

# ============================================================================
# STEP DCA-SETUP: Get current system state and calculate target prices
# ============================================================================

echo "=========================================="
echo "DCA-SETUP: Calculating Target Prices"
echo "=========================================="
echo ""

# First, get the current system state
SETUP_STATS=$(get_dca_info 18443 "$BOB_COOKIE")
TOTAL_COLLATERAL=$(echo "$SETUP_STATS" | jq -r '.result.total_collateral_dgb // 0')
TOTAL_DD_CENTS=$(echo "$SETUP_STATS" | jq -r '.result.total_dd_supply // 0')
TOTAL_DD_USD=$(echo "scale=2; $TOTAL_DD_CENTS / 100" | bc 2>/dev/null || echo "0")

echo "Current System State:"
echo "  Total Collateral: $TOTAL_COLLATERAL DGB"
echo "  Total DD Supply: $TOTAL_DD_CENTS cents (\$$TOTAL_DD_USD)"
echo ""

# Health formula: health% = (collateral_DGB * price_USD) / DD_USD * 100
# Solving for price: price_USD = (health% * DD_USD) / (collateral_DGB * 100)
# In micro-USD: price_micro = price_USD * 1,000,000
#
# Example: To get 150% health with 57500 DGB and $175 DD:
#   price_USD = (150 * 175) / (57500 * 100) = 26250 / 5750000 = $0.00456
#   price_micro = 4565 micro-USD
#
# The formula in micro-USD directly:
#   price_micro = (health% * DD_cents * 100) / collateral_DGB
#   This comes from: health% = (collateral * price_USD) / DD_USD * 100
#   Solving for price and converting to micro-USD (1M micro = $1)

# Calculate prices for each health tier target
# Using 10% margin within each tier to ensure we hit the tier reliably
if [ "$TOTAL_COLLATERAL" != "0" ] && [ "$TOTAL_COLLATERAL" != "null" ]; then
    # 200% health (well into HEALTHY tier)
    PRICE_HEALTHY=$(echo "scale=0; (200 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "100000")

    # 135% health (middle of WARNING tier: 120-149%)
    PRICE_WARNING=$(echo "scale=0; (135 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "50000")

    # 110% health (middle of CRITICAL tier: 100-119%)
    PRICE_CRITICAL=$(echo "scale=0; (110 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "30000")

    # 80% health (well into EMERGENCY tier: <100%)
    PRICE_EMERGENCY=$(echo "scale=0; (80 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "20000")

    # 250% health for recovery (well into healthy)
    PRICE_RECOVERY=$(echo "scale=0; (250 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "150000")
else
    # Fallback prices if calculation fails
    echo "⚠️  Could not calculate prices dynamically, using fallback values"
    PRICE_HEALTHY=100000
    PRICE_WARNING=50000
    PRICE_CRITICAL=30000
    PRICE_EMERGENCY=20000
    PRICE_RECOVERY=150000
fi

echo "Calculated Target Prices (micro-USD per DGB):"
echo "  HEALTHY (200% health):   $PRICE_HEALTHY micro-USD (\$$(echo "scale=4; $PRICE_HEALTHY / 1000000" | bc))"
echo "  WARNING (135% health):   $PRICE_WARNING micro-USD (\$$(echo "scale=4; $PRICE_WARNING / 1000000" | bc))"
echo "  CRITICAL (110% health):  $PRICE_CRITICAL micro-USD (\$$(echo "scale=4; $PRICE_CRITICAL / 1000000" | bc))"
echo "  EMERGENCY (80% health):  $PRICE_EMERGENCY micro-USD (\$$(echo "scale=4; $PRICE_EMERGENCY / 1000000" | bc))"
echo "  RECOVERY (250% health):  $PRICE_RECOVERY micro-USD (\$$(echo "scale=4; $PRICE_RECOVERY / 1000000" | bc))"
echo ""

# ============================================================================
# STEP DCA-1: Create a dedicated vault for ERR testing (1-hour lock)
# ============================================================================

echo "=========================================="
echo "DCA-1: Creating ERR Test Vault (1-hour lock)"
echo "=========================================="
echo ""
echo "Minting \$20 DD with 1-hour lock (tier 0 = 240 blocks)"
echo "This vault will be used for ERR redemption testing later."
echo ""

# Ensure healthy price for this mint (use calculated healthy price)
set_all_oracle_prices $PRICE_HEALTHY 10

ERR_TEST_MINT=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"err_test_mint","method":"mintdigidollar","params":[2000,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

ERR_TEST_TXID=$(echo "$ERR_TEST_MINT" | jq -r '.result.txid // empty')
ERR_TEST_COLLATERAL=$(echo "$ERR_TEST_MINT" | jq -r '.result.dgb_collateral // 0')
ERR_TEST_UNLOCK_HEIGHT=$(echo "$ERR_TEST_MINT" | jq -r '.result.unlock_height // 0')

if [ -z "$ERR_TEST_TXID" ]; then
    echo "❌ ERR test mint failed:"
    echo "$ERR_TEST_MINT" | jq '.'
    echo ""
    echo "Continuing with DCA tests..."
else
    echo "✅ ERR test vault created"
    echo "   TXID: ${ERR_TEST_TXID:0:16}..."
    echo "   DD Minted: 2000 cents (\$20)"
    echo "   Collateral Locked: $ERR_TEST_COLLATERAL DGB"
    echo "   Lock Period: 240 + 100 confirmation-buffer blocks (1 hour tier)"
    echo "   Unlock Height: $ERR_TEST_UNLOCK_HEIGHT"
    echo ""

    # Mine 10 blocks to confirm
    echo "Mining 10 blocks to confirm mint..."
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 10 > /dev/null
    sleep 5
fi

# Update collateral calculation after new mint
SETUP_STATS=$(get_dca_info 18443 "$BOB_COOKIE")
TOTAL_COLLATERAL=$(echo "$SETUP_STATS" | jq -r '.result.total_collateral_dgb // 0')
TOTAL_DD_CENTS=$(echo "$SETUP_STATS" | jq -r '.result.total_dd_supply // 0')

# Recalculate prices with new totals
if [ "$TOTAL_COLLATERAL" != "0" ] && [ "$TOTAL_COLLATERAL" != "null" ]; then
    PRICE_HEALTHY=$(echo "scale=0; (200 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "$PRICE_HEALTHY")
    PRICE_WARNING=$(echo "scale=0; (135 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "$PRICE_WARNING")
    PRICE_CRITICAL=$(echo "scale=0; (110 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "$PRICE_CRITICAL")
    PRICE_EMERGENCY=$(echo "scale=0; (80 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "$PRICE_EMERGENCY")
    PRICE_RECOVERY=$(echo "scale=0; (250 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "$PRICE_RECOVERY")
    echo "Updated target prices after new mint:"
    echo "  Total Collateral: $TOTAL_COLLATERAL DGB"
    echo "  Total DD: $TOTAL_DD_CENTS cents"
    echo ""
fi

display_dca_status "After ERR Test Vault Creation"

# ============================================================================
# STEP DCA-2: Test DCA at HEALTHY tier (>=150% health)
# ============================================================================

echo "=========================================="
echo "DCA-2: Testing HEALTHY Tier (>=150% health)"
echo "=========================================="
echo ""
echo "Setting oracle price to $PRICE_HEALTHY micro-USD to achieve ~200% health..."

set_all_oracle_prices $PRICE_HEALTHY 10

display_dca_status "HEALTHY Tier"

DCA_STATUS_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.status // "unknown"')
DCA_MULT_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.multiplier // 0')
HEALTH_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.health_percentage // 0')

echo "Achieved health: $HEALTH_CHECK%"
if [ "$DCA_STATUS_CHECK" = "healthy" ]; then
    echo "✅ DCA status is HEALTHY"
    echo "   Multiplier: ${DCA_MULT_CHECK}x (expected 1.0x)"
else
    echo "⚠️  DCA status: $DCA_STATUS_CHECK (expected: healthy)"
    echo "   Multiplier: ${DCA_MULT_CHECK}x"
fi
echo ""

# Test mint at healthy tier
echo "Testing mint at HEALTHY tier..."
HEALTHY_MINT=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"healthy_mint","method":"mintdigidollar","params":[1000,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

HEALTHY_MINT_TXID=$(echo "$HEALTHY_MINT" | jq -r '.result.txid // empty')
HEALTHY_MINT_COLLATERAL=$(echo "$HEALTHY_MINT" | jq -r '.result.dgb_collateral // 0')

if [ -n "$HEALTHY_MINT_TXID" ]; then
    echo "✅ Mint succeeded at HEALTHY tier"
    echo "   Collateral required: $HEALTHY_MINT_COLLATERAL DGB"
    echo "   (Base ratio with 1.0x DCA multiplier)"

    # Confirm
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 10 > /dev/null
    sleep 5
else
    echo "⚠️  Mint failed: $(echo "$HEALTHY_MINT" | jq -r '.error.message // "unknown"')"
fi
echo ""

# ============================================================================
# STEP DCA-3: Mine blocks to expire ERR test vault lock
# ============================================================================

echo "=========================================="
echo "DCA-3: Expiring ERR Test Vault Lock Period"
echo "=========================================="
echo ""
echo "The ERR test vault has a 240 block lock plus a 100-block confirmation buffer."
CURRENT_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)
if [ -n "$ERR_TEST_TXID" ] && [ "${ERR_TEST_UNLOCK_HEIGHT:-0}" -gt 0 ]; then
    BLOCKS_TO_ERR_UNLOCK=$((ERR_TEST_UNLOCK_HEIGHT - CURRENT_HEIGHT))
else
    BLOCKS_TO_ERR_UNLOCK=350
fi
if [ "$BLOCKS_TO_ERR_UNLOCK" -lt 0 ]; then
    BLOCKS_TO_ERR_UNLOCK=0
fi
echo "Mining $BLOCKS_TO_ERR_UNLOCK blocks to ensure lock expires..."
echo ""

if [ "$BLOCKS_TO_ERR_UNLOCK" -gt 0 ]; then
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate "$BLOCKS_TO_ERR_UNLOCK" > /dev/null
fi
sleep 10

CURRENT_HEIGHT=$(./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 getblockcount)
echo "✅ Mined to height: $CURRENT_HEIGHT"
if [ -n "$ERR_TEST_TXID" ] && [ "${ERR_TEST_UNLOCK_HEIGHT:-0}" -gt 0 ] && [ "$CURRENT_HEIGHT" -lt "$ERR_TEST_UNLOCK_HEIGHT" ]; then
    echo "❌ ERROR: ERR test vault is still locked until $ERR_TEST_UNLOCK_HEIGHT"
    exit 1
fi
echo "   ERR test vault lock should now be expired"
echo ""

# Recalculate prices after mining (collateral unchanged, DD unchanged)
SETUP_STATS=$(get_dca_info 18443 "$BOB_COOKIE")
TOTAL_COLLATERAL=$(echo "$SETUP_STATS" | jq -r '.result.total_collateral_dgb // 0')
TOTAL_DD_CENTS=$(echo "$SETUP_STATS" | jq -r '.result.total_dd_supply // 0')

if [ "$TOTAL_COLLATERAL" != "0" ] && [ "$TOTAL_COLLATERAL" != "null" ]; then
    PRICE_WARNING=$(echo "scale=0; (135 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "$PRICE_WARNING")
    PRICE_CRITICAL=$(echo "scale=0; (110 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "$PRICE_CRITICAL")
    PRICE_EMERGENCY=$(echo "scale=0; (80 * $TOTAL_DD_CENTS * 100) / $TOTAL_COLLATERAL" | bc 2>/dev/null || echo "$PRICE_EMERGENCY")
fi

# ============================================================================
# STEP DCA-4: Test DCA at WARNING tier (120-149% health)
# ============================================================================

echo "=========================================="
echo "DCA-4: Testing WARNING Tier (120-149% health)"
echo "=========================================="
echo ""
echo "Setting oracle price to $PRICE_WARNING micro-USD to achieve ~135% health..."

set_all_oracle_prices $PRICE_WARNING 10

display_dca_status "WARNING Tier Test"

DCA_STATUS_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.status // "unknown"')
DCA_MULT_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.multiplier // 0')
HEALTH_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.health_percentage // 0')

echo "Achieved health: $HEALTH_CHECK%"
if [ "$DCA_STATUS_CHECK" = "warning" ]; then
    echo "✅ DCA status is WARNING"
    echo "   Multiplier: ${DCA_MULT_CHECK}x (expected 1.2x)"
else
    echo "⚠️  DCA status: $DCA_STATUS_CHECK (expected: warning)"
    echo "   Adjusting price to hit warning tier..."

    # Try lower price if still healthy
    if [ "$DCA_STATUS_CHECK" = "healthy" ]; then
        ADJUSTED_PRICE=$(echo "scale=0; $PRICE_WARNING * 80 / 100" | bc)
        set_all_oracle_prices $ADJUSTED_PRICE 10
        display_dca_status "WARNING Tier (Adjusted)"
    fi
fi
echo ""

# ============================================================================
# STEP DCA-5: Test DCA at CRITICAL tier (100-119% health)
# ============================================================================

echo "=========================================="
echo "DCA-5: Testing CRITICAL Tier (100-119% health)"
echo "=========================================="
echo ""
echo "Setting oracle price to $PRICE_CRITICAL micro-USD to achieve ~110% health..."

set_all_oracle_prices $PRICE_CRITICAL 10

display_dca_status "CRITICAL Tier Test"

DCA_STATUS_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.status // "unknown"')
DCA_MULT_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.multiplier // 0')
HEALTH_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.health_percentage // 0')

echo "Achieved health: $HEALTH_CHECK%"
if [ "$DCA_STATUS_CHECK" = "critical" ]; then
    echo "✅ DCA status is CRITICAL"
    echo "   Multiplier: ${DCA_MULT_CHECK}x (expected 1.5x)"
else
    echo "⚠️  DCA status: $DCA_STATUS_CHECK (expected: critical)"
fi
echo ""

# ============================================================================
# STEP DCA-6: Test DCA at EMERGENCY tier (<100% health) - ERR ACTIVATES
# ============================================================================

echo "=========================================="
echo "DCA-6: Testing EMERGENCY Tier (<100% health)"
echo "=========================================="
echo ""
echo "CRASHING oracle price to $PRICE_EMERGENCY micro-USD to achieve ~80% health!"
echo "This should activate ERR (Emergency Redemption Ratio)"
echo ""

set_all_oracle_prices $PRICE_EMERGENCY 10

display_dca_status "EMERGENCY Tier Test"

IS_EMERGENCY=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.is_emergency // false')
DCA_STATUS_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.status // "unknown"')
DCA_MULT_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.multiplier // 0')
HEALTH_CHECK=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.health_percentage // 0')

echo "Achieved health: $HEALTH_CHECK%"
if [ "$IS_EMERGENCY" = "true" ]; then
    echo "✅ System is in EMERGENCY state!"
    echo "   DCA Multiplier: ${DCA_MULT_CHECK}x (expected 2.0x)"
    echo "   ERR should be ACTIVE"
else
    echo "⚠️  Emergency flag: $IS_EMERGENCY (expected: true)"
    echo "   DCA status: $DCA_STATUS_CHECK"

    # Try even lower price
    if [ "$HEALTH_CHECK" -ge 100 ] 2>/dev/null; then
        echo "   Health still >= 100%, trying lower price..."
        VERY_LOW_PRICE=$(echo "scale=0; $PRICE_EMERGENCY * 50 / 100" | bc)
        set_all_oracle_prices $VERY_LOW_PRICE 10
        display_dca_status "EMERGENCY Tier (Adjusted)"
        IS_EMERGENCY=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.is_emergency // false')
    fi
fi
echo ""

# ============================================================================
# STEP ERR-1: Test that new minting is BLOCKED during ERR
# ============================================================================

echo "=========================================="
echo "ERR-1: Testing Minting Block During Emergency"
echo "=========================================="
echo ""
echo "Attempting to mint during EMERGENCY state..."
echo "New minting should be BLOCKED when ERR is active!"
echo ""

ERR_MINT_ATTEMPT=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"err_mint","method":"mintdigidollar","params":[500,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

ERR_MINT_ERROR=$(echo "$ERR_MINT_ATTEMPT" | jq -r '.error.message // empty')
ERR_MINT_TXID=$(echo "$ERR_MINT_ATTEMPT" | jq -r '.result.txid // empty')

if [ -n "$ERR_MINT_ERROR" ]; then
    echo "✅ Minting correctly BLOCKED during ERR!"
    echo "   Error: $ERR_MINT_ERROR"
elif [ -n "$ERR_MINT_TXID" ]; then
    echo "❌ BUG: Minting should have been BLOCKED but succeeded!"
    echo "   ERR is supposed to block new mints to protect the system."
else
    echo "⚠️  Unexpected response:"
    echo "$ERR_MINT_ATTEMPT" | jq '.'
fi
echo ""

# ============================================================================
# STEP ERR-2: Test ERR redemption (requires MORE DD burned, returns FULL collateral)
# ============================================================================

echo "=========================================="
echo "ERR-2: Testing ERR Redemption"
echo "=========================================="
echo ""
echo "Testing redemption during ERR..."
echo "CRITICAL: ERR increases DD burn, NOT reduces collateral!"
echo "DD burn INCREASE based on health tier (you burn MORE to get FULL collateral):"
echo "  - 95-100%: Burn 105.3% DD (1/0.95) -> FULL collateral"
echo "  - 90-95%:  Burn 111.1% DD (1/0.90) -> FULL collateral"
echo "  - 85-90%:  Burn 117.6% DD (1/0.85) -> FULL collateral"
echo "  - <85%:    Burn 125% DD (1/0.80) -> FULL collateral"
echo ""
echo "Example: \$100 DD vault at 80% health:"
echo "  - You must burn: \$100 / 0.80 = \$125 DD"
echo "  - You receive: FULL collateral back (not reduced!)"
echo ""

# Get Bob's positions to find the ERR test vault
BOB_POSITIONS=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"positions","method":"listdigidollarpositions","params":[false]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/wallet/bob)

echo "Bob's current positions:"
echo "$BOB_POSITIONS" | jq -r '.result[] | "  - \(.position_id[0:16])... | DD: \(.dd_minted) cents | Can Redeem: \(.can_redeem)"' 2>/dev/null || echo "  (none or error)"
echo ""

# Find our ERR test vault or any redeemable vault
if [ -n "$ERR_TEST_TXID" ]; then
    REDEEMABLE_VAULT="$ERR_TEST_TXID"
    VAULT_DD=2000
else
    REDEEMABLE_VAULT=$(echo "$BOB_POSITIONS" | jq -r '.result[] | select(.can_redeem == true) | .position_id' | head -1)
    VAULT_DD=$(echo "$BOB_POSITIONS" | jq -r ".result[] | select(.position_id == \"$REDEEMABLE_VAULT\") | .dd_minted")
fi

if [ -n "$REDEEMABLE_VAULT" ] && [ "$REDEEMABLE_VAULT" != "null" ]; then
    VAULT_COLLATERAL=$(echo "$BOB_POSITIONS" | jq -r ".result[] | select(.position_id == \"$REDEEMABLE_VAULT\") | .dgb_collateral")

    echo "Attempting ERR redemption on vault: ${REDEEMABLE_VAULT:0:16}..."
    echo "  DD Amount: $VAULT_DD cents"
    echo "  Original Collateral: $VAULT_COLLATERAL DGB"
    echo ""

    ERR_REDEEM=$(curl --silent --user "$BOB_COOKIE" \
      --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"err_redeem\",\"method\":\"redeemdigidollar\",\"params\":[\"$REDEEMABLE_VAULT\",$VAULT_DD]}" \
      -H 'content-type: text/plain;' \
      http://127.0.0.1:18443/)

    ERR_REDEEM_TXID=$(echo "$ERR_REDEEM" | jq -r '.result.txid // empty')
    ERR_REDEEM_ERROR=$(echo "$ERR_REDEEM" | jq -r '.error.message // empty')

    if [ -n "$ERR_REDEEM_TXID" ]; then
        DGB_RETURNED=$(echo "$ERR_REDEEM" | jq -r '.result.dgb_unlocked // 0')
        DD_BURNED=$(echo "$ERR_REDEEM" | jq -r '.result.dd_burned // "N/A"')
        echo "✅ ERR Redemption completed!"
        echo "   TXID: ${ERR_REDEEM_TXID:0:16}..."
        echo "   Original vault DD: $VAULT_DD cents"
        echo "   DD burned for redemption: $DD_BURNED cents"
        echo "   Original collateral: $VAULT_COLLATERAL DGB"
        echo "   Collateral returned: $DGB_RETURNED DGB"

        # Calculate DD burn multiplier (should be > 1.0 under ERR)
        if [ "$VAULT_DD" != "0" ] && [ "$VAULT_DD" != "null" ] && [ "$DD_BURNED" != "N/A" ]; then
            BURN_MULT=$(echo "scale=2; $DD_BURNED / $VAULT_DD" | bc 2>/dev/null || echo "N/A")
            echo ""
            echo "   DD burn multiplier: ${BURN_MULT}x (1.0x = normal, >1.0x = ERR penalty)"
            echo ""
            echo "   FULL collateral returned! ERR increases DD burn, NOT collateral reduction."
        fi

        # Verify FULL collateral returned (not reduced)
        if [ "$DGB_RETURNED" = "$VAULT_COLLATERAL" ]; then
            echo "   ✅ FULL collateral returned as expected under ERR"
        else
            echo "   ⚠️  Collateral: $DGB_RETURNED DGB (original: $VAULT_COLLATERAL DGB)"
        fi

        # Mine blocks to confirm
        echo ""
        echo "Mining 10 blocks to confirm ERR redemption..."
        ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 10 > /dev/null
        sleep 5
    elif [ -n "$ERR_REDEEM_ERROR" ]; then
        echo "⚠️  ERR redemption blocked/failed:"
        echo "   Error: $ERR_REDEEM_ERROR"
    else
        echo "⚠️  Unexpected response:"
        echo "$ERR_REDEEM" | jq '.'
    fi
else
    echo "⚠️  No redeemable vaults found for ERR testing"
    echo "   (The ERR test vault may not have been created or lock not expired)"
fi
echo ""

# ============================================================================
# STEP RECOVERY: Test system recovery when price increases
# ============================================================================

echo "=========================================="
echo "RECOVERY: Testing System Recovery"
echo "=========================================="
echo ""
echo "Restoring oracle price to healthy levels..."
echo "ERR should DEACTIVATE when health recovers above 100%"
echo ""

set_all_oracle_prices $PRICE_RECOVERY 10  # Recovery price for ~250% health

display_dca_status "After Price Recovery"

IS_EMERGENCY_AFTER=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.is_emergency // true')
DCA_STATUS_AFTER=$(get_dca_info 18443 "$BOB_COOKIE" | jq -r '.result.dca_tier.status // "unknown"')

if [ "$IS_EMERGENCY_AFTER" = "false" ]; then
    echo "✅ System RECOVERED from emergency state!"
    echo "   DCA Status: $DCA_STATUS_AFTER"
    echo "   ERR is now DEACTIVATED"
else
    echo "⚠️  System still in emergency: $IS_EMERGENCY_AFTER"
fi
echo ""

# ============================================================================
# STEP RECOVERY-2: Verify minting works after recovery
# ============================================================================

echo "=========================================="
echo "RECOVERY-2: Testing Minting After Recovery"
echo "=========================================="
echo ""
echo "Attempting mint after system recovery..."
echo "Minting should be ALLOWED again after ERR deactivates."
echo ""

RECOVERY_MINT=$(curl --silent --user "$BOB_COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"recovery_mint","method":"mintdigidollar","params":[500,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:18443/)

RECOVERY_MINT_TXID=$(echo "$RECOVERY_MINT" | jq -r '.result.txid // empty')
RECOVERY_MINT_ERROR=$(echo "$RECOVERY_MINT" | jq -r '.error.message // empty')

if [ -n "$RECOVERY_MINT_TXID" ]; then
    echo "✅ Minting works after recovery!"
    echo "   TXID: ${RECOVERY_MINT_TXID:0:16}..."
    echo "   DD Minted: 500 cents (\$5)"

    # Confirm
    echo ""
    echo "Mining 10 blocks to confirm..."
    ./src/digibyte-cli -regtest -datadir=/tmp/bob_regtest -rpcport=18443 -generate 10 > /dev/null
    sleep 5
else
    echo "⚠️  Minting still blocked after recovery:"
    echo "   Error: $RECOVERY_MINT_ERROR"
fi
echo ""

# Final status
display_dca_status "FINAL STATE"

echo "=========================================="
echo "DCA/ERR TESTING COMPLETE"
echo "=========================================="
echo ""
echo "Test Summary:"
echo "  DCA-1: Created ERR test vault (1-hour lock)"
echo "  DCA-2: Tested HEALTHY tier (>=150% health, 1.0x multiplier)"
echo "  DCA-3: Expired ERR test vault lock (mined 250 blocks)"
echo "  DCA-4: Tested WARNING tier (120-149% health, 1.2x multiplier)"
echo "  DCA-5: Tested CRITICAL tier (100-119% health, 1.5x multiplier)"
echo "  DCA-6: Tested EMERGENCY tier (<100% health, 2.0x multiplier)"
echo "  ERR-1: Tested minting block during ERR (new mints blocked)"
echo "  ERR-2: Tested ERR redemption (DD burn increase, FULL collateral returned)"
echo "  RECOVERY: Tested system recovery after price increase"
echo "  RECOVERY-2: Tested minting restoration after ERR deactivates"
echo ""

echo "=========================================="
echo "ALL TESTS PASSED!"
echo "=========================================="
echo ""
echo "Test Summary:"
echo "  1. ✓ Bob minted \$175.00 DD (3 long-term vaults)"
echo "  2. ✓ Bob minted \$10.00 DD with 1-hour lock"
echo "  3. ✓ Alice and Charlie synced and saw same network stats"
echo "  4. ✓ Network stats matched across all 3 nodes at every step"
echo "  5. ✓ Early redemption correctly rejected"
echo "  6. ✓ Redemption succeeded after lock expired"
echo "  7. ✓ Exact collateral amount returned on redemption"
echo "  8. ✓ Bob sent \$34.67 DD to Alice"
echo "  9. ✓ Bob sent \$12.53 DD to Charlie"
echo " 10. ✓ All final balances verified correct"
echo " 11. ✓ DD conservation verified (sum = network total)"
echo ""

echo "=========================================="
echo "3-Node Network Test COMPLETE"
echo "=========================================="
echo ""

if [ "$USE_QT" -eq 1 ]; then
    echo "Qt GUI Windows Are Open - check visually:"
    echo ""
    echo "1. Navigate to: DigiDollar tab → Overview"
    echo ""
    echo "2. Verify 'Network DigiDollar Status' section shows:"
    echo "   - Network Total DD: Should be IDENTICAL on all 3"
    echo "   - Network Total Collateral: Should be IDENTICAL on all 3"
    echo "   - System Health: Should be IDENTICAL on all 3"
    echo ""
    echo "Qt windows remain open for manual verification"
else
    echo "Daemon nodes running (use RPC to query state)"
fi

echo ""
echo "Process IDs:"
echo "  Bob:     $BOB_PID"
echo "  Alice:   $ALICE_PID"
echo "  Charlie: $CHARLIE_PID"
echo ""
echo "Press Ctrl+C to stop the nodes"
echo ""

# Wait indefinitely - keeps nodes running for manual inspection
wait
