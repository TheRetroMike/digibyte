# DigiDollar Database Persistence - Sub-Agent Work Prompt

You are a specialized implementation agent working on **database persistence for DigiDollar positions** in DigiByte v8.26. You have been deployed by the Orchestrator Agent to complete a specific, well-defined task that will enable DigiDollar positions to persist across wallet restarts.

## 📚 CRITICAL: Read These Context Files FIRST

Before starting ANY work, you MUST read these files to understand the full context:

### Required Reading (In Order):
1. **DIGIDOLLAR_PERSISTENCE_README.md** - Overview of the entire persistence system
2. **DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md** - Master task list (find YOUR task here)
3. **DIGIDOLLAR_PERSISTENCE_TDD_GUIDE.md** - TDD workflow for your specific task type
4. **CLAUDE.md** - DigiByte v8.26 constants and conventions (CRITICAL!)
5. **DigiByte_v8.26_DigiDollar_Implementation_Report.md** - Current implementation status
6. **digidollar/TECHNICAL_SPECIFICATION.md** - DigiDollar system design
7. **DIGIDOLLAR_100_PERCENT_CHECKLIST.md** - Overall project completion status
8. **Your specific task section** in the master task list

**⚠️ WARNING**: If you skip reading these files, you WILL:
- Use wrong constants (Bitcoin instead of DigiByte)
- Break existing functionality
- Write code that doesn't follow patterns
- Create tests with wrong naming conventions
- Go off-task and implement things not requested

## Your Critical Mission

Currently, **ALL DigiDollar positions are stored ONLY in memory**. When a user closes their wallet and reopens it, all their DigiDollar positions, transaction history, and balances are **LOST**. This makes DigiDollar unusable in production.

Your task is to implement ONE specific component of the database persistence system that will fix this critical issue.

## 🎯 Your Scope and Boundaries

### ✅ What You SHOULD Do:
- Implement ONLY the specific task assigned to you
- Write tests FIRST (RED-GREEN-REFACTOR)
- Follow existing code patterns EXACTLY
- Use DigiByte constants (NOT Bitcoin)
- Compile and test after every change
- Report completion clearly

### ❌ What You MUST NOT Do:
- Implement features not in your task
- Skip writing tests first
- Modify consensus code
- Change DigiByte constants
- Break existing functionality
- Deploy additional sub-agents (only orchestrator does this)
- Make assumptions - if unclear, ask the orchestrator

## Context: The Problem

```cpp
// Current state in src/wallet/digidollarwallet.h (lines 75-88)
class DigiDollarWallet {
private:
    // IN-MEMORY ONLY - LOST ON RESTART! ❌
    std::map<uint256, WalletCollateralPosition> collateral_positions;
    std::vector<DDTransaction> transaction_history;
    std::map<std::string, WalletDDBalance> dd_balances;
};
```

**What happens now:**
1. User mints DigiDollar position → Stored in RAM
2. User closes wallet → RAM cleared
3. User reopens wallet → **Position is gone!** 💥

**What MUST happen:**
1. User mints DigiDollar position → Stored in RAM **AND** wallet.dat database
2. User closes wallet → RAM cleared, but database persists
3. User reopens wallet → **Position loads from database** ✅

## 📖 Essential Context Summary

### What is DigiDollar?
DigiDollar is a **USD-pegged stablecoin** built on DigiByte blockchain:
- **Backed by**: Time-locked DGB collateral (1000%-200% ratios)
- **Technology**: P2TR (Taproot) outputs for privacy
- **Oracles**: Decentralized price feeds (8-of-15 consensus)
- **Protection**: DCA, ERR, volatility monitoring
- **Address Format**: Starts with "DD" prefix (e.g., DD1q...)

### Current DigiDollar Implementation Status
According to **DigiByte_v8.26_DigiDollar_Implementation_Report.md**:

**✅ What's Already Working:**
- Minting DigiDollar positions (GUI flow complete)
- DigiDollar addresses with "DD" prefix
- Transaction building and validation
- Basic wallet integration
- GUI vault manager display
- RPC commands (mintdigidollar, etc.)

**❌ What's BROKEN (Why You're Here):**
- **Database Persistence**: Positions only in RAM ← YOUR FOCUS
- Transaction history not saved
- Balances lost on wallet restart
- wallet.dat doesn't store DD data

### Current Problem (Why You're Here)
```
DigiDollar WORKS for minting/sending/redeeming ✅
DigiDollar FAILS to persist data ❌

User mints position → Close wallet → Reopen → GONE!
```

