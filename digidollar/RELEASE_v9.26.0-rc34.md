# DigiByte Core v9.26.0-rc34 Release Notes

**WARNING: RC34 is testnet-only. Do not use it on mainnet.**

RC34 is the DigiDollar V1 hardening release candidate. It is not a flashy feature drop. It is the cleanup pass that turns test failures and edge cases into real fixes before the final public proving run.

The goal is simple: make DigiDollar behave the same way after mining, restart, rescan, reindex, wallet restore, oracle churn, and multi-node testnet use.

Development branch: `feature/digidollar-v1`

Developer chat: https://app.gitter.im/#/room/#digidollar:gitter.im

---

## Read This First

RC34 starts a fresh DigiDollar testnet: `testnet24`.

RC33 used `testnet23`. RC34 does not. Old `testnet23` blocks, chainstate, and DigiDollar positions are not RC34 proof. Back up wallets and oracle keys, then start clean on `testnet24`.

Why the reset matters:

- The RC34 code changed enough consensus, oracle, wallet, and harness behavior that the final proving network needs a clean start.
- A new genesis makes it obvious which nodes are actually testing RC34.
- It avoids old testnet state hiding new bugs.

What did not change:

- RC34 is still a testnet-only release candidate. Do not use it as a mainnet deployment.
- The V1 oracle model is still 9-of-17 MuSig2.
- Production-style testing still uses live exchange prices. No production mock price path was added.
- Jared still reviews, tags, releases, and deploys. Nothing in this notes update was pushed externally.

---

## Testnet24 Network Details

| Item | RC34 value |
| --- | --- |
| Testnet name | `testnet24` |
| Data directory | `testnet24` |
| Genesis hash | `0xe42636c490059fafe7e0278acc6fb451b901b6a316b31e10d7ccff565baf23df` |
| Merkle root | `0x502bf477644933ced36281bbfdcc6755895b3d9f75262eb148d2c1c2c21d7e73` |
| Genesis time | `2026-05-11 13:53:00 UTC` |
| Genesis nonce | `57535` |
| Network magic | `fe c4 b7 e5` |
| Default P2P port | `12031` |
| Default RPC port | `14026` |
| DigiDollar activation height | `600` |
| Oracle activation height | `600` |
| Oracle epoch length | `40` blocks |
| Oracle quorum | `9-of-17` |
| Oracle bundle format | `v0x03` MuSig2 aggregate bundle |

Code check:

- `src/chainparamsbase.cpp` selects `testnet24`.
- `src/kernel/chainparams.cpp` contains the genesis, magic bytes, port, activation height, oracle epoch, and quorum values above.

---

## RC34 In Plain English

RC34 fixes places where DigiDollar could get confused, trust the wrong thing, or tell the user the wrong story.

The important theme is this:

- Consensus should not depend on a local wallet cache.
- Mining should not depend on a stale local price.
- Oracle messages should not work on the wrong chain.
- Wallet RPCs should not say coins are spendable when they are not.
- Restart, rescan, reindex, and restore should not change the truth.
- Tests should fail loudly when the ecosystem is broken.

That is what the RC33-to-RC34 changes are about.

---

## Architecture Record

This is the part future reviewers should not have to reconstruct from commit archaeology.

RC34 found bugs that were not just "one bad if statement." Some failures showed that the system needed a sharper rule about where truth comes from: the block, the oracle bundle, the wallet database, the active chain, or the live oracle network. Those rules are recorded here.

This is not a new DigiDollar design. It is the V1 design made stricter where release testing found weak edges.

### Fresh public testnet

RC34 moves DigiDollar proving from `testnet23` to `testnet24`.

That changes the testnet data directory, genesis block, network magic, and P2P port. This is intentional, not cosmetic.

The bug class this avoids is stale proof. Old `testnet23` chainstate could make an RC34 node look healthier than it really is because the node would be carrying old activation history, old oracle history, and old wallet/index state. A fresh public chain forces the final proving run to stand on the RC34 code itself.

### Activation boundary is tighter

RC34 treats activation as a hard boundary.

Before activation, DigiDollar-looking data is just ordinary pre-activation data. After activation, DD-touching transactions and blocks must satisfy DigiDollar V1 rules. The testnet activation height, oracle activation height, and BIP9 minimum activation height are aligned at block `600`.

The bug class this closes is accidental supply and split validation. A pre-activation output must not wake up later as real DigiDollar value, and two upgraded nodes must not disagree about whether DD rules are active at the same height.

### Block oracle data is the consensus source

RC34 makes the block's v0x03 MuSig2 oracle bundle the price source for DD-touching block validation.

That means validators do not trust a local cache, a wallet view, or a mock price when validating a DD block. If a DD block needs a price, the block must carry a valid oracle bundle.

The bug class this closes is miner/validator price drift. A miner may have one local price cache and a validator may have another. The only price every node can agree on is the one committed into the block and proven by the oracle quorum.

### Production oracle path stays live and fail-closed

RC34 does not add a production fallback price path.

Live exchange data feeds the oracle system. MuSig2 oracles sign the consensus price. DigiDollar validation consumes the validated bundle. If the live oracle path cannot produce a valid bundle, price-dependent DD behavior stops instead of guessing.

The bug class this closes is fake safety. A fallback price can make tests look green while the real oracle network is broken. RC34 keeps that failure visible. If the live path is down, mint/redeem behavior that needs a price should fail closed.

### MuSig2 oracle messages are chain-bound

RC34 binds oracle bundle hashes and MuSig2 P2P authentication to the chain genesis hash.

This means a message signed for one network is not valid on another network with the same-looking payload. Mainnet, testnet24, regtest, and local mini-testnets now have clean oracle-message separation.

The bug class this closes is replay across networks. Oracle signatures are powerful. They must only mean something on the chain they were signed for.

### Lock tiers are canonical V1 rules

RC34 keeps V1 lock tiers strict.

A mint must use one of the canonical lock tiers. The tier byte, lock height, and collateral ratio must agree. Arbitrary custom lock periods are not accepted as V1 mints.

The bug class this closes is policy/consensus drift. Wallet code, RPC code, miner code, and validators all need to talk about the same small set of lock choices. Custom periods create too many places to disagree about collateral, unlock height, and user expectations.

### Redemption and ERR are full-vault rules

RC34 keeps redemption exact and full-vault.

Normal redemption burns the matching DigiDollar amount and releases the full collateral for that vault. Partial burns cannot release collateral. During ERR, the user still receives full collateral, but must burn more DD according to the emergency ratio.

The bug class this closes is collateral leakage. A vault should have a clean lifecycle: mint, optionally transfer the DD, then burn the required DD to release the vault. Partial-redemption accounting would add a second, harder-to-audit balance system inside the vault.

### Wallet security follows wallet locking

RC34 treats oracle signing keys as wallet-protected key material.

Encrypted wallets encrypt wallet-managed oracle keys. Locked encrypted wallets do not hand those keys back to RPC or oracle startup paths. This is a security behavior change, but it does not weaken consensus or change the oracle roster.

The bug class this closes is protected-wallet bypass. If a wallet is locked, the user is saying private signing material should not be usable. Oracle keys managed by that wallet follow the same rule.

### Local mini-testnet mode is explicit

The live multi-oracle script can use deterministic local oracle keys only through local `-easypow` mini-testnet mode.

Without that local mode, the configured public testnet oracle keys remain the default. This keeps test convenience separate from production-style testnet behavior.

The bug class this closes is test harness leakage. Deterministic local keys are useful for proving the ecosystem on one machine. They should not silently replace the public testnet oracle roster.

### What did not change

- The V1 quorum remains 9-of-17.
- The v0x03 bundle shape remains the production DigiDollar V1 oracle format.
- No production mock price path was added.
- DigiDollar economic constants were not rewritten to get tests green.
- Mainnet deployment still waits for Jared's review, tag, release, and deploy decision.

---

## Issues Fixed And Why They Matter

### 1. Release Metadata And Docs

The issue:

The release metadata moved to RC34, but the release notes still described an old same-testnet plan. That was dangerous because operators could read the notes and keep using the wrong chain.

The fix:

RC34 now documents the actual `testnet24` reset, the cleaned commit stack, the validation evidence, and the major hardening work in plain language.

Details:

- Version metadata is set to RC34 in the build/version files.
- The old RC34 note that said "same testnet" was replaced because the code now starts `testnet24`.
- Repo maps and DigiDollar/oracle docs were refreshed so reviewers can find the current consensus, oracle, wallet, RPC, Qt, test, and fuzz code paths.
- The notes now list the cleaned commits since RC33 and explain what each subsystem bucket is meant to prove.
- Validation logs are listed with the important limitation: the recorded final logs predate the final public `testnet24` reset commit, so the tag candidate still needs a fresh final gate run.

Why it matters:

Bad release notes create bad deployments. Operators need one clear answer: RC34 is `testnet24`, not `testnet23`.

Code/docs touched:

- `configure.ac`
- `src/clientversion.cpp`
- `src/clientversion.h`
- `RELEASE_v9.26.0-rc34.md`
- `ARCHITECTURE.md`
- `REPO_MAP.md`
- `REPO_MAP_DIGIDOLLAR.md`
- DigiDollar and oracle docs

### 2. DigiDollar Activation Rules

The issue:

Pre-activation DigiDollar-looking data must not become real DigiDollar supply later. If old or ordinary outputs can be reinterpreted after activation, upgraded and non-upgraded nodes can disagree.

The fix:

RC34 tightens activation gates in block validation, mempool validation, and DigiDollar validation. Pre-activation DigiDollar-looking outputs stay inert. Post-activation DigiDollar transactions must pass the real V1 rules.

Details:

- Testnet DigiDollar BIP9 activation and the static DigiDollar/oracle activation heights are aligned at block `600`.
- Pre-activation OP_ORACLE-looking and DigiDollar-looking data is ignored as ordinary DGB data instead of changing base-chain validity.
- Post-activation DD-touching blocks must carry valid v0x03 MuSig2 oracle data.
- Mempool/RPC paths reject DigiDollar actions before activation and accept them only after the deployment is active.
- Reorgs below activation clear stale DigiDollar mempool state instead of leaving post-activation DD transactions live on a pre-activation tip.
- IBD and reindex tests pin that a node syncing or rebuilding from disk lands on the same activation height and deployment state.
- Pre-activation DD-looking UTXOs cannot later be spent or redeemed as real DigiDollar supply.

Why it matters:

Activation must be boring. At height 600 on testnet24, every upgraded node should agree exactly when DigiDollar rules start.

Code/tests touched:

- `src/validation.cpp`
- `src/digidollar/validation.cpp`
- `src/kernel/chainparams.cpp`
- `src/test/digidollar_activation_wave12_tests.cpp`
- `test/functional/digidollar_activation_boundary.py`
- `test/functional/digidollar_activation_multinode.py`

### 3. Mint, Collateral, And Lock-Tier Safety

The issue:

Minting is where DigiDollar starts. If lock tiers, collateral math, or mint metadata are loose, bad positions can enter the system.

The fix:

RC34 hardens canonical lock-tier checks, collateral-ratio math, DCA calculations, amount bounds, and mint parameter validation. Overflow-style cases now fail closed instead of sneaking through with a bad number.

Details:

- The validator enforces the ten canonical lock tiers and rejects non-canonical custom durations.
- The OP_RETURN lock-tier byte must be in range and must match the actual committed lock height.
- Mint validation uses a bounded confirmation window, so valid mints remain mineable without letting arbitrary lock periods through.
- Collateral math uses wider integer arithmetic before narrowing back to money amounts.
- Required collateral above `MAX_MONEY` fails closed instead of being capped into an apparently valid amount.
- DCA applies basis-point math, rounds up where needed, and refuses stale health data when canonical health is available.
- The wallet txbuilder and consensus validator are pinned to the same canonical tier and collateral behavior so wallet-built mints match node validation.

Why it matters:

A mint should either be clearly valid or clearly rejected. There should not be a gray area where one path accepts a mint and another path rejects it.

Code/tests touched:

- `src/consensus/dca.cpp`
- `src/consensus/digidollar.cpp`
- `src/consensus/digidollar.h`
- `src/digidollar/txbuilder.cpp`
- `src/digidollar/validation.cpp`
- `src/test/digidollar_locktier_tests.cpp`
- `src/test/digidollar_health_dca_tests.cpp`
- `src/test/digidollar_wave6_health_dca_volatility_tests.cpp`
- `src/test/fuzz/digidollar_lock_tier.cpp`
- `src/test/fuzz/digidollar_dca_volatility.cpp`

### 4. Transfer, Redeem, Burn, And Collateral-Spend Rules

The issue:

DigiDollar transfers and redemptions must prove exactly what DigiDollar amount is being spent and what collateral is being released. Weak paths here can lead to fake burns, wrong collateral release, or vault spends that do not match DigiDollar state.

The fix:

RC34 makes transfer and redemption validation use authoritative DigiDollar input amounts. It tightens burn enforcement, collateral-vault spend detection, redemption accounting, timelock checks, and emergency-redemption behavior.

Details:

- Transfer and redeem paths derive DigiDollar input amounts from the coins being spent, not from a loose local guess.
- Pre-activation DD-looking inputs are rejected as DigiDollar inputs.
- Ordinary DGB spends of a DigiDollar collateral vault are rejected if they do not include the required DigiDollar burn.
- DD-marked non-redemption transactions cannot spend collateral vaults as if they were normal positive-value inputs.
- Unconfirmed mint collateral children are checked against the mint parent so mandatory burn validation is not skipped.
- Normal redemption is exact/full-vault: partial burn cannot release collateral.
- Redemption fee inputs cannot hide an under-burn or reduce the required collateral return.
- Multiple DD change outputs in a redemption are rejected because redemption metadata has one authoritative change amount.
- The mint timelock is enforced for both normal redemption and ERR redemption.
- ERR returns full collateral but requires extra DD burn based on the emergency ratio; the burn math is rounded conservatively.

Why it matters:

If a vault is released, the matching DigiDollar burn must be real. If a DigiDollar amount moves, every node needs to agree on the amount.

Code/tests touched:

- `src/consensus/err.cpp`
- `src/consensus/err.h`
- `src/consensus/digidollar.h`
- `src/digidollar/validation.cpp`
- `src/index/digidollarstatsindex.cpp`
- `src/test/digidollar_burn_enforcement_tests.cpp`
- `src/test/digidollar_err_tests.cpp`
- `src/test/digidollar_rh07_redemption_attacks_tests.cpp`
- `test/functional/digidollar_collateral_spend_guards.py`
- `test/functional/digidollar_redeem.py`
- `test/functional/digidollar_redeem_stats.py`

### 5. Block Validation, Mining, And Oracle Price Truth

The issue:

A node's local price cache is not consensus. If a miner builds a block using one local price but validators use another price, the network gets a split or a bad block.

The fix:

RC34 makes DD-touching blocks use the oracle bundle committed in the block. The miner skips or strips price-dependent DigiDollar transactions when it cannot include usable MuSig2 oracle data. `OP_CHECKPRICE` fails closed when there is no valid consensus price.

Details:

- Mempool admission requires a recent valid MuSig2 quote for price-dependent DigiDollar transactions.
- The miner mirrors the mempool freshness check so it does not include a DD transaction that mempool policy already refused.
- The miner looks at the live signing-session bundle that `AddOracleBundleToBlock()` can actually stamp into the block.
- If no valid bundle is ready, the miner skips the DigiDollar transaction and can keep mining non-DD transactions.
- If a DD transaction already made it into a candidate block but the oracle bundle cannot be stamped, the miner strips the DD transaction and rebuilds the coinbase, witness commitment, and merkle root.
- ConnectBlock validates DD-touching blocks against the v0x03 bundle in the coinbase.
- Missing, malformed, stale, legacy, or bad-signature oracle data produces explicit `bad-oracle-*` reject reasons instead of falling back to a local cache.
- `OP_CHECKPRICE` has no hardcoded fallback price. If the oracle hook has no valid consensus price, the opcode fails closed.

Why it matters:

The block must carry the price proof. Validators should not have to guess what the miner saw locally.

Code/tests touched:

- `src/node/miner.cpp`
- `src/validation.cpp`
- `src/script/interpreter.cpp`
- `src/oracle/bundle_manager.cpp`
- `src/test/miner_dd_validation_tests.cpp`
- `src/test/digidollar_wave13_parity_tests.cpp`
- `test/functional/digidollar_mempool_miner_parity.py`
- `test/functional/digidollar_oracle_block_rules_relay.py`

### 6. Live Oracle Price Path

The issue:

Production-style DigiDollar testing must use live exchange data. Mock prices and backup shortcuts are useful in isolated tests, but they must not become a production price escape hatch.

The fix:

RC34 removes obsolete backup exchange code, keeps the live exchange aggregator path, tightens exchange price parsing, and rejects malformed price strings instead of accepting partial junk.

Details:

- The stale backup exchange source file was removed.
- Production-style oracle behavior stays on the live exchange aggregator path.
- Exchange price parsing rejects malformed values and trailing junk instead of accepting a numeric prefix from a bad response.
- Local mock prices remain test-only tools for regtest/local harnesses, not production testnet fallback behavior.
- The oracle price path feeds MuSig2 consensus bundles; DigiDollar validation then consumes the block's validated bundle price.

Why it matters:

The oracle system is only meaningful if it proves live market data through the configured oracle set.

Code/tests touched:

- `src/oracle/exchange.cpp`
- `src/oracle/exchange.h`
- `src/oracle/exchange.cpp.backup` removed
- `src/test/digidollar_oracle_feed_safety_tests.cpp`
- `src/test/digidollar_oracle_feeds_wave11_tests.cpp`
- `src/test/oracle_exchange_tests.cpp`
- `src/test/fuzz/oracle_price_aggregator.cpp`

### 7. MuSig2 v0x03 Oracle Bundles

The issue:

DigiDollar V1 depends on final MuSig2 oracle bundles. Loose bundle parsing, stale bundle assumptions, or legacy bundle behavior can let bad price data look valid.

The fix:

RC34 hardens `v0x03` bundle creation, parsing, extraction, quorum checks, bitmap checks, epoch checks, timestamp checks, and aggregate signature validation. Deprecated non-MuSig2 `oraclebundle` relay is ignored for V1 production behavior.

Details:

- The v0x03 payload is exact: bitmap length, bitmap, epoch, price, timestamp, and a 64-byte aggregate Schnorr signature.
- Zero-length bitmaps, malformed bitmap sizes, truncated payloads, trailing junk, and wrong aggregate signature sizes are rejected.
- The bundle epoch must match the block's expected oracle epoch.
- The bundle timestamp must not be too old or too far in the future relative to the block.
- The signer bitmap is decoded against the active oracle roster, not reserve metadata.
- The number of signers must meet the configured threshold.
- The aggregate public key is recomputed from the bitmap-selected oracle keys.
- The aggregate signature is verified against the v0x03 message hash before the bundle is accepted.
- Legacy bundle versions are rejected for DD-touching V1 blocks.

Why it matters:

The bundle is the price receipt. It needs to be exact.

Code/tests touched:

- `src/primitives/oracle.cpp`
- `src/primitives/oracle.h`
- `src/oracle/bundle_manager.cpp`
- `src/oracle/bundle_manager.h`
- `src/oracle/musig2_oracle_participation.cpp`
- `src/net_processing.cpp`
- `src/test/musig2_bundle_creation_tests.cpp`
- `src/test/musig2_bundle_format_tests.cpp`
- `src/test/digidollar_oracle_bundle_matrix_tests.cpp`
- `src/test/fuzz/oracle_bundle_validation.cpp`
- `src/test/fuzz/oracle_bundle_version_reject.cpp`

### 8. Cross-Chain Replay Protection

The issue:

Without strong chain binding, an authenticated oracle message or bundle from one network could be replayed on another network that uses similar keys or formats.

The fix:

RC34 binds oracle bundle hashes and MuSig2 P2P authentication hashes to the chain identity through `hashGenesisBlock`.

Details:

- The v0x03 bundle hash includes a domain tag, the chain genesis hash, the epoch, the price, and the timestamp.
- The signer and validator use the same chain-bound bundle hash.
- MuSig2 nonce authentication uses its own message-type tag plus the chain genesis hash.
- MuSig2 partial-signature authentication uses a different message-type tag plus the chain genesis hash.
- Nonce auth signatures cannot be reused as partial-signature auth signatures, or the other way around.
- Fuzz coverage mutates chain hash, epoch, price, timestamp, and message type so replay and domain-mixing bugs are caught.

Why it matters:

Mainnet, testnet24, regtest, and local mini-testnets must not accept each other's oracle messages.

Code/tests touched:

- `src/oracle/bundle_manager.cpp`
- `src/protocol.cpp`
- `src/test/digidollar_oracle_domain_separation_tests.cpp`
- `src/test/rh15_crypto_primitives_tests.cpp`
- `src/test/fuzz/oracle_bundle_hash_domain_sep.cpp`
- `src/test/fuzz/oracle_musig2_auth_signature_domain.cpp`

### 9. MuSig2 Session And P2P Reliability

The issue:

The live oracle network has timing, restarts, stale epochs, duplicate messages, and partially complete sessions. That is where easy unit tests miss real bugs.

The fix:

RC34 hardens nonce handling, partial-signature verification, session transitions, stale epoch rejection, roster checks, relay bounds, pending-message cleanup, and P2P message authentication. It also fixes cases where remote nonces could be lost while a session moved into nonce collection.

Details:

- Remote nonces are preserved when a session transitions into nonce collection.
- Nonce and partial-signature messages are checked against the active oracle roster.
- Reserve/non-active oracle slots cannot satisfy pending-message quorum or MuSig2 relay admission.
- Stale epochs are dropped before relay or session ingestion.
- Partial signatures are verified before being accepted into the aggregate.
- Sessions aggregate once enough valid partial signatures are present.
- Completed MuSig2 sessions are the source for mining-ready v0x03 bundles.
- Seen-message and attestation caches are bounded so invalid oracle traffic cannot grow memory forever.
- Pending oracle state is cleared on restart paths so old sessions do not poison new epochs.

Why it matters:

The 9-of-17 oracle set must keep signing under real network behavior, not only in perfect lab timing.

Code/tests touched:

- `src/oracle/musig2_session.cpp`
- `src/oracle/signing_orchestrator.cpp`
- `src/oracle/musig2_aggregator.cpp`
- `src/oracle/node.cpp`
- `src/protocol.cpp`
- `src/net_processing.cpp`
- `src/test/digidollar_musig2_session_state_tests.cpp`
- `src/test/digidollar_wave20_p2p_pending_tests.cpp`
- `src/test/musig2_signing_orchestration_tests.cpp`
- `test/functional/digidollar_wave20_oracle_p2p.py`
- `src/test/fuzz/oracle_p2p_wire_messages.cpp`
- `src/test/fuzz/oracle_musig2_session_real.cpp`

### 10. Wallet Truth, RPC Truth, And Qt Safety

The issue:

Wallet state can lie if it is stale. A wallet can show an old position, claim something is spendable while locked, miss restored DigiDollar state after rescan, or let Qt refresh against a wallet model that is being destroyed.

The fix:

RC34 reconciles DigiDollar wallet state before list RPCs, tightens spendability and watch-only reporting, validates paging/filter inputs, restores DigiDollar state more reliably after restart/rescan/reindex/backup/restore, encrypts wallet-managed oracle keys, rejects invalid oracle slots, rejects whitespace-polluted DigiDollar addresses, and fixes Qt wallet lifetime and display states.

Details:

- `listdigidollarpositions` reconciles state before reporting positions.
- Balance-style RPCs separate confirmed, unconfirmed, total, spendable, watch-only, and redeemable state.
- Locked encrypted wallets no longer advertise DD as currently spendable/redeemable when the wallet cannot sign.
- `listdigidollaraddresses` hides empty addresses by default so fresh receive keys do not look like funded DD accounts.
- Position and transaction list RPCs validate paging and filter arguments.
- `createoraclekey`, `stoporacle`, and `getoraclepubkey` reject oracle IDs that are not valid for the configured roster.
- `validateddaddress` rejects whitespace anywhere in the input instead of silently decoding a trimmed variant.
- Wallet rescan restores mint positions, DD UTXOs, received DD, sends, redemptions, and transaction history where the wallet controls the keys.
- Reindex and restart paths rebuild active positions and DD balances instead of depending only on process-local maps.
- Descriptor export/import restore is tested for both Alice and Bob in the live script.
- Wallet-managed oracle keys are encrypted in encrypted wallets; plaintext `ORACLE_KEY` rows are removed during encryption migration.
- Locked encrypted wallets refuse to return oracle signing keys.
- Qt destroys wallet views before WalletModels so DigiDollar refresh timers do not dereference dead wallet pointers.
- Qt send displays only confirmed spendable DD as available balance.
- Qt uses a shared translator for DigiDollar/oracle reject reasons so users see meaningful errors.

Why it matters:

Users need the wallet to tell the truth. Operators need oracle keys protected by wallet locking and encryption. Qt should not keep touching a wallet after that wallet is gone.

Code/tests touched:

- `src/rpc/digidollar.cpp`
- `src/base58.cpp`
- `src/wallet/digidollarwallet.cpp`
- `src/wallet/wallet.cpp`
- `src/wallet/walletdb.cpp`
- `src/qt/digibyte.cpp`
- `src/qt/walletframe.cpp`
- `src/qt/digidollartab.cpp`
- `src/qt/digidollarsendwidget.cpp`
- `src/qt/digidollarpositionswidget.cpp`
- `src/qt/digidollar_qt_translate.h`
- `src/wallet/test/digidollar_wave16_persistence_tests.cpp`
- `src/wallet/test/digidollar_wave17_spendability_tests.cpp`
- `src/qt/test/digidollarwave19widgettests.cpp`
- `test/functional/wallet_digidollar_backup.py`
- `test/functional/wallet_digidollar_reindex.py`
- `test/functional/wallet_digidollar_persistence_restart.py`
- `test/functional/wallet_digidollar_wave16_load_rescan.py`

### 11. Chainstate And Storage Safety

The issue:

Release testing found hot paths that were reading too much historical block data and storage paths that needed to be safer around invalid blocks, disconnects, and reorgs.

The fix:

RC34 reduces repeated historical block reads during collateral-spend checks, removes noisy hot-path logging, protects cache updates around invalid blocks, and improves reorg/disconnect behavior for DigiDollar and oracle state.

Details:

- Collateral-spend checks avoid repeatedly rereading historical blocks in hot validation paths.
- Invalid block validation no longer leaves oracle/DigiDollar cache side effects behind.
- Oracle price cache updates happen on connected valid blocks and roll back on disconnect.
- Reorg tests cover both oracle price cache behavior and wallet position restoration.
- DigiDollar stats/index state is checked across reorg and reindex so rebuilt state matches the active chain.
- Reindex tests verify deployment state, DigiDollar stats, oracle price cache, balances, and positions after rebuild.

Why it matters:

The node should stay correct and usable when the chain rewinds, a block is rejected, or the wallet/index rebuilds state.

Code/tests touched:

- `src/validation.cpp`
- `src/digidollar/validation.cpp`
- `src/index/digidollarstatsindex.cpp`
- `src/oracle/bundle_manager.cpp`
- `test/functional/digidollar_oracle_reorg_cache.py`
- `test/functional/digidollar_verifychain_cache_side_effect.py`
- `test/functional/wallet_digidollar_reorg.py`

### 12. Amount Compression And Edge-Case Math

The issue:

Some amount-compression and integer edge cases were too easy to get wrong near large DigiByte values.

The fix:

RC34 preserves valid high DigiByte amounts up to `MAX_MONEY`, adds targeted compression tests, and expands fuzz coverage for integer and amount boundaries.

Details:

- Amount compression now round-trips high valid DGB values instead of losing information near the upper money range.
- Tests cover `MAX_MONEY` and near-`MAX_MONEY` compression cases.
- DigiDollar integer fuzzing covers collateral, DCA, txbuilder, validation, and boundary math.
- Oracle and DigiDollar fuzz reproducers caught edge cases before they became release failures.

Why it matters:

Money amounts are not a place for "close enough." Encoding and decoding must round-trip safely.

Code/tests touched:

- `src/compressor.cpp`
- `src/test/compress_tests.cpp`
- `src/test/fuzz/digidollar_integer_math.cpp`
- `src/test/fuzz/digidollar_txbuilder_validate.cpp`

### 13. Functional Harness And Multi-Oracle Testnet Script

The issue:

The live multi-oracle script is the best ecosystem test, but it exposed real reliability problems: restart/reindex nodes needed to reconnect cleanly, stale expectations needed updating, and startup failures needed to be preserved instead of hidden by cleanup.

The fix:

RC34 updates the testnet harness for activation, oracle epochs, wallet restore, reorg, rescan, reindex, and multi-wallet flows. After restart/reindex, the local mini-testnet refreshes P2P links so all eight oracle nodes converge before persistence checks continue.

Details:

- The script exercises 16 active oracle wallets across 8 Qt/node processes in local mini-testnet mode.
- It checks 9-of-17 MuSig2 consensus and on-chain v0x03 bundle behavior.
- It mines, mints, transfers, redeems, restarts wallets, backs up/restores wallets, rescans, reindexes, and compares wallet state afterward.
- Oracle price refresh and oracle restart steps run after wallet restart/reindex so the signing network is live again before DD checks continue.
- The script tests descriptor export/import restore and confirms restored wallets can see DD state and perform DD operations.
- Harness cleanup preserves startup failures instead of replacing the real error with a cleanup error.
- The script's internal debug-log and persistence paths now use `testnet24`, so diagnostics follow the active RC34 chain instead of the old RC33 testnet directory.
- Local deterministic oracle keys are enabled only through `-easypow` local mini-testnet mode; normal testnet operation keeps the configured production testnet oracle keys.

Why it matters:

Partial script progress is not enough. RC34 needs the whole DigiDollar ecosystem to survive a real run from mining through mint, transfer, redeem, restart, rescan, reindex, and oracle consensus.

Code/tests touched:

- `test_multi_oracle_testnet.sh`
- `test/functional/digidollar_wave14_multinode_ibd_reorg.py`
- `test/functional/digidollar_wave26_mixed_node_compat.py`
- `test/functional/wallet_digidollar_active_restore_redeem.py`
- `test/functional/wallet_digidollar_transfer_ancestor_reorg.py`
- `test/functional/test_framework/test_node.py`
- `test/functional/test_framework/util.py`

### 14. Functional Test Runner Reliability

The issue:

One final-gate failure was not a DigiDollar consensus bug. `feature_coinstatsindex.py` could restart a node with RPC reachable on IPv6 loopback while the test framework kept polling an IPv4 URL.

The fix:

RC34 teaches the functional RPC URL builder to handle bracketed IPv6 hosts and makes `feature_coinstatsindex.py` use `::1` explicitly for RPC while keeping normal P2P loopback behavior.

Details:

- `feature_coinstatsindex.py` now binds RPC to `::1` and allows `::1/128`.
- The test framework brackets IPv6 RPC hosts when constructing URLs.
- The test's port-availability check uses the RPC host for RPC and keeps P2P on normal loopback.
- This was fixed as a harness issue only after proving the node/index behavior was not the bug.

Why it matters:

This was a harness bug, not a protocol bug. Fixing it keeps the release gate honest instead of hiding a real failure behind a flaky connection mistake.

Code/tests touched:

- `test/functional/feature_coinstatsindex.py`
- `test/functional/test_framework/test_node.py`
- `test/functional/test_framework/util.py`

### 15. Unit, Functional, Qt, And Fuzz Coverage

The issue:

The old coverage was not enough for RC34. Some harnesses were stale, some fuzz targets were unregistered, and some tests were checking old behavior instead of final V1 behavior.

The fix:

RC34 adds and registers coverage for DigiDollar activation, lock tiers, burn enforcement, health/DCA/volatility, oracle quorum, MuSig2 sessions, parser boundaries, wallet persistence, Qt display/lifetime behavior, P2P wire messages, price aggregation, and amount boundaries.

Details:

- Unit tests were added or consolidated for DigiDollar activation, lock tiers, collateral, burn enforcement, ERR, oracle quorum/domain rules, MuSig2 sessions, and amount compression.
- Functional tests now cover activation boundaries, miner/mempool parity, oracle block rules, oracle P2P, IBD/reorg/reindex, wallet restore, wallet rescan, wallet backup, and mixed-node compatibility.
- Qt tests cover DigiDollar widgets, translated reject reasons, lock-tier display, watch-only/spendability display, and wallet-model lifetime behavior.
- Fuzz targets cover DigiDollar math, txbuilder validation, parser boundaries, oracle bundle parsing, oracle bundle domain separation, MuSig2 auth signatures, oracle P2P wire messages, bitmap mutations, price aggregation, and block-data validation.
- Previously stale or obsolete tests were removed where stricter RC34 coverage replaced them.
- The registered fuzz target count reached 247 in the recorded RC34 fuzz gate.

Why it matters:

These tests are not paperwork. They caught actual bugs during RC34 cleanup, and they now guard the fixes.

Code/tests touched:

- `src/Makefile.test.include`
- `src/Makefile.qttest.include`
- `src/test/digidollar_*`
- `src/test/musig2_*`
- `src/test/oracle_*`
- `src/qt/test/digidollar*`
- `src/test/fuzz/digidollar_*`
- `src/test/fuzz/oracle_*`
- `test/functional/digidollar_*`
- `test/functional/wallet_digidollar_*`
- `test/fuzz/test_runner.py`

---

## Validation Evidence

The code stack was tested during RC34 cleanup with build, unit, functional, extended functional, fuzz, and live multi-oracle testnet runs.

Important note: these logs were captured before the final public `testnet24` reset commit. They prove the RC34 code stack that led into the reset. The exact tag candidate should still rerun the final gates on the fresh `testnet24` chain.

Recorded local logs:

- Baseline logs: `/tmp/rc34_baseline`
- Final cleanup logs: `/tmp/rc34_final`

Recorded results:

- Build completed in `/tmp/rc34_final/build.log`.
- Unit tests reported no errors in `/tmp/rc34_final/unit_test_digibyte.log`.
- Default functional tests passed 369 of 369 in `/tmp/rc34_final/functional_test_runner.log`.
- Extended functional tests passed 373 of 373 in `/tmp/rc34_final/functional_test_runner_extended.log`.
- Fuzz registration covered 247 of 247 registered targets in `/tmp/rc34_final/fuzz_test_runner.log`.
- The live multi-oracle testnet script recorded 222 passed checks and 0 failed checks in `/tmp/rc34_final/test_multi_oracle_testnet_outer.log` before the final public `testnet24` reset.

---

## Upgrade Instructions

### Everyone

1. Back up wallets and oracle key material.
2. Stop old RC33 or pre-RC34 testnet nodes.
3. Do not reuse old `testnet23` chainstate as RC34 proof.
4. Start clean on `testnet24`.
5. Reconnect only to RC34-compatible peers.

### Oracle Operators

1. Keep your assigned oracle slot unless Jared tells you otherwise.
2. Back up the wallet that holds your oracle key.
3. Start the RC34 node with DigiDollar and oracle support enabled.
4. Confirm the node is on `testnet24`.
5. Confirm your oracle public key matches the configured slot.
6. Confirm live exchange prices are being fetched.
7. Confirm MuSig2 sessions complete and v0x03 bundles are mined after activation.

Useful checks:

```bash
digibyte-cli -testnet getblockchaininfo
digibyte-cli -testnet getnetworkinfo
digibyte-cli -testnet listoraclekeys
digibyte-cli -testnet getoracleinfo
digibyte-cli -testnet getdigidollarinfo
```

### Wallet Testers

Test the user path that matters:

1. Create or load a wallet.
2. Mine or receive testnet DGB collateral.
3. Mint DigiDollar after activation.
4. Transfer DigiDollar.
5. Restart and confirm balances persist.
6. Rescan or reindex and confirm positions return.
7. Redeem DigiDollar and confirm collateral behavior.

---

## Example Configuration

Minimal RC34 testnet configuration:

```ini
testnet=1
server=1
txindex=1
digidollar=1
fallbackfee=0.0001
```

Oracle operators need their assigned oracle configuration. Do not use mock or fallback price paths for production-style testing.

---

## Downloads

Prebuilt binaries, when published, should use the RC34 version label:

- `digibyte-9.26.0-rc34-x86_64-linux-gnu.tar.gz`
- `digibyte-9.26.0-rc34-aarch64-linux-gnu.tar.gz`
- `digibyte-9.26.0-rc34-win64-setup.exe`
- `digibyte-9.26.0-rc34-osx.dmg`

Verify checksums and signatures from the official release location before running binaries.

---

## Cleaned Commits Since RC33

- `release: prepare RC34 metadata and docs`
- `digidollar: harden consensus validation and storage`
- `oracle: harden live-price MuSig2 bundles`
- `wallet: harden DigiDollar RPC and Qt persistence`
- `test: consolidate DigiDollar unit and Qt coverage`
- `test: harden functional and testnet harnesses`
- `fuzz: exercise DigiDollar and oracle targets`
- `test: make coinstatsindex RPC host explicit`
- `chain: reset DigiDollar testnet for RC34`
- `test: align multi-oracle script with testnet24`
- `docs: rewrite RC34 release notes for testnet24`

Each commit is meant to be readable on its own: what changed, why it changed, how it was tested, and what risk remains.

---

## Review Notes

- RC34 is a testnet release candidate, not a mainnet release.
- Mainnet deployment still requires Jared's review, tag, release, and deploy decision.
- `testnet24` is the public RC34 proving network.
- Old `testnet23` chainstate is not valid RC34 proof.
- Production-style DigiDollar testing must use live oracle prices and MuSig2 multi-oracle bundles.
- Nothing was pushed externally by this release-note update.

---

## Feedback

When reporting a problem, include the exact command, log path, node version, network, wallet state, and whether the node was on `testnet24`.

Best channels:

- Gitter: https://app.gitter.im/#/room/#digidollar:gitter.im
- GitHub issues or pull requests against the DigiByte Core repository
