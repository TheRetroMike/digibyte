# TEST/FUNCTIONAL/ KNOWLEDGE BASE

## OVERVIEW

273 Python functional tests for DigiByte. Bitcoin Core v26.2 tests adapted with DigiByte constants, multi-algo support, and Dandelion++ handling.

## STRUCTURE

```
test/functional/
├── test_framework/         # Core test infrastructure
│   ├── test_framework.py   # DigiByteTestFramework base class
│   ├── blocktools.py       # COINBASE_MATURITY, create_block
│   ├── messages.py         # P2P messages, BLOCK_VERSION_*
│   ├── address.py          # dgbrt1 addresses
│   ├── script.py           # Script utilities
│   └── test_node.py        # PRIV_KEYS with DGB format
├── data/                   # Test data files
├── mocks/                  # Mock implementations
├── wallet_*.py             # Wallet tests
├── rpc_*.py                # RPC tests
├── p2p_*.py                # P2P tests
├── feature_*.py            # Feature tests
├── mining_*.py             # Mining tests
└── mempool_*.py            # Mempool tests
```

## DIGIBYTE TEST CONSTANTS

```python
# From test_framework/blocktools.py
COINBASE_MATURITY = 8        # NOT 100!
COINBASE_MATURITY_2 = 100    # For wallet tests
SUBSIDY = 72000              # NOT 50!

# Fees - 100x Bitcoin (KvB not vB)
MIN_RELAY_TX_FEE = Decimal('0.001')
DEFAULT_FEE = Decimal('0.1')

# From test_framework/messages.py
BLOCK_VERSION_SCRYPT  = (0 << 8)   # 0x0000
BLOCK_VERSION_SHA256D = (2 << 8)   # 0x0200
BLOCK_VERSION_GROESTL = (4 << 8)   # 0x0400
BLOCK_VERSION_SKEIN   = (6 << 8)   # 0x0600
BLOCK_VERSION_QUBIT   = (8 << 8)   # 0x0800
```

## COMMON TEST FIXES

### 1. Dandelion++ (MOST COMMON)
```python
# ALWAYS disable for transaction tests
def set_test_params(self):
    self.extra_args = [["-dandelion=0"], ["-dandelion=0"]]
```

### 2. Coinbase Maturity
```python
# Bitcoin: 100 blocks, DigiByte: 8 or 100
from test_framework.blocktools import COINBASE_MATURITY, COINBASE_MATURITY_2

# Initial setup: use 8
self.generate(node, COINBASE_MATURITY)

# Wallet tests expecting 100: keep 100
self.generate(node, COINBASE_MATURITY_2)
```

### 3. Fee Rates (100x multiplier)
```python
# Bitcoin: 10 sat/vB → DigiByte: 1000 sat/kB
fee_rate = 1000  # NOT 10

# fundrawtransaction
node.fundrawtransaction(raw_tx, {"fee_rate": 1000})
```

### 4. Address Prefixes
```python
# Regtest addresses
assert address.startswith('dgbrt1')  # NOT 'bcrt1'
# P2PKH starts with 's' or 't', NOT 'm' or 'n'
# P2SH starts with 'y', NOT '2'
```

### 5. Block Version
```python
# After block 100, set algo bits
if height >= 100:
    block.nVersion = 0x20000002  # Scrypt with BIP9
    # Or specific algo: 0x20000202 (SHA256D)
```

## RUNNING TESTS

```bash
# Single test with debug
./test/functional/wallet_basic.py --loglevel=debug --nocleanup

# With options
./test/functional/[test].py --descriptors --v2transport

# All tests
./test/functional/test_runner.py

# Specific tests
./test/functional/test_runner.py wallet_* rpc_*
```

## TEST VARIANTS

Always test with multiple flags:
- `--descriptors` - Descriptor wallets
- `--legacy-wallet` - Legacy wallets
- `--usecli` - CLI mode
- `--v2transport` - BIP324 P2P

## DIAGNOSTIC CHECKLIST

| Error | Fix |
|-------|-----|
| Fee insufficient | Multiply fee by 100x |
| Maturity error | Use COINBASE_MATURITY (8) |
| TX not in mempool | Add `-dandelion=0` |
| Invalid address | Use `dgbrt1` not `bcrt1` |
| Block rejected | Check version bits for height |

## KEY DOCUMENTATION

- `../DIGISWARM_AI/COMMON_FIXES.md` - Comprehensive fix patterns
- `../DIGISWARM_AI/WORK_GROUPS.md` - Test group status
- `../../CLAUDE.md` - Project-wide guidance
