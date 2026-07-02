# DigiDollar Send/Receive Implementation Tasks

## CRITICAL BUGS DISCOVERED - MUST FIX

### Current Implementation Status
**BROKEN** - Send/Receive does NOT work. Critical architectural flaws found:

#### 🔴 BUG #1: NO BLOCKCHAIN TRANSACTIONS
**Location**: `digidollarwallet.cpp:219-414` (TransferDigiDollar with string params)
- Transaction is built correctly by TransferTxBuilder
- **BUT NEVER BROADCAST TO NETWORK**
- Just sets `txid = result.tx.GetHash().ToString()` and returns
- Missing: Call to `CommitTransaction()` or `BroadcastTransaction()`

#### 🔴 BUG #2: BROKEN DD UTXO MODEL
**Location**: `digidollarwallet.cpp:994` (GetDDUTXOs)
```cpp
COutPoint dd_outpoint(timelock.dd_timelock_id, 1);  // DD always at index 1
```
- Assumes DD UTXO is ALWAYS `(mint_txid, 1)` from original mint
- Only works for FIRST spend after mint
- After one transfer, DD exists in NEW transaction outputs
- No tracking of DD outputs from transfer transactions
- **Result**: DD can only be spent once, then becomes "unspendable"

#### 🔴 BUG #3: DESTROYS TIME-LOCKS ON TRANSFER
**Location**: `digidollarwallet.cpp:314-392`
- Marks mint position as "inactive" when DD is transferred
- Creates fake "change position" with split collateral
- **WRONG**: Time-lock should stay ACTIVE until redemption
- Only DD ownership changes, NOT the locked DGB!

#### 🔴 BUG #4: NO RECEIVING LOGIC
- No code to detect incoming DD transfers
- No blockchain scanning for DD outputs to our addresses
- Receiving wallet has NO IDEA DD was sent to it

### THE CORRECT DIGIDOLLAR MODEL

#### Mint Transaction (Creates Time-Lock + DD)
```
Mint TX: abc123...
├─ vout[0]: 1000 DGB (Time-Locked until maturity) ← STAYS HERE UNTIL REDEMPTION
└─ vout[1]: 500 DD (spendable DigiDollar token)   ← CAN BE TRANSFERRED
```

#### Transfer Transaction (Moves DD, NOT Collateral!)
```
Transfer TX: def456...
Inputs:
  ├─ vin[0]: abc123:1 (spending 500 DD from mint)  ← Spending DD token
  └─ vin[1]: fee_utxo (DGB for fees)
Outputs:
  ├─ vout[0]: 300 DD (to recipient)                ← New DD UTXO
  ├─ vout[1]: 200 DD (change back to sender)       ← New DD UTXO
  └─ vout[2]: DGB change
```

**CRITICAL**: The time-locked DGB (mint TX vout[0]) NEVER MOVES!

#### What the Wallet Must Track

1. **Time-Lock Positions** (collateral_positions map)
   - Represents locked DGB from MINT transactions
   - Stays ACTIVE until redemption
   - NEVER modified during transfers

2. **DD UTXOs** (NEW - currently missing!)
   - Spendable DD outputs from BOTH mint AND transfer transactions
   - Can come from:
     - vout[1] of mint transactions
     - vout[0], vout[1], etc. of transfer transactions
   - Must track actual blockchain UTXOs, not assume position

3. **Balance Calculation**
   - Balance = Sum of spendable DD UTXOs (NOT sum of active positions!)

## IMPLEMENTATION PLAN - 6 CRITICAL FIXES

### FIX #1: Implement DD UTXO Tracking System
**Priority**: CRITICAL (Foundation for everything)

**Problem**: GetDDUTXOs() assumes DD is always (mint_tx, 1)
**Solution**: Track actual DD UTXOs from blockchain

**Tasks**:
1. Add `std::map<COutPoint, CAmount> dd_utxos` to DigiDollarWallet
   - Maps (txid, vout) → DD amount
   - Tracks ALL DD outputs (from mint AND transfers)

2. Update on Mint:
   - Add (mint_tx, 1) → dd_amount to dd_utxos map
   - Persist via WriteDDUTXO()

