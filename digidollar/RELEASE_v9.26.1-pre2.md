# DigiByte Core v9.26.1-pre2 Release Notes

`v9.26.1-pre2` is a one-time DigiDollar mainnet-oracle rehearsal build.

It is not a normal user release. It is not the final DigiDollar mainnet launch. It is a temporary, isolated test build for assigned oracle operators and release coordinators to prove the final mainnet oracle configuration, compressed mainnet activation path, MuSig2 oracle bundle activation, and DigiDollar wallet/RPC behavior before the real mainnet release.

Think of this build as halfway between mainnet and testnet:

- it keeps mainnet identity where oracle keys need it: mainnet genesis, mainnet message magic, mainnet address formats, and the final mainnet oracle keys;
- it isolates itself from public DigiByte mainnet by using PRE-only ports, no seed discovery, no fixed seeds, and a separate data directory;
- it compresses historical DigiByte fork points and DigiDollar activation timing so the full activation path can be tested quickly from a fresh chain.

Development branch: `feature/digidollar-v1`

Release tag: `v9.26.1-pre2` (PRE/oracle rehearsal only)

`v9.26.1-pre2` replaces the first PRE rehearsal tag for the current oracle test release. It keeps the same isolated mainnet-PRE network contract while carrying the latest DigiDollar mining/template fixes and updated wallet branding for this second PRE build.

Mining fix summary:

- Non-oracle miners can include DigiDollar oracle price bundles when they use the upgraded DigiDollar-aware block-template path. They do not need oracle private keys.
- Legacy miners can still mine valid normal DGB blocks. They just will not receive oracle-priced DigiDollar mint/redeem work unless they opt in to the new GBT rule.
- CPU mining was updated so patched `cpuminer` can request DigiDollar-aware templates with `--digidollar` and preserve the oracle commitment in the coinbase.
- Pool/miner software should request `getblocktemplate` with `{"rules":["segwit","digidollar-oracle"]}` and copy `default_oracle_commitment` exactly when Core returns it.

## Required PRE Configuration

Run this build with a fresh PRE data directory. Put `digibyte.conf` in that fresh data directory, not in an existing normal mainnet `~/.digibyte` directory. The node will still report `chain=main`, but this build stores its chain data under the `mainnet-pre` network subdirectory.

Example operator `digibyte.conf`:

```ini
# DigiByte Core v9.26.1-pre2 isolated mainnet-oracle rehearsal
# Use this only with a fresh PRE data directory. Do not use normal ~/.digibyte.

server=1
listen=1
txindex=1
digidollar=1

# Mainnet-PRE network ports
port=12046
rpcport=14046

# Join the coordinated PRE network through the bootstrap node.
addnode=digibyte.io:12046

# Keep this PRE build isolated from public DigiByte mainnet peer discovery.
dnsseed=0
fixedseeds=0
discover=0
listenonion=0
onion=0
upnp=0
natpmp=0

# Keep RPC local unless you have a separate trusted tunnel/firewall.
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
```

Network/firewall requirements:

- Open inbound TCP `12046` for the PRE P2P network if this node should accept peer/oracle connections.
- Do not open or use public mainnet P2P port `12024` for this rehearsal.
- Do not expose RPC port `14046` to the public internet.
- Use `addnode=digibyte.io:12046`; the PRE bootstrap/oracle node must also listen on TCP `12046`.

---

## Read This First

Do not run `v9.26.1-pre2` as a normal wallet release.

This build is for a controlled oracle rehearsal. It intentionally changes mainnet startup behavior in order to create a side-mainnet test chain that uses real mainnet oracle keys but does not join public DigiByte mainnet.

Normal users should not install this build for everyday funds, merchant use, mining, exchange use, or public mainnet operation.

Oracle operators should only run it with the coordinated PRE instructions and a fresh PRE data directory. Do not point it at an existing `~/.digibyte` mainnet directory.

After this PRE rehearsal is complete, the PRE isolation/fast-activation commit is expected to be reverted before preparing the final mainnet launch build.

---

## Purpose

The goal of `v9.26.1-pre2` is to test the final mainnet DigiDollar oracle setup under mainnet-like conditions without touching public mainnet.

This lets us verify:

- the final 35-slot mainnet oracle roster;
- 7-of-35 MuSig2 oracle quorum configuration;
- DigiDollar, oracle, and MuSig2 activation at the same height;
- mainnet address/key handling for oracle wallets;
- BIP9 activation flow with real mined blocks;
- DigiDollar wallet and RPC behavior after activation;
- peer isolation from public mainnet.

The build exists because public testnet does not perfectly reproduce mainnet conditions. This PRE chain keeps the important mainnet identity pieces while using testnet-style fast activation timing.

---

## Mainnet-PRE Network Details

| Item | `v9.26.1-pre2` value |
| --- | --- |
| Chain type | Main (`chain=main`) |
| Data directory suffix | `mainnet-pre` |
| Mainnet genesis | unchanged: `0x7497ea1b465eb39f1c8f507bc877078fe016d6fcb6dfad3a64c98dcc6e1e8496` |
| Mainnet message magic | unchanged: `fa c3 b6 da` |
| Address format | unchanged mainnet DigiByte addresses |
| Default PRE P2P port | `12046` |
| Default PRE RPC port | `14046` |
| Default PRE onion target port | `14146` |
| Public mainnet P2P port | not used; public `12024` peers are blocked for this PRE build |
| DNS seeds | disabled |
| Fixed seeds | disabled |
| Checkpoints | genesis-only for PRE chain replay |
| AssumeUTXO / assumevalid | disabled for PRE rehearsal |
| DigiDollar BIP9 bit | `23` |
| BIP9 window | `100` blocks |
| BIP9 threshold | `70` blocks |
| DigiDollar minimum activation height | `600` |
| Oracle activation height | `600` |
| MuSig2 format activation height | `600` |
| Oracle roster | 35 active slots |
| Oracle quorum | 7 signatures from the configured 35-key MuSig2 roster |

The PRE build deliberately keeps the mainnet genesis and magic bytes so oracle operators test the same mainnet key context. Isolation comes from the PRE data directory, PRE ports, disabled peer discovery, and the public-mainnet-port guard.

---

## Fast Activation Logic

The PRE chain compresses historical DigiByte rule changes so a fresh chain reaches the modern DigiDollar test surface quickly.

Compressed PRE fork schedule:

| Rule point | PRE height |
| --- | --- |
| BIP34 | `1` |
| BIP65 | `1` |
| BIP66 | `1` |
| CSV | `1` |
| SegWit | `0` |
| DigiShield boundary | `67` |
| MultiAlgo | `100` |
| Always-update difficulty | `200` |
| Work computation update | `400` |
| ReserveAlgoBits | `450` |
| Algo swap | `490` |
| Odocrypt | `500` |
| DigiDollar / oracle / MuSig2 | `600` |

DigiDollar uses BIP9 bit `23` with a 100-block window and 70-block threshold. The expected PRE flow is:

| Height range | Expected deployment state |
| --- | --- |
| early chain through first signalling window | `started` once the window opens |
| after a successful 70-of-100 signalling window | `locked_in` |
| after the height-599 tip / block-600 gate | `active` |
| height `600+` | DigiDollar, oracle runtime, and MuSig2 bundle format are all enabled together |

This is intentionally fast. It is designed to prove activation behavior, not to reproduce years of mainnet elapsed time.

---

## What Changed From RC46

