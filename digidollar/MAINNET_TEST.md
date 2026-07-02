# DigiDollar Mainnet-PRE Test Plan (`v9.26.1-pre`)

## Objective

Create a temporary, explicitly reversible `v9.26.1-pre` build that runs the **mainnet chain type** from the real DigiByte genesis block, uses the real mainnet network magic bytes and mainnet DigiDollar/oracle economics, but is isolated from public DigiByte mainnet by using a new P2P port, no public mainnet peer discovery, and a separate data directory.

The purpose is a realistic mini-mainnet rehearsal for DigiDollar activation and oracle operation before the formal mainnet release. The PRE patch must be one commit that can be reverted before the real launch.

## Non-negotiable invariants

1. **Keep mainnet genesis unchanged**: `0x7497ea1b465eb39f1c8f507bc877078fe016d6fcb6dfad3a64c98dcc6e1e8496`.
2. **Keep mainnet message magic unchanged**: `fa c3 b6 da`.
3. **Do not use public DigiByte mainnet P2P port `12024`**.
4. **Do not discover public DigiByte mainnet peers** via DNS seeds, fixed seeds, stale `peers.dat`, copied config, or manual `addnode`/`connect` entries.
5. **Use fresh PRE data only**. Never reuse `~/.digibyte` mainnet `blocks/`, `chainstate/`, `peers.dat`, `settings.json`, or wallets unless deliberately imported.
6. **Keep the PRE patch easy to revert**. The formal mainnet launch should be the normal mainnet path after reverting the PRE commit.

## RC46 source facts verified

### Versioning

- Source of truth is `configure.ac`.
- `FormatFullVersion()` in `src/clientversion.cpp` returns `"v" PACKAGE_VERSION` plus a git/build suffix for non-release builds.
- `src/config/digibyte-config.h`, `configure`, `Makefile`, and `config.status` are generated/ignored in this tree; release builders must regenerate them after editing `configure.ac`.

### Current mainnet chain parameters

In `src/kernel/chainparams.cpp` mainnet currently has:

- Message magic: `fa c3 b6 da`.
- P2P port: `12024`.
- Genesis: `CreateGenesisBlock(1389388394, 2447652, 0x1e0ffff0, 1, 8000)` with the real mainnet hash.
- DNS seeds populated with public mainnet seeds.
- Fixed seeds populated from `chainparams_seed_main`.
- Historical activation heights: MultiAlgo `145000`, MultiShield `400000`, DigiSpeed `1430000`, Odo `9100000`/`9112320`, ReserveAlgoBits `8547840`.
- BIP34/BIP65/BIP66/CSV/SegWit buried at `4394880`.
- Taproot is a mainnet BIP9 deployment using bit `2`, start `1736510438`, timeout `1799582438`.
- DigiDollar is a mainnet BIP9 deployment using bit `23`, start `1780272000`, timeout `1811808000`, `min_activation_height = 23627520`.
- DigiDollar mainnet height gates: `nDDActivationHeight = 23627520`, `nOracleActivationHeight = 23627520`, `nDigiDollarMuSig2Height = 23627520`; MuSig2 activates alongside DigiDollar/oracle, not from genesis.
- Mainnet oracle quorum is `7-of-35`, with `nOraclePubkeyCount = 35`, `nOracleConsensusRequired = 7`, `nOracleTotalOracles = 35`.

### Current testnet26 fast-activation model

In `src/kernel/chainparams.cpp` testnet26 currently has the schedule we want to mirror for PRE activation mechanics:

- BIP34/BIP65/BIP66/CSV at `1`; SegWit at `0`.
- BIP9 window `200`, threshold `140`.
- MultiAlgo at `0`, MultiShield at `200`, DigiSpeed at `400`, Odo at `500`, ReserveAlgoBits at `0`.
- Taproot `ALWAYS_ACTIVE`.
- DigiDollar bit `23`, start `1780156800`, timeout `1830297600`, `min_activation_height = 600`.
- Oracle/DigiDollar/MuSig2 height gates all at `600`.

### Current DigiDollar economics and lock tiers

In `src/consensus/digidollar.h` the default DigiDollar params currently include:

- Minimum mint: `10000` cents = `$100`.
- Maximum mint: `10000000` cents = `$100,000`.
- Minimum output: `100` cents = `$1`.
- Oracle threshold: `7` of `35`.
- Price valid window: `20` blocks.
- DCA and ERR thresholds are mainnet-style defaults.
- Collateral tiers include **a 1-hour / 240-block tier at 1000%** plus 30d, 90d, 180d, 1y, 2y, 3y, 5y, 7y, 10y.

The 1-hour tier is not just a GUI option. It is encoded in several places:

- `src/consensus/digidollar.h`: `collateralRatios` contains `{240, 1000}`.
- `src/consensus/digidollar.cpp`: `LockDaysToBlocks(0)` returns `240`.
- `src/digidollar/validation.cpp`: hardcoded `TIER_LOCK_DAYS[] = {0, 30, 90, ...}` validates OP_RETURN lock tier vs lock height.
- `src/digidollar/txbuilder.cpp`: uses lock days and `MINT_LOCK_CONFIRMATION_BUFFER_BLOCKS`.
- `src/qt/digidollarmintwidget.cpp` and `src/qt/walletmodel.cpp`: GUI/wallet tier arrays expose tier `0` as 1 hour and default the combobox back to tier `0` on clear.

**Conclusion:** if PRE must prove “no one-hour mainnet mint,” the PRE/mainnet launch patch must remove or disable the 1-hour tier at the consensus layer, not merely hide it in Qt.

## Recommended PRE port and local ports

Use **P2P port `12046`** for `v9.26.1-pre`.

Validation performed in this tree:

- `12024` = current mainnet.
- `12033` = current testnet26.
- `12030`, `12031`, `12032`, `12035` appear as retired testnet ports in docs.
- `12034` appears in oracle-discovery architecture examples.
- `12046` had no hits in the source/docs scan outside build artifacts, and no `/etc/services` assignment was found locally.

Recommended local PRE ports:

- P2P: `12046`.
- RPC: `14046` to avoid conflicts with a normal mainnet node on `14022`.
- Onion service target: `14146`, or disable onion listening for PRE unless deliberately testing Tor.

## Required future PRE commit checklist

### 1. Version / branding

File: `configure.ac`

- Set package version to `9.26.1-pre`.
- Keep `CLIENT_VERSION_MAJOR=9`, `CLIENT_VERSION_MINOR=26`, `CLIENT_VERSION_BUILD=1`.
- Keep `CLIENT_VERSION_IS_RELEASE=false`.
- Regenerate ignored build outputs before release packaging (`./autogen.sh`, `./configure`, or the Guix/release pipeline equivalent).

Qt splash/about dialogs already render `FormatFullVersion()`, so after regeneration they should display `v9.26.1-pre...`. No static splash PNG was found that needs editing for the version text.

### 2. Mainnet-PRE network isolation

Files: `src/kernel/chainparams.cpp`, `src/chainparamsbase.cpp`, possibly `src/init.cpp`

Mainnet params for PRE should:

- Change `nDefaultPort` from `12024` to `12046`.
- Keep `pchMessageStart` unchanged (`fa c3 b6 da`).
- Keep genesis creation and genesis hash assertions unchanged.
- Clear or replace public mainnet DNS seeds:
  - `vSeeds.clear();`
  - do not leave `seed.digibyte.io`, `seed.diginode.tools`, etc. in the PRE binary.
- Clear fixed seeds:
  - replace `vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));` with `vFixedSeeds.clear();` for PRE.
- Change mainnet base ports in `src/chainparamsbase.cpp` for PRE:
  - RPC `14046`.
  - onion target `14146` or disable onion by operator config.
- Strongly consider setting the main chain data-dir suffix to something like `mainnet-pre` for the PRE commit, even though the chain type remains `MAIN`. If we leave mainnet’s base data dir empty, operators must pass `-datadir` manually and a single mistake can reuse normal mainnet data.
- Add a PRE startup guard or explicit warning if a manual peer string uses `:12024` in `-addnode`, `-connect`, or `-seednode`. Because magic/genesis remain mainnet, a manual `host:12024` can defeat the port isolation.

Operator-side config should also include:

```ini
# PRE datadir/conf only; never in normal ~/.digibyte/digibyte.conf
dnsseed=0
fixedseeds=0
listenonion=0
onion=0
port=12046
rpcport=14046
txindex=1
server=1
```

Use explicit PRE peers only:

```ini
addnode=<bootstrap-pre-node>:12046
# or for a private lab node:
connect=<bootstrap-pre-node>:12046
```

### 3. Fast historical fork schedule while preserving mainnet identity

File: `src/kernel/chainparams.cpp`

For PRE only, copy the current testnet26 activation timing into `CMainParams` so a fresh chain reaches the modern rule set quickly:

- `BIP34Height = 1` and `BIP34Hash = 0x0` or a correct PRE block-1 hash after mining if we decide to enforce it.
- `BIP65Height = 1`.
- `BIP66Height = 1`.
- `CSVHeight = 1`.
- `SegwitHeight = 0`.
- `MinBIP9WarningHeight = 0` or a PRE-appropriate low value.
- `nRuleChangeActivationThreshold = 140`.
- `nMinerConfirmationWindow = 200`.
- `multiAlgoDiffChangeTarget = 0`.
- `alwaysUpdateDiffChangeTarget = 200`.
- `workComputationChangeTarget = 400`.
- `algoSwapChangeTarget = 500`.
- `OdoHeight = 500`.
- `ReserveAlgoBitsHeight = 0`.
- `nOdoShapechangeInterval = 1 * 24 * 60 * 60` if mirroring testnet26.
- Taproot should be `ALWAYS_ACTIVE`/`NO_TIMEOUT` with `min_activation_height = 0`, because DigiDollar outputs use P2TR.

Important tradeoff: current code entangles some historical heights with difficulty/reward-era behavior (`nDiffChangeTarget`, `patchBlockRewardDuration`, `patchBlockRewardDuration2`). If the goal is “current mainnet consensus rules by block 600,” we should use the compressed testnet-style heights. If the goal is exact DGB emission economics, those compressed heights are not exact mainnet history. For DigiDollar PRE, the recommendation is to prioritize current-rule activation by block 600 and document the reward-history compression as a deliberate PRE-only testing compromise.

### 4. DigiDollar BIP9 and height gates

File: `src/kernel/chainparams.cpp`

For PRE mainnet:

- Keep deployment bit `23`.
- Use the testnet-style BIP9 sequence:
  - `nStartTime = 1780156800` or another value already safely in the past at test time.
  - `nTimeout = 1830297600` or a generous future timeout.
  - `min_activation_height = 600`.
  - `nRuleChangeActivationThreshold = 140`.
  - `nMinerConfirmationWindow = 200`.
- Set:
  - `consensus.nDDActivationHeight = 600`.
  - `consensus.nOracleActivationHeight = 600`.
  - `digidollarParams.minMintAmountActivationHeight = 600`.
  - `consensus.nDDOracleEpochBlocks = 40`.
  - `consensus.nDDOracleUpdateInterval = 4`.
  - `consensus.nOracleEpochLength = 40`.
  - `consensus.nOracleRequiredMessages = 7`.
  - `consensus.nOracleTotalOracles = 35`.
  - `consensus.nDigiDollarMuSig2Height = consensus.nDDActivationHeight` (`600` for PRE).
  - `consensus.nOraclePubkeyCount = 35`.
  - `consensus.nOracleConsensusRequired = 7`.

Expected BIP9 timeline with 200-block windows:

- Blocks `0-199`: DEFINED.
- Blocks `200-399`: STARTED; miners signal bit 23.
- Blocks `400-599`: LOCKED_IN if at least 140/200 blocks signaled.
- Blocks `600+`: ACTIVE.

`getblocktemplate` should automatically set the deployment bit when the versionbits state is STARTED, but mining/pool templates should be verified before the PRE release.

### 5. Oracle key roster and operator workflow

Files: `src/kernel/chainparams.cpp`, `src/rpc/digidollar.cpp`, `src/wallet/rpc/wallet.cpp`

Current RC46 command reality:

- Valid command: `createoraclekey`.
- Invalid/nonexistent command: `createoracle`.
- `createoraclekey` is wallet-scoped local key management. It stores a key for later use but does not start an oracle, sign prices, relay data, or change consensus state.
- `startoracle <id>` starts the runtime oracle only after the key is authorized in chainparams.
- Operators share only the compressed `pubkey`; never share private keys.
- `pubkey_xonly` is derived from the compressed `pubkey` and must align with `consensus.vOraclePublicKeys`.

Operator flow for PRE/mainnet keys:

```bash
# Use the matching v9.26.1-pre digibyte-cli and digibyted.
# Use the PRE datadir, not normal mainnet.
digibyte-cli -datadir=<PRE_DATADIR> createwallet <wallet_name>
digibyte-cli -datadir=<PRE_DATADIR> -rpcwallet=<wallet_name> createoraclekey <oracle_id>
```

If the wallet is encrypted:

```bash
digibyte-cli -datadir=<PRE_DATADIR> -rpcwallet=<wallet_name> walletpassphrase "<passphrase>" 600
digibyte-cli -datadir=<PRE_DATADIR> -rpcwallet=<wallet_name> createoraclekey <oracle_id>
```

After the final roster is committed and the chain is active enough:

```bash
digibyte-cli -datadir=<PRE_DATADIR> -rpcwallet=<wallet_name> startoracle <oracle_id>
digibyte-cli -datadir=<PRE_DATADIR> getoraclepubkey <oracle_id>
digibyte-cli -datadir=<PRE_DATADIR> getoracleprice
```

PRE chainparams must update both rosters in slot order:

- `consensus.vOraclePublicKeys` — x-only 32-byte keys.
- `vOracleNodes` — full compressed 33-byte pubkeys plus PRE endpoints.

All `vOracleNodes` endpoints should use the PRE P2P port `12046`, not `12024`. Startup `ValidateOracleNodeAlignment()` must pass.

### 6. Remove or disable the 1-hour mint tier for PRE/mainnet realism

This is required if the PRE release is supposed to prove real mainnet behavior with the first mint tier at 30 days.

Do **not** only remove the GUI combobox option. The consensus path must reject 1-hour mints.

Recommended implementation approach:

1. Introduce a single canonical lock-tier table/helper used by consensus validation, txbuilder, walletmodel, and Qt instead of separate hardcoded arrays.
2. For mainnet/PRE, define tiers as:
   - 30 days — 500%.
   - 90 days — 400%.
   - 180 days — 350%.
   - 365 days — 300%.
   - 730 days — 275%.
   - 1095 days — 250%.
   - 1825 days — 225%.
   - 2555 days — 212%.
   - 3650 days — 200%.
3. Either reserve old tier ID `0` as invalid on mainnet/PRE, or remap tier ID `0` to 30 days and update every OP_RETURN/validation/UI path consistently. Reserving `0` as invalid is safer for auditability because any 1-hour-style transaction fails obviously.
4. Keep 1-hour only on regtest/test-only chains if still useful for automated tests.
5. Add unit tests proving:
   - mainnet/PRE rejects 1-hour mint metadata;
   - mainnet/PRE accepts 30-day as the minimum tier;
   - Qt/wallet cannot build 1-hour mints;
   - validation rejects malformed tier/height mismatches.

### 7. Mining and bootstrap procedure

Recommended PRE launch procedure:

1. Build `v9.26.1-pre` binaries from the PRE commit.
2. Start one bootstrap node with a fresh PRE datadir:

```bash
digibyted -datadir=<PRE_DATADIR> \
  -server=1 -txindex=1 \
  -dnsseed=0 -fixedseeds=0 -listenonion=0 -onion=0 \
  -port=12046 -rpcport=14046 \
  -debug=net -debug=validation -debug=digidollar
```

3. Start miners/pools against the PRE bootstrap node.
4. Verify block templates signal bit `23` during the STARTED window.
5. Mine past block `600`.
6. Verify activation:

```bash
digibyte-cli -datadir=<PRE_DATADIR> getblockchaininfo
digibyte-cli -datadir=<PRE_DATADIR> getdeploymentinfo
digibyte-cli -datadir=<PRE_DATADIR> getblocktemplate
```

7. Start at least 7 authorized oracle operators; 35 preferred for the full rehearsal.
8. Verify MuSig2 oracle bundle flow:

```bash
digibyte-cli -datadir=<PRE_DATADIR> getoracles
digibyte-cli -datadir=<PRE_DATADIR> listoracle
digibyte-cli -datadir=<PRE_DATADIR> getoracleprice
```

9. Execute DigiDollar flows:
   - Mint minimum `$100` DD using 30-day minimum lock.
   - Transfer DD between wallets.
   - Verify wallet restore/rescan sees DD UTXOs and collateral positions.
   - Verify stats/index/RPC outputs.
   - For normal redemption, either mine the full 30-day lock window (`172800 + 100` blocks) or run a separate local-only test patch/regtest. Do not reintroduce 1-hour into the PRE release if the goal is mainnet realism.

### 8. Validation and test gates before releasing PRE

Minimum pre-release gates:

```bash
git diff --check
make -C src -j$(nproc) test/test_digibyte
./src/test/test_digibyte --run_test=oracle_config_tests --catch_system_errors=no
./src/test/test_digibyte --run_test=rh50_oracle_keyset_alignment_tests --catch_system_errors=no
./src/test/test_digibyte --run_test=digidollar_activation_tests --catch_system_errors=no
```

Release-quality gates:

- Full unit suite.
- Full functional suite relevant to mining, versionbits, wallet, oracle, and DigiDollar.
- Fuzz smoke for DigiDollar/oracle targets if time permits.
- Manual PRE node startup from empty datadir.
- Verify `getnetworkinfo` shows local port `12046`.
- Verify no DNS seed queries to public mainnet seeds.
- Verify no outbound connection to `:12024`.
- Verify `peers.dat` is fresh/empty except PRE peers.
- Verify `ValidateOracleNodeAlignment()` passes at startup.
- Verify BIP9 state reaches ACTIVE at block `600`.
- Verify a PRE node refuses or at least loudly warns on manual `addnode/connect` entries using `:12024`.

### 9. Rollback / formal mainnet launch

The PRE commit should contain only temporary PRE items:

- Version suffix `9.26.1-pre`.
- PRE P2P/RPC/onion ports.
- PRE datadir suffix or operator-only PRE datadir instructions.
- Cleared/replaced seeds.
- Compressed activation heights/windows for mainnet PRE.
- PRE oracle endpoint ports.
- Any PRE-only manual-peer safety guard.

For formal mainnet launch:

1. Revert the PRE commit.
2. Keep the final real mainnet oracle key roster if it was committed separately; otherwise re-apply only that roster in a clean launch commit.
3. Restore normal mainnet port `12024`, RPC `14022`, public seeds, fixed seeds, normal activation heights, and formal deployment timing.
4. Build/release the formal version after full test suite and release checks.

## Biggest risks to close before coding

1. **Manual peer override risk**: because magic and genesis are intentionally unchanged, `connect=<mainnet-node>:12024` can bridge PRE to public mainnet. Add a PRE guard or make operator docs brutally clear.
2. **Datadir reuse risk**: mainnet normally uses `~/.digibyte` directly. PRE should either change the main datadir suffix or require `-datadir=<PRE_DATADIR>` everywhere.
3. **1-hour tier risk**: current RC46 source still allows tier `0` = 1 hour at consensus and GUI layers. If no-1-hour is a launch requirement, fix it in consensus before PRE.
4. **Historical-height/economics tradeoff**: testnet-style compressed fork heights are needed for block-600 activation, but they are not exact historical mainnet reward timing. This is acceptable for a DigiDollar activation rehearsal if documented.
5. **Oracle roster timing**: do not release PRE to operators until all final mainnet oracle compressed pubkeys are in `vOracleNodes` and x-only keys are aligned in `consensus.vOraclePublicKeys`.
