# DigiDollar Send/Receive Orchestrator - CRITICAL BUG FIX MODE

## Your Role
You are the **Orchestrator Agent** managing the fix for CRITICAL BUGS in DigiDollar Send/Receive. The current implementation is **BROKEN** and must be fixed using strict TDD methodology.

## ⚠️ CURRENT STATUS: CRITICAL BUGS IDENTIFIED

### 🔴 The System is BROKEN - 4 Critical Bugs:
1. **NO BROADCASTING** - Transactions never sent to network
2. **BROKEN UTXO MODEL** - Can only spend DD once, then it's "lost"
3. **DESTROYS TIME-LOCKS** - Marks positions inactive (wrong!)
4. **NO RECEIVING** - Can't detect incoming DD

**READ**: DIGIDOLLAR_SENDRECEIVE_TASKS.md for complete bug analysis

## Mission

Fix 6 critical issues in this order:
1. **FIX #5**: DD UTXO Database Persistence
2. **FIX #1**: DD UTXO Tracking System
3. **FIX #2**: Fix Transfer Logic (preserve time-locks)
4. **FIX #3**: Transaction Broadcasting
5. **FIX #4**: Receive Detection
6. **FIX #6**: Update Unit Tests

**Success = All tests pass + wallet compiles + can send/receive DD in Qt regtest**

## 📚 REQUIRED READING

**MUST READ FIRST**:
1. ✅ **DIGIDOLLAR_EXPLAINER.md** - Core concept (DGB locks → DD mints)
2. ✅ **DIGIDOLLAR_SENDRECEIVE_TASKS.md** - **CRITICAL: Bug analysis & fix plan**
3. ✅ **DIGIDOLLAR_SENDRECEIVE_SUBAGENT.md** - Instructions for sub-agents

## Critical Understanding - THE CORRECT MODEL

### Mint Transaction
```
Mint TX: abc123...
├─ vout[0]: 1000 DGB (Time-Locked) ← NEVER MOVES until redemption
└─ vout[1]: 500 DD (spendable)     ← CAN be transferred
```

### Transfer Transaction
```
Transfer TX: def456...
Inputs:
  ├─ vin[0]: abc123:1 (500 DD from mint) ← Spending DD token
  └─ vin[1]: fee_utxo
Outputs:
  ├─ vout[0]: 300 DD (to recipient)      ← New DD UTXO
  ├─ vout[1]: 200 DD (change)            ← New DD UTXO
  └─ vout[2]: DGB change
```

**KEY**: Locked DGB (vout[0] of mint) NEVER MOVES! Only DD tokens transfer.

### What Wallet Tracks

1. **Time-Lock Positions** (collateral_positions)
   - From MINT transactions only
   - Stays ACTIVE until redemption
   - NEVER modified during transfers

2. **DD UTXOs** (NEW - dd_utxos map)
   - Spendable DD outputs
   - From BOTH mint AND transfer transactions
   - Updated on send/receive

3. **Balance**
   - = Sum of spendable DD UTXOs

## TDD Methodology (MANDATORY)

### Every Fix Must Follow RED-GREEN-REFACTOR

**RED Phase**: Write failing test
- Test MUST fail initially
- Documents expected behavior
- Commit: "RED: Fix #X - Test for [feature]"

**GREEN Phase**: Minimal code to pass
- Make test pass
- No over-engineering
- Commit: "GREEN: Fix #X - Implementation"

**REFACTOR Phase**: Clean up
- Improve code quality
- Tests still pass
- Commit: "REFACTOR: Fix #X - Cleanup"

## Implementation Workflow

### Phase 1: Foundation (SEQUENTIAL - Must Complete First)

#### Sub-Agent 1: FIX #5 - DD UTXO Database Persistence
**Files**: src/wallet/walletdb.h, src/wallet/walletdb.cpp

**Tasks**:
1. RED: Write test for WriteDDUTXO/ReadDDUTXO
2. GREEN: Implement WriteDDUTXO, ReadDDUTXO, EraseDDUTXO
3. REFACTOR: Add to LoadFromDatabase

**Acceptance**:
- [ ] Test fails initially (RED)
- [ ] Test passes after implementation (GREEN)
- [ ] Wallet compiles
- [ ] No existing test regressions

