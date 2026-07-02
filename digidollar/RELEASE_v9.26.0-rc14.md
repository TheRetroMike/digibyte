# DigiByte v9.26.0-rc14 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## What's New in RC14

### 🔴 Critical Bug Fix

- **DGB Transactions No Longer Get Stuck After Minting DigiDollar** — Fixed a bug where sending DGB after minting DUSD could result in permanently stuck (unconfirmed) transactions. The wallet was accidentally trying to spend coins that were locked as DigiDollar collateral. Now the wallet properly locks collateral UTXOs on mint, filters them from coin selection, re-locks on restart/rescan, and unlocks on redeem. (5 layers of defense-in-depth)

### 🟠 Oracle Fixes

- **Oracle Keys Updated for Aussie Epic & LookIntoMyEyes** — Both operators generated new oracle wallets under RC13 after their RC12 keys were corrupted. Their new public keys are now in the code. Both can start their oracles immediately after upgrading to RC14.

- **Fixed "7-of-5 Consensus" Display Bug** — Oracle nodes were showing inflated consensus counts (e.g., "7-of-5") because stale messages from offline oracles were never cleaned up. Now purges messages older than 1 hour and displays accurate counts.

- **Fixed "is_stale: true" When Oracles Are Active** — The `getoracleprice` RPC was always reporting prices as stale, even with 7 oracles actively reporting. The check was purely block-height based, which fails when testnet blocks are slow. Now uses both block-height AND time-based checks — if either says it's fresh, it's not stale.

- **Fixed Oracle RPC Staleness, Count Limits, and Missing Names** — The `getoracles` RPC was showing incorrect online/offline status and missing oracle names.

- **Show Pending P2P Oracle Prices in `getoracles`** — You can now see oracle prices that have been received via P2P but haven't been included in a block yet.

- **Fixed Oracle Price Consistency** — `price_cents` and `price_usd` in `getoracleprice` now derive from the same source (micro-USD), fixing a mismatch where they could show inconsistent values.

### 🔒 Security Hardening

- **Oracle P2P Rate Limiting Fixed** — Legitimate peers were getting banned because duplicate relay messages (normal in P2P gossip networks) inflated rate limit counters. Duplicates are now filtered before counting, and only novel messages count toward limits.

- **Oracle DDoS Protection Hardened** — Schnorr signature verification now happens before rate limiting, so attackers can't exhaust rate limits with fake messages. Rate limit penalties increased (ban after 20 violations instead of 100+). Per-peer oracle message tracking added (mirrors how transaction relay works). Novel message limit tightened from 200/hr to 50/hr.

---

## Commits Since RC13

```
c9be4e4a67 fix: oracle price_cents consistency and test_oracle_price_format test
e4705433fa fix(oracle): purge stale messages to fix consensus count display
154040bcd0 fix(digidollar): Lock collateral UTXOs to prevent stuck DGB transactions
07a02cda86 security: harden oracle P2P message handlers against DDoS
2133d8c3a6 rpc: fix getoracleprice is_stale false positive on slow networks
14a91cd898 fix: oracle message rate limiting was banning legitimate peers
e0fd3dd43f Update oracle keys for Aussie Epic (ID 6) and LookIntoMyEyes (ID 7)
41f3c819b9 fix: getoracleprice RPC now works correctly with MockOracleManager in RegTest
9b9293537a rpc: fix oracle RPC staleness, count limits, and missing names
10160721aa rpc: show pending P2P oracle prices in getoracles
```

---

## Technical Changes

| File | Change |
|------|--------|
| `configure.ac` | Version bump RC13 → RC14 |
| `src/kernel/chainparams.cpp` | Updated oracle keys for Aussie Epic (ID 6) and LookIntoMyEyes (ID 7) |
| `src/wallet/spend.cpp` | Filter DD-locked UTXOs from coin selection |
| `src/wallet/digidollarwallet.cpp` | Re-lock collateral on wallet load and rescan |
| `src/rpc/digidollar.cpp` | Lock collateral on mint, unlock on redeem, fix price consistency, fix staleness |
| `src/oracle/bundle_manager.cpp` | Purge stale oracle messages, fix consensus count, cap seen_message_hashes |
| `src/net_processing.cpp` | Harden oracle P2P rate limiting and DDoS protection |
| `src/test/digidollar_rpc_tests.cpp` | Fix test_oracle_price_format for sub-cent prices |
| `src/qt/res/icons/digibyte_wallet.png` | Update wallet splash to RC14 |

---

## Upgrade Notes

**RC14 uses the same testnet13 network as RC12/RC13 (port 12030). Your existing testnet data and wallets will work — no migration needed.**

If you're upgrading from RC13, simply replace the binaries and restart.

**Oracle operators (Aussie Epic & LookIntoMyEyes):** Your new keys are active in this release. After upgrading, run `startoracle <your_id>` to begin reporting prices.

---

## Known Issues

- `digidollar_oracle.py` functional test has a pre-existing assertion failure (price aggregation median calculation) — does not affect runtime behavior
- Windows users may experience oracle connectivity issues after ~24 hours of continuous operation — investigating for RC15
- Fixed testnet mining difficulty causes slower-than-normal block times on some algorithms

---

## Testing

All tests validated:
- ✅ All 1489/1489 C++ unit tests pass (zero failures)
- ✅ 310/311 Python functional tests pass (1 pre-existing `digidollar_oracle.py` failure)

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (testnet13) |
| Default P2P Port | 12030 |
| Default RPC Port | 14025 |
| Oracle Node | oracle1.digibyte.io:12030 |
| Address Prefix | dgbt1... (bech32) |
| Phase Two Activation | Block 100 |
| Oracle Consensus | 5-of-8 Schnorr threshold |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc14-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc14-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc14-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc14-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc14-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc14-aarch64-linux-gnu.tar.gz` |

---

## Quick Start

If you're new to DigiDollar testing, see the complete setup instructions in the [RC12 Release Notes](./RELEASE_v9.26.0-rc12.md) — the setup process is identical.

---

## Troubleshooting

### "DigiDollar tab not appearing"
- Verify `digidollar=1` is under `[test]` section in config
- Verify `testnet=1` is at the top of config (not under any section)
- Restart the wallet after config changes

### "Not connecting to network"
- Check your firewall allows port 12030
- Verify `addnode=oracle1.digibyte.io` is under `[test]` in config

### "Oracle price shows 0 or N/A"
- Wait for sync to complete
- The oracle broadcasts price updates every few minutes
- Check Window > Console: `getoracleprice`

### "Transaction stuck / unconfirmed"
- **If you upgraded from RC13:** This was a known bug where minting DD could cause subsequent DGB sends to get stuck. RC14 fixes this. After upgrading, your wallet will automatically re-lock the correct UTXOs on startup.
- If still stuck, try: `abandontransaction <txid>` in the console

### "No wallet is loaded" when running oracle commands
- Add `-rpcwallet=oracle` to your `createoraclekey` and `startoracle` commands

### "Oracle not configured" from `startoracle`
- Run `createoraclekey` first to generate and store the key in your wallet

### "Old testnet data causing crashes"
- Delete your testnet10 and testnet11 folders completely
- RC14 uses testnet13 blockchain

For complete troubleshooting, see [RC12 Release Notes](./RELEASE_v9.26.0-rc12.md).

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

When reporting bugs, start your message with **BUG:** and include: what happened, steps to reproduce, platform (Windows/Linux/Mac), and any error messages from your debug.log file.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
