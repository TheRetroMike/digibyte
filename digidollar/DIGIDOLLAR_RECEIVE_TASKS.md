# DigiDollar Receive Implementation - Complete Task Breakdown

## MISSION: Complete DigiDollar Receiving Functionality

### Current Status Assessment

**What Works** ✅:
- DigiDollar minting (lock DGB → create DD)
- DigiDollar sending (transfer DD between addresses)
- DD UTXO tracking in sender's wallet
- Transaction broadcasting to network
- Time-lock preservation during transfers
- Database persistence for DD UTXOs

**What's Broken** ❌:
- **No receive detection** - Receiving wallet doesn't know DD was sent to it
- **No balance updates** - Recipient's balance stays at 0 after receiving
- **No transaction history** - Incoming DD transactions not recorded
- **No GUI updates** - Interface doesn't show received DD
- **No persistence of received DD** - Even if detected, not saved to DB

### The Core Problem

**Scenario**: Alice sends 300 DD to Bob's address (DD1abc...)

**Current Behavior** (BROKEN):
```
Alice's Wallet:
  ✅ Builds transaction
  ✅ Broadcasts to network
  ✅ Updates own balance (500 → 200 DD change)
  ✅ Transaction appears in Alice's history

Network:
  ✅ Transaction enters mempool
  ✅ Gets included in block
  ✅ Relays to all nodes

Bob's Wallet:
  ❌ No detection of incoming transaction
  ❌ Balance stays at 0
  ❌ No transaction in history
  ❌ GUI shows nothing
  ❌ DD is "lost" to Bob (but spendable by whoever has the key)
```

**Expected Behavior** (GOAL):
```
Bob's Wallet:
  ✅ Detects transaction with DD output to Bob's address
  ✅ Adds DD UTXO to tracking map
  ✅ Updates balance (0 → 300 DD)
  ✅ Adds transaction to history
  ✅ Persists to database
  ✅ Notifies GUI
  ✅ GUI displays 300 DD balance
  ✅ Bob can now spend the 300 DD
```

## The Correct Architecture

### How DigiDollar Receiving SHOULD Work

#### 1. Transaction Flow
```
Alice sends 300 DD to Bob (DD1abc...)

Transfer TX: def456...
├─ Inputs:
│  ├─ vin[0]: abc123:1 (500 DD from Alice's mint)
│  └─ vin[1]: fee_utxo (DGB for fees)
└─ Outputs:
   ├─ vout[0]: 300 DD → DD1abc... (Bob's address) ← BOB MUST DETECT THIS
   ├─ vout[1]: 200 DD → Alice's change address
   └─ vout[2]: DGB change
```

#### 2. Wallet Processing Pipeline

**When Transaction Enters Wallet**:
```
CWallet::AddToWalletIfInvolvingMe(tx)
  ├─ Standard processing (DGB inputs/outputs)
  ├─ Add to wallet transaction list
  └─ NEW: DigiDollarWallet::ScanForIncomingDD(tx) ← MISSING!
      ├─ For each vout in tx:
      │  ├─ IsDigiDollarOutput(vout) ← TASK 2
      │  ├─ IsOurDDOutput(vout) ← TASK 3
      │  └─ If both true:
      │     ├─ AddDDUTXO(outpoint, amount) ← TASK 4
      │     ├─ UpdateBalance() ← TASK 5
      │     ├─ AddIncomingTransaction() ← TASK 6
      │     └─ NotifyGUI() ← TASK 7
      └─ Return
```

#### 3. Data Flow

**Detection → Storage → Display**:
```
1. Detect DD Output
   ↓
2. Verify Ownership (our address?)
   ↓
3. Add to dd_utxos map: (def456:0) → 30000 (300 DD)
   ↓
4. Persist to database (WriteDDUTXO)
   ↓
5. Update balance (sum all DD UTXOs)
   ↓
6. Add to transaction history (category: "receive")
   ↓
7. Notify GUI (signal balance changed)
   ↓
8. GUI displays new balance
```

## IMPLEMENTATION PLAN - 7 Core Tasks

---

## TASK 1: Transaction Scanning Hook ⭐ CRITICAL FOUNDATION

### Priority: HIGHEST - Everything depends on this

### Problem
The wallet has NO entry point to scan transactions for DigiDollar outputs. When a DD transaction arrives, it's processed as a regular DGB transaction and DD outputs are ignored.

### Solution
Hook DigiDollar scanning into the wallet's transaction processing pipeline.

### Files to Modify
- **src/wallet/wallet.cpp** (main wallet class)
- **src/wallet/digidollarwallet.h** (DD wallet interface)
- **src/wallet/wallet.h** (wallet class definition)

### TDD Implementation