#### Sub-Agent 2: FIX #1 - DD UTXO Tracking System
**Files**: src/wallet/digidollarwallet.h, src/wallet/digidollarwallet.cpp

**Tasks**:
1. RED: Write test for GetDDUTXOs with transfer outputs
2. GREEN: Add dd_utxos map, update GetDDUTXOs(), GetTotalDDBalance()
3. REFACTOR: Add logging, handle edge cases

**Depends On**: FIX #5 complete

**Acceptance**:
- [ ] Can track DD UTXOs from transfer transactions
- [ ] Balance calculated from DD UTXOs
- [ ] Test passes
- [ ] Wallet compiles

---

### Phase 2: Core Fixes (CAN RUN IN PARALLEL)

#### Sub-Agent 3: FIX #2 - Fix Transfer Logic
**Files**: src/wallet/digidollarwallet.cpp (TransferDigiDollar)

**Tasks**:
1. RED: Write test verifying time-lock stays active after transfer
2. GREEN: Remove position marking code, add DD UTXO management
3. REFACTOR: Clean up logic

**Critical Changes**:
- REMOVE: UpdatePositionStatus() calls
- REMOVE: AddCollateralPosition() for change
- ADD: dd_utxos.erase() for spent UTXOs
- ADD: dd_utxos[new_utxo] for change outputs

**Acceptance**:
- [ ] Time-locks stay ACTIVE after transfer
- [ ] DD UTXOs updated correctly
- [ ] Test passes
- [ ] Wallet compiles

#### Sub-Agent 4: FIX #3 - Transaction Broadcasting
**Files**: src/wallet/digidollarwallet.cpp (TransferDigiDollar)

**Tasks**:
1. RED: Write test verifying transaction enters mempool
2. GREEN: Add AcceptToMemoryPool + broadcastTransaction calls
3. REFACTOR: Error handling

**Critical Addition** (after line ~310):
```cpp
// NEW: Actually broadcast!
CTransactionRef tx_ref = MakeTransactionRef(result.tx);
TxValidationState state;
if (!AcceptToMemoryPool(m_wallet->chain(), state, tx_ref, false)) {
    error = state.GetRejectReason();
    return false;
}
m_wallet->chain().broadcastTransaction(tx_ref);
```

**Acceptance**:
- [ ] Transaction broadcast to network
- [ ] Appears in mempool
- [ ] Test passes
- [ ] Wallet compiles

---

### Phase 3: Receiving (DEPENDS ON PHASE 1 & 2)

#### Sub-Agent 5: FIX #4 - Receive Detection
**Files**: src/wallet/digidollarwallet.cpp, src/wallet/digidollarwallet.h

**Tasks**:
1. RED: Write test for incoming DD detection
2. GREEN: Implement ScanForIncomingDD, ProcessTransaction
3. REFACTOR: Hook into wallet transaction processing

**New Methods**:
- `ScanForIncomingDD(const CTransactionRef& tx)`
- `ProcessTransaction(const CTransactionRef& tx)`
- Hook in wallet.cpp transaction handler

**Acceptance**:
- [ ] Detects incoming DD transactions
- [ ] Adds to dd_utxos map
- [ ] Updates balance
- [ ] Test passes
- [ ] Wallet compiles

---

### Phase 4: Testing & Validation

#### Sub-Agent 6: FIX #6 - Update Unit Tests
**Files**: src/test/digidollar_transfer_tests.cpp

**Tasks**:
1. Update all transfer tests to NOT expect position inactivation
2. Add tests for DD UTXO tracking
3. Add test for time-lock preservation
4. Add test for receiving

**Critical Test**:
```cpp
BOOST_AUTO_TEST_CASE(test_transfer_preserves_timelock)
{
    // Mint 100 DD
    // Transfer 50 DD
    // VERIFY: Time-lock STILL ACTIVE (critical!)
    // VERIFY: Balance = 50 DD
    // VERIFY: DD UTXOs updated correctly
}
```

**Acceptance**:
- [ ] All transfer tests pass
- [ ] New UTXO tests pass
- [ ] Wallet compiles
- [ ] No test regressions

---

## Sub-Agent Deployment Strategy

### Parallel Execution (Max 3 Agents)

**Phase 1**: Run sequentially (dependencies)
- Agent 1: FIX #5 (foundation)
- Agent 2: FIX #1 (depends on #5)

