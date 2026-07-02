# DigiDollar Receive Sub-Agent - Implementation Guide

## Your Role
You are a **Sub-Agent** assigned to implement ONE specific component of DigiDollar receiving functionality. You will use the **EXISTING FAILING TEST** to guide your implementation.

## 🎯 MISSION: Make test/functional/digidollar_transfer.py PASS

**Critical Files**:
1. **test/functional/digidollar_transfer.py** - THE TEST (already exists, currently fails)
2. **DIGIDOLLAR_RECEIVE_TASKS.md** - Complete task breakdown

### The Problem - Proven by Failing Test

**Test File**: `test/functional/digidollar_transfer.py`

**Current Status** (lines 126-158):
```python
def test_simple_transfers(self):
    # Setup works ✅
    sender_initial = self.nodes[0].getdigidollarbalance()  # ✅ 5000
    receiver_initial = self.nodes[3].getdigidollarbalance()  # ✅ 0
    receiver_address = self.nodes[3].getdigidollaraddress()  # ✅ Works

    # Sending works ✅
    transfer_amount_cents = 1000
    result = self.nodes[0].senddigidollar(receiver_address, transfer_amount_cents)  # ✅

    # Confirmation works ✅
    self.nodes[0].generate(1)  # ✅
    self.sync_all()  # ✅

    # Receiving BROKEN ❌
    sender_final = self.nodes[0].getdigidollarbalance()  # ✅ 4000 (correct!)
    receiver_final = self.nodes[3].getdigidollarbalance()  # ❌ 0 (SHOULD BE 1000!)

    assert_equal(receiver_final, expected_receiver)  # ❌ FAILS: 0 != 1000
```

**This test IS your RED phase!**

## Implementation Strategy

### 🔴 RED Phase: Test Already Fails!

**Before you write ANY code:**
```bash
# Run the test
./test/functional/digidollar_transfer.py

# Expected output:
# ...
# AssertionError: 0 != 1000  ← Receiving is broken!
```

This proves receiving doesn't work. **You don't need to write this test - it exists!**

### 🟢 GREEN Phase: Make Test Pass

**For each task, your process is:**

1. **Understand current failure**:
   ```bash
   ./test/functional/digidollar_transfer.py
   # Note WHERE it fails, WHAT the error is
   ```

2. **Implement your code**:
   - Write the code for your assigned task
   - Follow the implementation guide below

3. **COMPILE** (MANDATORY - NO EXCEPTIONS):
   ```bash
   make -j$(nproc) src/qt/digibyte-qt

   # If this fails:
   # - Read the error carefully
   # - Fix the error
   # - Compile again
   # - DO NOT PROCEED until compilation succeeds
   ```

4. **Test again**:
   ```bash
   ./test/functional/digidollar_transfer.py

   # Check:
   # - Does it fail at the same place? (no progress)
   # - Does it fail further? (progress!)
   # - Does it pass? (success!)
   ```

5. **Report progress**:
   - Compilation: SUCCESS/FAILED
   - Test status: Line number where it fails or PASS
   - What changed

### ♻️ REFACTOR Phase: Improve While Passing

Only after test passes (or advances further):
- Add better logging
- Improve error handling
- Add edge case checks

**MUST compile and test after refactoring:**
```bash
make -j$(nproc) src/qt/digibyte-qt  # MUST succeed
./test/functional/digidollar_transfer.py  # MUST still pass
```

## Task-Specific Instructions

### TASK 1: Transaction Scanning Hook

**Files**: `src/wallet/wallet.cpp`, `src/wallet/digidollarwallet.h`

**Implementation**:
```cpp
// In wallet.cpp - CWallet::AddToWalletIfInvolvingMe()
bool CWallet::AddToWalletIfInvolvingMe(const CTransactionRef& ptx, ...) {
    // ... existing code ...

    // NEW: Check for DigiDollar transactions
    if (m_dd_wallet) {
        m_dd_wallet->ScanForIncomingDD(ptx);
    }

    return true;
}
```

**Add to digidollarwallet.h**:
```cpp
class DigiDollarWallet {
public:
    void ScanForIncomingDD(const CTransactionRef& tx);
};
```