#### RED: Test Hook Exists
```cpp
// File: src/test/digidollar_receive_tests.cpp
BOOST_AUTO_TEST_CASE(test_transaction_scanning_hook)
{
    // Create wallet with DD support
    auto wallet = std::make_shared<CWallet>(chain, "", CreateMockWalletDatabase());
    wallet->LoadWallet();

    // Create DD wallet
    DigiDollarWallet* dd_wallet = new DigiDollarWallet(wallet.get());
    wallet->m_dd_wallet.reset(dd_wallet);

    // Create mock DD transaction
    CMutableTransaction tx = CreateMockDDTransferToAddress(
        wallet->GenerateDDAddress(), // Our address
        30000  // 300 DD
    );

    // Add transaction to wallet
    CTransactionRef ptx = MakeTransactionRef(tx);
    wallet->AddToWalletIfInvolvingMe(ptx, ...);

    // Verify: ScanForIncomingDD should have been called
    // (This will fail initially - proves hook missing)
    BOOST_CHECK(dd_wallet->GetLastScannedTx() == ptx->GetHash()); // ← FAILS
}
```

#### GREEN: Implement Hook
```cpp
// In src/wallet/wallet.cpp - CWallet::AddToWalletIfInvolvingMe()

bool CWallet::AddToWalletIfInvolvingMe(const CTransactionRef& ptx,
                                       const SyncTxState& state,
                                       const bool fUpdate,
                                       const bool rescanning_old_block)
{
    // ... existing code for standard DGB processing ...

    // NEW: Scan for DigiDollar outputs
    if (m_dd_wallet) {
        m_dd_wallet->ScanForIncomingDD(ptx);
        LogPrint(BCLog::DD, "DigiDollar: Scanned %s for incoming DD\n",
                 ptx->GetHash().ToString());
    }

    return true;
}
```

#### GREEN: Add Interface
```cpp
// In src/wallet/digidollarwallet.h

class DigiDollarWallet {
private:
    CWallet* m_wallet;
    std::map<COutPoint, CAmount> dd_utxos;

public:
    // NEW: Main scanning method
    void ScanForIncomingDD(const CTransactionRef& tx);

    // Helper for testing
    uint256 GetLastScannedTx() const { return m_last_scanned_txid; }

private:
    uint256 m_last_scanned_txid; // For testing
};
```

#### REFACTOR: Add Safety Checks
```cpp
void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    if (!tx) {
        LogPrint(BCLog::DD, "DigiDollar: Null transaction in scan\n");
        return;
    }

    m_last_scanned_txid = tx->GetHash();

    LogPrint(BCLog::DD, "DigiDollar: Scanning %s for incoming DD\n",
             tx->GetHash().ToString());

    // TODO: Implement actual scanning logic (Tasks 2-6)
}
```

### Acceptance Criteria
- [ ] CWallet::AddToWalletIfInvolvingMe() calls ScanForIncomingDD()
- [ ] Only called if m_dd_wallet exists
- [ ] Test passes (hook verified to execute)
- [ ] Wallet compiles
- [ ] No existing tests broken

---

## TASK 2: DD Output Detection Logic ⭐ CRITICAL

### Priority: HIGHEST - Core identification

### Problem
The wallet cannot distinguish DigiDollar outputs from regular DGB outputs. Even if we scan transactions, we don't know which outputs are DD.

### Solution
Implement logic to detect and extract DD amount from transaction outputs.

### Files to Modify
- **src/wallet/digidollarwallet.cpp**
- **src/wallet/digidollarwallet.h**

### TDD Implementation

#### RED: Test DD Detection
```cpp
BOOST_AUTO_TEST_CASE(test_detect_dd_output)
{
    DigiDollarWallet wallet;

    // Create DD output (300 DD to some address)
    CDigiDollarAddress recipient("DD1test...");
    CScript ddScript = CreateDigiDollarP2TR(recipient.GetPubKey(), 30000);
    CTxOut dd_output(0, ddScript);  // DD outputs have 0 DGB value

    // Create non-DD output (regular DGB)
    CPubKey regularKey;
    CScript regularScript = GetScriptForDestination(PKHash(regularKey));
    CTxOut regular_output(1000 * COIN, regularScript);

    // Test DD output detection
    CAmount dd_amount = 0;
    BOOST_CHECK(wallet.IsDigiDollarOutput(dd_output, dd_amount)); // ← FAILS initially
    BOOST_CHECK_EQUAL(dd_amount, 30000);

    // Test regular output rejection
    CAmount amount2 = 0;
    BOOST_CHECK(!wallet.IsDigiDollarOutput(regular_output, amount2));
}
```

