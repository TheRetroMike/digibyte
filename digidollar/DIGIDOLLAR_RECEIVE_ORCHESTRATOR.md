# DigiDollar Receive Process Orchestrator - Implementation Guide

## Your Role
You are the **Orchestrator Agent** managing the complete implementation of DigiDollar receiving functionality. This builds upon the recently completed sending implementation and completes the full transaction flow.

## 🎯 MISSION: Complete DigiDollar Receive Implementation

### Current Status
- ✅ **Sending Works**: DigiDollars can be sent and broadcast to network
- ✅ **DD UTXO Tracking**: Wallet tracks spendable DD outputs
- ✅ **Time-lock Preservation**: Collateral positions remain active during transfers
- ❌ **Receiving Broken**: Incoming DD transactions not detected or processed

**Goal**: Implement complete receive detection, wallet integration, balance updates, and GUI display.

## 📚 REQUIRED READING

**MUST READ FIRST**:
1. ✅ **DIGIDOLLAR_EXPLAINER.md** - Core DigiDollar concept
2. ✅ **DIGIDOLLAR_RECEIVE_TASKS.md** - **CRITICAL: Task breakdown & implementation plan**
3. ✅ **DIGIDOLLAR_RECEIVE_SUBAGENT.md** - Instructions for sub-agents
4. ✅ **test/functional/digidollar_transfer.py** - **THE TEST THAT PROVES THE BUG**

## 🔴 THE FAILING TEST - Our RED Phase

### We Already Have a Perfect Failing Test!

**File**: `test/functional/digidollar_transfer.py`

**Current Status**:
```python
# Lines 126-148: ✅ ALL WORK PERFECTLY
def test_simple_transfers(self):
    # Get initial balances
    sender_initial = self.nodes[0].getdigidollarbalance()  # ✅ Works
    receiver_initial = self.nodes[3].getdigidollarbalance()  # ✅ Works (0)

    # Get receiver address
    receiver_address = self.nodes[3].getdigidollaraddress()  # ✅ Works

    # Perform transfer
    transfer_amount_cents = 1000  # $10.00
    result = self.nodes[0].senddigidollar(receiver_address, transfer_amount_cents)  # ✅ Works
    assert 'txid' in result  # ✅ Works

    # Mine block to confirm
    self.nodes[0].generate(1)  # ✅ Works
    self.sync_all()  # ✅ Works

# Lines 151-158: ❌ FAILS HERE - RECEIVING BROKEN
    # Verify balances
    sender_final = self.nodes[0].getdigidollarbalance()  # ✅ Correct (4000)
    receiver_final = self.nodes[3].getdigidollarbalance()  # ❌ STILL 0!

    expected_sender = sender_initial - transfer_amount  # 4000
    expected_receiver = receiver_initial + transfer_amount  # 1000

    assert_equal(sender_final, expected_sender)  # ✅ Passes
    assert_equal(receiver_final, expected_receiver)  # ❌ FAILS! (0 != 1000)
```

**What This Proves**:
- Sending: **WORKS** ✅
- Broadcasting: **WORKS** ✅
- Sender balance update: **WORKS** ✅
- Receiver balance update: **BROKEN** ❌

**This is our RED phase** - The test already exists and fails!

## Critical Understanding - DigiDollar Receive Flow

### What Happens When DD is Sent

**Sender's Wallet** (Node 0 - WORKING):
```
1. Builds transfer transaction ✅
2. Spends DD UTXO inputs ✅
3. Creates DD outputs to recipient address ✅
4. Broadcasts to network ✅
5. Updates local DD UTXOs (removes spent, adds change) ✅
```

**Network** (WORKING):
```
1. Receives transaction in mempool ✅
2. Validates transaction (consensus rules) ✅
3. Relays to all nodes ✅
4. Includes in next block ✅
```

**Receiver's Wallet** (Node 3 - BROKEN):
```
1. ❌ Should detect incoming transaction
2. ❌ Should extract DD outputs to our addresses
3. ❌ Should add to dd_utxos map
4. ❌ Should update balance
5. ❌ Should add to transaction history
6. ❌ Should notify GUI
```

### The Correct Architecture

#### Transaction Flow
```
Node 0 sends 1000 DD cents ($10) to Node 3's DD address

Transfer TX: abc123...
Inputs:
  ├─ vin[0]: xyz789:1 (5000 DD from Node 0's mint)
  └─ vin[1]: fee_utxo (DGB for network fee)
Outputs:
  ├─ vout[0]: 1000 DD → Node 3's DD address ← NODE 3 MUST DETECT THIS
  ├─ vout[1]: 4000 DD → Node 0's change address
  └─ vout[2]: DGB change
```