3. Update on Send:
   - Remove spent UTXOs from dd_utxos map
   - Add change outputs from transaction to dd_utxos map
   - DO NOT mark time-lock as inactive!

4. Update on Receive:
   - Scan transaction outputs for DD to our addresses
   - Add received UTXOs to dd_utxos map
   - Update balance

5. Update GetDDUTXOs():
```cpp
std::vector<DDUtxo> DigiDollarWallet::GetDDUTXOs() const {
    std::vector<DDUtxo> utxos;
    for (const auto& [outpoint, dd_amount] : dd_utxos) {
        // Verify UTXO is still unspent in wallet
        if (IsUTXOSpendable(outpoint)) {
            utxos.emplace_back(outpoint, dd_amount);
        }
    }
    return utxos;
}
```

6. Update GetTotalDDBalance():
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

**Files to Modify**:
- src/wallet/digidollarwallet.h (add dd_utxos map)
- src/wallet/digidollarwallet.cpp (GetDDUTXOs, GetTotalDDBalance)
- src/wallet/walletdb.h (add WriteDDUTXO/ReadDDUTXO)
- src/wallet/walletdb.cpp (implement UTXO persistence)

---

### FIX #2: Fix Transfer to Preserve Time-Locks
**Priority**: CRITICAL

**Problem**: Transfer marks positions inactive and creates fake change positions
**Solution**: Leave time-locks alone, only manage DD UTXOs

**Current BAD Code** (lines 309-392):
```cpp
// Mark spent input positions as inactive  ← WRONG!
for (const auto& dd_utxo : dd_utxos) {
    UpdatePositionStatus(dd_timelock_id, false);  ← DO NOT DO THIS!
}

// Create change position  ← WRONG!
WalletCollateralPosition changePosition(changeTxId, dd_change, ...);  ← NO!
AddCollateralPosition(changePosition);  ← WRONG!
```

**New CORRECT Code**:
```cpp
// CRITICAL: Time-locks (collateral positions) NEVER change during transfers!
// Only DD UTXOs move. The locked DGB stays in place until redemption.

// Remove spent DD UTXOs from tracking
for (const auto& spent_utxo : params.ddUtxos) {
    dd_utxos.erase(spent_utxo);
    LogPrintf("DigiDollar: Marked DD UTXO %s:%d as spent\n",
              spent_utxo.hash.ToString(), spent_utxo.n);
}

// Add new DD UTXOs from transaction outputs (for change)
// Extract DD outputs from result.tx and add to dd_utxos map
for (size_t i = 0; i < result.tx.vout.size(); i++) {
    CAmount dd_amount = 0;
    if (ExtractDDAmount(result.tx.vout[i].scriptPubKey, dd_amount)) {
        // Check if this output is to our address (change)
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

**Files to Modify**:
- src/wallet/digidollarwallet.cpp (TransferDigiDollar - both versions)

---

### FIX #3: Implement Transaction Broadcasting
**Priority**: CRITICAL

**Problem**: Transactions built but never broadcast
**Solution**: Actually send transactions to network

**Add after transaction is built** (line ~310):
```cpp
// Build transaction (already working)
DigiDollar::TxBuilderResult result = builder.BuildTransferTransaction(params);
if (!result.success) {
    error = result.error;
    return false;
}

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

**Files to Modify**:
- src/wallet/digidollarwallet.cpp (TransferDigiDollar)

**Dependencies**:
- Need access to chain interface for broadcasting
- May need to pass CWallet pointer or chain pointer

---

### FIX #4: Implement Receive Detection
**Priority**: CRITICAL

**Problem**: No incoming transaction detection
**Solution**: Scan blockchain for DD outputs to our addresses

