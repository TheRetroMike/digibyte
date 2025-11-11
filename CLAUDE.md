# CLAUDE.md - AI Assistant Guide for DigiByte Development

## Overview
This file provides essential context for AI assistants working on the DigiByte codebase, particularly for the Bitcoin Core v26.2 merge creating DigiByte v8.26.

## Repository Structure
```
digibyte/                        # Current v8.26 working directory
├── src/                         # Source code
├── test/functional/             # Python functional tests
├── bitcoin-v26.2-for-digibyte/  # Bitcoin v26.2 reference
├── digibyte-v8.22.2/           # DigiByte v8.22.2 (SOURCE OF TRUTH)
└── doc/                        # Documentation
```

## Test Fix Workflow Documentation

### For Orchestrator Agents
- **MAIN_WORK_PROMPT.md** - Instructions for managing sub-agents
- **WORK_GROUPS.md** - Test groups with status tracking
- **TEST_FIX_PROGRESS.md** - Overall progress dashboard

### For Sub-Agent Workers
- **SUBAGENT_TEST_FIX_PROMPT.md** - Test fix methodology
- **COMMON_FIXES.md** - Reusable fix patterns (CHECK FIRST!)
- **APPLICATION_BUGS.md** - Track real bugs found

### Current Status (2025-08-24)
- **Total Tests**: 315 (109 failing)
- **Strategy**: Orchestrator deploys sub-agents on test groups
- **Phase 1**: Groups 1-3 (sequential - critical foundation)
- **Phase 2**: Groups 4-9 (parallel - max 3 agents)
- **Phase 3**: Groups 10-13 (parallel - cleanup)

## Critical DigiByte Constants

### Always use these values instead of Bitcoin defaults:
```python
# Block & Mining
BLOCK_TIME = 15                  # seconds (NOT 600!)
COINBASE_MATURITY = 8           # blocks (NOT 100!)
COINBASE_MATURITY_2 = 100       # After certain height
SUBSIDY = 72000                  # DGB (NOT 50!)
MAX_MONEY = 21000000000          # 21 billion DGB

# Fees (DigiByte uses KvB not vB!)
MIN_RELAY_TX_FEE = Decimal('0.001')      # DGB/kB
DEFAULT_TRANSACTION_FEE = Decimal('0.1')  # DGB/kB

# Network
P2P_PORT = 12024                 # Mainnet
P2P_PORT_TESTNET = 12025        # Testnet

# Address Formats
REGTEST_BECH32 = 'dgbrt'        # NOT 'bcrt'
TESTNET_BECH32 = 'dgbt'         # NOT 'tb'
```

## DigiByte Unique Features

### Multi-Algorithm Mining
- 5 algorithms: SHA256D (0), Scrypt (1), Groestl (2), Skein (3), Qubit (4)
- Odocrypt (7) activates at height 9,112,320
- Each targets 75-second block time (15s × 5 algos)

### Custom RPC Commands
- `getblockreward` - Current block reward
- Enhanced `getmininginfo` - Per-algorithm stats
- Enhanced `getdifficulty` - All algorithm difficulties

### Dandelion++ Privacy
- Two-pool system: stempool (private) and mempool (public)
- **CRITICAL**: Read doc/DANDELION_INFO.md before changes
- Files: src/dandelion.cpp, src/stempool.h

### Difficulty Adjustment Evolution
- DigiShield V1 (block 67,200)
- MultiAlgo V2 (block 145,000)
- MultiShield V3 (block 400,000)
- DigiSpeed V4 (block 1,430,000)

## Quick Debug Commands

```bash
# Check for Bitcoin constants that need updating
grep -r "50.*BTC\|600.*seconds\|100.*blocks\|bcrt1" test/functional/

# Compare with working v8.22.2
diff digibyte-v8.22.2/test/functional/[test].py test/functional/[test].py

# Run test with debug info
./test/functional/[test].py --loglevel=debug --nocleanup

# Check current failing tests
python3 test/functional/test_runner.py --list-failing
```

## Common Test Fix Patterns

1. **Block Rewards**: 50 → 72000
2. **Block Time**: 600s → 15s  
3. **Maturity**: 100 → 8 (or keep 100 for COINBASE_MATURITY_2)
4. **Fees**: vB → KvB (multiply by 1000)
5. **Address**: bcrt1 → dgbrt1

For detailed patterns, see COMMON_FIXES.md

## Important Notes

- **Three-way comparison**: Always compare v8.26 ↔ v8.22.2 ↔ Bitcoin v26.2
- **Test all variants**: --descriptors, --legacy-wallet, --usecli, --v2transport
- **Document everything**: Update tracking files immediately
- **Mock scrypt**: Currently using mock, causes PoW validation issues
- **V2 transport**: BIP324 V2 P2P transport is fully supported and tested (disabled by default, enable with `-v2transport=1`)

---

*For detailed test fix instructions, see the workflow documentation files listed above.*