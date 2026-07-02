# DigiDollar Oracle Roster Update Plan

## Simple Introduction

DigiDollar needs a way to add or replace oracle keys after launch without shipping a new binary every time and without creating the same fork risk we just saw on testnet25.

The current code uses a static oracle roster from chain parameters. That means every node decides which oracle IDs and keys are valid from local software. If one node knows 18 active oracles and another node only knows 17, the same block can be valid to one node and invalid to the other. That is exactly the type of split we need to prevent.

The fix is to make oracle roster changes part of deterministic chain state.

In plain terms:

1. The initial oracle roster is hardcoded once.
2. Future additions, key rotations, and disables are recorded on-chain.
3. Existing active oracles approve those changes with signatures.
4. Every upgraded node independently validates the approval signatures.
5. After a delay, every upgraded node activates the same new roster at the same block height.

This means future keys do not need to be known when the software ships. The software only needs to know the rules for accepting roster updates.

## One-Minute Explanation

Think of the oracle roster as the official voter list.

Today, that voter list is hardcoded into the software. That is why changing it can split the chain: some nodes may have one voter list and other nodes may have another.

The safer design is:

```text
The current voter list signs the next voter list.
The signed change is recorded on-chain.
Everyone waits a fixed delay.
Then the new voter list becomes active.
```

So adding an oracle is not "developer adds a key to code." It becomes:

```text
current active oracles approve slot 17
chain records that approval
slot 17 activates later
slot 17 cannot sign before then
```

Rotating a lost or compromised key is the same:

```text
current active oracles approve a new key for slot 6
chain records that approval
old key works until the activation height
new key works after the activation height
```

The old key does not need to sign its own replacement. That is what solves lost keys.

## Current Code Validation

The current implementation is static and chainparams-driven:

- `src/kernel/chainparams.cpp`
  - Mainnet and testnet have 35 reserved oracle slots, 35 active pubkeys, and 7 required MuSig2 signers.
  - Regtest has 7 oracle slots and 4 required signers.
- `src/consensus/params.h`
  - `nOraclePubkeyCount` defines how many oracle IDs are consensus-active.
  - `nOracleTotalOracles` is used as the reserved bitmap/slot count. Mainnet/testnet currently use all 35 slots as active signers.
  - `nOracleConsensusRequired` defines the MuSig2 price bundle threshold.
  - `vOraclePublicKeys` contains the hardcoded x-only public keys.
- `src/oracle/bundle_manager.cpp`
  - `ValidateMuSig2Bundle()` decodes the v0x03 participation bitmap.
  - It rejects any signer where `oracle_id >= nOraclePubkeyCount`.
  - That means current v0x03 consensus is a contiguous active pubkey prefix, not a sparse dynamic active-slot set.
  - It computes the aggregate MuSig2 pubkey from the local roster and verifies the aggregate signature.
- `src/oracle/musig2_aggregator.cpp`
  - Aggregate pubkey creation reads pubkeys from local chainparams.
- `src/node/miner.cpp`
  - Miners stamp `OP_RETURN OP_ORACLE <0x03> <payload>` into coinbase when an oracle bundle is available.
- `src/validation.cpp`
  - Block validation calls `OracleDataValidator::ValidateBlockOracleData()`.
  - Price-dependent DigiDollar mint/redeem blocks require a valid MuSig2 oracle bundle after activation.
- `src/rpc/digidollar.cpp`
  - `createoraclekey` is local wallet key management and can work before DigiDollar activation.
  - `startoracle` is activation-gated, but today it checks only that a slot exists in chainparams. It needs a future height-aware roster eligibility check.
- `src/wallet/wallet.cpp`
  - Wallet oracle autostart loops stored oracle keys after activation. This also needs the same roster eligibility check.

Current tests cover the static roster well, but there is no reorg-aware dynamic roster state machine yet.

## Deep Code Audit Conclusions

Five focused code-audit passes found the same core issue from different angles: the current software treats the local static chainparams roster as the live consensus roster.

That assumption appears in all fork-critical layers:

- block validation rejects v0x03 signers using `oracle_id >= nOraclePubkeyCount`
- aggregate pubkey construction reads `vOraclePublicKeys` from chainparams
- aggregate pubkey cache is keyed by bitmap only
- `ExtractOracleBundle()` accepts v0x03 only and rejects unknown versions
- mining stamps only v0x03 price bundles
- P2P relay authorization uses `IsAuthorizedMuSig2OracleIdForRelay()`, which has no height, epoch, or roster-root input
- MuSig2 signing sessions are keyed mainly by epoch, not roster root
- pending oracle messages and attestations are keyed by oracle ID, not epoch/root
- price attestations and MuSig2 auth hashes do not commit to the active roster root
- wallet autostart loops fixed slots and starts any stored key after activation
- `createoraclekey` stores only one key per oracle slot
- `getoracles` reports consensus membership as a contiguous prefix of `nOraclePubkeyCount`
- reorg handling currently rolls back price cache only, not roster state

The audit conclusion is:

```text
Dynamic roster support must be a chainstate-backed consensus feature.
It cannot be a UI/RPC convention, a website list, a wallet setting, or a chainparams-only change.
```

The plan must therefore include:

- a first-class active roster snapshot for each target height/epoch
- root-bound v0x05 price bundles after registry activation
- root-bound P2P/signing messages after registry activation
- chainstate persistence for the roster snapshot and pending update
- explicit reorg/reindex/pruned-node behavior
- wallet key storage that can hold multiple keys for the same slot during rotation
- one shared eligibility predicate used by validation, mining, P2P, signing, RPC, and wallet autostart

## The Core Rule

An oracle ID or key must not be allowed to sign live oracle material until the chain state says that ID/key is active for the target block height or epoch.

This rule must apply everywhere:

- price attestations
- MuSig2 nonces
- MuSig2 partial signatures
- final price bundles
- `startoracle`
- wallet oracle autostart
- P2P relay and receive paths
- mining
- block validation
- RPC status display

There must be one shared predicate:

```text
IsOracleEligible(oracle_id, pubkey, target_height, target_epoch)
```

No code path should treat "slot exists in chainparams" as meaning "this oracle may sign now."

## V1 Non-Negotiable Invariants

These are the rules that keep future roster changes from forking upgraded nodes:

```text
same active chain -> same roster root -> same eligible oracle set -> same block validity result
```

V1 invariants:

- The hardcoded launch roster is sequence `0`.
- Every later roster state is derived only from accepted blocks on the active chain.
- A roster update is valid only against the exact `prev_roster_sequence` and `prev_roster_root` it names.
- A roster update never affects the block that contains it.
- Roster activation happens only on oracle epoch boundaries.
- A pending update must be known before any next-epoch signing prestart window for the epoch it changes.
- If a reorg changes the pending or active roster root, all signing sessions, partial signatures, pending messages, heartbeats, aggregate-key caches, and broadcast trackers for the old root are cleared.
- After registry activation, price-dependent DD mint/redeem blocks must use v0x05 root-bound price bundles.
- After registry activation, rootless v0x03 price bundles are legacy-only and must not satisfy live DD price requirements.
- v0x04 roster-update blocks must not contain price-dependent DD mint/redeem transactions.
- Transfers remain price-independent.
- Consensus does not use endpoint, DNS name, website name, or operator display name in V1.
- Wallet keys are local key management only. They do not make a slot active.
- A local oracle process can be configured before eligibility, but it must not sign or relay until its exact slot/pubkey is eligible for the target height/epoch/root.
- In registry mode, the mainnet/testnet price threshold remains 9 active roster signers; the eligible pool is every active oracle up to 35.
- Roster governance uses a stronger threshold than price signing.

