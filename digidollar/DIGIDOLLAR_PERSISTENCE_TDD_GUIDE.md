# DigiDollar Persistence - TDD Implementation Guide

**This document supplements DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md with complete TDD workflows for every task.**

---

## Universal TDD Workflow for ALL Tasks

Every single task in the persistence implementation MUST follow this exact workflow:

### Step 1: 🔴 RED - Write Failing Test

```bash
# Create test file FIRST
touch src/test/digidollar_persistence_[component]_tests.cpp

# Write test that WILL FAIL
# Compile and run to VERIFY it fails
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_[component]_tests
# EXPECT: RED (test fails or doesn't compile)
```

### Step 2: 🟢 GREEN - Minimal Implementation

```bash
# Write JUST ENOUGH code to pass the test
# Compile and run
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_[component]_tests
# EXPECT: GREEN (all tests pass)
```

### Step 3: 🔵 REFACTOR - Improve Quality

```bash
# Improve code (error handling, logging, comments)
# Verify tests still pass
./src/test/test_digibyte --run_test=digidollar_persistence_[component]_tests
# EXPECT: Still GREEN
```

### Step 4: ✅ VERIFY - Full Test Suite

```bash
# 1. Clean rebuild
make clean && make -j$(nproc)

# 2. Run new persistence tests
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_persistence_*

# 3. Run ALL DigiDollar tests (NO REGRESSION)
./src/test/test_digibyte --log_level=test_suite --run_test=digidollar_*

# 4. Check for warnings
make 2>&1 | grep -i warning

# 5. Functional tests (if applicable)
./test/functional/digidollar_persistence_basic.py
```

---

## Phase 1: Database Schema - TDD Workflows

### Task 1.1: Database Keys

**Test File:** `src/test/digidollar_persistence_keys_tests.cpp`

**See DIGIDOLLAR_DATABASE_PERSISTENCE_TASKS.md lines 86-205 for complete implementation.**

---

### Task 1.2: Serialization - WalletCollateralPosition

**🔴 RED Phase:**

**Test File:** `src/test/digidollar_persistence_serialization_tests.cpp`

```cpp
#include <boost/test/unit_test.hpp>
#include <wallet/digidollarwallet.h>
#include <streams.h>

BOOST_AUTO_TEST_SUITE(digidollar_persistence_serialization_tests)

BOOST_AUTO_TEST_CASE(walletcollateralposition_serialize_roundtrip)
{
    // Create original position
    WalletCollateralPosition original;
    original.position_id = uint256S("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    original.dd_minted = 10000;  // $100.00
    original.dgb_collateral = 500000;
    original.lock_tier = 3;
    original.unlock_height = 100000;
    original.is_active = true;

    // Serialize
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << original;  // WILL FAIL - no SERIALIZE_METHODS yet

    // Deserialize
    WalletCollateralPosition deserialized;
    stream >> deserialized;

    // Verify all fields match
    BOOST_CHECK_EQUAL(deserialized.position_id, original.position_id);
    BOOST_CHECK_EQUAL(deserialized.dd_minted, original.dd_minted);
    BOOST_CHECK_EQUAL(deserialized.dgb_collateral, original.dgb_collateral);
    BOOST_CHECK_EQUAL(deserialized.lock_tier, original.lock_tier);
    BOOST_CHECK_EQUAL(deserialized.unlock_height, original.unlock_height);
    BOOST_CHECK_EQUAL(deserialized.is_active, original.is_active);
}

BOOST_AUTO_TEST_CASE(ddtransaction_serialize_roundtrip)
{
    // Create original transaction
    DDTransaction original;
    original.txid = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    original.amount = 5000;  // $50.00
    original.timestamp = 1234567890;
    original.confirmations = 6;
    original.incoming = true;
    original.address = "DD1qtest123...";
    original.category = "receive";

    // Serialize
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << original;  // WILL FAIL - no SERIALIZE_METHODS yet

    // Deserialize
    DDTransaction deserialized;
    stream >> deserialized;

    // Verify
    BOOST_CHECK_EQUAL(deserialized.txid, original.txid);
    BOOST_CHECK_EQUAL(deserialized.amount, original.amount);
    BOOST_CHECK_EQUAL(deserialized.timestamp, original.timestamp);
    BOOST_CHECK_EQUAL(deserialized.confirmations, original.confirmations);
    BOOST_CHECK_EQUAL(deserialized.incoming, original.incoming);
    BOOST_CHECK_EQUAL(deserialized.address, original.address);
    BOOST_CHECK_EQUAL(deserialized.category, original.category);
}

BOOST_AUTO_TEST_CASE(walletddbalance_serialize_roundtrip)
{
    // Create original balance
    WalletDDBalance original;
    original.address = CDigiDollarAddress("DD1qtest123...");
    original.balance = 75000;  // $750.00
    original.last_updated = 1234567890;

    // Serialize
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << original;  // WILL FAIL - no SERIALIZE_METHODS yet

    // Deserialize
    WalletDDBalance deserialized;
    stream >> deserialized;

    // Verify
    BOOST_CHECK(deserialized.address == original.address);
    BOOST_CHECK_EQUAL(deserialized.balance, original.balance);
    BOOST_CHECK_EQUAL(deserialized.last_updated, original.last_updated);
}

BOOST_AUTO_TEST_SUITE_END()
```

