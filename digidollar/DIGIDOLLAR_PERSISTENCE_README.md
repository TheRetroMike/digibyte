# DigiDollar Database Persistence - Implementation Guide

## 📋 Document Overview

This directory contains **4 comprehensive documents** for implementing 100% database persistence for DigiDollar positions:

### 1. **DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md** (Master Task List)
- **Purpose**: Complete technical roadmap with 8 phases
- **Content**: Detailed implementation steps, code examples, file locations
- **Use**: Reference for what needs to be built and where

### 2. **DIGIDOLLAR_PERSISTENCE_ORCHESTRATOR.md** (Orchestrator Agent Prompt)
- **Purpose**: Instructions for coordinating sub-agent deployment
- **Content**: Phase management, quality control, progress tracking
- **Use**: For the orchestrator agent managing the overall implementation

### 3. **DIGIDOLLAR_PERSISTENCE_SUBAGENT.md** (Sub-Agent Work Prompt)
- **Purpose**: Detailed instructions for implementing specific tasks
- **Content**: TDD workflows, code patterns, deliverable requirements
- **Use**: For each sub-agent working on individual tasks

### 4. **DIGIDOLLAR_PERSISTENCE_TDD_GUIDE.md** (Test-Driven Development Guide)
- **Purpose**: Complete TDD workflows for every task
- **Content**: RED-GREEN-REFACTOR cycles, test examples, verification commands
- **Use**: Reference for writing tests FIRST before any implementation

---

## 🎯 The Problem We're Solving

**Current State (❌ BROKEN):**
```
User mints DigiDollar → Stored in RAM only
User closes wallet → RAM cleared
User reopens wallet → Position is GONE! 💥
```

**Target State (✅ FIXED):**
```
User mints DigiDollar → Stored in RAM + wallet.dat database
User closes wallet → RAM cleared, database persists
User reopens wallet → Position loads from database ✅
```

---

## 📚 How to Use These Documents

### For Orchestrator Agents:

1. **Read First:**
   - DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md (understand all 8 phases)
   - DIGIDOLLAR_PERSISTENCE_ORCHESTRATOR.md (understand your role)
   - DIGIDOLLAR_PERSISTENCE_TDD_GUIDE.md (understand TDD requirements)

2. **Deploy Sub-Agents:**
   - Use the template in DIGIDOLLAR_PERSISTENCE_ORCHESTRATOR.md
   - Deploy ONE sub-agent at a time
   - Ensure TDD is followed (tests FIRST)

3. **Track Progress:**
   - Update task checklist after each sub-agent completes
   - Verify compilation after every task
   - Run full test suite to check for regression

### For Sub-Agent Workers:

1. **Read Your Assignment:**
   - Orchestrator will give you a specific task
   - Read relevant section in DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md
   - Study the TDD workflow in DIGIDOLLAR_PERSISTENCE_TDD_GUIDE.md

2. **Follow TDD Strictly:**
   - 🔴 RED: Write failing test FIRST
   - 🟢 GREEN: Write minimal code to pass
   - 🔵 REFACTOR: Improve code quality
   - ✅ VERIFY: Run full test suite

3. **Use Patterns:**
   - Follow examples in DIGIDOLLAR_PERSISTENCE_SUBAGENT.md
   - Copy existing patterns from codebase (WriteIC/ReadIC/EraseIC)
   - Test file naming: `digidollar_persistence_*.cpp`

4. **Report Completion:**
   - Use completion report template
   - Include test results
   - Confirm compilation success

---

## 🧪 Test-Driven Development (TDD) Mandate

**CRITICAL**: Every single task MUST follow RED-GREEN-REFACTOR:

### The TDD Cycle

```
┌─────────────────────────────────────────────────┐
│  1. 🔴 RED: Write Failing Test                 │
│     - Create test file with digidollar_        │
│       persistence_ prefix                       │
│     - Write test that WILL fail                 │
│     - Verify it fails for right reason          │
└──────────────┬──────────────────────────────────┘
               ▼
┌─────────────────────────────────────────────────┐
│  2. 🟢 GREEN: Write Minimal Implementation      │
│     - Add just enough code to pass              │
│     - No extra features                         │
│     - Verify test passes                        │
└──────────────┬──────────────────────────────────┘
               ▼
┌─────────────────────────────────────────────────┐
│  3. 🔵 REFACTOR: Improve Quality                │
│     - Add error handling                        │
│     - Add logging                               │
│     - Add comments                              │
│     - Verify tests still pass                   │
└──────────────┬──────────────────────────────────┘
               ▼
┌─────────────────────────────────────────────────┐
│  4. ✅ VERIFY: Full Test Suite                  │
│     - Clean rebuild                             │
│     - Run new tests                             │
│     - Run ALL DigiDollar tests                  │
│     - Check for warnings                        │
│     - Run functional tests                      │
└─────────────────────────────────────────────────┘
```

### Test Naming Convention

