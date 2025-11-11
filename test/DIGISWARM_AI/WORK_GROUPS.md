# Test Groups Organization

## Overall Status (2025-08-27)
- **Total Tests**: 247 (excluding skipped)
- **Passing**: 234 (94.7%)
- **Failing**: 13 (5.3%)
- **Skipped**: 18

---

## Group 1: Core Features & P2P (5 tests)
**Status**: 🔴 Not Started
```
feature_block.py
feature_segwit.py --legacy-wallet
p2p_headers_sync_with_minchainwork.py
wallet_balance.py --descriptors
wallet_balance.py --legacy-wallet
```

## Group 2: RPC & PSBT Operations (4 tests)
**Status**: 🔴 Not Started
```
rpc_packages.py
rpc_psbt.py --descriptors
rpc_psbt.py --legacy-wallet
rpc_rawtransaction.py --legacy-wallet
```

## Group 3: Wallet Transactions & Fees (4 tests)
**Status**: 🔴 Not Started
```
wallet_bumpfee.py --descriptors
wallet_bumpfee.py --legacy-wallet
wallet_create_tx.py --descriptors
wallet_create_tx.py --legacy-wallet
```

## Common Fix Patterns

### Critical Constants
- Block reward: 50 BTC → 72000 DGB
- Block time: 600s → 15s
- Maturity: 100 → 8 blocks (or 100 for COINBASE_MATURITY_2)
- Fees: vB → KvB (multiply by 1000)
- Addresses: bcrt1 → dgbrt1

### Common Solutions
- Add `-dandelion=0` to disable Dandelion++
- Use `-maxtxfee=100` for high fee transactions
- Apply `maxfeerate=0` for sendrawtransaction calls

## Notes
- Always test both --descriptors and --legacy-wallet variants where applicable
- Check digibyte-v8.22.2 reference when in doubt
- Document any new fix patterns in COMMON_FIXES.md