## High-Level Flow

```text
New operator or existing oracle creates a fresh key
        |
        v
Current active oracles review the requested roster change
        |
        v
Enough current active oracles sign a RosterUpdate
        |
        v
RosterUpdate is broadcast to miners and peers
        |
        v
Miner includes signed RosterUpdate in OP_ORACLE coinbase data
        |
        v
Every node validates signatures using the current active roster
        |
        v
If valid, node stores the update as pending
        |
        v
At effective_height, every node activates the new roster
        |
        v
Only then can the new/rotated key sign price bundles
```

## What The Signing Bundle Is

A roster signing bundle is a signed permission slip.

It says:

```text
We, the current active oracle roster, approve this exact roster change.
It applies only to this chain.
It applies only after this future height.
It applies only if the current roster root still matches.
```

Example ADD_ORACLE bundle:

```text
operation:              ADD_ORACLE
target_slot:            17
new_pubkey:             03649d...
prev_roster_sequence:   4
prev_roster_root:       abc123...
effective_height:       50000
expiry_height:          51000
signed_by:              0, 2, 3, 4, 5, 7, 8, 9, 11, 12, 14, 16
signatures:             one Schnorr signature from each signer
```

Nodes validate it like this:

```text
1. Is this signed by enough current active oracles?
2. Are all signers valid under the current roster root?
3. Is the target slot valid?
4. Is the new key valid and not duplicated?
5. Is the activation height far enough in the future?
6. Has the update expired?
7. Does applying it create a valid new roster?
```

If yes, the node stores it as pending. It does not activate immediately.

This keeps the mental model simple:

```text
current roster signs the next roster
next roster waits
then next roster becomes current
```

## Recommended Design

Use an on-chain oracle registry.

The registry starts from the hardcoded launch roster. After that, it is updated by valid on-chain roster updates signed by the current active roster.

Consensus state tracks:

```text
roster_sequence
roster_root
active_slots[0..34]
pubkey_for_slot[0..34]
pending_update, if any
```

The active roster for a block is no longer read directly from static chainparams. It is computed from:

```text
hardcoded launch roster + validated on-chain roster updates
```

## V1 MVP Scope

V1 should do only the smallest set of things needed to safely add, rotate, or disable oracle keys without a new binary.

V1 operations:

```text
ADD_ORACLE
ROTATE_KEY
DISABLE_ORACLE
```

Optional only if needed:

```text
ENABLE_ORACLE
```

V1 does not include:

```text
treasury votes
general governance proposals
changing oracle policy by vote
changing price threshold by vote
changing governance threshold by vote
combined price/update containers
endpoint/name metadata on-chain
```

V1 keeps on-chain oracle records simple:

```text
v0x03 = legacy static-roster price bundle, accepted only before registry activation
v0x04 = standalone roster update bundle
v0x05 = root-bound price bundle, required after registry activation
```

V1 rule:

```text
A v0x04 roster update block cannot also be a price-dependent DigiDollar mint/redeem block.
```

This avoids needing a combined container immediately while still making live price signing roster-root aware.

V1 success criteria:

- add a new oracle into an inactive slot
- rotate a lost key without the old key signing
- rotate a compromised key through emergency delay
- disable a compromised or dead oracle
- enforce activation delay
- reject early signing
- survive restart, reindex, and reorg
- keep existing DigiDollar mint/send/redeem behavior unchanged

## Possible V2 Scope

V2 can extend the system after V1 is proven on regtest/testnet.

Possible V2 features:

- `ENABLE_ORACLE` if not included in V1
- combined oracle container so one block can carry both price and roster update data
- richer `getoracles` history and audit RPCs
- signed off-chain operator metadata
- optional advisory proposal records
- optional treasury proposal records
- broader governance that includes voters beyond oracle operators

V2 should still avoid letting oracles alone control unrelated protocol rules unless that power is explicitly intended.

Treasury and general governance should be separate from V1 because they create much larger incentives and attack surface.

## One-Time Activation

This mechanism itself is a consensus upgrade.

For mainnet, it should be shipped before DigiDollar mainnet activation. That way mainnet does not launch with a static roster that later needs dangerous hardcoded key changes.

Recommended mainnet path:

1. Add the oracle registry mechanism.
2. Activate the mechanism with the DigiDollar deployment or before it.
3. Launch DigiDollar with registry-aware oracle validation.
4. Use on-chain roster updates for future adds, rotates, and disables.

After this one-time activation, adding or swapping oracle keys does not require a new binary.

Important limitation:

An old binary that does not understand the registry mechanism cannot be made to accept future registry blocks. No design can make obsolete consensus software understand rules it does not have. The goal is to avoid future forks among upgraded nodes after the registry mechanism is active.

## Roster Capacity And Quorums

Keep the 35-slot model.

Recommended constants:

These are proposed mainnet/testnet registry consensus parameters. Regtest can keep smaller test-only thresholds. The implementation should store these in `Consensus::Params` and set them in chainparams, not as wallet/RPC policy.

```text
MAX_ORACLE_SLOTS = 35
MAX_ACTIVE_ORACLES = 35
MIN_ACTIVE_ORACLES = 13
PRICE_SIGNATURES_REQUIRED = 9
MAX_ROSTER_OPERATIONS_PER_UPDATE = 1
MAX_PENDING_ROSTER_UPDATES = 1
NORMAL_GOVERNANCE_DELAY = 5760 blocks      # about 24 hours at 15 seconds/block
EMERGENCY_GOVERNANCE_DELAY = 480 blocks    # about 2 hours at 15 seconds/block
ROSTER_UPDATE_MIN_EXPIRY_WINDOW = 1440 blocks
ROSTER_UPDATE_MAX_FUTURE_DELAY = 40320 blocks
```

Recommended price model:

```text
eligible price signer pool = all active oracles in the registry roster, up to 35
mainnet/testnet price threshold = 9 active-oracle signatures
```

This means normal price signing is:

```text
9 of 17, if 17 oracles are active
9 of 21, if 21 oracles are active
9 of 32, if 32 oracles are active
9 of 35, if 35 oracles are active
```

The current MuSig2 signing session already chooses the actual threshold participants from nonce-submitters by epoch-seeded ordering. Dynamic rosters should preserve that behavior, but the eligible pool must come from the active roster root instead of static chainparams. Today, v0x03 block validation still uses the static contiguous pubkey prefix `oracle_id < nOraclePubkeyCount`; sparse active slots require the registry-aware v0x05 path.

Roster governance should be stronger than price signing:

```text
governance_required = floor((active_oracle_count * 2) / 3) + 1
```

Examples:

```text
17 active oracles -> 12 signatures required
18 active oracles -> 13 signatures required
35 active oracles -> 24 signatures required
```

This means:

- price updates remain live with 9 signatures
- roster changes require a stronger supermajority
- 9 keys cannot rewrite the roster

Recommended emergency governance:

```text
emergency_governance_required = floor((active_count * 3) / 4) + 1
```

Emergency updates should be limited to:

```text
ROTATE_KEY
DISABLE_ORACLE
```

Emergency updates should not be allowed for `ADD_ORACLE`; adding a new signer should always use the normal delay.

Threshold examples:

```text
active_count   normal >2/3   emergency >3/4
17             12            13
18             13            14
21             15            16
35             24            27
```