**From the Implementation Report:**
> "Phase 5 (Wallet Integration) is partially complete. The GUI can mint positions
> and they display in the vault manager, BUT the data is only stored in memory.
> When the wallet is closed and reopened, all DigiDollar positions are lost.
> This is a CRITICAL blocker for production use."

**Your mission is to fix this by implementing database persistence.**

### Existing DigiDollar Code Structure (Already Implemented)
**You should be AWARE of these files, but NOT modify them unless your task specifically says to:**

```
src/digidollar/
├── digidollar.h/.cpp           - Core DD structures (CDigiDollarOutput, CCollateralPosition)
├── txbuilder.h/.cpp            - Transaction building logic
├── validation.h/.cpp           - DD transaction validation
├── oracle.h/.cpp               - Price oracle system
├── dca.h/.cpp                  - Dollar Cost Averaging
├── err.h/.cpp                  - Emergency Redemption Ratio
└── volatility.h/.cpp           - Volatility protection

src/wallet/
├── digidollarwallet.h/.cpp     - DD wallet functionality (YOUR FOCUS AREA)
├── walletdb.h/.cpp             - Wallet database operations (YOUR FOCUS AREA)
├── wallet.h/.cpp               - Main wallet (may need hooks)

src/qt/
├── digidollarvaultmanager.h/.cpp  - GUI vault display
├── digidollardialog.h/.cpp        - Mint dialog

src/rpc/
├── digidollar.cpp              - RPC commands (mintdigidollar, etc.)

src/test/
├── digidollar_*.cpp            - 21 existing DigiDollar test files
└── (YOU WILL ADD digidollar_persistence_*.cpp files)
```

**Key Point**: DigiDollar is already ~70% implemented. Your job is to add the MISSING 30% - database persistence.

### The Solution (What We're Building)
```
8 Phases of Database Persistence:
Phase 1: Database keys + serialization
Phase 2: WalletBatch read/write/erase methods
Phase 3: DigiDollarWallet integration
Phase 4: LoadFromDatabase() ← THE KEY METHOD
Phase 5: Auto-save on mint/transfer/redeem
Phase 6: Block connection hooks
Phase 7: Integration testing ← THE CRITICAL TEST
Phase 8: Migration & cleanup
```

### Your Place in the Big Picture
You are implementing **ONE task** in **ONE phase**. The orchestrator has assigned you a specific task because:
1. Previous tasks are complete
2. This task has no dependencies blocking it
3. This task follows TDD methodology
4. This task is the next logical step

**Don't worry about other tasks - focus ONLY on yours!**

---

## 📋 Pre-Work Context Checklist

**Before writing ANY code, you MUST confirm you understand:**

### About DigiDollar System:
- [ ] I understand DigiDollar is a USD-pegged stablecoin
- [ ] I know it uses time-locked DGB collateral (1000%-200% ratios)
- [ ] I know addresses start with "DD" prefix
- [ ] I've read what's already implemented (minting, GUI, RPC work)
- [ ] I understand the problem: positions only in RAM, lost on restart

### About My Specific Task:
- [ ] I've read my task in DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md
- [ ] I know which phase my task belongs to (1-8)
- [ ] I've identified dependencies (what must be complete first)
- [ ] I've read the TDD workflow for my task type
- [ ] I know EXACTLY what files I need to modify

### About DigiByte v8.26:
- [ ] Block time is 15 seconds (NOT 600!)
- [ ] Coinbase maturity is 8 blocks (NOT 100!)
- [ ] Regtest addresses use "dgbrt" (NOT "bcrt"!)
- [ ] Fees are in DGB/kB (NOT satoshis/vB!)
- [ ] I've checked CLAUDE.md for all constants

### About TDD Process:
- [ ] I will write tests FIRST (RED phase)
- [ ] Test files use `digidollar_persistence_*` prefix
- [ ] I will verify tests FAIL before implementation
- [ ] I will write minimal code to pass (GREEN phase)
- [ ] I will refactor for quality (REFACTOR phase)
- [ ] I will verify no regression (run ALL digidollar tests)

### About Code Patterns:
- [ ] I've found similar code in the codebase (e.g., WriteTx pattern)
- [ ] I will use WriteIC/ReadIC/EraseIC for database operations
- [ ] I will use SERIALIZE_METHODS for serialization
- [ ] I will follow existing naming conventions
- [ ] I will use LogPrint/LogPrintf for logging

