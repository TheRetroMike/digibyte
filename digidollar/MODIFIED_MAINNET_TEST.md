# Modified Mainnet DigiDollar Oracle Test

This file documents the temporary `v9.26.1-pre` modified-mainnet test setup.

This is not a normal user release and not the final DigiDollar mainnet launch. It is a one-time isolated rehearsal chain that keeps the mainnet identity needed for real oracle-key testing, while changing the local network and activation timing so the final DigiDollar oracle path can be tested quickly.

## Goal

Test final DigiDollar mainnet oracle behavior under mainnet-like conditions without joining public DigiByte mainnet.

This PRE build is meant to prove:

- final 35-slot mainnet oracle roster loads correctly;
- 7-of-35 MuSig2 oracle quorum is configured correctly;
- DigiDollar, oracle rules, and MuSig2 bundle format activate together;
- mainnet oracle wallets and keys work in a mainnet key/address context;
- the BIP9 activation path works from `defined` to `started` to `locked_in` to `active`;
- nodes remain isolated from the existing public mainnet network.

## Mainnet Identity Kept

These values intentionally stay the same as real mainnet:

- chain type: `main`
- genesis hash: `0x7497ea1b465eb39f1c8f507bc877078fe016d6fcb6dfad3a64c98dcc6e1e8496`
- message magic: `fa c3 b6 da`
- mainnet address formats
- final mainnet oracle public keys

Keeping genesis, magic bytes, and address/key context is the point of this test. It lets oracle operators test the actual mainnet key environment.

## Isolation Changes

The PRE chain must not touch normal mainnet data or connect to public mainnet peers.

Changed for the PRE build:

- data directory suffix: `mainnet-pre`
- default P2P port: `12046`
- default RPC port: `14046`
- default onion target port: `14146`
- public mainnet P2P port `12024` is blocked for outbound PRE use;
- DNS seeds are disabled;
- fixed seeds are disabled;
- public-mainnet checkpoints are bypassed for PRE replay after genesis;
- assumevalid and assumeutxo are disabled for the PRE rehearsal.

Local test nodes should use fresh temporary directories, not `~/.digibyte`.

## Compressed Fork Heights

The modified mainnet chain compresses historical DigiByte fork points so the side chain reaches modern rules before DigiDollar activation testing.

| Height | Rule point |
| --- | --- |
| `0` | SegWit active |
| `0` | Taproot always active |
| `1` | BIP34 |
| `1` | BIP65 |
| `1` | BIP66 |
| `1` | CSV |
| `10` | compressed old reward-era boundary 1 |
| `67` | DigiShield difficulty boundary |
| `80` | compressed old reward-era boundary 2 |
| `100` | MultiAlgo |
| `200` | always-update difficulty / MultiShield-style boundary |
| `400` | work computation update / DigiSpeed-style boundary |
| `450` | ReserveAlgoBits |
| `490` | algo swap boundary |
| `500` | Odocrypt |
| `600` | DigiDollar minimum activation gate |
| `600` | oracle minimum activation gate |
| `600` | MuSig2 bundle-format minimum gate |

## DigiDollar BIP9 Settings

PRE uses fast testnet-style signaling on modified mainnet:

- deployment bit: `23`
- signaling window: `100` blocks
- threshold: `70` blocks
- minimum activation gate: `600`
- timeout: `1830297600` (`2028-01-01 00:00:00 UTC`)

Expected successful PRE path:

```text
defined -> started -> locked_in -> active
```

`failed` is the alternate BIP9 terminal state if signaling does not lock in before timeout. A successful activation rehearsal should not go through `failed`.

Height `600` is the PRE minimum activation gate. In this controlled PRE chain we mine enough signaling blocks to lock in before that gate, so it should become active there. For real public mainnet, the configured height must be described as the earliest possible activation gate, not a guaranteed activation height.

## Oracle Configuration

The PRE chain uses the final mainnet oracle roster:

- oracle slots: `35`
- active oracle keys: `35`
- quorum: `7` signatures
- bundle format: MuSig2 `v0x03`
- PRE endpoints use port `12046`

For the local five-node test, only the local DigiSwarm oracle wallet is required to prove the operator path works. The PRE chain still carries the full 35-key mainnet roster.

## Mining Test Shape

Mining is real mainnet-style PoW for the isolated side chain. Easy difficulty is not enabled.

The local rehearsal uses five Qt nodes and CPU mining across the DigiByte algorithms. The intended algorithm proof is:

- start with scrypt;
- after the first 100 blocks, mine sha256d;
- after 50 more blocks, mine skein;
- switch back to scrypt;
- mine qubit;
- mine Groestl before the Odocrypt gate and Odocrypt after height `500` as needed for full multi-algo coverage.

The purpose is to prove the modified mainnet chain can progress through the compressed historical fork schedule and still activate DigiDollar/oracle/MuSig2 correctly.

## Safety Rules

- Do not run this as a normal wallet release.
- Do not use a normal mainnet data directory.
- Do not copy normal mainnet `peers.dat`.
- Do not manually add public mainnet peers on `12024`.
- Do not publish this configuration as final mainnet.
- Keep all PRE changes in an easy-to-review, easy-to-revert commit or small commit stack.

After the rehearsal is complete, preserve logs/results, shut down PRE nodes, and revert the PRE isolation/fast-activation changes before preparing the final public mainnet launch build.
