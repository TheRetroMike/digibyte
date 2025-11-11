#!/bin/bash
# Script to generate correct assumeutxo values for DigiByte regtest

set -e

echo "Generating DigiByte regtest assumeutxo values..."

# Start digibyted in regtest mode
echo "Starting digibyted in regtest mode..."
./src/digibyted -regtest -daemon -datadir=/tmp/digibyte_regtest

# Wait for startup
sleep 5

# Generate a new address
ADDR=$(./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest getnewaddress)
echo "Generated address: $ADDR"

# Generate 110 blocks
echo "Generating 110 blocks..."
./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest generatetoaddress 110 $ADDR > /dev/null

# Get block info at height 110
BLOCK110=$(./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest getblockhash 110)
CHAINTX110=$(./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest getblockheader $BLOCK110 | jq -r '.nTx')
echo "Block at height 110: $BLOCK110"

# Dump UTXO set at height 110
echo "Creating UTXO snapshot at height 110..."
./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest dumptxoutset /tmp/snapshot_110.dat

# Extract hash_serialized from snapshot (this would need a small utility program)
# For now, we'll note that this needs to be extracted

# Generate to height 299
echo "Generating to height 299..."
./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest generatetoaddress 189 $ADDR > /dev/null

# Get block info at height 299
BLOCK299=$(./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest getblockhash 299)
CHAINTX299=$(./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest getblockheader $BLOCK299 | jq -r '.nTx')
echo "Block at height 299: $BLOCK299"

# Dump UTXO set at height 299
echo "Creating UTXO snapshot at height 299..."
./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest dumptxoutset /tmp/snapshot_299.dat

# Stop digibyted
./src/digibyte-cli -regtest -datadir=/tmp/digibyte_regtest stop

echo ""
echo "Results for chainparams.cpp:"
echo "=========================="
echo "Height 110:"
echo "  blockhash: $BLOCK110"
echo "  nChainTx: 111 (need to verify)"
echo "  hash_serialized: (needs to be extracted from snapshot)"
echo ""
echo "Height 299:"
echo "  blockhash: $BLOCK299"
echo "  nChainTx: 300 (need to verify)"
echo "  hash_serialized: (needs to be extracted from snapshot)"
echo ""
echo "Snapshots saved to /tmp/snapshot_110.dat and /tmp/snapshot_299.dat"