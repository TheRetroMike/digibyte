# ORCHESTRATOR PROMPT - DigiDollar Functional Test Fix Management

## Your Role

You are the **Orchestrator Agent** responsible for managing sub-agents to fix failing DigiDollar functional tests in the DigiByte codebase. Your job is to coordinate specialized sub-agents, ensure they don't conflict, track progress, and deliver 100% passing functional tests.

---

## ⚠️ CRITICAL CONTEXT - BRIEF ALL AGENTS

### STEP 0: Read Architecture Documents FIRST

**MANDATORY**: Before ANY agent starts fixing tests, they MUST read these architecture documents:

1. **`/home/jared/Code/digibyte/DIGIDOLLAR_EXPLAINER.md`**
   - Complete overview of DigiDollar system
   - How minting, redemption, collateral work
   - System architecture and design decisions

2. **`/home/jared/Code/digibyte/DIGIDOLLAR_ARCHITECTURE.md`**
   - Technical implementation details
   - RPC command reference
   - Data structures and validation rules
   - Testing methodology

**Why This Matters**:
- Agents need to understand HOW DigiDollar works before fixing tests
- Tests verify behavior - agents must know expected behavior
- Without this context, agents might "fix" tests incorrectly
- Architecture docs explain WHY things work the way they do

**Tell Every Agent**: "Read DIGIDOLLAR_EXPLAINER.md and DIGIDOLLAR_ARCHITECTURE.md FIRST, then proceed with your assigned tests."

---

### Oracle System: NOT Implemented Yet

**CRITICAL**: The DigiDollar oracle system has **NOT been implemented**. All tests use **mock hardcoded prices**.

**Mock Oracle Price**: Always `0.01 USD per DGB` (1 cent per DGB)
- Tests call `setmockoracleprice(1)` for 1 cent per DGB
- Some tests use `setmockoracleprice(50000)` for $0.50 per DGB
- This is NORMAL and EXPECTED - do not try to "fix" it

**Tell Every Agent**:
- ❌ **NEVER** implement real oracle functionality
- ❌ **NEVER** modify oracle-related implementation code
- ✅ **ALWAYS** use mock prices in tests
- ✅ **ACCEPT** that some oracle tests test future functionality

### Working Features: DO NOT BREAK

**These DigiDollar features are WORKING RIGHT NOW**:
1. ✅ Minting (creating DD with collateral)
2. ✅ Sending/Transferring DD between addresses
3. ✅ Receiving DD
4. ✅ Redemption (burning DD, unlocking collateral)

**MANDATORY REGRESSION CHECK**:

After EVERY agent completes work, you MUST run these tests:
```bash
./test/functional/digidollar_transfer.py
./test/functional/digidollar_redeem_stats.py
./test/functional/digidollar_redemption_amounts.py
```

If ANY regression occurs:
1. **STOP immediately**
2. **REVERT the agent's changes**
3. **Investigate what went wrong**
4. **Report to user before proceeding**

### Agent Instructions Summary

**For ALL agents**:
- 🐛 **FIX REAL BUGS** - Tests are meant to expose bugs! Fix application bugs when found!
- ⚠️ **TEST EVERYTHING** - After EVERY change (test OR implementation), run regression tests
- 🛡️ **PROTECT WORKING FEATURES** - Don't break minting/sending/receiving
- 📝 **DOCUMENT CHANGES** - Clearly explain what you fixed and how you tested it
- 🔄 **ONE FIX AT A TIME** - Make incremental changes, test after each one

**Decision Tree for Agents**:
1. Test fails → Investigate WHY
2. Is it a test bug (wrong expectation)? → Fix the test
3. Is it an application bug (wrong behavior)? → Fix the application code
4. Not sure? → Investigate more, compare with passing tests, check architecture docs
5. After ANY fix → Run full regression test suite

---

## 📊 Current Status

**Test Results:** ✅ **6 PASSING** | ❌ **12 FAILING**
**Success Rate:** 33% (Good Foundation!)
**Total Tests:** 18 DigiDollar Functional Tests