#### GREEN: Implement Detection
```cpp
// In src/wallet/digidollarwallet.h
class DigiDollarWallet {
public:
    // Detect if output is DigiDollar and extract amount
    bool IsDigiDollarOutput(const CTxOut& txout, CAmount& dd_amount) const;
};

// In src/wallet/digidollarwallet.cpp
bool DigiDollarWallet::IsDigiDollarOutput(const CTxOut& txout, CAmount& dd_amount) const {
    // DD outputs are always P2TR (Taproot)
    if (!txout.scriptPubKey.IsPayToTaproot()) {
        return false;
    }

    // DD outputs typically have 0 DGB value (value in witness/script)
    // But allow small amounts for edge cases
    if (txout.nValue > DUST_THRESHOLD) {
        return false;  // Likely regular P2TR, not DD
    }

    // Extract DD amount from script
    // DD scripts contain OP_DIGIDOLLAR marker + amount
    if (!ExtractDDAmount(txout.scriptPubKey, dd_amount)) {
        return false;  // No DD marker found
    }

    // Valid DD output must have positive amount
    return dd_amount > 0;
}
```

#### REFACTOR: Add Robust Validation
```cpp
bool DigiDollarWallet::IsDigiDollarOutput(const CTxOut& txout, CAmount& dd_amount) const {
    dd_amount = 0;  // Initialize

    // Check 1: Must be P2TR
    if (!txout.scriptPubKey.IsPayToTaproot()) {
        LogPrint(BCLog::DD, "Not P2TR output\n");
        return false;
    }

    // Check 2: Extract DD amount using existing utility
    if (!ExtractDDAmount(txout.scriptPubKey, dd_amount)) {
        LogPrint(BCLog::DD, "No DD amount in script\n");
        return false;
    }

    // Check 3: Validate amount range
    if (dd_amount <= 0 || dd_amount > MAX_DD_AMOUNT) {
        LogPrint(BCLog::DD, "Invalid DD amount: %d\n", dd_amount);
        return false;
    }

    LogPrint(BCLog::DD, "Detected DD output: %d cents\n", dd_amount);
    return true;
}
```

### Acceptance Criteria
- [ ] Correctly identifies DD outputs (P2TR + OP_DIGIDOLLAR marker)
- [ ] Extracts DD amount in cents
- [ ] Rejects regular DGB outputs
- [ ] Rejects P2TR outputs without DD marker
- [ ] Test passes
- [ ] Wallet compiles

---

## TASK 3: Ownership Verification ⭐ CRITICAL

### Priority: HIGHEST - Security critical

### Problem
We can detect DD outputs, but we don't know if they belong to OUR wallet. Without ownership verification, we'd track ALL DD transactions on the blockchain.

### Solution
Verify that DD output's destination address is controlled by our wallet.

### Files to Modify
- **src/wallet/digidollarwallet.cpp**
- **src/wallet/digidollarwallet.h**

### TDD Implementation

#### RED: Test Ownership Detection
```cpp
BOOST_AUTO_TEST_CASE(test_dd_ownership_verification)
{
    // Create wallet
    auto wallet = std::make_shared<CWallet>(chain, "", CreateMockWalletDatabase());
    DigiDollarWallet dd_wallet(wallet.get());

    // Generate our DD address
    CPubKey ourKey = wallet->GenerateNewKey();
    CDigiDollarAddress ourAddress;
    ourAddress.Set(PKHash(ourKey), CChainParams::DIGIDOLLAR_ADDRESS);

    // Create DD output to our address
    CScript ourScript = CreateDigiDollarP2TR(XOnlyPubKey(ourKey), 30000);
    CTxOut our_output(0, ourScript);

    // Create DD output to someone else's address
    CPubKey otherKey;
    otherKey.MakeNewKey(true);
    CScript otherScript = CreateDigiDollarP2TR(XOnlyPubKey(otherKey), 50000);
    CTxOut other_output(0, otherScript);

    // Test: Should detect ours as ours
    BOOST_CHECK(dd_wallet.IsOurDDOutput(our_output));  // ← FAILS initially

    // Test: Should reject others
    BOOST_CHECK(!dd_wallet.IsOurDDOutput(other_output));
}
```

#### GREEN: Implement Ownership Check
```cpp
// In src/wallet/digidollarwallet.h
class DigiDollarWallet {
public:
    // Check if DD output belongs to our wallet
    bool IsOurDDOutput(const CTxOut& txout) const;
};

// In src/wallet/digidollarwallet.cpp
bool DigiDollarWallet::IsOurDDOutput(const CTxOut& txout) const {
    // Extract destination from DD script
    CTxDestination dest;
    if (!ExtractDestination(txout.scriptPubKey, dest)) {
        return false;
    }

    // Check if we can spend this output
    isminetype mine = m_wallet->IsMine(dest);

    // We need spendable, not just watch-only
    return mine == ISMINE_SPENDABLE;
}
```

