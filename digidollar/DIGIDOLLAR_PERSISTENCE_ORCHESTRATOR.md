# DigiDollar Database Persistence - Orchestrator Agent Prompt

You are the Orchestrator Agent responsible for implementing **100% persistent database storage** for DigiDollar positions in wallet.dat. This is a **CRITICAL** feature that blocks production use - currently all DigiDollar positions are lost when the wallet is closed.

## Your Mission

Coordinate the implementation of full database persistence for DigiDollar so that:
- ✅ Minted positions survive wallet restart
- ✅ Transaction history persists across sessions
- ✅ Balances remain intact after closing/reopening
- ✅ wallet.dat exports/imports preserve all DD data

## Critical Context Documents

**MUST READ BEFORE STARTING (in this order):**
1. **DIGIDOLLAR_PERSISTENCE_README.md** - Overview and navigation guide for all documents
2. **DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md** - Your master implementation roadmap (8 phases)
3. **DIGIDOLLAR_PERSISTENCE_TDD_GUIDE.md** - Complete TDD workflows for every task
4. **CLAUDE.md** - DigiByte v8.26 constants and conventions (CRITICAL!)
5. **DigiByte_v8.26_DigiDollar_Implementation_Report.md** - Current implementation status
6. **digidollar/TECHNICAL_SPECIFICATION.md** - DigiDollar system design
7. **DIGIDOLLAR_100_PERCENT_CHECKLIST.md** - Overall project completion status
8. **digidollar/IMPLEMENTATION_TASKS.md** - Master DigiDollar task tracking

**These documents provide:**
- What's already implemented (DigiDollar minting, GUI, RPC)
- What's broken (database persistence)
- How to fix it (8-phase roadmap)
- How to test it (TDD guide with examples)
- What constants to use (DigiByte, not Bitcoin)

## Implementation Strategy

### Test-Driven Development (TDD) - MANDATORY

**CRITICAL**: This implementation MUST follow strict Red-Green-Refactor TDD:

1. **RED**: Write failing tests FIRST
2. **GREEN**: Write minimal code to pass tests
3. **REFACTOR**: Improve code quality while keeping tests green

### Test File Naming Convention

ALL DigiDollar persistence tests MUST use prefix `digidollar_persistence_`:

```
src/test/digidollar_persistence_tests.cpp           # Main persistence tests
src/wallet/test/digidollar_persistence_wallet_tests.cpp  # Wallet-specific tests
test/functional/digidollar_persistence_basic.py     # Integration tests
test/functional/digidollar_persistence_restart.py   # Wallet restart tests
```

### Existing DigiDollar Tests

The codebase already has 21 DigiDollar test files (see `src/test/digidollar_*.cpp`). Your persistence tests will:
- **EXTEND** existing tests (e.g., add persistence checks to `digidollar_wallet_tests.cpp`)
- **CREATE NEW** dedicated persistence test files
- **VERIFY** no regression in existing DD functionality

### Compilation Verification

After EVERY sub-agent task:
```bash
# Compile the code
make -j$(nproc)

# Run unit tests
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_persistence_*

# Run functional tests (when ready)
./test/functional/digidollar_persistence_basic.py
```

## Phase Breakdown (8 Phases from Master Task List)

### PHASE 1: Database Schema Design (Week 1, Days 1-2)
**Goal**: Define database keys and add serialization to structures

**Sub-Agent Tasks:**
1. **Task 1.1**: Add DD database keys to `src/wallet/walletdb.h` and `.cpp`
   - Keys: DD_POSITION, DD_TRANSACTION, DD_BALANCE, DD_OUTPUT, DD_METADATA
   - TDD: Write tests that verify key uniqueness and format

2. **Task 1.2**: Add SERIALIZE_METHODS to DD structures
   - `WalletCollateralPosition`
   - `DDTransaction`
   - `WalletDDBalance`
   - TDD: Write serialization round-trip tests

**Success Criteria**:
- [ ] Tests written FIRST with `digidollar_persistence_` prefix
- [ ] All tests pass (GREEN)
- [ ] Code compiles without errors
- [ ] Keys defined in DBKeys namespace
- [ ] Structures can serialize/deserialize

---

### PHASE 2: WalletBatch Write Methods (Week 1, Days 3-5)
**Goal**: Implement database write operations

**Sub-Agent Tasks:**
1. **Task 2.1**: Implement WalletBatch write methods
   - `WritePosition()`
   - `WriteDDTransaction()`
   - `WriteDDBalance()`
   - `WriteDDOutput()`
   - `WriteDDMetadata()`
   - TDD: Write tests that verify data is written to database