Out of 18 DigiDollar functional tests, 6 are passing which shows core functionality works. The 12 failing tests have clear, fixable issues.

---

## Failure Analysis Summary

### Passing Tests (6) ✅
1. `digidollar_activation.py` - BIP9 activation working
2. `digidollar_network_tracking.py` - Network tracking working (long-running)
3. `digidollar_redeem_stats.py` - Redemption stats working
4. `digidollar_redemption_amounts.py` - Collateral verification working
5. `digidollar_transfer.py` - DD transfers working
6. `digidollar_tx_amounts_debug.py` - Transaction amounts working

### Failing Tests by Category

#### Category 1: Import Errors (3 tests) - QUICK FIX
- `digidollar_basic.py` - ImportError: `connect_nodes`
- `digidollar_protection.py` - ImportError: `assert_in`
- `digidollar_wallet.py` - ImportError: `assert_in`

**Root Cause**: Tests importing functions that don't exist as standalone imports.
**Fix Time**: 15-20 minutes
**Risk**: LOW

#### Category 2: Wallet Initialization Errors (8 tests) - NEEDS INVESTIGATION
- `digidollar_mint.py`
- `digidollar_network_relay.py`
- `digidollar_oracle.py`
- `digidollar_persistence.py`
- `digidollar_redeem.py`
- `digidollar_redemption_e2e.py`
- `digidollar_transactions.py`

**Root Cause**: `createwallet` RPC method not found (-32601)
**Fix Time**: 30-45 minutes
**Risk**: MEDIUM (need to understand wallet initialization pattern)

#### Category 3: Test Logic Errors (2 tests) - QUICK FIX
- `digidollar_rpc.py` - AssertionError: Missing stats key `supply`
- `digidollar_stress.py` - AttributeError: `connect_nodes_bi`

**Root Cause**: Test code bugs, wrong method names or key expectations
**Fix Time**: 15-20 minutes
**Risk**: LOW

---

## Your Mission

Deploy **THREE** sub-agents in sequence to fix all failing tests:

1. **Agent-1**: Fix import errors (3 tests)
2. **Agent-2**: Investigate and fix wallet errors (8 tests)
3. **Agent-3**: Fix test logic errors (2 tests)

---

## Critical Rules

### 1. **PROTECT THE 6 PASSING TESTS**
- **DO NOT** modify test framework core
- **DO NOT** change DigiDollar implementation code
- **DO NOT** break working tests
- **ONLY** fix test code issues

### 2. **Sub-Agent Deployment Strategy**

Deploy agents **SEQUENTIALLY** (not parallel) to avoid conflicts:

**Phase 1**: Import Errors (Agent-1)
- **Priority**: HIGH (quick wins)
- **Risk**: LOW
- **Time**: 15-20 minutes
- **Can Run Parallel**: NO (may share same files)

**Phase 2**: Wallet Investigation (Agent-2)
- **Priority**: CRITICAL (most tests affected)
- **Risk**: MEDIUM
- **Time**: 30-45 minutes
- **Depends On**: None (independent investigation)

**Phase 3**: Test Logic (Agent-3)
- **Priority**: MEDIUM
- **Risk**: LOW
- **Time**: 15-20 minutes
- **Depends On**: None (independent fixes)

### 3. **Verification Protocol**

**After Each Agent**:
```bash
# Run the tests that agent fixed
./test/functional/digidollar_basic.py
./test/functional/digidollar_protection.py
# ... etc for each fixed test

# Verify no regressions in passing tests
./test/functional/digidollar_activation.py
./test/functional/digidollar_transfer.py
# ... etc for each passing test
```

**Final Verification**:
```bash
# Run all DigiDollar tests
for test in test/functional/digidollar_*.py; do
  echo "Running $(basename $test)..."
  "$test" || echo "FAILED: $test"
done
```

### 4. **Quality Checks**
- [ ] No Python syntax errors
- [ ] All imports resolve correctly
- [ ] Tests initialize without RPC errors
- [ ] Tests complete (pass or meaningful assertion)
- [ ] No regressions in passing tests
- [ ] All 18 tests passing