**Run tests (should FAIL):**
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_serialization_tests
# Expected: Compilation errors - no serialization methods
```

---

**🟢 GREEN Phase:**

**File:** `src/wallet/digidollarwallet.h`

Add serialization to ALL three structures:

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

**Run tests (should PASS):**
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_serialization_tests
# Expected: All 3 tests pass (GREEN)
```

---

**✅ VERIFICATION:**
```bash
make clean && make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_*
./src/test/test_digibyte --run_test=digidollar_*
make 2>&1 | grep -i warning
```

---

## Phase 2: WalletBatch Methods - TDD Workflows

### Task 2.1: Write Methods

**🔴 RED Phase:**

**Test File:** `src/test/digidollar_persistence_walletbatch_tests.cpp`

```cpp
#include <boost/test/unit_test.hpp>
#include <wallet/walletdb.h>
#include <wallet/digidollarwallet.h>
#include <wallet/test/util.h>

BOOST_FIXTURE_TEST_SUITE(digidollar_persistence_walletbatch_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(walletbatch_write_position)
{
    // Create test wallet
    std::unique_ptr<WalletDatabase> database = CreateMockWalletDatabase();
    WalletBatch batch(*database);

    // Create test position
    WalletCollateralPosition pos;
    pos.position_id = uint256S("0x1234...");
    pos.dd_minted = 10000;
    pos.dgb_collateral = 500000;
    pos.lock_tier = 3;
    pos.unlock_height = 100000;
    pos.is_active = true;

    // Write (WILL FAIL - method doesn't exist)
    BOOST_CHECK(batch.WritePosition(pos));
}

BOOST_AUTO_TEST_CASE(walletbatch_write_ddtransaction)
{
    std::unique_ptr<WalletDatabase> database = CreateMockWalletDatabase();
    WalletBatch batch(*database);

    DDTransaction tx;
    tx.txid = "abcd...";
    tx.amount = 5000;
    tx.category = "mint";

    BOOST_CHECK(batch.WriteDDTransaction(tx));  // WILL FAIL
}

BOOST_AUTO_TEST_CASE(walletbatch_write_ddbalance)
{
    std::unique_ptr<WalletDatabase> database = CreateMockWalletDatabase();
    WalletBatch batch(*database);

    WalletDDBalance balance;
    balance.address = CDigiDollarAddress("DD1qtest...");
    balance.balance = 10000;

    std::string address = balance.address.ToString();
    BOOST_CHECK(batch.WriteDDBalance(address, balance));  // WILL FAIL
}

BOOST_AUTO_TEST_SUITE_END()
```

**Run (should FAIL):**
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_walletbatch_tests
# Expected: Compilation errors - methods don't exist
```

---

**🟢 GREEN Phase:**

**File 1:** `src/wallet/walletdb.h` (add to WalletBatch class)

```cpp
class WalletBatch {
public:
    // ... existing methods ...

    // DigiDollar persistence write methods
    bool WritePosition(const WalletCollateralPosition& position);
    bool WriteDDTransaction(const DDTransaction& ddtx);
    bool WriteDDBalance(const std::string& address, const WalletDDBalance& balance);
    bool WriteDDOutput(const uint256& output_id, const CDigiDollarOutput& output);
    bool WriteDDMetadata(const std::string& key, const std::string& value);
};
```

**File 2:** `src/wallet/walletdb.cpp` (add implementations)

```cpp
bool WalletBatch::WritePosition(const WalletCollateralPosition& position)
{
    return WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);
}

bool WalletBatch::WriteDDTransaction(const DDTransaction& ddtx)
{
    uint256 txid;
    txid.SetHex(ddtx.txid);
    return WriteIC(std::make_pair(DBKeys::DD_TRANSACTION, txid), ddtx);
}

bool WalletBatch::WriteDDBalance(const std::string& address, const WalletDDBalance& balance)
{
    return WriteIC(std::make_pair(DBKeys::DD_BALANCE, address), balance);
}

