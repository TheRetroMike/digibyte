# DigiByte Python Functional Tests - Setup Guide

## Prerequisites

The DigiByte functional tests require the `digibyte-scrypt` Python package for proper Proof-of-Work validation.

### Installing digibyte-scrypt

The package is available on PyPI: https://pypi.org/project/digibyte-scrypt/

#### Requirements
- Python 3.6+
- Python development headers (python3-dev)
- C compiler (gcc/build-essential)

#### Installation Steps

1. **Install system dependencies (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install -y python3-dev build-essential
```

2. **Install digibyte-scrypt package:**
```bash
pip3 install digibyte-scrypt
# OR with --break-system-packages flag if needed:
pip3 install --break-system-packages digibyte-scrypt
```

#### Alternative: User Installation
If you don't have sudo access:
```bash
pip3 install --user digibyte-scrypt
```

#### Troubleshooting

**Error: "Python.h: No such file or directory"**
- Solution: Install python3-dev headers
```bash
sudo apt-get install python3-dev
```

**Error: "error: Microsoft Visual C++ 14.0 is required" (Windows)**
- Solution: Install Visual Studio Build Tools

### Running Tests

Once digibyte-scrypt is installed:
```bash
# Run all tests (parallel execution - recommended)
python3 test/functional/test_runner.py --jobs=16

# Run all tests (sequential - slower)
python3 test/functional/test_runner.py

# Run specific test
python3 test/functional/test_runner.py feature_block.py

# Run multiple specific tests in parallel
python3 test/functional/test_runner.py feature_block.py wallet_basic.py --jobs=4

# Run with specific wallet type
python3 test/functional/test_runner.py wallet_basic.py --descriptors
python3 test/functional/test_runner.py wallet_basic.py --legacy-wallet

# Run with parallel execution and specific wallet type
python3 test/functional/test_runner.py wallet_*.py --legacy-wallet --jobs=16
```

**Performance Note:** Using `--jobs=16` can significantly speed up test execution by running multiple tests in parallel. Adjust the number based on your system's CPU cores.

### Mock Implementation

If you cannot install the real digibyte-scrypt package, a mock implementation is provided in:
`test/functional/test_framework/digibyte_scrypt.py`

**Note:** The mock uses SHA256 instead of scrypt, which will cause PoW validation tests to fail. This is only suitable for testing non-mining functionality.

## Common Test Issues

### Block Mining Failures
- **Symptom:** "high-hash" errors, "bad-pow" errors
- **Cause:** Mock scrypt implementation or incorrect PoW calculation
- **Solution:** Install real digibyte-scrypt package

### DigiByte-Specific Constants
Tests need to use DigiByte values, not Bitcoin values:
- Block reward: 72000 DGB (not 50 BTC)
- Block time: 15 seconds (not 600 seconds)
- Coinbase maturity: 8 blocks initially, 100 blocks after hardforks
- Address prefixes: dgbrt1 for regtest (not bcrt1)

### Fee Calculations
- DigiByte uses KvB (kilovirtual bytes)
- Bitcoin uses vB (virtual bytes)
- Multiply Bitcoin fee rates by 1000 for DigiByte

## Test Categories

The functional tests are organized into several categories:
- **feature_** - Core features and consensus rules
- **wallet_** - Wallet functionality
- **p2p_** - Peer-to-peer network behavior
- **rpc_** - RPC interface tests
- **mining_** - Mining and block generation
- **mempool_** - Memory pool management
- **interface_** - CLI and other interfaces

## Support

For issues specific to DigiByte test failures, check:
- COMMON_FIXES.md - Known patterns and solutions
- APPLICATION_BUGS.md - Tracked bugs in the application code
- TEST_FIX_PROGRESS.md - Current status of test fixes