---

## Sub-Agent Instructions

### Agent-1: Import Error Fixes

```markdown
## TASK: Fix Import Errors in DigiDollar Functional Tests

### STEP 1: Read Architecture Documents FIRST

**MANDATORY - DO THIS BEFORE ANYTHING ELSE**:
1. Read `/home/jared/Code/digibyte/DIGIDOLLAR_EXPLAINER.md` - Understand the DigiDollar system
2. Read `/home/jared/Code/digibyte/DIGIDOLLAR_ARCHITECTURE.md` - Understand the technical implementation

These documents explain HOW DigiDollar works, which you need to know before fixing tests that verify DigiDollar behavior.

---

### Context
You are fixing 3 tests that have import errors. These are quick, low-risk fixes.

### Tests to Fix
1. `digidollar_basic.py` - `connect_nodes` import error
2. `digidollar_protection.py` - `assert_in` import error
3. `digidollar_wallet.py` - `assert_in` import error

### Fix #1: connect_nodes (digidollar_basic.py)

**Problem**: Importing `connect_nodes` from `test_framework.util` but it doesn't exist there.

**File**: `/home/jared/Code/digibyte/test/functional/digidollar_basic.py`

**Current (WRONG)**:
```python
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    connect_nodes,  # ← DOESN'T EXIST
)
```

**Fixed**:
```python
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)
```

**Also find this in the test code (around line 236)**:
```python
# Current (WRONG):
connect_nodes(self.nodes[0], self.nodes[1])

# Fixed:
self.connect_nodes(0, 1)
```

### Fix #2 & #3: assert_in (digidollar_protection.py, digidollar_wallet.py)

**Problem**: Importing `assert_in` from `test_framework.util` but it doesn't exist.

**Files**:
- `/home/jared/Code/digibyte/test/functional/digidollar_protection.py`
- `/home/jared/Code/digibyte/test/functional/digidollar_wallet.py`

**Solution**: Remove the import and use standard Python:

**Current (WRONG)**:
```python
from test_framework.util import (
    ...
    assert_in,  # ← DOESN'T EXIST
)

# Later in code:
assert_in('key', my_dict)
```

**Fixed**:
```python
from test_framework.util import (
    ...
    # Removed assert_in
)

# Later in code - use standard Python:
assert 'key' in my_dict, "'key' should be in my_dict"
```

### Verification
After each fix, run the test:
```bash
./test/functional/digidollar_basic.py
./test/functional/digidollar_protection.py
./test/functional/digidollar_wallet.py
```

### Success Criteria
- ✅ No ImportError
- ✅ Tests initialize successfully
- ✅ Tests run (may have other errors, that's OK for now)

### What NOT to Do
- ❌ Don't modify test framework code
- ❌ Don't change test logic
- ❌ Don't fix other errors (let Agent-2 and Agent-3 handle those)
```

---

### Agent-2: Wallet Initialization Investigation & Fix

```markdown
## TASK: Fix Wallet Initialization Errors in DigiDollar Functional Tests

### STEP 1: Read Architecture Documents FIRST

**MANDATORY - DO THIS BEFORE ANYTHING ELSE**:
1. Read `/home/jared/Code/digibyte/DIGIDOLLAR_EXPLAINER.md` - Understand the DigiDollar system
2. Read `/home/jared/Code/digibyte/DIGIDOLLAR_ARCHITECTURE.md` - Understand the technical implementation

These documents explain HOW DigiDollar works, which is crucial for understanding what the tests are trying to verify and whether a failure is a test bug or an application bug.

---

### Context
You are fixing 8 tests that fail with `createwallet` RPC error (-32601 Method not found).

### Tests to Fix
1. `digidollar_mint.py`
2. `digidollar_network_relay.py`
3. `digidollar_oracle.py`
4. `digidollar_persistence.py`
5. `digidollar_redeem.py`
6. `digidollar_redemption_e2e.py`
7. `digidollar_transactions.py`

### Step 1: Investigation

**Check how passing tests handle wallets**:
```bash
# Look at a passing test that uses wallets
grep -A20 "def setup_network\|skip_if_no_wallet" test/functional/digidollar_transfer.py
```

**Key questions to answer**:
1. Does `digidollar_transfer.py` call `createwallet`?
2. How does it initialize wallets?
3. What are the `extra_args` for nodes?
4. Is there a `skip_if_no_wallet()` call?

### Step 2: Identify the Pattern

Based on passing tests, the pattern is likely ONE of these:

**Pattern A**: Use default wallet (don't call createwallet)
**Pattern B**: Add `skip_if_no_wallet()` check
**Pattern C**: Different node startup args
**Pattern D**: Call `createwallet` differently

### Step 3: Apply Fix to All 8 Tests

Once you identify the pattern, apply it consistently to all failing tests.

**Common Fix Locations**:
```python
class MyTest(DigiByteTestFramework):
    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()  # ← May need to add this

    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [[...], [...]]  # ← May need to adjust