- Mainnet is temporarily configured as `mainnet-pre` for this rehearsal.
- Mainnet genesis, message magic, address formats, and oracle keys stay mainnet.
- PRE P2P/RPC ports are `12046` / `14046`.
- DNS seeds and fixed seeds are disabled so the node does not discover public mainnet peers.
- Public mainnet P2P port `12024` is guarded against accidental outbound use.
- Historical DigiByte fork heights are compressed so a fresh chain can reach the current rule set before DigiDollar activation.
- DigiDollar BIP9 uses a 100-block window, 70-block threshold, and minimum activation height `600`.
- DigiDollar, oracle runtime, and MuSig2 oracle bundle format all activate together at height `600`.
- The final 35-slot mainnet oracle roster is configured on PRE port `12046`.
- `getblocktemplate` now has a DigiDollar-aware path using the `digidollar-oracle` rule so non-oracle miners and pools can safely receive oracle commitments and price-dependent DigiDollar mint/redeem transactions.
- Legacy `getblocktemplate` callers remain safe: they do not receive `default_oracle_commitment` and they do not receive oracle-priced DigiDollar mint/redeem work they cannot mine correctly.
- Block assembly now checks the same completed MuSig2 session source used to stamp the coinbase oracle bundle, so a miner node can include valid price bundles without being one of the signing oracle wallets.
- Stale oracle-bearing GBT templates are rebuilt instead of being reused after the oracle price ages out.
- The PRE CPU-mining harness can pass `--digidollar` to patched `cpuminer`, allowing CPU miners to preserve `default_oracle_commitment` while mining the normal DigiByte proof-of-work algos.

---

## DigiDollar Mining Fixes In Pre2

The first PRE rehearsal proved oracle signing could complete, but mining needed a clearer handoff between Core, CPU miners, and pool software.

The core issue was simple: DigiDollar mint and redeem transactions need a fresh MuSig2 oracle price bundle in the block coinbase. A miner that builds a block without that bundle can still mine a valid normal DGB block, but it cannot correctly confirm price-dependent DigiDollar mint/redeem transactions.

`v9.26.1-pre2` fixes that flow without requiring every miner to become an oracle.

Mining now has two explicit paths:

| Miner path | Request | Result |
| --- | --- | --- |
| Legacy GBT | `{"rules":["segwit"]}` | Mines valid normal DigiByte blocks. Does not receive oracle-priced DigiDollar mint/redeem transactions or `default_oracle_commitment`. |
| DigiDollar-aware GBT | `{"rules":["segwit","digidollar-oracle"]}` | Receives `default_oracle_commitment` when a fresh bundle is available and can include DigiDollar mint/redeem work that depends on that price. |

When Core returns `default_oracle_commitment`, miner or pool software must copy that script exactly into the coinbase as a zero-value output. Do not invent an oracle commitment if Core does not provide one.

What was fixed:

- Non-oracle miners can mine blocks with oracle price bundles if their upgraded node has the completed MuSig2 bundle at template time.
- `getblocktemplate` advertises DigiDollar-aware work with `!digidollar-oracle` in `rules` and exposes the exact `default_oracle_commitment` script to preserve.
- GBT caches separate legacy templates from DigiDollar-aware templates so legacy miners do not accidentally receive work they cannot assemble correctly.
- GBT rebuilds stale oracle-bearing templates after the bundle freshness window moves forward, preventing stale price commitments from being reused.
- Block assembly removes price-dependent mint/redeem transactions when no fresh oracle bundle is available instead of creating an invalid block.
- Price-independent DigiDollar transfers can still flow without an oracle price bundle.
- Patched `cpuminer` can be run with `--digidollar` so solo CPU mining requests the DigiDollar-aware GBT rule and preserves the returned oracle commitment.

Pool and miner software requirements:

- Use `{"rules":["segwit","digidollar-oracle"]}` for DigiDollar-aware GBT.
- Preserve `default_witness_commitment` and `default_oracle_commitment` as zero-value coinbase outputs when they are present.
- Do not include DigiDollar mint/redeem transactions from outside the returned template when Core did not provide `default_oracle_commitment`.
- Downstream ASIC/GPU workers do not need oracle private keys. The pool server or solo miner builds the correct coinbase from Core's template.
- See `DIGIDOLLAR_MINING_INTEGRATION_GUIDE.md` for pool, Stratum, cpuminer, DigiHashV2, and node-stratum-pool integration rules, including public reference-code links to:
  - `https://github.com/JaredTate/cpuminer-multi`
  - `https://github.com/JaredTate/node-stratum-pool`
  - `https://github.com/JaredTate/digihashv2`