#### REFACTOR: Enhanced Ownership Detection
```cpp
bool DigiDollarWallet::IsOurDDOutput(const CTxOut& txout) const {
    // First verify it's actually a DD output
    CAmount dd_amount = 0;
    if (!IsDigiDollarOutput(txout, dd_amount)) {
        LogPrint(BCLog::DD, "Not a DD output in ownership check\n");
        return false;
    }

    // Extract destination
    CTxDestination dest;
    if (!ExtractDestination(txout.scriptPubKey, dest)) {
        LogPrint(BCLog::DD, "Could not extract destination\n");
        return false;
    }

    // Check ownership level
    isminetype mine = m_wallet->IsMine(dest);

    // Log for debugging
    LogPrint(BCLog::DD, "Ownership check for DD output: %s (mine=%d)\n",
             EncodeDestination(dest), mine);

    // Accept only spendable outputs (we have the private key)
    return mine == ISMINE_SPENDABLE;
}
```

### Acceptance Criteria
- [ ] Correctly identifies outputs to our addresses
- [ ] Rejects outputs to other addresses
- [ ] Works with both DD addresses and standard P2TR
- [ ] Only accepts ISMINE_SPENDABLE (not watch-only)
- [ ] Test passes
- [ ] Wallet compiles

---

## TASK 4: DD UTXO Addition 🔧 STATE MANAGEMENT

### Priority: HIGH - Core state update

### Problem
After detecting and verifying a DD output belongs to us, we have no mechanism to add it to our spendable UTXO set.

### Solution
Add method to register received DD UTXO in dd_utxos map and persist to database.

### Files to Modify
- **src/wallet/digidollarwallet.cpp**
- **src/wallet/digidollarwallet.h**

### TDD Implementation

#### RED: Test UTXO Addition
```cpp
BOOST_AUTO_TEST_CASE(test_add_dd_utxo_on_receive)
{
    auto wallet = std::make_shared<CWallet>(chain, "", CreateMockWalletDatabase());
    DigiDollarWallet dd_wallet(wallet.get());

    // Create outpoint
    uint256 txid;
    txid.SetHex("abc123...");
    COutPoint outpoint(txid, 0);
    CAmount dd_amount = 30000;  // 300 DD

    // Initially empty
    BOOST_CHECK_EQUAL(dd_wallet.GetDDUTXOs().size(), 0);

    // Add UTXO
    dd_wallet.AddDDUTXO(outpoint, dd_amount);

    // Should be in map
    auto utxos = dd_wallet.GetDDUTXOs();
    BOOST_CHECK_EQUAL(utxos.size(), 1);  // ← FAILS initially
    BOOST_CHECK_EQUAL(utxos[0].dd_amount, 30000);
    BOOST_CHECK(utxos[0].outpoint == outpoint);
}
```

#### GREEN: Implement UTXO Addition
```cpp
// In src/wallet/digidollarwallet.h
class DigiDollarWallet {
public:
    // Add received DD UTXO to tracking
    void AddDDUTXO(const COutPoint& outpoint, CAmount dd_amount);

private:
    std::map<COutPoint, CAmount> dd_utxos;
};

// In src/wallet/digidollarwallet.cpp
void DigiDollarWallet::AddDDUTXO(const COutPoint& outpoint, CAmount dd_amount) {
    // Add to in-memory map
    dd_utxos[outpoint] = dd_amount;

    // Persist to database (using existing WriteDDUTXO from send implementation)
    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteDDUTXO(outpoint, dd_amount);

    LogPrintf("DigiDollar: Added DD UTXO %s:%d (%d cents)\n",
              outpoint.hash.ToString(), outpoint.n, dd_amount);
}
```

#### REFACTOR: Add Validation and Error Handling
```cpp
void DigiDollarWallet::AddDDUTXO(const COutPoint& outpoint, CAmount dd_amount) {
    // Validate inputs
    if (outpoint.IsNull()) {
        LogPrintf("DigiDollar: ERROR - Cannot add null outpoint\n");
        return;
    }

    if (dd_amount <= 0) {
        LogPrintf("DigiDollar: ERROR - Cannot add DD UTXO with amount %d\n", dd_amount);
        return;
    }

    // Check for duplicates
    if (dd_utxos.count(outpoint)) {
        LogPrintf("DigiDollar: WARNING - UTXO %s:%d already exists, updating amount\n",
                  outpoint.hash.ToString(), outpoint.n);
    }

    // Add to map
    dd_utxos[outpoint] = dd_amount;

    // Persist
    WalletBatch batch(m_wallet->GetDatabase());
    if (!batch.WriteDDUTXO(outpoint, dd_amount)) {
        LogPrintf("DigiDollar: ERROR - Failed to persist DD UTXO to database\n");
    }

    LogPrintf("DigiDollar: Added DD UTXO %s:%d (%d cents, $%.2f)\n",
              outpoint.hash.ToString(), outpoint.n, dd_amount, dd_amount / 100.0);
}
```

