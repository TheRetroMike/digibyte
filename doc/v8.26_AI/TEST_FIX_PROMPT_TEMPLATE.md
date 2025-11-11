# DigiByte v8.26 C++ Unit Test Fix Instructions

## Your Mission
You are tasked with systematically fixing C++ unit tests for DigiByte v8.26 (Bitcoin v26.2 merge). Your goal is to ensure all tests pass while identifying and fixing any real bugs discovered during the process.

## Your Assigned Category
**[CATEGORY_NAME_HERE]**

Test files to fix:
[LIST_TEST_FILES_HERE]

## Initial Setup

1. **Read CLAUDE.md First**
   ```bash
   cat CLAUDE.md
   ```
   This contains critical information about DigiByte-specific values and the test categorization.

2. **Setup Your Environment**
   ```bash
   cd /mnt/c/Users/Jared/code/digibyte
   ./autogen.sh
   ./configure --enable-tests --enable-bench --enable-debug CXXFLAGS="-O0 -g"
   make -j6
   ```

## Test Fixing Process

### Step 1: Run Each Test File Individually
For each test file in your assigned category:

```bash
# Example for a test file
./src/test/test_digibyte --log_level=all --run_test=TEST_NAME_HERE 2>&1 | tee TEST_NAME_output.log
```

### Step 2: Analyze Failures
1. Identify the specific test cases that fail
2. Note the exact error messages
3. Determine if it's a Bitcoin vs DigiByte difference

### Step 3: Compare with Reference Code
**CRITICAL**: Always check three codebases:

```bash
# Your test file
vim src/test/YOUR_TEST_FILE.cpp

# DigiByte v8.22.2 reference (SOURCE OF TRUTH for DigiByte values)
vim ../digibyte-v8.22.2/src/test/YOUR_TEST_FILE.cpp

# Bitcoin v26.2 reference (to understand what changed)
vim ../bitcoin-v26.2-for-digibyte/src/test/YOUR_TEST_FILE.cpp
```

### Step 4: Fix Test Failures

#### For DigiByte-Specific Values:
```cpp
// WRONG (Bitcoin value)
BOOST_CHECK_EQUAL(MAX_MONEY, 21000000 * COIN);

// CORRECT (DigiByte value from v8.22.2)
BOOST_CHECK_EQUAL(MAX_MONEY, 21000000000 * COIN);
```

#### Common DigiByte Replacements:
- Bitcoin addresses (1..., 3...) → DigiByte addresses (D..., S...)
- "bc1" → "dgb1", "tb1" → "dgbt1", "bcrt1" → "dgbrt1"
- 21 million → 21 billion supply
- 600 seconds → 15 seconds block time
- Single algorithm → 5 algorithms (SHA256D, Scrypt, Groestl, Skein, Qubit) + Odocrypt
- For Dandelion++ issues: Read doc/DANDELION_INFO.md FIRST

### Step 5: Handle Application Bugs

**IMPORTANT**: If you discover a bug in the actual application code (not just test code):

1. **Fix the Bug** in the application code
2. **Document the Fix** with detailed comments
3. **Report the Bug** using this format:

```markdown
## APPLICATION BUG FIXED
**File**: src/validation.cpp:1234
**Test**: validation_tests.cpp::CheckBlockIndex
**Issue**: Missing DigiByte-specific block validation for multi-algo
**Root Cause**: Bitcoin v26.2 merge removed multi-algo validation logic
**Fix Applied**: 
```cpp
// Added DigiByte multi-algo validation
if (pindex->nHeight >= consensusParams.nMultiAlgoActivationHeight) {
    if (!CheckProofOfWork(block.GetPoWHash(block.GetAlgo()), pindex->nBits, block.GetAlgo(), consensusParams)) {
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "bad-diffbits", "incorrect proof of work");
    }
}
```
**Impact**: Blocks mined with alternate algorithms would be rejected
**Testing**: Verified with multi-algo block acceptance test
```

### Step 6: Verify Your Fix
```bash
# Run the specific test again
./src/test/test_digibyte --log_level=all --run_test=YOUR_TEST_NAME

# If it passes, run all tests in the file to ensure no regressions
./src/test/test_digibyte --log_level=all --run_test=YOUR_TEST_FILE_WITHOUT_CPP
```

### Step 7: Document Your Changes
For each test file fixed, create a summary:

```markdown
## Test: YOUR_TEST_FILE.cpp
**Status**: FIXED ✓
**Changes Made**:
1. [List specific changes]
2. [More changes]

**Application Bugs Found**: [None/List them]
**v8.22.2 Reference**: [Files and lines used as reference]
```

## Critical DigiByte Values Reference

Always use these values from v8.22.2:

```cpp
// Network
static const int MAINNET_DEFAULT_PORT = 12024;
static const unsigned char MAINNET_MESSAGE_START[4] = {0xfa, 0xc3, 0xb6, 0xda};

// Consensus
static const CAmount MAX_MONEY = 21000000000 * COIN;  // 21 billion
static const int POW_TARGET_SPACING = 15;  // 15 seconds

// Address Prefixes
base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,30);  // D
base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,63);  // S
bech32_hrp = "dgb";  // dgb1...

// Mining
enum {
    ALGO_SHA256D = 0,
    ALGO_SCRYPT = 1,
    ALGO_GROESTL = 2,
    ALGO_SKEIN = 3,
    ALGO_QUBIT = 4,
    ALGO_ODO = 7  // Odocrypt (height 9,112,320+)
};

// Example addresses
"DGSbdXzKqPNLBpPDWK7MXgXN45LxYvPqFD"  // P2PKH
"SfBbrV3yCGjKW52dac8Tgkvqd6zMgvPZFG"  // P2SH
```

## Test Data Files
Many tests use JSON data files. Update these too:
- `src/test/data/key_io_valid.json`
- `src/test/data/key_io_invalid.json`
- `src/test/data/tx_valid.json`
- `src/test/data/tx_invalid.json`

## Final Checklist
- [ ] All tests in your category pass
- [ ] No tests were disabled or commented out
- [ ] All DigiByte-specific values verified against v8.22.2
- [ ] All application bugs documented and fixed
- [ ] Test output logs saved
- [ ] Summary report created

## Remember
- **NEVER** skip or disable failing tests
- **ALWAYS** check v8.22.2 for correct DigiByte values
- **FIX** application bugs when found (don't just work around them)
- **DOCUMENT** every change thoroughly
- **TEST** comprehensively after each fix

Your systematic approach will ensure DigiByte v8.26 is production-ready with all tests passing and all bugs fixed.