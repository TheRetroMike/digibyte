# DigiDollar Mint Privacy and PQC Hardening Plan
*Updated: 2026-04-28*
*Status: **Research / proposal only — no code changes.** Document describes a future MINT v2 format that does not exist in the current codebase. Treat all "Mint format v2", "Phase A–E", and "P2MR" content as forward-looking design, not deployed behavior.*
*Scope: Remove owner pubkey exposure from MINT OP_RETURN without breaking DigiDollar behavior*

> Code reality (RC33 / `feature/digidollar-v1`):
> - `src/digidollar/txbuilder.cpp:355` still emits `OP_RETURN <"DD"> <txType=1> <ddAmount> <lockHeight> <lockTier> <ownerXOnlyPubKey>` for mints — **the legacy v1 format described below is the only format produced today**.
> - `src/digidollar/validation.cpp:920+` still extracts the owner x-only pubkey from OP_RETURN and reconstructs the expected collateral Taproot script using the BIP-341 NUMS internal key from `src/digidollar/scripts.h`.
> - The `sendmanydigidollar` RPC introduced a new TRANSFER-side OP_RETURN layout (`<"DD"> <txType=2> <output_count> <amount1>...<amountN>` — `txbuilder.cpp:743`); this is unrelated to the mint privacy concern but worth noting since it touches OP_RETURN parsing.
> - No `mint_format_version` field exists on chain. No alternative collateral commitment is implemented.
>
> Anyone implementing this proposal must add v2 alongside v1, keep v1 valid forever, and follow the security properties listed in the "Security properties the replacement must preserve" section below.

---

## TL;DR

Right now DigiDollar mint transactions intentionally expose the owner's **x-only secp256k1 public key** in the MINT `OP_RETURN`.

That was added for a valid security reason: validators currently use that pubkey to reconstruct the expected collateral Taproot output and verify the collateral vault was built with the **BIP-341 NUMS internal key** instead of an attacker-controlled internal key.

The problem is that this creates a long-lived on-chain quantum target. Once the owner pubkey is published in the mint transaction, an attacker can archive it forever and wait for practical Shor attacks.

### My recommendation

**Do not keep the owner pubkey in MINT `OP_RETURN`.**

Instead, move DigiDollar collateral mints to a design where validators can verify the vault structure **without any owner pubkey being committed on chain at mint time**.

The cleanest safe path is:

1. **Add a new mint format/version** for DigiDollar collateral outputs.
2. **Replace the current P2TR(NUMS internal key + owner pubkey in tapscript) collateral construction** with a script-committed construction that does not require publishing the owner pubkey in `OP_RETURN`.
3. **Validate the collateral by parsing and matching a committed script/template**, not by reconstructing it from an owner pubkey taken from metadata.
4. **Keep old mint format fully valid for existing chain history and wallet restore.**
5. Longer term, align DigiDollar collateral with the repo PQC direction: **P2MR/script-only commitment first, PQC leaf later**.

The only way to truly guarantee the owner pubkey is **never exposed during mint** is to stop depending on a plain secp256k1 pubkey in the mint output definition itself.

---

## What the code does today

### 1. MINT builder currently writes the owner x-only pubkey into `OP_RETURN`

In `src/digidollar/txbuilder.cpp`, mint metadata is built as:

```cpp
// Format: OP_RETURN <"DD"> <txType> <ddAmount> <lockHeight> <lockTier> <ownerXOnlyPubKey>
```

The current code explicitly serializes:

- `"DD"`
- `txType = 1`
- `ddAmount`
- `lockHeight`
- `lockTier`
- `ownerXOnlyPubKey`

The code comment says this is needed so validators can reconstruct the expected collateral output and verify the mint used the NUMS internal key.

Relevant file:
- `src/digidollar/txbuilder.cpp`

### 2. Validation currently depends on that pubkey

In `src/digidollar/validation.cpp`, mint validation tracks:

- `ownerXOnlyPubKeyData`
- `hasOwnerPubKey`
- `actualCollateralScript`
- `ddOpReturnCount`
- `collateralOutputCount`

For MINTs, validation currently:

1. extracts the owner x-only pubkey from `OP_RETURN`
2. reconstructs expected collateral script using:
   - `ddAmount`
   - `lockHeight`
   - `ownerKey`
   - `GetCollateralNUMSKey()`
   - oracle keys
