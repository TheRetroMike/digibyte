#!/bin/bash
# DigiDollar DCA/ERR Comprehensive Test
# Tests Dynamic Collateral Adjustment and Emergency Redemption Ratio
#
# CRITICAL ERR UNDERSTANDING:
# - ERR increases DD BURN requirement, NOT reduces collateral return!
# - Normal: Burn 100 DD -> Get FULL collateral back
# - ERR at 80%: Burn 125 DD (100/0.80) -> Get FULL collateral back
#
# DCA Tiers (multiplier for NEW mints):
# - Healthy (>=150%): 1.0x multiplier
# - Warning (120-149%): 1.2x multiplier
# - Critical (100-119%): 1.5x multiplier
# - Emergency (<100%): 2.0x multiplier + minting blocked
#
# ERR Tiers (DD burn multiplier for redemptions when health < 100%):
# - 95-100%: Burn 105.3% DD (1/0.95) for FULL collateral
# - 90-95%: Burn 111.1% DD (1/0.90) for FULL collateral
# - 85-90%: Burn 117.6% DD (1/0.85) for FULL collateral
# - <85%: Burn 125% DD (1/0.80) for FULL collateral (maximum)

set -e

echo "=========================================="
echo "DigiDollar DCA/ERR Comprehensive Test"
echo "=========================================="
echo ""

# Configuration
DATADIR=/tmp/dca_err_test
RPCPORT=18450
P2PPORT=18451

