# DigiDollar Send/Receive Sub-Agent - BUG FIX MODE

## Your Role
You are a **Sub-Agent** assigned to fix ONE specific bug in DigiDollar Send/Receive. You MUST follow strict TDD methodology: RED → GREEN → REFACTOR.

## 🔴 CRITICAL: The System is BROKEN

**READ**: DIGIDOLLAR_SENDRECEIVE_TASKS.md for complete bug analysis

### The 4 Critical Bugs:
1. NO BROADCASTING - Transactions never sent to network
2. BROKEN UTXO MODEL - Can only spend DD once
3. DESTROYS TIME-LOCKS - Marks positions inactive (wrong!)
4. NO RECEIVING - Can't detect incoming DD

## The CORRECT DigiDollar Model

### Mint Transaction
```
Mint TX: abc123...
├─ vout[0]: 1000 DGB (Time-Locked) ← NEVER MOVES until redemption!
└─ vout[1]: 500 DD (spendable)     ← CAN be transferred
```

###Transfer Transaction
```
Transfer TX: def456...
Inputs:
  ├─ vin[0]: abc123:1 (500 DD)  ← Spending DD token from mint
  └─ vin[1]: fee_utxo
Outputs:
  ├─ vout[0]: 300 DD (recipient) ← NEW DD UTXO
  ├─ vout[1]: 200 DD (change)    ← NEW DD UTXO
  └─ vout[2]: DGB change
```

**KEY INSIGHT**: The locked DGB (vout[0] of mint) NEVER MOVES! Only DD tokens transfer between wallets.

### What Wallet Must Track

**1. Time-Lock Positions** (collateral_positions map)
- Represents locked DGB from MINT transactions
- Stays ACTIVE until redemption
- **NEVER modified during transfers!** ← This is the core bug!

**2. DD UTXOs** (NEW - dd_utxos map)
- Spendable DD outputs
- From BOTH mint AND transfer transactions
- Updated on send/receive

**3. Balance**
- = Sum of spendable DD UTXOs (NOT sum of active positions!)

## TDD Process (MANDATORY)

### Step 1: RED Phase
**Write a FAILING test first - prove the bug exists**

**Example for FIX #2** (Time-lock preservation):
```cpp
// File: src/test/digidollar_transfer_tests.cpp
BOOST_AUTO_TEST_CASE(test_transfer_preserves_timelock)
{
    // Setup: Create mint position
    DigiDollarWallet wallet;
    uint256 mint_txid = CreateMintPosition(wallet, 10000); // 100 DD

    // Verify initial state
    BOOST_CHECK_EQUAL(wallet.GetTotalDDBalance(), 10000);
    auto timelocks = wallet.GetDDTimeLocks(true);
    BOOST_CHECK_EQUAL(timelocks.size(), 1);
    BOOST_CHECK(timelocks[0].is_active);  // Active before transfer

    // Execute: Transfer 50 DD
    std::string txid, error;
    CDigiDollarAddress recipient("DD1test...");
    bool success = wallet.TransferDigiDollar(recipient, 5000, txid, error);

    BOOST_CHECK(success);

    // CRITICAL TEST: Time-lock must STILL be active!
    timelocks = wallet.GetDDTimeLocks(true);
    BOOST_CHECK_EQUAL(timelocks.size(), 1);
    BOOST_CHECK(timelocks[0].is_active);  // ← THIS WILL FAIL (proves bug!)

    // Verify balance decreased
    BOOST_CHECK_EQUAL(wallet.GetTotalDDBalance(), 5000); // 50 DD remaining
}
```

**Expected RED Output**:
```
test/digidollar_transfer_tests.cpp(XX): error:
  check timelocks[0].is_active has failed [false != true]
  ← GOOD! Test proves time-lock incorrectly marked inactive
```

### Step 2: GREEN Phase
**Fix the bug - make test pass**

**For FIX #2** (Remove bad code that marks positions inactive):

**REMOVE this BAD code** (lines 314-392 in digidollarwallet.cpp):
```cpp
// ❌ BAD CODE - DELETE THIS!
for (const auto& dd_utxo : dd_utxos) {
    UpdatePositionStatus(dd_timelock_id, false);  // ← WRONG!
}

// ❌ BAD CODE - DELETE THIS!
WalletCollateralPosition changePosition(...);
AddCollateralPosition(changePosition);  // ← WRONG!
```