2. **Task 2.2**: Implement WalletBatch read methods
   - `ReadPosition()`
   - `ReadDDTransaction()`
   - `ReadDDBalance()`
   - `ReadDDOutput()`
   - `ReadDDMetadata()`
   - TDD: Write tests that verify data can be read back

3. **Task 2.3**: Implement WalletBatch erase methods
   - `ErasePosition()`
   - `EraseDDTransaction()`
   - `EraseDDBalance()`
   - `EraseDDOutput()`
   - TDD: Write tests that verify data is deleted

**Success Criteria**:
- [ ] Tests written FIRST
- [ ] Write/Read/Erase round-trip works
- [ ] Compatible with BerkeleyDB and SQLite
- [ ] All tests pass

---

### PHASE 3: DigiDollarWallet Integration (Week 2, Days 1-3)
**Goal**: Make DigiDollarWallet save data to database

**Sub-Agent Tasks:**
1. **Task 3.1**: Implement `DigiDollarWallet::WriteDDBalance()`
   - Update in-memory cache
   - Persist to database
   - Recalculate total balance
   - TDD: Test balance persistence

2. **Task 3.2**: Implement `DigiDollarWallet::WritePosition()`
   - Update in-memory positions map
   - Persist to database
   - Update locked collateral total
   - TDD: Test position persistence

3. **Task 3.3**: Implement `DigiDollarWallet::UpdatePositionStatus()`
   - Mark positions active/inactive
   - Update database
   - Recalculate collateral
   - TDD: Test status updates

**Success Criteria**:
- [ ] Tests written FIRST
- [ ] Data written to wallet.dat
- [ ] In-memory and database stay in sync
- [ ] Totals correctly calculated
- [ ] All tests pass

---

### PHASE 4: Database Loading (Week 2, Days 4-5) - **THE CRITICAL PHASE**
**Goal**: Load DigiDollar data from database on wallet startup

**Sub-Agent Tasks:**
1. **Task 4.1**: Implement `DigiDollarWallet::LoadFromDatabase()`
   - `LoadPositionsFromDatabase()` - Use DatabaseCursor to iterate DD_POSITION entries
   - `LoadTransactionsFromDatabase()` - Load transaction history
   - `LoadBalancesFromDatabase()` - Load DD balances
   - `RecalculateTotals()` - Rebuild totals from loaded data
   - TDD: Write tests that verify data loads correctly

2. **Task 4.2**: Hook `LoadFromDatabase()` into wallet initialization
   - Find wallet loading code in `src/wallet/wallet.cpp`
   - Call `LoadFromDatabase()` after wallet loads
   - TDD: Integration test that verifies loading on wallet creation

**Success Criteria**:
- [ ] Tests written FIRST
- [ ] LoadFromDatabase() restores complete state
- [ ] Called during wallet initialization
- [ ] All positions/transactions/balances restored
- [ ] **Integration test: Write data → Close wallet → Reopen → Data still there**
- [ ] All tests pass

---

### PHASE 5: Transaction Hooks (Week 3, Days 1-2)
**Goal**: Auto-save positions when minting/transferring/redeeming

**Sub-Agent Tasks:**
1. **Task 5.1**: Hook mint transaction saving
   - Modify `DigiDollarWallet::MintDigiDollar()` in `digidollarwallet.cpp`
   - Call `WritePosition()` after successful mint
   - Save mint transaction to history
   - TDD: Test that mint persists position

2. **Task 5.2**: Hook transfer transaction saving
   - Modify `DigiDollarWallet::TransferDigiDollar()`
   - Save transaction to history
   - Update balances
   - TDD: Test that transfer updates database

3. **Task 5.3**: Hook redemption transaction saving
   - Modify `DigiDollarWallet::RedeemDigiDollar()`
   - Call `UpdatePositionStatus(position_id, false)`
   - Save redemption transaction
   - TDD: Test that redeem marks position inactive

**Success Criteria**:
- [ ] Tests written FIRST
- [ ] Mint saves position to database
- [ ] Transfer saves transaction to database
- [ ] Redeem marks position inactive
- [ ] All existing DigiDollar tests still pass (no regression)
- [ ] All new tests pass

---

### PHASE 6: Block Connection Hooks (Week 3, Days 3-4)
**Goal**: Update confirmations and handle reorgs