3. compares reconstructed script to actual output 0 collateral script

If `OP_RETURN` owner pubkey is missing, the mint is rejected.

Relevant file:
- `src/digidollar/validation.cpp`

### 3. The reason this exists is legitimate

The code is protecting against a real attack:

- if an attacker can create the collateral Taproot output with **their own internal key** instead of NUMS,
- they can potentially use **key-path spend** to bypass CLTV/tapscript restrictions,
- redeem or steal collateral early,
- and leave unbacked DigiDollars in circulation.

So the current design is not stupid. It is a real security patch. It just trades one security problem for another.

### 4. Wallet restore does **not** appear to require the owner pubkey from chain today

Wallet rescan and position reconstruction in `src/wallet/digidollarwallet.cpp` pull position data from MINT metadata using:

- DD amount
- unlock height
- lock tier
- tx type

I did **not** find wallet restore depending on the owner pubkey from `OP_RETURN` for position reconstruction.

The wallet stores owner keys locally in `dd_owner_keys` / encrypted owner key storage, and uses those local keys for signing transfers and redemptions.

That matters because it means we can remove the on-chain owner pubkey from the MINT metadata **without automatically breaking restore**, as long as:

- tx type remains recoverable
- dd amount remains recoverable
- unlock height remains recoverable
- lock tier remains recoverable
- old transactions remain supported

Relevant files:
- `src/wallet/digidollarwallet.cpp`
- `src/digidollar/health.cpp`

---

## Why the current design is a PQC problem

The repo PQC docs are directionally right here.

From `DGB_PQC_PLAN.md` and `DGB_PQC_LANDSCAPE.md`:

- exposed pubkeys are the attack surface
- P2MR is attractive because it removes the at-rest pubkey exposure
- commit-reveal only helps with spend-time front-running, not with pubkeys already published years earlier

That means the current DigiDollar mint design is the opposite of what we want for a quantum-aware system:

- **today:** owner pubkey is exposed at mint and lives on chain forever
- **desired:** owner identity/spend authorization should remain hidden until spend time, or ideally be hash-committed / PQC-committed instead

If a public key is visible at mint time, the attacker gets an effectively unlimited offline attack window.

---

## NUMS: what it is, where it is defined, and where it is used

### In this codebase

NUMS is defined in:
- `src/digidollar/scripts.h`

Specifically:
- `COLLATERAL_NUMS_POINT_BYTES`
- `GetCollateralNUMSKey()`

The comment correctly identifies it as the **BIP-341 NUMS point** used as a Taproot internal key for DigiDollar collateral outputs.

The bytes match the standard BIP-341 Taproot NUMS point, not some DigiDollar-specific custom point.

### In DigiDollar

It is used here:
- `src/digidollar/txbuilder.cpp` when building collateral vaults
- `src/digidollar/validation.cpp` when reconstructing expected collateral scripts
- tests under `src/test/*` heavily validate the NUMS assumption

### In Bitcoin / DigiByte protocol generally

NUMS is a **Taproot design concept**, not a separate opcode or transaction type.

In Taproot, when someone wants a script-only construction and does **not** want a usable key-path spend, they can use an unspendable or NUMS internal key. That avoids giving anyone a known private key for the internal key.

So:

- **Bitcoin protocol:** NUMS is part of Taproot practice/design under BIP-341, especially for script-only or provably-unspendable internal key constructions.
- **DigiByte protocol:** same Taproot concept applies wherever Taproot is used.
- **This repo specifically:** I found explicit NUMS usage in DigiDollar collateral code and in `src/test/miniscript_tests.cpp`, but not broad general-purpose wallet/protocol use elsewhere.

Important nuance:

Using a NUMS internal key only prevents **key-path spending through that internal key**. It does **not** by itself solve the PQC problem if the actual spend condition still embeds a standard secp256k1 owner pubkey in the script tree or reveals it at mint time through metadata.

That is exactly the issue here.

---

## The actual root cause

The root problem is not really `OP_RETURN` itself.

The root problem is this validation strategy:

> "To prove the collateral output used NUMS and the right owner, consensus reconstructs the full expected Taproot output from owner pubkey + lock params + oracle set, and compares it to the actual collateral output."

That forces the owner pubkey to be available to every validator at mint validation time.

