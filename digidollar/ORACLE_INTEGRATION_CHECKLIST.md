# Oracle System Integration Checklist

## Overview

This checklist is a historical integration checklist refreshed with V1-critical
terminology. Use the final audit ledger/report as the authoritative release
readiness checklist.

**Phase**: V1 MuSig2 oracle bundles
**Status**: Pre-Deployment Verification
**Target Network**: mainnet/testnet/regtest behind deployment gates

---

## Pre-Deployment Verification

### 1. Exchange API Integration

- [ ] **BinanceFetcher** - Fetches DGB/USDT price
- [ ] **CoinGeckoFetcher** - Fetches DGB/USD price (no API key)
- [ ] **CoinMarketCapFetcher** - Fetches DGB/USD price (API key required)
- [ ] **CoinbaseFetcher** - Fetches DGB/USD price
- [ ] **KrakenFetcher** - Fetches DGB/USD price
- [ ] **MessariFetcher** - Fetches DGB/USD price (no API key)
- [ ] **KuCoinFetcher** - Fetches DGB/USDT price
- [ ] **CryptoComFetcher** - Fetches DGB/USD price

**Verification**:
```bash
# Test each exchange fetcher individually
./test/functional/test_oracle_exchange_apis.py --binance
./test/functional/test_oracle_exchange_apis.py --coingecko
./test/functional/test_oracle_exchange_apis.py --coinmarketcap
# ... etc
```

**Expected Results**:
- At least 3 exchanges return valid prices
- Prices within 10% of each other
- Median calculation works correctly
- Outlier filtering works (if 1-2 exchanges fail)

**Status**: ⬜ Not Started | ⬜ In Progress | ⬜ Complete | ⬜ Failed

---

### 2. Oracle Node Integration

- [ ] **OracleNode initialization** - Node starts with valid keypair
- [ ] **Price fetching** - Fetches median from exchanges every 60 seconds
- [ ] **Message creation** - Creates COraclePriceMessage with valid data
- [ ] **Schnorr signing** - Signs message with BIP-340 signature
- [ ] **Signature verification** - Verify() returns true
- [ ] **Broadcasting** - Broadcasts to P2P network
- [ ] **Thread safety** - Price update thread works correctly

**Verification**:
```bash
# Unit tests
./src/test/test_digibyte --run_test=oracle_node_tests

# Start oracle node manually
digibyted -testnet -oracle=1 -oracleid=0 -oraclekey=<private_key_hex>

# Check logs
tail -f ~/.digibyte/testnet3/debug.log | grep "Oracle:"
```

**Expected Results**:
- Oracle node starts successfully
- Price fetched every 60 seconds
- Messages signed with valid Schnorr signatures
- Messages broadcast to peers
- No crashes or errors

**Status**: ⬜ Not Started | ⬜ In Progress | ⬜ Complete | ⬜ Failed

---

### 3. P2P Network Integration

- [ ] **ORACLEPRICE message type** - Defined in protocol.h
- [ ] **ORACLEBUNDLE message type** - Defined in protocol.h
- [ ] **GETORACLES message type** - Defined in protocol.h
- [ ] **Message serialization** - Serialize/deserialize correctly
- [ ] **ProcessMessage handler** - Processes ORACLEPRICE messages
- [ ] **Message validation** - ValidateIncomingMessage() works
- [ ] **Rate limiting** - DOS protection active
- [ ] **Duplicate detection** - Rejects duplicate messages
- [ ] **Message relay** - Floods to connected peers

**Verification**:
```bash
# Unit tests
./src/test/test_digibyte --run_test=oracle_p2p_tests

# Functional tests
./test/functional/feature_oracle_p2p.py

# Multi-node test
./test/functional/feature_oracle_p2p.py --nodes=10
```

**Expected Results**:
- Messages propagate to all nodes
- Invalid messages rejected
- Rate limiting prevents spam
- Duplicate messages ignored
- Network stable under load

**Status**: ⬜ Not Started | ⬜ In Progress | ⬜ Complete | ⬜ Failed

---

### 4. Bundle Manager Integration