**Sub-Agent Tasks:**
1. **Task 6.1**: Implement DD block handlers
   - Add `DigiDollarWallet::ProcessDDTransaction(tx, height, connect)`
   - Hook into `CWallet::BlockConnected()` in `wallet.cpp`
   - Hook into `CWallet::BlockDisconnected()` for reorgs
   - Update confirmations in database
   - TDD: Test confirmation updates and reorg handling

**Success Criteria**:
- [ ] Tests written FIRST
- [ ] Confirmations increase with new blocks
- [ ] Reorgs correctly decrement confirmations
- [ ] Database stays consistent
- [ ] All tests pass

---

### PHASE 7: Integration Testing (Week 3, Day 5)
**Goal**: Comprehensive end-to-end testing

**Sub-Agent Tasks:**
1. **Task 7.1**: Write comprehensive unit tests
   - File: `src/test/digidollar_persistence_tests.cpp`
   - Test all write/read/erase operations
   - Test serialization round-trips
   - Test edge cases (empty data, large datasets)
   - TDD: Already written tests, now verify 100% coverage

2. **Task 7.2**: Write integration tests
   - File: `test/functional/digidollar_persistence_restart.py`
   - Test: Mint → Stop wallet → Start wallet → Verify position exists
   - Test: Send → Restart → Transaction history intact
   - Test: Export wallet.dat → Import → All data restored
   - TDD: Full end-to-end scenarios

**Success Criteria**:
- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] Mint → Restart → Position persists (THE KEY TEST)
- [ ] wallet.dat export/import works
- [ ] No regression in existing functionality
- [ ] Test coverage > 80%

---

### PHASE 8: Migration & Cleanup (Bonus)
**Goal**: Future-proof the implementation

**Sub-Agent Tasks:**
1. **Task 8.1**: Implement schema versioning
   - Add `dd_schema_version` metadata
   - Check version on wallet load
   - TDD: Test version checking

2. **Task 8.2**: Implement cleanup utility
   - `DigiDollarWallet::CleanupInactivePositions(blocks_ago)`
   - Remove old redeemed positions
   - TDD: Test cleanup functionality

**Success Criteria**:
- [ ] Tests written FIRST
- [ ] Schema version written to new wallets
- [ ] Cleanup removes old positions
- [ ] All tests pass

---

## Sub-Agent Deployment Template

When deploying a sub-agent, use this exact format:

```markdown
## Task: [Specific Task Name from Phase]

### Objective
[Clear, measurable objective from master task list]

### Context
- **Current Phase**: [Phase X - Name]
- **Dependencies**: [What must be complete before this task]
- **Related Files**:
  - `src/wallet/walletdb.h` (if modifying)
  - `src/wallet/walletdb.cpp` (if modifying)
  - `src/wallet/digidollarwallet.h` (if modifying)
  - `src/wallet/digidollarwallet.cpp` (if modifying)

### Test-First Requirements (CRITICAL)

**Step 1: Write Failing Tests (RED)**
Create test file: `src/test/digidollar_persistence_[component]_tests.cpp`

Example:
```cpp
BOOST_AUTO_TEST_SUITE(digidollar_persistence_[component]_tests)

BOOST_AUTO_TEST_CASE(test_write_position_to_database) {
    // This test will FAIL initially because WritePosition() doesn't exist yet
    WalletCollateralPosition pos(test_data);
    BOOST_CHECK(wallet_batch.WritePosition(pos) == true);
}

BOOST_AUTO_TEST_SUITE_END()
```

**Step 2: Verify Test Fails**
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_[component]_tests
# Should see RED (failing tests)
```

**Step 3: Implement Minimal Code (GREEN)**
Add just enough code to make tests pass.

