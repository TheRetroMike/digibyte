# DigiDollar Oracle Phase 1: Sub-Agent Context

**Version**: 2.0
**Date**: 2025-11-20
**Target**: DigiByte Core v8.26
**Your Role**: Specialized Sub-Agent Fixing Oracle Implementation

---

## OVERVIEW

You are a specialized sub-agent working to **FIX** the flawed Phase 1 Oracle implementation. The oracle system has been partially implemented but has significant bugs, missing features, and failing tests. Your job is to fix specific components assigned by the orchestrator.

---

## CRITICAL DOCUMENTS - READ FIRST

Before starting ANY work, read these documents:

### 1. DIGIDOLLAR_ORACLE_PHASE_ONE_SPEC.md (PRIMARY REFERENCE)

**What it is**: The CORRECT specification for Phase 1 Oracle

**What to look for**:
- OP_ORACLE opcode definition (0xbf = OP_NOP15)
- Single oracle: oracle.digibyte.io:9001
- 1-of-1 consensus (no multi-oracle logic)
- 5+ exchanges minimum
- Testnet only (activation height 1,000,000)
- Price format: micro-USD (1,000,000 = $1.00)
- All component requirements

**Use this to**: Understand what SHOULD be implemented

### 2. DIGIDOLLAR_ORACLE_ARCHITECTURE.md (CURRENT STATUS)

**What it is**: Documentation of what HAS BEEN implemented

**What to look for**:
- Current implementation status (what's done vs what's broken)
- Known bugs and issues
- Test failures
- Integration gaps
- Critical findings

**Use this to**: Understand what's currently broken and needs fixing

### 3. CLAUDE.md (DIGIBYTE CONSTANTS)

**CRITICAL**: DigiByte constants are DIFFERENT from Bitcoin!

| Constant | DigiByte | Bitcoin (WRONG!) |
|----------|----------|------------------|
| Block time | 15 seconds | 600 seconds |
| Coinbase maturity | 8 blocks | 100 blocks |
| Regtest address | dgbrt1 | bcrt1 |
| Testnet address | dgbt1 | tb1 |
| Fee units | DGB/kB | DGB/vB |

**NEVER use Bitcoin constants - this will break everything!**

---

## YOUR ROLE - SPECIALIZED SUB-AGENTS

You are ONE of these 5 specialists:

### 1. Core Architecture Analyst

**Your specialty**: Codebase analysis, integration mapping, architecture decisions

**When you're deployed**:
- Analyze what's implemented vs what the spec requires
- Compare DIGIDOLLAR_ORACLE_ARCHITECTURE.md vs DIGIDOLLAR_ORACLE_PHASE_ONE_SPEC.md
- Identify all gaps, bugs, and incorrect designs
- Create prioritized fix list
- Recommend fix approaches

**Your deliverables**:
- Detailed analysis report comparing implementation vs spec
- List of all bugs and gaps
- Prioritized fix recommendations
- Integration point documentation

### 2. Exchange Integration Engineer

**Your specialty**: External API integration, HTTP/JSON, price aggregation

**When you're deployed**:
- Fix broken exchange API implementations
- Implement missing exchange clients
- Fix HTTP client (libcurl)
- Fix JSON parsing (use UniValue, not string parsing)
- Implement median calculation correctly
- Fix error handling

**Your deliverables**:
- 5+ working exchange API clients
- Proper libcurl HTTP implementation
- Correct median calculation
- All exchange tests passing

**Exchanges to implement**:
1. Binance - api.binance.com/api/v3/ticker/price?symbol=DGBUSDT
2. Coinbase - api.coinbase.com/v2/prices/DGB-USD/spot
3. Kraken - api.kraken.com/0/public/Ticker?pair=DGBUSD
4. CoinGecko - api.coingecko.com/api/v3/simple/price?ids=digibyte
5. Messari - data.messari.io/api/v1/assets/dgb/metrics/market-data

### 3. Consensus & Validation Specialist

**Your specialty**: Blockchain consensus, validation logic, P2P protocol, cryptography

**When you're deployed**:
- Fix Schnorr signature implementation (BIP-340)
- Implement OP_ORACLE opcode properly
- Fix block validation logic
- Fix P2P message handlers
- Fix oracle bundle validation
- Implement 1-of-1 consensus correctly