**New Method**:
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    LogPrintf("DigiDollar: Scanning transaction %s for incoming DD\n",
              tx->GetHash().ToString());

    // Check each output
    for (size_t i = 0; i < tx->vout.size(); i++) {
        const CTxOut& txout = tx->vout[i];

        // Extract DD amount from scriptPubKey
        CAmount dd_amount = 0;
        if (!ExtractDDAmount(txout.scriptPubKey, dd_amount)) {
            continue; // Not a DD output
        }

        // Check if output is to our address
        if (!IsMine(txout)) {
            continue; // Not ours
        }

        // Add to our DD UTXOs
        COutPoint new_utxo(tx->GetHash(), i);
        dd_utxos[new_utxo] = dd_amount;

        // Persist to database
        WalletBatch batch(m_wallet->GetDatabase());
        batch.WriteDDUTXO(new_utxo, dd_amount);

        // Update balance
        CAmount new_balance = GetTotalDDBalance();
        batch.WriteDDBalance(GetDDAddress(), new_balance);

        // Add to transaction history
        DDTransaction ddtx;
        ddtx.txid = tx->GetHash().ToString();
        ddtx.amount = dd_amount;
        ddtx.timestamp = GetTime();
        ddtx.confirmations = 0;
        ddtx.incoming = true;
        ddtx.category = "receive";
        batch.WriteDDTransaction(ddtx);

        LogPrintf("DigiDollar: Received %d DD cents in %s:%d\n",
                  dd_amount, tx->GetHash().ToString(), i);

        // Notify UI
        NotifyDDTransactionChanged(this, ddtx);
    }
}
```

**Hook into Wallet Transaction Processing**:
```cpp
// In wallet transaction processor (when new tx confirmed or enters mempool)
void DigiDollarWallet::ProcessTransaction(const CTransactionRef& tx) {
    // Check if this is a DD transaction
    if (IsDigiDollarTransaction(tx)) {
        ScanForIncomingDD(tx);
    }
}
```

**Files to Create/Modify**:
- src/wallet/digidollarwallet.cpp (add ScanForIncomingDD, ProcessTransaction)
- src/wallet/digidollarwallet.h (declare methods)
- src/wallet/wallet.cpp (call DigiDollarWallet::ProcessTransaction when tx arrives)

---

### FIX #5: Add DD UTXO Database Persistence
**Priority**: CRITICAL (Required by Fix #1)

**Problem**: DD UTXOs not persisted to wallet.dat
**Solution**: Add UTXO read/write to WalletBatch

**Add to walletdb.h**:
```cpp
/** Write DD UTXO to database */
bool WriteDDUTXO(const COutPoint& outpoint, const CAmount& dd_amount);

/** Read DD UTXO from database */
bool ReadDDUTXO(const COutPoint& outpoint, CAmount& dd_amount);

/** Erase DD UTXO from database */
bool EraseDDUTXO(const COutPoint& outpoint);
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

**Add to LoadFromDatabase**:
```cpp
// Load DD UTXOs from database
void DigiDollarWallet::LoadFromDatabase() {
    WalletBatch batch(m_wallet->GetDatabase());

    // Load existing time-locks (already working)
    // ...

    // Load DD UTXOs (NEW)
    Dbc* cursor = batch.GetCursor();
    while (true) {
        CDataStream ssKey, ssValue;
        DatabaseCursor::Status status = cursor->ReadAtCursor(ssKey, ssValue);
        if (status == DatabaseCursor::DONE) break;

        std::string key_type;
        ssKey >> key_type;

        if (key_type == DBKeys::DD_UTXO) {
            COutPoint outpoint;
            CAmount dd_amount;
            ssKey >> outpoint;
            ssValue >> dd_amount;
            dd_utxos[outpoint] = dd_amount;
            LogPrintf("DigiDollar: Loaded DD UTXO %s:%d (%d cents)\n",
                      outpoint.hash.ToString(), outpoint.n, dd_amount);
        }
    }

    LogPrintf("DigiDollar: Loaded %d DD UTXOs from database\n", dd_utxos.size());
}
```

**Files to Modify**:
- src/wallet/walletdb.h (declare methods)
- src/wallet/walletdb.cpp (implement persistence)
- src/wallet/digidollarwallet.cpp (LoadFromDatabase)

---

### FIX #6: Update Unit Tests
**Priority**: HIGH (Verify fixes work)

**Problem**: Existing tests assume broken model
**Solution**: Update tests to match correct architecture

**Update**: src/test/digidollar_transfer_tests.cpp