**ADD this CORRECT code**:
```cpp
// ✅ CORRECT CODE - Time-locks NEVER change during transfers!
// Only DD UTXOs move. Locked DGB stays in place.

// Remove spent DD UTXOs from tracking
for (const auto& spent_utxo : params.ddUtxos) {
    dd_utxos.erase(spent_utxo);
    LogPrintf("DigiDollar: Marked DD UTXO %s:%d as spent\n",
              spent_utxo.hash.ToString(), spent_utxo.n);
}

// Add new DD UTXOs from transaction outputs (for change)
for (size_t i = 0; i < result.tx.vout.size(); i++) {
    CAmount dd_amount = 0;
    if (ExtractDDAmount(result.tx.vout[i].scriptPubKey, dd_amount)) {
        // Check if output is to our address (change)
        if (IsMine(result.tx.vout[i])) {
            COutPoint new_utxo(result.tx.GetHash(), i);
            dd_utxos[new_utxo] = dd_amount;
            LogPrintf("DigiDollar: Added change DD UTXO %s:%d (%d cents)\n",
                      new_utxo.hash.ToString(), i, dd_amount);
        }
    }
}

// Time-lock positions remain ACTIVE and UNCHANGED
LogPrintf("DigiDollar: Transfer complete - time-locks preserved\n");
```

**Expected GREEN Output**:
```
Running test/digidollar_transfer_tests.cpp...
test_transfer_preserves_timelock: PASSED ✅
```

### Step 3: REFACTOR Phase
**Clean up code while keeping test passing**

Add:
- Better logging
- Error handling
- Edge case checks
- Documentation

Run test again - MUST still pass!

## Fix-Specific Instructions

### FIX #5: DD UTXO Database Persistence

**Files**: src/wallet/walletdb.h, src/wallet/walletdb.cpp

**TDD Steps**:
1. RED: Write test for WriteDDUTXO/ReadDDUTXO
2. GREEN: Implement methods in WalletBatch
3. REFACTOR: Add to LoadFromDatabase

**Add to walletdb.h**:
```cpp
class WalletBatch {
public:
    /** Write DD UTXO to database */
    bool WriteDDUTXO(const COutPoint& outpoint, const CAmount& dd_amount);

    /** Read DD UTXO from database */
    bool ReadDDUTXO(const COutPoint& outpoint, CAmount& dd_amount);

    /** Erase DD UTXO from database */
    bool EraseDDUTXO(const COutPoint& outpoint);
};
```

**Add to walletdb.cpp**:
```cpp
bool WalletBatch::WriteDDUTXO(const COutPoint& outpoint, const CAmount& dd_amount) {
    return WriteIC(std::make_pair(DBKeys::DD_UTXO, outpoint), dd_amount);
}

bool WalletBatch::ReadDDUTXO(const COutPoint& outpoint, CAmount& dd_amount) {
    return m_batch->Read(std::make_pair(DBKeys::DD_UTXO, outpoint), dd_amount);
}

bool WalletBatch::EraseDDUTXO(const COutPoint& outpoint) {
    return m_batch->Erase(std::make_pair(DBKeys::DD_UTXO, outpoint));
}
```

---

### FIX #1: DD UTXO Tracking System

**Files**: src/wallet/digidollarwallet.h, src/wallet/digidollarwallet.cpp

**TDD Steps**:
1. RED: Write test expecting DD UTXOs from transfer
2. GREEN: Add dd_utxos map, update GetDDUTXOs()
3. REFACTOR: Update GetTotalDDBalance()

**Add to digidollarwallet.h**:
```cpp
class DigiDollarWallet {
private:
    // NEW: Track actual DD UTXOs (from mint AND transfers)
    std::map<COutPoint, CAmount> dd_utxos;
public:
    // ... existing methods ...
};
```

**Update GetDDUTXOs()**:
```cpp
std::vector<DDUtxo> DigiDollarWallet::GetDDUTXOs() const {
    std::vector<DDUtxo> utxos;
    for (const auto& [outpoint, dd_amount] : dd_utxos) {
        // Verify UTXO still unspent
        if (IsUTXOSpendable(outpoint)) {
            utxos.emplace_back(outpoint, dd_amount);
        }
    }
    return utxos;
}
```

**Update GetTotalDDBalance()**:
```cpp
CAmount DigiDollarWallet::GetTotalDDBalance() const {
    CAmount balance = 0;
    for (const auto& [outpoint, dd_amount] : dd_utxos) {
        if (IsUTXOSpendable(outpoint)) {
            balance += dd_amount;
        }
    }
    return balance;
}
```

---

### FIX #2: Fix Transfer Logic

**Files**: src/wallet/digidollarwallet.cpp

**TDD Steps**:
1. RED: Test time-lock stays active (see example above)
2. GREEN: Remove position marking, add UTXO management
3. REFACTOR: Clean up

**Critical Changes**:
- DELETE lines 314-332: UpdatePositionStatus() calls
- DELETE lines 333-392: AddCollateralPosition() for change
- ADD: dd_utxos.erase() for spent UTXOs
- ADD: dd_utxos[new_utxo] for change

---

### FIX #3: Transaction Broadcasting

**Files**: src/wallet/digidollarwallet.cpp

**TDD Steps**:
1. RED: Test transaction enters mempool
2. GREEN: Add AcceptToMemoryPool + broadcastTransaction
3. REFACTOR: Error handling

