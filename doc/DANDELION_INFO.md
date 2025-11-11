# DigiByte v8.22.2 Dandelion++ Implementation - Complete Reference Guide

## Overview

DigiByte v8.22.2 implements Dandelion++, an enhanced privacy protocol for transaction propagation based on BIP-156. This document provides a comprehensive analysis of every aspect of the Dandelion++ implementation in DigiByte's codebase, serving as the definitive reference for porting to v8.26.

## Table of Contents

1. [Protocol Summary](#protocol-summary)
2. [File Structure and Dependencies](#file-structure-and-dependencies)
3. [Core Implementation Details](#core-implementation-details)
4. [Data Structures and Classes](#data-structures-and-classes)
5. [Key Functions and Methods](#key-functions-and-methods)
6. [Message Flow and Processing](#message-flow-and-processing)
7. [Stempool Implementation](#stempool-implementation)
8. [Embargo System](#embargo-system)
9. [Route Management](#route-management)
10. [Logging and Debugging](#logging-and-debugging)
11. [Configuration and Constants](#configuration-and-constants)
12. [Testing](#testing)
13. [Integration Points](#integration-points)
14. [Critical Implementation Notes](#critical-implementation-notes)

## Protocol Summary

### What is Dandelion++?

Dandelion++ is a privacy-enhancing transaction routing mechanism that protects users from network-level deanonymization attacks. It operates in two distinct phases:

1. **Stem Phase**: Transactions are forwarded along a linear path through randomly selected peers
2. **Fluff Phase**: Transactions are broadcast using traditional flooding propagation

The protocol provides near-optimal anonymity guarantees by breaking the symmetry of transaction propagation patterns.

### Key Differences from BIP-156

DigiByte implements Dandelion++ (the enhanced version) with these key differences:
- **Two destinations** instead of one for improved reliability
- **Embargo system** for additional timing obfuscation
- **Discovery mechanism** using special inventory messages
- **Stempool** as a separate transaction pool for stem phase

## File Structure and Dependencies

### Core Implementation Files

#### 1. `/src/dandelion.cpp` (327 lines)
The main Dandelion++ implementation file containing:
- Connection management functions
- Route selection and shuffling algorithms
- Embargo management
- Thread management for periodic shuffling
- Debug string generation

#### 2. `/src/dandelion.h` (IMPLICIT - methods declared in net.h)
All Dandelion methods are declared within the CConnman class in net.h

#### 3. `/src/protocol.h` & `/src/protocol.cpp`
Network protocol definitions:
```cpp
// Message type for stem phase transactions
extern const char *DANDELIONTX;

// Inventory types
MSG_DANDELION_TX = 5
MSG_DANDELION_WITNESS_TX = MSG_DANDELION_TX | MSG_WITNESS_FLAG

// In protocol.cpp
const char *DANDELIONTX = "dandeliontx";
```

#### 4. `/src/net.h` & `/src/net.cpp`
Network layer integration with Dandelion data structures and methods:
- Peer vectors and routing maps
- Configuration constants
- PoissonNextSend function
- Connection handling

#### 5. `/src/net_processing.h` & `/src/net_processing.cpp`
Message processing and transaction relay:
- ProcessMessage handling for DANDELIONTX
- RelayDandelionTransaction implementation
- CheckDandelionEmbargoes periodic check
- Fluff probability constant

#### 6. `/src/node/transaction.cpp`
Transaction broadcasting interface:
- Wallet transaction submission to stempool
- Embargo creation for local transactions
- Dandelion routing for wallet transactions

#### 7. `/src/txmempool.h` & `/src/txmempool.cpp`
Memory pool with stempool support:
- m_is_stempool flag
- isStempool() method
- Constructor modification for stempool creation

#### 8. `/src/validation.h` & `/src/validation.cpp`
Transaction validation with stempool:
- Stempool parameter in CChainState
- AcceptToMemoryPool handling for stempool
- Reorg handling for both pools

### Supporting Files

#### Wallet Integration
- `/src/wallet/wallet.h` - DEFAULT_DISABLE_DANDELION constant
- `/src/wallet/init.cpp` - Command-line option "-disabledandelion"
- `/src/dummywallet.cpp` - Stub implementation

#### Initialization & Logging
- `/src/init.cpp` - Stempool creation and thread startup
- `/src/logging.h` & `/src/logging.cpp` - BCLog::DANDELION category

#### Build System
- `/src/Makefile.am` - Includes dandelion.cpp in build

## Core Implementation Details

### Key Constants and Configuration

```cpp
// Network Constants (net.h)
static const bool DEFAULT_DANDELION = true;                    
static const int DANDELION_MAX_DESTINATIONS = 2;               
static constexpr auto DANDELION_SHUFFLE_INTERVAL = 10min;      
static constexpr auto DANDELION_EMBARGO_MINIMUM = 10s;         
static constexpr auto DANDELION_EMBARGO_AVG_ADD = 20s;         
static const uint256 DANDELION_DISCOVERYHASH = uint256S("0xfff...fff");

// Processing Constants (net_processing.h)
static const unsigned int DANDELION_FLUFF = 10;  // 10% fluff probability

// Wallet Constants (wallet.h)
static const bool DEFAULT_DISABLE_DANDELION = false;
```

## Data Structures and Classes

### CConnman Dandelion Members (net.h)

```cpp
// Peer Management Vectors
std::vector<CNode*> vDandelionInbound;      // Can send us stem txs
std::vector<CNode*> vDandelionOutbound;     // We can send stem txs to
std::vector<CNode*> vDandelionDestination;  // Selected routing destinations

// Routing Infrastructure  
std::map<CNode*, CNode*> mDandelionRoutes;  // Inbound->outbound mapping
CNode* localDandelionDestination = nullptr;  // Our own stem destination

// Embargo Management
std::map<uint256, std::chrono::microseconds> mDandelionEmbargo;

// Thread Management
std::thread threadDandelionShuffle;
```

### CTxMemPool Stempool Support (txmempool.h)

```cpp
class CTxMemPool {
private:
    const bool m_is_stempool;
    
public:
    explicit CTxMemPool(CBlockPolicyEstimator* estimator = nullptr, 
                       int check_ratio = 0, 
                       bool isStempool = false);
    bool isStempool() const { return m_is_stempool; }
};
```

### NodeContext Stempool (node/context.h)

```cpp
struct NodeContext {
    std::unique_ptr<CTxMemPool> mempool;
    std::unique_ptr<CTxMemPool> stempool;  // Separate pool for stem txs
};
```

## Key Functions and Methods

### Connection Management (dandelion.cpp)

#### isDandelionInbound
```cpp
bool CConnman::isDandelionInbound(const CNode* const pnode) const
```
Checks if a peer is in the Dandelion inbound vector.

#### setLocalDandelionDestination
```cpp
bool CConnman::setLocalDandelionDestination()
```
Sets the local node's Dandelion destination for outbound stem transactions.

#### getDandelionDestination
```cpp
CNode* CConnman::getDandelionDestination(CNode* pfrom)
```
Gets or creates a Dandelion route for an inbound peer.

#### localDandelionDestinationPushInventory
```cpp
bool CConnman::localDandelionDestinationPushInventory(const CInv& inv)
```
Pushes inventory to the local Dandelion destination.

#### SelectFromDandelionDestinations
```cpp
CNode* CConnman::SelectFromDandelionDestinations() const
```
Selects a destination with the fewest routes (load balancing).

#### CloseDandelionConnections
```cpp
void CConnman::CloseDandelionConnections(const CNode* const pnode)
```
Comprehensive cleanup when a Dandelion peer disconnects:
- Removes from all vectors
- Updates routes
- Selects replacement if needed

### Embargo Management (dandelion.cpp)

#### insertDandelionEmbargo
```cpp
bool CConnman::insertDandelionEmbargo(const uint256& hash, std::chrono::microseconds& embargo)
```
Creates an embargo entry for a transaction.

#### isTxDandelionEmbargoed
```cpp
bool CConnman::isTxDandelionEmbargoed(const uint256& hash) const
```
Checks if a transaction is currently embargoed.

#### removeDandelionEmbargo
```cpp
bool CConnman::removeDandelionEmbargo(const uint256& hash)
```
Removes an embargo entry.

### Route Shuffling (dandelion.cpp)

#### DandelionShuffle
```cpp
void CConnman::DandelionShuffle()
```
Complete route reshuffling algorithm:
1. Clears all existing routes
2. Clears destinations
3. Randomly selects new destinations (max 2)
4. Regenerates all inbound routes

#### ThreadDandelionShuffle
```cpp
void CConnman::ThreadDandelionShuffle()
```
Thread function that:
1. Waits for IBD completion
2. Shuffles routes every ~10 minutes (randomized)
3. Uses PoissonNextSend for timing

### Transaction Processing (net_processing.cpp)

#### RelayDandelionTransaction
```cpp
void PeerManagerImpl::RelayDandelionTransaction(const CTransaction& tx, CNode* pfrom)
```
Core relay logic:
```cpp
FastRandomContext rng;
bool willFluff = rng.randrange(100) < DANDELION_FLUFF;  // 10% chance

if (willFluff) {
    // Move from stempool to mempool
    CTransactionRef ptx = m_stempool.get(tx.GetHash());
    AcceptToMemoryPool(chainstate, m_mempool, ptx, false);
    RelayTransaction(tx.GetHash(), tx.GetWitnessHash());
} else {
    // Continue stem phase
    CNode* destination = m_connman.getDandelionDestination(pfrom);
    if (destination) {
        CInv inv(MSG_DANDELION_TX, tx.GetHash());
        destination->PushOtherInventory(inv);
    }
}
```

#### CheckDandelionEmbargoes
```cpp
void PeerManagerImpl::CheckDandelionEmbargoes()
```
Periodic check that:
1. Removes expired embargoes
2. Moves embargoed transactions to mempool if found there
3. Handles cleanup for missing transactions

### Utility Functions

#### PoissonNextSend (net.cpp)
```cpp
std::chrono::microseconds PoissonNextSend(std::chrono::microseconds now, 
                                         std::chrono::seconds average_interval)
```
Generates exponentially distributed random delays for:
- Embargo timeouts
- Route shuffling intervals
- Enhanced privacy through timing obfuscation

#### GetDandelionRoutingDataDebugString (dandelion.cpp)
```cpp
std::string CConnman::GetDandelionRoutingDataDebugString() const
```
Generates detailed debug output showing:
- All Dandelion vectors
- Current routes
- Local destination

## Message Flow and Processing

### 1. Local Transaction Broadcast (node/transaction.cpp)

```cpp
if (gArgs.GetBoolArg("-dandelion", DEFAULT_DANDELION)) {
    // Submit to stempool
    AcceptToMemoryPool(chainstate, *node.stempool, tx, false, false);
    
    // Create embargo
    auto current_time = GetTime<std::chrono::milliseconds>();
    std::chrono::microseconds nEmbargo = DANDELION_EMBARGO_MINIMUM + 
        PoissonNextSend(current_time, DANDELION_EMBARGO_AVG_ADD);
    node.connman->insertDandelionEmbargo(txid, nEmbargo);
    
    // Send via Dandelion
    CInv embargoTx(MSG_DANDELION_TX, txid);
    node.connman->localDandelionDestinationPushInventory(embargoTx);
}
```

### 2. Receiving DANDELIONTX Messages (net_processing.cpp)

```cpp
if (msg_type == NetMsgType::DANDELIONTX) {
    CTransactionRef ptx;
    vRecv >> ptx;
    const CTransaction& tx = *ptx;
    CInv inv(MSG_DANDELION_TX, tx.GetHash());
    
    if (m_connman.isDandelionInbound(&pfrom)) {
        if (!m_stempool.exists(inv.hash)) {
            MempoolAcceptResult result = AcceptToMemoryPool(
                m_chainman.ActiveChainstate(), m_stempool, ptx, false);
                
            if (result.m_result_type == MempoolAcceptResult::ResultType::VALID) {
                // Create embargo
                auto current_time = GetTime<std::chrono::milliseconds>();
                std::chrono::microseconds nEmbargo = DANDELION_EMBARGO_MINIMUM + 
                    PoissonNextSend(current_time, DANDELION_EMBARGO_AVG_ADD);
                m_connman.insertDandelionEmbargo(tx.GetHash(), nEmbargo);
                
                // Relay onward
                RelayDandelionTransaction(tx, &pfrom);
            }
        }
    }
}
```

### 3. Connection Establishment

#### Inbound Connection (net.cpp)
```cpp
// When accepting inbound connection
vDandelionInbound.push_back(pnode);
CNode* pto = SelectFromDandelionDestinations();
if (pto) {
    mDandelionRoutes.insert(std::make_pair(pnode, pto));
}
```

#### Outbound Connection (net.cpp)
```cpp
// When making outbound connection
vDandelionOutbound.push_back(pnode);
if (vDandelionDestination.size() < DANDELION_MAX_DESTINATIONS) {
    vDandelionDestination.push_back(pnode);
}
// Send discovery message
CInv dummyInv(MSG_DANDELION_TX, DANDELION_DISCOVERYHASH);
pnode->PushInventory(dummyInv);
```

### 4. GETDATA Handling with Embargo

```cpp
// In ProcessGetData (net_processing.cpp)
if (inv.IsDandelionMsg() && pfrom.m_tx_relay->setDandelionInventoryKnown.count(inv.hash)) {
    // Check embargo first
    if (m_connman.isTxDandelionEmbargoed(inv.hash)) {
        vNotFound.push_back(inv);
        continue;
    }
    
    // If not embargoed, send if we have it
    if (txinfo.tx) {
        m_connman.PushMessage(&pfrom, msgMaker.Make(nSendFlags, 
            NetMsgType::DANDELIONTX, *txinfo.tx));
    } else {
        vNotFound.push_back(inv);
    }
}
```

## Stempool Implementation

### Initialization (init.cpp)

```cpp
// Create separate stempool instance
node.stempool = std::make_unique<CTxMemPool>(nullptr, 0, true);

// Initialize chainstate with both pools
chainman.InitializeChainstate(Assert(node.mempool.get()), 
                             Assert(node.stempool.get()));
```

### CChainState Integration (validation.cpp)

```cpp
CChainState::CChainState(CTxMemPool* mempool, CTxMemPool* stempool, 
                         BlockManager& blockman, 
                         std::optional<uint256> from_snapshot_blockhash)
    : m_mempool(mempool),
      m_stempool(stempool),
      m_params(::Params()),
      m_blockman(blockman),
      m_from_snapshot_blockhash(from_snapshot_blockhash) {}
```

### Reorg Handling (validation.cpp)

Both pools are updated during reorgs:
```cpp
// Remove from both pools
m_mempool->removeRecursive(**it, MemPoolRemovalReason::REORG);
m_stempool->removeRecursive(**it, MemPoolRemovalReason::REORG);

// Update both pools
m_mempool->UpdateTransactionsFromBlock(vHashUpdate);
m_stempool->UpdateTransactionsFromBlock(vHashUpdate);

// Remove immature from both
m_mempool->removeForReorg(*this, STANDARD_LOCKTIME_VERIFY_FLAGS);
m_stempool->removeForReorg(*this, STANDARD_LOCKTIME_VERIFY_FLAGS);

// Limit both pool sizes
LimitMempoolSize(*m_mempool, ...);
LimitMempoolSize(*m_stempool, ...);
```

### Important Stempool Behavior

1. **No GetMainSignals notification** for stempool additions:
```cpp
if (!this->m_pool.isStempool()) {
    GetMainSignals().TransactionAddedToMempool(ptx, m_pool.GetAndIncrementSequence());
}
```

2. **Separate validation** from regular mempool
3. **Same size limits** as regular mempool
4. **Moves to mempool** during fluff phase

## Embargo System

### Purpose
Embargoes prevent timing analysis attacks by:
1. Delaying responses to GETDATA requests
2. Using randomized delays (10-30 seconds)
3. Responding with NOTFOUND during embargo period

### Embargo Lifecycle

1. **Creation** (when receiving/creating stem tx):
```cpp
auto current_time = GetTime<std::chrono::milliseconds>();
std::chrono::microseconds nEmbargo = DANDELION_EMBARGO_MINIMUM + 
    PoissonNextSend(current_time, DANDELION_EMBARGO_AVG_ADD);
m_connman.insertDandelionEmbargo(tx.GetHash(), nEmbargo);
```

2. **Checking** (in isTxDandelionEmbargoed):
```cpp
auto now = GetTime<std::chrono::microseconds>();
if (now < it->second) {
    return true;  // Still embargoed
}
return false;  // Embargo expired
```

3. **Periodic Cleanup** (CheckDandelionEmbargoes):
- Called periodically in message processing
- Removes expired embargoes
- Handles transactions that moved to mempool

## Route Management

### Route Selection Algorithm (SelectFromDandelionDestinations)

1. Count connections per destination
2. Find destinations with minimum connections
3. Randomly select from candidates
4. Ensures load balancing across destinations

### Route Shuffling Process

1. **Triggered every ~10 minutes** (randomized via PoissonNextSend)
2. **Complete reset**:
   - All routes cleared
   - Destinations re-selected
   - New routes generated
3. **Logging** of before/after state

### Disconnection Handling (CloseDandelionConnections)

Comprehensive cleanup when peer disconnects:
1. Remove from vDandelionInbound
2. Remove from vDandelionOutbound  
3. Remove from vDandelionDestination
4. If was destination, select replacement
5. Update all routes using this peer
6. Replace localDandelionDestination if needed

## Logging and Debugging

### Logging Category
- **Category**: BCLog::DANDELION
- **Enable**: `-debug=dandelion`

### Key Log Messages

1. **Route Changes**:
```cpp
LogPrint(BCLog::DANDELION, "Set local Dandelion destination:\n%s", 
         GetDandelionRoutingDataDebugString());
```

2. **Transaction Flow**:
```cpp
LogPrint(BCLog::DANDELION, "Dandelion fluff: %s\n", tx.GetHash().ToString());
LogPrint(BCLog::DANDELION, "dandeliontx %s embargoed for %d seconds\n", 
         txid.ToString(), embargo_timeout);
```

3. **Shuffle Events**:
```cpp
LogPrint(BCLog::DANDELION, "Before Dandelion shuffle:\n%s", 
         GetDandelionRoutingDataDebugString());
LogPrint(BCLog::DANDELION, "After Dandelion shuffle:\n%s", 
         GetDandelionRoutingDataDebugString());
```

### Debug String Format
```
vDandelionInbound: 1 2 3
vDandelionOutbound: 4 5 6
vDandelionDestination: 4 5
mDandelionRoutes: (1,4) (2,5) (3,4)
localDandelionDestination: 4
```

## Configuration and Constants

### Command Line Options

1. **`-dandelion=<0|1>`** (default: 1)
   - Enables/disables Dandelion++ protocol
   - Checked in node/transaction.cpp for wallet transactions

2. **`-disabledandelion`** (wallet option)
   - Disables Dandelion support for wallet
   - Added in wallet/init.cpp

3. **`-debug=dandelion`**
   - Enables detailed Dandelion logging

### Network Protocol

1. **Message Types**:
   - `dandeliontx` - Stem phase transaction message

2. **Inventory Types**:
   - `MSG_DANDELION_TX = 5`
   - `MSG_DANDELION_WITNESS_TX = MSG_DANDELION_TX | MSG_WITNESS_FLAG`

3. **Discovery Hash**:
   - `0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff`
   - Used to detect Dandelion capability

## Testing

### Functional Test: p2p_dandelion.py

Tests three key properties:

1. **Resistance to Active Probing**:
   - Transaction sent via stem
   - Immediate GETDATA returns NOTFOUND
   - Validates embargo system

2. **Loop Behavior**:
   - Tests behavior after ~5 seconds
   - Should still return NOTFOUND

3. **Resistance to Black Holes**:
   - After ~45 seconds
   - Transaction should be available
   - Tests embargo expiration

### Test Setup
```python
# Three node ring: 0 --> 1 --> 2 --> 0
self.extra_args = [["-dandelion=1"] for i in range(3)]
```

### Unit Test Coverage

Found in:
- `/src/test/denialofservice_tests.cpp` - DoS protection
- `/src/test/validation_*_tests.cpp` - Validation with stempool
- `/src/wallet/test/wallet_tests.cpp` - Wallet integration

## Integration Points

### 1. PeerManager Creation (init.cpp)
```cpp
node.peerman = PeerManager::make(chainparams, *node.connman, *node.addrman, 
                                node.banman.get(), *node.scheduler, chainman, 
                                *node.mempool, *node.stempool, ignores_incoming_txs);
```

### 2. Transaction Submission (node/transaction.cpp)
- Checks `-dandelion` flag
- Routes to stempool vs mempool
- Creates embargo for stem transactions

### 3. Network Message Processing
- Special handling for DANDELIONTX messages
- Embargo checks in GETDATA processing
- Periodic embargo cleanup

### 4. Connection Lifecycle
- Dandelion setup on connection
- Route generation
- Cleanup on disconnection

### 5. Thread Management
- ThreadDandelionShuffle started after networking
- Waits for IBD completion
- Runs until shutdown

## Critical Implementation Notes

### 1. Thread Safety
- All Dandelion vectors protected by cs_vNodes
- Embargo map accessed under connman lock
- Route modifications synchronized

### 2. Memory Management
- Stempool has same size limits as mempool
- Transactions exist in either stempool OR mempool, never both
- Proper cleanup on disconnection

### 3. Privacy Considerations
- PoissonNextSend prevents timing correlation
- Per-inbound-edge routing prevents fingerprinting
- Limited destinations reduce graph analysis

### 4. Compatibility
- Non-Dandelion nodes handled gracefully
- Discovery mechanism for capability detection
- Fallback to regular broadcast if no stem peers

### 5. Attack Resistance
- 10% fluff probability ensures propagation
- Embargo timeouts prevent indefinite delays
- Route shuffling limits learning attacks
- Missing routes trigger immediate fluff

### 6. Important Behavioral Details

1. **Stem transactions are never signed as witness** in inventory
2. **Embargoes use microsecond precision** for timing
3. **Route selection uses load balancing** not pure random
4. **Stempool transactions don't trigger wallet notifications**
5. **CheckDandelionEmbargoes called twice** in ProcessMessage

### 7. Key Differences from Bitcoin

1. **Two destinations** instead of one
2. **Embargo system** not in original BIP-156
3. **Stempool** as separate data structure
4. **Discovery mechanism** for capability detection
5. **Load-balanced** route selection

## Porting Checklist for v8.26

When porting to v8.26, ensure:

- [ ] All 27 files with Dandelion code are updated
- [ ] Stempool initialization in init.cpp
- [ ] PeerManager constructor accepts stempool
- [ ] CChainState constructor accepts stempool
- [ ] Network message handlers for DANDELIONTX
- [ ] Protocol constants (MSG_DANDELION_TX = 5)
- [ ] Thread management in CConnman
- [ ] Wallet integration for stem transactions
- [ ] Embargo system fully implemented
- [ ] Route shuffling thread started
- [ ] Logging category added
- [ ] Command line options working
- [ ] Test coverage ported
- [ ] All constants match v8.22.2 values
- [ ] Debug string generation working
- [ ] Reorg handling for both pools
- [ ] Connection lifecycle management
- [ ] Load balancing in route selection
- [ ] Discovery hash mechanism
- [ ] Proper thread shutdown handling

## Summary

DigiByte's Dandelion++ implementation in v8.22.2 is a sophisticated privacy enhancement that:
- Routes transactions through random linear paths before broadcasting
- Uses a separate stempool for stem phase transactions  
- Implements embargoes to prevent timing analysis
- Periodically reshuffles routes to prevent learning attacks
- Maintains compatibility with non-Dandelion nodes
- Provides comprehensive logging for debugging

The implementation spans 27 files with careful integration throughout the codebase, requiring attention to thread safety, memory management, and network protocol details when porting to v8.26.