#### Node 3's Wallet Must:
1. **Scan Transaction**: When TX enters mempool or confirms
2. **Identify DD Outputs**: Check each vout for DD amount + our address
3. **Extract Information**:
   - Output index: 0
   - DD amount: 1000 cents
   - Destination: Node 3's DD address
4. **Verify Ownership**: Is output's address in our wallet? YES!
5. **Update State**:
   - Add to `dd_utxos` map: `(abc123:0) → 1000`
   - Update balance: 0 → 1000
   - Add to history
   - Notify GUI

## Implementation Plan - 7 Core Tasks

### Phase 1: Core Receiving Infrastructure (SEQUENTIAL)

#### Task 1: Transaction Scanning Hook
**Objective**: Hook into wallet's transaction processing pipeline
**Files**: `src/wallet/wallet.cpp`, `src/wallet/digidollarwallet.h`
**Priority**: CRITICAL - Foundation for everything

**Test Verification**:
```bash
# After implementing Task 1
./test/functional/digidollar_transfer.py
# Should still fail at line 158, but with better logging
```

**Compilation Check** (MANDATORY):
```bash
make -j$(nproc) src/qt/digibyte-qt
# MUST succeed before moving to Task 2
```

#### Task 2: DD Output Detection Logic
**Objective**: Identify DigiDollar outputs in any transaction
**Files**: `src/wallet/digidollarwallet.cpp`
**Priority**: CRITICAL

**Test Verification**:
```bash
./test/functional/digidollar_transfer.py
# Should still fail but detect DD output in logs
```

**Compilation Check** (MANDATORY):
```bash
make -j$(nproc) src/qt/digibyte-qt
# MUST succeed before moving to Task 3
```

#### Task 3: Ownership Verification
**Objective**: Determine if DD output belongs to our wallet
**Files**: `src/wallet/digidollarwallet.cpp`
**Priority**: CRITICAL

**Test Verification**:
```bash
./test/functional/digidollar_transfer.py
# Should still fail but verify ownership in logs
```

**Compilation Check** (MANDATORY):
```bash
make -j$(nproc) src/qt/digibyte-qt
# MUST succeed before moving to Phase 2
```

---

### Phase 2: State Management (CAN RUN IN PARALLEL)

#### Task 4: DD UTXO Addition
**Test Verification**:
```bash
./test/functional/digidollar_transfer.py
# Should show UTXO added in logs, balance still 0
```

**Compilation Check** (MANDATORY):
```bash
make -j$(nproc) src/qt/digibyte-qt
```

#### Task 5: Balance Update System
**Test Verification**:
```bash
./test/functional/digidollar_transfer.py
# 🎉 SHOULD PASS AT LINE 158! Balance = 1000!
```

**Compilation Check** (MANDATORY):
```bash
make -j$(nproc) src/qt/digibyte-qt
```

#### Task 6: Transaction History
**Test Verification**:
```bash
./test/functional/digidollar_transfer.py
# Should pass with transaction in history
```

**Compilation Check** (MANDATORY):
```bash
make -j$(nproc) src/qt/digibyte-qt
```

---

### Phase 3: GUI Integration (DEPENDS ON PHASE 1 & 2)

#### Task 7: GUI Notification & Display
**Test Verification**:
```bash
# Manual GUI test in regtest
./src/qt/digibyte-qt -regtest
```

**Compilation Check** (MANDATORY):
```bash
make -j$(nproc) src/qt/digibyte-qt
```

---

## TDD Strategy Using Existing Test

### The Test IS Our RED Phase

**You don't need to write failing tests - we have one!**

**Current Failure**:
```bash
$ ./test/functional/digidollar_transfer.py

# Output:
...
2025-10-04T... TestFramework (INFO): Testing simple DD transfers...
2025-10-04T... TestFramework (INFO): Transferring $10.0 DD (1000 cents) from node 0 to node 3...
2025-10-04T... TestFramework (ERROR): Assertion failed
Traceback (most recent call last):
  ...
  File "test/functional/digidollar_transfer.py", line 158, in test_simple_transfers
    assert_equal(receiver_final, expected_receiver)
AssertionError: 0 != 1000  ← THIS IS THE BUG!
```

### Implementation Strategy

**For Each Task**:

1. **Before Implementation** (RED):
   ```bash
   # Run test - should fail
   ./test/functional/digidollar_transfer.py
   # Note: WHERE it fails, WHAT the error is
   ```

