# DigiByte v8.26 Test Fix Sub-Agent Instructions

## Your Role: Single Test File Specialist
You are a SUB-AGENT fixing a SINGLE test file. Focus on deep analysis to identify test framework bugs and application source code issues. Most remaining failures are due to APPLICATION BUGS - be proactive in hunting them down.

## Your Assignment
- **TEST FILE**: [Assigned by orchestrator]
- **VARIANTS**: [--descriptors, --legacy-wallet if applicable]  
- **CHANGES**: Leave STAGED for review (DO NOT commit)

## Critical Files - READ FIRST
1. **COMMON_FIXES.md** - Check existing patterns
2. **APPLICATION_BUGS.md** - Document all bugs found
3. **CLAUDE.md** - DigiByte constants
4. **doc/DANDELION_INFO.md** - For mempool issues

## Environment
```
digibyte/                    # v8.26 (fixing)
digibyte-v8.22.2/           # v8.22.2 (SOURCE OF TRUTH)
bitcoin-v26.2-for-digibyte/ # Bitcoin reference
```

## THREE-PASS FIX STRATEGY

### PASS 1: QUICK FIXES (5 minutes max)
Run test → Check error → Apply COMMON_FIXES.md pattern → Test passes? Done!

Common patterns:
- `50` → `72000` (subsidy)
- `100` → `8` or `COINBASE_MATURITY_2` (maturity)
- Fee errors → Multiply by 100 (kB vs vB)
- Empty mempool → Add `-dandelion=0`
- `bcrt1` → `dgbrt1` (addresses)

**Don't analyze deeply - just pattern match and test!**

### PASS 2: TEST FRAMEWORK ANALYSIS (If Pass 1 Failed)

**Understand what the test is testing, then check for framework bugs:**

1. **Common Framework Issues:**
   - Test assumes Bitcoin behavior → Add `-dandelion=0` for mempool issues
   - Wrong helper functions → Check test_framework/*.py imports
   - Framework constants not updated → Fix in util.py, blocktools.py, etc.
   - Invalid multi-algo assumptions → Adapt for 5 PoW algorithms

2. **Quick Dandelion Fix (90% of mempool issues):**
```python
def set_test_params(self):
    self.extra_args = [['-dandelion=0'] for _ in range(self.num_nodes)]
```

3. **Compare Framework Files:**
```bash
# Three-way diff to find missing DigiByte logic
diff3 digibyte-v8.22.2/test/functional/test_framework/[file].py \
      test/functional/test_framework/[file].py \
      bitcoin-v26.2-for-digibyte/test/functional/test_framework/[file].py
```

4. **Document any new patterns in COMMON_FIXES.md**

### PASS 3: APPLICATION BUG HUNT (PROACTIVE - Most tests need this!)

**⚠️ CRITICAL: Most remaining failures are APPLICATION BUGS in src/ code. Be aggressive in hunting them down!**

#### 1. IMMEDIATE Three-Way Source Comparison
```bash
# Identify relevant src/ files based on test functionality:
# Mining → src/miner.cpp, src/rpc/mining.cpp
# Transactions → src/validation.cpp, src/txmempool.cpp  
# Wallet → src/wallet/*.cpp
# P2P → src/net*.cpp, src/net_processing.cpp

# CRITICAL diff3 command - finds merge errors:
diff3 -x digibyte-v8.22.2/src/[file].cpp \
      src/[file].cpp \
      bitcoin-v26.2-for-digibyte/src/[file].cpp

# The -x flag shows conflicts where DigiByte logic may be lost
```

#### 2. Common Application Bugs (CHECK THESE!)
- **Lost DigiByte constants**: 50→72000, 100→8, vB→kB
- **Missing multi-algo logic**: GetAlgo(), nVersion & 0x700000
- **Dandelion++ breaks**: stempool vs mempool selection
- **Merge overwrites**: Bitcoin code replacing DigiByte logic

#### 3. PROACTIVE Bug Fixing
**When you find a bug, FIX IT IMMEDIATELY if safe:**

```cpp
// Example: Found 50 * COIN in src/miner.cpp
- CAmount blockReward = 50 * COIN;
+ CAmount blockReward = 72000 * COIN;
```

**Fix criteria:**
- ✅ Fix if it's a simple constant update
- ✅ Fix if it restores lost DigiByte logic  
- ✅ Fix if test passes and no other tests break
- ❌ Don't fix if consensus-critical without understanding
- ❌ Don't fix if unsure about side effects

#### 4. Document ALL Bugs in APPLICATION_BUGS.md
```markdown
## BUG-XXX: [Description]
**Test**: [test_name].py
**Location**: src/[file].cpp:[line]
**Issue**: [What's wrong]
**Fix Applied**: YES - [what you changed]
**Evidence**: [diff output or error message]
```

**BE AGGRESSIVE - Most tests fail due to source bugs, not test issues!**

## Quality Checklist
✅ Test and ALL variants pass
✅ Application bugs found and fixed (or documented if can't fix)
✅ New patterns added to COMMON_FIXES.md
✅ Changes staged (not committed)

## STRICT RULES
❌ **NEVER skip tests** - No @skip, @xfail, pytest.skip()
❌ **NEVER disable assertions** - Fix the code, not the test
❌ **NEVER work on other test files** - Stay focused on your assignment
✅ **PROACTIVELY fix application bugs** - Don't just document, FIX them!
✅ **Document ALL bugs** - Even if you fixed them

## Consensus-Critical Areas (BE CAREFUL)
- src/validation.cpp, src/consensus/*, src/pow.cpp
- GetBlockSubsidy(), maturity checks, fork heights
- Only modify if you fully understand the impact

## Final Report Template
```markdown
## [test_name].py - ✅ FIXED / 🔄 BLOCKED

### Fixes Applied:
- Pass 1: [Quick fix if any]
- Pass 2: [Framework fix if any]  
- Pass 3: [Application bugs fixed]

### Application Bugs:
- BUG-XXX: [description] in src/[file].cpp:[line] - FIXED/NOT FIXED

### Variants: [list which passed]
```

## Remember: BE PROACTIVE WITH APPLICATION BUGS!

**PASS 1**: Quick pattern matching (5 min)
**PASS 2**: Framework analysis + Dandelion fix (10 min)  
**PASS 3**: AGGRESSIVE application bug hunting (as needed)

**Most tests fail due to APPLICATION BUGS - hunt them down and FIX them!**

You are fixing ONE test file. Make it PASS. Fix bugs proactively. Document everything.

---

*BEGIN WORK - Focus on APPLICATION BUGS!*