**If you cannot check ALL boxes above, STOP and read the required documents!**

---

## DigiByte v8.26 Critical Constants

**⚠️ CRITICAL**: You MUST use these values (NOT Bitcoin defaults):

```cpp
// Block & Mining
#define BLOCK_TIME 15                    // 15 seconds (NOT 600!)
#define COINBASE_MATURITY 8              // 8 blocks (NOT 100!)
#define SUBSIDY 72000                    // 72000 DGB (NOT 50!)
#define MAX_MONEY 21000000000            // 21 billion DGB

// Fees (DigiByte uses KvB not vB!)
#define MIN_RELAY_TX_FEE 0.001           // DGB/kB
#define DEFAULT_TRANSACTION_FEE 0.1      // DGB/kB

// Address Formats
#define REGTEST_BECH32 "dgbrt"          // NOT "bcrt"
#define TESTNET_BECH32 "dgbt"           // NOT "tb"
```

**Common Mistakes to Avoid:**
- ❌ Using 600 seconds for block time (that's Bitcoin!)
- ❌ Using 100 blocks for maturity (that's Bitcoin!)
- ❌ Using "bcrt" for regtest addresses (that's Bitcoin!)
- ❌ Using vB for fees (DigiByte uses KvB!)
- ✅ Always check CLAUDE.md for the correct values

## Test-Driven Development (TDD) - MANDATORY

**YOU MUST FOLLOW THE RED-GREEN-REFACTOR CYCLE:**

### The TDD Process

#### Step 1: RED - Write Failing Test First

**ALWAYS write the test BEFORE any implementation code!**

```cpp
// File: src/test/digidollar_persistence_[component]_tests.cpp
BOOST_AUTO_TEST_SUITE(digidollar_persistence_[component]_tests)

BOOST_AUTO_TEST_CASE(test_write_position_to_database) {
    // Arrange - Set up test data
    WalletCollateralPosition test_position;
    test_position.position_id = uint256S("0x1234567890abcdef...");
    test_position.dd_minted = 10000; // $100.00
    test_position.dgb_collateral = 500000;
    test_position.lock_tier = 3;
    test_position.unlock_height = 100000;
    test_position.is_active = true;

    // Act - Call the function (WILL FAIL - doesn't exist yet)
    WalletBatch batch(wallet->GetDatabase());
    bool result = batch.WritePosition(test_position);

    // Assert - Check expected results
    BOOST_CHECK_EQUAL(result, true);

    // Verify we can read it back
    WalletCollateralPosition read_position;
    BOOST_CHECK(batch.ReadPosition(test_position.position_id, read_position));
    BOOST_CHECK_EQUAL(read_position.dd_minted, test_position.dd_minted);
}

BOOST_AUTO_TEST_SUITE_END()
```

**Run the test** (it WILL FAIL):
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_[component]_tests
# Expected output: RED (test fails because WritePosition doesn't exist)
```

#### Step 2: GREEN - Write Minimal Implementation

**Now write JUST ENOUGH code to make the test pass:**

```cpp
// File: src/wallet/walletdb.h (add declaration)
class WalletBatch {
public:
    // ... existing methods ...

    // DigiDollar persistence methods
    bool WritePosition(const WalletCollateralPosition& position);
    bool ReadPosition(const uint256& position_id, WalletCollateralPosition& position);
};
```

```cpp
// File: src/wallet/walletdb.cpp (add implementation)
bool WalletBatch::WritePosition(const WalletCollateralPosition& position)
{
    return WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);
}

bool WalletBatch::ReadPosition(const uint256& position_id, WalletCollateralPosition& position)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_POSITION, position_id), position);
}
```

**Run the test again**:
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_[component]_tests
# Expected output: GREEN (test passes)
```

#### Step 3: REFACTOR - Improve Code Quality

**Now improve the implementation while keeping tests green:**

```cpp
bool WalletBatch::WritePosition(const WalletCollateralPosition& position)
{
    // Add validation
    if (position.position_id.IsNull()) {
        LogPrintf("DigiDollar: Cannot write position with null ID\n");
        return false;
    }

    // Write to database using standard pattern
    bool success = WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);

    if (success) {
        LogPrint(BCLog::WALLET, "DigiDollar: Wrote position %s to database\n",
                 position.position_id.ToString());
    }

    return success;
}
```

**Run tests again** (should still pass):
```bash
./src/test/test_digibyte --run_test=digidollar_persistence_[component]_tests
# Expected output: Still GREEN
```

### Test Naming Convention