2. **Implement Code** (GREEN):
   ```bash
   # Make code changes

   # MANDATORY: Compile after every change
   make -j$(nproc) src/qt/digibyte-qt
   # If compilation fails, fix before proceeding

   # Run test
   ./test/functional/digidollar_transfer.py
   # Check if failure moves forward or passes
   ```

3. **Refactor** (REFACTOR):
   ```bash
   # Improve code quality

   # MANDATORY: Compile
   make -j$(nproc) src/qt/digibyte-qt

   # Run test - must still pass
   ./test/functional/digidollar_transfer.py
   ```

### Compilation is MANDATORY

**NEVER proceed to next task if compilation fails!**

```bash
# After EVERY code change:
make -j$(nproc) src/qt/digibyte-qt

# If it fails:
# 1. Read the error
# 2. Fix the error
# 3. Compile again
# 4. Repeat until success

# Only then run the test
./test/functional/digidollar_transfer.py
```

## Sub-Agent Deployment Strategy

### Phase 1: Foundation (Sequential)
**Deploy one at a time, wait for completion**

**Agent 1**: Task 1 - Transaction Scanning Hook
- Implement hook
- **COMPILE**: `make -j$(nproc) src/qt/digibyte-qt`
- **TEST**: `./test/functional/digidollar_transfer.py`
- Must complete before Agent 2

**Agent 2**: Task 2 - DD Output Detection
- Implement detection
- **COMPILE**: `make -j$(nproc) src/qt/digibyte-qt`
- **TEST**: `./test/functional/digidollar_transfer.py`
- Must complete before Agent 3

**Agent 3**: Task 3 - Ownership Verification
- Implement verification
- **COMPILE**: `make -j$(nproc) src/qt/digibyte-qt`
- **TEST**: `./test/functional/digidollar_transfer.py`
- Must complete before Phase 2

### Phase 2: State Management (Parallel - Max 3 Agents)
**Deploy after Phase 1 complete, can run in parallel**

**Agent 4**: Task 4 - DD UTXO Addition
- **COMPILE**: `make -j$(nproc) src/qt/digibyte-qt`
- **TEST**: `./test/functional/digidollar_transfer.py`

**Agent 5**: Task 5 - Balance Update System
- **COMPILE**: `make -j$(nproc) src/qt/digibyte-qt`
- **TEST**: `./test/functional/digidollar_transfer.py`
- **🎉 This should make the test PASS!**

**Agent 6**: Task 6 - Transaction History
- **COMPILE**: `make -j$(nproc) src/qt/digibyte-qt`
- **TEST**: `./test/functional/digidollar_transfer.py`

### Phase 3: GUI (After Phase 2)

**Agent 7**: Task 7 - GUI Notification & Display
- **COMPILE**: `make -j$(nproc) src/qt/digibyte-qt`
- **TEST**: Manual Qt GUI testing

### Quality Checkpoints (EVERY Agent)

**After EVERY task - NO EXCEPTIONS**:

```bash
# 1. COMPILE (MANDATORY)
make -j$(nproc) src/qt/digibyte-qt
# If this fails, STOP and fix compilation errors

# 2. Run functional test
./test/functional/digidollar_transfer.py
# Note: Does it fail further? Pass? Where?

# 3. Check for warnings
# Review compilation output for warnings

# 4. Git commit (if all good)
git add [modified files]
git commit -m "Task #X: [description] - Test status: [pass/fail at line Y]"
```

### Task Assignment Format

```markdown
## Task: Receive Task #X - [Name]

**Your Mission**: [specific implementation]

**Testing Strategy**:
1. Run: `./test/functional/digidollar_transfer.py` (note current failure)
2. Implement your code
3. **COMPILE**: `make -j$(nproc) src/qt/digibyte-qt` (MUST succeed)
4. Run: `./test/functional/digidollar_transfer.py` (note new status)
5. Repeat until test passes or advances

**Files to Modify**:
- [file1] - [what to change]

**Critical Requirements**:
- Wallet MUST compile after changes
- Test MUST run (pass or fail is informative)
- No regressions in existing tests

**Report Back**:
- Compilation: SUCCESS/FAILED
- Test result: Line number of failure or PASS
- Changes made
```

## Success Criteria

### Technical Validation (ALL Must Pass)

**Compilation** (NO EXCEPTIONS):
```bash
make -j$(nproc) src/qt/digibyte-qt
# Exit code: 0
# Output: No errors
```

**Functional Test**:
```bash
./test/functional/digidollar_transfer.py
# Exit code: 0
# Output: All tests passed
```

**Other Tests** (No Regressions):
```bash
./src/test/test_digibyte --run_test=digidollar_*
# All should still pass
```

### Functional Validation (Qt Regtest)