**Add after line ~310** (after transaction building):
```cpp
// NEW: Actually broadcast the transaction!
CTransactionRef tx_ref = MakeTransactionRef(result.tx);

// Submit to mempool
TxValidationState state;
if (!AcceptToMemoryPool(m_wallet->chain(), state, tx_ref,
                        /* bypass_limits */ false)) {
    error = strprintf("Transaction rejected: %s", state.GetRejectReason());
    LogPrintf("DigiDollar: Mempool rejection - %s\n", error);
    return false;
}

// Broadcast to network
m_wallet->chain().broadcastTransaction(tx_ref);
LogPrintf("DigiDollar: Transaction broadcast successful - txid: %s\n",
          tx_ref->GetHash().ToString());

txid = tx_ref->GetHash().ToString();
```

---

### FIX #4: Receive Detection

**Files**: src/wallet/digidollarwallet.cpp, digidollarwallet.h

**TDD Steps**:
1. RED: Test incoming DD detection
2. GREEN: Implement ScanForIncomingDD
3. REFACTOR: Hook into wallet transaction processing

**Add to digidollarwallet.h**:
```cpp
void ScanForIncomingDD(const CTransactionRef& tx);
void ProcessTransaction(const CTransactionRef& tx);
```

**Add to digidollarwallet.cpp**:
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    LogPrintf("DigiDollar: Scanning %s for incoming DD\n",
              tx->GetHash().ToString());

    for (size_t i = 0; i < tx->vout.size(); i++) {
        const CTxOut& txout = tx->vout[i];

        // Extract DD amount
        CAmount dd_amount = 0;
        if (!ExtractDDAmount(txout.scriptPubKey, dd_amount)) {
            continue; // Not DD
        }

        // Check if ours
        if (!IsMine(txout)) {
            continue; // Not ours
        }

        // Add to DD UTXOs
        COutPoint new_utxo(tx->GetHash(), i);
        dd_utxos[new_utxo] = dd_amount;

        // Persist
        WalletBatch batch(m_wallet->GetDatabase());
        batch.WriteDDUTXO(new_utxo, dd_amount);

        // Update balance
        CAmount new_balance = GetTotalDDBalance();
        batch.WriteDDBalance(GetDDAddress(), new_balance);

        // Add to history
        DDTransaction ddtx;
        ddtx.txid = tx->GetHash().ToString();
        ddtx.amount = dd_amount;
        ddtx.timestamp = GetTime();
        ddtx.confirmations = 0;
        ddtx.incoming = true;
        ddtx.category = "receive";
        batch.WriteDDTransaction(ddtx);

        LogPrintf("DigiDollar: Received %d DD in %s:%d\n",
                  dd_amount, tx->GetHash().ToString(), i);
    }
}
```

---

### FIX #6: Update Unit Tests

**Files**: src/test/digidollar_transfer_tests.cpp

**TDD Steps**:
1. Update all tests to NOT expect position inactivation
2. Add test for time-lock preservation (see example above)
3. Add test for DD UTXO tracking

**Changes Needed**:
- REMOVE assertions expecting `is_active == false` after transfer
- ADD assertions verifying `is_active == true` after transfer
- ADD tests for dd_utxos map updates

---

## Quality Requirements (EVERY Fix)

### After Implementation:

**1. Compile Check**:
```bash
make -j$(nproc) src/qt/digibyte-qt
# MUST succeed with no errors
```

**2. Test Check**:
```bash
./src/test/test_digibyte --run_test=digidollar_*
# ALL tests MUST pass
```

**3. No Warnings**:
- Zero compiler warnings
- Zero test warnings

**4. Git Commit**:
```bash
git add [modified files]
git commit -m "GREEN: Fix #X - [description]"
```

## Report Format

### On Completion:

```markdown
## FIX #X Complete ✅

**RED Phase**:
- Test file: src/test/[file]:LINE
- Initial failure: [error message]

**GREEN Phase**:
- Implementation: src/wallet/[file]:LINES
- Test now passes ✅

**REFACTOR Phase**:
- Added logging
- Improved error handling

**Verification**:
- Compilation: ✅ SUCCESS
- All tests: ✅ PASS (X/X)
- Warnings: ✅ NONE

**Files Modified**:
- [list files with line ranges]
```

### If Blocked:

```markdown
## FIX #X Blocked ❌

**Issue**: [problem description]
**Error**: [compilation/test error]
**Need**: [what's needed to proceed]
```

## Remember

- **TDD is MANDATORY**: Write failing test FIRST
- **Time-locks NEVER change during transfers**: Core concept!
- **Compile after EVERY change**: No broken builds
- **All tests must pass**: No regressions
- **Report back immediately**: Keep orchestrator informed

---

**Sub-Agent Version**: 2.0 - Bug Fix Mode
**Your Mission**: Fix ONE bug, follow TDD, report success
**Success**: Test passes + wallet compiles + no regressions