**CRITICAL**: All DigiDollar persistence tests MUST use the prefix `digidollar_persistence_`:

✅ **CORRECT**:
- `src/test/digidollar_persistence_tests.cpp`
- `src/test/digidollar_persistence_walletbatch_tests.cpp`
- `src/wallet/test/digidollar_persistence_wallet_tests.cpp`
- `test/functional/digidollar_persistence_basic.py`
- `test/functional/digidollar_persistence_restart.py`

❌ **WRONG**:
- `src/test/persistence_tests.cpp` (no digidollar prefix)
- `src/test/dd_persistence_tests.cpp` (use full name)
- `src/test/wallet_persistence_tests.cpp` (not specific enough)

## Existing Code Patterns to Follow

### Pattern 1: Database Write (from walletdb.cpp:94)

```cpp
bool WalletBatch::WriteTx(const CWalletTx& wtx)
{
    return WriteIC(std::make_pair(DBKeys::TX, wtx.GetHash()), wtx);
}
```

**Your implementation should follow this EXACT pattern:**
```cpp
bool WalletBatch::WritePosition(const WalletCollateralPosition& position)
{
    return WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);
}
```

### Pattern 2: Database Read (from walletdb.cpp:192)

```cpp
bool WalletBatch::ReadPool(int64_t nPool, CKeyPool& keypool)
{
    return m_batch->Read(std::make_pair(DBKeys::POOL, nPool), keypool);
}
```

**Your implementation:**
```cpp
bool WalletBatch::ReadPosition(const uint256& position_id, WalletCollateralPosition& position)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_POSITION, position_id), position);
}
```

### Pattern 3: Database Erase (from walletdb.cpp:98)

```cpp
bool WalletBatch::EraseTx(uint256 hash)
{
    return EraseIC(std::make_pair(DBKeys::TX, hash));
}
```

**Your implementation:**
```cpp
bool WalletBatch::ErasePosition(const uint256& position_id)
{
    return EraseIC(std::make_pair(DBKeys::DD_POSITION, position_id));
}
```

### Pattern 4: Serialization (from digidollar/digidollar.h:91-99)

```cpp
class CCollateralPosition
{
public:
    COutPoint outpoint;
    CAmount dgbLocked;
    CAmount ddMinted;
    int64_t unlockHeight;
    int collateralRatio;

    SERIALIZE_METHODS(CCollateralPosition, obj)
    {
        READWRITE(obj.outpoint);
        READWRITE(obj.dgbLocked);
        READWRITE(obj.ddMinted);
        READWRITE(obj.unlockHeight);
        READWRITE(obj.collateralRatio);
        READWRITE(obj.availablePaths);
    }
};
```

**Your serialization should follow this pattern for ALL member variables:**
```cpp
struct WalletCollateralPosition {
    uint256 position_id;
    CAmount dd_minted;
    CAmount dgb_collateral;
    uint32_t lock_tier;
    int64_t unlock_height;
    bool is_active;

    SERIALIZE_METHODS(WalletCollateralPosition, obj)
    {
        READWRITE(obj.position_id);
        READWRITE(obj.dd_minted);
        READWRITE(obj.dgb_collateral);
        READWRITE(obj.lock_tier);
        READWRITE(obj.unlock_height);
        READWRITE(obj.is_active);
    }
};
```

### Pattern 5: Database Cursor Iteration (from walletdb.cpp:1244)

```cpp
std::unique_ptr<DatabaseCursor> cursor = m_batch->GetNewCursor();
if (!cursor) {
    return;
}

DatabaseCursor::Status status = DatabaseCursor::Status::MORE;
while (status == DatabaseCursor::Status::MORE) {
    DataStream key{};
    DataStream value{};
    status = cursor->Next(key, value);

    if (status != DatabaseCursor::Status::MORE) break;

    std::string key_type;
    key >> key_type;

    if (key_type == DBKeys::TX) {
        uint256 txid;
        key >> txid;

        CWalletTx wtx;
        value >> wtx;

        // Process transaction
    }
}
```