**Step 4: Verify Test Passes**
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_[component]_tests
# Should see GREEN (passing tests)
```

**Step 5: Refactor**
Improve code quality while keeping tests green.

### Specifications
[Copy relevant section from DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md]

### Existing Code Patterns to Follow

**Database Write Pattern:**
```cpp
// From walletdb.cpp:94
bool WalletBatch::WriteTx(const CWalletTx& wtx)
{
    return WriteIC(std::make_pair(DBKeys::TX, wtx.GetHash()), wtx);
}
```

**Database Read Pattern:**
```cpp
// From walletdb.cpp:192
bool WalletBatch::ReadPool(int64_t nPool, CKeyPool& keypool)
{
    return m_batch->Read(std::make_pair(DBKeys::POOL, nPool), keypool);
}
```

**Serialization Pattern:**
```cpp
// From digidollar/digidollar.h:41-48
SERIALIZE_METHODS(CDigiDollarOutput, obj)
{
    READWRITE(obj.nDDAmount);
    READWRITE(obj.collateralId);
    READWRITE(obj.nLockTime);
    READWRITE(obj.internalKey);
    READWRITE(obj.taprootMerkleRoot);
}
```

### Deliverables
1. **Test File**: `src/test/digidollar_persistence_[component]_tests.cpp`
   - [ ] Tests written FIRST
   - [ ] Tests verify all requirements
   - [ ] Tests pass after implementation

2. **Implementation Files**:
   - [ ] `src/wallet/walletdb.h` (declarations)
   - [ ] `src/wallet/walletdb.cpp` (implementations)
   - [ ] OR `src/wallet/digidollarwallet.cpp` (for wallet methods)

3. **Functions Implemented**:
   - [ ] [List specific functions from task]

4. **Compilation Verification**:
   - [ ] Code compiles: `make -j$(nproc)`
   - [ ] Tests pass: `./src/test/test_digibyte --run_test=digidollar_persistence_*`
   - [ ] No warnings or errors

### Success Criteria
- [ ] Tests written FIRST with `digidollar_persistence_` prefix
- [ ] All tests pass (GREEN)
- [ ] Code compiles without errors or warnings
- [ ] Follows DigiByte coding standards
- [ ] Follows existing database patterns (WriteIC/ReadIC/EraseIC)
- [ ] No regression in existing DigiDollar tests
- [ ] Test coverage for new code > 80%

### Verification Commands
```bash
# Compile
make -j$(nproc)

# Run unit tests
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_persistence_*

# Run ALL DigiDollar tests to verify no regression
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_*

# Check for compilation warnings
make 2>&1 | grep -i warning
```

### Additional Notes
[Any special considerations, warnings, or integration points]
```

---

## Sub-Agent Work Tracking

Use this checklist format to track sub-agent tasks:

### Phase 1: Database Schema
- [ ] Task 1.1: Add DD database keys (READY TO START)
  - Sub-agent deployed: [Date/Time]
  - Status: ⏳ Pending / 🔄 In Progress / ✅ Complete / ❌ Blocked
  - Tests written first: [Yes/No]
  - Tests passing: [Yes/No]
  - Notes: [Any issues or decisions]

- [ ] Task 1.2: Add serialization to structures
  - Sub-agent deployed: [Date/Time]
  - Status: ⏳ Pending / 🔄 In Progress / ✅ Complete / ❌ Blocked
  - Tests written first: [Yes/No]
  - Tests passing: [Yes/No]
  - Notes: [Any issues or decisions]

[... repeat for all tasks in all phases ...]

---

## Quality Standards

All code MUST:
1. ✅ **Tests written FIRST** (Red-Green-Refactor)
2. ✅ **Test prefix**: Use `digidollar_persistence_` for new tests
3. ✅ **Extend existing tests**: Add persistence checks to `digidollar_wallet_tests.cpp`
4. ✅ **Compile cleanly**: No errors or warnings
5. ✅ **Follow patterns**: Use existing WalletBatch patterns (WriteIC/ReadIC/EraseIC)
6. ✅ **No regression**: All existing DigiDollar tests still pass
7. ✅ **Comprehensive**: Test coverage > 80%
8. ✅ **Integration**: End-to-end restart test passes

---

## Critical Success Test (The North Star)

**THIS IS THE TEST THAT MATTERS MOST:**

```python
# File: test/functional/digidollar_persistence_restart.py
def run_test(self):
    # Step 1: Mint a DigiDollar position
    position_id = self.nodes[0].mintdigidollar(amount=100, lockdays=365)

    # Step 2: Verify position exists
    positions = self.nodes[0].listddpositions()
    assert len(positions) == 1
    assert positions[0]['position_id'] == position_id

    # Step 3: Stop the wallet
    self.stop_node(0)

    # Step 4: Restart the wallet
    self.start_node(0)

    # Step 5: Verify position STILL exists (THIS IS THE MAGIC)
    positions = self.nodes[0].listddpositions()
    assert len(positions) == 1, "FAIL: Position lost after restart!"
    assert positions[0]['position_id'] == position_id, "FAIL: Position data corrupted!"

    print("SUCCESS: DigiDollar positions persist across wallet restart!")
```

**This test MUST pass before Phase 7 is considered complete.**

---

## Communication with Sub-Agents

### Deploying a Sub-Agent
1. **Copy the template above**
2. **Fill in specific task details** from master task list
3. **Provide code examples** from existing codebase
4. **Specify exact file paths** and line numbers
5. **Emphasize TDD**: Tests FIRST, then implementation
6. **Deploy ONE sub-agent at a time**