`MIN_ACTIVE_ORACLES = 13` is intentional. With a 9-signature price threshold and greater-than-two-thirds roster governance, 12 active oracles would make roster governance exactly 9 signatures, the same threshold as price signing. V1 should keep roster governance strictly stronger than price signing, so it should reject `DISABLE_ORACLE` updates that would drop active count below 13. Prefer `ROTATE_KEY` over `DISABLE_ORACLE` when possible.

## Recommended V1 Parameter Choices

These choices are deliberately conservative and simple.

### Maximum Oracles

Use the existing 35-slot model:

```text
maximum registered slots = 35
maximum active oracles = 35
```

Slot IDs are stable. Do not compact or renumber slots after a disable.

### Price Signing

Keep price signing aligned with current DigiDollar oracle behavior after registry activation:

```text
all active roster oracles are eligible for the epoch, up to 35
the final MuSig2 bundle needs 9 valid active signers
the signing session selects the 9 participants from nonce-submitters by epoch-seeded ordering
```

There is no separate fixed 17-oracle committee in V1. If 21 oracles are active, the eligible pool is 21. If 32 are active, the eligible pool is 32. On mainnet/testnet, the threshold remains 9. Regtest uses its own consensus threshold for tests.

### Roster Governance

Normal roster updates:

```text
threshold = floor((active_count * 2) / 3) + 1
delay = 5760 blocks, about 24 hours
```

Emergency roster updates:

```text
threshold = floor((active_count * 3) / 4) + 1
delay = 480 blocks, about 2 hours
allowed operations = ROTATE_KEY or DISABLE_ORACLE only
```

### Changes Per Update

V1 should allow exactly one roster operation per update:

```text
MAX_ROSTER_OPERATIONS_PER_UPDATE = 1
MAX_PENDING_ROSTER_UPDATES = 1
```

Why:

- easier to audit
- easier to explain
- simpler reorg handling
- no ambiguity about operation ordering
- avoids overlapping pending roots

If batching is needed later, add it as a new version after V1 has proven itself on regtest/testnet.

### When An Update Takes Effect

The update does not take effect when it is signed.

It takes effect only after it is mined into a valid block and the activation delay passes.

Normal update:

```text
included_height = H
minimum_effective_height = H + 5760
effective_height = first oracle epoch boundary >= minimum_effective_height
```

Emergency update:

```text
included_height = H
minimum_effective_height = H + 480
effective_height = first oracle epoch boundary >= minimum_effective_height
```

Activation should happen at an oracle epoch boundary because live MuSig2 signing sessions are epoch-scoped. The current code uses 40-block oracle signing epochs on mainnet/testnet/regtest, about 10 minutes at 15 seconds per block.

### Expiry

A signed update should expire if it is not mined quickly enough.

Recommended:

```text
expiry_height >= signed_at_height + 1440 blocks
expiry_height <= signed_at_height + 40320 blocks
```

This prevents an old signed update from being mined months later after social context has changed.

## On-Chain Recording Format

Current blocks use:

```text
OP_RETURN OP_ORACLE <0x03> <price_bundle_payload>
```

For the first implementation, introduce two new oracle record versions:

```text
OP_RETURN OP_ORACLE <0x04> <roster_update_payload>
OP_RETURN OP_ORACLE <0x05> <price_bundle_v2_payload>
```

Keep v1 simple:

```text
v0x03 = legacy static-roster price bundle before registry activation
v0x04 = roster update bundle
v0x05 = root-bound price bundle after registry activation
```

Do not put a price bundle and roster update in the same block in v1. After registry activation, price-dependent DigiDollar mint/redeem blocks must carry v0x05, not v0x03.

Why:

- current consensus permits at most one `OP_ORACLE` output per coinbase
- the existing parser accepts v0x03 price bundles only, so v0x04/v0x05 are part of the one-time registry consensus upgrade
- a simple v0x04 record is easier to validate, test, and explain
- roster updates are rare, so they do not need to share a block with a price bundle

A v0x04 roster-update block must not contain price-dependent DigiDollar mint or redeem transactions, because that block carries no price bundle. Price-independent DD transfers can continue because transfers do not require an oracle price.

Future versions can add a combined container that carries both a price bundle and a roster update if that becomes necessary. Do not start there.

Standalone v0x04 payload:

```text
payload_version:          uint8 = 1
operation_type:           uint8
target_slot:              uint8
new_pubkey_compressed:    33 bytes, zero-filled when unused
expected_old_pubkey_hash: uint256
prev_roster_sequence:     uint32
prev_roster_root:         uint256
effective_height:         int32
expiry_height:            int32
flags:                    uint32
signer_bitmap_len:        uint8
signer_bitmap:            bytes
signatures:               64 bytes each, in ascending signer ID order
```

The parser must reject:

- unknown payload versions
- trailing bytes
- payloads above a strict size limit
- malformed signer bitmaps
- signature count that does not match the signer bitmap

## Price Bundle Handling

The code audit changed the original idea here.

Do not silently reuse rootless v0x03 price bundles after dynamic rosters are active.

Why:

- current v0x03 signs only chain, epoch, price, and timestamp
- runtime MuSig2 messages do not include `roster_root`
- signing sessions are currently keyed mainly by epoch
- a next-epoch prestart session from the old root can overlap a roster activation boundary
- after ADD_ORACLE, many old signers are still valid signers, so a rootless signature can be ambiguous

Therefore V1 should introduce a root-bound price bundle at the same time as the registry:

```text
v0x03 = legacy static-roster price bundle before registry activation
v0x05 = root-bound registry price bundle after registry activation
```

v0x05 signs:

```text
Hash("DigiDollar/OraclePriceBundle/v2",
     chain_genesis_hash,
     roster_root,
     epoch,
     price_micro_usd,
     timestamp)
```

This is not only an on-chain parser change. The runtime signing hash currently used by `ComputeOracleMessageHash` and the MuSig2 session context construction must be changed for v0x05 so nonces, context IDs, partial signatures, and the final aggregate signature all commit to `roster_root`.

v0x05 payload:

```text
roster_sequence:          uint32
roster_root:              uint256
signer_bitmap_len:        uint8
signer_bitmap:            bytes
epoch:                    int32
price_micro_usd:          uint64
timestamp:                int64
aggregate_sig:            64 bytes
```

Validation:

1. Look up the active roster at this block height.
2. Verify `roster_sequence` and `roster_root` match that active roster.
3. Decode signer bitmap against `MAX_ORACLE_SLOTS`.
4. Verify every signer is active in the roster for this block height/epoch.
5. Verify signer count is at least `PRICE_SIGNATURES_REQUIRED`.
6. Compute aggregate pubkey from the active roster pubkeys.
7. Verify aggregate signature against the V2 price hash.
8. Verify epoch, price range, and timestamp exactly as current code does.

Aggregate pubkey cache key must include:

```text
(chain_genesis_hash, roster_root, signer_bitmap)
```

It must not be keyed only by bitmap. The same bitmap under a rotated key set can represent a different aggregate pubkey.

v0x03 remains useful only for:

- pre-registry testnet/regtest compatibility
- tests that explicitly exercise the legacy static-roster path
- historical blocks before the registry activation height

Once the registry is active, a price-dependent block that carries v0x03 must be rejected with a clear consensus error.

`ConnectBlock` integration must distinguish oracle record types before mutating price cache state. A v0x04 roster-update record is not price data and must not be treated as a block price even if parsing code populates generic fields. Only a valid v0x05 price bundle should update the live price cache after registry activation.

## Roster Update V1

Use individual Schnorr signatures for roster governance.

This is intentionally simple:

- no MuSig2 ceremony required for rare governance changes
- every signer is visible
- each signature is independently verifiable
- the payload is larger, but roster updates are rare and coinbase data can handle it

The roster update payload is the v0x04 payload listed above.

Fields:

```text
version:                  uint8 = 1
operation_type:           uint8
target_slot:              uint8
new_pubkey_compressed:    33 bytes, only for ADD_ORACLE and ROTATE_KEY
expected_old_pubkey_hash: uint256, required for ROTATE_KEY and DISABLE_ORACLE
prev_roster_sequence:     uint32
prev_roster_root:         uint256
effective_height:         int32
expiry_height:            int32
flags:                    uint32
signer_bitmap_len:        uint8
signer_bitmap:            bytes
signatures:               64 bytes each, in ascending signer ID order
```

Operation types:

```text
0x01 = ADD_ORACLE
0x02 = ROTATE_KEY
0x03 = DISABLE_ORACLE
0x04 = ENABLE_ORACLE, optional / V2 unless needed
```

V1 should allow only one operation per update. Batching can be added later if really needed.

The signed hash:

```text
Hash("DigiDollar/OracleRosterUpdate/v1",
     chain_genesis_hash,
     operation_type,
     target_slot,
     new_pubkey_compressed,
     expected_old_pubkey_hash,
     prev_roster_sequence,
     prev_roster_root,
     effective_height,
     expiry_height,
     flags)
```

Do not sign endpoints or display names in consensus v1.

Consensus should care about:

- slot ID
- compressed pubkey
- active or disabled status
- activation height

Endpoint, operator name, website, and other metadata should be non-consensus. They can be distributed by RPC, DNS, website, signed metadata, or a later non-critical registry layer. Changing a hostname should not require a consensus roster update.

## Roster Update Validation

When a block contains `OP_ORACLE <0x04>`, validate it against the active roster before applying it.

Rules:

1. The registry mechanism must be active.
2. The update block height must be `<= expiry_height`.
3. `effective_height` must be at least `block_height + delay`.
4. Normal updates use `NORMAL_GOVERNANCE_DELAY`.
5. Emergency updates may use `EMERGENCY_GOVERNANCE_DELAY`, but require a stronger threshold.
6. `prev_roster_sequence` must equal the node's current roster sequence.
7. `prev_roster_root` must equal the node's current roster root.
8. There must be no other pending roster update.
9. `target_slot` must be between 0 and 34.
10. `signer_bitmap` must decode cleanly against 35 slots.
11. Every signer must be active in the current roster.
12. Signer count must meet `governance_required`.
13. Signatures must verify against the current pubkey for each signer.
14. Signature order must match ascending signer ID order.
15. No duplicate signers.
16. No duplicate active pubkeys after the update.
17. Active oracle count must stay within safe bounds.
18. The resulting roster root must be deterministic.
19. The block must not contain a price-dependent DD mint or redeem transaction.

Operation-specific rules:

### ADD_ORACLE

```text
target slot must be inactive or empty
new pubkey must be valid compressed secp256k1
new pubkey must not already exist in any active slot
active count after update must be <= 35
```

### ROTATE_KEY

```text
target slot must be active
expected_old_pubkey_hash must match the current slot pubkey
new pubkey must be valid compressed secp256k1
new pubkey must not already exist in any active slot
active count does not change
old key is valid before effective_height
new key is valid at and after effective_height
```

The old key does not need to sign its own rotation. This solves lost keys and compromised keys.

### DISABLE_ORACLE

```text
target slot must be active
expected_old_pubkey_hash must match the current slot pubkey
active count after disable must be >= minimum safe active count
disabled slot cannot sign price or governance after effective_height
```

This is for emergency removal when a key is compromised and no replacement key is ready.

### ENABLE_ORACLE

```text
target slot must be disabled but already have a valid pubkey
active count after enable must be <= 35
```

This is optional. If not needed, leave it out of v1 and use ADD/ROTATE only.

## Applying Updates

Roster updates must not affect the same block that contains them.

Block validation order should be:

```text
Before validating block H:
    build a candidate roster snapshot with any pending update whose effective_height <= H
    do not mutate global roster state yet

Validate block H price bundle:
    use the candidate roster active at H if the block has v0x05

Validate any roster update inside block H:
    use the same candidate roster active at H
    require update.effective_height > H
    stage it as pending if valid

After every other block check succeeds:
    commit price cache, candidate roster activation, and any staged pending update
```

This mirrors the current price-cache safety pattern in `ConnectBlock`: do not mutate global oracle state until the block is otherwise valid.

## Reorg And Reindex Rules

The registry must be reorg-aware.

On connect:

```text
validate update
store pending update
activate pending update when height reaches effective_height
write undo data
clear aggregate pubkey caches for changed roster roots
```

On disconnect:

```text
undo any update mined in the disconnected block
undo any update that became active at the disconnected height
restore previous roster root and sequence
clear aggregate pubkey caches
clear signing sessions tied to the disconnected roster root
```

On startup/reindex:

```text
normal startup: load roster snapshot tied to the coins best block
reindex/reindex-chainstate: start from hardcoded launch roster
reindex/reindex-chainstate: replay active-chain roster update records in height order
rebuild current roster state and pending update state
```

Chosen V1 storage design:

```text
Persist a small roster snapshot and compact roster undo/history in chainstate.
Rebuild all runtime caches.
```

Persist:

- active roster snapshot at the chainstate best block
- `roster_sequence`
- `roster_root`
- 35-slot table: pubkey, active/disabled/empty status, activation/retirement metadata
- pending update, if any
- compact undo entries for blocks that queued or activated roster updates
- snapshot best block hash and height

Rebuild:

- aggregate MuSig2 pubkey cache
- signing sessions, nonces, contexts, partial signatures
- pending P2P oracle messages and heartbeats
- broadcast/dedup trackers
- display metadata
- price cache, using the same kind of rebuild model as current oracle price cache

Do not store consensus roster state in:

- block index DB, because it survives `-reindex-chainstate` and has the wrong lifecycle
- optional indexes, because they can lag validation
- wallet DB, because wallets are local and non-consensus
- website/operator metadata, because that is not part of block validity

Preferred implementation:

```text
Store roster state in the chainstate LevelDB namespace and flush it with the same logical best block as coins.
```

The roster write must be crash-consistent with the coins best-block transition. In practice that means either extending the chainstate batch path that writes `DB_BEST_BLOCK`, or using an equally explicit roster best-block marker plus recovery code. An independent roster write that can get ahead of or behind the UTXO best block is not acceptable.

AssumeUTXO and snapshot chainstates must be treated as separate chainstates. Roster snapshots, pending updates, undo state, and root-scoped caches must belong to the specific `Chainstate` being validated, not a global singleton.

If a separate small roster DB is used instead, it must have an explicit best-block marker and crash recovery. On startup, if the roster snapshot best block does not match the coins best block, the node must replay blocks from the roster snapshot to the coins best block or fail loudly before validating new blocks. Pruned nodes are the reason this cannot rely on an unbounded rescan from genesis.

Do not extend `CBlockUndo` unless implementation proves it is necessary. The existing undo file format is UTXO-only; changing it adds compatibility risk. Chainstate-local roster undo/snapshots are smaller and easier to reason about.

## Local Storage And RPC Visibility

There should not be a loose human-edited "oracle roster cache file."

The consensus roster state should live in chainstate storage:

```text
<datadir>/<network>/chainstate/
```

That directory is a LevelDB database, not a single readable text file. It is the same lifecycle as the UTXO chainstate: wiped by `-reindex-chainstate`, rebuilt by block validation, and tied to a best block.

Recommended chainstate DB records:

```text
DB_ORACLE_ROSTER_SNAPSHOT  -> active roster snapshot at chainstate best block
DB_ORACLE_ROSTER_PENDING   -> pending roster update, if any
DB_ORACLE_ROSTER_UNDO      -> compact undo entries for queued/activated updates
DB_ORACLE_ROSTER_HISTORY   -> optional confirmed update index for RPC/audit
```

Runtime-only caches should stay in memory:

```text
aggregate MuSig2 pubkeys
signing sessions
pending P2P oracle messages
heartbeats
broadcast/dedup trackers
```

Wallet oracle private keys stay in the wallet database, not chainstate. That is local key material and must not be consensus state.

RPC must make the chainstate roster visible enough that operators can audit it without reading LevelDB files.

Required read RPCs:

```text
node RPC:   getoracleroster [height]
node RPC:   getoracleupdates [count]
node RPC:   getpendingoracleupdate
node RPC:   decodeoracleupdate <hex>
node RPC:   getoraclepolicy
wallet RPC: listoraclekeys
```

Historical roster RPCs need explicit retention behavior. If `getoracleroster <height>` or `getoracleupdates` asks for history the node did not persist or cannot reconstruct on a pruned node, the RPC should return a clear "history unavailable" error rather than guessing from current state.

`getoracleroster` should return:

```text
chain_height
roster_sequence
roster_root
active_count
max_slots
min_active_oracles
price_signatures_required
price_eligible_active_count
normal_governance_required
emergency_governance_required
current_epoch
next_epoch
slots[]
pending_update
```

Each slot should show:

```text
slot
status: empty | active | disabled | pending_add | pending_rotate | pending_disable
pubkey
pubkey_hash
active_since_height
retired_height
price_signing_eligible_now
eligible_now
eligible_next_epoch
operator_metadata, if locally known
```

`getoraclepolicy` should return the constants and formulas:

```text
max_slots
max_active_oracles
min_active_oracles
max_pending_updates
max_operations_per_update
price_signatures_required
price_signer_pool
normal_delay_blocks
emergency_delay_blocks
normal_governance_formula
emergency_governance_formula
expiry_min_blocks
expiry_max_blocks
```

`getpendingoracleupdate` should clearly distinguish:

```text
local candidate update waiting to be mined
confirmed on-chain update waiting for effective_height
no pending update
```

This matters because a submitted roster update is not a normal wallet transaction. Until it is mined into a coinbase OP_ORACLE record, it has no consensus effect.

## Roster Update Pool And Mining

Roster updates are not regular transactions.

They are signed data objects that miners can place in the coinbase oracle output.

Recommended flow:

```text
submitoracleupdate <signed_update_hex>
    validates the update against the current active roster
    stores it in a local in-memory roster-update pool
    relays it to peers that support registry messages
    returns accepted_for_mining_pool=true
```

Mining logic:

```text
if a valid roster update candidate exists:
    if chain already has a pending roster update:
        do not mine it
    if candidate is expired or no longer matches current root:
        drop it
    if block contains price-dependent DD mint/redeem:
        do not include v0x04 in that block
    otherwise:
        put v0x04 roster update in coinbase OP_ORACLE output
```

Once mined:

```text
ConnectBlock validates the v0x04 record
chainstate stores it as confirmed pending
RPC shows effective_height and blocks_remaining
```

If a node restarts before the update is mined, the local in-memory candidate pool can be empty. The operator can resubmit the same signed update as long as it has not expired and still matches the current roster root.

## How Adding A New Oracle Works

```text
1. Candidate operator runs:
   createoraclekey <slot>

2. Candidate shares only the compressed public key.

3. Existing active oracles verify:
   - operator identity
   - slot assignment
   - key format
   - endpoint readiness, off-chain
   - that the key is not reused

4. An active oracle builds ADD_ORACLE update:
   operation = ADD_ORACLE
   target_slot = unused slot, for example 17
   new_pubkey = candidate compressed pubkey
   prev_roster_root = current root
   effective_height = current_height + delay
   expiry_height = current_height + expiry window

5. Current active oracles sign the update hash.

6. Once enough signatures are collected, the update is broadcast.

7. A miner includes it in OP_ORACLE v0x04 coinbase data.

8. Nodes validate the update and queue it.

9. At effective_height, slot 17 becomes active.

10. Only then can the new oracle run startoracle and sign live price material.
```

If the new oracle signs early, upgraded nodes reject the message and do not relay it.

## Operator Command Flow

These commands describe the V1 registry RPCs, not the current RC43 RPC surface. Today `createoraclekey` accepts only `oracle_id` and wallet storage allows one key per slot. V1 must either add an optional purpose argument like `add`/`rotate`, or keep `createoraclekey <slot>` and record the purpose through a separate metadata/update command. Either way, wallet storage must key oracle private keys by `(oracle_id, pubkey_hash)` so a slot can hold old, active, candidate, and retired keys through rotation and reorgs.

Example ADD_ORACLE flow:

```bash
# Candidate operator, local only. Does not activate anything.
createoraclekey 17 add

# Existing oracle builds an unsigned update from the current roster root.
createoracleupdate add 17 03649d750bcad5b42b3dd0f11c8d98d62ed5afd515cd986663f81c35f086e58d47

# Each current active oracle signs the exact same update hash.
signoracleupdate <update_hex> <signing_oracle_id>

# Anyone combines enough signatures.
combineoracleupdate <update_hex> <sig1> <sig2> ...

# Anyone broadcasts/submits it.
submitoracleupdate <signed_update_hex>
```

Example ROTATE_KEY flow:

```bash
createoraclekey 6 rotate
createoracleupdate rotate 6 <new_compressed_pubkey> <expected_old_pubkey_hash>
signoracleupdate <update_hex> <signing_oracle_id>
combineoracleupdate <update_hex> <sig1> <sig2> ...
submitoracleupdate <signed_update_hex>
```

Example DISABLE_ORACLE flow:

```bash
createoracleupdate disable 6 <expected_old_pubkey_hash>
signoracleupdate <update_hex> <signing_oracle_id>
combineoracleupdate <update_hex> <sig1> <sig2> ...
submitoracleupdate <signed_update_hex>
```

Inspection commands:

```bash
getoracleroster
getoracleroster <height>
getoracleupdates
decodeoracleupdate <update_hex>
listoraclekeys
getoracles
```

These commands are examples, not final RPC spelling. The required behavior is more important than the exact names.

## How Key Rotation Works

Use the same slot ID.

Example: oracle 6 lost a key or wants scheduled rotation.

```text
1. Oracle 6 generates a new key:
   createoraclekey 6

2. Oracle 6 shares the new compressed pubkey with current active oracles.

3. Current active oracles build ROTATE_KEY update:
   operation = ROTATE_KEY
   target_slot = 6
   expected_old_pubkey_hash = hash(current slot 6 pubkey)
   new_pubkey = fresh key
   effective_height = current_height + delay

4. Governance threshold signs it.

5. Miner includes it on-chain.

6. Nodes queue it.

7. Before effective_height:
   slot 6 old key is valid
   slot 6 new key is invalid

8. At and after effective_height:
   slot 6 old key is invalid
   slot 6 new key is valid
```

The old key is not required to sign the rotation. That is what makes lost-key recovery possible.

## Wallet Key Storage For Rotation

Current wallet storage maps one key to one `oracle_id`. That is not enough for dynamic roster updates.

V1 wallet storage must allow multiple keys for the same oracle slot:

```text
(oracle_id, pubkey_hash) -> private key + metadata
```

This requires a wallet DB schema addition/migration for unencrypted and encrypted oracle keys. Existing single-key records should remain readable, but new registry-aware code must not overwrite a slot's previous key when a candidate rotation key is created.

Suggested metadata:

```text
slot
pubkey
created_for_operation
created_height
effective_height, if known
retired_height, if known
status: candidate | active | retired | disabled | archived
```

Why this is required:

- during ROTATE_KEY, the old key remains valid before `effective_height`
- the new key becomes valid at `effective_height`
- a reorg can temporarily make the old key valid again
- operators need to recover their stored public key after creation

`startoracle` and wallet autostart should not start "whatever key is stored for slot 6." They should:

```text
resolve target roster snapshot
find the local key whose pubkey matches slot 6 in that snapshot
start/sign only with that exact key
```

Old retired keys should be kept through a safe reorg retention window, then may be archived or removed by an explicit local cleanup command.

## How A Compromised Key Is Handled

Preferred path:

```text
ROTATE_KEY with emergency delay
```

If no replacement key is ready:

```text
DISABLE_ORACLE with emergency delay
```

Emergency updates should require a stronger threshold than normal updates.

Example:

```text
normal governance:    floor(2N/3) + 1
emergency governance: floor(3N/4) + 1
```

For 17 active oracles:

```text
normal:    12 signatures
emergency: 13 signatures
```

The compromised oracle does not need to sign its own removal.

Important reality:

If enough oracle keys are compromised to meet the governance threshold, software cannot distinguish that from legitimate governance. This is a threshold security system. The mitigation is high governance threshold, public delay, monitoring, and diverse oracle operators.

## Runtime Gates That Must Change

Current code must stop asking only:

```text
oracle_id < nOraclePubkeyCount
```

It must ask:

```text
IsOracleEligible(oracle_id, pubkey, target_height, target_epoch)
```

Required call sites:

- `src/oracle/bundle_manager.cpp`
  - `ValidateMuSig2Bundle`
  - `AddOracleMessage`
  - `AddConsensusAttestation`
  - `ComputeConsensusValues`
  - `ComputeConsensusValuesForOracles`
- `src/oracle/musig2_aggregator.cpp`
  - aggregate pubkey creation must take an explicit roster/root
- `src/oracle/signing_orchestrator.cpp`
  - nonce generation
  - context proposal verification
  - partial signature verification
  - completed session storage
- `src/protocol.cpp`
  - MuSig2 P2P authorization
- `src/net_processing.cpp`
  - price message auth
  - nonce auth
  - context auth
  - partial signature auth
  - heartbeat auth
- `src/rpc/digidollar.cpp`
  - `startoracle`
  - `getoracles`
  - `getoraclepubkey`
- `src/wallet/wallet.cpp`
  - oracle key autostart
- `src/node/miner.cpp`
  - price bundle stamping
- `src/validation.cpp`
  - block validation and connect/disconnect side effects

## Root-Bound Runtime Messages

The code audit found that changing only on-chain bundle validation is not enough.

The live signing network also needs roster-root awareness, otherwise old-root sessions, next-epoch prestarter messages, or stale partial signatures can survive across a roster boundary.

After registry activation, root-bound message versions should be used for:

- price attestations
- consensus price evidence
- MuSig2 nonces
- MuSig2 context proposals
- MuSig2 partial signatures
- oracle heartbeats, if heartbeats remain tied to signer identity

Each root-bound runtime message should include or sign:

```text
chain_genesis_hash
target_epoch
target_height or epoch_start_height
roster_sequence
roster_root
oracle_id or proposer_id
attempt_id / context_id, when applicable
payload-specific fields
```

Preferred P2P compatibility shape:

```text
keep old message handlers for pre-registry legacy operation
add new V2/root-bound message structs or message names for registry operation
reject or ignore rootless oracle signing messages after registry activation
```

Do not silently append fields to an existing wire message if that causes old peers to deserialize garbage or disconnect unexpectedly. A clear versioned message path is easier to test and easier to explain.

Implementation must cover both the serialized messages and the relay plumbing:

- `protocol.h` / `protocol.cpp` message names
- `CInv` oracle inventory types and string names
- message dedup/cache keys
- `GETORACLES` response contents
- relay authorization, which must check the target roster/root instead of only static chainparams

Every receive path should:

1. Check the message target epoch is current or next epoch only.
2. Resolve the roster snapshot for that target epoch.
3. Require the message `roster_root` to match the snapshot.
4. Verify the signer is eligible in that snapshot.
5. Verify the signature against the snapshot pubkey.
6. Deduplicate, buffer, relay, or ingest under a key that includes roster root.

Recommended runtime cache keys:

```text
aggregate pubkey cache:      (chain_hash, roster_root, signer_bitmap)
completed MuSig2 session:    (epoch, roster_root, attempt_id)
pending price message:       (epoch, roster_root, oracle_id)
pending attestation:         (epoch, roster_root, oracle_id)
nonce/context/partial state: (epoch, roster_root, attempt_id, context_id)
broadcast/dedup tracker:     (message_type, epoch, roster_root, message_hash)
```

Next-epoch prestart rule:

```text
If the next epoch has a pending roster-root change, the next root must be known before any prestart message for that epoch is accepted.
If the root is not known, delay next-epoch signing instead of accepting rootless or old-root messages.
```

On reorg or roster activation:

```text
clear all sessions and caches tied to roots that are no longer active or pending
```

## Preventing Forks

This plan prevents future roster-change forks among upgraded nodes because every upgraded node gets the same answer from chain data:

```text
same block history -> same roster root -> same active keys -> same bundle validation result
```

It prevents the testnet25 failure mode because a new oracle cannot simply be enabled by changing local chainparams and signing.

The new oracle must first be activated by a valid on-chain roster update. Until then:

```text
startoracle/signing refuses to use it for live oracle signing
P2P refuses it
bundle building ignores it
block validation rejects it
```

## Attack Vectors And Mitigations

### Old Software

Risk:

Old software that does not understand the registry mechanism can still fork at the one-time registry activation.

Mitigation:

Activate the registry before mainnet DigiDollar launch and require operators to upgrade before DigiDollar activates. Future roster updates then use the registry and do not need new binaries.

### Rogue Miner Includes Fake Update

Risk:

A miner includes a roster update that adds their own oracle.

Mitigation:

Nodes verify governance signatures against the current active roster. Without enough valid signatures, the block is invalid.

### Replay Across Chains

Risk:

A valid testnet roster update is replayed on mainnet.

Mitigation:

The signed hash includes `chain_genesis_hash`.

### Replay On Same Chain After Another Update

Risk:

An old valid update is replayed after the roster has changed.

Mitigation:

The signed hash includes `prev_roster_root` and `prev_roster_sequence`. The update is valid only against that exact prior roster.

### Update Mined Too Late

Risk:

An old signed update is mined months later.

Mitigation:

The payload includes `expiry_height`.

### New Oracle Signs Early

Risk:

A staged oracle signs before activation.

Mitigation:

All signing, relay, mining, and validation checks use `IsOracleEligible` for the target height/epoch.

### Aggregate Pubkey Cache Poisoning

Risk:

The same bitmap is reused after key rotation, causing a cached aggregate pubkey from the old roster to be reused.

Mitigation:

Cache aggregate pubkeys by `(roster_root, bitmap)`.

### Duplicate Pubkeys

Risk:

One key is registered into multiple slots.

Mitigation:

Reject roster updates that create duplicate active pubkeys.

### Endpoint Hijack

Risk:

An attacker tries to control consensus by changing DNS/endpoint metadata.

Mitigation:

Do not make endpoint metadata consensus-critical in v1. Consensus only validates slot, pubkey, active status, and signatures.

### Governance Capture

Risk:

A supermajority of oracle keys signs a malicious update.

Mitigation:

This is the core trust boundary. Use a higher governance threshold than price threshold, public activation delay, monitoring, and diverse operators. If the governance threshold is compromised, no purely automatic mechanism can know the update is malicious.

### Too Many Lost Keys

Risk:

Enough oracle keys are lost that the remaining operators cannot meet governance threshold.

Mitigation:

Normal one-key loss is handled because the lost key does not need to sign its own rotation. Catastrophic loss below governance threshold requires a coordinated software upgrade or pre-defined social recovery path. Do not pretend this can be solved automatically without introducing another trusted authority.

### Fixed 9-Signature Price Threshold

Risk:

With a 9-signature price threshold, any 9 compromised active oracle keys can produce a valid price bundle, whether the active roster has 17, 21, 32, or 35 members.

Mitigation:

This is the current DigiDollar price-signing model and should be treated as a deliberate V1 policy choice, not an accidental roster-update side effect. Roster governance must be stronger than price signing so 9 keys cannot add, rotate, or disable oracle slots. If the community later wants price security to scale with active count, that should be a separate explicit consensus change, not hidden inside roster updates.

### Miner Censorship

Risk:

Miners refuse to include a valid roster update.

Mitigation:

Roster updates are small and can be rebroadcast until included. Any miner can include a valid signed update. The update is not controlled by one developer, website, or oracle.

## Implementation Map

Recommended new files:

```text
src/oracle/roster.h
src/oracle/roster.cpp
src/test/oracle_roster_update_tests.cpp
src/test/fuzz/oracle_roster_update.cpp
src/test/fuzz/oracle_roster_state_machine.cpp
```

Core classes:

```cpp
struct COracleRosterSlot {
    uint8_t slot;
    CPubKey pubkey;
    bool active;
};

struct COracleRoster {
    uint32_t sequence;
    uint256 root;
    std::array<COracleRosterSlot, 35> slots;
};

struct COracleRosterUpdate {
    uint8_t version;
    uint8_t operation_type;
    uint8_t target_slot;
    CPubKey new_pubkey;
    uint256 expected_old_pubkey_hash;
    uint32_t prev_roster_sequence;
    uint256 prev_roster_root;
    int32_t effective_height;
    int32_t expiry_height;
    uint32_t flags;
    std::vector<unsigned char> signer_bitmap;
    std::vector<std::array<unsigned char, 64>> signatures;
};

class OracleRosterManager {
public:
    const COracleRoster& GetRosterForHeight(int32_t height) const;
    bool IsOracleEligible(uint8_t id, const CPubKey& key, int32_t height, int32_t epoch) const;
    bool ValidateUpdate(const COracleRosterUpdate& update, int32_t block_height, std::string& error) const;
    bool QueueUpdate(const COracleRosterUpdate& update, int32_t block_height);
    void ActivatePendingUpdates(int32_t height);
    void DisconnectBlock(int32_t height);
};
```

Existing code changes:

- Replace static `nOraclePubkeyCount` checks in live paths with `OracleRosterManager`.
- Keep chainparams as the launch roster and fallback for pre-registry regtest fixtures.
- Make `ValidateMuSig2Bundle` accept an explicit roster/root.
- Add v0x04 OP_ORACLE roster-update parsing.
- Add v0x05 OP_ORACLE root-bound price-bundle parsing and creation.
- Keep v0x03 only as the pre-registry legacy/static-roster price path.
- Add `submitoracleupdate` RPC for a fully signed update.
- Add `decodeoracleupdate` RPC for inspection.
- Add `createoracleupdate` RPC for ADD/ROTATE/DISABLE unsigned update construction.
- Add `signoracleupdate` RPC for active operators to sign an update.
- Add `combineoracleupdate` RPC to merge enough signatures into one submit-ready update.
- Add `getpendingoracleupdate` RPC to show local candidate and confirmed pending update state.
- Add `getoraclepolicy` RPC to show consensus roster constants and formulas.
- Add `listoraclekeys` wallet RPC to show locally stored oracle pubkeys and status.
- Add `getoracleroster` RPC to show current and pending roster state.
- Add `getoracleupdates` RPC to show confirmed/pending roster update history.
- Extend `getoracles` to show:
  - configured slot
  - active now
  - pending activation
  - disabled
  - price signing eligible now
  - eligible to sign next epoch

### Complete V1 Code Checklist

Consensus parameters and chainparams:

- Keep `vOraclePublicKeys` and `vOracleNodes` as launch-roster data only.
- Add registry activation parameters to `Consensus::Params` and chainparams.
- Add governance delays, expiry windows, min-active rule, max pending update count, max operations per update, and registry price threshold as consensus parameters.
- Reconcile the duplicate oracle-count fields in `DigiDollar::ConsensusParams` (`activeOracles`, `oracleThreshold`) with the registry roster. After registry activation, status/health/UI code should read live active count and threshold from the roster manager/consensus params instead of stale launch constants.
- Stop treating `nOraclePubkeyCount` as the live active set after registry activation.
- Preserve old/static behavior only before registry activation and in explicit legacy tests.

Oracle primitives:

- Add `COracleRosterUpdate`.
- Add v0x04 serialization/deserialization with strict trailing-byte rejection.
- Add v0x05 price bundle serialization/deserialization.
- Add deterministic roster root hashing.
- Preserve current epoch-seeded participant ordering for choosing the 9 MuSig2 signers from nonce-submitters.

Roster manager:

- Build launch roster from chainparams.
- Resolve `OracleRosterSnapshot` for current height, next epoch, and historical block validation.
- Validate ADD/ROTATE/DISABLE update rules.
- Queue pending updates.
- Activate due updates at epoch-boundary heights.
- Produce compact undo records for queued and activated updates.
- Disconnect blocks and restore prior root/sequence/pending state.
- Clear root-scoped caches on root changes.

Chainstate storage:

- Store roster snapshot and pending update in chainstate-owned storage.
- Flush roster state with the same logical best block as coins.
- Rebuild from chainparams plus active-chain updates on reindex.
- Support pruned restart from persisted snapshot.
- Refuse to proceed if roster snapshot and coins best block are inconsistent and cannot be repaired.

Block validation:

- Replace `CheckMuSig2OracleBundleVersion()` with registry-aware oracle-record validation.
- Update `ConnectBlock` oracle extraction so v0x04 roster updates are not interpreted as price bundles.
- Before validating block `H`, apply any pending update whose effective height is `H`.
- Validate v0x05 price bundles against the roster active at `H`.
- Validate v0x04 roster updates against the roster active at `H`.
- Reject v0x04 blocks that contain price-dependent DD mint/redeem transactions.
- Reject v0x03 as a live price bundle after registry activation.
- Keep all roster mutations out of `fJustCheck` and failed-block paths.
- Validate historical/recent oracle bundle checks against the roster active at each checked block, not the current tip roster.

Mining:

- Stamp v0x05 price bundles after registry activation.
- Stamp v0x04 roster updates only when selected from a valid pending update pool.
- Never combine v0x04 with price-dependent DD mint/redeem transactions in V1.
- Ensure completed signing sessions match the target block's `roster_root`.
- If no valid v0x05 is ready, remove price-dependent DD transactions exactly as current price-bundle failure handling does.

MuSig2 aggregation and signing:

- Remove implicit `Params()` pubkey lookup from aggregate key construction.
- Pass explicit roster pubkeys and root into aggregation.
- Key aggregate cache by `(chain_hash, roster_root, signer_bitmap)`.
- Key sessions by `(epoch, roster_root, attempt_id)`.
- Update `ComputeOracleMessageHash` for v0x05 so the signed message commits to `roster_root`.
- Make session context IDs commit to `roster_root`.
- Filter local running oracles through target roster eligibility before nonce generation, context proposal, and partial signing.
- Clear sessions when `SetEpochSelectionSeed()` or roster root changes invalidate an already-created session.

P2P and protocol:

- Add root-bound V2 oracle signing messages or a versioned oracle message envelope.
- Update `protocol.h`, `protocol.cpp`, `CInv` handling, message strings, and oracle message dedup keys.
- Keep legacy rootless messages only for pre-registry operation.
- Verify received root-bound messages against the target roster snapshot before relay and before ingestion.
- Soft-drop unknown future-root messages or buffer them only under strict size/time limits.
- Bind price attestations, nonces, contexts, partial signatures, and heartbeats to chain, epoch, root, and signer.
- Stop serving rootless pending messages through `GETORACLES` after registry activation.

Wallet and RPC:

- Allow `createoraclekey` before activation as local key management.
- Store multiple oracle keys per slot, keyed by `(oracle_id, pubkey_hash)` or equivalent.
- Add wallet DB migration/read paths for old single-key oracle records and encrypted oracle key records.
- Keep old and new slot keys during rotation through a reorg-safe retention window.
- Add `listoraclekeys` or extend `getoraclepubkey` so operators can recover stored pubkeys without starting an oracle.
- Make `startoracle` choose the key matching the active/pending roster pubkey for the target epoch.
- Make wallet autostart skip stale, rotated-out, disabled, or future-only keys.
- Register wallet-scoped commands in wallet RPC registration and node-scoped roster/policy/update-pool commands in node RPC registration.
- Update `getoracles` to show sparse active sets, disabled slots, pending activation, current price-signing eligibility, and next-epoch eligibility.

Legacy helpers and tests:

- Either remove or root-gate older helper paths that accept raw MuSig2 nonces/partials without auth.
- Add tests proving no rootless runtime message can feed a registry-era signing session.
- Update tests and health/UI assumptions that currently treat the 9-signature price threshold as a strict majority or at least 50% of active oracles. Registry V1 deliberately allows 9-of-21, 9-of-32, and 9-of-35 price signing; only roster governance must be a supermajority.

## Test Plan

Unit tests:

- static launch roster root is deterministic
- v0x04 payload round-trips and signing hash is stable
- v0x04 parser rejects unknown payload versions, trailing bytes, oversized payloads, malformed bitmaps, and signature-count mismatches
- v0x05 payload round-trips and signs `roster_root`
- v0x05 rejects wrong root, wrong sequence, inactive signers, and rootless replay
- ADD_ORACLE succeeds for slot 17 and slot 34
- ADD_ORACLE rejects slot 35
- ADD_ORACLE rejects duplicate pubkey
- ADD_ORACLE rejects already active slot
- ROTATE_KEY switches key exactly at effective height
- old key valid before effective height
- old key invalid after effective height
- new key invalid before effective height
- new key valid after effective height
- DISABLE_ORACLE removes signer exactly at effective height
- update with wrong previous root is rejected
- update with wrong sequence is rejected
- update with too few governance signatures is rejected
- update with duplicate signer signatures is rejected
- update with malformed pubkey is rejected
- update mined after expiry is rejected
- update with effective height too soon is rejected
- second pending update is rejected
- aggregate pubkey cache changes when roster root changes
- session context id changes when roster root changes
- governance threshold boundaries are correct for 17, 18, and 35 active oracles
- v0x04 update has no same-block effect
- legacy v0x03 is rejected after registry activation

Functional tests:

- node mines valid ADD_ORACLE update and all nodes activate same roster
- new oracle cannot start before activation
- new oracle starts after activation
- new oracle cannot sign price bundle before activation
- new oracle can sign price bundle after activation
- key rotation works through restart
- key rotation works through reindex
- key rotation rolls back on reorg
- disabled oracle cannot sign after effective height
- old branch and new branch produce deterministic roster roots
- DD mint/redeem continues to validate across roster activation
- DD transfer remains price-independent during oracle outage
- v0x04 plus DD mint/redeem is rejected
- v0x04 plus DD transfer is accepted
- restart preserves active and pending roster state
- `-reindex-chainstate` rebuilds the same roster root
- pruned restart validates post-update blocks from persisted roster state
- `verifychain` and `TestBlockValidity` do not mutate live roster state
- rootless P2P oracle messages are ignored/rejected after registry activation

Fuzz tests:

- roster update payload parser
- v0x05 price payload parser
- roster state machine connect/disconnect/reorg
- signer bitmap mutation across roster roots
- duplicate pubkey and duplicate signer mutations
- update replay against wrong chain/root/sequence
- P2P oracle message auth against active/future/disabled keys
- wallet oracle key storage and stale-key rejection
- v0x03/v0x04/v0x05 coinbase mix rules
- aggregate cache separation across roster roots

## Recommended Rollout

For testnet:

1. Stop adding active oracle keys through chainparams.
2. Either reset testnet or coordinate all operators onto one branch.
3. Revert any accidental active reserve oracle if the goal is only staging.
4. Implement registry mechanism on regtest first.
5. Deploy to testnet with a clear activation height.
6. Test add, rotate, disable, reorg, restart, and 35-slot expansion.

For mainnet:

1. Ship registry mechanism before DigiDollar activation.
2. Keep initial launch roster hardcoded.
3. Allow `createoraclekey` before activation.
4. Keep `startoracle` disabled until DigiDollar and registry are active.
5. After activation, use signed on-chain roster updates for all future changes.

## Key Points Summary

- Hardcoding future keys does not solve future unknown keys.
- Adding an active oracle is consensus-visible.
- Changing an oracle pubkey is consensus-visible.
- The chain must become the source of truth for future roster changes.
- Current active oracles approve changes with signed roster updates.
- Roster updates are mined on-chain and validated by every node.
- Updates activate after a delay, never in the same block.
- The old key does not need to sign its own rotation.
- New keys can be generated any time, but cannot sign until activated.
- V1 MVP is only ADD_ORACLE, ROTATE_KEY, and DISABLE_ORACLE.
- V1 keeps v0x03 as legacy-only, adds v0x04 standalone roster updates, and uses v0x05 root-bound price bundles after registry activation.
- Treasury, broad governance, policy voting, and combined containers are V2 or later.
- Use 35 registered slots with all active slots eligible for price signing.
- Price bundles require 9 active roster signers, for example 9-of-17, 9-of-21, 9-of-32, or 9-of-35 depending on active count.
- Use a stronger governance threshold, such as greater than 2/3 of active oracles.
- Cache aggregate pubkeys by chain hash, roster root, and bitmap.
- Make the roster manager reorg-aware.
- After this mechanism is active, future oracle additions and key swaps do not require new binaries.