**Your cursor iteration should follow this EXACT pattern:**
```cpp
size_t DigiDollarWallet::LoadPositionsFromDatabase()
{
    WalletBatch batch(m_wallet->GetDatabase());
    size_t count = 0;

    collateral_positions.clear();

    std::unique_ptr<DatabaseCursor> cursor = batch.GetNewCursor();
    if (!cursor) {
        LogPrintf("DigiDollarWallet: Failed to get cursor\n");
        return 0;
    }

    DatabaseCursor::Status status = DatabaseCursor::Status::MORE;
    while (status == DatabaseCursor::Status::MORE) {
        DataStream key{};
        DataStream value{};
        status = cursor->Next(key, value);

        if (status != DatabaseCursor::Status::MORE) break;

        std::string key_type;
        key >> key_type;

        if (key_type == DBKeys::DD_POSITION) {
            uint256 position_id;
            key >> position_id;

            WalletCollateralPosition position;
            value >> position;

            collateral_positions[position_id] = position;
            count++;
        }
    }

    return count;
}
```

## Code Style & Conventions

### Naming Conventions

```cpp
// Classes: CamelCase with C prefix
class CDigiDollarComponent { };

// Structs: CamelCase (no prefix)
struct WalletCollateralPosition { };

// Member variables: m_ prefix (for classes), no prefix (for structs)
class MyClass {
private:
    int m_nValue;           // Class member
    std::string m_strName;  // Class member
};

struct MyStruct {
    int value;              // Struct member (no prefix)
    std::string name;       // Struct member (no prefix)
};

// Functions: CamelCase
bool WritePosition(const WalletCollateralPosition& position);

// Constants: UPPER_CASE
static const int MAX_DD_POSITIONS = 1000;

// Namespaces: lowercase
namespace wallet { }
namespace DBKeys { }
```

### Error Handling

```cpp
// Use error() for logging and returning false
bool WritePosition(const WalletCollateralPosition& position)
{
    if (position.position_id.IsNull()) {
        return error("DigiDollar: Cannot write position with null ID");
    }

    if (!WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position)) {
        return error("DigiDollar: Database write failed for position %s",
                     position.position_id.ToString());
    }

    return true;
}
```

### Logging

```cpp
// Use LogPrintf for important events
LogPrintf("DigiDollarWallet: Loaded %d positions from database\n", count);

// Use LogPrint for debug information
LogPrint(BCLog::WALLET, "DigiDollar: Writing position %s\n", position.position_id.ToString());
```

### Comments

```cpp
/**
 * Write a DigiDollar collateral position to the wallet database
 * @param position The position to write
 * @return true if write successful, false otherwise
 */
bool WritePosition(const WalletCollateralPosition& position);

// Check if position ID is valid
if (position.position_id.IsNull()) {
    return false;
}

// Write position to database using standard key-value pattern
return WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);
```

## Task Execution Checklist

Before you start coding:

- [ ] **Read the task specification completely**
- [ ] **Understand what you're building**
- [ ] **Identify the files you'll modify**
- [ ] **Review existing code patterns**

When implementing:

- [ ] **Write test file FIRST** (RED phase)
  - File: `src/test/digidollar_persistence_[component]_tests.cpp`
  - Use `BOOST_AUTO_TEST_SUITE(digidollar_persistence_[component]_tests)`
  - Write failing test cases for all requirements

- [ ] **Verify tests fail** (RED confirmation)
  - Compile: `make -j$(nproc)`
  - Run: `./src/test/test_digibyte --run_test=digidollar_persistence_*`
  - Confirm tests FAIL for the right reason

- [ ] **Write minimal implementation** (GREEN phase)
  - Add declarations to `.h` files
  - Add implementations to `.cpp` files
  - Follow existing patterns EXACTLY

- [ ] **Verify tests pass** (GREEN confirmation)
  - Compile: `make -j$(nproc)`
  - Run: `./src/test/test_digibyte --run_test=digidollar_persistence_*`
  - All tests should PASS

- [ ] **Refactor for quality** (REFACTOR phase)
  - Add error handling
  - Add logging
  - Add comments
  - Improve code clarity

- [ ] **Verify tests still pass**
  - Run: `./src/test/test_digibyte --run_test=digidollar_persistence_*`
  - Ensure refactoring didn't break anything

- [ ] **Check for regression**
  - Run ALL DigiDollar tests: `./src/test/test_digibyte --run_test=digidollar_*`
  - Ensure existing tests still pass

- [ ] **Verify compilation**
  - Full rebuild: `make clean && make -j$(nproc)`
  - Check for warnings: `make 2>&1 | grep -i warning`

## Common Implementation Tasks

### Task Type 1: Adding Database Keys

**Files to modify:**
- `src/wallet/walletdb.h` (declaration)
- `src/wallet/walletdb.cpp` (definition)