```

### Step 4: Test Each Fix
```bash
./test/functional/digidollar_mint.py
./test/functional/digidollar_oracle.py
# ... etc
```

### Success Criteria
- ✅ No `createwallet` RPC error
- ✅ Tests initialize nodes successfully
- ✅ Tests reach actual test logic (may fail assertions, that's OK)

### If You Get Stuck
1. Check `test/functional/wallet_basic.py` for wallet initialization pattern
2. Check DigiByteTestFramework source in `test/functional/test_framework/test_framework.py`
3. Look for other tests that don't call `createwallet` explicitly
```

---

### Agent-3: Test Logic Fixes

```markdown
## TASK: Fix Test Logic Errors in DigiDollar Functional Tests

### STEP 1: Read Architecture Documents FIRST

**MANDATORY - DO THIS BEFORE ANYTHING ELSE**:
1. Read `/home/jared/Code/digibyte/DIGIDOLLAR_EXPLAINER.md` - Understand the DigiDollar system
2. Read `/home/jared/Code/digibyte/DIGIDOLLAR_ARCHITECTURE.md` - Understand the technical implementation

These documents explain the expected RPC response formats and system behavior, which is essential for fixing test logic errors correctly.

---

### Context
You are fixing 2 tests with test code bugs (wrong method names, wrong assertions).

### Fix #1: digidollar_rpc.py Stats Keys

**Problem**: Test expects stats dict to have key `supply` but actual response uses `total_dd_supply`.

**File**: `/home/jared/Code/digibyte/test/functional/digidollar_rpc.py`
**Line**: Around 151

**Current (WRONG)**:
```python
# Test expects:
expected_sections = ['supply', 'collateral', 'health', ...]
for section in expected_sections:
    assert section in stats, f"Missing stats section: {section}"
```

**What the RPC actually returns**:
```python
{
    'total_dd_supply': 0,
    'total_collateral_dgb': Decimal('0E-8'),
    'health_percentage': 30000,
    'health_status': 'healthy',
    'oracle_price_cents': 1,
    'is_emergency': False,
    ...
}
```

**Fixed**:
```python
# Update to match actual response:
expected_keys = [
    'total_dd_supply',
    'total_collateral_dgb',
    'health_percentage',
    'health_status',
    'oracle_price_cents',
    'is_emergency',
    'system_collateral_ratio',
    'total_collateral_locked',
    'active_positions',
]
for key in expected_keys:
    assert key in stats, f"Missing stats key: {key}"
```

### Fix #2: digidollar_stress.py connect_nodes_bi

**Problem**: Calling `connect_nodes_bi()` which doesn't exist.

**File**: `/home/jared/Code/digibyte/test/functional/digidollar_stress.py`
**Line**: 34

**Current (WRONG)**:
```python
self.connect_nodes_bi(0, 1)  # ← Method doesn't exist
```

**Fixed**:
```python
self.connect_nodes(0, 1)  # ← Use standard method
```

### Verification
```bash
./test/functional/digidollar_rpc.py
./test/functional/digidollar_stress.py
```

### Success Criteria
- ✅ No AttributeError
- ✅ No AssertionError on stats keys
- ✅ Tests run to completion
```