This keeps fork risk low: old miners continue producing valid normal blocks, while upgraded DigiDollar-aware miners and pools have a deterministic path to mine mints/redeems with the correct oracle bundle.

---

## What Did Not Change

- The real mainnet genesis hash did not change.
- The real mainnet message magic did not change.
- Mainnet address prefixes did not change.
- The DigiDollar economic rules are not redesigned in this PRE build.
- Oracle quorum remains 7-of-35.
- Testnet26 remains its own network and is not reset by this PRE release.
- Regtest activation behavior is not changed by this PRE release.
- This PRE release does not activate DigiDollar on public mainnet.

---

## Final Mainnet-PRE Oracle Roster

All oracle endpoints use PRE port `12046` for this rehearsal. Public mainnet oracle operation will use the final launch instructions after PRE is reverted and the formal mainnet build is prepared.

| Slot | Operator | PRE endpoint | Compressed pubkey |
| --- | --- | --- | --- |
| 0 | DigiByte.Io Oracle | `oracle1.digibyte.io:12046` | `0345c1c7aeb4559f1b79629e0de36fb101cf279a07c5853e305240233e6945882e` |
| 1 | Green Candle | `oracle2.digidollar.org:12046` | `02615653c7883eadb97f5625b0e7fecc960094b9d33a1e281c3a6dfbaa47d529ef` |
| 2 | Bastian | `oracle3.digidollar.org:12046` | `02842d9481a07d949a60b526cea8d315db24f467f33901c1e73875f378bb40c75d` |
| 3 | DanGB | `oracle4.digidollar.org:12046` | `03ddb1e57cc9691d4f4aef18b7b46a7240170f73307037822444b4a76327f6d27e` |
| 4 | Shenger | `oracle5.digidollar.org:12046` | `02d4199fb00150d02ffae0b1b3fed207a08029c91a1d044fbc27125ac8f6d45261` |
| 5 | Ycagel | `oracle6.digidollar.org:12046` | `035e80419393fad8ff0426bd32bde8ccc6570b024a9138660f3fae5ecd60bc195a` |
| 6 | Aussie | `oracle7.digidollar.org:12046` | `03b9a11dde09f69e864223615daecf4e0829b6e2f5e417f50f8cfd74d0a6181488` |
| 7 | LookInto | `oracle8.digidollar.org:12046` | `03d15eb49e30bedecbf37f5863d266ea6ca19e81863f4df268d717c62013741f79` |
| 8 | JohnnyLawDGB | `oracle9.digidollar.org:12046` | `0299a8442a35abaac5b9c4922e0754d0de6c8ed9d946a1ad9690b62f89a791b374` |
| 9 | Ogilvie | `oracle10.digidollar.org:12046` | `0220bcc8be5f739f826c771f86f45eda0b5b50d11d8f8832c92ed626a65c44c4f2` |
| 10 | ChopperBrian | `oracle11.digidollar.org:12046` | `02b710dfc3a74137a0d9f3e26d3fd5091ba9ce29b990e12920ac1101b001e23858` |
| 11 | Crypto Corner Shop | `oracle12.digidollar.org:12046` | `03b9ca28f68bbe4f0b6f93c08b184e81e11b754906f2426739a1cfee48508276ca` |
| 12 | DaPunzy | `oracle13.digidollar.org:12046` | `032c4422fb8e5eac5d27423272d35abe6cff0acb19ab051e51204e3392365c18f3` |
| 13 | DigiByte_FORCE | `oracle14.digidollar.org:12046` | `0258889fb092fdf366004032d3af7cc35fddd951eb8413409699e879f795111a0e` |
| 14 | Neel | `oracle15.digidollar.org:12046` | `034b013d2203e06461187604992333b367db165f0e79905c0974ba8a554bc69fec` |
| 15 | DigiSwarm | `oracle16.digidollar.org:12046` | `030f809ccbeea32bcca9c817dbf674e38e2fbc234f4462b88612876287489197a9` |
| 16 | GTO90 | `oracle17.digidollar.org:12046` | `03943744e1635c2871db786b2ef98d3d5007b2d1f8bb622b44a99b31fbe63cc5bf` |
| 17 | digibyte-maxi | `oracle18.digidollar.org:12046` | `039d83219cf9056854da1f56fbf1d792ef14b1853ec027cd9c0a51465e2ec2afaa` |
| 18 | Anthony | `oracle19.digidollar.org:12046` | `03e2fa8be3480929a59fb93b284e49f4b74bfb52ffed3934de751bdb1c33c54e6e` |
| 19 | mbah_jambon | `oracle20.digidollar.org:12046` | `02507a43d15e0b6c149dd1835347879a1b723bdbecb4cc8afe124c52df37212295` |
| 20 | Camden | `oracle21.digidollar.org:12046` | `021bf934b18dd8bab94cec0dfc127f293afd542b8042df60a439affd5e64831285` |
| 21 | Twoface123 | `oracle22.digidollar.org:12046` | `0275481d5d543b27e298ba726674730d11d5bc1139f13205957b20d9936225ea38` |
| 22 | LivingTheLife | `oracle23.digidollar.org:12046` | `02deec78492920e9010c3015c50bacbcc158272a941220bdf74208f4796d962542` |
| 23 | ChozenOne43 | `oracle24.digidollar.org:12046` | `025b25677c412923376670284021ed9e99b53ebee6f4e3ef6123e742fb020e1f48` |
| 24 | ckunchained | `oracle25.digidollar.org:12046` | `0339543917295ddbba5bf3fa1198f3d7f63321364192f5839e9699a6f50572b68a` |
| 25 | JMag | `oracle26.digidollar.org:12046` | `03d476d0565e20ca0901c1b61a0aee36e3a6544346df48bdf45447ea051827ddee` |
| 26 | HashedMax | `oracle27.digidollar.org:12046` | `0336031b96a287f671172106f2d62cbeec0e547d31b591169ebbe9ddef7277e5b9` |
| 27 | DennisPitallano | `oracle28.digidollar.org:12046` | `0380f5bdf4f359c7daccfa8156d9569f8b48e36619feadb38adb5292a93c877563` |
| 28 | DigiHash Mining Pool | `digihash.digibyte.io:12046` | `03810ab54165a93b0d219494da9a7830ef245f49696662ddc49644d25b081c79d3` |
| 29 | medgboracle3452 | `oracle30.digidollar.org:12046` | `021ff6510c5c34604fdde76b866656f40a966e04fbabf27589155b754033139d3d` |
| 30 | DigibyteDaily | `oracle31.digidollar.org:12046` | `0317aaa874bc5d71a1f9f2e0b54ca9188cabd732fe9776530aa79e7ac2dc0f0b67` |
| 31 | Peer2Peer / DigiRoos | `oracle32.digidollar.org:12046` | `03a59c6d65b529fe956704ea6199a55dc1363f4fd4b79d56903f2d1737c7b09284` |
| 32 | 3DogsKanab | `oracle33.digidollar.org:12046` | `03451f9d309a35d1eeefdc2bdacadb0d3dd88c50c51dce54317de6ae6bceb49370` |
| 33 | LiberatedLark | `oracle34.digidollar.org:12046` | `032218c8607635c1f4d71d5497a9d395404fe3c01adf58f92432fbc84b3849534c` |
| 34 | Manu_DGB_oracle | `oracle35.digidollar.org:12046` | `038e6bc5e7addbf15369c87cd194cc63590c710b8d5c9e592d52ea13ce03927446` |