Today the system solves that by putting the owner pubkey in `OP_RETURN`.

If we want the owner pubkey never exposed during mint, then the validation strategy itself must change.

---

## What solutions are actually on the table

## Option A, Minimal patch: remove owner pubkey from `OP_RETURN` and trust local wallet state

### Verdict
**Reject.**

### Why
Consensus cannot depend on wallet-local data. Nodes validating a mint must be able to validate it from the transaction and consensus state alone.

This would either:
- break cross-node validation, or
- silently disable the NUMS defense

Unsafe.

---

## Option B, Replace owner pubkey in `OP_RETURN` with a hash of the owner pubkey

### Verdict
**Not sufficient by itself.**

### Why
A hash in `OP_RETURN` does reduce direct at-rest exposure, but current validation reconstructs the exact collateral output, which requires the **actual owner pubkey**, not just a hash.

With only a hash, validators cannot reconstruct the current Taproot tree and cannot prove the output used the right owner key unless the actual pubkey is also present somewhere else in the mint transaction.

That means this only works if the collateral script format is redesigned.

### Good news
Hashed commitments are still useful in a redesigned format.

---

## Option C, Keep Taproot but make collateral validation parse the committed script tree instead of reconstructing it from `OP_RETURN`

### Verdict
**Possible in principle, but not clean with current P2TR output format.**

### Why
A P2TR output commits only to the **output key**. It does **not** reveal the script tree at creation time.

So at mint-validation time, the node sees only:

- `OP_1 <32-byte output key>`

It does **not** see:

- the script leaves
- control block
- internal key

unless those are revealed in a spend witness later.

So for an unspent newly-created collateral output, a validator cannot parse the hidden tapscript tree from the output alone.

That is exactly why the current implementation reconstructs the script externally from metadata.

Conclusion: with plain P2TR, you cannot both:

1. hide the full tree, and
2. fully validate the hidden tree at mint time,

unless enough reconstruction data is provided elsewhere.

That reconstruction data is currently the owner pubkey in `OP_RETURN`.

---

## Option D, Move DigiDollar collateral from P2TR-with-hidden-tree to a script-committed output format that reveals enough structure for mint-time validation without exposing owner pubkey

### Verdict
**This is the right direction.**

### Why
If the mint output itself commits directly to a script or Merkle root that can be validated without a raw owner pubkey in `OP_RETURN`, consensus no longer needs the owner pubkey in metadata.

This is the same strategic direction as the PQC docs: **remove the at-rest pubkey exposure from the output design**.

There are two realistic subpaths.

---

### Option D1, Short/medium term: DigiDollar-specific script-only collateral output

Use a collateral output format where the mint output directly commits to a known script template rather than a Taproot output key that hides the tree.

Conceptually:

- commit to `lockHeight`
- commit to `lockTier` if needed
- commit to oracle/redemption rules
- commit to **hash(owner spend key)**, not raw owner pubkey
- enforce spend via reveal-at-redemption witness

This gives validators enough mint-time visibility to verify:

- the right lock exists
- the right redemption conditions exist
- the owner authorization is committed
- no key-path bypass exists

without requiring the owner pubkey in `OP_RETURN`.

#### Pros
- solves the specific DigiDollar mint leak cleanly
- can be designed to preserve current wallet/restore logic with metadata unchanged except for removing pubkey
- no dependence on future global soft fork if implemented as an already-supported script form

#### Cons
- likely larger and uglier than Taproot
- may reduce privacy and efficiency versus Taproot
- may require more wallet/script plumbing than current design
- if it still uses plain secp256k1 script pubkey reveal at spend, it only removes **at-rest** exposure, not long-term post-spend quantum risk

This is acceptable as an interim mitigation, but it is not the best long-term architecture.

---

### Option D2, Preferred long term: P2MR-style collateral for DigiDollar

This matches the direction already described in `DGB_PQC_PLAN.md`.

Instead of Taproot `OP_1 <output_key>`, use a Merkle-root committed output style where the output commits to script structure without exposing a Taproot internal key or key-path spend path.

Conceptually:

```text
scriptPubKey: OP_2 <32-byte merkle_root>
```

The mint output commits only to the script tree / Merkle root.

Then DigiDollar collateral can be:

- script-only
- no key-path bypass at all
- no owner pubkey required in `OP_RETURN`
- owner authorization committed via hashed script leaf or future PQC leaf