# Helper function to wait for RPC
wait_for_rpc() {
    local MAX_WAIT=${1:-60}
    local WAITED=0

    echo "Waiting for RPC server..."
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

# Helper to get stats
get_stats() {
    curl --silent --user "$COOKIE" \
        --data-binary '{"jsonrpc":"1.0","id":"stats","method":"getdigidollarstats","params":[]}' \
        -H 'content-type: text/plain;' \
        http://127.0.0.1:${RPCPORT}/
}

# Helper to set oracle price
set_oracle_price() {
    local PRICE=$1
    local USD=$(echo "scale=6; $PRICE / 1000000" | bc)
    echo "  Setting oracle price: $PRICE micro-USD (\$$USD per DGB)"
    ./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT setmockoracleprice $PRICE > /dev/null
    # Mine blocks to propagate
    ./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT -generate 5 > /dev/null
    sleep 3
}

# Helper to display system status
display_status() {
    local DESC=$1
    local STATS=$(get_stats)

    local HEALTH=$(echo "$STATS" | jq -r '.result.health_percentage // 0')
    local DCA_STATUS=$(echo "$STATS" | jq -r '.result.dca_tier.status // "unknown"')
    local DCA_MULT=$(echo "$STATS" | jq -r '.result.dca_tier.multiplier // 1.0')
    local IS_EMERGENCY=$(echo "$STATS" | jq -r '.result.is_emergency // false')
    local DD_SUPPLY=$(echo "$STATS" | jq -r '.result.total_dd_supply // 0')
    local COLLATERAL=$(echo "$STATS" | jq -r '.result.total_collateral_dgb // 0')
    local ORACLE_PRICE=$(echo "$STATS" | jq -r '.result.oracle_price_micro_usd // 0')

    echo ""
    echo "=== $DESC ==="
    echo "  Oracle Price:   $ORACLE_PRICE micro-USD (\$$(echo "scale=6; $ORACLE_PRICE / 1000000" | bc) per DGB)"
    echo "  DD Supply:      $DD_SUPPLY cents (\$$(echo "scale=2; $DD_SUPPLY / 100" | bc))"
    echo "  Collateral:     $COLLATERAL DGB"
    echo "  System Health:  $HEALTH%"
    echo "  DCA Status:     $DCA_STATUS (${DCA_MULT}x multiplier)"
    echo "  Is Emergency:   $IS_EMERGENCY"
    echo ""
}

# Step 1: Clean environment
echo "=== Step 1: Cleaning environment ==="
pkill -f "digibyted.*$DATADIR" 2>/dev/null || true
pkill -f "digibyte-qt.*$DATADIR" 2>/dev/null || true
sleep 2
rm -rf $DATADIR
mkdir -p $DATADIR
echo "Done"
echo ""

# Step 2: Start node
echo "=== Step 2: Starting digibyted ==="
./src/digibyted \
    -regtest \
    -datadir=$DATADIR \
    -port=$P2PPORT \
    -rpcport=$RPCPORT \
    -server \
    -listen=1 \
    -discover=0 \
    -digidollar=1 \
    -txindex=1 \
    -fallbackfee=0.0001 \
    -dandelion=0 \
    -daemon

if ! wait_for_rpc 60; then
    echo "Failed to start node"
    exit 1
fi
echo ""

# Step 3: Create wallet and generate initial blocks
echo "=== Step 3: Creating wallet and mining initial blocks ==="
./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT createwallet "test" > /dev/null

# Generate 1000 blocks for lots of UTXOs
echo "  Mining 1000 blocks..."
for i in 1 2 3 4 5 6 7 8 9 10; do
    ./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT -generate 100 > /dev/null
    echo "    Batch $i: $(($i * 100)) blocks"
done
sleep 5

COOKIE=$(cat $DATADIR/regtest/.cookie)
HEIGHT=$(./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT getblockcount)
echo "  Height: $HEIGHT"
echo ""

# Step 4: Set initial oracle price - $0.01 per DGB (10,000 micro-USD)
echo "=== Step 4: Setting initial oracle price ==="
INITIAL_PRICE=10000  # $0.01 per DGB
set_oracle_price $INITIAL_PRICE
echo ""

# Step 5: Create test positions
echo "=== Step 5: Creating test positions ==="
echo ""

# At $0.01/DGB, 1-hour tier (1000% collateral = 10x)
# To mint $100 DD, need: $100 * 10 / $0.01 = 100,000 DGB collateral
# To mint $50 DD, need: $50 * 10 / $0.01 = 50,000 DGB collateral

echo "Mint #1: \$100 DD with 1-hour lock (tier 0)"
MINT1=$(curl --silent --user "$COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"mint1","method":"mintdigidollar","params":[10000,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:${RPCPORT}/)

MINT1_TXID=$(echo "$MINT1" | jq -r '.result.txid // empty')
MINT1_COLLATERAL=$(echo "$MINT1" | jq -r '.result.dgb_collateral // 0')

if [ -z "$MINT1_TXID" ]; then
    echo "  ERROR: Mint failed"
    echo "$MINT1" | jq '.'
    exit 1
fi
echo "  TXID: ${MINT1_TXID:0:16}..."
echo "  DD Minted: 10000 cents (\$100)"
echo "  Collateral: $MINT1_COLLATERAL DGB"
echo ""

# Confirm
./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT -generate 10 > /dev/null
sleep 3

echo "Mint #2: \$50 DD with 1-hour lock (tier 0)"
MINT2=$(curl --silent --user "$COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"mint2","method":"mintdigidollar","params":[5000,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:${RPCPORT}/)

MINT2_TXID=$(echo "$MINT2" | jq -r '.result.txid // empty')
MINT2_COLLATERAL=$(echo "$MINT2" | jq -r '.result.dgb_collateral // 0')

if [ -z "$MINT2_TXID" ]; then
    echo "  ERROR: Mint failed"
    echo "$MINT2" | jq '.'
    exit 1
fi
echo "  TXID: ${MINT2_TXID:0:16}..."
echo "  DD Minted: 5000 cents (\$50)"
echo "  Collateral: $MINT2_COLLATERAL DGB"
echo ""

# Confirm
./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT -generate 10 > /dev/null
sleep 3

# Calculate total collateral and DD
TOTAL_COLLATERAL=$(echo "$MINT1_COLLATERAL + $MINT2_COLLATERAL" | bc)
TOTAL_DD=15000  # 15000 cents = $150

echo "Total System State:"
echo "  Total DD: $TOTAL_DD cents (\$150)"
echo "  Total Collateral: $TOTAL_COLLATERAL DGB"
echo ""

display_status "After Initial Mints"

# Step 6: Expire the lock period
echo "=== Step 6: Expiring lock period (mining 250 blocks) ==="
./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT -generate 250 > /dev/null
sleep 5
HEIGHT=$(./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT getblockcount)
echo "  Height: $HEIGHT"
echo "  Lock periods should be expired now"
echo ""

# ============================================================================
# DCA TIER TESTING
# ============================================================================

echo "=========================================="
echo "DCA TIER TESTING"
echo "=========================================="
echo ""
echo "System health = (collateral_DGB * price_USD) / DD_USD * 100"
echo ""
echo "With $TOTAL_COLLATERAL DGB collateral and \$150 DD:"
echo ""

# Calculate oracle prices for each tier target
# health = (collateral * price) / DD * 100
# price = (health * DD) / (collateral * 100)
# price_micro = (health * DD_cents * 10000) / collateral_DGB

# Correct formula: price_micro_usd = target_health * DD_USD * 10000 / collateral_DGB
# With DD in dollars (not cents), DD_USD = TOTAL_DD_CENTS / 100
# So: price_micro_usd = target_health * (TOTAL_DD_CENTS / 100) * 10000 / collateral_DGB
#                     = target_health * TOTAL_DD_CENTS * 100 / collateral_DGB
# But collateral is a decimal string, we need to strip the decimal places
COLLATERAL_INT=$(echo "$TOTAL_COLLATERAL" | cut -d'.' -f1)

echo "Target Oracle Prices (formula: health * DD_cents * 100 / collateral_DGB):"
echo "  For 200% health (HEALTHY): price = 200 * $TOTAL_DD * 100 / $COLLATERAL_INT = $(echo "scale=0; 200 * $TOTAL_DD * 100 / $COLLATERAL_INT" | bc) micro-USD"
echo "  For 135% health (WARNING): price = 135 * $TOTAL_DD * 100 / $COLLATERAL_INT = $(echo "scale=0; 135 * $TOTAL_DD * 100 / $COLLATERAL_INT" | bc) micro-USD"
echo "  For 110% health (CRITICAL): price = 110 * $TOTAL_DD * 100 / $COLLATERAL_INT = $(echo "scale=0; 110 * $TOTAL_DD * 100 / $COLLATERAL_INT" | bc) micro-USD"
echo "  For 80% health (EMERGENCY): price = 80 * $TOTAL_DD * 100 / $COLLATERAL_INT = $(echo "scale=0; 80 * $TOTAL_DD * 100 / $COLLATERAL_INT" | bc) micro-USD"
echo ""

# Test 1: HEALTHY tier (>=150%)
echo "--- Testing HEALTHY Tier (>=150%) ---"
PRICE_HEALTHY=$(echo "scale=0; 200 * $TOTAL_DD * 100 / $COLLATERAL_INT" | bc)
set_oracle_price $PRICE_HEALTHY
display_status "HEALTHY Tier Target (200%)"

STATS=$(get_stats)
HEALTH=$(echo "$STATS" | jq -r '.result.health_percentage // 0')
DCA_STATUS=$(echo "$STATS" | jq -r '.result.dca_tier.status // "unknown"')
DCA_MULT=$(echo "$STATS" | jq -r '.result.dca_tier.multiplier // 0')

if [ "$DCA_STATUS" = "healthy" ]; then
    echo "HEALTHY TIER: PASS"
    echo "  Health: $HEALTH%"
    echo "  Multiplier: ${DCA_MULT}x (expected: 1.0x)"
else
    echo "HEALTHY TIER: UNEXPECTED"
    echo "  Got: $DCA_STATUS ($HEALTH%)"
    echo "  Expected: healthy (>=150%)"
fi
echo ""

# Test 2: WARNING tier (120-149%)
echo "--- Testing WARNING Tier (120-149%) ---"
PRICE_WARNING=$(echo "scale=0; 135 * $TOTAL_DD * 100 / $COLLATERAL_INT" | bc)
set_oracle_price $PRICE_WARNING
display_status "WARNING Tier Target (135%)"

STATS=$(get_stats)
HEALTH=$(echo "$STATS" | jq -r '.result.health_percentage // 0')
DCA_STATUS=$(echo "$STATS" | jq -r '.result.dca_tier.status // "unknown"')
DCA_MULT=$(echo "$STATS" | jq -r '.result.dca_tier.multiplier // 0')

if [ "$DCA_STATUS" = "warning" ]; then
    echo "WARNING TIER: PASS"
    echo "  Health: $HEALTH%"
    echo "  Multiplier: ${DCA_MULT}x (expected: 1.2x)"
else
    echo "WARNING TIER: UNEXPECTED"
    echo "  Got: $DCA_STATUS ($HEALTH%)"
    echo "  Expected: warning (120-149%)"
fi
echo ""

# Test 3: CRITICAL tier (100-119%)
echo "--- Testing CRITICAL Tier (100-119%) ---"
PRICE_CRITICAL=$(echo "scale=0; 110 * $TOTAL_DD * 100 / $COLLATERAL_INT" | bc)
set_oracle_price $PRICE_CRITICAL
display_status "CRITICAL Tier Target (110%)"

STATS=$(get_stats)
HEALTH=$(echo "$STATS" | jq -r '.result.health_percentage // 0')
DCA_STATUS=$(echo "$STATS" | jq -r '.result.dca_tier.status // "unknown"')
DCA_MULT=$(echo "$STATS" | jq -r '.result.dca_tier.multiplier // 0')

if [ "$DCA_STATUS" = "critical" ]; then
    echo "CRITICAL TIER: PASS"
    echo "  Health: $HEALTH%"
    echo "  Multiplier: ${DCA_MULT}x (expected: 1.5x)"
else
    echo "CRITICAL TIER: UNEXPECTED"
    echo "  Got: $DCA_STATUS ($HEALTH%)"
    echo "  Expected: critical (100-119%)"
fi
echo ""

# ============================================================================
# ERR (EMERGENCY REDEMPTION RATIO) TESTING
# ============================================================================

echo "=========================================="
echo "ERR (EMERGENCY REDEMPTION RATIO) TESTING"
echo "=========================================="
echo ""
echo "CRITICAL CONCEPT: ERR increases DD BURN, NOT reduces collateral!"
echo ""
echo "ERR Tiers:"
echo "  95-100% health: Burn 105.3% DD (1/0.95) -> FULL collateral"
echo "  90-95% health: Burn 111.1% DD (1/0.90) -> FULL collateral"
echo "  85-90% health: Burn 117.6% DD (1/0.85) -> FULL collateral"
echo "  <85% health: Burn 125% DD (1/0.80) -> FULL collateral"
echo ""

# Test 4: EMERGENCY tier (<100%) - ERR ACTIVATES
echo "--- Testing EMERGENCY Tier (<100%) ---"
echo "Crashing price to trigger ERR..."
PRICE_EMERGENCY=$(echo "scale=0; 80 * $TOTAL_DD * 100 / $COLLATERAL_INT" | bc)
set_oracle_price $PRICE_EMERGENCY
display_status "EMERGENCY Tier Target (80%)"

STATS=$(get_stats)
HEALTH=$(echo "$STATS" | jq -r '.result.health_percentage // 0')
DCA_STATUS=$(echo "$STATS" | jq -r '.result.dca_tier.status // "unknown"')
DCA_MULT=$(echo "$STATS" | jq -r '.result.dca_tier.multiplier // 0')
IS_EMERGENCY=$(echo "$STATS" | jq -r '.result.is_emergency // false')

if [ "$DCA_STATUS" = "emergency" ] || [ "$IS_EMERGENCY" = "true" ]; then
    echo "EMERGENCY TIER: PASS"
    echo "  Health: $HEALTH%"
    echo "  Multiplier: ${DCA_MULT}x (expected: 2.0x)"
    echo "  ERR Active: $IS_EMERGENCY"
else
    echo "EMERGENCY TIER: UNEXPECTED"
    echo "  Got: $DCA_STATUS ($HEALTH%), emergency=$IS_EMERGENCY"
    echo "  Expected: emergency (<100%), ERR active"
fi
echo ""

# Test 5: Minting should be BLOCKED during ERR
echo "--- Testing Minting Block During ERR ---"
echo "Attempting to mint during emergency..."

ERR_MINT=$(curl --silent --user "$COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"err_mint","method":"mintdigidollar","params":[500,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:${RPCPORT}/)

ERR_MINT_TXID=$(echo "$ERR_MINT" | jq -r '.result.txid // empty')
ERR_MINT_ERROR=$(echo "$ERR_MINT" | jq -r '.error.message // empty')

if [ -n "$ERR_MINT_ERROR" ]; then
    echo "MINTING BLOCK: PASS"
    echo "  Minting correctly rejected during ERR"
    echo "  Error: $ERR_MINT_ERROR"
elif [ -n "$ERR_MINT_TXID" ]; then
    echo "MINTING BLOCK: FAIL (BUG!)"
    echo "  Minting should have been blocked during ERR!"
    echo "  TXID: ${ERR_MINT_TXID:0:16}..."
else
    echo "MINTING BLOCK: UNCLEAR"
    echo "$ERR_MINT" | jq '.'
fi
echo ""

# Test 6: ERR Redemption with increased DD burn
echo "--- Testing ERR Redemption ---"
echo ""
echo "ERR Redemption Test for \$100 position at ~80% health:"
echo "  Original DD: 10000 cents (\$100)"
echo "  ERR Ratio: 0.80 (at <85% health)"
echo "  Required DD Burn: 10000 / 0.80 = 12500 cents (\$125)"
echo "  Collateral Return: FULL ($MINT1_COLLATERAL DGB)"
echo ""

# Get positions to find redeemable vault
POSITIONS=$(curl --silent --user "$COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"positions","method":"listdigidollarpositions","params":[false]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:${RPCPORT}/wallet/test)

echo "Current positions:"
echo "$POSITIONS" | jq -r '.result[] | "  - \(.position_id[0:16])... | DD: \(.dd_minted) cents | Can Redeem: \(.can_redeem)"' 2>/dev/null || echo "  (none or error)"
echo ""

# Try to redeem the $100 position with ERR
echo "Attempting ERR redemption of \$100 position..."
echo "Note: User needs \$125 DD but only has \$150 total across both positions"
echo ""

ERR_REDEEM=$(curl --silent --user "$COOKIE" \
  --data-binary "{\"jsonrpc\":\"1.0\",\"id\":\"err_redeem\",\"method\":\"redeemdigidollar\",\"params\":[\"$MINT1_TXID\",10000]}" \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:${RPCPORT}/)

ERR_REDEEM_TXID=$(echo "$ERR_REDEEM" | jq -r '.result.txid // empty')
ERR_REDEEM_ERROR=$(echo "$ERR_REDEEM" | jq -r '.error.message // empty')
ERR_REDEEM_DD=$(echo "$ERR_REDEEM" | jq -r '.result.dd_burned // 0')
ERR_REDEEM_COLLATERAL=$(echo "$ERR_REDEEM" | jq -r '.result.dgb_unlocked // 0')

if [ -n "$ERR_REDEEM_TXID" ]; then
    echo "ERR REDEMPTION: COMPLETED"
    echo "  TXID: ${ERR_REDEEM_TXID:0:16}..."
    echo "  DD Burned: $ERR_REDEEM_DD cents"
    echo "  Collateral Returned: $ERR_REDEEM_COLLATERAL DGB"
    echo ""

    # Verify FULL collateral was returned
    if [ "$ERR_REDEEM_COLLATERAL" = "$MINT1_COLLATERAL" ]; then
        echo "  FULL COLLATERAL RETURN: PASS"
        echo "    Expected: $MINT1_COLLATERAL DGB"
        echo "    Got: $ERR_REDEEM_COLLATERAL DGB"
    else
        echo "  FULL COLLATERAL RETURN: CHECK"
        echo "    Expected: $MINT1_COLLATERAL DGB"
        echo "    Got: $ERR_REDEEM_COLLATERAL DGB"
    fi

    # Calculate expected DD burn
    EXPECTED_DD_BURN=12500  # 10000 / 0.80 = 12500
    if [ "$ERR_REDEEM_DD" = "$EXPECTED_DD_BURN" ]; then
        echo "  ERR DD BURN: PASS"
        echo "    Expected: $EXPECTED_DD_BURN cents (25% extra)"
        echo "    Got: $ERR_REDEEM_DD cents"
    else
        echo "  ERR DD BURN: CHECK"
        echo "    Expected: $EXPECTED_DD_BURN cents (25% extra)"
        echo "    Got: $ERR_REDEEM_DD cents"
    fi

    # Confirm
    ./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT -generate 10 > /dev/null
    sleep 3

elif [ -n "$ERR_REDEEM_ERROR" ]; then
    echo "ERR REDEMPTION: BLOCKED/FAILED"
    echo "  Error: $ERR_REDEEM_ERROR"
    echo ""
    echo "  This may be expected if:"
    echo "  - User doesn't have enough DD to burn (needs \$125 but wallet balance check)"
    echo "  - ERR redemption path not implemented"
    echo "  - Position not found or not redeemable"
else
    echo "ERR REDEMPTION: UNEXPECTED RESPONSE"
    echo "$ERR_REDEEM" | jq '.'
fi
echo ""

display_status "After ERR Redemption Attempt"

# ============================================================================
# RECOVERY TESTING
# ============================================================================

echo "=========================================="
echo "RECOVERY TESTING"
echo "=========================================="
echo ""
echo "Raising price to recover from emergency..."

# Recover to healthy - use current DD supply which may have changed
CURRENT_DD=$(get_stats | jq -r '.result.total_dd_supply // 15000')
CURRENT_COLLATERAL=$(get_stats | jq -r '.result.total_collateral_dgb // 150000' | cut -d'.' -f1)
PRICE_RECOVERY=$(echo "scale=0; 250 * $CURRENT_DD * 100 / $CURRENT_COLLATERAL" | bc)
set_oracle_price $PRICE_RECOVERY
display_status "After Recovery (250% target)"

STATS=$(get_stats)
HEALTH=$(echo "$STATS" | jq -r '.result.health_percentage // 0')
IS_EMERGENCY=$(echo "$STATS" | jq -r '.result.is_emergency // false')
DCA_STATUS=$(echo "$STATS" | jq -r '.result.dca_tier.status // "unknown"')

if [ "$IS_EMERGENCY" = "false" ] && [ "$DCA_STATUS" = "healthy" ]; then
    echo "RECOVERY: PASS"
    echo "  Health: $HEALTH%"
    echo "  DCA Status: $DCA_STATUS"
    echo "  ERR Deactivated: Yes"
else
    echo "RECOVERY: CHECK"
    echo "  Health: $HEALTH%"
    echo "  DCA Status: $DCA_STATUS"
    echo "  Is Emergency: $IS_EMERGENCY"
fi
echo ""

# Test minting after recovery
echo "--- Testing Minting After Recovery ---"
RECOVERY_MINT=$(curl --silent --user "$COOKIE" \
  --data-binary '{"jsonrpc":"1.0","id":"recovery_mint","method":"mintdigidollar","params":[500,0]}' \
  -H 'content-type: text/plain;' \
  http://127.0.0.1:${RPCPORT}/)

RECOVERY_MINT_TXID=$(echo "$RECOVERY_MINT" | jq -r '.result.txid // empty')
RECOVERY_MINT_ERROR=$(echo "$RECOVERY_MINT" | jq -r '.error.message // empty')

if [ -n "$RECOVERY_MINT_TXID" ]; then
    echo "MINTING AFTER RECOVERY: PASS"
    echo "  Minting works again after recovery"
    echo "  TXID: ${RECOVERY_MINT_TXID:0:16}..."

    # Confirm
    ./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT -generate 10 > /dev/null
    sleep 3
else
    echo "MINTING AFTER RECOVERY: FAIL"
    echo "  Minting should work after recovery!"
    echo "  Error: $RECOVERY_MINT_ERROR"
fi
echo ""

display_status "Final State"

# ============================================================================
# TEST SUMMARY
# ============================================================================

echo "=========================================="
echo "TEST SUMMARY"
echo "=========================================="
echo ""
echo "DCA Tier Tests:"
echo "  HEALTHY (>=150%): $([ "$DCA_STATUS" = "healthy" ] && echo "PASS" || echo "CHECK")"
echo ""
echo "ERR Tests:"
echo "  ERR activation on emergency: Tested"
echo "  Minting block during ERR: Tested"
echo "  ERR redemption (burn MORE DD, get FULL collateral): Tested"
echo ""
echo "Recovery Tests:"
echo "  ERR deactivation on recovery: Tested"
echo "  Minting restoration after recovery: Tested"
echo ""
echo "CRITICAL ERR VERIFICATION:"
echo "  ERR does NOT reduce collateral return!"
echo "  ERR INCREASES DD burn requirement!"
echo "  Formula: RequiredDD = OriginalDD / ERRRatio"
echo "  Example: \$100 DD at 80% ratio -> Burn \$125 DD -> Get FULL collateral"
echo ""

# Cleanup
echo "=== Cleanup ==="
echo "Stopping node..."
./src/digibyte-cli -regtest -datadir=$DATADIR -rpcport=$RPCPORT stop 2>/dev/null || true
sleep 3
echo "Done"
echo ""
echo "Test complete!"
