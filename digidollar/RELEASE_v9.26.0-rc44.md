# DigiByte Core v9.26.0-rc44 Release Notes

RC44 is the DigiDollar release candidate for a full public testnet reset and
the 35-slot oracle roster. Mainnet and testnet26 now expose 35 active MuSig2
oracle slots with a 7-signature quorum. Slot 28 now uses the DigiHash Mining
Pool key, slot 31 uses the Peer2Peer / DigiRoos key, and all 35 configured
slots contain valid compressed secp256k1 oracle keys.

## Testnet26 Reset

RC44 starts a fresh public DigiDollar testnet: `testnet26`.

RC41/RC42/RC43 used `testnet25`. RC44 does not. Old `testnet25` blocks,
chainstate, peer addresses, cached oracle messages, signed nonces, and
DigiDollar positions are not RC44 proof. Back up wallet and oracle key material
before wiping old data, then start clean on `testnet26`.

| Parameter | Value |
|-----------|-------|
| Testnet name | `testnet26` |
| Data directory | `testnet26` |
| Genesis timestamp | `1780156800` (2026-05-30 16:00:00 UTC) |
| Genesis hash | `0x0c9af936f28f7bd0e90c8f6235399063a026ed267bb53da398313b5d7aa55d82` |
| Merkle root | `0x76eca59f6a477206c602b72682a901fe87c48d1a6335ec3d094d5837c508c197` |
| Genesis nonce | `1283721` |
| Network magic | `fe c6 b9 e7` |
| Default P2P port | `12033` |
| RPC port | `14026` |
| DigiDollar activation | BIP9 bit 23, minimum height 600 |

## Operator Migration

1. Stop RC43 or older testnet nodes.
2. Back up wallets and oracle key material.
3. Do not copy old `blocks/`, `chainstate/`, `peers.dat`, oracle messages, or cached MuSig2 state from `testnet25`.
4. Start RC44 with `-testnet`, using the new `testnet26` data directory.
5. Open and advertise P2P port `12033`.
6. Update static `addnode` and oracle endpoint config from port `12032` to `12033`.
7. Confirm `getblockchaininfo` reports the RC44 genesis hash above.
8. For mainnet, generate a new wallet/oracle key. Keep existing testnet keys for testnet26 only.

## Oracle Roster

Mainnet and testnet26 now use 35 active oracle slots and require 7 valid MuSig2
signatures per oracle bundle. Active oracle IDs are `0` through `34`; ID `35`
is outside the configured roster and must be rejected.

New slots added since the RC43 active roster:

| Slot | Operator | Compressed public key |
|------|----------|-----------------------|
| 21 | Twoface123 | `03d8165aa05b045de2a9b979b23a63cca1fec865784d12ab6f3f1bca8a90f3dd86` |
| 22 | LivingTheLife | `039241688b464c3f03f957cd85a3d1d6a760963be3707a16805b8064a8740e07ef` |
| 23 | ChozenOne43 | `03b6302e3cc8ee6d474c3c0078c25b87ce708757e2a81e8f4f01975dc4b25e0f6d` |
| 24 | ckunchained | `03926ed40635d294a554ec046a96d3fa58587521385c7df58ff21ede12a31add0e` |
| 25 | JMag | `034103ed4168d11dcaafa96494d5b3dd37247fa6deefa08d47f7004568792b1672` |
| 26 | HashedMax / HMPool | `038adf7df5fcd114178643f16aa0e3be8fa1e221ca421479e48c0bd04f2561d3a8` |
| 27 | DennisPitallano | `02557029e2419af54984f3f2fb600004c0a6f8573dac5730cfaab2048c80ba6894` |
| 28 | DigiHash Mining Pool | `03532efd6277226f38903401ec8317ba7cc8f13eb48dc8cb1e102fdc23488d7cef` |
| 29 | Michael E / medgboracle3452 | `03d566a244719aa577d828da31ad9863f94686f710ac1f0638914eff5692ec7d58` |
| 30 | Scott K / DigibyteDaily | `03603a0175197a1fe28859c71c69fd0081710c5569fceceaa96ce7de386d3ebf61` |
| 31 | Peer2Peer / DigiRoos | `02e96759ba5c67df6fc5cfc86977389d08fa31b4b297586f13d43bdd1569b459ba` |
| 32 | 3DogsKanab | `035878eb72be3710d8c3f9585e27374011a476378f5ea7f3ab2f2830ab052ec5f8` |
| 33 | LiberatedLark | `035b3729a255bfbcff1bfd5b72fdb1a971b098545db7e874751179bc22c7f7f0b0` |
| 34 | Manu_DGB_oracle | `03d8fe0cd773604fa78fb1cef7d1e88a05c2473961178e40a4ac3c9aeeb4cbca87` |

All oracle operators, miners, seeders, and testnet nodes must run the same RC44
binary before signing or mining on the new public testnet.

## Changes Since RC43

RC44 carries forward the RC43 wallet-state stabilization work and adds the
release reset and final oracle launch roster:

- New `testnet26` chain with new network magic, genesis block, P2P port `12033`,
  and a clean data directory.
- Mainnet and testnet26 oracle consensus expanded from the RC43 active set to
  35 active oracle slots with a 7-signature MuSig2 quorum.
- Added final oracle slots 21-34, including DigiHash Mining Pool at slot 28 and
  Peer2Peer / DigiRoos at slot 31.
- Added `getoraclesigners` to decode MuSig2 bundle signer IDs and participation
  details from oracle bundles.
- Hardened oracle P2P activation gating so oracle relay, heartbeat relay, and
  DigiDollar state-changing actions require the correct DigiDollar activation
  state.
- Hardened MuSig2 relay against stale, far-future, negative, and epoch-zero
  edge cases.
- Fixed stale oracle-bearing `getblocktemplate` cache reuse so miners rebuild
  templates when oracle commitments expire.
- Improved wallet DigiDollar UTXO tracking, redeem pending-state handling,
  redeem change history, stale redemption reconciliation, collateral outpoint
  resolution, restored wallet key visibility before `startoracle`, and rapid DD
  send safety.
- Updated operator scripts for testnet26, port `12033`, RPC `14026`, genesis
  validation, wallet-key startup, datadir stop behavior, and stricter RPC
  credential-file permissions.
- Cleaned stale operator documentation and scripts for private-key CLI
  assumptions, retired `getoracleinfo` references, retired testnet/port values,
  and older 17/21/24-oracle wording.
- Polished Qt DigiDollar surfaces for QR save failure handling, cross-network DD
  receive-request filtering, clearer transfer finality copy, volatility mint
  rejection handling, pending redeem display, and system-health polling.

## Validation

RC44 was validated after regenerating the final testnet26 genesis parameters
for the 7-of-35 oracle reset:

- `digibyted`, `digibyte-cli`, `digibyte-wallet`, and `test/test_digibyte`
  release build targets.
- Focused oracle tests covering MuSig2 activation, oracle RPC, roster alignment,
  and oracle config.
- Full unit suite: 3396 Boost test cases.
- DigiDollar operator-surface lint and `git diff --check`.
- Full functional suite: 375/375 passed with expected environment/unsupported
  skips.
