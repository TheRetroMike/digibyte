# DigiDollar Database Persistence Implementation Task List

**Status:** PLANNING COMPLETE - IMPLEMENTATION READY
**Priority:** CRITICAL - Blocks Production Use
**Estimated Effort:** 2-3 weeks
**Goal:** 100% persistent storage of DigiDollar positions in wallet.dat so they survive wallet restarts and re-imports
**Development Methodology:** RED-GREEN-REFACTOR Test-Driven Development (TDD)

---

## Executive Summary

Currently, all DigiDollar positions are stored **in-memory only** (see `src/wallet/digidollarwallet.h:75-88`). When a user closes the wallet and reopens it, or re-imports their wallet.dat file, all DigiDollar positions are **lost**. This task list provides a complete roadmap to implement full database persistence using DigiByte's existing BerkeleyDB/SQLite infrastructure, mirroring the proven patterns used for storing UTXOs, transactions, and keys.

## Test-Driven Development (TDD) Mandate

**CRITICAL**: Every task in this document MUST follow the RED-GREEN-REFACTOR cycle:

1. **🔴 RED Phase**: Write failing tests FIRST
2. **🟢 GREEN Phase**: Write minimal code to make tests pass
3. **🔵 REFACTOR Phase**: Improve code quality while keeping tests green
4. **✅ VERIFY**: Compile and run all tests to ensure no regression

### Test Naming Convention
ALL DigiDollar persistence tests MUST use the prefix `digidollar_persistence_`:
- Unit tests: `src/test/digidollar_persistence_*.cpp`
- Wallet tests: `src/wallet/test/digidollar_persistence_*.cpp`
- Functional tests: `test/functional/digidollar_persistence_*.py`

### Compilation & Testing After Every Task
```bash
# 1. Compile
make clean && make -j$(nproc)

# 2. Run new persistence tests
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_persistence_*

# 3. Run all DigiDollar tests (check for regression)
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_*

# 4. Check for warnings
make 2>&1 | grep -i warning

# 5. Run functional tests (when applicable)
./test/functional/digidollar_persistence_basic.py
```

## Current State Analysis

### What Exists Now (In-Memory Only)
```cpp
// src/wallet/digidollarwallet.h lines 75-88
class DigiDollarWallet {
private:
    // IN-MEMORY ONLY - LOST ON RESTART!
    CAmount mockBalance;
    std::vector<DDTransaction> mockHistory;
    std::vector<CDigiDollarOutput> mockUTXOs;
    std::map<std::string, WalletDDBalance> dd_balances;
    std::map<uint256, WalletCollateralPosition> collateral_positions;  // CRITICAL: Not persisted
    std::vector<DDTransaction> transaction_history;                     // CRITICAL: Not persisted
    CAmount total_dd_balance;
    CAmount locked_collateral;
    wallet::CWallet* m_wallet;
};
```

### What Needs to Persist
1. **DigiDollar Positions** (`WalletCollateralPosition`) - Mint transactions with locked DGB
2. **DigiDollar Transactions** (`DDTransaction`) - Send/receive/mint/redeem history
3. **DigiDollar Balances** (`WalletDDBalance`) - Balance per DD address
4. **DigiDollar Outputs** (`CDigiDollarOutput`) - Unspent DD UTXOs

### Database Infrastructure Available
- **BerkeleyDB** (legacy wallets) via `WalletBatch::WriteIC()` / `ReadIC()`
- **SQLite** (descriptor wallets) via same interface
- **Serialization** via `SERIALIZE_METHODS` macro (see `src/digidollar/digidollar.h:41-48, 91-99`)
- **Key-Value Pattern** via `std::make_pair(DBKeys::*, identifier)` (see `src/wallet/walletdb.cpp:72-299`)

---

## PHASE 1: Database Schema Design & Key Definitions (Week 1 - Days 1-2)

### Task 1.1: Add DigiDollar Database Keys

**🔴 RED Phase: Write Tests First**

**Test File:** `src/test/digidollar_persistence_keys_tests.cpp` (NEW FILE)

```cpp
#include <boost/test/unit_test.hpp>
#include <wallet/walletdb.h>

BOOST_AUTO_TEST_SUITE(digidollar_persistence_keys_tests)

BOOST_AUTO_TEST_CASE(digidollar_persistence_keys_exist)
{
    // Test that DD keys are defined
    BOOST_CHECK(!wallet::DBKeys::DD_POSITION.empty());
    BOOST_CHECK(!wallet::DBKeys::DD_TRANSACTION.empty());
    BOOST_CHECK(!wallet::DBKeys::DD_BALANCE.empty());
    BOOST_CHECK(!wallet::DBKeys::DD_OUTPUT.empty());
    BOOST_CHECK(!wallet::DBKeys::DD_METADATA.empty());
}

BOOST_AUTO_TEST_CASE(digidollar_persistence_keys_unique)
{
    // Verify DD keys don't conflict with existing keys
    BOOST_CHECK(wallet::DBKeys::DD_POSITION != wallet::DBKeys::TX);
    BOOST_CHECK(wallet::DBKeys::DD_POSITION != wallet::DBKeys::KEY);
    BOOST_CHECK(wallet::DBKeys::DD_POSITION != wallet::DBKeys::CSCRIPT);
    BOOST_CHECK(wallet::DBKeys::DD_TRANSACTION != wallet::DBKeys::TX);
    BOOST_CHECK(wallet::DBKeys::DD_BALANCE != wallet::DBKeys::NAME);
}

BOOST_AUTO_TEST_CASE(digidollar_persistence_keys_format)
{
    // Verify key naming convention (lowercase, descriptive)
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_POSITION, "ddposition");
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_TRANSACTION, "ddtx");
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_BALANCE, "ddbalance");
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_OUTPUT, "ddutxo");
    BOOST_CHECK_EQUAL(wallet::DBKeys::DD_METADATA, "ddmeta");
}

BOOST_AUTO_TEST_SUITE_END()
```