bool WalletBatch::WriteDDOutput(const uint256& output_id, const CDigiDollarOutput& output)
{
    return WriteIC(std::make_pair(DBKeys::DD_OUTPUT, output_id), output);
}

bool WalletBatch::WriteDDMetadata(const std::string& key, const std::string& value)
{
    return WriteIC(std::make_pair(DBKeys::DD_METADATA, key), value);
}
```

**Run (should PASS):**
```bash
make -j$(nproc)
./src/test/test_digibyte --run_test=digidollar_persistence_walletbatch_tests
# Expected: All tests pass (GREEN)
```

---

**🔵 REFACTOR Phase:**

Add validation and logging:

```cpp
bool WalletBatch::WritePosition(const WalletCollateralPosition& position)
{
    if (position.position_id.IsNull()) {
        return error("DigiDollar: Cannot write position with null ID");
    }

    bool success = WriteIC(std::make_pair(DBKeys::DD_POSITION, position.position_id), position);

    if (success) {
        LogPrint(BCLog::WALLET, "DigiDollar: Wrote position %s to database\n",
                 position.position_id.ToString());
    }

    return success;
}
```

---

### Task 2.2: Read Methods

**Add to test file:**
```cpp
BOOST_AUTO_TEST_CASE(walletbatch_read_position)
{
    std::unique_ptr<WalletDatabase> database = CreateMockWalletDatabase();
    WalletBatch batch(*database);

    // Write position
    WalletCollateralPosition original;
    original.position_id = uint256S("0x1234...");
    original.dd_minted = 10000;
    BOOST_CHECK(batch.WritePosition(original));

    // Read position (WILL FAIL - method doesn't exist)
    WalletCollateralPosition read_pos;
    BOOST_CHECK(batch.ReadPosition(original.position_id, read_pos));
    BOOST_CHECK_EQUAL(read_pos.dd_minted, original.dd_minted);
}
```

**Implementation:**
```cpp
// walletdb.h
bool ReadPosition(const uint256& position_id, WalletCollateralPosition& position);
bool ReadDDTransaction(const uint256& txid, DDTransaction& ddtx);
bool ReadDDBalance(const std::string& address, WalletDDBalance& balance);

// walletdb.cpp
bool WalletBatch::ReadPosition(const uint256& position_id, WalletCollateralPosition& position)
{
    return m_batch->Read(std::make_pair(DBKeys::DD_POSITION, position_id), position);
}
// ... etc
```

---

### Task 2.3: Erase Methods

**Follow same TDD pattern: Test → Implement → Refactor → Verify**

---

## Phase 3: DigiDollarWallet Integration - TDD Workflows

### Task 3.1: WriteDDBalance()

**Test File:** `src/wallet/test/digidollar_persistence_wallet_tests.cpp`

```cpp
#include <boost/test/unit_test.hpp>
#include <wallet/wallet.h>
#include <wallet/digidollarwallet.h>
#include <wallet/test/wallet_test_fixture.h>

BOOST_FIXTURE_TEST_SUITE(digidollar_persistence_wallet_tests, WalletTestingSetup)

BOOST_AUTO_TEST_CASE(digidollarwallet_write_balance_persists)
{
    // Create wallet with DD wallet
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chain)->getTip(), return _locked_chain_tip;));
    DigiDollarWallet dd_wallet(wallet.get());

    // Write balance
    CDigiDollarAddress addr("DD1qtest...");
    CAmount balance = 10000;
    BOOST_CHECK(dd_wallet.WriteDDBalance(addr, balance));

    // Verify written to database
    WalletBatch batch(wallet->GetDatabase());
    WalletDDBalance read_balance;
    BOOST_CHECK(batch.ReadDDBalance(addr.ToString(), read_balance));
    BOOST_CHECK_EQUAL(read_balance.balance, balance);

    // Verify in-memory cache updated
    BOOST_CHECK_EQUAL(dd_wallet.GetDDBalance(addr), balance);
}