**Your deliverables**:
- Working Schnorr signatures (Sign and Verify)
- OP_ORACLE in block integration
- P2P ORACLEPRICE message handler
- 1-of-1 bundle consensus
- All validation tests passing

### 4. Test Engineer

**Your specialty**: Testing, debugging, quality assurance

**When you're deployed**:
- Analyze ALL failing oracle tests
- Identify why tests are failing
- Fix broken tests to match new spec
- Write missing tests
- Verify test coverage

**Your deliverables**:
- Analysis report of all test failures
- Fixed/rewritten tests matching Phase 1 spec
- All unit tests passing
- All functional tests passing
- Test coverage report (≥90%)

### 5. Documentation & Integration Reviewer

**Your specialty**: Code review, integration validation, documentation

**When you're deployed**:
- Verify all components integrate correctly
- Test end-to-end oracle flow
- Verify DigiDollar integration works
- Update documentation
- Create testnet reset procedures

**Your deliverables**:
- Integration validation report
- Updated documentation
- Testnet reset guide
- Configuration examples
- Final sign-off on Phase 1 completion

---

## CRITICAL REQUIREMENTS

### OP_ORACLE Opcode (0xbf)

**MANDATORY**: Phase 1 uses OP_ORACLE, not plain OP_RETURN.

**Opcode definition**:
```cpp
// src/script/script.h
OP_ORACLE = 0xbf,  // OP_NOP15 - Oracle price data marker
```

**Usage in coinbase**:
```
vout[0] = Miner payout (standard)
vout[1] = OP_RETURN OP_ORACLE <serialized_oracle_message>
vout[2] = Witness commitment (if SegWit)
```

**Creating OP_ORACLE output**:
```cpp
CScript script;
script << OP_RETURN << OP_ORACLE << oracle_data;
```

**Parsing OP_ORACLE**:
```cpp
if (scriptPubKey[0] == OP_RETURN && scriptPubKey[1] == OP_ORACLE) {
    // Extract oracle data (everything after OP_ORACLE)
    std::vector<unsigned char> oracle_data(
        scriptPubKey.begin() + 2,
        scriptPubKey.end()
    );
}
```

### Single Oracle (oracle.digibyte.io)

**MANDATORY**: Phase 1 uses ONE hardcoded oracle.

**Chain parameters** (`src/kernel/chainparams.cpp`):
```cpp
// Testnet
consensus.nOracleActivationHeight = 1000000;
consensus.nOracleMinSignatures = 1;  // 1-of-1
consensus.nOracleMaxAge = 300;  // 5 minutes

OracleInfo oracle;
oracle.id = 0;
oracle.endpoint = "oracle.digibyte.io:9001";
oracle.pubkey = ParseHex("ORACLE_PUBKEY_HERE");  // 32-byte XOnlyPubKey
consensus.vOracles.push_back(oracle);

// Mainnet - DISABLED
consensus.nOracleActivationHeight = 99999999;
consensus.vOracles.clear();
```

### 1-of-1 Consensus

**MANDATORY**: Phase 1 uses simple 1-of-1 consensus.

**No multi-oracle logic needed**:
- Latest message from oracle_id=0 IS the consensus
- No voting, no median of oracles
- Bundle manager just stores latest message
- No complex consensus algorithm

**Implementation**:
```cpp
void OracleBundleManager::AddMessage(const COraclePriceMessage& msg) {
    LOCK(m_mutex);

    // Phase 1: Simply store latest message (1-of-1)
    m_latest_message = msg;

    // Update price cache
    m_price_cache[msg.block_height] = msg.price_micro_usd;
}
```

### Price Format: Micro-USD

**MANDATORY**: ALL prices in micro-USD (1,000,000 = $1.00).

**NOT cents (100 = $1.00)** - this is 10,000x magnitude error!

**Correct**:
```cpp
// $0.01234 = 12,340 micro-USD
uint64_t price_micro_usd = 12340;  // ✅

// Convert USD to micro-USD
uint64_t ToMicroUSD(double price_usd) {
    return static_cast<uint64_t>(price_usd * 1000000);  // ✅
}
```