**Run tests (should FAIL):**
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_keys_tests
# Expected: Compilation errors - keys don't exist yet
```

---

**🟢 GREEN Phase: Minimal Implementation**

**File 1:** `src/wallet/walletdb.h` (after line 90 in `namespace DBKeys`)

```cpp
namespace DBKeys {
    // ... existing keys ...

    // DigiDollar database keys
    extern const std::string DD_POSITION;      // "ddposition" - Collateral positions
    extern const std::string DD_TRANSACTION;   // "ddtx"       - DD transaction history
    extern const std::string DD_BALANCE;       // "ddbalance"  - DD balance per address
    extern const std::string DD_OUTPUT;        // "ddutxo"     - DD UTXO tracking
    extern const std::string DD_METADATA;      // "ddmeta"     - DD wallet metadata
}
```

**File 2:** `src/wallet/walletdb.cpp` (after line 62 in `namespace DBKeys`)

```cpp
namespace DBKeys {
    // ... existing keys ...

    const std::string DD_POSITION{"ddposition"};
    const std::string DD_TRANSACTION{"ddtx"};
    const std::string DD_BALANCE{"ddbalance"};
    const std::string DD_OUTPUT{"ddutxo"};
    const std::string DD_METADATA{"ddmeta"};
}
```

**Run tests (should PASS):**
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_keys_tests
# Expected: All tests pass (GREEN)
```

---

**🔵 REFACTOR Phase:**

No refactoring needed for simple key definitions.

---

**✅ VERIFICATION:**
```bash
# 1. Compile cleanly
make clean && make -j$(nproc)

# 2. Tests pass
./src/test/test_digibyte --run_test=digidollar_persistence_keys_tests

# 3. No regression
./src/test/test_digibyte --run_test=digidollar_*

# 4. No warnings
make 2>&1 | grep -i warning
```

**Success Criteria:**
- [x] Tests written FIRST
- [x] Tests pass after implementation
- [x] Keys are unique (no conflicts)
- [x] Follow naming convention
- [x] Code compiles without warnings
- [x] No regression in existing tests

---

### Task 1.2: Add Serialization Support to DigiDollar Structures
**Files:** `src/wallet/digidollarwallet.h`

**Current state:** `WalletCollateralPosition` and `DDTransaction` lack serialization

**ADD serialization to WalletCollateralPosition (lines 54-66):**
```cpp
struct WalletCollateralPosition {
    uint256 position_id;
    CAmount dd_minted;
    CAmount dgb_collateral;
    uint32_t lock_tier;
    int64_t unlock_height;
    bool is_active;

    WalletCollateralPosition() : dd_minted(0), dgb_collateral(0), lock_tier(0),
                                  unlock_height(0), is_active(false) {}

    WalletCollateralPosition(const uint256& id, CAmount dd, CAmount dgb,
                            uint32_t tier, int64_t height)
        : position_id(id), dd_minted(dd), dgb_collateral(dgb),
          lock_tier(tier), unlock_height(height), is_active(true) {}

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

**ADD serialization to DDTransaction (lines 24-34):**
```cpp
struct DDTransaction {
    std::string txid;
    CAmount amount;
    uint64_t timestamp;
    int confirmations;
    bool incoming;
    std::string address;
    std::string category;

    DDTransaction() : amount(0), timestamp(0), confirmations(0), incoming(false) {}

    // ADD THIS:
    SERIALIZE_METHODS(DDTransaction, obj)
    {
        READWRITE(obj.txid);
        READWRITE(obj.amount);
        READWRITE(obj.timestamp);
        READWRITE(obj.confirmations);
        READWRITE(obj.incoming);
        READWRITE(obj.address);
        READWRITE(obj.category);
    }
};
```

**ADD serialization to WalletDDBalance (lines 44-52):**
```cpp
struct WalletDDBalance {
    CDigiDollarAddress address;
    CAmount balance;
    int64_t last_updated;

    WalletDDBalance() : balance(0), last_updated(0) {}
    WalletDDBalance(const CDigiDollarAddress& addr, CAmount bal)
        : address(addr), balance(bal), last_updated(0) {}

    // ADD THIS:
    SERIALIZE_METHODS(WalletDDBalance, obj)
    {
        READWRITE(obj.address);
        READWRITE(obj.balance);
        READWRITE(obj.last_updated);
    }
};
```

**Verification:**
- Structures can be serialized to binary format
- Deserialization reconstructs exact same data
- Compatible with BerkeleyDB and SQLite backends

---

## PHASE 2: WalletBatch Write Methods (Week 1 - Days 3-5)

### Task 2.1: Implement WalletBatch::WritePosition()
**File:** `src/wallet/walletdb.h`
**Location:** Add declaration around line 350 (in WalletBatch class)

**Pattern to follow:** `WriteTx()` (line 94 in walletdb.cpp)

**ADD to walletdb.h:**
```cpp
class WalletBatch {
public:
    // ... existing methods ...

    // DigiDollar database write methods
    bool WritePosition(const WalletCollateralPosition& position);
    bool WriteDDTransaction(const DDTransaction& ddtx);
    bool WriteDDBalance(const std::string& address, const WalletDDBalance& balance);
    bool WriteDDOutput(const uint256& output_id, const CDigiDollarOutput& output);
    bool WriteDDMetadata(const std::string& key, const std::string& value);
};
```

**ADD to walletdb.cpp** (after line 300):
```cpp
bool WalletBatch::WritePosition(const WalletCollateralPosition& position)
{
    // Store by position_id (txid of mint transaction)
    return WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);
}

