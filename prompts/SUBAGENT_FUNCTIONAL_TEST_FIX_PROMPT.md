# SUBAGENT FUNCTIONAL TEST FIX PROMPT - DigiDollar Functional Test Fix Methodology

## Your Mission

You are a **Python/Bitcoin test specialist** tasked with fixing failing DigiDollar functional tests in the DigiByte codebase. Your goal is to fix the tests while protecting the 6 tests that are already passing.

---

## 📚 STEP 0: Read Architecture Documents FIRST

**MANDATORY - DO THIS BEFORE ANYTHING ELSE**:

Before you fix ANY test, you MUST read these two architecture documents:

1. **`/home/jared/Code/digibyte/DIGIDOLLAR_EXPLAINER.md`**
   - Complete overview of DigiDollar system
   - How minting, redemption, collateral work
   - System architecture and design decisions
   - User-facing perspective of how the system works

2. **`/home/jared/Code/digibyte/DIGIDOLLAR_ARCHITECTURE.md`**
   - Technical implementation details
   - RPC command reference and expected responses
   - Data structures and validation rules
   - Testing methodology and patterns

**Why This Is Critical**:
- You're fixing tests that verify DigiDollar behavior
- You MUST understand HOW DigiDollar is supposed to work
- Without this context, you might "fix" tests incorrectly
- You need to distinguish test bugs from application bugs
- Architecture docs explain WHY things work the way they do

**DO NOT skip this step**. Read both documents thoroughly before proceeding to fix tests.

---

## ⚠️ CRITICAL RULES - READ BEFORE STARTING

### Oracle System Context

**STOP**: The DigiDollar oracle system has **NOT been implemented yet**!

**What This Means**:
- All tests use **mock hardcoded oracle prices**
- The standard mock price is **`0.01 USD per DGB`** (1 cent per DGB)
- Tests call `setmockoracleprice(1)` to set this price
- Some tests use `setmockoracleprice(50000)` which is $0.50 per DGB - this is NORMAL

**Your Rules**:
- ❌ **NEVER** try to implement real oracle functionality
- ❌ **NEVER** modify oracle implementation code in `src/`
- ❌ **NEVER** "fix" mock prices thinking they're wrong
- ✅ **ALWAYS** use mock prices in tests
- ✅ **ACCEPT** that oracle tests may test future functionality

### Working Functionality: DO NOT BREAK

**These features are CURRENTLY WORKING**:
1. ✅ **Minting** - Users can create DigiDollars with DGB collateral
2. ✅ **Sending** - Users can transfer DD between addresses
3. ✅ **Receiving** - Users can receive DD transfers
4. ✅ **Redemption** - Users can burn DD and unlock collateral

**ABSOLUTE RULE**: Your fixes must NOT break any of these features!

### Mandatory Regression Testing

**After EVERY change you make**, run these tests:
```bash
./test/functional/digidollar_transfer.py        # Tests sending/receiving
./test/functional/digidollar_redeem_stats.py    # Tests redemption
./test/functional/digidollar_redemption_amounts.py  # Tests collateral
```

**If ANY of these fail**:
1. 🛑 **STOP** - Don't continue
2. ⏮️ **REVERT** - Undo your changes immediately
3. 📝 **REPORT** - Document what went wrong
4. 🤔 **INVESTIGATE** - Figure out why before trying again

### What You SHOULD Fix