**Full Send/Receive Flow Test**:
1. ✅ Start two fresh regtest wallets (Alice & Bob)
2. ✅ Alice: Generate 650 blocks
3. ✅ Alice: Mint 1000 DD
4. ✅ Bob: Generate new DD address
5. ✅ Alice: Send 500 DD to Bob's DD address
6. ✅ **Transaction broadcasts successfully**
7. ✅ **Bob's wallet detects incoming transaction** (NEW!)
8. ✅ **Bob's balance updates to 500 DD** (NEW!)
9. ✅ **Bob sees transaction in history as "receive"** (NEW!)
10. ✅ **Bob's GUI shows 500 DD balance** (NEW!)
11. ✅ Alice's balance shows 500 DD (change)
12. ✅ Alice's time-lock still ACTIVE
13. ✅ Mine 1 block to confirm
14. ✅ Both wallets show confirmed balances
15. ✅ Bob can send 250 DD to Alice (reverse test)
16. ✅ Alice receives 250 DD
17. ✅ Restart both wallets
18. ✅ All balances persist correctly
19. ✅ All transaction history persists
20. ✅ **SUCCESS**: Complete send/receive cycle works! ✅

## The Moment of Success

**When Task 5 (Balance Update) is complete:**

```bash
$ ./test/functional/digidollar_transfer.py

# Expected Output:
2025-10-04T... TestFramework (INFO): Testing DigiDollar transfer operations...
2025-10-04T... TestFramework (INFO): Generating initial blocks for test setup...
2025-10-04T... TestFramework (INFO): Creating initial DD positions for testing...
2025-10-04T... TestFramework (INFO): Testing simple DD transfers...
2025-10-04T... TestFramework (INFO): Transferring $10.0 DD (1000 cents) from node 0 to node 3...
2025-10-04T... TestFramework (INFO): SUCCESS: Receiver balance updated to 1000 cents! ✅
2025-10-04T... TestFramework (INFO): Testing multi-input transfers...
...
2025-10-04T... TestFramework (INFO): All tests passed!

# Exit code: 0
# 🎉 VICTORY! 🎉
```

## Communication Protocol

### Report Progress After Each Task

```markdown
## Receive Task #X Complete ✅

**Compilation**:
- Command: `make -j$(nproc) src/qt/digibyte-qt`
- Result: SUCCESS
- Time: 2m 34s
- Warnings: None

**Test Results**:
- Command: `./test/functional/digidollar_transfer.py`
- Result: FAIL at line 158 (expected - balance update not yet implemented)
- Progress: Now detecting DD outputs in logs
- Next failure point: Balance still 0, need Task 5

**Files Modified**:
- src/wallet/digidollarwallet.cpp (+45 lines)
- src/wallet/digidollarwallet.h (+3 lines)

**Next**: Ready for Task #Y
```

### If Compilation Fails

```markdown
## Receive Task #X Blocked - COMPILATION FAILED ❌

**Error**:
```
src/wallet/digidollarwallet.cpp:234:15: error: 'ScanForIncomingDD' was not declared in this scope
```

**Root Cause**: Missing method declaration in header

**Fix Applied**: Added declaration to digidollarwallet.h

**Retry**: Compiling again...
```

### If Blocked (Non-Compilation)

```markdown
## Receive Task #X Blocked ❌

**Issue**: [describe problem]
**Test Output**: [relevant test output]
**Need**: [what's needed to unblock]
```

## Remember

- **THE TEST IS YOUR GUIDE**: `./test/functional/digidollar_transfer.py`
- **COMPILE AFTER EVERY CHANGE**: `make -j$(nproc) src/qt/digibyte-qt`
- **Test shows progress**: Each task moves the failure point forward
- **Task 5 is the winner**: Balance update makes the test pass
- **Don't break sending**: Test also validates sending still works
- **No exceptions**: Compile → Test → Commit (in that order, always)

---

**Orchestrator Version**: 2.0 - Using Existing Functional Test
**Strategy**: Compilation-First, Test-Driven, Sequential+Parallel Execution
**Goal**: Make `./test/functional/digidollar_transfer.py` pass completely

## Your First Action

1. Read DIGIDOLLAR_RECEIVE_TASKS.md (understand all 7 tasks)
2. Run `./test/functional/digidollar_transfer.py` (see current failure)
3. Deploy Sub-Agent 1 for Task 1 (Transaction Scanning Hook)
4. Agent 1 must: Implement → Compile → Test → Report
5. Deploy Sub-Agent 2 for Task 2 (DD Output Detection)
6. Agent 2 must: Implement → Compile → Test → Report
7. Continue through all tasks
8. **CELEBRATE** when test passes! 🎉