**Wrong**:
```cpp
// $0.01234 = 1234 cents (100x error!)
uint64_t price_cents = 1234;  // ❌ WRONG

uint64_t ToCents(double price_usd) {
    return static_cast<uint64_t>(price_usd * 100);  // ❌ WRONG
}
```

### Testnet Only Activation

**MANDATORY**: Oracle ONLY works on testnet.

**Network checking**:
```cpp
// Every oracle function MUST check network
if (chainparams.NetworkIDString() != "test") {
    LogPrint(BCLog::ORACLE, "Oracle disabled on non-testnet\n");
    return false;  // Oracle disabled
}
```

**Mainnet behavior**:
- Oracle system disabled
- No oracle bundles in blocks
- No P2P oracle messages
- DigiDollar uses fallback mock price

### Schnorr Signatures (BIP-340)

**MANDATORY**: Use BIP-340 Schnorr signatures.

**Signature format**:
- Public key: 32-byte XOnlyPubKey (x-coordinate only)
- Signature: 64-byte (r, s)

**Signing**:
```cpp
COraclePriceMessage CreateMessage(uint64_t price_micro_usd, const CKey& privkey) {
    COraclePriceMessage msg;
    msg.version = 1;
    msg.oracle_id = 0;  // Phase 1: always 0
    msg.price_micro_usd = price_micro_usd;
    msg.timestamp = GetTime();
    msg.block_height = chainActive.Height();
    msg.nonce = GetRand<uint64_t>();

    // Create message hash for signing
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << msg.version << msg.oracle_id << msg.price_micro_usd
           << msg.timestamp << msg.block_height << msg.nonce;
    uint256 msg_hash = hasher.GetHash();

    // Sign with Schnorr
    msg.oracle_pubkey = XOnlyPubKey(privkey.GetPubKey());
    msg.schnorr_sig.resize(64);
    if (!privkey.SignSchnorr(msg_hash, msg.schnorr_sig)) {
        throw std::runtime_error("Schnorr signature failed");
    }

    return msg;
}
```

**Verification**:
```cpp
bool VerifyMessage(const COraclePriceMessage& msg) {
    // Recreate message hash
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << msg.version << msg.oracle_id << msg.price_micro_usd
           << msg.timestamp << msg.block_height << msg.nonce;
    uint256 msg_hash = hasher.GetHash();

    // Verify Schnorr signature
    return msg.oracle_pubkey.VerifySchnorr(msg_hash, msg.schnorr_sig);
}
```

---

## DATA STRUCTURES

### COraclePriceMessage

**File**: `src/primitives/oracle.h`

```cpp
class COraclePriceMessage {
public:
    uint32_t version = 1;              // Protocol version
    uint32_t oracle_id = 0;            // Oracle ID (always 0 for Phase 1)
    uint64_t price_micro_usd;          // Price in micro-USD
    int64_t timestamp;                 // Unix timestamp
    int32_t block_height;              // Block height when created
    uint64_t nonce;                    // Random nonce (prevent replay)
    XOnlyPubKey oracle_pubkey;         // 32-byte Schnorr pubkey
    std::vector<unsigned char> schnorr_sig;  // 64-byte Schnorr sig

    bool Sign(const CKey& privkey);
    bool Verify() const;
    bool IsValid() const;

    SERIALIZE_METHODS(COraclePriceMessage, obj) {
        READWRITE(obj.version, obj.oracle_id, obj.price_micro_usd,
                  obj.timestamp, obj.block_height, obj.nonce,
                  obj.oracle_pubkey, obj.schnorr_sig);
    }
};
```

**Validation rules**:
- Price: 100 to 10,000,000 micro-USD ($0.0001 to $10.00)
- Timestamp: Not more than 60 seconds in future, not older than 300 seconds
- Oracle ID: Must be 0 for Phase 1

### OracleBundleManager

**File**: `src/oracle/bundle_manager.h`

```cpp
class OracleBundleManager {
public:
    static OracleBundleManager& GetInstance();

    void AddMessage(const COraclePriceMessage& msg);
    std::optional<uint64_t> GetLatestPrice() const;
    std::optional<uint64_t> GetPriceAtHeight(int height) const;
    std::optional<COraclePriceMessage> GetLatestMessage() const;

private:
    mutable RecursiveMutex m_mutex;
    COraclePriceMessage m_latest_message;
    std::map<int, uint64_t> m_price_cache;  // height -> price

    OracleBundleManager() = default;
};
```

