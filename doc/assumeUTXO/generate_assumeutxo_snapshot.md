# Generate AssumeUTXO Snapshot for DigiByte Block 21,500,000

This guide provides the exact commands needed to generate the assumeutxo snapshot data for block 21,500,000.

## Prerequisites
- DigiByte node fully synced past block 21,500,000
- Sufficient disk space for snapshot file (~2-3 GB)

## Commands

```bash
# 1. Start digibyted if not already running
./src/digibyted -daemon

# 2. Wait for sync to complete (check current block height)
./src/digibyte-cli getblockcount

# 3. Get block info at height 21,500,000 to verify hash
./src/digibyte-cli getblockhash 21500000
./src/digibyte-cli getblock 00000000000000007cbe22612937832c2e6341ec867e881979e2246df44fa727

# 4. Generate UTXO snapshot at block 21,500,000
./src/digibyte-cli dumptxoutset snapshot_21500000.dat 00000000000000007cbe22612937832c2e6341ec867e881979e2246df44fa727

# 5. Get the total transaction count (nChainTx) at block 21,500,000
./src/digibyte-cli getchaintxstats 21500000

# 6. Extract snapshot info from the dumptxoutset output
# The output will show something like:
# {
#   "coins_written": 12345678,
#   "base_hash": "00000000000000007cbe22612937832c2e6341ec867e881979e2246df44fa727",
#   "base_height": 21500000,
#   "path": "snapshot_21500000.dat",
#   "txoutset_hash": "HASH_VALUE_HERE",
#   "nchaintx": TOTAL_TX_COUNT_HERE
# }
```

## Required Values

From the `dumptxoutset` output, extract:
- **hash_serialized**: Use the `txoutset_hash` value
- **nChainTx**: Use the `nchaintx` value (or from `getchaintxstats`)

## Update chainparams.cpp

Once you have the values, update `src/kernel/chainparams.cpp`:

```cpp
m_assumeutxo_data = {
    {
        .height = 21'500'000,
        .hash_serialized = AssumeutxoHash{uint256S("0xREPLACE_WITH_TXOUTSET_HASH")},
        .nChainTx = REPLACE_WITH_NCHAINTX_VALUE,
        .blockhash = uint256S("0x00000000000000007cbe22612937832c2e6341ec867e881979e2246df44fa727")
    },
};
```

## Notes
- The snapshot generation may take 10-30 minutes depending on hardware
- The snapshot file can be distributed to allow fast node bootstrapping
- Users can load the snapshot with `loadtxoutset` RPC command