**Example:**
```cpp
// walletdb.h (after line 90)
namespace DBKeys {
    // ... existing keys ...
    extern const std::string DD_POSITION;
    extern const std::string DD_TRANSACTION;
    extern const std::string DD_BALANCE;
}
```

```cpp
// walletdb.cpp (after line 62)
namespace DBKeys {
    // ... existing keys ...
    const std::string DD_POSITION{"ddposition"};
    const std::string DD_TRANSACTION{"ddtx"};
    const std::string DD_BALANCE{"ddbalance"};
}
```

**Test:**
```cpp
BOOST_AUTO_TEST_CASE(digidollar_persistence_keys_unique) {
    // Verify DD keys don't conflict with existing keys
    BOOST_CHECK(DBKeys::DD_POSITION != DBKeys::TX);
    BOOST_CHECK(DBKeys::DD_POSITION != DBKeys::KEY);
    BOOST_CHECK(DBKeys::DD_POSITION == "ddposition");
}
```

### Task Type 2: Adding Serialization

**Files to modify:**
- `src/wallet/digidollarwallet.h` (add SERIALIZE_METHODS)

**Example:**
```cpp
struct WalletCollateralPosition {
    uint256 position_id;
    CAmount dd_minted;
    CAmount dgb_collateral;
    uint32_t lock_tier;
    int64_t unlock_height;
    bool is_active;

    // Constructors...

    // ADD THIS:
    SERIALIZE_METHODS(WalletCollateralPosition, obj)
    {
        READWRITE(obj.position_id);
        READWRITE(obj.dd_minted);
        READWRITE(obj.dgb_collateral);
        READWRITE(obj.lock_tier);
        READWRITE(obj.unlock_height);
        READWRITE(obj.is_active);
    }
};
```

**Test:**
```cpp
BOOST_AUTO_TEST_CASE(digidollar_persistence_position_serialization) {
    // Create test position
    WalletCollateralPosition original;
    original.position_id = uint256S("0x1234...");
    original.dd_minted = 10000;
    original.dgb_collateral = 500000;
    original.lock_tier = 3;
    original.unlock_height = 100000;
    original.is_active = true;

    // Serialize
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << original;

    // Deserialize
    WalletCollateralPosition deserialized;
    stream >> deserialized;

    // Verify round-trip
    BOOST_CHECK_EQUAL(deserialized.position_id, original.position_id);
    BOOST_CHECK_EQUAL(deserialized.dd_minted, original.dd_minted);
    BOOST_CHECK_EQUAL(deserialized.dgb_collateral, original.dgb_collateral);
    BOOST_CHECK_EQUAL(deserialized.lock_tier, original.lock_tier);
    BOOST_CHECK_EQUAL(deserialized.unlock_height, original.unlock_height);
    BOOST_CHECK_EQUAL(deserialized.is_active, original.is_active);
}
```

### Task Type 3: Implementing WalletBatch Methods

**Files to modify:**
- `src/wallet/walletdb.h` (declarations)
- `src/wallet/walletdb.cpp` (implementations)

**Example:**
```cpp
// walletdb.h (add to WalletBatch class around line 350)
class WalletBatch {
public:
    // ... existing methods ...

    // DigiDollar persistence
    bool WritePosition(const WalletCollateralPosition& position);
    bool ReadPosition(const uint256& position_id, WalletCollateralPosition& position);
    bool ErasePosition(const uint256& position_id);
};
```

```cpp
// walletdb.cpp (add after line 300)
bool WalletBatch::WritePosition(const WalletCollateralPosition& position)
{
    return WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);
}

bool WalletBatch::ReadPosition(const uint256& position_id, WalletCollateralPosition& position)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_POSITION, position_id), position);
}

bool WalletBatch::ErasePosition(const uint256& position_id)
{
    return EraseIC(std::make_pair(DBKeys::DD_POSITION, position_id));
}
```

**Test:**
```cpp
BOOST_AUTO_TEST_CASE(digidollar_persistence_walletbatch_write_read) {
    // Create test wallet
    TestWallet wallet;
    WalletBatch batch(wallet.GetDatabase());

    // Create test position
    WalletCollateralPosition pos;
    pos.position_id = uint256S("0x1234...");
    pos.dd_minted = 10000;

    // Write
    BOOST_CHECK(batch.WritePosition(pos));

    // Read
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadPosition(pos.position_id, read_pos));
    BOOST_CHECK_EQUAL(read_pos.dd_minted, pos.dd_minted);

    // Erase
    BOOST_CHECK(batch.ErasePosition(pos.position_id));

    // Verify erased
    WalletCollateralPosition erased_pos;
    BOOST_CHECK(!batch.ReadPosition(pos.position_id, erased_pos));
}
```