**Stub implementation in digidollarwallet.cpp**:
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    if (!tx) return;

    LogPrint(BCLog::DD, "DigiDollar: Scanning %s for incoming DD\n",
             tx->GetHash().ToString());

    // TODO: Tasks 2-6 will fill this in
}
```

**Compile**:
```bash
make -j$(nproc) src/qt/digibyte-qt
# MUST succeed
```

**Test**:
```bash
./test/functional/digidollar_transfer.py
# Should still fail at line 158, but may show scan log
```

---

### TASK 2: DD Output Detection Logic

**Files**: `src/wallet/digidollarwallet.cpp`, `src/wallet/digidollarwallet.h`

**Add to digidollarwallet.h**:
```cpp
class DigiDollarWallet {
public:
    bool IsDigiDollarOutput(const CTxOut& txout, CAmount& dd_amount) const;
};
```

**Implement in digidollarwallet.cpp**:
```cpp
bool DigiDollarWallet::IsDigiDollarOutput(const CTxOut& txout, CAmount& dd_amount) const {
    dd_amount = 0;

    // DD outputs are P2TR
    if (!txout.scriptPubKey.IsPayToTaproot()) {
        return false;
    }

    // Extract DD amount using existing utility
    if (!ExtractDDAmount(txout.scriptPubKey, dd_amount)) {
        return false;
    }

    // Validate amount
    if (dd_amount <= 0 || dd_amount > MAX_DD_AMOUNT) {
        return false;
    }

    LogPrint(BCLog::DD, "Detected DD output: %d cents\n", dd_amount);
    return true;
}
```

**Update ScanForIncomingDD**:
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    if (!tx) return;

    LogPrint(BCLog::DD, "DigiDollar: Scanning %s\n", tx->GetHash().ToString());

    for (size_t i = 0; i < tx->vout.size(); i++) {
        CAmount dd_amount = 0;
        if (IsDigiDollarOutput(tx->vout[i], dd_amount)) {
            LogPrint(BCLog::DD, "Found DD output at %d: %d cents\n", i, dd_amount);
            // TODO: Task 3 - verify ownership
        }
    }
}
```

**Compile**:
```bash
make -j$(nproc) src/qt/digibyte-qt
```

**Test**:
```bash
./test/functional/digidollar_transfer.py
# Should still fail but log "Found DD output"
```

---

### TASK 3: Ownership Verification

**Add to digidollarwallet.h**:
```cpp
class DigiDollarWallet {
public:
    bool IsOurDDOutput(const CTxOut& txout) const;
};
```

**Implement**:
```cpp
bool DigiDollarWallet::IsOurDDOutput(const CTxOut& txout) const {
    // Extract destination
    CTxDestination dest;
    if (!ExtractDestination(txout.scriptPubKey, dest)) {
        return false;
    }

    // Check if we can spend it
    isminetype mine = m_wallet->IsMine(dest);

    LogPrint(BCLog::DD, "Ownership check: %s (mine=%d)\n",
             EncodeDestination(dest), mine);

    return mine == ISMINE_SPENDABLE;
}
```

**Update ScanForIncomingDD**:
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    if (!tx) return;

    for (size_t i = 0; i < tx->vout.size(); i++) {
        CAmount dd_amount = 0;
        if (!IsDigiDollarOutput(tx->vout[i], dd_amount)) {
            continue;
        }

        if (!IsOurDDOutput(tx->vout[i])) {
            continue;  // Not ours
        }

        LogPrint(BCLog::DD, "Received DD output: %d cents at %s:%d\n",
                 dd_amount, tx->GetHash().ToString(), i);
        // TODO: Task 4 - add to UTXOs
    }
}
```

**Compile & Test**:
```bash
make -j$(nproc) src/qt/digibyte-qt
./test/functional/digidollar_transfer.py
# Should log "Received DD output" but balance still 0
```

---

### TASK 4: DD UTXO Addition

**Add to digidollarwallet.h**:
```cpp
class DigiDollarWallet {
public:
    void AddDDUTXO(const COutPoint& outpoint, CAmount dd_amount);
};
```

**Implement**:
```cpp
void DigiDollarWallet::AddDDUTXO(const COutPoint& outpoint, CAmount dd_amount) {
    if (outpoint.IsNull() || dd_amount <= 0) {
        return;
    }

    // Add to map
    dd_utxos[outpoint] = dd_amount;

    // Persist (WriteDDUTXO already exists from send implementation)
    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteDDUTXO(outpoint, dd_amount);

    LogPrintf("DigiDollar: Added DD UTXO %s:%d (%d cents)\n",
              outpoint.hash.ToString(), outpoint.n, dd_amount);
}
```

**Update ScanForIncomingDD**:
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    if (!tx) return;

    for (size_t i = 0; i < tx->vout.size(); i++) {
        CAmount dd_amount = 0;
        if (!IsDigiDollarOutput(tx->vout[i], dd_amount)) {
            continue;
        }

        if (!IsOurDDOutput(tx->vout[i])) {
            continue;
        }

        // Add to UTXOs
        COutPoint outpoint(tx->GetHash(), i);
        AddDDUTXO(outpoint, dd_amount);

        // TODO: Task 5 - update balance
    }
}
```