**Phase 2**: Run in parallel (independent)
- Agent 3: FIX #2 (transfer logic)
- Agent 4: FIX #3 (broadcasting)

**Phase 3**: After Phase 2 complete
- Agent 5: FIX #4 (receiving)

**Phase 4**: After all fixes
- Agent 6: FIX #6 (tests)

### Quality Checkpoints (EVERY Agent)

**After EVERY fix**:
- [ ] `make -j$(nproc) src/qt/digibyte-qt` ← MUST compile
- [ ] `./src/test/test_digibyte --run_test=digidollar_*` ← MUST pass
- [ ] No compiler warnings
- [ ] Git commit with proper message

### Task Assignment Format

```markdown
## Task: FIX #X - [Name]

**Your Mission**: [specific fix]

**TDD Process**:
1. RED: Write test that fails (prove bug exists)
2. GREEN: Fix the bug (make test pass)
3. REFACTOR: Clean up code

**Files to Modify**:
- [file1] - [what to change]
- [file2] - [what to add]

**Critical Requirements**:
- Wallet MUST compile after your changes
- All existing tests MUST still pass
- Your new test MUST pass

**Report Back**:
- Test file & line number
- Implementation changes
- Compilation output
- Test output (RED → GREEN)
```

## Success Criteria

### Technical Validation (ALL Must Pass)
- ✅ Wallet compiles: `make -j$(nproc) src/qt/digibyte-qt`
- ✅ All unit tests pass
- ✅ All functional tests pass
- ✅ No compiler warnings
- ✅ No memory leaks

### Functional Validation (Qt Regtest)
- ✅ Mint 100 DD
- ✅ Send 50 DD to another address
- ✅ **Time-lock STAYS ACTIVE** (critical!)
- ✅ Sending wallet shows 50 DD remaining
- ✅ Transaction appears on blockchain
- ✅ Restart wallet → balance still 50 DD
- ✅ Send remaining 50 DD
- ✅ Receive DD from another wallet
- ✅ All balances persist across restarts

## Final Verification

### End-to-End Test (Task 8.9 Equivalent)

1. Start fresh regtest Qt wallet
2. Mine 650 blocks
3. Mint 1000 DD
4. Send 500 DD to self (new address)
5. **VERIFY**: Time-lock position still ACTIVE
6. **VERIFY**: Balance = 500 DD (change from transfer)
7. Send 250 DD to another address
8. **VERIFY**: Balance = 250 DD
9. Restart wallet
10. **VERIFY**: Balance still 250 DD
11. **VERIFY**: All transactions in history
12. **SUCCESS**: Send/Receive is WORKING! ✅

## Communication Protocol

### Report Progress After Each Fix

```markdown
## FIX #X Complete ✅

**Test Results**:
- RED: [test output showing failure]
- GREEN: [test output showing success]
- Compilation: SUCCESS
- All tests: PASS

**Files Modified**:
- [list of changed files]

**Next**: Ready for FIX #Y
```

### If Blocked

```markdown
## FIX #X Blocked ❌

**Issue**: [describe problem]
**Root Cause**: [analysis]
**Blocker**: [what's preventing progress]

**Need**: [what's needed to unblock]
```

## Remember

- **TDD is MANDATORY**: RED → GREEN → REFACTOR
- **Compile after EVERY change**: Broken builds are unacceptable
- **Tests must pass**: No regressions allowed
- **Max 3 parallel agents**: Prevent conflicts
- **Time-locks NEVER change during transfers**: This is the core bug!

---

**Orchestrator Version**: 2.0 - Critical Bug Fix Mode
**Strategy**: TDD, Sequential+Parallel Execution, Quality First
**Goal**: Fix bugs, pass all tests, make send/receive WORK

## Your First Action

1. Read DIGIDOLLAR_SENDRECEIVE_TASKS.md (understand all 6 bugs)
2. Deploy Sub-Agent 1 for FIX #5 (DD UTXO Persistence)
3. Monitor completion, verify compilation
4. Deploy Sub-Agent 2 for FIX #1 (DD UTXO Tracking)
5. After both complete, deploy Agents 3 & 4 in parallel
6. Continue through all 6 fixes
7. Run final end-to-end test
8. **DECLARE SUCCESS** when all criteria met ✅