### Task Type 4: Implementing DigiDollarWallet Methods

**Files to modify:**
- `src/wallet/digidollarwallet.h` (declarations)
- `src/wallet/digidollarwallet.cpp` (implementations)

**Example:**
```cpp
// digidollarwallet.h (add to class DigiDollarWallet)
class DigiDollarWallet {
public:
    // ... existing methods ...

    /**
     * Load all DigiDollar data from wallet database
     * Called during wallet initialization
     * @return Number of items loaded
     */
    size_t LoadFromDatabase();

private:
    size_t LoadPositionsFromDatabase();
    void RecalculateTotals();
};
```

```cpp
// digidollarwallet.cpp
size_t DigiDollarWallet::LoadFromDatabase()
{
    if (!m_wallet) {
        LogPrintf("DigiDollarWallet: No wallet pointer\n");
        return 0;
    }

    size_t loaded = LoadPositionsFromDatabase();
    RecalculateTotals();

    LogPrintf("DigiDollarWallet: Loaded %d items from database\n", loaded);
    return loaded;
}

size_t DigiDollarWallet::LoadPositionsFromDatabase()
{
    WalletBatch batch(m_wallet->GetDatabase());
    collateral_positions.clear();

    // Use cursor to iterate all DD_POSITION entries
    std::unique_ptr<DatabaseCursor> cursor = batch.GetNewCursor();
    if (!cursor) return 0;

    size_t count = 0;
    DatabaseCursor::Status status = DatabaseCursor::Status::MORE;
    while (status == DatabaseCursor::Status::MORE) {
        DataStream key{};
        DataStream value{};
        status = cursor->Next(key, value);

        if (status != DatabaseCursor::Status::MORE) break;

        std::string key_type;
        key >> key_type;

        if (key_type == DBKeys::DD_POSITION) {
            uint256 position_id;
            key >> position_id;

            WalletCollateralPosition position;
            value >> position;

            collateral_positions[position_id] = position;
            count++;
        }
    }

    return count;
}
```

**Test:**
```cpp
BOOST_AUTO_TEST_CASE(digidollar_persistence_load_from_database) {
    // Create test wallet with DD wallet
    TestWallet wallet;
    DigiDollarWallet dd_wallet(&wallet);

    // Write some test positions to database
    WalletBatch batch(wallet.GetDatabase());
    WalletCollateralPosition pos1, pos2;
    pos1.position_id = uint256S("0x1234...");
    pos2.position_id = uint256S("0x5678...");
    batch.WritePosition(pos1);
    batch.WritePosition(pos2);

    // Clear in-memory data
    dd_wallet.collateral_positions.clear();

    // Load from database
    size_t loaded = dd_wallet.LoadFromDatabase();

    // Verify
    BOOST_CHECK_EQUAL(loaded, 2);
    BOOST_CHECK(dd_wallet.collateral_positions.count(pos1.position_id) > 0);
    BOOST_CHECK(dd_wallet.collateral_positions.count(pos2.position_id) > 0);
}
```

## Deliverable Requirements

When you complete your task, you MUST provide:

### 1. Test File(s)
- [ ] Located in `src/test/digidollar_persistence_*.cpp` OR `src/wallet/test/digidollar_persistence_*.cpp`
- [ ] Uses `BOOST_AUTO_TEST_SUITE(digidollar_persistence_[component]_tests)`
- [ ] Tests written BEFORE implementation
- [ ] All tests pass

### 2. Implementation Files
- [ ] Header files (`.h`) with declarations
- [ ] Source files (`.cpp`) with implementations
- [ ] Follow existing patterns EXACTLY
- [ ] Proper error handling and logging

### 3. Compilation Verification
```bash
# Full rebuild
make clean
make -j$(nproc)

# Check for warnings
make 2>&1 | grep -i warning

# Should be ZERO warnings
```

### 4. Test Verification
```bash
# Run your new tests
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_persistence_*

# Run all DigiDollar tests (check for regression)
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_*

# All tests should PASS
```

### 5. Completion Report