✅ **CORRECT:**
- `src/test/digidollar_persistence_keys_tests.cpp`
- `src/test/digidollar_persistence_serialization_tests.cpp`
- `src/test/digidollar_persistence_walletbatch_tests.cpp`
- `src/wallet/test/digidollar_persistence_wallet_tests.cpp`
- `test/functional/digidollar_persistence_restart.py`

❌ **WRONG:**
- `src/test/persistence_tests.cpp` (missing digidollar prefix)
- `src/test/dd_persist_tests.cpp` (use full name)
- `src/test/wallet_db_tests.cpp` (not specific enough)

### Verification Commands (Run After Every Task)

```bash
# 1. Clean rebuild
make clean && make -j$(nproc)

# 2. Run new persistence tests
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_persistence_*

# 3. Run ALL DigiDollar tests (check for regression)
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_*

# 4. Check for compilation warnings
make 2>&1 | grep -i warning

# 5. Run functional tests (when applicable)
./test/functional/digidollar_persistence_basic.py
./test/functional/digidollar_persistence_restart.py
```

---

## 📊 Implementation Phases

### Phase 1: Database Schema (Week 1, Days 1-2)
- **Task 1.1**: Add DD database keys (DD_POSITION, DD_TRANSACTION, etc.)
- **Task 1.2**: Add serialization to WalletCollateralPosition, DDTransaction, WalletDDBalance
- **Tests**: `digidollar_persistence_keys_tests.cpp`, `digidollar_persistence_serialization_tests.cpp`

### Phase 2: WalletBatch Methods (Week 1, Days 3-5)
- **Task 2.1**: Implement Write methods (WritePosition, WriteDDTransaction, etc.)
- **Task 2.2**: Implement Read methods (ReadPosition, ReadDDTransaction, etc.)
- **Task 2.3**: Implement Erase methods (ErasePosition, EraseDDTransaction, etc.)
- **Tests**: `digidollar_persistence_walletbatch_tests.cpp`

### Phase 3: DigiDollarWallet Integration (Week 2, Days 1-3)
- **Task 3.1**: Implement WriteDDBalance() - persist balances
- **Task 3.2**: Implement WritePosition() - persist positions
- **Task 3.3**: Implement UpdatePositionStatus() - update active/inactive
- **Tests**: `digidollar_persistence_wallet_tests.cpp`

### Phase 4: Database Loading (Week 2, Days 4-5) ⭐ CRITICAL
- **Task 4.1**: Implement LoadFromDatabase() - THE KEY METHOD
- **Task 4.2**: Hook into wallet initialization
- **Tests**: Integration tests verifying data loads on startup

### Phase 5: Transaction Hooks (Week 3, Days 1-2)
- **Task 5.1**: Auto-save on mint
- **Task 5.2**: Auto-save on transfer
- **Task 5.3**: Auto-save on redeem
- **Tests**: Extend existing `digidollar_wallet_tests.cpp`

### Phase 6: Block Connection Hooks (Week 3, Days 3-4)
- **Task 6.1**: Update confirmations on new blocks
- **Tests**: Block connection/disconnection tests

### Phase 7: Integration Testing (Week 3, Day 5) 🎯 THE FINAL TEST
- **Task 7.1**: Comprehensive unit tests
- **Task 7.2**: **THE CRITICAL TEST** - Mint → Restart → Position Exists
- **Tests**: `digidollar_persistence_restart.py` functional test

### Phase 8: Migration & Cleanup (Bonus)
- **Task 8.1**: Schema versioning
- **Task 8.2**: Cleanup old positions

---

## 🎯 The Ultimate Success Test

**This test MUST pass before implementation is considered complete:**

```python
# File: test/functional/digidollar_persistence_restart.py

def run_test(self):
    # Step 1: Mint a position
    position_id = self.nodes[0].mintdigidollar(amount=100, lockdays=365)

    # Step 2: Verify it exists
    positions = self.nodes[0].listddpositions()
    assert len(positions) == 1

    # Step 3: Stop wallet
    self.stop_node(0)

    # Step 4: Restart wallet
    self.start_node(0)

    # Step 5: THE MAGIC - Position STILL exists!
    positions = self.nodes[0].listddpositions()
    assert len(positions) == 1, "FAIL: Position lost!"
    assert positions[0]['position_id'] == position_id, "FAIL: Data corrupted!"

    print("SUCCESS: DigiDollar positions persist across restart!")
```

**If this test passes, the feature is complete. If it fails, keep working!**

---

## 📁 Files That Will Be Modified

### New Test Files (Created):
- `src/test/digidollar_persistence_keys_tests.cpp`
- `src/test/digidollar_persistence_serialization_tests.cpp`
- `src/test/digidollar_persistence_walletbatch_tests.cpp`
- `src/wallet/test/digidollar_persistence_wallet_tests.cpp`
- `test/functional/digidollar_persistence_basic.py`
- `test/functional/digidollar_persistence_restart.py`