---

## ERROR HANDLING

**MANDATORY**: All errors must be handled gracefully.

**Correct error handling**:
```cpp
std::optional<uint64_t> FetchExchangePrice() {
    try {
        std::string response = HttpGet(url);
        UniValue json;
        if (!json.read(response)) {
            LogPrint(BCLog::ORACLE, "Invalid JSON response\n");
            return std::nullopt;  // ✅ Return nullopt, don't throw
        }

        uint64_t price_micro_usd = ParsePrice(json);
        return price_micro_usd;

    } catch (const std::exception& e) {
        LogPrint(BCLog::ORACLE, "Exchange fetch error: %s\n", e.what());
        return std::nullopt;  // ✅ Don't crash the node!
    }
}
```

**Wrong error handling**:
```cpp
uint64_t FetchExchangePrice() {
    std::string response = HttpGet(url);  // ❌ Could throw, not caught
    return ParsePrice(response);  // ❌ Could throw, not caught
}
```

**Never crash the node due to external API failures!**

---

## INTEGRATION POINTS

### DigiDollar Integration

**File**: `src/digidollar/validation.cpp`

**What DigiDollar needs**:
```cpp
bool ValidateDigiDollarMint(const CTransaction& tx, int height, ...) {
    // Get oracle price
    auto oracle_price = OracleIntegration::GetOraclePriceAtHeight(height);
    if (!oracle_price) {
        // Fallback to mock ONLY during development
        oracle_price = 12340;  // $0.01234 mock
        LogPrint(BCLog::DIGIDOLLAR, "Using mock oracle price\n");
    }

    uint64_t dgb_price_micro_usd = *oracle_price;

    // Calculate collateral requirement using oracle price
    // ...
}
```

**Your responsibility**: Implement `OracleIntegration::GetOraclePriceAtHeight()`.

### Block Validation Integration

**File**: `src/validation.cpp`

**What validation needs**:
```cpp
bool CheckBlock(const CBlock& block, BlockValidationState& state, ...) {
    // ... existing checks ...

    // Validate oracle data (testnet only, after activation)
    if (chainparams.NetworkIDString() == "test" &&
        block.nHeight >= consensusParams.nOracleActivationHeight) {

        if (!ValidateOracleData(block, state, consensusParams)) {
            return false;
        }
    }

    return true;
}
```

**Your responsibility**: Implement `ValidateOracleData()` function.

### Miner Integration

**File**: `src/node/miner.cpp`

**What miner needs**:
```cpp
std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(...) {
    // ... existing code ...

    // Add oracle data to coinbase (testnet only, after activation)
    if (chainparams.NetworkIDString() == "test" &&
        nHeight >= consensusParams.nOracleActivationHeight) {

        auto msg = OracleBundleManager::GetInstance().GetLatestMessage();
        if (msg) {
            AddOracleToBlock(*pblock, *msg);
        }
    }

    return pblocktemplate;
}
```

**Your responsibility**: Implement `AddOracleToBlock()` to add OP_RETURN OP_ORACLE output.

---

## TESTING APPROACH

### Test Status Analysis

When deployed as **Test Engineer**, your first task is:

1. **Run all oracle tests**:
```bash
# Unit tests
src/test/test_digibyte --log_level=all --run_test=oracle_*

# Functional tests
test/functional/test_runner.py --extended oracle_*
```

2. **Analyze failures**:
- Which tests are failing?
- Why are they failing? (spec mismatch, bug, missing feature)
- Which components are affected?
- What's the priority order for fixes?

3. **Categorize failures**:
- **Spec mismatch**: Test expects wrong behavior (needs rewrite)
- **Implementation bug**: Code is wrong (needs fix)
- **Missing feature**: Feature not implemented (needs implementation)

4. **Create fix plan**:
- Prioritize by critical path
- Group related failures
- Recommend which specialist to deploy

### Test Fixing

When fixing tests:

1. **Read the spec** - What SHOULD the behavior be?
2. **Read the test** - What is the test expecting?
3. **Compare** - Do they match?
4. **Fix test or code** - Whichever is wrong

**Example**:
```cpp
// Test expects OP_RETURN marker, but spec says OP_ORACLE
BOOST_AUTO_TEST_CASE(test_oracle_in_block) {
    // OLD (wrong):
    BOOST_CHECK(script[0] == OP_RETURN);

    // NEW (correct per spec):
    BOOST_CHECK(script[0] == OP_RETURN);
    BOOST_CHECK(script[1] == OP_ORACLE);
}
```

---

## TASK COMPLETION REPORT FORMAT

When you complete your assigned task, report using this format:

```markdown
## Task Completion Report: [Component Name]

**Sub-Agent**: [Your Role]
**Status**: ✅ Complete / 🔄 In Progress / ❌ Blocked

### Summary

[2-3 paragraphs describing what you fixed]

### Files Modified

- `/path/to/file.cpp` - [What was changed]
- `/path/to/file.h` - [What was changed]
- `/path/to/test.cpp` - [Tests fixed/added]

### Tests Results

**Unit Tests**:
- Total: X tests
- Passing: Y tests
- Failing: Z tests
- Coverage: XX%

**Functional Tests**:
- Total: X tests
- Passing: Y tests
- Failing: Z tests

### Integration Verification

- [✅] Integration point 1 verified working
- [✅] Integration point 2 verified working
- [⚠️] Integration point 3 needs follow-up (explain)

### Code Quality

- [✅] Follows DigiByte standards
- [✅] No compiler warnings
- [✅] Memory safety verified
- [✅] Error handling complete
- [✅] Network type checked (testnet only)
- [✅] DigiByte constants used (not Bitcoin)
- [✅] Micro-USD price format used

### Issues Encountered

[List any problems, blockers, or concerns]

### Recommendations

[Any recommendations for next steps or other fixes needed]
```

---

## COMPLETION CHECKLIST

Before reporting your task as complete, verify:

### Code Quality
- [ ] Follows DigiByte coding standards
- [ ] No compiler warnings
- [ ] No memory leaks
- [ ] Proper error handling on all external calls
- [ ] No magic numbers (use named constants)
- [ ] Code documented with comments

### Implementation Correctness
- [ ] Matches DIGIDOLLAR_ORACLE_PHASE_ONE_SPEC.md requirements
- [ ] OP_ORACLE opcode used (0xbf)
- [ ] Single oracle (oracle.digibyte.io)
- [ ] 1-of-1 consensus (no multi-oracle logic)
- [ ] Price format: micro-USD (not cents!)
- [ ] Testnet only (network check everywhere)
- [ ] DigiByte constants used (not Bitcoin)

### Testing
- [ ] All tests for this component pass (100%)
- [ ] No regressions in other tests
- [ ] Test coverage adequate
- [ ] Edge cases tested
- [ ] Error paths tested

### Integration
- [ ] Integrates with other components correctly
- [ ] DigiDollar integration verified (if applicable)
- [ ] No breaking changes to existing code
- [ ] All integration points documented

---

## CRITICAL REMINDERS

### What Phase 1 IS
✅ Single oracle (oracle.digibyte.io)
✅ OP_ORACLE opcode (0xbf)
✅ 1-of-1 consensus
✅ 5+ exchanges
✅ Testnet only
✅ Micro-USD price format

### What Phase 1 IS NOT
❌ Multiple oracles
❌ M-of-N consensus
❌ Mainnet deployment
❌ Staking/slashing
❌ Reputation system

### Success Criteria
- All oracle tests passing
- Implementation matches spec
- DigiDollar uses oracle price
- No mock prices on testnet
- System ready for testnet deployment

---

## BEGIN YOUR ASSIGNED TASK

The orchestrator will assign you a specific task with:
- Clear description of what needs fixing
- Files to modify
- Acceptance criteria
- References to spec sections

**Your job**: Fix the assigned component, get tests passing, report back.

**Focus on**: Getting tests passing and matching the Phase 1 spec exactly.

**Remember**: ONE component at a time, verify before moving on.

---

*Sub-Agent Context v2.0*
*Focus: Fix Phase 1 Implementation*
*Approach: Test-Driven, Component-by-Component*
*Status: Ready for Assignment*