bool WalletBatch::WriteDDTransaction(const DDTransaction& ddtx)
{
    // Store by txid
    uint256 txid;
    txid.SetHex(ddtx.txid);
    return WriteIC(std::make_pair(DBKeys::DD_TRANSACTION, txid), ddtx);
}

bool WalletBatch::WriteDDBalance(const std::string& address, const WalletDDBalance& balance)
{
    // Store by DD address string
    return WriteIC(std::make_pair(DBKeys::DD_BALANCE, address), balance);
}

bool WalletBatch::WriteDDOutput(const uint256& output_id, const CDigiDollarOutput& output)
{
    // Store by output hash (txid:vout)
    return WriteIC(std::make_pair(DBKeys::DD_OUTPUT, output_id), output);
}

bool WalletBatch::WriteDDMetadata(const std::string& key, const std::string& value)
{
    // Store metadata (e.g., "total_dd_balance", "locked_collateral")
    return WriteIC(std::make_pair(DBKeys::DD_METADATA, key), value);
}
```

**Verification:**
- Write operations return true on success
- Data is persisted to wallet.dat
- Keys are unique per entry

---

### Task 2.2: Implement WalletBatch Read Methods
**File:** `src/wallet/walletdb.cpp`
**Location:** After write methods (after line ~330)

**Pattern to follow:** `ReadPool()` (line 192 in walletdb.cpp)

**ADD to walletdb.h:**
```cpp
class WalletBatch {
public:
    // ... existing methods ...

    // DigiDollar database read methods
    bool ReadPosition(const uint256& position_id, WalletCollateralPosition& position);
    bool ReadDDTransaction(const uint256& txid, DDTransaction& ddtx);
    bool ReadDDBalance(const std::string& address, WalletDDBalance& balance);
    bool ReadDDOutput(const uint256& output_id, CDigiDollarOutput& output);
    bool ReadDDMetadata(const std::string& key, std::string& value);
};
```

**ADD to walletdb.cpp:**
```cpp
bool WalletBatch::ReadPosition(const uint256& position_id, WalletCollateralPosition& position)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_POSITION, position_id), position);
}

bool WalletBatch::ReadDDTransaction(const uint256& txid, DDTransaction& ddtx)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_TRANSACTION, txid), ddtx);
}

bool WalletBatch::ReadDDBalance(const std::string& address, WalletDDBalance& balance)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_BALANCE, address), balance);
}

bool WalletBatch::ReadDDOutput(const uint256& output_id, CDigiDollarOutput& output)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_OUTPUT, output_id), output);
}

bool WalletBatch::ReadDDMetadata(const std::string& key, std::string& value)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_METADATA, key), value);
}
```

**Verification:**
- Read operations populate passed-in references
- Return false if key not found
- Data matches what was written

---

### Task 2.3: Implement Erase Methods
**File:** `src/wallet/walletdb.cpp`
**Location:** After read methods

**Pattern to follow:** `EraseTx()` (line 98 in walletdb.cpp)

**ADD to walletdb.h:**
```cpp
class WalletBatch {
public:
    // ... existing methods ...

    // DigiDollar database erase methods
    bool ErasePosition(const uint256& position_id);
    bool EraseDDTransaction(const uint256& txid);
    bool EraseDDBalance(const std::string& address);
    bool EraseDDOutput(const uint256& output_id);
};
```

**ADD to walletdb.cpp:**
```cpp
bool WalletBatch::ErasePosition(const uint256& position_id)
{
    return EraseIC(std::make_pair(DBKeys::DD_POSITION, position_id));
}

bool WalletBatch::EraseDDTransaction(const uint256& txid)
{
    return EraseIC(std::make_pair(DBKeys::DD_TRANSACTION, txid));
}

bool WalletBatch::EraseDDBalance(const std::string& address)
{
    return EraseIC(std::make_pair(DBKeys::DD_BALANCE, address));
}

bool WalletBatch::EraseDDOutput(const uint256& output_id)
{
    return EraseIC(std::make_pair(DBKeys::DD_OUTPUT, output_id));
}
```

**Verification:**
- Erase operations remove entries from database
- Return true even if key didn't exist
- Subsequent reads return false

---

## PHASE 3: DigiDollarWallet Integration (Week 2 - Days 1-3)

### Task 3.1: Implement DigiDollarWallet::WriteDDBalance()
**File:** `src/wallet/digidollarwallet.cpp`
**Location:** Around line 50 (existing stub implementation)

**Current stub (lines 105-110 in digidollarwallet.h):**
```cpp
bool WriteDDBalance(const CDigiDollarAddress& addr, const CAmount& balance);
```

**REPLACE stub with full implementation:**
```cpp
bool DigiDollarWallet::WriteDDBalance(const CDigiDollarAddress& addr, const CAmount& balance)
{
    if (!m_wallet) {
        LogPrintf("DigiDollarWallet::WriteDDBalance - No wallet pointer set\n");
        return false;
    }

    // Create balance record
    WalletDDBalance bal_record(addr, balance);
    bal_record.last_updated = GetTime();

    // Get wallet database batch
    WalletBatch batch(m_wallet->GetDatabase());

    // Write to database using DD address as key
    std::string addr_str = addr.ToString();
    if (!batch.WriteDDBalance(addr_str, bal_record)) {
        LogPrintf("DigiDollarWallet::WriteDDBalance - Database write failed for %s\n", addr_str);
        return false;
    }

    // Update in-memory cache
    dd_balances[addr_str] = bal_record;

    // Update total balance metadata
    CAmount total = 0;
    for (const auto& [addr, bal] : dd_balances) {
        total += bal.balance;
    }
    total_dd_balance = total;
    batch.WriteDDMetadata("total_dd_balance", std::to_string(total));

    LogPrint(BCLog::WALLET, "DigiDollarWallet: Wrote balance %d for address %s\n", balance, addr_str);
    return true;
}
```

**Verification:**
- Balance is written to database
- In-memory cache is updated
- Total balance metadata is recalculated
- Returns true on success

---

### Task 3.2: Implement DigiDollarWallet::WritePosition()
**File:** `src/wallet/digidollarwallet.cpp`
**Location:** Around line 100

**Current stub (lines 112-117 in digidollarwallet.h):**
```cpp
bool WritePosition(const WalletCollateralPosition& position);
```

**IMPLEMENT:**
```cpp
bool DigiDollarWallet::WritePosition(const WalletCollateralPosition& position)
{
    if (!m_wallet) {
        LogPrintf("DigiDollarWallet::WritePosition - No wallet pointer set\n");
        return false;
    }

    // Get wallet database batch
    WalletBatch batch(m_wallet->GetDatabase());

    // Write position to database
    if (!batch.WritePosition(position)) {
        LogPrintf("DigiDollarWallet::WritePosition - Database write failed for %s\n",
                  position.position_id.ToString());
        return false;
    }

    // Update in-memory cache
    collateral_positions[position.position_id] = position;

    // Update locked collateral metadata if active
    if (position.is_active) {
        CAmount total_locked = 0;
        for (const auto& [id, pos] : collateral_positions) {
            if (pos.is_active) {
                total_locked += pos.dgb_collateral;
            }
        }
        locked_collateral = total_locked;
        batch.WriteDDMetadata("locked_collateral", std::to_string(total_locked));
    }

    LogPrint(BCLog::WALLET, "DigiDollarWallet: Wrote position %s (DD: %d, DGB: %d)\n",
             position.position_id.ToString(), position.dd_minted, position.dgb_collateral);
    return true;
}
```

**Verification:**
- Position is written to database
- In-memory cache is updated
- Locked collateral total is recalculated
- Active/inactive positions handled correctly

---

### Task 3.3: Implement DigiDollarWallet::UpdatePositionStatus()
**File:** `src/wallet/digidollarwallet.cpp`
**Location:** After WritePosition()

**Current stub (lines 119-125 in digidollarwallet.h):**
```cpp
bool UpdatePositionStatus(const uint256& position_id, bool active);
```

**IMPLEMENT:**
```cpp
bool DigiDollarWallet::UpdatePositionStatus(const uint256& position_id, bool active)
{
    if (!m_wallet) {
        LogPrintf("DigiDollarWallet::UpdatePositionStatus - No wallet pointer set\n");
        return false;
    }

    // Check if position exists in memory
    auto it = collateral_positions.find(position_id);
    if (it == collateral_positions.end()) {
        LogPrintf("DigiDollarWallet::UpdatePositionStatus - Position %s not found\n",
                  position_id.ToString());
        return false;
    }

    // Update status
    it->second.is_active = active;

    // Write updated position to database
    WalletBatch batch(m_wallet->GetDatabase());
    if (!batch.WritePosition(it->second)) {
        LogPrintf("DigiDollarWallet::UpdatePositionStatus - Database write failed\n");
        return false;
    }

    // Recalculate locked collateral
    CAmount total_locked = 0;
    for (const auto& [id, pos] : collateral_positions) {
        if (pos.is_active) {
            total_locked += pos.dgb_collateral;
        }
    }
    locked_collateral = total_locked;
    batch.WriteDDMetadata("locked_collateral", std::to_string(total_locked));

    LogPrint(BCLog::WALLET, "DigiDollarWallet: Updated position %s status to %s\n",
             position_id.ToString(), active ? "active" : "inactive");
    return true;
}
```

**Verification:**
- Position status is updated in database
- Locked collateral total is recalculated
- Returns false if position not found

---

## PHASE 4: Database Loading on Wallet Startup (Week 2 - Days 4-5)

### Task 4.1: Implement DigiDollarWallet::LoadFromDatabase()
**File:** `src/wallet/digidollarwallet.cpp`
**Location:** Around line 150 (new method)

**This is THE CRITICAL METHOD that fixes the persistence issue**

**ADD to digidollarwallet.h:**
```cpp
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
    /**
     * Helper: Load all positions from database
     */
    size_t LoadPositionsFromDatabase();

    /**
     * Helper: Load all DD transactions from database
     */
    size_t LoadTransactionsFromDatabase();

    /**
     * Helper: Load all DD balances from database
     */
    size_t LoadBalancesFromDatabase();
};
```

**IMPLEMENT in digidollarwallet.cpp:**
```cpp
size_t DigiDollarWallet::LoadFromDatabase()
{
    if (!m_wallet) {
        LogPrintf("DigiDollarWallet::LoadFromDatabase - No wallet pointer set\n");
        return 0;
    }

    LogPrint(BCLog::WALLET, "DigiDollarWallet: Loading data from database...\n");

    size_t positions_loaded = LoadPositionsFromDatabase();
    size_t txs_loaded = LoadTransactionsFromDatabase();
    size_t balances_loaded = LoadBalancesFromDatabase();

    size_t total_loaded = positions_loaded + txs_loaded + balances_loaded;

    LogPrintf("DigiDollarWallet: Loaded %d positions, %d transactions, %d balances from database\n",
              positions_loaded, txs_loaded, balances_loaded);

    // Recalculate totals
    RecalculateTotals();

    return total_loaded;
}