✅ **Fix test code bugs**:
- Import errors (wrong imports)
- Method name errors (calling methods that don't exist)
- Wrong assertions (test expects wrong thing)
- Wallet initialization issues

✅ **Fix application bugs**:
- Tests are MEANT to expose bugs in the implementation!
- When a test fails because the implementation is wrong → **FIX THE BUG**
- DigiDollar is in active development - bugs exist and should be fixed
- Just test thoroughly after each fix (see regression testing rules above)

### How to Decide: Test Bug vs Application Bug

**Decision Process**:
1. 🔍 **Test fails** → Investigate WHY it failed
2. 📚 **Compare** with passing tests and working functionality
3. 🤔 **Determine root cause**:
   - Is the test expectation wrong? → Fix the test
   - Is the implementation behavior wrong? → Fix the implementation
   - Not sure? → Investigate more deeply
4. 🔧 **Apply the fix** (test code OR implementation code)
5. ✅ **Run full regression suite** (see mandatory testing rules above)
6. 📝 **Document what you fixed and why**

**Example - Test Bug**:
```python
# Test expects wrong key name
# OLD (wrong):
assert 'supply' in stats, "Missing stats section: supply"
# NEW (correct):
assert 'total_dd_supply' in stats, "Missing stats key: total_dd_supply"
```

**Example - Application Bug**:
```python
# Test revealed collateral calculation is wrong in src/digidollar.cpp
# The implementation was returning collateral - fee instead of just collateral
# FIXED: Modified src/digidollar.cpp line 247 to return full collateral
# Test now passes with correct expectation:
assert returned_collateral == expected_collateral, "Collateral should match exactly"
```

---

## 🎯 Current Assignment

**Status:** Out of 18 total DigiDollar functional tests, **6 are passing** and **12 are failing** (33% pass rate)

**Your Goal:** Fix the failing tests to achieve 100% pass rate (18/18)

---

## Test Categories & Fixes

### Category 1: Import Errors (3 tests) ⚡ QUICK FIX

#### Problem
Tests are importing functions that don't exist as standalone imports in `test_framework.util`.

#### Affected Tests
1. `digidollar_basic.py` - Importing `connect_nodes`
2. `digidollar_protection.py` - Importing `assert_in`
3. `digidollar_wallet.py` - Importing `assert_in`

#### Fix Pattern #1: connect_nodes

**File**: `/home/jared/Code/digibyte/test/functional/digidollar_basic.py`

**Current Code (BROKEN)**:
```python
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    connect_nodes,  # ← DOESN'T EXIST IN test_framework.util
)

# Later in code (line 236):
def test_multi_node_sync(self):
    connect_nodes(self.nodes[0], self.nodes[1])  # ← WRONG
```

**Why It Fails**:
- `connect_nodes` doesn't exist as a standalone function in `test_framework.util`
- It's a METHOD of the `DigiByteTestFramework` class
- Available as `self.connect_nodes(node_a_index, node_b_index)`

**Fixed Code**:
```python
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    # Removed connect_nodes - use self.connect_nodes() instead
)

# Later in code:
def test_multi_node_sync(self):
    self.connect_nodes(0, 1)  # ← CORRECT (class method, uses node indices)
```

**Steps**:
1. Remove `connect_nodes` from imports (line 16)
2. Find all calls to `connect_nodes(...)` in the file
3. Replace with `self.connect_nodes(0, 1)` (using node indices, not node objects)

---

#### Fix Pattern #2: assert_in

**Files**:
- `/home/jared/Code/digibyte/test/functional/digidollar_protection.py`
- `/home/jared/Code/digibyte/test/functional/digidollar_wallet.py`

**Current Code (BROKEN)**:
```python
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    assert_in,  # ← DOESN'T EXIST
)

# Later in code:
assert_in('supply', stats)
assert_in(dd_address, wallet_addresses)
```

**Why It Fails**:
- `assert_in` doesn't exist in `test_framework.util`
- It was never part of the DigiByte/Bitcoin test framework

**Fix Option A - Use Standard Python**:
```python
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    # Removed assert_in
)

# Replace assert_in calls with standard Python:
assert 'supply' in stats, "'supply' should be in stats"
assert dd_address in wallet_addresses, "DD address should be in wallet addresses"
```

**Fix Option B - Define Helper Locally** (if used many times):
```python
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

# Define helper at module level (after imports):
def assert_in(item, collection, msg=""):
    """Assert that item is in collection."""
    assert item in collection, msg or f"{item} not found in {collection}"

# Use as before:
assert_in('supply', stats)
```

**Recommended**: Use Option A (standard Python) for simplicity.

---

### Category 2: Wallet Initialization Errors (8 tests) 🔍 INVESTIGATION NEEDED

#### Problem
All 8 tests fail during setup with: `test_framework.authproxy.JSONRPCException: Method not found (-32601)` when calling `createwallet`.

#### Affected Tests
1. `digidollar_mint.py`
2. `digidollar_network_relay.py`
3. `digidollar_oracle.py`
4. `digidollar_persistence.py`
5. `digidollar_redeem.py`
6. `digidollar_redemption_e2e.py`
7. `digidollar_transactions.py`

#### Error Details
```python
File "test_framework/test_framework.py", line 439, in init_wallet
    n.createwallet(wallet_name=wallet_name, descriptors=self.options.descriptors, load_on_startup=True)
...
test_framework.authproxy.JSONRPCException: Method not found (-32601)
```

#### Investigation Steps

**Step 1: Check How Passing Tests Handle Wallets**

Look at `digidollar_transfer.py` (which passes) to see the pattern:
```bash
grep -A30 "class.*TestFramework\|skip_if_no_wallet\|set_test_params\|setup_network" test/functional/digidollar_transfer.py | head -60
```

**Step 2: Compare with Failing Tests**

Check `digidollar_mint.py` (which fails):
```bash
grep -A30 "class.*TestFramework\|skip_if_no_wallet\|set_test_params" test/functional/digidollar_mint.py | head -60
```

**Step 3: Identify the Difference**

Common patterns in Bitcoin/DigiByte tests:
- Some tests call `self.skip_if_no_wallet()` in `skip_test_if_missing_module()`
- Some tests use specific `extra_args` for wallet
- Some tests don't create wallets at all (use default)

#### Likely Solutions

**Solution A: Add skip_if_no_wallet** (if test actually needs wallet):
```python
def skip_test_if_missing_module(self):
    self.skip_if_no_wallet()  # ← Add this
```

**Solution B: Don't Create Wallet** (if framework does it automatically):
```python
# The test framework may auto-create wallets
# Check if tests have this in set_test_params():
def set_test_params(self):
    self.num_nodes = 2
    self.setup_clean_chain = True
    # May NOT need to call createwallet manually
```

**Solution C: Different Node Args**:
```python
def set_test_params(self):
    self.extra_args = [
        ["-wallet="],  # ← Or specific wallet args
        ["-wallet="],
    ]
```

**How to Determine Which Solution**:
1. If `digidollar_transfer.py` has `skip_if_no_wallet()` → use Solution A
2. If `digidollar_transfer.py` doesn't call `createwallet` → use Solution B
3. If `digidollar_transfer.py` has different `extra_args` → use Solution C

#### Fix Pattern (Once Pattern Identified)

Apply the same fix to ALL 8 failing tests. They should all use the same wallet initialization pattern.

---

### Category 3: Test Logic Errors (2 tests) ⚡ QUICK FIX

#### Fix #1: digidollar_rpc.py - Wrong Stats Keys

**File**: `/home/jared/Code/digibyte/test/functional/digidollar_rpc.py`
**Line**: Around 151

**Current Code (BROKEN)**:
```python
def test_system_monitoring_commands(self):
    # ...
    stats = self.nodes[0].getdigidollarstats()

    # Test expects old key names:
    expected_sections = ['supply', 'collateral', 'health', 'oracle']
    for section in expected_sections:
        assert section in stats, f"Missing stats section: {section}"  # ← FAILS HERE
```

**What RPC Actually Returns**:
```python
{
    'health_percentage': 30000,
    'health_status': 'healthy',
    'total_collateral_dgb': Decimal('0E-8'),
    'total_dd_supply': 0,  # ← NOT 'supply'
    'oracle_price_cents': 1,
    'is_emergency': False,
    'system_collateral_ratio': 30000,
    'total_collateral_locked': Decimal('0E-8'),
    'active_positions': 0,
    'oracle_price_age': 0,
    'dca_tier': {...},
}
```

**Fixed Code**:
```python
def test_system_monitoring_commands(self):
    # ...
    stats = self.nodes[0].getdigidollarstats()

    # Test for actual key names:
    expected_keys = [
        'health_percentage',
        'health_status',
        'total_collateral_dgb',
        'total_dd_supply',  # ← Correct key name
        'oracle_price_cents',
        'is_emergency',
        'system_collateral_ratio',
        'total_collateral_locked',
        'active_positions',
    ]
    for key in expected_keys:
        assert key in stats, f"Missing stats key: {key}"
```

**Steps**:
1. Read the test file to find where stats are checked
2. Update the expected keys to match actual RPC response
3. Change assertion message from "section" to "key"

---

#### Fix #2: digidollar_stress.py - Wrong Method Name

**File**: `/home/jared/Code/digibyte/test/functional/digidollar_stress.py`
**Line**: 34

**Current Code (BROKEN)**:
```python
def setup_network(self):
    self.setup_nodes()
    self.connect_nodes_bi(0, 1)  # ← Method doesn't exist
```

**Error**:
```
AttributeError: 'DigiDollarStressTest' object has no attribute 'connect_nodes_bi'.
Did you mean: 'connect_nodes'?
```

**Why It Fails**:
- `connect_nodes_bi` doesn't exist in the test framework
- It's a typo or confusion with `connect_nodes`

**Fixed Code**:
```python
def setup_network(self):
    self.setup_nodes()
    self.connect_nodes(0, 1)  # ← Use correct method name
```

**Steps**:
1. Find the line with `connect_nodes_bi`
2. Replace with `connect_nodes`

---

## Workflow Summary

### Quick Win Path (Start Here!)

**Fix Order** (easiest → hardest):
1. ✅ `digidollar_stress.py` - One line change (connect_nodes_bi → connect_nodes)
2. ✅ `digidollar_basic.py` - Remove import, change method call
3. ✅ `digidollar_protection.py` - Remove import, use standard Python
4. ✅ `digidollar_wallet.py` - Remove import, use standard Python
5. ✅ `digidollar_rpc.py` - Update expected keys
6. 🔍 **THEN** investigate wallet pattern for remaining 8 tests

### For Each Test Fix

1. **Read the test file** - Understand what it's trying to do
2. **Identify the error** - Import? Wallet? Logic?
3. **Apply the fix** - Use patterns above
4. **Run the test** - Verify it works:
   ```bash
   ./test/functional/digidollar_basic.py
   ```
5. **Check for regressions** - Make sure passing tests still pass:
   ```bash
   ./test/functional/digidollar_transfer.py
   ```

---

## Testing Commands

### Run Individual Test
```bash
cd /home/jared/Code/digibyte
./test/functional/digidollar_basic.py
```

### Run All DigiDollar Tests
```bash
for test in test/functional/digidollar_*.py; do
  echo "=== Running $(basename $test) ==="
  "$test" && echo "✅ PASS" || echo "❌ FAIL"
  echo ""
done
```

### Check Test Output
```bash
# See detailed output
./test/functional/digidollar_basic.py --loglevel=debug

# See just pass/fail
./test/functional/digidollar_basic.py 2>&1 | tail -5
```

---

## Troubleshooting

### Import Still Fails After Removing
**Check**: Did you remove from the import statement AND fix all uses?
```bash
grep "connect_nodes\|assert_in" test/functional/digidollar_basic.py
```

### Wallet Error Persists
**Check**: Look at a passing test's wallet pattern:
```bash
grep -A5 "skip_if_no_wallet\|createwallet" test/functional/digidollar_transfer.py
```

### Test Runs But Fails Assertions
**Good!** This means the fix worked. The test is now actually running. The assertion failure may be a real bug or a test expectation issue (report it).

### Test Hangs
**Check**: May be waiting for Dandelion embargo (60s). Look for "Waiting 60 seconds..." in output. This is normal for some tests.

---

## Success Criteria

### You Are DONE When

1. ✅ Test runs without ImportError
2. ✅ Test runs without createwallet error
3. ✅ Test runs without AttributeError
4. ✅ Test completes (pass or meaningful assertion failure)
5. ✅ No regressions in other tests

### Report Template

When you're done, report:

```markdown
## Functional Test Fixes Complete

### Tests Fixed: X/12

#### Import Errors (3 tests)
- ✅ digidollar_basic.py - Removed connect_nodes import, use self.connect_nodes()
- ✅ digidollar_protection.py - Removed assert_in import, use standard Python
- ✅ digidollar_wallet.py - Removed assert_in import, use standard Python

#### Wallet Errors (8 tests)
- Pattern Identified: [describe pattern]
- ✅ digidollar_mint.py - [describe fix]
- ... etc

#### Logic Errors (2 tests)
- ✅ digidollar_rpc.py - Updated stats key expectations
- ✅ digidollar_stress.py - Fixed connect_nodes_bi → connect_nodes

### Verification
- All 18 DigiDollar tests: X passing, Y failing
- No regressions in previously passing tests

### Files Modified
- [list all files changed]
```

---

## Important Reminders

1. 🐛 **Fix REAL bugs** - Tests expose application bugs, fix them when found!
2. 🧪 **Test THOROUGHLY** - After every change (test OR implementation), run regression tests
3. 🛡️ **Protect working code** - Don't break minting, sending, receiving, or redemption
4. 📝 **Document changes** - Explain what you fixed (test bug vs app bug) and how you tested it
5. 🔄 **One fix at a time** - Don't batch changes, test incrementally

---

## Example Fix Workflow

Let's walk through fixing `digidollar_basic.py`:

1. **Identify Error**:
   ```
   ImportError: cannot import name 'connect_nodes' from 'test_framework.util'
   ```

2. **Open File**:
   ```bash
   vim test/functional/digidollar_basic.py
   ```

3. **Remove Bad Import** (line 16):
   ```python
   # Before:
   from test_framework.util import (
       assert_equal,
       connect_nodes,  # ← DELETE THIS LINE
   )

   # After:
   from test_framework.util import (
       assert_equal,
   )
   ```

4. **Fix Method Call** (line 236):
   ```python
   # Before:
   connect_nodes(self.nodes[0], self.nodes[1])

   # After:
   self.connect_nodes(0, 1)
   ```

5. **Test**:
   ```bash
   ./test/functional/digidollar_basic.py
   ```

6. **Verify** - Should now get past import error and actually run the test!

---

**Your mission**: Fix the failing tests using these patterns. Report back when complete with test results.

**Success indicator**: "✅ COMPLETE - 18/18 DigiDollar functional tests passing"

Good luck! 🚀