**Compile & Test**:
```bash
make -j$(nproc) src/qt/digibyte-qt
./test/functional/digidollar_transfer.py
# UTXO added but GetTotalDDBalance() not updated yet
```

---

### TASK 5: Balance Update System

**Add to digidollarwallet.h**:
```cpp
class DigiDollarWallet {
public:
    void UpdateBalance();
};
```

**Implement**:
```cpp
void DigiDollarWallet::UpdateBalance() {
    // Recalculate from all DD UTXOs (GetTotalDDBalance already exists)
    CAmount new_balance = GetTotalDDBalance();

    // Persist
    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteDDBalance(GetDDAddress(), new_balance);

    LogPrintf("DigiDollar: Balance updated to %d cents\n", new_balance);
}
```

**Update ScanForIncomingDD**:
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    if (!tx) return;

    bool found_incoming = false;

    for (size_t i = 0; i < tx->vout.size(); i++) {
        CAmount dd_amount = 0;
        if (!IsDigiDollarOutput(tx->vout[i], dd_amount)) {
            continue;
        }

        if (!IsOurDDOutput(tx->vout[i])) {
            continue;
        }

        COutPoint outpoint(tx->GetHash(), i);
        AddDDUTXO(outpoint, dd_amount);
        found_incoming = true;
    }

    if (found_incoming) {
        UpdateBalance();  // ← THIS SHOULD FIX THE TEST!
        // TODO: Task 6 - add to history
    }
}
```

**Compile & Test**:
```bash
make -j$(nproc) src/qt/digibyte-qt
./test/functional/digidollar_transfer.py

# 🎉 SHOULD PASS AT LINE 158! 🎉
# receiver_final should now be 1000!
```

---

### TASK 6: Transaction History

**Add to digidollarwallet.h**:
```cpp
class DigiDollarWallet {
public:
    void AddIncomingTransaction(const CTransactionRef& tx, CAmount dd_amount);
};
```

**Implement**:
```cpp
void DigiDollarWallet::AddIncomingTransaction(
    const CTransactionRef& tx,
    CAmount dd_amount)
{
    DDTransaction ddtx;
    ddtx.txid = tx->GetHash().ToString();
    ddtx.amount = dd_amount;
    ddtx.timestamp = GetTime();
    ddtx.confirmations = 0;
    ddtx.incoming = true;
    ddtx.category = "receive";

    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteDDTransaction(ddtx);

    LogPrintf("DigiDollar: Recorded receive TX %s: %d DD\n",
              tx->GetHash().ToString(), dd_amount);
}
```

**Update ScanForIncomingDD**:
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    if (!tx) return;

    bool found_incoming = false;
    CAmount total_received = 0;

    for (size_t i = 0; i < tx->vout.size(); i++) {
        CAmount dd_amount = 0;
        if (!IsDigiDollarOutput(tx->vout[i], dd_amount)) {
            continue;
        }

        if (!IsOurDDOutput(tx->vout[i])) {
            continue;
        }

        COutPoint outpoint(tx->GetHash(), i);
        AddDDUTXO(outpoint, dd_amount);
        total_received += dd_amount;
        found_incoming = true;
    }

    if (found_incoming) {
        UpdateBalance();
        AddIncomingTransaction(tx, total_received);
    }
}
```

**Compile & Test**:
```bash
make -j$(nproc) src/qt/digibyte-qt
./test/functional/digidollar_transfer.py
# Should pass with transaction in history
```

---

### TASK 7: GUI Notification

**Files**: `src/qt/walletmodel.cpp`, `src/qt/digidollaroverviewwidget.cpp`

**In walletmodel.cpp**:
```cpp
void WalletModel::pollBalanceChanged() {
    // ... existing DGB balance ...

    // Check DD balance
    if (wallet().m_dd_wallet) {
        CAmount dd_balance = wallet().m_dd_wallet->GetTotalDDBalance();
        if (dd_balance != cachedDDBalance) {
            cachedDDBalance = dd_balance;
            Q_EMIT digidollarBalanceChanged(dd_balance);
        }
    }
}
```