size_t DigiDollarWallet::LoadPositionsFromDatabase()
{
    WalletBatch batch(m_wallet->GetDatabase());
    size_t count = 0;

    // Clear in-memory positions
    collateral_positions.clear();

    // Iterate through database looking for DD_POSITION entries
    // Pattern: Use Cursor to iterate all keys with DD_POSITION prefix
    std::unique_ptr<DatabaseCursor> cursor = batch.GetNewCursor();
    if (!cursor) {
        LogPrintf("DigiDollarWallet::LoadPositionsFromDatabase - Failed to get cursor\n");
        return 0;
    }

    // Read all position entries
    DatabaseCursor::Status status = DatabaseCursor::Status::MORE;
    while (status == DatabaseCursor::Status::MORE) {
        DataStream key{};
        DataStream value{};
        status = cursor->Next(key, value);

        if (status != DatabaseCursor::Status::MORE) break;

        // Check if this is a position entry
        std::string key_type;
        key >> key_type;

        if (key_type == DBKeys::DD_POSITION) {
            uint256 position_id;
            key >> position_id;

            WalletCollateralPosition position;
            value >> position;

            collateral_positions[position_id] = position;
            count++;

            LogPrint(BCLog::WALLET, "DigiDollarWallet: Loaded position %s\n",
                     position_id.ToString());
        }
    }

    return count;
}

size_t DigiDollarWallet::LoadTransactionsFromDatabase()
{
    WalletBatch batch(m_wallet->GetDatabase());
    size_t count = 0;

    // Clear in-memory transaction history
    transaction_history.clear();

    std::unique_ptr<DatabaseCursor> cursor = batch.GetNewCursor();
    if (!cursor) {
        LogPrintf("DigiDollarWallet::LoadTransactionsFromDatabase - Failed to get cursor\n");
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

        if (key_type == DBKeys::DD_TRANSACTION) {
            uint256 txid;
            key >> txid;

            DDTransaction ddtx;
            value >> ddtx;

            transaction_history.push_back(ddtx);
            count++;

            LogPrint(BCLog::WALLET, "DigiDollarWallet: Loaded transaction %s\n",
                     ddtx.txid);
        }
    }

    return count;
}

size_t DigiDollarWallet::LoadBalancesFromDatabase()
{
    WalletBatch batch(m_wallet->GetDatabase());
    size_t count = 0;

    // Clear in-memory balances
    dd_balances.clear();

    std::unique_ptr<DatabaseCursor> cursor = batch.GetNewCursor();
    if (!cursor) {
        LogPrintf("DigiDollarWallet::LoadBalancesFromDatabase - Failed to get cursor\n");
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

        if (key_type == DBKeys::DD_BALANCE) {
            std::string address;
            key >> address;

            WalletDDBalance balance;
            value >> balance;

            dd_balances[address] = balance;
            count++;

            LogPrint(BCLog::WALLET, "DigiDollarWallet: Loaded balance for %s\n", address);
        }
    }

    return count;
}