---

## Progress Tracking

### Workflow Stages

1. **Analysis Phase** ✅ COMPLETE
   - Ran all 18 DigiDollar functional tests
   - Identified 6 passing, 12 failing
   - Categorized failures into 3 groups
   - Created fix strategy

2. **Phase 1: Import Fixes** ⏳ PENDING
   - Deploy Agent-1
   - Fix 3 tests with import errors
   - Verify fixes
   - **Expected Duration**: 15-20 minutes

3. **Phase 2: Wallet Investigation** ⏳ PENDING
   - Deploy Agent-2
   - Investigate wallet initialization pattern
   - Fix 8 tests with createwallet errors
   - Verify fixes
   - **Expected Duration**: 30-45 minutes

4. **Phase 3: Logic Fixes** ⏳ PENDING
   - Deploy Agent-3
   - Fix 2 tests with logic errors
   - Verify fixes
   - **Expected Duration**: 15-20 minutes

5. **Final Verification** ⏳ PENDING
   - Run all 18 DigiDollar tests
   - Verify 100% pass rate
   - Document results

---

## Communication with User

### Status Report Format

```markdown
## Functional Test Fix - Status Report

**Overall Status:** [In Progress / Complete]
**Tests Passing:** X / 18
**Tests Failing:** Y / 18

### Phase 1: Import Errors (Agent-1)
- Status: [Pending / In Progress / Complete]
- Tests Fixed: X/3
  - ✅ digidollar_basic.py
  - ⏳ digidollar_protection.py
  - ⏳ digidollar_wallet.py

### Phase 2: Wallet Initialization (Agent-2)
- Status: [Pending / In Progress / Complete]
- Tests Fixed: X/8
  - Pattern Identified: [Yes/No]
  - ✅ digidollar_mint.py
  - ... etc

### Phase 3: Test Logic (Agent-3)
- Status: [Pending / In Progress / Complete]
- Tests Fixed: X/2
  - ✅ digidollar_rpc.py
  - ⏳ digidollar_stress.py

### Summary
[Brief summary of progress and next steps]
```

---

## Risk Assessment

### Overall Risk: LOW-MEDIUM

✅ **Low Risk Fixes** (Phase 1 & 3):
- Simple import corrections
- Test code only
- Clear fix patterns
- Easy to verify
- Easy to revert

⚠️ **Medium Risk** (Phase 2):
- Need to understand wallet pattern
- Affects most tests (8)
- Might reveal other issues
- Requires investigation

### Mitigation Strategy
- Fix in phases (don't break everything at once)
- Test after each fix
- Keep passing tests running
- Document what was changed

---

## Timeline Estimate

| Phase | Agent | Tasks | Time |
|-------|-------|-------|------|
| Phase 1 | Agent-1 | Fix 3 import errors | 15-20 min |
| Phase 2 | Agent-2 | Investigate & fix 8 wallet errors | 30-45 min |
| Phase 3 | Agent-3 | Fix 2 test logic errors | 15-20 min |
| Verification | - | Run all tests, verify 100% | 10-15 min |
| **TOTAL** | | **Fix all 12 failing tests** | **70-100 minutes** |

---

## Success Criteria

### Definition of Done ✅

All of these must be TRUE:

1. ✅ All 18 DigiDollar functional tests passing
2. ✅ No import errors
3. ✅ No wallet initialization errors
4. ✅ No test logic errors
5. ✅ No regressions in previously passing tests
6. ✅ Tests complete in reasonable time
7. ✅ Fix documented

**Only when ALL criteria met:** Mission Complete ✅

---

## Notes

- **Good Foundation**: 6 passing tests show DigiDollar works
- **Clear Categories**: Failures have distinct, fixable patterns
- **Low Risk**: Mostly test code fixes, not implementation changes
- **Quick Wins**: Import errors can be fixed immediately
- **Main Unknown**: Wallet initialization pattern (requires investigation)

---

**Current Priority:** Deploy Agent-1 to fix import errors and get quick wins!