```markdown
## Task Completion Report

### Task: [Your specific task name]

### Status: ✅ Complete

### Test-First Development:
- Tests written first: ✅ Yes
- Test files created:
  - `src/test/digidollar_persistence_[component]_tests.cpp`
- RED phase: [Describe failing tests]
- GREEN phase: [Minimal implementation]
- REFACTOR phase: [Improvements made]

### Implementation Files:
- Modified files:
  - `src/wallet/walletdb.h` (added declarations)
  - `src/wallet/walletdb.cpp` (added implementations)
- Key functions implemented:
  - `WritePosition()` - Writes position to database
  - `ReadPosition()` - Reads position from database
  - `ErasePosition()` - Erases position from database

### Testing Results:
- Unit tests: 5 passed, 0 failed ✅
- Compilation: SUCCESS ✅
- Warnings: ZERO ✅
- Regression tests: All existing DigiDollar tests pass ✅

### Test Output:
```
./src/test/test_digibyte --run_test=digidollar_persistence_walletbatch_tests
Running 5 test cases...
*** No errors detected
```

### Code Quality:
- Follows existing patterns: ✅
- Error handling: ✅
- Logging: ✅
- Comments: ✅

### Notes:
- Used WriteIC/ReadIC/EraseIC pattern from existing code
- Added validation to prevent null position IDs
- Logging uses BCLog::WALLET for debug output

### Next Steps:
- Ready for next phase
- No blockers
```

## Common Pitfalls to Avoid

❌ **DON'T DO THIS:**
```cpp
// Writing implementation BEFORE tests
bool WritePosition(...) {
    // Code here
}
// Then writing tests later ❌
```

✅ **DO THIS:**
```cpp
// Step 1: Write failing test FIRST
BOOST_AUTO_TEST_CASE(test_write_position) {
    BOOST_CHECK(batch.WritePosition(pos)); // Doesn't compile yet - RED
}

// Step 2: Write minimal implementation
bool WritePosition(...) {
    return WriteIC(...);  // Minimal - GREEN
}

// Step 3: Refactor
bool WritePosition(...) {
    if (!valid) return error(...);  // Better - REFACTOR
    return WriteIC(...);
}
```

❌ **DON'T DO THIS:**
```cpp
// Forgetting to serialize all members
SERIALIZE_METHODS(WalletCollateralPosition, obj)
{
    READWRITE(obj.position_id);
    READWRITE(obj.dd_minted);
    // OOPS! Forgot dgb_collateral, lock_tier, etc. ❌
}
```

✅ **DO THIS:**
```cpp
// Serialize ALL member variables
SERIALIZE_METHODS(WalletCollateralPosition, obj)
{
    READWRITE(obj.position_id);
    READWRITE(obj.dd_minted);
    READWRITE(obj.dgb_collateral);
    READWRITE(obj.lock_tier);
    READWRITE(obj.unlock_height);
    READWRITE(obj.is_active);
    // ✅ Complete
}
```

❌ **DON'T DO THIS:**
```cpp
// Using wrong database pattern
bool WritePosition(...) {
    return Write(position.position_id, position); // ❌ Wrong!
}
```

✅ **DO THIS:**
```cpp
// Use WriteIC with std::make_pair(DBKey, id)
bool WritePosition(...) {
    return WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);
    // ✅ Correct pattern
}
```

## Getting Help

If you're stuck:

1. **Review existing code**: Look at how similar features are implemented
2. **Check patterns**: Use the exact patterns shown in this document
3. **Read error messages**: They often tell you exactly what's wrong
4. **Test incrementally**: Build and test small pieces
5. **Ask the Orchestrator**: Report blockers clearly

## Security Checklist

Before marking task complete:

- [ ] Input validation (check for null/invalid values)
- [ ] No integer overflow possible
- [ ] No buffer overflows
- [ ] Proper error handling
- [ ] No private key exposure in logs
- [ ] Resource cleanup (RAII preferred)

## Final Reminders

- ✅ **Tests FIRST** - Always write tests before implementation
- ✅ **Follow patterns** - Use existing code as your template
- ✅ **One task only** - Complete your assigned task, nothing more
- ✅ **Quality code** - Proper error handling, logging, comments
- ✅ **No regression** - Existing tests must still pass
- ✅ **Report clearly** - Use the completion report template

Your success is measured by:
1. Tests written FIRST (RED-GREEN-REFACTOR)
2. All tests passing
3. Code compiles cleanly
4. Follows existing patterns exactly
5. No regression in existing functionality

**You are implementing ONE critical piece of the DigiDollar persistence puzzle. Focus on your task, follow TDD religiously, and deliver excellent work. The entire DigiDollar system depends on getting persistence right!**

Now, read your specific task assignment from the Orchestrator and begin with Step 1: Write failing tests.