#### Why this is the best cryptographic fit

It directly fixes the thing we are trying to fix:

- no exposed owner pubkey in metadata
- no exposed Taproot internal/output pubkey at rest
- no key-path bypass class
- clean upgrade path to PQC leaves later

#### Tradeoff
This likely requires base protocol work, probably a soft fork if implemented as witness v2 / P2MR.

So this is the **correct architecture**, but not the fastest local patch.

---

## Option E, Keep current collateral shape but add a second on-chain commitment that binds the owner key without revealing it

### Verdict
**Only works if validation changes to verify the commitment rather than reconstruct from raw pubkey.**

Examples:
- owner pubkey hash in `OP_RETURN`
- owner leaf hash in `OP_RETURN`
- full script tree hash in `OP_RETURN`

The cleanest version of this is:

- `OP_RETURN` stores a **collateral template hash** instead of owner pubkey
- mint output must match a deterministic collateral commitment format
- validation checks commitment consistency rather than rebuilding from owner pubkey

This can work, but only if the output format exposes enough information or the commitment target is standardized enough that consensus can verify it without hidden inputs.

That again pushes us toward script-committed or P2MR-like outputs.

---

## Recommended implementation plan

## Recommendation

Use a **two-track plan**:

### Track 1, practical DigiDollar fix
Implement a **new DigiDollar collateral mint format** that removes the owner pubkey from `OP_RETURN` and stops requiring it for consensus validation.

### Track 2, protocol-aligned future state
Align that format with the repo’s broader **P2MR then PQC leaf** roadmap so we do not redesign this twice.

---

## Concrete design recommendation

### Mint format v1, current legacy format
Current mint metadata:

```text
OP_RETURN <"DD"> <1> <ddAmount> <lockHeight> <lockTier> <ownerXOnlyPubKey>
```

Keep this valid forever for backward compatibility.

### Mint format v2, new format
Proposed metadata:

```text
OP_RETURN <"DD"> <1> <format=2> <ddAmount> <lockHeight> <lockTier> <collateralCommitment>
```

Where `collateralCommitment` is **not** the owner pubkey.

It should be one of:

1. **preferred:** Merkle root / script commitment for the collateral program
2. **acceptable interim:** hash of a fully-specified collateral script template

The important point is that consensus validates against a **commitment to the redemption program**, not against a plain owner pubkey.

---

## Validation rules for v2

For MINT v2:

1. require exactly one collateral output
2. require exactly one DD token output
3. require exactly one DD `OP_RETURN`
4. parse:
   - tx type
   - format version
   - DD amount
   - lock height
   - lock tier
   - collateral commitment
5. verify the collateral output matches the committed DigiDollar collateral template/rules
6. verify the collateral format has **no attacker-controlled key-path bypass**
7. do **not** require raw owner pubkey from `OP_RETURN`

For legacy v1:
- preserve existing validation path exactly as-is

This avoids breaking historical transactions and existing wallets.

---

## What the collateral commitment should bind

Whatever v2 uses, the commitment must bind **all consensus-relevant mint semantics**:

- owner authorization commitment
- lock height
- DD amount if used in script semantics
- oracle threshold / oracle set commitment
- ERR / normal redemption branches
- leaf versions if Taproot-like structure remains
- any tweak / root / tree ordering assumptions

If any of these are omitted from the commitment, validation may accept a collateral output that is structurally similar but semantically weaker.

The red-team tests in this repo already show why complete binding matters.

---

## How to avoid breaking current functionality

## 1. Backward compatibility

Do **not** mutate the interpretation of historical mints.

Instead:
- keep current parser/validator for old format
- add a new MINT format discriminator for new mints

That means:
- existing on-chain mints still validate
- existing wallets can still restore old positions
- migration can be gradual

## 2. Wallet restore and rescans

Current wallet restore extracts from `OP_RETURN`:
- DD amount
- unlock height
- lock tier

Keep those fields present in v2.

Do **not** make wallet restore depend on hidden witness data or off-chain lookup.

## 3. Transfers

Transfers appear to depend on locally stored owner keys, `dd_owner_keys`, wallet descriptor state, and P2TR output-key matching for DD token UTXOs.

Since the requested fix is about **MINT owner pubkey leakage in collateral metadata**, we should avoid changing the DD token transfer path in the first pass.

Recommendation:
- leave DD token transfer outputs unchanged for now
- fix collateral mint privacy first

That isolates the change surface.

## 4. Redemptions

If the new collateral format causes pubkey reveal only at redemption time, that is still materially better than reveal-at-mint.

If Jared wants stronger protection later, add the PQC-doc’s optional next step:
- **commit-reveal redemption flow** for high-value vaults

That addresses spend-time front-running without reopening the mint leak.

---

## Security properties the replacement must preserve

Any acceptable replacement must preserve all of these:

1. **No key-path bypass** of collateral lock conditions
2. **Consensus-validatable** from tx + chain state, not wallet-local data
3. **Deterministic commitment binding** so reconstruction mismatch attacks fail
4. **Exactly-one collateral / DD / metadata output rules** already enforced in validation
5. **Wallet restore viability** using on-chain fields that remain present
6. **No new ambiguity** around owner identity, lock tier, or oracle set

If a proposal fails any one of those, it is not ready.

---

## What I would implement first

If the goal is to fix the quantum target problem without breaking current functionality, my order would be:

### Phase A, research/design freeze
- define a `mint_format_version`
- define exact v2 metadata serialization
- define exact commitment object being hashed/committed
- define exact compatibility behavior for old vs new mints

### Phase B, implement v2 validation path first
- add parser for new metadata format
- add collateral commitment verification
- keep legacy path untouched
- add failure cases before wallet changes

### Phase C, implement v2 tx builder and wallet creation path
- stop writing owner x-only pubkey into MINT `OP_RETURN`
- emit commitment instead
- keep DD amount / lock height / lock tier fields for rescan compatibility

### Phase D, wallet/rescan compatibility tests
- mint new v2 positions
- restore wallet from seed/descriptors
- rescan
- verify:
  - positions restored
  - DD token balance restored
  - transfer still works
  - redemption still works
  - old v1 positions still restore and redeem

### Phase E, optional future hardening
- add commit-reveal for redemption
- align with P2MR / witness-v2 if broader protocol work proceeds
- later add PQC leaf / PQC signature mode

---

## Specific recommendation on architecture choice

If you want the blunt answer:

### Best immediate safe answer
**Add MINT v2 and replace the owner pubkey field with a collateral commitment field.**

But that only makes sense if the collateral format is also redesigned so consensus can verify the commitment without needing the raw owner pubkey.

### Best long-term answer
**Move DigiDollar collateral toward P2MR-style script-only commitments, then later add PQC authorization leaves.**

That is the cleanest way to make:
- mint-time pubkey exposure disappear
- key-path bypass disappear
- future PQC migration sane

### What I would not do
- do not just remove the field and hope
- do not replace it with a hash while leaving validation logic structurally the same
- do not move consensus validation burden to wallet-local state
- do not break old mint parsing to force a clean slate

---

## Bottom line

The current owner-pubkey-in-`OP_RETURN` design is understandable, but it is not compatible with the repo’s own PQC direction.

If the requirement is:

> prevent the public key from ever being exposed during a mint, without breaking existing functionality

then the answer is:

1. **legacy mint path stays supported**
2. **new mint path must stop using owner pubkey as consensus reconstruction input**
3. **consensus must validate a collateral commitment / script commitment instead**
4. **best long-term form is P2MR-style script-only collateral**
5. **wallet restore remains safe as long as ddAmount + lockHeight + lockTier stay on chain**

That is the technologically honest path.

---

## Files validated during this research

- `DGB_PQC_PLAN.md`
- `DGB_PQC_LANDSCAPE.md`
- `DIGIDOLLAR_ARCHITECTURE.md`
- `REPO_MAP_DIGIDOLLAR.md`
- `src/digidollar/txbuilder.cpp`
- `src/digidollar/validation.cpp`
- `src/digidollar/scripts.h`
- `src/digidollar/scripts.cpp`
- `src/digidollar/health.cpp`
- `src/wallet/digidollarwallet.cpp`

---

## Final opinion

The current fix for the NUMS bypass made consensus safer, but it also pinned a giant secp256k1 target to every mint.

For DigiDollar, that is fixable.

The right move is not to hide the symptom. The right move is to change the mint commitment model so consensus no longer needs the owner pubkey on chain in the first place.