BOOST_AUTO_TEST_SUITE_END()
```

---

## Phase 4: LoadFromDatabase() - TDD Workflows

### Task 4.1: The Critical Load Method

**Test File:** `src/wallet/test/digidollar_persistence_wallet_tests.cpp`

```cpp
BOOST_AUTO_TEST_CASE(digidollarwallet_load_from_database_positions)
{
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chain)->getTip(), return _locked_chain_tip;));
    DigiDollarWallet dd_wallet(wallet.get());

    // Write test positions to database
    WalletBatch batch(wallet->GetDatabase());
    WalletCollateralPosition pos1, pos2;
    pos1.position_id = uint256S("0x1111...");
    pos1.dd_minted = 10000;
    pos2.position_id = uint256S("0x2222...");
    pos2.dd_minted = 20000;

    batch.WritePosition(pos1);
    batch.WritePosition(pos2);

    // Clear in-memory data
    dd_wallet.collateral_positions.clear();
    BOOST_CHECK_EQUAL(dd_wallet.collateral_positions.size(), 0);

    // Load from database (WILL FAIL - method doesn't exist)
    size_t loaded = dd_wallet.LoadFromDatabase();

    // Verify positions restored
    BOOST_CHECK_EQUAL(loaded, 2);
    BOOST_CHECK(dd_wallet.collateral_positions.count(pos1.position_id) > 0);
    BOOST_CHECK(dd_wallet.collateral_positions.count(pos2.position_id) > 0);
    BOOST_CHECK_EQUAL(dd_wallet.collateral_positions[pos1.position_id].dd_minted, 10000);
}
```

---

## Phase 7: Integration Testing - The Ultimate Test

### Functional Test: Restart Persistence

**Test File:** `test/functional/digidollar_persistence_restart.py`

```python
#!/usr/bin/env python3
"""Test DigiDollar position persistence across wallet restart."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal

class DigiDollarPersistenceRestartTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        self.log.info("Starting DigiDollar persistence restart test...")

        # Step 1: Mint a DigiDollar position
        self.log.info("Minting DigiDollar position...")
        position_result = self.nodes[0].mintdigidollar(amount=100, lockdays=365)
        position_id = position_result['position_id']

        # Step 2: Verify position exists
        positions_before = self.nodes[0].listddpositions()
        assert_equal(len(positions_before), 1)
        assert_equal(positions_before[0]['position_id'], position_id)
        self.log.info("Position exists: %s" % position_id)

        # Step 3: Stop the wallet
        self.log.info("Stopping wallet...")
        self.stop_node(0)

        # Step 4: Restart the wallet
        self.log.info("Restarting wallet...")
        self.start_node(0)
        self.wait_until(lambda: self.nodes[0].getblockcount() > 0)

        # Step 5: THE CRITICAL TEST - Verify position STILL exists
        self.log.info("Checking if position persists...")
        positions_after = self.nodes[0].listddpositions()

        assert_equal(len(positions_after), 1, "FAIL: Position lost after restart!")
        assert_equal(positions_after[0]['position_id'], position_id, "FAIL: Position ID corrupted!")
        assert_equal(positions_after[0]['dd_minted'], 10000, "FAIL: Position data corrupted!")

        self.log.info("SUCCESS: DigiDollar position persisted across wallet restart!")

if __name__ == '__main__':
    DigiDollarPersistenceRestartTest().main()
```

**Run:**
```bash
./test/functional/digidollar_persistence_restart.py
# This MUST pass before Phase 7 is complete
```

---

## Summary: Test Requirements by Phase

| Phase | Test Files Required | Verification Commands |
|-------|-------------------|---------------------|
| Phase 1 | `digidollar_persistence_keys_tests.cpp`<br>`digidollar_persistence_serialization_tests.cpp` | `./src/test/test_digibyte --run_test=digidollar_persistence_*` |
| Phase 2 | `digidollar_persistence_walletbatch_tests.cpp` | Same + check read/write/erase works |
| Phase 3 | `digidollar_persistence_wallet_tests.cpp` (in `src/wallet/test/`) | Same + verify database writes |
| Phase 4 | Extend `digidollar_persistence_wallet_tests.cpp` | Same + verify LoadFromDatabase() works |
| Phase 5 | Extend existing `digidollar_wallet_tests.cpp` | Verify mint/transfer/redeem persist |
| Phase 6 | Extend existing `digidollar_wallet_tests.cpp` | Verify confirmations update |
| Phase 7 | `digidollar_persistence_restart.py` (functional) | **THE CRITICAL TEST** |

---

## Final Verification Checklist

Before marking implementation complete:

- [ ] All unit tests pass: `./src/test/test_digibyte --run_test=digidollar_persistence_*`
- [ ] No regression: `./src/test/test_digibyte --run_test=digidollar_*`
- [ ] Functional test passes: `./test/functional/digidollar_persistence_restart.py`
- [ ] Code compiles cleanly: `make clean && make -j$(nproc)`
- [ ] Zero warnings: `make 2>&1 | grep -i warning`
- [ ] **Critical test passes**: Mint → Restart → Position Exists ✅

---

**This TDD guide ensures that every single piece of the persistence implementation is tested BEFORE it's written, guaranteeing correctness and preventing regressions.**