- [ ] **Singleton initialization** - GetInstance() works
- [ ] **AddOracleMessage()** - Adds messages to pending pool
- [ ] **Message validation** - Validates signatures before adding
- [ ] **Bundle creation** - Creates v0x03 MuSig2 bundles at quorum
- [ ] **GetCurrentBundle()** - Returns valid bundle for epoch
- [ ] **HasConsensus()** - Returns true only at the active quorum threshold
- [ ] **ExtractOracleBundle()** - Extracts bundle from coinbase
- [ ] **CreateOracleScript()** - Creates valid OP_RETURN script
- [ ] **AddOracleBundleToBlock()** - Adds bundle to coinbase
- [ ] **Price cache** - UpdatePriceCache() and GetOraclePriceForHeight()

**Verification**:
```bash
# Unit tests
./src/test/test_digibyte --run_test=oracle_bundle_manager_tests

# Integration tests
./src/test/test_digibyte --run_test=oracle_integration_tests
```

**Expected Results**:
- Bundle created with 1 message
- Bundle serialization < 83 bytes
- Signatures verify correctly
- Price cache updated on ConnectBlock
- Thread-safe operations

**Status**: ⬜ Not Started | ⬜ In Progress | ⬜ Complete | ⬜ Failed

---

### 5. Miner Integration

- [ ] **CreateNewBlock()** - Calls AddOracleBundleToBlock()
- [ ] **DigiDollar activation check** - Only adds after activation
- [ ] **Coinbase structure** - vout[0] = payout, vout[1] = OP_RETURN
- [ ] **OP_RETURN format** - Correct serialization
- [ ] **Size limits** - Bundle fits in 83 bytes
- [ ] **Graceful degradation** - Continues if no oracle data
- [ ] **Block template** - Valid with oracle bundle
- [ ] **Merkle root** - Recalculated correctly

**Verification**:
```bash
# Unit tests
./src/test/test_digibyte --run_test=oracle_miner_tests

# Functional test
./test/functional/feature_oracle_mining.py

# Manual mining
digibyte-cli -testnet generatetoaddress 1 <address>
digibyte-cli -testnet getblock <blockhash> 2
# Check coinbase vout[1] for OP_RETURN
```

**Expected Results**:
- Blocks contain oracle bundles
- OP_RETURN in vout[1]
- Bundle size < 83 bytes
- Blocks mine successfully
- Merkle root valid

**Status**: ⬜ Not Started | ⬜ In Progress | ⬜ Complete | ⬜ Failed

---

### 6. Block Validation Integration

- [ ] **CheckBlock()** - Calls ValidateBlockOracleData()
- [ ] **ValidateBlockOracleData()** - Validates bundle structure
- [ ] **Signature verification** - Verifies Schnorr signatures
- [ ] **Quorum check** - Requires the V1 active signer threshold
- [ ] **Timestamp validation** - Messages not too old
- [ ] **Epoch validation** - Bundle epoch matches block height
- [ ] **OP_RETURN extraction** - Correctly extracts from coinbase
- [ ] **Deserialization** - Handles corrupt data gracefully
- [ ] **Invalid block rejection** - Rejects blocks with bad oracle data
- [ ] **Optional oracle data** - Accepts blocks without oracle data

**Verification**:
```bash
# Unit tests
./src/test/test_digibyte --run_test=oracle_block_validation_tests

# Functional tests
./test/functional/feature_oracle_validation.py

# Invalid block test
./test/functional/feature_oracle_invalid_blocks.py
```

**Expected Results**:
- Valid blocks accepted
- Invalid signatures rejected
- Wrong message count rejected
- Blocks without oracle data accepted
- No crashes on corrupt data

**Status**: ⬜ Not Started | ⬜ In Progress | ⬜ Complete | ⬜ Failed

---

### 7. Price Cache Integration

- [ ] **ConnectBlock()** - Updates price cache
- [ ] **UpdatePriceCache()** - Stores height → price mapping
- [ ] **GetOraclePriceForHeight()** - Retrieves cached price
- [ ] **Thread safety** - Mutex-protected cache access
- [ ] **LRU eviction** - Keeps last 1000 blocks
- [ ] **Deployment-gated** - Active only when the deployment predicate is active
- [ ] **Reorg handling** - Handles chain reorganizations
- [ ] **Persistence** - Price cache survives restarts (optional)

**Verification**:
```bash
# Unit tests
./src/test/test_digibyte --run_test=oracle_price_cache_tests

# Functional test
./test/functional/feature_oracle_price_cache.py

# Reorg test
./test/functional/feature_oracle_reorg.py
```