### Acceptance Criteria
- [ ] Adds UTXO to dd_utxos map
- [ ] Persists to database via WriteDDUTXO
- [ ] Handles duplicates gracefully
- [ ] Validates inputs
- [ ] Test passes
- [ ] GetDDUTXOs() returns added UTXO

---

## TASK 5: Balance Update System 🔧 STATE MANAGEMENT

### Priority: HIGH - User-visible impact

### Problem
Even if we add DD UTXOs, the wallet balance doesn't update. Users see 0 DD even after receiving.

### Solution
Implement automatic balance recalculation and persistence when DD received.

### Files to Modify
- **src/wallet/digidollarwallet.cpp**
- **src/wallet/digidollarwallet.h**

### TDD Implementation

#### RED: Test Balance Update
```cpp
BOOST_AUTO_TEST_CASE(test_balance_updates_on_receive)
{
    auto wallet = std::make_shared<CWallet>(chain, "", CreateMockWalletDatabase());
    DigiDollarWallet dd_wallet(wallet.get());

    // Initial balance
    BOOST_CHECK_EQUAL(dd_wallet.GetTotalDDBalance(), 0);

    // Receive 300 DD
    COutPoint outpoint1(GetRandHash(), 0);
    dd_wallet.AddDDUTXO(outpoint1, 30000);
    dd_wallet.UpdateBalance();

    BOOST_CHECK_EQUAL(dd_wallet.GetTotalDDBalance(), 30000);  // ← FAILS initially

    // Receive another 500 DD
    COutPoint outpoint2(GetRandHash(), 0);
    dd_wallet.AddDDUTXO(outpoint2, 50000);
    dd_wallet.UpdateBalance();

    BOOST_CHECK_EQUAL(dd_wallet.GetTotalDDBalance(), 80000);  // 300 + 500
}
```

#### GREEN: Implement Balance Update
```cpp
// In src/wallet/digidollarwallet.h
class DigiDollarWallet {
public:
    // Update DD balance after receive/send
    void UpdateBalance();

private:
    CAmount cached_balance = 0;
};

// In src/wallet/digidollarwallet.cpp
void DigiDollarWallet::UpdateBalance() {
    // Recalculate from all DD UTXOs
    CAmount new_balance = GetTotalDDBalance();

    // Update cache
    cached_balance = new_balance;

    // Persist to database
    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteDDBalance(GetDDAddress(), new_balance);

    LogPrintf("DigiDollar: Balance updated to %d cents ($%.2f)\n",
              new_balance, new_balance / 100.0);
}
```

#### REFACTOR: Add UI Notification
```cpp
void DigiDollarWallet::UpdateBalance() {
    // Calculate new balance
    CAmount new_balance = GetTotalDDBalance();

    // Only update if changed
    if (new_balance == cached_balance) {
        return;
    }

    CAmount old_balance = cached_balance;
    cached_balance = new_balance;

    // Persist
    WalletBatch batch(m_wallet->GetDatabase());
    if (!batch.WriteDDBalance(GetDDAddress(), new_balance)) {
        LogPrintf("DigiDollar: ERROR - Failed to persist balance\n");
    }

    // Notify UI (Task 7 will connect this)
    NotifyDDBalanceChanged(this, new_balance);

    LogPrintf("DigiDollar: Balance %s from %d to %d cents\n",
              new_balance > old_balance ? "increased" : "decreased",
              old_balance, new_balance);
}
```

### Acceptance Criteria
- [ ] Recalculates balance from all DD UTXOs
- [ ] Persists new balance to database
- [ ] Updates only when balance changes
- [ ] Prepares for UI notification
- [ ] Test passes
- [ ] GetTotalDDBalance() returns correct sum

---

## TASK 6: Transaction History 📊 RECORD KEEPING

### Priority: MEDIUM - User visibility

### Problem
Users can't see incoming DD transactions in their transaction history. No record of when/how much DD was received.

### Solution
Add incoming DD transactions to wallet history with proper metadata.

### Files to Modify
- **src/wallet/digidollarwallet.cpp**
- **src/wallet/digidollarwallet.h**

### TDD Implementation

