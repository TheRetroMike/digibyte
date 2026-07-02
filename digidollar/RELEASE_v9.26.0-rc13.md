# DigiByte v9.26.0-rc13 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## What's New in RC13

### 🐛 Bug Fixes & Improvements

RC13 is a stabilization release with Qt GUI improvements and RPC fixes.

#### Qt GUI Fixes

- **Oracle Price Auto-Refresh in Mint Widget** — The DigiDollar Mint widget now automatically refreshes the oracle price, ensuring users always see current pricing when minting DUSD
- **Improved RPC Exception Handling** — Better error handling in DigiDollar RPC calls prevents GUI crashes when the backend returns unexpected data
- **Lock Tier Display Names Fixed** — The DigiDollar GUI widgets now correctly display lock tier names (was showing incorrect tier labels)

#### RPC Fixes

- **`getalloracleprices` Client Conversion** — Fixed missing entry in the client conversion table that was preventing proper RPC response handling

---

## Commits Since RC12

```
fix(rpc): add getalloracleprices to client conversion table
fix(qt): add oracle price auto-refresh timer to DD Mint widget
fix(qt): improve exception handling in DigiDollar RPC calls
fix(qt): correct lock tier display names in DigiDollar GUI widgets
```

---

## Upgrade Notes

**RC13 uses the same testnet13 network as RC12 (port 12030). Your existing testnet data and wallets will work — no migration needed.**

If you're upgrading from RC12, simply replace the binaries and restart.

---

## Technical Changes

| File | Change |
|------|--------|
| `configure.ac` | Version bump RC12 → RC13 |
| `src/qt/digidollarmintwidget.cpp` | Add oracle price auto-refresh timer |
| `src/qt/digidollar*.cpp` | Improve RPC exception handling |
| `src/qt/digidollarpositionswidget.cpp` | Fix lock tier display names |
| `src/rpc/client.cpp` | Add `getalloracleprices` to conversion table |
| `src/qt/res/icons/digibyte_wallet.png` | Update wallet splash to RC13 |

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
| Oracle Consensus | 4-of-7 Schnorr threshold |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc13-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc13-win64.zip` |
| macOS Apple Silicon (M1/M2/M3/M4) | `digibyte-9.26.0-rc13-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc13-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc13-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc13-aarch64-linux-gnu.tar.gz` |

---

## Quick Start

If you're new to DigiDollar testing, see the complete setup instructions in the [RC12 Release Notes](./RELEASE_v9.26.0-rc12.md) — the setup process is identical.

---

## Feedback & Community

Please report issues and feedback to help us prepare for mainnet launch.

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues
