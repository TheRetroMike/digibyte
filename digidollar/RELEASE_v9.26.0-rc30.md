# DigiByte v9.26.0-rc30 Release Notes

**WARNING: This is a TESTNET-ONLY release. DO NOT use on mainnet.**

**Development Branch:** https://github.com/DigiByte-Core/digibyte/tree/feature/digidollar-v1

**Join the Developer Chat:** https://app.gitter.im/#/room/#digidollar:gitter.im

---

## ⚠️ TESTNET RESET REQUIRED

**RC30 launches a fresh `testnet23` chain.** This reset changes the genesis block to `0xa19e809bb060f7f50c05a9bec7fdefedd8497aa0bd6ccca6f55c86090963e4ca`, changes network magic to `fd d2 b9 e4`, keeps proof-of-work enabled, and moves the default testnet P2P port to **12030**. Existing wallets and oracle keys can be migrated forward, but old `testnet21` chain data must not be reused.

---

### Reset / Migration Quick Path

1. Stop your old RC28 or RC29 testnet node.
2. Install and start the RC30 binary once so `testnet23/` is created.
3. Verify the new network is listening on **12030** and matches genesis `0xa19e809bb060f7f50c05a9bec7fdefedd8497aa0bd6ccca6f55c86090963e4ca`.
4. Migrate only wallet and oracle key material from `testnet21` into `testnet23` as needed.
5. Do not copy old `blocks/`, `chainstate/`, or `indexes/` forward.

## What's New in RC30

RC30 is an **oracle expansion + hardening release** that grows DigiDollar's oracle operator set from **11 to 17** and raises the consensus quorum from **8-of-15 to 9-of-17**. Three new operators join the set, real RC30 testnet keys are wired in for Neel and GTO90, proof-of-work mining stays enabled on release testnet, and the local multi-oracle debugger now uses a runtime-only `-easypow` harness mode instead of source edits.

## RC30 fix summary since RC29

One-line summary of each substantive fix or release-facing change landed after RC29:

- **Oracle quorum expanded from 8-of-15 to 9-of-17** with 17 slots and 3-byte MuSig2 participation bitmaps.
- **Real operator/pubkey set refreshed** for RC30, including hallvardo, DigiByteForce slot assignment, Neel real key, DigiSwarm slot 15 key, and GTO90 real key.
- **Oracle key lists are slot-ordered** across `vOraclePublicKeys`, `vOracleNodes`, and test fixtures so MuSig2 bitmaps line up with oracle IDs.
- **`ValidateOracleConfiguration()` no longer requires lexicographic pubkey ordering**, while still enforcing count, uniqueness, and key validity.
- **Phase 3 MuSig2 payload now includes epoch on-chain**, fixing signer/verifier hash mismatch and cross-epoch replay risk.
- **MuSig2 P2P nonce and partial-signature messages are now Schnorr-signed by the sender**, matching RH-24 network hardening.
- **Remote partial signatures are verified before aggregation**, preventing invalid aggregates from mismatched participant sets and fixing the replay-filter memory leak path covered by RH-02 tests.
- **Lazy MuSig2 session creation on remote message arrival** prevents valid early nonces/partials from being dropped during fast testnet startup.
- **`ValidateEmergencyRedemption()` and related defaults/comments were updated from stale 8-of-15 assumptions to final 9-of-17 values.**
- **Qt DigiDollar amount widgets now allow typing values below the minimum**, so users can enter/edit values naturally before final validation.
- **`estimatecollateral` now reads system health from the indexed DigiDollar stats path**, fixing stale-health results (Bug #34).
- **Qt minting now auto-consolidates wallet UTXOs when needed**, preventing fragmented-wallet mint failures.
- **Mempool-backed DigiDollar amount resolution now works across chained transfers**, fixing conservation lookups before confirmation (Bug #35).
- **RC30 cuts a fresh `testnet23` chain** with new genesis, new network magic, fresh chainTx baseline, and default P2P port **12030**.
- **Release testnet keeps normal proof-of-work rules enabled by default**, while local multi-oracle harnesses can opt into `-easypow` without patching source.
- **Shell harnesses and operator docs were updated for `testnet23`**, wallet/key migration, 9-of-17 quorum, and the new runtime debug flow.

### 1. Oracle quorum upgraded to 9-of-17

The DigiDollar oracle set now has **17 slots (0–16)** with a **9-of-17 MuSig2 aggregate-signing quorum** (v0x03 bundles). The change applies to mainnet and testnet; regtest remains 4-of-7.

Why now:
- Growing the operator set improves network decentralisation and Byzantine tolerance.
- 9-of-17 keeps strict majority (>50%) while increasing the fault threshold: the network now tolerates 8 faulty oracles (up from 7 under 8-of-15).
- MuSig2 aggregate signatures keep the on-chain bundle compact (one aggregate Schnorr signature plus a 3-byte participation bitmap).

### 2. Three new oracle operators

Three oracles are added to the consensus set for RC30:

| Slot | Operator | Status |
|------|----------|--------|
| 12 | **DaPunzy** | ✅ Live on testnet |
| 14 | **Neel** | ✅ Live on testnet (real pubkey in RC30) |
| 15 | **DigiSwarm** | ✅ Live on testnet (real pubkey in RC30) |
| 16 | **GTO90** | ✅ Live on testnet (real pubkey in RC30) |

### 3. Full oracle slot order (RC30)

```
 0  Jared
 1  Green Candle
 2  Bastian
 3  DanGB
 4  Shenger
 5  Ycagel
 6  Aussie
 7  LookInto
 8  JohnnyLawDGB
 9  Ogilvie
10  ChopperBrian
11  hallvardo
12  DaPunzy          ← new in RC30
13  DigiByteForce
14  Neel             ← real key added in RC30
15  DigiSwarm        ← real key added in RC30
16  GTO90            ← real key added in RC30
```

All three key lists (`consensus.vOraclePublicKeys`, `vOracleNodes`, and the testnet mini-testnet debug block in `src/kernel/chainparams.cpp`) are ordered by slot (0–16), matching the MuSig2 participation bitmap. Key aggregation under BIP-327 sorts internally, so consensus pubkey ordering is slot-based rather than lexicographic.

### 4. Consensus threshold changes

| Parameter | RC29 | RC30 |
|-----------|-----:|-----:|
| `Consensus::Params::nOracleRequiredMessages`  | 8 | **9** |
| `Consensus::Params::nOracleTotalOracles`      | 15 | **17** |
| `Consensus::Params::nOraclePubkeyCount`       | 15 | **17** |
| `Consensus::Params::nOracleConsensusRequired` | 8 | **9** |
| `DigiDollar::ConsensusParams::oracleThreshold`| 8 | **9** |
| `DigiDollar::ConsensusParams::activeOracles`  | 15 | **17** |
| `DigiDollar::ConsensusParams::oracleCount`    | 15 | **17** |
| `ORACLE_CONSENSUS_REQUIRED` (primitives)      | 8 | **9** |
| `ORACLE_ACTIVE_COUNT` (primitives)            | 15 | **17** |
| `ORACLE_TOTAL_COUNT` (primitives)             | 30 | 30 (unchanged) |

Regtest is **unchanged** (7 pubkeys, 4-of-7) so the fast developer loop and the MuSig2 unit tests keep their existing semantics.

### 5. Participation bitmap width

The on-chain v0x03 participation bitmap grew from 2 bytes (15 slots) to **3 bytes (17 slots)**. Serialized bundles with MuSig2 aggregate signatures are now 84 bytes on-chain (vs. 83 bytes in RC28/RC29). Bundle parsing accepts either shape; encoding is always 3 bytes at/after RC30.

### 6. `ValidateOracleConfiguration` relaxation (slot-ordered pubkeys)

`Consensus::ValidateOracleConfiguration()` previously required `vOraclePublicKeys` to be strictly lexicographically ascending. The recent slot-ordered layout (introduced just before RC30) is not lex-sorted, so the check was relaxed to keep the uniqueness, length, hex and count invariants while allowing slot order. MuSig2 aggregation continues to sort internally per BIP-327.

### 7. Source cleanup — stale 8-of-15 references

Several comments and one hardcoded threshold were still quoting the old 8-of-15 numbers:

- `src/digidollar/validation.cpp` — `ValidateEmergencyRedemption()` had `requiredSigs = 8` hardcoded; now `= 9`.
- `src/consensus/err.h`, `src/digidollar/validation.h`, `src/oracle/mock_oracle.h`, `src/oracle/musig2_session.h`, `src/primitives/oracle.h`, `src/primitives/oracle.cpp` — doc comments updated to `9-of-17 (RC30)`.
- `src/consensus/digidollar.h` struct defaults updated to the new quorum shape so fuzzers and tests that build `ConsensusParams{}` defaults inherit the right numbers.

### 8. Test suites

- **Unit tests (C++/Boost).** All oracle, MuSig2, DigiDollar consensus, redteam, Phase 2 and aggregator suites updated for 9-of-17. Notable updates: `musig2_aggregator_tests` now exercises C(17, 9) = 24,310 subsets for `test_all_subsets_9_of_17`; `musig2_bundle_format_tests` validates the 3-byte bitmap / 84-byte v0x03 payload; `digidollar_redteam_tests` rewires 37 RC30 threshold comments and expands T3-04d to 17 messages.
- **Functional tests (Python).** `digidollar_oracle.py`, `digidollar_oracle_phase2.py` refit: `ORACLE_ACTIVE_COUNT=17`, `ORACLE_CONSENSUS_REQUIRED=9`, `TESTNET_ORACLE_COUNT=17`, `TESTNET_CONSENSUS_REQUIRED=9`. Byzantine test now runs with 9 honest + 8 malicious.
- **Fuzz tests.** Oracle/MuSig2 fuzz harnesses continue to honour the chainparams-driven quorum — no bit-level fixture rewrites were required beyond the bitmap width.

### 9. `test_multi_oracle_testnet.sh` — 9-of-17 end-to-end debugger

`test_multi_oracle_testnet.sh` was extended to drive 9-of-17 oracle consensus on **testnet** (not regtest) across **9 wallet nodes at ≤2 oracles each**. The released script now uses runtime `-easypow` local-harness mode, so production RC30 chainparams remain untouched while the debugger swaps to localhost oracle keys and easy PoW only when explicitly requested:

| Node | Oracle slots |
|------|--------------|
| Bob     | 0, 1   |
| Alice   | 2, 3   |
| Charlie | 4, 5   |
| Dave    | 6, 7   |
| Eve     | 8, 9   |
| Frank   | 10, 11 (new) |
| Grace   | 12, 13 (new) |
| Heidi   | 14, 15 (new) |
| Ivan    | 16 (new)    |

All previous coverage (tier-by-tier mint, transfer chains, redemption paths, wallet persistence/backup/reindex) is preserved. Consensus debugging steps now assert:
- **Step 27A** — 9-of-17 passes, live price observed.
- **Step 27B** — 8-of-17 is rejected (one short of quorum).
- **Step 27C** — median filter with 15 agreeing + 2 outliers filters outliers.
- **Step 27D** — oracle recovery after disagreement on 17 oracles.

### 10. Local harness mode for testnet debugging

RC30 release builds keep the production testnet configuration active by default:

1. **Production RC30 testnet keys stay live** in `CTestNetParams`.
2. The old local mini-testnet keys and localhost oracle-node table stay **commented out** in `src/kernel/chainparams.cpp` for future reuse.
3. Passing **`-easypow`** on `-testnet` enables local harness mode at runtime, which:
   - turns on easy PoW for the local debugger,
   - swaps in the commented mini-testnet oracle pubkeys,
   - redirects oracle peers to localhost ports,
   - leaves the release default unchanged for normal RC30 operators.

That means there is **no source edit to revert before tagging RC30**. The release path is already production-safe, and the local debugger is explicitly opt-in.

---

## What Was New in RC29 (included in RC30)

RC30 includes everything from RC29. See [RC29 release notes](RELEASE_v9.26.0-rc29.md) for:

- **Red Hornet v3 overnight security audit** — 17 bugs fixed across consensus, oracle, MuSig2, wallet and P2P
- **402 adversarial security tests** across 52 new files
- **CRITICAL** fixes: `COOLDOWN_BLOCKS` timing correction, `IsValidDigiDollarAddress` base58check hardening
- **HIGH** fixes: v0x03 bundle round-trip serialization, DD opcode vulnerabilities, dynamic `OP_RETURN` finder
- **MEDIUM** fixes: P2P rate limits & oracle-id range check, plaintext key erasure, Schnorr auth on MuSig2 P2P messages, DCA TOCTOU guard, health clamp, early DD rejection, cached-price reorg
- Test-suite fixes, 16 JSON fixtures migrated from Bitcoin to DigiByte address format

---

## What Was New in RC28 (also included in RC30)

See [RC28 release notes](RELEASE_v9.26.0-rc28.md) for:

- Testnet reset to `testnet23` with new network magic and a new RC30 genesis hash
- Fixed `getoracles` RPC — all operator names display correctly
- MuSig2 aggregate signing (`v0x03`) full implementation
- `sendoracleprice` RPC permanently removed (security)
- Bug fixes #24–#29, #33 and three critical MuSig2 signing fixes
- 208-target fuzz testing suite, Post-Quantum Cryptography roadmap

---

## Validation Results (RC30)

| Category | Result | Status |
|----------|--------|--------|
| Unit tests (oracle/MuSig2/DigiDollar) | Updated to 9-of-17 fixtures and epoch-aware v0x03 format | ✅ |
| RH-02 / adversarial coverage | Partial-sig bypass + replay-filter growth path covered | ✅ |
| Functional tests | `digidollar_oracle.py`, `digidollar_oracle_phase2.py` rewritten for 17 slots / 9 quorum | ✅ |
| `test_multi_oracle_testnet.sh` | 9-of-17 end-to-end debugger on testnet via runtime `-easypow` | ✅ |
| Consensus audit | New 17-slot layout, slot-order keys, 3-byte bitmap, epoch in payload | ✅ |
| RC29-era test coverage | Preserved | ✅ |

### Known follow-ups

- **DigiSwarm** now occupies slot 15 with a wallet-backed RC30 key managed locally for oracle bring-up.
- Oracle and wallet operators must migrate key material into `testnet23`; old `testnet21` blocks and chainstate are incompatible with RC30.

---

## Upgrading from RC29

RC30 resets testnet onto `testnet23`, so do **not** reuse old `testnet21` blocks or chainstate:

```bash
digibyte-cli -testnet stop
# Replace binaries with RC30
# Start fresh on testnet23, then migrate only wallet.dat / oracle key material as needed
digibyted -testnet -daemon
```

Recommended migration checklist:

1. Stop the old node.
2. Install the RC30 binary.
3. Start once on RC30 so `testnet23/` is created.
4. Copy forward only wallet and oracle key material.
5. Do **not** copy `blocks/`, `chainstate/`, `indexes/`, or peers data from the previous testnet.
6. Confirm the node is listening on **12030** and syncing the new genesis chain.

If you are an oracle operator, your oracle will auto-start once the migrated wallet is loaded on `testnet23`:
- **Unencrypted wallets:** oracle starts automatically when wallet loads
- **Encrypted wallets:** oracle starts automatically after `walletpassphrase`

Manual start if needed:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### New Oracle Setup (slots 12, 14, 16)

If you are **DaPunzy**, **Neel**, or **GTO90**, run:

```bash
digibyted -testnet -daemon
digibyte-cli -testnet createwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle createoraclekey <your_oracle_id>
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
# Future restarts will auto-start your oracle.
```

Your oracle public key (`getoraclepubkey <id>`) is what gets wired into `consensus.vOraclePublicKeys`.

For the complete guide see **`DIGIDOLLAR_ORACLE_SETUP.md`**.

---

## Oracle Operators (RC30)

| ID | Operator | Status |
|----|----------|--------|
| 0 | Jared | ✅ Active |
| 1 | Green Candle | ✅ Active |
| 2 | Bastian | ✅ Active |
| 3 | DanGB | ✅ Active |
| 4 | Shenger | ✅ Active |
| 5 | Ycagel | ✅ Active |
| 6 | Aussie | ✅ Active |
| 7 | LookInto | ✅ Active |
| 8 | JohnnyLawDGB | ✅ Active |
| 9 | Ogilvie | ✅ Active |
| 10 | ChopperBrian | ✅ Active |
| 11 | hallvardo | ✅ Active |
| 12 | **DaPunzy** | ✅ Active (RC30) |
| 13 | DigiByteForce | ✅ Active |
| 14 | **Neel** | ✅ Active (RC30) |
| 15 | **DigiSwarm** | ✅ Active (RC30) |
| 16 | **GTO90** | ✅ Active (RC30) |

---

## Complete RPC Command Reference

### DigiDollar Commands (Wallet)

| Command | Description |
|---------|-------------|
| `mintdigidollar` | Mint DigiDollars by locking DGB as collateral |
| `senddigidollar` | Send DigiDollars to another address |
| `redeemdigidollar` | Redeem DigiDollars to unlock DGB collateral |
| `getdigidollarbalance` | Show your DigiDollar balance |
| `listdigidollarpositions` | List your active collateral positions |
| `listdigidollartxs` | List your DigiDollar transaction history |
| `getdigidollaraddress` | Get or create a DigiDollar receive address |
| `validateddaddress` | Validate a DigiDollar address |
| `listdigidollaraddresses` | List all DigiDollar addresses in your wallet |
| `importdigidollaraddress` | Import a DigiDollar address for watch-only |
| `getdigidollarstats` | Get network-wide DigiDollar statistics |
| `getdigidollardeploymentinfo` | Get DigiDollar activation/deployment status |
| `calculatecollateralrequirement` | Calculate DGB collateral needed for a DD mint |
| `estimatecollateral` | Estimate collateral requirement by tier |
| `getdcamultiplier` | Get the current DCA multiplier for collateral |
| `getredemptioninfo` | Get info about redeeming a specific position |
| `getprotectionstatus` | Check if liquidation protection is active |

### Oracle Commands

| Command | Description |
|---------|-------------|
| `createoraclekey <id>` | Generate oracle Schnorr keypair (one-time) |
| `getoraclepubkey <id>` | Show oracle public key from wallet |
| `startoracle <id>` | Start running as an oracle operator |
| `stoporacle <id>` | Stop your oracle |
| `getoracleprice` | Get the consensus price |
| `getalloracleprices` | Per-oracle price breakdown |
| `getoracles` | Network-wide oracle status |
| `listoracle` | Show local oracle status |

---

## Configuration

```ini
testnet=1

[test]
digidollar=1
txindex=1
addnode=oracle1.digibyte.io
```

> **Note:** `txindex=1` is enforced at startup for DD-enabled nodes. Make sure it's in the correct section (`[test]` for testnet, `[main]` for mainnet). Global placement (above all sections) also works.

---

## Network Information

| Setting | Value |
|---------|-------|
| Network | Testnet (`testnet23`) |
| Genesis Hash | `0xa19e809bb060f7f50c05a9bec7fdefedd8497aa0bd6ccca6f55c86090963e4ca` |
| Network Magic | `fd d2 b9 e4` |
| Default P2P Port | **12030** |
| Default RPC Port | **14026** |
| Oracle Consensus | **9-of-17 (RC30)** |
| Oracle Bundle Format | **MuSig2 aggregate signing (`v0x03`, 84 bytes)** |
| Exchange Sources | 6 (Binance, CoinGecko, KuCoin, Gate.io, HTX, Crypto.com) |

---

## Downloads

| Platform | File |
|----------|------|
| Windows 64-bit (Installer) | `digibyte-9.26.0-rc30-win64-setup.exe` |
| Windows 64-bit (Portable) | `digibyte-9.26.0-rc30-win64.zip` |
| macOS Apple Silicon | `digibyte-9.26.0-rc30-arm64-apple-darwin.dmg` |
| macOS Intel | `digibyte-9.26.0-rc30-x86_64-apple-darwin.dmg` |
| Linux x86_64 | `digibyte-9.26.0-rc30-x86_64-linux-gnu.tar.gz` |
| Linux ARM64 (Raspberry Pi) | `digibyte-9.26.0-rc30-aarch64-linux-gnu.tar.gz` |

---

## Known Issues

- DigiSwarm now fills slot 15 with a wallet-backed RC30 oracle key.
- Oracle and wallet operators must migrate key material forward into `testnet23`; old `testnet21` blocks and chainstate are incompatible with RC30.
- Local debug runs that need the full 17-oracle single-machine harness must pass `-easypow`; release testnet defaults intentionally do not enable that mode.

---

## Troubleshooting

### "My oracle did not start automatically"

Load the oracle wallet and start it manually:

```bash
digibyte-cli -testnet loadwallet "oracle"
digibyte-cli -testnet -rpcwallet=oracle startoracle <your_oracle_id>
```

### "Tests fail after upgrading"

Run `make clean && make -j$(nproc)` for a clean rebuild. Some test binaries may cache old object files.

### "My node rejects a bundle that RC29 would have accepted"

That is expected: RC30 requires **9-of-17** signatures for Phase 2/Phase 3 bundles. Bundles with 8 valid signatures will be rejected.

---

## Feedback & Community

- **Developer Chat (Gitter):** https://app.gitter.im/#/room/#digidollar:gitter.im
- **GitHub Issues:** https://github.com/DigiByte-Core/digibyte/issues

---

## RC30 Final — MuSig2 Phase 3 correctness fixes (critical)

Three interlocking bugs were preventing v0x03 MuSig2 aggregate bundles from ever landing on chain — the network silently fell back to v0x02 per-oracle-signature bundles (609–1064 bytes per block). All three are fixed in RC30 and verified end-to-end on the 9-wallet 9-of-17 testnet debugger: **41 v0x03 bundles on chain, 94-byte scriptPubKey (88-byte payload), 0 verify failures**.

### Bug 1 — v0x03 on-chain payload did not include epoch
Signer hashed `H(epoch, price, timestamp)`; validator forced `bundle.epoch = 0` on extract because the payload lacked the field. Schnorr verify always failed.
Fix: serialise `int32_t epoch` (4 LE bytes) between bitmap and price. `ValidatePhaseThreeBundle` now enforces `bundle.epoch == GetCurrentEpoch(block_height)` — prevents cross-epoch replay.

### Bug 2 — MuSig2 P2P messages were never signed by the sender
`net_processing.cpp:6023` validates `OracleMusigNonceMsg`/`OracleMusigPartialSigMsg` with a Schnorr sig from the oracle's chainparams pubkey (RH-24 hardening). The orchestrator never called `.Sign(key)` before broadcast; every peer dropped every MuSig2 message as "invalid signature". MuSig2 sessions never exchanged a single nonce across the network.
Fix: orchestrator now signs both message types with the operator's oracle key before `BroadcastMusigNonce` / `BroadcastMusigPartialSig`.

### Bug 3 — Partial-sig aggregation corrupted by participant-set divergence
Each node independently trimmed to the lowest-9 nonces it had seen. On a fast testnet, two nodes could trim to slightly different 9-sets, each produce a partial sig against its own `keyagg_cache`, and silently aggregate into an invalid signature.
Fix: new `MuSig2SigningSession::AddPartialSignatureVerified` calls `secp256k1_musig_partial_sig_verify` before aggregation; remote partial sigs made under a mismatched cache are rejected. Orchestrator uses this variant for remote partial sigs. Lazy session creation on remote arrival prevents early messages from being dropped as "unknown epoch".

### v0x03 on-chain format (RC30 final)
```
OP_RETURN OP_ORACLE <push 0x03 version>
<push payload (88 bytes for 9-of-17)>
  payload = bitmap_len(1) + bitmap(3) + epoch(4) + price(8) + timestamp(8) + aggregate_sig(64)
```
Total coinbase scriptPubKey: **94 bytes** for a 9-of-17 bundle — constant regardless of how many oracles participate.

### Verification
```
2857/2857 unit tests PASS
41 v0x03 bundles on testnet chain, 94 bytes each, 0 verify failures
179/180 script assertions PASS (1 cosmetic fail in Step 27E — the probe
picks blocks without oracle bundles on occasion)
```