**Expected Results**:
- Prices cached on block connect
- Prices retrieved correctly
- Cache size limited to 1000 entries
- Thread-safe under concurrent access
- Reorgs handled correctly

**Status**: ⬜ Not Started | ⬜ In Progress | ⬜ Complete | ⬜ Failed

---

### 8. DigiDollar Integration

- [ ] **OracleIntegration namespace** - Defined in bundle_manager.h
- [ ] **GetOraclePriceForHeight()** - Returns price for height
- [ ] **GetCurrentOraclePrice()** - Returns latest price
- [ ] **IsOracleSystemReady()** - Checks system status
- [ ] **Fallback price** - Returns safe default if unavailable
- [ ] **Mint validation** - Uses oracle price correctly
- [ ] **Redeem validation** - Uses oracle price correctly
- [ ] **Mempool validation** - Uses current oracle price
- [ ] **Block validation** - Uses height-specific price

**Verification**:
```bash
# Unit tests
./src/test/test_digibyte --run_test=digidollar_oracle_tests

# Functional tests
./test/functional/feature_digidollar_oracle.py

# Mint test
digibyte-cli -testnet createdigidollarmint <dgb_amount>
digibyte-cli -testnet sendrawtransaction <signed_tx>

# Redeem test
digibyte-cli -testnet createdigidollarredeem <dd_amount>
digibyte-cli -testnet sendrawtransaction <signed_tx>
```

**Expected Results**:
- DigiDollar mints use oracle price
- DigiDollar redeems use oracle price
- Fallback price used if oracle unavailable
- Mempool accepts valid DD transactions
- Invalid DD transactions rejected

**Status**: ⬜ Not Started | ⬜ In Progress | ⬜ Complete | ⬜ Failed

---

## Integration Test Suite

### Unit Tests

- [ ] **oracle_exchange_tests.cpp** - Exchange API tests
- [ ] **oracle_message_tests.cpp** - COraclePriceMessage tests
- [ ] **oracle_p2p_tests.cpp** - P2P message tests
- [ ] **oracle_bundle_manager_tests.cpp** - Bundle manager tests
- [ ] **oracle_miner_tests.cpp** - Miner integration tests
- [ ] **oracle_block_validation_tests.cpp** - Block validation tests
- [ ] **oracle_integration_tests.cpp** - End-to-end integration tests
- [ ] **digidollar_oracle_tests.cpp** - DigiDollar integration tests

**Verification**:
```bash
# Run all oracle unit tests
./src/test/test_digibyte --run_test=oracle_*
./src/test/test_digibyte --run_test=digidollar_oracle_tests

# Expected: All tests PASS
```

**Status**: ⬜ All Pass | ⬜ Some Fail | ⬜ Not Run

---

### Functional Tests

- [ ] **feature_oracle_exchange_apis.py** - Exchange API integration
- [ ] **feature_oracle_p2p.py** - P2P message propagation
- [ ] **feature_oracle_mining.py** - Oracle bundle mining
- [ ] **feature_oracle_validation.py** - Block validation
- [ ] **feature_oracle_price_cache.py** - Price cache
- [ ] **feature_oracle_reorg.py** - Chain reorganization
- [ ] **feature_oracle_integration.py** - End-to-end integration
- [ ] **feature_digidollar_oracle.py** - DigiDollar integration

**Verification**:
```bash
# Run all oracle functional tests
./test/functional/test_runner.py feature_oracle_*
./test/functional/test_runner.py feature_digidollar_oracle.py

# Expected: All tests PASS
```

**Status**: ⬜ All Pass | ⬜ Some Fail | ⬜ Not Run

---

## Performance Testing

### Network Performance

- [ ] **Message propagation time** - < 5 seconds to all nodes
- [ ] **Bandwidth usage** - < 20 bytes/second per node
- [ ] **P2P message overhead** - Negligible impact
- [ ] **Block size increase** - ~150 bytes per block (< 1%)

**Verification**:
```bash
# Multi-node propagation test
./test/functional/feature_oracle_p2p.py --nodes=50 --measure-time

# Expected: Messages reach all nodes in < 5 seconds
```

**Status**: ⬜ Pass | ⬜ Fail | ⬜ Not Run

---

### Block Validation Performance

- [ ] **Signature verification time** - < 1ms per message
- [ ] **Bundle extraction time** - < 0.1ms
- [ ] **Total validation overhead** - < 2ms per block
- [ ] **No impact on block time** - Still 15 seconds average