#### RED: Test Transaction History
```cpp
BOOST_AUTO_TEST_CASE(test_incoming_dd_transaction_history)
{
    auto wallet = std::make_shared<CWallet>(chain, "", CreateMockWalletDatabase());
    DigiDollarWallet dd_wallet(wallet.get());

    // Create mock incoming transaction
    CMutableTransaction tx = CreateMockDDTransferToAddress(
        wallet->GenerateDDAddress(), 30000
    );
    CTransactionRef ptx = MakeTransactionRef(tx);

    // Add to history
    dd_wallet.AddIncomingTransaction(ptx, 30000);

    // Check history
    auto history = dd_wallet.GetDDTransactionHistory();
    BOOST_CHECK_EQUAL(history.size(), 1);  // ← FAILS initially
    BOOST_CHECK_EQUAL(history[0].category, "receive");
    BOOST_CHECK_EQUAL(history[0].amount, 30000);
    BOOST_CHECK(history[0].incoming == true);
}
```

#### GREEN: Implement History Addition
```cpp
// In src/wallet/digidollarwallet.h
class DigiDollarWallet {
public:
    // Add incoming DD transaction to history
    void AddIncomingTransaction(const CTransactionRef& tx, CAmount dd_amount);
};

// In src/wallet/digidollarwallet.cpp
void DigiDollarWallet::AddIncomingTransaction(
    const CTransactionRef& tx,
    CAmount dd_amount)
{
    DDTransaction ddtx;
    ddtx.txid = tx->GetHash().ToString();
    ddtx.amount = dd_amount;
    ddtx.timestamp = GetTime();
    ddtx.confirmations = 0;  // Will update when confirmed
    ddtx.incoming = true;
    ddtx.category = "receive";

    // Persist to database
    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteDDTransaction(ddtx);

    LogPrintf("DigiDollar: Received %d DD in %s\n",
              dd_amount, tx->GetHash().ToString());
}
```

#### REFACTOR: Enhanced Metadata
```cpp
void DigiDollarWallet::AddIncomingTransaction(
    const CTransactionRef& tx,
    CAmount dd_amount)
{
    // Build transaction record
    DDTransaction ddtx;
    ddtx.txid = tx->GetHash().ToString();
    ddtx.amount = dd_amount;
    ddtx.timestamp = GetTime();

    // Check if already confirmed
    uint256 block_hash;
    int confirmations = 0;
    if (m_wallet->chain().findBlock(tx->GetHash(), FoundBlock().hash(block_hash))) {
        confirmations = m_wallet->GetLastBlockHeight() -
                       m_wallet->chain().getBlockHeight(block_hash).value_or(0) + 1;
    }
    ddtx.confirmations = confirmations;

    ddtx.incoming = true;
    ddtx.category = "receive";

    // Extract sender info if available
    // (Could parse inputs to find sender address)

    // Persist
    WalletBatch batch(m_wallet->GetDatabase());
    if (!batch.WriteDDTransaction(ddtx)) {
        LogPrintf("DigiDollar: ERROR - Failed to persist transaction\n");
        return;
    }

    LogPrintf("DigiDollar: Recorded incoming TX %s: %d DD (%d confirmations)\n",
              tx->GetHash().ToString(), dd_amount, confirmations);
}
```

### Acceptance Criteria
- [ ] Adds transaction to DD history
- [ ] Sets category = "receive"
- [ ] Sets incoming = true
- [ ] Records timestamp and confirmations
- [ ] Persists via WriteDDTransaction
- [ ] Test passes
- [ ] GetDDTransactionHistory() returns transaction

---

## TASK 7: GUI Notification & Display 🎨 USER INTERFACE

### Priority: MEDIUM - User experience

### Problem
Even if we detect and track received DD, the GUI doesn't update. Users don't see the new balance or transactions.

### Solution
Connect wallet balance changes to GUI updates via Qt signals/slots.

### Files to Modify
- **src/qt/digidollaroverviewwidget.cpp**
- **src/qt/walletmodel.cpp**
- **src/qt/walletmodel.h**
- **src/wallet/digidollarwallet.cpp**

### TDD Implementation

#### RED: Test GUI Update (Integration Test)
```cpp
// Note: This is more of an integration test
BOOST_AUTO_TEST_CASE(test_gui_updates_on_receive)
{
    // This test verifies the signal/slot chain works
    // Actual GUI testing done manually in Qt

    auto wallet = std::make_shared<CWallet>(chain, "", CreateMockWalletDatabase());
    DigiDollarWallet dd_wallet(wallet.get());

    // Track if notification fired
    bool notification_received = false;
    CAmount notified_balance = 0;

    // Connect to notification (simulating Qt slot)
    dd_wallet.SetBalanceNotificationCallback([&](CAmount balance) {
        notification_received = true;
        notified_balance = balance;
    });

    // Receive DD
    COutPoint outpoint(GetRandHash(), 0);
    dd_wallet.AddDDUTXO(outpoint, 30000);
    dd_wallet.UpdateBalance();

    // Verify notification
    BOOST_CHECK(notification_received);  // ← FAILS initially
    BOOST_CHECK_EQUAL(notified_balance, 30000);
}
```