**In digidollaroverviewwidget.cpp**:
```cpp
void DigiDollarOverviewWidget::setModel(WalletModel *model) {
    this->model = model;

    if (model && model->wallet().m_dd_wallet) {
        connect(model, &WalletModel::digidollarBalanceChanged,
                this, &DigiDollarOverviewWidget::updateBalance);

        updateBalance(model->wallet().m_dd_wallet->GetTotalDDBalance());
    }
}

void DigiDollarOverviewWidget::updateBalance(const CAmount& balance) {
    ui->labelDDBalance->setText(
        BitcoinUnits::formatWithUnit(BitcoinUnits::DD, balance)
    );
}
```

**Compile & Test**:
```bash
make -j$(nproc) src/qt/digibyte-qt
# Manual GUI test in regtest
```

## Compilation Protocol (MANDATORY)

**After EVERY code change:**

```bash
# 1. COMPILE
make -j$(nproc) src/qt/digibyte-qt

# 2. Check result
# If SUCCESS: proceed to testing
# If FAILED:
#   - Read error message
#   - Fix the error
#   - Compile again
#   - DO NOT proceed until compilation succeeds
```

**Common Compilation Errors:**

1. **Missing declaration**: Add to .h file
2. **Undefined reference**: Add implementation to .cpp file
3. **Type mismatch**: Check function signatures match
4. **Missing include**: Add required headers

## Testing Protocol

**After successful compilation:**

```bash
# Run the functional test
./test/functional/digidollar_transfer.py

# Observe:
# - WHERE does it fail? (line number)
# - WHAT is the error? (assertion message)
# - Has it progressed? (failing later = progress)
# - Does it pass? (success!)
```

**Test Progress Indicators:**

- **Task 1**: Still fails at 158, may see "Scanning" log
- **Task 2**: Still fails at 158, see "Found DD output" log
- **Task 3**: Still fails at 158, see "Received DD output" log
- **Task 4**: Still fails at 158, see "Added DD UTXO" log
- **Task 5**: **PASSES at 158!** Balance = 1000 ✅
- **Task 6**: Passes, transaction in history
- **Task 7**: GUI shows balance

## Report Format

### On Completion:

```markdown
## Receive Task #X Complete ✅

**Compilation**:
- Command: `make -j$(nproc) src/qt/digibyte-qt`
- Result: SUCCESS
- Time: 2m 15s
- Warnings: None

**Test Results**:
- Command: `./test/functional/digidollar_transfer.py`
- Previous status: FAIL at line 158 (balance = 0)
- New status: FAIL at line 158 (balance = 0, but UTXO added in logs)
- Progress: UTXO addition working, need balance update

**Files Modified**:
- src/wallet/digidollarwallet.h (+5 lines)
- src/wallet/digidollarwallet.cpp (+35 lines)

**Logs Observed**:
```
DigiDollar: Scanning abc123... for incoming DD
DigiDollar: Found DD output at 0: 1000 cents
DigiDollar: Received DD output: 1000 cents at abc123:0
DigiDollar: Added DD UTXO abc123:0 (1000 cents)
```

**Next**: Ready for Task 5 (Balance Update)
```

### If Compilation Fails:

```markdown
## Receive Task #X Blocked - COMPILATION FAILED ❌

**Error**:
```
src/wallet/digidollarwallet.cpp:145:5: error: 'IsDigiDollarOutput' was not declared in this scope
     if (IsDigiDollarOutput(tx->vout[i], dd_amount)) {
     ^~~~~~~~~~~~~~~~~~
```

**Root Cause**: Method declared but not implemented

**Fix Attempted**: Added implementation to digidollarwallet.cpp

**Status**: Recompiling...
```

## Remember

- **THE TEST IS YOUR RED PHASE**: Don't write new tests, use what exists
- **COMPILE ALWAYS**: After every change, no exceptions
- **TEST SHOWS PROGRESS**: Each task moves failure forward or fixes it
- **Task 5 is critical**: Balance update makes test pass
- **Report everything**: Compilation status, test status, progress
- **No regressions**: Test validates sending still works

---

**Sub-Agent Version**: 2.0 - Using Existing Functional Test
**Your Mission**: Make test/functional/digidollar_transfer.py PASS
**Success**: Compilation succeeds + Test passes + Receiving works