### Existing Files (Modified):
- `src/wallet/walletdb.h` - Add DD database keys, method declarations
- `src/wallet/walletdb.cpp` - Implement read/write/erase methods
- `src/wallet/digidollarwallet.h` - Add serialization, LoadFromDatabase()
- `src/wallet/digidollarwallet.cpp` - Implement persistence logic
- `src/wallet/wallet.cpp` - Hook LoadFromDatabase() into initialization
- `src/wallet/test/digidollar_wallet_tests.cpp` - Extend with persistence checks

---

## 🔍 Code Patterns to Follow

### Database Write Pattern
```cpp
bool WalletBatch::WritePosition(const WalletCollateralPosition& position)
{
    return WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);
}
```

### Database Read Pattern
```cpp
bool WalletBatch::ReadPosition(const uint256& position_id, WalletCollateralPosition& position)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_POSITION, position_id), position);
}
```

### Serialization Pattern
```cpp
SERIALIZE_METHODS(WalletCollateralPosition, obj)
{
    READWRITE(obj.position_id);
    READWRITE(obj.dd_minted);
    READWRITE(obj.dgb_collateral);
    READWRITE(obj.lock_tier);
    READWRITE(obj.unlock_height);
    READWRITE(obj.is_active);
}
```

### Database Cursor Iteration Pattern
```cpp
std::unique_ptr<DatabaseCursor> cursor = batch.GetNewCursor();
DatabaseCursor::Status status = DatabaseCursor::Status::MORE;
while (status == DatabaseCursor::Status::MORE) {
    DataStream key{}, value{};
    status = cursor->Next(key, value);
    if (status != DatabaseCursor::Status::MORE) break;

    std::string key_type;
    key >> key_type;

    if (key_type == DBKeys::DD_POSITION) {
        uint256 position_id;
        key >> position_id;
        WalletCollateralPosition position;
        value >> position;
        // Process position...
    }
}
```

---

## ✅ Definition of Done

Implementation is complete when ALL of the following are true:

- [x] All 8 phases implemented
- [x] All unit tests pass: `./src/test/test_digibyte --run_test=digidollar_persistence_*`
- [x] No regression: `./src/test/test_digibyte --run_test=digidollar_*`
- [x] Critical test passes: `./test/functional/digidollar_persistence_restart.py`
- [x] Code compiles cleanly: `make clean && make -j$(nproc)`
- [x] Zero compilation warnings
- [x] wallet.dat export/import preserves all DD data
- [x] **Mint → Close Wallet → Reopen → Position Exists** ✅

---

## 🚀 Getting Started

### For Orchestrators:
1. Read all 4 documents completely
2. Understand the 8-phase structure
3. Prepare first sub-agent deployment (Phase 1, Task 1.1)
4. Deploy sub-agents ONE AT A TIME
5. Verify tests pass after each task

### For Sub-Agents:
1. Read your specific task assignment
2. Read the TDD guide for your task type
3. Write tests FIRST (RED)
4. Implement minimal code (GREEN)
5. Refactor for quality (REFACTOR)
6. Verify no regression (VERIFY)
7. Report completion

---

## 📞 Quick Reference

| Need | See Document | Section |
|------|-------------|---------|
| Overall roadmap | DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md | All phases |
| Phase management | DIGIDOLLAR_PERSISTENCE_ORCHESTRATOR.md | Phase breakdown |
| How to implement a task | DIGIDOLLAR_PERSISTENCE_SUBAGENT.md | Task execution |
| TDD workflow | DIGIDOLLAR_PERSISTENCE_TDD_GUIDE.md | Phase-specific TDD |
| Code patterns | DIGIDOLLAR_PERSISTENCE_SUBAGENT.md | Existing patterns |
| Test examples | DIGIDOLLAR_PERSISTENCE_TDD_GUIDE.md | Test files |
| Verification commands | All documents | Verification sections |

---

## ⚠️ Critical Reminders

1. **ALWAYS write tests FIRST** (RED-GREEN-REFACTOR)
2. **ONE sub-agent at a time** (no parallel deployment)
3. **Compile after EVERY task** (catch errors early)
4. **Run ALL tests after EVERY task** (prevent regression)
5. **Follow existing patterns EXACTLY** (WriteIC/ReadIC/EraseIC)
6. **Test naming: `digidollar_persistence_*`** (required convention)
7. **The restart test MUST pass** (non-negotiable)

---

**This implementation will make DigiDollar positions as permanent as regular Bitcoin/DigiByte UTXOs. Once complete, users will never lose their DigiDollar positions, balances, or transaction history - even if they close and reopen their wallet 1000 times!**

**Start with Phase 1, Task 1.1 and work systematically through all 8 phases. Quality over speed. Test everything. Success is measured by the restart test passing.**

🚀 **Ready to begin? Start by reading DIGIDOLLAR_PERSISTENCE_ORCHESTRATOR.md and deploying your first sub-agent!**