**Verification**:
```bash
# Benchmark block validation
./src/bench/bench_digibyte --filter=OracleBlockValidation

# Expected: < 2ms overhead per block
```

**Status**: ⬜ Pass | ⬜ Fail | ⬜ Not Run

---

### Memory Usage

- [ ] **Price cache size** - ~16 KB (1000 blocks)
- [ ] **Pending messages** - bounded by P2P/session admission limits
- [ ] **Bundle manager state** - < 10 KB
- [ ] **Total overhead** - < 100 KB

**Verification**:
```bash
# Monitor memory usage
digibyted -testnet &
PID=$!
watch -n 1 "ps -p $PID -o rss | tail -1"

# Check memory increase with oracle system
# Expected: < 100 KB increase
```

**Status**: ⬜ Pass | ⬜ Fail | ⬜ Not Run

---

## Security Testing

### DOS Protection

- [ ] **Rate limiting** - P2P admission limits messages before relay; operator fetch loop is 60 seconds
- [ ] **Message size limits** - Rejects oversized messages
- [ ] **Signature verification** - Before relay, not after
- [ ] **Duplicate detection** - Rejects duplicate messages
- [ ] **Peer misbehavior** - Bans peers sending invalid messages

**Verification**:
```bash
# DOS attack test
./test/functional/feature_oracle_dos_protection.py

# Expected: Node remains stable, attackers banned
```

**Status**: ⬜ Pass | ⬜ Fail | ⬜ Not Run

---

### Signature Security

- [ ] **Schnorr signatures** - BIP-340 compliant
- [ ] **No signature malleability** - Fixed-size signatures
- [ ] **Deterministic signing** - RFC 6979
- [ ] **X-only pubkeys** - 32-byte compact format
- [ ] **Invalid signatures rejected** - No false positives

**Verification**:
```bash
# Signature malleability test
./src/test/test_digibyte --run_test=oracle_signature_tests

# Expected: All invalid signatures rejected
```

**Status**: ⬜ Pass | ⬜ Fail | ⬜ Not Run

---

### Price Manipulation Resistance

- [ ] **Outlier filtering** - 10% deviation threshold
- [ ] **Minimum sources** - Requires 3 valid exchanges
- [ ] **Median calculation** - Resistant to 1-2 outliers
- [ ] **Fallback price** - Safe default if all fail

**Note**: Current V1 release behavior uses MuSig2 v0x03 oracle bundles with a
9-signature launch quorum. Older 1-of-1 Phase One language is historical only.

**Verification**:
```bash
# Outlier filtering test
./src/test/test_digibyte --run_test=oracle_exchange_tests/outlier_filtering

# Expected: Outliers correctly filtered
```

**Status**: ⬜ Pass | ⬜ Fail | ⬜ Not Run

---

## Deployment Readiness

### Configuration

- [ ] **chainparams.cpp** - Oracle pubkeys hardcoded
- [ ] **Default config** - Oracle enabled on testnet
- [ ] **RPC commands** - Oracle RPC endpoints working
- [ ] **Logging** - Oracle logs with BCLog::DIGIDOLLAR
- [ ] **Help text** - Oracle CLI arguments documented

**Verification**:
```bash
# Check oracle config
digibyted -testnet -help | grep -i oracle

# Check RPC commands
digibyte-cli -testnet help | grep -i oracle

# Expected: All oracle options documented
```

**Status**: ⬜ Complete | ⬜ Incomplete

---

### Documentation

- [ ] **ORACLE_INTEGRATION_FLOW.md** - Integration flow documented
- [ ] **ORACLE_INTEGRATION_CHECKLIST.md** - This checklist
- [ ] **DIGIDOLLAR_ORACLE_ORCHESTRATOR_PROMPT.md** - System specification
- [ ] **Code comments** - All oracle code well-commented
- [ ] **RPC documentation** - Oracle RPC commands documented
- [ ] **User guide** - Oracle node setup guide

**Status**: ⬜ Complete | ⬜ Incomplete

---

### Monitoring & Observability

- [ ] **Logging** - Comprehensive oracle logging
- [ ] **RPC stats** - getoracle stats command
- [ ] **Metrics** - Oracle message counts, cache hits, etc.
- [ ] **Health checks** - Oracle system status endpoint
- [ ] **Alerts** - Warning on oracle failures