### Reviewing Sub-Agent Work
1. **Check tests were written first** ✅
2. **Verify tests pass** ✅
3. **Compile the code** ✅
4. **Run all DigiDollar tests** (check for regression) ✅
5. **Review code quality** ✅
6. **Update tracking checklist** ✅

### If Sub-Agent Fails
1. Identify the specific issue
2. Provide corrective guidance with examples
3. Deploy a new sub-agent with clearer instructions
4. Consider breaking task into smaller pieces

---

## Error Recovery & Debugging

### Common Issues

**Issue**: Tests don't compile
- **Fix**: Ensure forward declarations and includes are correct
- **Check**: `#include <wallet/walletdb.h>`, `#include <wallet/digidollarwallet.h>`

**Issue**: Serialization fails
- **Fix**: Verify SERIALIZE_METHODS includes ALL member variables
- **Check**: Round-trip test (write → read → verify equality)

**Issue**: Database cursor iteration fails
- **Fix**: Check cursor status in while loop: `while (status == DatabaseCursor::Status::MORE)`
- **Check**: Properly deserialize key and value from DataStream

**Issue**: Data doesn't persist
- **Fix**: Verify `WriteIC()` returns true
- **Check**: Database batch is flushed (automatic on destruction)

**Issue**: Wallet loading doesn't call LoadFromDatabase()
- **Fix**: Ensure hook is in correct location in wallet initialization
- **Check**: Add LogPrintf to verify function is called

---

## Integration Points to Monitor

1. **Consensus**: No changes to consensus - this is wallet-only
2. **Existing DD Code**: Don't break existing mint/transfer/redeem logic
3. **Wallet Database**: Compatible with BerkeleyDB AND SQLite
4. **GUI**: Positions should show in vault manager after restart
5. **RPC**: `listddpositions` should return persisted data

---

## Progress Reporting

After each phase, update this summary:

### Overall Progress
- **Phase 1 (Schema)**: ⏳ Not Started / 🔄 In Progress / ✅ Complete
- **Phase 2 (WalletBatch)**: ⏳ Not Started / 🔄 In Progress / ✅ Complete
- **Phase 3 (Integration)**: ⏳ Not Started / 🔄 In Progress / ✅ Complete
- **Phase 4 (Loading)**: ⏳ Not Started / 🔄 In Progress / ✅ Complete
- **Phase 5 (Hooks)**: ⏳ Not Started / 🔄 In Progress / ✅ Complete
- **Phase 6 (Blocks)**: ⏳ Not Started / 🔄 In Progress / ✅ Complete
- **Phase 7 (Testing)**: ⏳ Not Started / 🔄 In Progress / ✅ Complete
- **Phase 8 (Bonus)**: ⏳ Not Started / 🔄 In Progress / ✅ Complete

### Test Results
- **Unit Tests**: [X passed / Y failed]
- **Integration Tests**: [X passed / Y failed]
- **Critical Restart Test**: ❌ Not Run / 🔄 Running / ✅ PASSED

---

## Final Checklist Before Production

Before marking this feature complete:

- [ ] All 8 phases complete
- [ ] All unit tests passing
- [ ] All functional tests passing
- [ ] **Critical restart test passes** (mint → restart → position exists)
- [ ] wallet.dat export/import works
- [ ] No regression in existing DigiDollar functionality
- [ ] No regression in normal DigiByte wallet functionality
- [ ] Code compiles without warnings
- [ ] Test coverage > 80%
- [ ] Documentation updated
- [ ] Code reviewed and refactored

---

## Starting Your Work

**FIRST STEPS:**
1. Read `DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md` completely
2. Review existing database code patterns in `src/wallet/walletdb.cpp`
3. Review existing DigiDollar wallet code in `src/wallet/digidollarwallet.cpp`
4. Prepare Phase 1, Task 1.1 sub-agent deployment
5. Deploy first sub-agent with TDD template

**Remember**: You are coordinating this implementation, not doing it yourself. Each sub-agent handles ONE specific task. Your job is to ensure:
- Tasks are clearly defined
- TDD is followed religiously
- Tests pass after each task
- Code compiles after each task
- No regression occurs
- Progress is tracked

**The success of DigiDollar persistence depends on your careful orchestration. Take your time, be methodical, and ensure quality at every step.**

Begin by preparing your first sub-agent deployment for **Phase 1, Task 1.1: Add DD Database Keys**.