#### GREEN: Implement Notification System
```cpp
// In src/wallet/digidollarwallet.h
class DigiDollarWallet {
public:
    // Notification callback (for GUI)
    using BalanceNotifyFn = std::function<void(CAmount)>;
    void SetBalanceNotificationCallback(BalanceNotifyFn fn) {
        m_balance_notify = fn;
    }

private:
    BalanceNotifyFn m_balance_notify;
};

// In src/wallet/digidollarwallet.cpp - UpdateBalance()
void DigiDollarWallet::UpdateBalance() {
    CAmount new_balance = GetTotalDDBalance();

    if (new_balance == cached_balance) {
        return;
    }

    cached_balance = new_balance;

    // Persist
    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteDDBalance(GetDDAddress(), new_balance);

    // Notify GUI
    if (m_balance_notify) {
        m_balance_notify(new_balance);
    }

    LogPrintf("DigiDollar: Balance updated to %d, GUI notified\n", new_balance);
}
```

#### GREEN: Connect to Qt GUI
```cpp
// In src/qt/walletmodel.cpp

void WalletModel::pollBalanceChanged() {
    // ... existing DGB balance polling ...

    // NEW: Poll DD balance
    if (wallet().m_dd_wallet) {
        CAmount dd_balance = wallet().m_dd_wallet->GetTotalDDBalance();
        if (dd_balance != cachedDDBalance) {
            cachedDDBalance = dd_balance;
            Q_EMIT digidollarBalanceChanged(dd_balance);
        }
    }
}

// In src/qt/digidollaroverviewwidget.cpp

void DigiDollarOverviewWidget::setModel(WalletModel *model) {
    this->model = model;

    if (model && model->wallet().m_dd_wallet) {
        // Connect balance change signal
        connect(model, &WalletModel::digidollarBalanceChanged,
                this, &DigiDollarOverviewWidget::updateBalance);

        // Initial update
        updateBalance(model->wallet().m_dd_wallet->GetTotalDDBalance());
    }
}

void DigiDollarOverviewWidget::updateBalance(const CAmount& balance) {
    ui->labelDDBalance->setText(
        BitcoinUnits::formatWithUnit(BitcoinUnits::DD, balance)
    );

    LogPrint(BCLog::QT, "DigiDollar GUI: Updated balance display to %d\n", balance);
}
```

#### REFACTOR: Add Transaction List Updates
```cpp
// In src/qt/digidollaroverviewwidget.cpp

void DigiDollarOverviewWidget::setModel(WalletModel *model) {
    this->model = model;

    if (model && model->wallet().m_dd_wallet) {
        // Balance updates
        connect(model, &WalletModel::digidollarBalanceChanged,
                this, &DigiDollarOverviewWidget::updateBalance);

        // Transaction list updates
        connect(model, &WalletModel::digidollarTransactionAdded,
                this, &DigiDollarOverviewWidget::updateTransactionList);

        // Initial updates
        updateBalance(model->wallet().m_dd_wallet->GetTotalDDBalance());
        updateTransactionList();
    }
}

void DigiDollarOverviewWidget::updateTransactionList() {
    if (!model || !model->wallet().m_dd_wallet) return;

    auto history = model->wallet().m_dd_wallet->GetDDTransactionHistory();

    // Update table widget
    ui->tableTransactions->setRowCount(history.size());

    for (size_t i = 0; i < history.size(); i++) {
        const auto& tx = history[i];

        // Add to table
        ui->tableTransactions->setItem(i, 0,
            new QTableWidgetItem(QString::fromStdString(tx.category)));
        ui->tableTransactions->setItem(i, 1,
            new QTableWidgetItem(BitcoinUnits::formatWithUnit(
                BitcoinUnits::DD, tx.amount)));
        // ... more columns ...
    }
}
```

### Acceptance Criteria
- [ ] Balance changes trigger GUI updates
- [ ] GUI displays correct DD balance
- [ ] Transaction list shows incoming DD
- [ ] Updates happen in real-time (mempool + confirmed)
- [ ] Test verifies notification chain
- [ ] Qt GUI compiles and runs

---

## COMPLETE RECEIVE FLOW IMPLEMENTATION

### Main ScanForIncomingDD Method (Orchestrates All Tasks)