void DigiDollarWallet::RecalculateTotals()
{
    // Recalculate total DD balance
    total_dd_balance = 0;
    for (const auto& [addr, bal] : dd_balances) {
        total_dd_balance += bal.balance;
    }

    // Recalculate locked collateral
    locked_collateral = 0;
    for (const auto& [id, pos] : collateral_positions) {
        if (pos.is_active) {
            locked_collateral += pos.dgb_collateral;
        }
    }

    LogPrint(BCLog::WALLET, "DigiDollarWallet: Recalculated totals - DD Balance: %d, Locked Collateral: %d\n",
             total_dd_balance, locked_collateral);
}
```

**Verification:**
- All positions loaded from database
- All transactions loaded from database
- All balances loaded from database
- Totals correctly recalculated
- Log output shows loaded item count

---

### Task 4.2: Hook LoadFromDatabase() into CWallet Initialization
**File:** `src/wallet/wallet.cpp`
**Location:** Search for wallet loading code (around `CWallet::LoadWallet()` or similar)

**Pattern to follow:** Look for where wallet loads transactions, keys, etc.

**FIND the wallet initialization code** (likely in `CWallet::LoadWallet()` or constructor):
```cpp
// Existing code loads transactions, keys, etc.
nKeyPoolSize = walletInstance->GetKeyPoolSize();
walletInstance->TopUpKeyPool();
// etc.
```

**ADD after wallet data loads:**
```cpp
// Load DigiDollar data from database
if (walletInstance->m_digidollar_wallet) {
    size_t dd_items_loaded = walletInstance->m_digidollar_wallet->LoadFromDatabase();
    LogPrintf("Loaded %d DigiDollar items from wallet database\n", dd_items_loaded);
}
```

**Alternative approach** (if CWallet doesn't have direct DD wallet reference):
Look for where `DigiDollarWallet` is instantiated and call `LoadFromDatabase()` immediately after:
```cpp
DigiDollarWallet* dd_wallet = new DigiDollarWallet(&wallet);
dd_wallet->LoadFromDatabase();  // ADD THIS
```

**Verification:**
- DigiDollar data loads on wallet startup
- Log shows loading message
- Positions visible in GUI immediately after wallet opens

---

## PHASE 5: Transaction Hooks - Auto-Save on Mint/Transfer/Redeem (Week 3 - Days 1-2)

### Task 5.1: Hook Mint Transaction Saving
**File:** `src/wallet/digidollarwallet.cpp`
**Location:** In `MintDigiDollar()` method (around line 300)

**CURRENT CODE** (likely):
```cpp
bool DigiDollarWallet::MintDigiDollar(CAmount amount, int lockDays, CWalletTx& wtx)
{
    // ... builds mint transaction ...
    // ... broadcasts transaction ...

    // Store position in memory
    WalletCollateralPosition position(txid, collateral_dgb, amount, unlock_height, ratio);
    collateral_positions[txid] = position;  // IN-MEMORY ONLY!

    return true;
}
```

**ADD persistence:**
```cpp
bool DigiDollarWallet::MintDigiDollar(CAmount amount, int lockDays, CWalletTx& wtx)
{
    // ... existing transaction building code ...

    // Create position record
    WalletCollateralPosition position(txid, collateral_dgb, amount, unlock_height, ratio);

    // PERSIST TO DATABASE (new code)
    if (!WritePosition(position)) {
        LogPrintf("DigiDollarWallet::MintDigiDollar - Failed to write position to database\n");
        // Don't fail the mint, but log the error
    }

    // Create DD transaction record
    DDTransaction ddtx;
    ddtx.txid = txid.ToString();
    ddtx.amount = amount;
    ddtx.timestamp = GetTime();
    ddtx.confirmations = 0;
    ddtx.incoming = false;
    ddtx.address = "";  // Mint has no counterparty
    ddtx.category = "mint";

    // PERSIST TRANSACTION (new code)
    WalletBatch batch(m_wallet->GetDatabase());
    if (!batch.WriteDDTransaction(ddtx)) {
        LogPrintf("DigiDollarWallet::MintDigiDollar - Failed to write transaction to database\n");
    }

    return true;
}
```

**Verification:**
- Minted positions appear in database immediately
- Positions persist across wallet restart
- Transaction history includes mint

---

### Task 5.2: Hook Transfer Transaction Saving
**File:** `src/wallet/digidollarwallet.cpp`
**Location:** In `TransferDigiDollar()` method

**ADD persistence:**
```cpp
bool DigiDollarWallet::TransferDigiDollar(const std::vector<CRecipient>& recipients, CWalletTx& wtx)
{
    // ... existing transfer code ...

    // After successful transfer, record transaction
    for (const auto& recipient : recipients) {
        DDTransaction ddtx;
        ddtx.txid = wtx.GetHash().ToString();
        ddtx.amount = recipient.nAmount;
        ddtx.timestamp = GetTime();
        ddtx.confirmations = 0;
        ddtx.incoming = false;
        ddtx.address = EncodeDestination(recipient.dest);
        ddtx.category = "send";

        // PERSIST TRANSACTION
        WalletBatch batch(m_wallet->GetDatabase());
        batch.WriteDDTransaction(ddtx);

        // Update balance
        CAmount new_balance = GetTotalDDBalance() - recipient.nAmount;
        // ... update balance in database ...
    }

    return true;
}
```

**Verification:**
- Sent transactions appear in history
- Balances update correctly
- Database reflects new state

---

### Task 5.3: Hook Redemption Transaction Saving
**File:** `src/wallet/digidollarwallet.cpp`
**Location:** In `RedeemDigiDollar()` method

**ADD persistence:**
```cpp
bool DigiDollarWallet::RedeemDigiDollar(const COutPoint& collateral, RedemptionPath path, CWalletTx& wtx)
{
    // ... existing redemption code ...

    // After successful redemption, update position
    uint256 position_id = collateral.hash;

    // Mark position as inactive
    if (!UpdatePositionStatus(position_id, false)) {
        LogPrintf("DigiDollarWallet::RedeemDigiDollar - Failed to update position status\n");
    }

    // Record redemption transaction
    DDTransaction ddtx;
    ddtx.txid = wtx.GetHash().ToString();
    ddtx.amount = position.dd_minted;
    ddtx.timestamp = GetTime();
    ddtx.confirmations = 0;
    ddtx.incoming = true;  // Receiving DGB back
    ddtx.address = "";
    ddtx.category = "redeem";

    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteDDTransaction(ddtx);

    return true;
}
```

**Verification:**
- Redeemed positions marked inactive
- Redemption appears in transaction history
- Locked collateral total decreases

---

## PHASE 6: Block Connection Hooks (Week 3 - Days 3-4)

### Task 6.1: Implement DigiDollar Block Connect/Disconnect Handlers
**File:** `src/wallet/wallet.cpp`
**Location:** Search for `CWallet::BlockConnected()` and `CWallet::BlockDisconnected()`

**Current pattern:**
```cpp
void CWallet::BlockConnected(const CBlock& block, int height)
{
    // ... existing code handles regular transactions ...
}
```

**ADD DigiDollar handling:**
```cpp
void CWallet::BlockConnected(const CBlock& block, int height)
{
    // ... existing code ...

    // Process DigiDollar transactions in block
    if (m_digidollar_wallet) {
        for (const auto& tx : block.vtx) {
            // Check if this is a DD transaction
            if (HasDigiDollarMarker(tx->nVersion)) {
                m_digidollar_wallet->ProcessDDTransaction(*tx, height, true);
            }
        }
    }
}

