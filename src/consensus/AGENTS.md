# CONSENSUS/ KNOWLEDGE BASE

## OVERVIEW

DigiByte consensus rules. Multi-algorithm difficulty adjustment (DCA), DigiDollar transaction validation, and fork activation logic.

## STRUCTURE

```
consensus/
├── dca.cpp/h           # Difficulty Calculation Algorithm (DigiShield, MultiShield)
├── volatility.cpp/h    # DigiDollar volatility tracking
├── err.cpp/h           # Consensus error handling
├── digidollar.cpp/h    # DigiDollar consensus rules
├── digidollar_transaction_validation.cpp/h  # DD tx validation
├── digidollar_tx.cpp/h # DD transaction types
├── params.h            # Consensus parameters per network
├── tx_check.cpp/h      # Transaction validation
├── tx_verify.cpp/h     # TX input verification
├── merkle.cpp/h        # Merkle tree computation
├── amount.h            # MAX_MONEY = 21 billion
└── validation.h        # Validation state
```

## DIGIBYTE-SPECIFIC

**dca.cpp - Difficulty Calculation Algorithm:**
- DigiShield V1 (block 67,200 mainnet)
- MultiAlgo V2 (block 145,000)
- MultiShield V3 (block 400,000)
- DigiSpeed V4 (block 1,430,000)
- Per-algorithm difficulty tracking

**DigiDollar Files:**
- `digidollar.cpp/h` - Core DD consensus
- `digidollar_transaction_validation.cpp` - DD TX rules
- `volatility.cpp/h` - Price stability tracking
- `err.cpp/h` - DD error codes

## KEY FUNCTIONS

```cpp
// dca.cpp
GetNextWorkRequired()        // Per-algo difficulty
DigiShieldGetNextWorkRequired()
MultiShieldGetNextWorkRequired()

// digidollar_transaction_validation.cpp
ValidateDigiDollarTransaction()
CheckDigiDollarMint()
CheckDigiDollarBurn()

// tx_check.cpp
CheckTransaction()           // Basic TX validation
```

## FORK HEIGHTS (params.h)

| Parameter | Mainnet | Testnet | Regtest |
|-----------|---------|---------|---------|
| multiAlgoHeight | 145,000 | 145,000 | 100 |
| multiShieldHeight | 400,000 | 400,000 | 200 |
| digiShieldHeight | 67,200 | 67,200 | 334 |
| digiSpeedHeight | 1,430,000 | 1,430,000 | 400 |
| odoHeight | 9,112,320 | 9,112,320 | 600 |

## ANTI-PATTERNS

| NEVER | Why |
|-------|-----|
| Assume single difficulty | 5 independent difficulty targets |
| Hardcode Bitcoin block times | 15s blocks, 75s per algo |
| Skip DigiDollar validation | Consensus-critical |
| Ignore fork heights in tests | Behavior changes at boundaries |