**Verification**:
```bash
# Check oracle stats
digibyte-cli -testnet getoracle stats

# Check oracle logs
tail -f ~/.digibyte/testnet3/debug.log | grep "Oracle:"

# Expected: Detailed stats and logs available
```

**Status**: ⬜ Complete | ⬜ Incomplete

---

## Testnet Deployment

### Pre-Deployment

- [ ] **All tests passing** - Unit + functional tests
- [ ] **Performance acceptable** - < 2ms validation overhead
- [ ] **Security verified** - DOS protection working
- [ ] **Documentation complete** - All docs written
- [ ] **Code review** - Peer review completed

**Status**: ⬜ Ready | ⬜ Not Ready

---

### Deployment Steps

1. [ ] **Deploy oracle node** - Start oracle node on testnet
2. [ ] **Verify connectivity** - Oracle connects to testnet peers
3. [ ] **Verify price fetching** - Oracle fetches prices from exchanges
4. [ ] **Verify message broadcasting** - Messages broadcast to network
5. [ ] **Verify mining** - Blocks mined with oracle bundles
6. [ ] **Verify validation** - Blocks validated correctly
7. [ ] **Verify price cache** - Prices cached and accessible
8. [ ] **Verify DigiDollar** - DD transactions use oracle prices

**Deployment Commands**:
```bash
# 1. Start oracle node
digibyted -testnet -oracle=1 -oracleid=0 -oraclekey=<private_key_hex> -debug=digidollar

# 2. Verify connectivity
digibyte-cli -testnet getpeerinfo | grep -c "addr"

# 3. Verify price fetching
tail -f ~/.digibyte/testnet3/debug.log | grep "Oracle: Fetched price"

# 4. Verify broadcasting
tail -f ~/.digibyte/testnet3/debug.log | grep "Oracle: Broadcast"

# 5. Mine blocks
digibyte-cli -testnet generatetoaddress 10 <address>

# 6. Check oracle bundles in blocks
digibyte-cli -testnet getblock <blockhash> 2 | grep -A 20 "vout"

# 7. Check price cache
digibyte-cli -testnet getoracle price <height>

# 8. Test DigiDollar mint
digibyte-cli -testnet createdigidollarmint 100
```

**Status**: ⬜ Deployed | ⬜ Not Deployed

---

### Post-Deployment Monitoring

- [ ] **24-hour stability test** - No crashes or errors
- [ ] **Price accuracy** - Prices match exchange medians
- [ ] **Block propagation** - No delays from oracle bundles
- [ ] **DigiDollar usage** - DD transactions working correctly
- [ ] **No security incidents** - No DOS attacks successful

**Monitoring Period**: 7 days minimum

**Status**: ⬜ Stable | ⬜ Issues Found | ⬜ Not Started

---

## Post-V1 Preparation

### Future Enhancements

- [ ] **Roster expansion** - deterministic governance/activation for post-launch slots
- [ ] **Signer reselection** - policy for intra-epoch withholding
- [ ] **Operator recovery** - clearer status and restart procedures
- [ ] **Mainnet deployment** - gated by final audit and Jared decisions
- [ ] **Oracle reputation** - Track oracle accuracy

**Status**: ⬜ Planned | ⬜ In Progress | ⬜ Not Started

---

## Sign-Off

### Integration Team

- [ ] **Oracle Developer** - _________________ Date: _______
- [ ] **DigiDollar Developer** - _________________ Date: _______
- [ ] **P2P Developer** - _________________ Date: _______
- [ ] **Validation Developer** - _________________ Date: _______
- [ ] **QA Engineer** - _________________ Date: _______

### Deployment Approval

- [ ] **Technical Lead** - _________________ Date: _______
- [ ] **Security Auditor** - _________________ Date: _______
- [ ] **Project Manager** - _________________ Date: _______

---

## Issue Tracking

| Issue ID | Component | Severity | Status | Owner | Notes |
|----------|-----------|----------|--------|-------|-------|
| | | | | | |
| | | | | | |
| | | | | | |

---

## Deployment Checklist Summary

**Total Items**: 142
**Completed**: ___
**Failed**: ___
**Not Started**: ___

**Overall Status**: ⬜ Ready for Deployment | ⬜ Not Ready

**Deployment Date**: _______________

---

**Document Version**: 1.0
**Last Updated**: 2026-05-05
**Next Review**: Final-audit closure