void CWallet::BlockDisconnected(const CBlock& block, int height)
{
    // ... existing code ...

    // Revert DigiDollar transactions in block
    if (m_digidollar_wallet) {
        for (const auto& tx : block.vtx) {
            if (HasDigiDollarMarker(tx->nVersion)) {
                m_digidollar_wallet->ProcessDDTransaction(*tx, height, false);
            }
        }
    }
}
```

**ADD to DigiDollarWallet:**
```cpp
void DigiDollarWallet::ProcessDDTransaction(const CTransaction& tx, int height, bool connect)
{
    uint256 txid = tx.GetHash();
    DigiDollarTxType type = GetDigiDollarTxType(tx.nVersion);

    if (connect) {
        // Block connected - update confirmations, activate positions
        LogPrint(BCLog::WALLET, "DigiDollarWallet: Processing DD tx %s at height %d\n",
                 txid.ToString(), height);

        // Update confirmations in transaction history
        for (auto& ddtx : transaction_history) {
            if (ddtx.txid == txid.ToString()) {
                ddtx.confirmations++;

                // Persist updated transaction
                WalletBatch batch(m_wallet->GetDatabase());
                batch.WriteDDTransaction(ddtx);
                break;
            }
        }

        // If this is a mint, ensure position is saved
        if (type == DigiDollarTxType::DD_TX_MINT) {
            // Position should already be saved from MintDigiDollar()
            // But verify it's in database
            auto it = collateral_positions.find(txid);
            if (it != collateral_positions.end()) {
                WritePosition(it->second);
            }
        }

    } else {
        // Block disconnected (reorg) - decrease confirmations
        LogPrint(BCLog::WALLET, "DigiDollarWallet: Reverting DD tx %s from height %d\n",
                 txid.ToString(), height);

        for (auto& ddtx : transaction_history) {
            if (ddtx.txid == txid.ToString()) {
                if (ddtx.confirmations > 0) {
                    ddtx.confirmations--;

                    WalletBatch batch(m_wallet->GetDatabase());
                    batch.WriteDDTransaction(ddtx);
                }
                break;
            }
        }
    }
}
```

**Verification:**
- Confirmations update as blocks arrive
- Reorgs correctly decrement confirmations
- Database stays in sync with blockchain

---

## PHASE 7: Testing & Verification (Week 3 - Day 5)

### Task 7.1: Unit Tests for Database Persistence
**File:** `src/wallet/test/digidollar_wallet_tests.cpp` (new or existing)

**ADD tests:**
```cpp
BOOST_AUTO_TEST_CASE(digidollar_position_persistence)
{
    // Create test wallet
    TestingSetup test_setup;
    auto wallet = CreateWallet(test_setup);
    DigiDollarWallet dd_wallet(wallet.get());

    // Create a test position
    uint256 pos_id = uint256S("0x1234...");
    WalletCollateralPosition position(pos_id, 100000, 50000, 3, 100000, 1);

    // Write to database
    BOOST_CHECK(dd_wallet.WritePosition(position));

    // Clear in-memory data
    dd_wallet.collateral_positions.clear();

    // Reload from database
    BOOST_CHECK_GT(dd_wallet.LoadFromDatabase(), 0);

    // Verify position was restored
    BOOST_CHECK(dd_wallet.collateral_positions.count(pos_id) > 0);
    BOOST_CHECK_EQUAL(dd_wallet.collateral_positions[pos_id].dd_minted, 50000);
}

BOOST_AUTO_TEST_CASE(digidollar_balance_persistence)
{
    // Test balance persistence
    TestingSetup test_setup;
    auto wallet = CreateWallet(test_setup);
    DigiDollarWallet dd_wallet(wallet.get());

    // Create test address and balance
    CDigiDollarAddress addr("DD1qtest...");
    CAmount balance = 100000;

    // Write to database
    BOOST_CHECK(dd_wallet.WriteDDBalance(addr, balance));

    // Clear in-memory
    dd_wallet.dd_balances.clear();

    // Reload
    dd_wallet.LoadFromDatabase();

    // Verify
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr), balance);
}