**Key Changes**:
1. Tests should NOT expect positions to be marked inactive after transfer
2. Tests should verify DD UTXOs are tracked correctly
3. Tests should verify time-locks stay active
4. Add test for receiving DD

**Example Test**:
```cpp
BOOST_AUTO_TEST_CASE(test_transfer_preserves_timelock)
{
    // Setup: Create mint position
    uint256 mint_txid = CreateMintPosition(wallet, 10000); // 100 DD

    // Verify initial state
    BOOST_CHECK_EQUAL(wallet.GetTotalDDBalance(), 10000);
    auto timelocks = wallet.GetDDTimeLocks(true);
    BOOST_CHECK_EQUAL(timelocks.size(), 1);
    BOOST_CHECK(timelocks[0].is_active);

    // Transfer 50 DD
    std::string txid, error;
    CDigiDollarAddress recipient("DD1test...");
    bool success = wallet.TransferDigiDollar(recipient, 5000, txid, error);

    BOOST_CHECK(success);

    // Verify time-lock is STILL ACTIVE (critical!)
    timelocks = wallet.GetDDTimeLocks(true);
    BOOST_CHECK_EQUAL(timelocks.size(), 1);
    BOOST_CHECK(timelocks[0].is_active); // Must still be active!

    // Verify balance decreased
    BOOST_CHECK_EQUAL(wallet.GetTotalDDBalance(), 5000); // 50 DD remaining

    // Verify DD UTXOs updated
    auto utxos = wallet.GetDDUTXOs();
    // Should have change UTXO from transfer, NOT original mint UTXO
    bool found_change = false;
    for (const auto& utxo : utxos) {
        if (utxo.hash.ToString() == txid) {
            found_change = true;
            BOOST_CHECK_EQUAL(utxo.dd_amount, 5000); // 50 DD change
        }
    }
    BOOST_CHECK(found_change);
}
```

**Files to Modify**:
- src/test/digidollar_transfer_tests.cpp (update all transfer tests)
- Add new test file: src/test/digidollar_utxo_tests.cpp

---

## IMPLEMENTATION ORDER (CRITICAL PATH)

### Phase 1: Foundation (MUST DO FIRST)
1. ✅ FIX #5: DD UTXO Database Persistence (walletdb.h/cpp)
2. ✅ FIX #1: DD UTXO Tracking System (digidollarwallet.h/cpp)

### Phase 2: Core Fixes (SEQUENTIAL)
3. ✅ FIX #2: Fix Transfer Logic (preserve time-locks)
4. ✅ FIX #3: Transaction Broadcasting

### Phase 3: Receiving (DEPENDS ON PHASE 1 & 2)
5. ✅ FIX #4: Receive Detection

### Phase 4: Testing & Validation
6. ✅ FIX #6: Update Unit Tests
7. ✅ Compile and test
8. ✅ End-to-end Qt testing

---

## SUCCESS CRITERIA

### Must ALL Pass:
- [ ] Can mint 100 DD in regtest Qt wallet
- [ ] Can send 50 DD to another address
- [ ] Sending wallet shows 50 DD remaining
- [ ] **Time-lock position stays ACTIVE after send** (critical!)
- [ ] Transaction appears in blockchain (not just in-memory)
- [ ] Can send DD to self, receive it properly
- [ ] Restart wallet, balance still 50 DD
- [ ] Can send the remaining 50 DD
- [ ] All unit tests pass
- [ ] All functional tests pass

---

## FILES TO MODIFY - SUMMARY

### Header Files
- src/wallet/digidollarwallet.h (add dd_utxos map, new methods)
- src/wallet/walletdb.h (DD UTXO persistence methods)

### Implementation Files
- src/wallet/digidollarwallet.cpp (ALL 6 fixes)
- src/wallet/walletdb.cpp (UTXO persistence)
- src/wallet/wallet.cpp (hook receive detection)

### Test Files
- src/test/digidollar_transfer_tests.cpp (update existing tests)
- src/test/digidollar_utxo_tests.cpp (NEW - UTXO tracking tests)

---

**Document Version**: 2.0 - CRITICAL BUG FIX EDITION
**Last Updated**: 2025-10-03
**Status**: Ready for Implementation - FIX THESE BUGS!