```cpp
// This is the complete implementation using all tasks

void DigiDollarWallet::ScanForIncomingDD(const CTransactionRef& tx) {
    if (!tx) {
        LogPrint(BCLog::DD, "DigiDollar: Null transaction in scan\n");
        return;
    }

    LogPrint(BCLog::DD, "DigiDollar: Scanning %s for incoming DD\n",
             tx->GetHash().ToString());

    bool found_incoming = false;
    CAmount total_received = 0;

    // Check each output
    for (size_t i = 0; i < tx->vout.size(); i++) {
        const CTxOut& txout = tx->vout[i];

        // TASK 2: Detect DD output
        CAmount dd_amount = 0;
        if (!IsDigiDollarOutput(txout, dd_amount)) {
            continue;  // Not a DD output
        }

        // TASK 3: Verify ownership
        if (!IsOurDDOutput(txout)) {
            LogPrint(BCLog::DD, "DD output not ours: %s:%d\n",
                     tx->GetHash().ToString(), i);
            continue;  // Not ours
        }

        // Found DD output to our address!
        found_incoming = true;
        total_received += dd_amount;

        // TASK 4: Add to DD UTXOs
        COutPoint new_utxo(tx->GetHash(), i);
        AddDDUTXO(new_utxo, dd_amount);

        LogPrintf("DigiDollar: Received %d DD in %s:%d\n",
                  dd_amount, tx->GetHash().ToString(), i);
    }

    if (found_incoming) {
        // TASK 5: Update balance
        UpdateBalance();

        // TASK 6: Add to transaction history
        AddIncomingTransaction(tx, total_received);

        // TASK 7: GUI notified automatically via UpdateBalance()

        LogPrintf("DigiDollar: Successfully received %d DD total\n",
                  total_received);
    } else {
        LogPrint(BCLog::DD, "No incoming DD found in %s\n",
                 tx->GetHash().ToString());
    }
}
```

## Implementation Order & Dependencies

### Phase 1: Foundation (SEQUENTIAL)
These MUST be done in order:

1. **Task 1** - Transaction Scanning Hook
   - No dependencies
   - Everything else depends on this

2. **Task 2** - DD Output Detection
   - Depends on Task 1
   - Task 3 depends on this

3. **Task 3** - Ownership Verification
   - Depends on Task 2
   - Tasks 4-6 depend on this

### Phase 2: State Management (PARALLEL)
These can run in parallel after Phase 1:

4. **Task 4** - DD UTXO Addition
5. **Task 5** - Balance Update
6. **Task 6** - Transaction History

### Phase 3: User Interface (AFTER PHASE 2)

7. **Task 7** - GUI Notification

## Success Validation

### Complete End-to-End Test

**Test Scenario**: Alice → Bob send/receive cycle

```bash
# Terminal 1: Alice's wallet
./src/qt/digibyte-qt -regtest -datadir=/tmp/alice

alice> generate 650
alice> mintdigidollar 1000
alice> getdigidollarbalance
Expected: 1000

# Terminal 2: Bob's wallet
./src/qt/digibyte-qt -regtest -datadir=/tmp/bob

bob> getdigidollaraddress
Returns: DD1abc123...

# Alice sends to Bob
alice> senddigidollar DD1abc123... 300

# CRITICAL TEST: Bob should receive
bob> getdigidollarbalance
Expected: 300  ← THIS IS THE GOAL!

bob> listdigidollartxs
Expected: Shows receive transaction

# Confirm
alice> generate 1

bob> getdigidollarbalance
Expected: 300 (confirmed)

# GUI Check
# Bob's Qt wallet should show:
# - Balance: 300 DD
# - Recent transaction: Received 300 DD
# - Transaction list: 1 entry (receive)
```

### All Tests Must Pass

```bash
# Unit tests
./src/test/test_digibyte --run_test=digidollar_receive_*
# Expected: ALL PASS

# Send tests (regression check)
./src/test/test_digibyte --run_test=digidollar_transfer_*
# Expected: ALL PASS (no regressions!)

# Functional test
./test/functional/digidollar_transfer.py
# Expected: PASS
```

## Files Summary

### Files to Create
- **src/test/digidollar_receive_tests.cpp** - Unit tests for receiving
- **test/functional/digidollar_transfer.py** - End-to-end send/receive test

### Files to Modify
- **src/wallet/wallet.cpp** - Add ScanForIncomingDD hook (Task 1)
- **src/wallet/digidollarwallet.h** - Add receive methods (Tasks 1-7)
- **src/wallet/digidollarwallet.cpp** - Implement receive logic (Tasks 1-7)
- **src/qt/walletmodel.h** - Add DD balance signal
- **src/qt/walletmodel.cpp** - Poll DD balance (Task 7)
- **src/qt/digidollaroverviewwidget.cpp** - Update GUI (Task 7)

### Estimated Lines of Code
- Task 1: ~50 lines
- Task 2: ~80 lines
- Task 3: ~60 lines
- Task 4: ~40 lines
- Task 5: ~50 lines
- Task 6: ~70 lines
- Task 7: ~100 lines
- Tests: ~400 lines

**Total**: ~850 lines of new/modified code

---

**Document Version**: 1.0 - DigiDollar Receive Implementation
**Last Updated**: 2025-10-04
**Status**: Ready for Implementation - Complete DigiDollar Send/Receive!