BOOST_AUTO_TEST_CASE(digidollar_transaction_persistence)
{
    // Test transaction history persistence
    // ... similar pattern ...
}
```

**Verification:**
- All unit tests pass
- Write/Read/Erase operations work correctly
- LoadFromDatabase() restores exact state

---

### Task 7.2: Integration Test - Full Mint/Restart Cycle
**Test Procedure:**
1. Start digibyte-qt or digibyted
2. Use GUI or RPC to mint DigiDollar position
3. Verify position shows in GUI vault manager
4. **Close wallet completely** (`stop` RPC or quit GUI)
5. **Restart wallet**
6. Verify position still shows in GUI vault manager
7. Verify balance is correct
8. Attempt to redeem position
9. Verify redemption works and position is marked inactive

**Expected Results:**
- ✅ Position persists across restart
- ✅ Balance persists across restart
- ✅ Transaction history persists across restart
- ✅ Redemption updates database correctly
- ✅ Re-importing wallet.dat shows all positions

**If test fails:**
- Check logs for "LoadFromDatabase" messages
- Verify database keys are correct
- Check serialization is working
- Ensure WalletBatch calls are not being skipped

---

## PHASE 8: Database Migration & Backward Compatibility (Week 3 - Bonus)

### Task 8.1: Implement Database Version Check
**File:** `src/wallet/walletdb.cpp`

**ADD version check for DD data:**
```cpp
// In wallet loading code
void WalletBatch::CheckDDVersion()
{
    std::string dd_version;
    if (!ReadDDMetadata("dd_schema_version", dd_version)) {
        // First time - write current version
        WriteDDMetadata("dd_schema_version", "1");
    } else {
        // Check if migration needed
        if (dd_version != "1") {
            LogPrintf("DigiDollar schema migration needed from version %s to 1\n", dd_version);
            // Future: Run migration code
        }
    }
}
```

**Verification:**
- New wallets get schema version written
- Existing wallets can be migrated in future

---

### Task 8.2: Add Database Compaction/Cleanup
**File:** `src/wallet/digidollarwallet.cpp`

**ADD cleanup method:**
```cpp
size_t DigiDollarWallet::CleanupInactivePositions(int blocks_ago)
{
    if (!m_wallet) return 0;

    WalletBatch batch(m_wallet->GetDatabase());
    size_t cleaned = 0;

    int current_height = m_wallet->GetLastBlockHeight();

    // Remove positions that were redeemed >blocks_ago
    auto it = collateral_positions.begin();
    while (it != collateral_positions.end()) {
        if (!it->second.is_active &&
            current_height - it->second.unlock_height > blocks_ago) {

            // Remove from database
            batch.ErasePosition(it->first);

            // Remove from memory
            it = collateral_positions.erase(it);
            cleaned++;
        } else {
            ++it;
        }
    }

    LogPrintf("DigiDollarWallet: Cleaned up %d inactive positions\n", cleaned);
    return cleaned;
}
```

**Verification:**
- Old redeemed positions can be cleaned up
- Database doesn't grow indefinitely
- User can optionally keep history

---

## CRITICAL SUCCESS CRITERIA

### ✅ Definition of Done
1. **Mint a DigiDollar position** → Close wallet → Reopen wallet → **Position still exists**
2. **Send DigiDollars** → Close wallet → Reopen wallet → **Transaction history intact**
3. **Check balance** → Close wallet → Reopen wallet → **Balance unchanged**
4. **Export wallet.dat to backup** → Import on different machine → **All positions restored**
5. **Unit tests pass** showing write/read/erase operations work
6. **No data loss** under any circumstance (crash, reorg, etc.)

### 🚨 Known Risks
1. **Database corruption** - Mitigated by using proven WalletBatch infrastructure
2. **Serialization bugs** - Mitigated by comprehensive unit tests
3. **Performance issues** - Mitigated by efficient key design and indexing
4. **Migration complexity** - Mitigated by schema versioning

### 📊 Performance Targets
- **Write latency:** <10ms per position
- **Read latency:** <5ms per position
- **Wallet load time:** <1 second for 1000 positions
- **Database growth:** ~500 bytes per position

---

## IMPLEMENTATION CHECKLIST

### Week 1: Database Schema & Write Methods
- [ ] Task 1.1: Add DD database keys to walletdb.h/cpp
- [ ] Task 1.2: Add serialization to WalletCollateralPosition, DDTransaction, WalletDDBalance
- [ ] Task 2.1: Implement WalletBatch write methods
- [ ] Task 2.2: Implement WalletBatch read methods
- [ ] Task 2.3: Implement WalletBatch erase methods

### Week 2: Wallet Integration & Loading
- [ ] Task 3.1: Implement DigiDollarWallet::WriteDDBalance()
- [ ] Task 3.2: Implement DigiDollarWallet::WritePosition()
- [ ] Task 3.3: Implement DigiDollarWallet::UpdatePositionStatus()
- [ ] Task 4.1: Implement DigiDollarWallet::LoadFromDatabase()
- [ ] Task 4.2: Hook LoadFromDatabase() into CWallet initialization

### Week 3: Transaction Hooks & Testing
- [ ] Task 5.1: Hook mint transaction saving
- [ ] Task 5.2: Hook transfer transaction saving
- [ ] Task 5.3: Hook redemption transaction saving
- [ ] Task 6.1: Implement block connect/disconnect handlers
- [ ] Task 7.1: Write unit tests for persistence
- [ ] Task 7.2: Run integration test (mint → restart → verify)

### Bonus: Migration & Cleanup
- [ ] Task 8.1: Implement database version check
- [ ] Task 8.2: Add database cleanup for old positions

---

## FILES TO MODIFY

### Core Database Files
1. **src/wallet/walletdb.h** - Add DD database keys, method declarations
2. **src/wallet/walletdb.cpp** - Add DD read/write/erase implementations

### DigiDollar Wallet Files
3. **src/wallet/digidollarwallet.h** - Add LoadFromDatabase() declaration, serialization
4. **src/wallet/digidollarwallet.cpp** - Implement all persistence methods

### Integration Points
5. **src/wallet/wallet.cpp** - Hook LoadFromDatabase() into wallet init, add block handlers

### Testing Files
6. **src/wallet/test/digidollar_wallet_tests.cpp** - Add persistence tests

---

## REFERENCE: Existing Patterns to Follow

### How CWalletTx is Persisted
```cpp
// walletdb.cpp:93-94
bool WalletBatch::WriteTx(const CWalletTx& wtx)
{
    return WriteIC(std::make_pair(DBKeys::TX, wtx.GetHash()), wtx);
}
```

### How Keys are Persisted
```cpp
// walletdb.cpp:119
return WriteIC(std::make_pair(DBKeys::KEY, vchPubKey),
               std::make_pair(vchPrivKey, Hash(vchKey)), false);
```

### How Locked UTXOs are Persisted
```cpp
// walletdb.cpp:293
return WriteIC(std::make_pair(DBKeys::LOCKED_UTXO,
               std::make_pair(output.hash, output.n)), uint8_t{'1'});
```

**Pattern:** Always use `std::make_pair(DBKeys::*, identifier)` as key, and serialize the data structure as value.

---

## CONCLUSION

This task list provides a **complete, step-by-step roadmap** to implement full database persistence for DigiDollar positions. The implementation follows proven patterns from the existing DigiByte Core wallet database infrastructure, ensuring compatibility with both BerkeleyDB (legacy wallets) and SQLite (descriptor wallets).

**Once complete, users will be able to:**
- Mint DigiDollar positions that survive wallet restarts
- Import wallet.dat files with all DD positions intact
- View complete transaction history across sessions
- Trust that their DigiDollar positions are as safe as their DGB UTXOs

**Total Estimated Time:** 2-3 weeks for full implementation and testing
**Priority Level:** CRITICAL - Blocks production use
**Risk Level:** LOW - Uses proven database infrastructure
**Impact:** HIGH - Enables real-world DigiDollar usage

---

*This document should be updated as implementation progresses. Mark tasks complete with [x] and note any deviations from the plan.*