---

## Operator Guidance

This release is only for assigned oracle operators and coordinated PRE testers.

Minimum rules:

- Use a fresh PRE data directory.
- Do not reuse normal mainnet `~/.digibyte` data.
- Do not copy normal mainnet `peers.dat`.
- Do not add or connect to public mainnet peers on port `12024`.
- Open/use PRE P2P port `12046`.
- Confirm `getblockchaininfo` reports `chain=main` while the data directory path is `mainnet-pre`.
- Confirm `getdigidollardeploymentinfo` reports:
  - `min_activation_height = 600`
  - `oracle_activation_height = 600`
  - `musig2_format_activation_height = 600`
  - `oracle_pubkey_count = 35`
  - `oracle_consensus_required = 7`
  - `oracle_total_slots = 35`
- Start oracle runtime only after the PRE chain reaches DigiDollar/oracle activation.

This PRE chain is allowed to use mainnet oracle keys because it stays isolated from public mainnet.

---

## Validation Status

Known local validation for the PRE code line:

| Gate | Status |
| --- | --- |
| Mainnet-PRE chainparams unit coverage | PASS: verifies mainnet genesis/magic, PRE ports/datadir, disabled seeds, compressed fork heights, 100/70 BIP9 window, DigiDollar/oracle/MuSig2 at 600, and 35-slot oracle roster on port 12046 |
| Full unit suite | PASS: `./src/test/test_digibyte --log_level=error` ran 3407 test cases with no errors during PRE staging |
| Full functional suite | PASS on local PRE branch before release-note drafting |
| Fuzz target smoke run | PASS on local PRE branch before release-note drafting |
| Five-node Qt PRE rehearsal | PASS: five Qt nodes running from isolated temp datadirs, synced together, with localhost-only peers |
| CPU mining rehearsal | PASS: separate CPU miners proved sha256d, scrypt, Groestl/Myriad-Groestl, skein, and qubit block production before the Odocrypt gate; post-Odo mining continued to activation |
| DigiDollar GBT opt-in | PASS: `test/functional/digidollar_gbt_optin.py` proves legacy GBT stays safe while DigiDollar-aware GBT returns `default_oracle_commitment` and includes oracle-priced DD work |
| Stale oracle GBT cache | PASS: `test/functional/digidollar_oracle_gbt_stale_cache.py` proves stale oracle-bearing templates are rebuilt and stale mint/redeem work is stripped when no fresh bundle is available |
| Mining integration guide | PASS: `DIGIDOLLAR_MINING_INTEGRATION_GUIDE.md` documents the pool/cpuminer/GBT requirements for preserving `default_oracle_commitment` and links the public cpuminer, node-stratum-pool, and DigiHashV2 reference repos |
| DigiDollar BIP9 live state | PASS: observed `started`, `locked_in`, and `active`; live PRE chain reached height 605 with `enabled=true`, `status=active`, 35 oracle slots, and 7-signature quorum |
| DigiSwarm oracle wallet | PASS/PENDING: wallet loaded, configured, and authorized for slot 15 with the expected mainnet key; runtime start is pending wallet unlock with `walletpassphrase` |

Before publishing binaries, rerun and record:

```bash
make -j$(nproc)
./src/test/test_digibyte --show_progress
test/functional/test_runner.py --jobs=8
./src/test/fuzz/test_runner.py <fuzz-bin-dir> -l DEBUG <target-list>
./test_oracle_mainnet.sh
```

---

## Revert Plan

The PRE changes should stay isolated in an easy-to-revert commit or small commit stack.

After the oracle rehearsal is complete:

1. Save the PRE activation/oracle test results.
2. Shut down PRE nodes.
3. Preserve logs and oracle validation artifacts.
4. Revert the PRE isolation/fast-activation commit.
5. Prepare the final mainnet launch build with normal mainnet ports, normal mainnet peer discovery, and the chosen final activation parameters.

Do not ship this PRE configuration as the final public mainnet release.
