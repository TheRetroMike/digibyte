# DigiDollar Implementation Specification v5.0 - Taproot Enhanced
This document is intended to be a living document modified and improved by community discussion.

## Executive Summary

This document provides a technical specification for implementing DigiDollar as a native stablecoin on the DigiByte blockchain, leveraging the recently activated Taproot upgrade.

**In Simple Terms**: DigiDollar is a digital currency that always equals $1 USD, built directly into DigiByte. When you want to create DigiDollars, you lock up DigiByte coins as collateral (like putting down a deposit). The system uses DigiByte's newest technology called Taproot to make all transactions look the same (better privacy), cost less in fees, and work more efficiently.

The implementation extends DigiByte Core v8.22.0 with new opcodes and consensus rules to enable a fully decentralized USD-pegged stablecoin backed by time-locked DGB collateral. By utilizing Taproot's P2TR outputs, Schnorr signatures, and Tapscript, DigiDollar achieves enhanced privacy, efficiency, and flexibility while maintaining the security and simplicity of a UTXO-based design.

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Core Consensus Changes](#2-core-consensus-changes)
3. [Taproot-Enhanced Script System](#3-taproot-enhanced-script-system)
4. [Oracle System with Schnorr Threshold Signatures](#4-oracle-system-with-schnorr-threshold-signatures)
5. [Transaction Types with P2TR](#5-transaction-types-with-p2tr)
6. [Price Volatility Protection](#6-price-volatility-protection)
7. [Wallet Integration](#7-wallet-integration)
8. [RPC Interface Extensions](#8-rpc-interface-extensions)
9. [Running a DigiDollar Oracle Node](#9-running-a-digidollar-oracle-node)
10. [DigiDollar Fungibility and Redemption](#10-digidollar-fungibility-and-redemption)
11. [Security Considerations](#11-security-considerations)
12. [Implementation Phases](#12-implementation-phases)
13. [Testing Strategy](#13-testing-strategy)
14. [Implementation Blueprint](#14-implementation-blueprint)

## 1. Architecture Overview

### 1.1 System Components

**What This Section Covers**: Think of DigiDollar as a new feature being added to DigiByte, like adding a new app to your phone. This section explains all the different parts that work together to make DigiDollar function.

The DigiDollar implementation leverages Taproot to provide enhanced functionality:

1. **Consensus Layer Modifications**

   **In Simple Terms**: These are the core rules that all DigiByte nodes must follow. Like traffic laws that everyone must obey, these rules ensure everyone agrees on what's valid.

   - New opcodes for stablecoin operations (OP_DIGIDOLLAR with Tapscript)
   - P2TR-based transaction validation for all DigiDollar operations
   - Time-locked collateral using Taproot script paths
   - Schnorr-based oracle threshold signatures with OP_CHECKSIGADD

2. **Taproot Script System**

   **In Simple Terms**: Taproot is like having a Swiss Army knife instead of carrying multiple tools. It lets us do many things efficiently while keeping transactions private.

   - All DigiDollar outputs use P2TR for privacy and efficiency
   - MAST-based conditional redemption paths
   - Key path spending for simple operations
   - Script path for complex conditions

3. **Oracle Infrastructure**

   **In Simple Terms**: Oracles are like trusted news reporters who tell the blockchain the current price of DigiByte in US dollars. We use 15 oracles and need at least 8 to agree on the price.

   - Schnorr threshold signatures (8-of-15 oracles)
   - Efficient batch verification with OP_CHECKSIGADD
   - Taproot-based oracle commitments
   - Privacy-preserving price feeds

4. **Price Stability Mechanism**

   **In Simple Terms**: This is like a safety system that monitors if the DigiByte price is changing too quickly. If prices are too volatile, it temporarily pauses new DigiDollar creation to protect users.

   - Volatility detection integrated with Tapscript
   - Dynamic collateral requirements in MAST branches
   - Efficient price verification using witness data

5. **Wallet Enhancements**

   **In Simple Terms**: Your wallet software gets new features to handle DigiDollars, like being able to see your balance, send them to others, and convert between DGB and DigiDollars.

   - P2TR address generation and management
   - Taproot-aware balance tracking
   - PSBT support for complex transactions
   - Hardware wallet compatibility

### 1.2 Design Principles

**These Are Our Core Values**: Like building a house with a strong foundation, these principles guide every decision in building DigiDollar.

- **Privacy First**: All transactions appear identical using P2TR outputs (nobody can tell if you're minting, transferring, or redeeming)
- **Efficiency**: Leverage key path spending for common operations (cheaper fees for everyday use)
- **Flexibility**: MAST enables multiple redemption conditions (different ways to unlock your collateral)
- **Future-Proof**: OP_SUCCESSx opcodes allow soft fork upgrades (we can add features later without breaking anything)
- **UTXO-Native**: Maintains stateless validation model (works perfectly with DigiByte's existing design)

## 2. Core Consensus Changes

**What This Section Covers**: This section explains the fundamental changes to DigiByte's rules that make DigiDollar possible. Think of it as updating the "laws" of the blockchain to recognize and handle this new type of money.

### 2.1 Tapscript Integration

**In Simple Terms**: We're adding new "commands" (opcodes) to DigiByte's scripting language. These are like new vocabulary words that let the blockchain understand DigiDollar operations.

#### 2.1.1 OP_DIGIDOLLAR in Tapscript Context

**What This Does**: This code defines the new commands. OP_DIGIDOLLAR is our main command that marks outputs as containing DigiDollars (not regular DGB).

```cpp
// Location: src/script/script.h
enum opcodetype {
    // ... existing opcodes ...
    OP_CHECKSIGADD = 0xba,         // BIP342 - for oracle thresholds
    OP_DIGIDOLLAR = 0xbb,          // Custom - marks DD outputs
    // Reserved OP_SUCCESSx for future upgrades
    OP_SUCCESS203 = 0xcb,          // Future DD features
    OP_SUCCESS204 = 0xcc,          // Future DD features
};
```

#### 2.1.2 Tapscript Validation Rules

**What This Does**: This ensures DigiDollar operations only work with DigiByte's newest transaction format (Taproot). It's like requiring a specific type of container for shipping certain goods.

```cpp
// Location: src/script/interpreter.cpp
// DigiDollar operations are only valid in Tapscript (v1 witness)
if (sigversion == SigVersion::TAPSCRIPT) {
    // OP_DIGIDOLLAR is allowed
    // Uses Schnorr signature verification
    // Subject to Tapscript resource limits
}
```

### 2.2 P2TR-Based Transaction Validation

**In Simple Terms**: This section defines how the network checks if DigiDollar transactions are valid. It's like having quality control inspectors who verify every transaction follows the rules.

#### 2.2.1 Enhanced DigiDollar Validation

**What This Function Does**:
- Checks that all DigiDollar outputs use the new P2TR format (for privacy)
- Identifies what type of transaction it is (minting new DD, transferring DD, or redeeming DD for DGB)
- Runs the appropriate validation for each type

```cpp
// Location: src/consensus/tx_check.cpp
bool CheckDigiDollarTransaction(const CTransaction& tx, TxValidationState& state,
                               const CCoinsViewCache& view, int nHeight) {
    // All DigiDollar outputs must be P2TR
    for (const auto& output : tx.vout) {
        if (IsDigiDollarOutput(output) && !output.scriptPubKey.IsPayToTaproot()) {
            return state.Invalid("digidollar-must-use-p2tr");
        }
    }

    // Validate based on transaction type
    DigiDollarTxType txType = GetDigiDollarTxType(tx);
    switch(txType) {
        case DD_TX_MINT:
            return ValidateMintTransaction(tx, view, nHeight, state);
        case DD_TX_TRANSFER:
            return ValidateTransferTransaction(tx, view, state);
        case DD_TX_REDEEM:
            return ValidateRedeemTransaction(tx, view, nHeight, state);
        default:
            return true;
    }
}
```

### 2.3 Updated Consensus Parameters

**In Simple Terms**: These are the "settings" for DigiDollar. Like setting the rules for a game, these parameters define things like minimum amounts, time periods, and safety thresholds.

**Key Settings Explained**:
- **Collateral Ratios (Treasury-Based Model)**:
  - 1 hour: 1000% ($10.00 DGB locked per $1.00 DD)
  - 30 days: 500% ($5.00 DGB locked per $1.00 DD)
  - 3 months: 400% ($4.00 DGB locked per $1.00 DD)
  - 6 months: 350% ($3.50 DGB locked per $1.00 DD)
  - 1 year: 300% ($3.00 DGB locked per $1.00 DD)
  - 2 years: 275% ($2.75 DGB locked per $1.00 DD)
  - 3 years: 250% ($2.50 DGB locked per $1.00 DD)
  - 5 years: 225% ($2.25 DGB locked per $1.00 DD)
  - 7 years: 212% ($2.12 DGB locked per $1.00 DD)
  - 10 years: 200% ($2.00 DGB locked per $1.00 DD)
- **Lock Time Options**: From 1 hour to 10 years
- **Mint Amount ($100 min)**: You must create at least $100 worth of DigiDollars at a time
- **Oracle Settings**: 15 price reporters, need 8 to agree on the price

```cpp
// Location: src/chainparams.cpp
// DigiDollar consensus parameters with Taproot enhancements
// Treasury-based collateral ratios: shorter terms require more collateral
std::map<int64_t, int> nDDCollateralRatios = {
    {30 * 24 * 60 * 4, 300},           // 30 days: 300%
    {90 * 24 * 60 * 4, 250},           // 3 months: 250%
    {180 * 24 * 60 * 4, 200},          // 6 months: 200%
    {365 * 24 * 60 * 4, 175},          // 1 year: 175%
    {3 * 365 * 24 * 60 * 4, 150},      // 3 years: 150%
    {5 * 365 * 24 * 60 * 4, 125},      // 5 years: 125%
    {10 * 365 * 24 * 60 * 4, 100}      // 10 years: 100%
};

consensus.nDDMinLockTime = 30 * 24 * 60 * 4;   // 30 days (in blocks)
consensus.nDDMaxLockTime = 10 * 365 * 24 * 60 * 4;  // 10 years
consensus.nDDMinMintAmount = 100 * CENT;       // $100 minimum
consensus.nDDMaxMintAmount = 100000 * CENT;    // $100k maximum per tx
consensus.nDDMinOutputAmount = 100;            // $1 minimum output

// Enhanced oracle configuration for Schnorr threshold
consensus.nDDOracleCount = 15;                 // Total oracles
consensus.nDDOracleThreshold = 8;              // 8-of-15 threshold
consensus.nDDPriceValidBlocks = 20;            // Price valid for 20 blocks
consensus.nDDVolatilityThreshold = 20;         // 20% price change triggers freeze

// Helper function to get collateral ratio based on lock time
int GetCollateralRatioForLockTime(int64_t lockBlocks) {
    // Find the appropriate ratio for the lock time
    for (const auto& [blocks, ratio] : nDDCollateralRatios) {
        if (lockBlocks <= blocks) {
            return ratio;
        }
    }
    return 100; // Default to 1:1 for 10 years
}

// Calculate required collateral amount
CAmount CalculateCollateral(CAmount ddAmount, CAmount pricePerDGB, int collateralRatio) {
    // ddAmount is in cents (100 = $1.00)
    // pricePerDGB is in satoshis
    // collateralRatio is in percentage (300 = 300%)
    CAmount usdValue = ddAmount; // DD amount equals USD value in cents
    CAmount dgbRequired = (usdValue * COIN) / pricePerDGB; // DGB for 100% collateral
    return (dgbRequired * collateralRatio) / 100; // Apply collateral ratio
}
```

### 2.4 Treasury-Based Collateral Model

**In Simple Terms**: Just like U.S. Treasury bonds pay different interest rates based on how long you lock up your money, DigiDollar requires different amounts of collateral based on your chosen lock-up period. Shorter periods need more collateral (higher safety margin), while longer periods can use less.

**Why This Makes Sense**:
- **Short-term (30 days)**: High volatility risk, needs 300% collateral
- **Long-term (10 years)**: Lower volatility risk over time, only needs 100% collateral
- **Rewards patience**: Users who commit for longer get better collateral efficiency

#### 2.4.1 Collateral Ratio Table

| Lock Period | Collateral Ratio | Example: $100 DigiDollars |
|-------------|------------------|---------------------------|
| 30 days     | 300%            | Lock $300 worth of DGB    |
| 3 months    | 250%            | Lock $250 worth of DGB    |
| 6 months    | 200%            | Lock $200 worth of DGB    |
| 1 year      | 175%            | Lock $175 worth of DGB    |
| 3 years     | 150%            | Lock $150 worth of DGB    |
| 5 years     | 125%            | Lock $125 worth of DGB    |
| 10 years    | 100%            | Lock $100 worth of DGB    |

#### 2.4.2 Implementation Details

```cpp
// Location: src/consensus/digidollar.cpp
bool ValidateMintCollateral(const CTransaction& tx, int nHeight) {
    // Extract lock time from transaction
    int64_t lockBlocks = GetLockTimeFromTx(tx);

    // Validate lock time is within allowed range
    if (lockBlocks < consensus.nDDMinLockTime ||
        lockBlocks > consensus.nDDMaxLockTime) {
        return false;
    }

    // Get appropriate collateral ratio
    int collateralRatio = GetCollateralRatioForLockTime(lockBlocks);

    // Verify sufficient collateral is provided
    CAmount requiredCollateral = CalculateCollateral(
        GetDDAmountFromTx(tx),
        GetMedianOraclePrice(),
        collateralRatio
    );

    return GetCollateralFromTx(tx) >= requiredCollateral;
}
```

## 3. Taproot-Enhanced Script System

**What This Section Covers**: This explains how we use Taproot's advanced features to create flexible and private DigiDollar transactions. Think of it as designing different "locks" for your digital money that can be opened in different ways.

### 3.1 P2TR DigiDollar Output Structure

**In Simple Terms**: P2TR (Pay-to-Taproot) is the newest and most advanced way to "lock" DigiBytes and DigiDollars. It's like having a smart lock that can be opened with either a simple key OR a complex combination, but nobody can tell which method you'll use until you actually use it.

#### 3.1.1 Taproot Output Construction

**What This Function Does**:
- Creates a special Taproot "container" for DigiDollars
- Allows multiple ways to spend the funds (like having multiple keys to a safe)
- Keeps all options private until one is used

```cpp
// Location: src/digidollar/scripts.cpp
CScript CreateDigiDollarP2TR(const XOnlyPubKey& internalKey,
                            CAmount ddAmount,
                            const std::vector<CScript>& scriptPaths) {
    // Build Taproot script tree with multiple spending conditions
    TaprootBuilder builder;

    // Add script paths (MAST leaves)
    for (const auto& script : scriptPaths) {
        builder.Add(script);
    }

    // Finalize with internal key
    builder.Finalize(internalKey);

    // Create P2TR output
    CScript scriptPubKey;
    scriptPubKey << OP_1 << builder.GetOutput();  // Version 1 witness program

    // Embed DD amount in witness commitment (via annex when available)
    return scriptPubKey;
}
```

#### 3.1.2 MAST-Based Redemption Paths

**What This Creates**: Three different ways to unlock your collateral:
1. **Normal Path**: Wait for the time lock to expire (most common)
2. **Emergency Path**: Multiple oracles can override in extreme situations
3. **Future Path**: Reserved for features we might add later

**MAST Explained**: MAST (Merkelized Alternative Script Trees) is like having multiple doors to exit a building, but only the door you use is revealed. The other doors remain secret.

```cpp
// Collateral output with multiple redemption conditions
std::vector<CScript> CreateCollateralRedemptionPaths(
    const XOnlyPubKey& ownerKey,
    CAmount ddAmount,
    int64_t lockTime) {

    std::vector<CScript> paths;

    // Path 1: Normal redemption after timelock
    CScript normalPath;
    normalPath << lockTime << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    normalPath << OP_DIGIDOLLAR << ddAmount << OP_EQUALVERIFY;
    normalPath << ownerKey << OP_CHECKSIG;
    paths.push_back(normalPath);

    // Path 2: Emergency override (requires oracle threshold)
    CScript emergencyPath;
    emergencyPath << OP_DIGIDOLLAR << ddAmount << OP_EQUALVERIFY;
    // 8-of-15 oracle threshold using OP_CHECKSIGADD
    for (int i = 0; i < 15; i++) {
        emergencyPath << consensus.oracleKeys[i] << OP_CHECKSIGADD;
    }
    emergencyPath << OP_8 << OP_EQUAL;
    paths.push_back(emergencyPath);

    // Path 3: Partial redemption (future upgrade via OP_SUCCESSx)
    CScript partialPath;
    partialPath << OP_SUCCESS203;  // Reserved for future partial redemption
    paths.push_back(partialPath);

    return paths;
}
```

### 3.2 Schnorr-Based Script Operations

**In Simple Terms**: Schnorr signatures are a newer, better way to sign transactions. They're smaller, faster to verify, and enable cool features like combining multiple signatures into one.

#### 3.2.1 DigiDollar Transfer Script

**What This Does**: Creates the simplest possible way to transfer DigiDollars between users. It optimizes for the "key path" - meaning it looks just like a regular DigiByte transaction, hiding the fact that it contains DigiDollars.

```cpp
// Simple P2TR transfer uses key path spending
TaprootSpendData CreateDigiDollarTransfer(const XOnlyPubKey& recipientKey,
                                         CAmount ddAmount) {
    // For transfers, optimize for key path (no script revelation needed)
    TaprootBuilder builder;

    // Add a simple script path as backup
    CScript backupScript;
    backupScript << OP_DIGIDOLLAR << ddAmount << OP_EQUALVERIFY;
    backupScript << recipientKey << OP_CHECKSIG;
    builder.Add(backupScript);

    // Finalize with recipient key for key path spending
    builder.Finalize(recipientKey);

    return builder.GetSpendData();
}
```

## 4. Oracle System with Schnorr Threshold Signatures

**What This Section Covers**: This explains how DigiDollar knows the current price of DigiByte in US dollars. It's like having multiple trusted price reporters, and we need most of them to agree before accepting a price.

### 4.1 Enhanced Oracle Configuration

**In Simple Terms**: We have 15 independent "price reporters" (oracles) who constantly tell the blockchain what DGB is worth in USD. To prevent manipulation, we require at least 8 of them to agree on the price. This is like requiring multiple witnesses to verify something important.

```cpp
// Location: src/consensus/params.h
struct OracleConfig {
    std::vector<XOnlyPubKey> vOracleXOnlyPubKeys;  // 15 Schnorr public keys
    uint32_t nThreshold = 8;                        // 8-of-15 threshold
    uint32_t nPriceValidityBlocks = 20;            // 5 minutes at 15s blocks
    bool fUseSchnorrThreshold = true;               // Enable OP_CHECKSIGADD
};
```

### 4.2 Schnorr Oracle Price Structure

**What This Stores**: Each price report from an oracle contains:
- The timestamp (when the price was checked)
- The actual price (e.g., 500 = $5.00 per DGB)
- A block height (which block this price is for)
- A nonce (random number to prevent replay attacks)
- A Schnorr signature (cryptographic proof the oracle sent this)

**Why This Matters**: This structure ensures price data is authentic, fresh, and can't be reused maliciously.

```cpp
// Location: src/primitives/oracle.h
class CSchnorrOraclePrice {
public:
    uint32_t nTimestamp;
    uint32_t nPricePerDGB;              // Price in cents per DGB
    uint32_t nBlockHeight;
    uint256 nonce;                      // Replay protection

    // Schnorr signature (64 bytes)
    std::vector<unsigned char> vchSchnorrSig;

    // Commitment for commit-reveal (optional)
    uint256 hashCommitment;

    ADD_SERIALIZE_METHODS;

    bool VerifySchnorrSignature(const XOnlyPubKey& pubkey) const;
    uint256 GetMessageHash() const;
};
```

### 4.3 Threshold Signature Validation

**What This Does**: This is the "voting mechanism" for oracle prices:
1. Collects price reports from multiple oracles
2. Verifies each signature is valid
3. Counts how many oracles provided prices
4. Ensures at least 8 out of 15 agree

**The Magic of OP_CHECKSIGADD**: This new opcode lets us efficiently verify multiple signatures in one operation, making the system faster and cheaper than old methods.

```cpp
// Location: src/digidollar/oracle.cpp
bool ValidateOracleThreshold(const std::vector<CSchnorrOraclePrice>& prices,
                            const OracleConfig& config) {
    if (prices.size() < config.nThreshold)
        return false;

    // Build Tapscript for threshold validation
    CScript thresholdScript;

    // Add each oracle signature using OP_CHECKSIGADD
    for (size_t i = 0; i < prices.size() && i < config.vOracleXOnlyPubKeys.size(); i++) {
        thresholdScript << prices[i].vchSchnorrSig;
        thresholdScript << config.vOracleXOnlyPubKeys[i];
        thresholdScript << OP_CHECKSIGADD;
    }

    // Verify threshold is met
    thresholdScript << CScriptNum(config.nThreshold) << OP_EQUAL;

    // Execute script to validate
    return EvalScript(thresholdScript, SCRIPT_VERIFY_TAPSCRIPT);
}

// Efficient batch verification of all oracle signatures
bool BatchVerifyOracleSignatures(const std::vector<CSchnorrOraclePrice>& prices) {
    // Leverage Schnorr batch verification for efficiency
    return CSchnorrSig::BatchVerify(prices);
}
```

## 5. Transaction Types with P2TR

**What This Section Covers**: This explains the three main types of DigiDollar transactions: Minting (creating new DD), Transferring (sending DD to others), and Redeeming (converting DD back to DGB). Each uses Taproot for privacy and efficiency.

### 5.1 Mint Transaction with Taproot

**In Simple Terms**: Minting is like going to a bank and depositing gold (DGB) to get paper money (DigiDollars). You lock up your DGB as collateral, and the system creates new DigiDollars for you to use.

#### 5.1.1 Structure

**What This Shows**: The anatomy of a mint transaction:
- **Inputs**: Your regular DGB that will be locked as collateral
- **Outputs**:
  - Locked collateral (time-locked DGB)
  - New DigiDollars (sent to you)
  - Metadata (records the transaction details)
  - Change (any leftover DGB)

```cpp
class CTaprootMintTransaction {
    // Inputs: DGB from user (any type)
    std::vector<CTxIn> vDGBInputs;

    // Outputs (all P2TR):
    CTxOut collateralOutput;    // P2TR with MAST redemption tree
    CTxOut digidollarOutput;    // P2TR DigiDollar to user
    CTxOut metadataOutput;      // OP_RETURN with commitment
    CTxOut changeOutput;        // P2TR change (if any)
};
```

#### 5.1.2 Mint Transaction Creation

**What This Function Does**:
1. Checks the current DGB price from oracles
2. Calculates how much DGB you need to lock (varies by time period)
3. Creates a time-locked "vault" for your collateral
4. Issues new DigiDollars to your address
5. Records everything on the blockchain

**Example**: If you want $100 in DigiDollars and DGB is $0.01:
- 30-day lock: Need 30,000 DGB ($300 worth)
- 1-year lock: Need 17,500 DGB ($175 worth)
- 10-year lock: Need 10,000 DGB ($100 worth)

```cpp
bool CreateMintTransaction(const CWallet& wallet,
                          CAmount ddAmount,
                          int64_t lockDays,
                          CMutableTransaction& tx) {
    // Calculate required collateral based on lock time
    CAmount pricePerDGB = GetMedianOraclePrice();
    int collateralRatio = GetCollateralRatioForLockTime(lockDays * 24 * 60 * 4);
    CAmount requiredDGB = CalculateCollateral(ddAmount, pricePerDGB, collateralRatio);

    // Create internal key for collateral
    CKey internalKey;
    internalKey.MakeNewKey(true);
    XOnlyPubKey xonlyKey(internalKey.GetPubKey());

    // Build MAST tree for collateral redemption
    std::vector<CScript> redeemPaths = CreateCollateralRedemptionPaths(
        xonlyKey, ddAmount, GetTimeLockHeight(lockDays)
    );

    // Create P2TR collateral output
    tx.vout.push_back(CTxOut(requiredDGB,
        CreateDigiDollarP2TR(xonlyKey, ddAmount, redeemPaths)));

    // Create P2TR DigiDollar output
    CPubKey userPubKey = wallet.GetNewPubKey();
    tx.vout.push_back(CTxOut(0,  // 0 DGB, carries DD value
        CreateDigiDollarP2TR(XOnlyPubKey(userPubKey), ddAmount, {})));

    // Add metadata commitment
    CScript metadata;
    metadata << OP_RETURN << OP_DIGIDOLLAR;
    metadata << SerializeHash(ddAmount, requiredDGB, lockDays);
    tx.vout.push_back(CTxOut(0, metadata));

    return true;
}
```

### 5.2 Transfer Transaction with Key Path Spending

**In Simple Terms**: Transferring DigiDollars is like sending an email - quick and simple. Thanks to Taproot's "key path," these transactions look exactly like regular DigiByte transactions, keeping your DigiDollar usage private.

#### 5.2.1 Efficient P2TR Transfers

**What This Function Does**:
1. Finds your DigiDollar "coins" in your wallet
2. Selects enough to cover the amount you want to send
3. Creates a new output for the recipient
4. Returns any "change" to you (like getting change from a $20 bill)
5. Signs everything using the most efficient method

**Privacy Benefit**: Nobody can tell this is a DigiDollar transaction - it looks identical to a regular DGB transfer!

```cpp
bool CreateTransferTransaction(const CWallet& wallet,
                              const CTxDestination& dest,
                              CAmount ddAmount,
                              CMutableTransaction& tx) {
    // Find P2TR DigiDollar UTXOs
    std::vector<COutput> vDDCoins = wallet.GetP2TRDigiDollarCoins();

    // Select inputs
    CAmount totalIn = 0;
    for (const auto& coin : vDDCoins) {
        tx.vin.push_back(CTxIn(coin.outpoint));
        totalIn += GetDigiDollarAmount(coin.tx->vout[coin.i]);
        if (totalIn >= ddAmount) break;
    }

    // Create P2TR output for recipient
    XOnlyPubKey recipientKey = GetXOnlyPubKey(dest);
    tx.vout.push_back(CTxOut(0,
        CreateDigiDollarP2TR(recipientKey, ddAmount, {})));

    // Change output (if any)
    if (totalIn > ddAmount) {
        CPubKey changeKey = wallet.GetNewPubKey();
        tx.vout.push_back(CTxOut(0,
            CreateDigiDollarP2TR(XOnlyPubKey(changeKey), totalIn - ddAmount, {})));
    }

    // Sign using key path (most efficient)
    return wallet.SignP2TRKeyPath(tx);
}
```

### 5.3 Redemption Transaction with Script Path

**In Simple Terms**: Redemption is like returning to the bank to get your gold (DGB) back by giving them your paper money (DigiDollars). You must "burn" (destroy) the DigiDollars to unlock your collateral.

#### 5.3.1 MAST-Based Redemption

**What This Function Does**:
1. References your locked collateral
2. Gathers enough DigiDollars to burn (must match what you originally minted)
3. Burns the DigiDollars (they're destroyed forever)
4. Unlocks your DGB collateral and sends it back to you
5. Uses the appropriate unlocking method (normal timelock or emergency)

**Important**: You can only redeem after your timelock expires (1 hour to 10 years, depending on what you chose), unless there's an emergency situation validated by oracles.

```cpp
bool CreateRedemptionTransaction(const CWallet& wallet,
                                const COutPoint& collateralOutpoint,
                                const std::string& redeemPath,
                                CMutableTransaction& tx) {
    // Add collateral input
    tx.vin.push_back(CTxIn(collateralOutpoint));

    // Add DigiDollar inputs to burn
    CAmount ddToBurn = GetCollateralDDAmount(collateralOutpoint);
    std::vector<COutput> vDDCoins = wallet.GetP2TRDigiDollarCoins();

    CAmount burnedDD = 0;
    for (const auto& coin : vDDCoins) {
        tx.vin.push_back(CTxIn(coin.outpoint));
        burnedDD += GetDigiDollarAmount(coin.tx->vout[coin.i]);
        if (burnedDD >= ddToBurn) break;
    }

    // Create DGB output (no DD outputs allowed in redemption)
    CAmount dgbAmount = GetCollateralAmount(collateralOutpoint);
    CPubKey userKey = wallet.GetNewPubKey();
    tx.vout.push_back(CTxOut(dgbAmount,
        GetScriptForDestination(PKHash(userKey))));  // Regular P2PKH for DGB

    // Sign using appropriate script path
    if (redeemPath == "normal") {
        // Use timelock path (most common)
        return wallet.SignP2TRScriptPath(tx, 0, SIGHASH_DEFAULT, 0);  // First path
    } else if (redeemPath == "emergency") {
        // Requires oracle threshold signatures
        return wallet.SignP2TRScriptPath(tx, 0, SIGHASH_DEFAULT, 1);  // Second path
    }

    return false;
}
```

## 6. Price Volatility Protection

**What This Section Covers**: This explains how DigiDollar protects users from wild price swings. Think of it as an automatic safety brake that activates when the DGB price is moving too fast.

### 6.1 Tapscript-Enhanced Volatility Detection

**In Simple Terms**: This system constantly monitors the DGB price. If the price changes more than 20% in an hour, it temporarily stops new DigiDollar creation. This protects both new users and the system from extreme market conditions.

**How It Works**:
1. Collects price data from the last hour (240 blocks at 15 seconds each)
2. Verifies all oracle signatures in one batch (super efficient!)
3. Calculates how much the price has changed
4. If change is over 20%, minting is paused

**Why This Matters**: Prevents people from gaming the system during price crashes or spikes.

```cpp
// Location: src/consensus/digidollar.cpp
class CTaprootVolatilityChecker {
    bool IsMintingFrozen(int nHeight) {
        // Use Schnorr-signed oracle prices for efficiency
        std::vector<CSchnorrOraclePrice> recentPrices;

        // Collect prices from last hour
        for (int i = 0; i < 240; i++) {
            auto prices = GetBlockOraclePrices(nHeight - i);
            recentPrices.insert(recentPrices.end(), prices.begin(), prices.end());
        }

        // Batch verify all signatures for efficiency
        if (!BatchVerifyOracleSignatures(recentPrices))
            return true;  // Freeze if signatures invalid

        // Calculate volatility
        auto [minPrice, maxPrice] = GetPriceRange(recentPrices);
        int64_t volatility = (maxPrice - minPrice) * 100 / minPrice;

        return volatility > consensus.nDDVolatilityThreshold;
    }
};
```

### 6.2 Dynamic Collateral with MAST

**In Simple Terms**: During volatile markets, the system can require more collateral. It's like a bank asking for a bigger down payment when conditions are risky. MAST lets us encode different collateral levels that activate based on market conditions.

**Three Collateral Levels**:
1. **Normal Market**: 150% collateral (you lock $150 of DGB to get $100 DD)
2. **Volatile Market**: 200% collateral (you lock $200 of DGB to get $100 DD)
3. **Extreme Volatility**: 250% collateral (you lock $250 of DGB to get $100 DD)

```cpp
// Different collateral requirements encoded in MAST tree
std::vector<CScript> CreateDynamicCollateralPaths(CAmount ddAmount) {
    std::vector<CScript> paths;

    // Path 1: Normal market (150% collateral)
    paths.push_back(CreateCollateralScript(ddAmount, 150));

    // Path 2: Volatile market (200% collateral)
    paths.push_back(CreateCollateralScript(ddAmount, 200));

    // Path 3: Extreme volatility (250% collateral)
    paths.push_back(CreateCollateralScript(ddAmount, 250));

    return paths;
}
```

## 7. Wallet Integration

**What This Section Covers**: This explains how your DigiByte wallet software is upgraded to handle DigiDollars. Think of it as adding a new "currency compartment" to your digital wallet.

### 7.1 Taproot-Aware Wallet Functions

**In Simple Terms**: Your wallet needs new abilities to:
- Create special Taproot addresses for DigiDollars
- Track your DigiDollar balance separately from DGB
- Monitor your locked collateral positions
- Create all three types of transactions (mint, transfer, redeem)
- Work with hardware wallets like Ledger or Trezor

**Key Features**:
- **Balance Tracking**: Shows both your DGB and DigiDollar balances
- **Position Monitor**: Tracks all your locked collateral with countdown timers
- **Smart Path Selection**: Automatically uses the most efficient transaction method
- **Hardware Wallet Support**: Works with PSBT (Partially Signed DigiByte Transactions)

```cpp
// Location: src/wallet/digidollar.h
class CTaprootDigiDollarWallet : public CDigiDollarWallet {
private:
    // Taproot key management
    std::map<COutPoint, TaprootSpendData> m_taproot_spends;
    std::map<uint256, CKey> m_internal_keys;

public:
    // P2TR address generation
    CTxDestination GetNewP2TRAddress(const std::string& label = "");

    // Taproot-specific balance calculation
    CAmount GetP2TRDigiDollarBalance() const;
    std::vector<COutput> GetP2TRDigiDollarCoins() const;

    // Enhanced position tracking with MAST paths
    struct TaprootCollateralPosition {
        COutPoint outpoint;
        CAmount dgbLocked;
        CAmount ddIssued;
        int64_t unlockTime;
        TaprootSpendData spendData;
        std::vector<std::string> availablePaths;
    };
    std::vector<TaprootCollateralPosition> GetTaprootPositions() const;

    // Taproot transaction creation
    bool CreateTaprootMintTransaction(CAmount ddAmount, int64_t lockDays,
                                    CWalletTx& wtxNew, std::string& strError);
    bool CreateTaprootRedeemTransaction(const COutPoint& collateral,
                                      const std::string& redeemPath,
                                      CWalletTx& wtxNew, std::string& strError);

    // PSBT support for hardware wallets
    bool CreateDigiDollarPSBT(const std::vector<CTxIn>& inputs,
                            const std::vector<CTxOut>& outputs,
                            PartiallySignedTransaction& psbt);
};
```

### 7.2 Enhanced GUI Components

**In Simple Terms**: The wallet's user interface gets new screens and features specifically for DigiDollar:
- **Privacy Indicator**: Shows when your transactions are maximally private
- **Redemption Path Selector**: Choose how to unlock your collateral
- **Optimization Toggle**: Let the wallet pick the cheapest transaction method

**Visual Elements**:
- Green shield icon when privacy is maximized
- Dropdown menu showing available redemption options
- Checkbox for "Use most efficient method"

```cpp
// Location: src/qt/digidollarpage.h
class TaprootDigiDollarPage : public QWidget {
    // ... existing members ...

    // New Taproot-specific UI elements
    QLabel* labelPrivacyStatus;      // Shows transaction privacy level
    QComboBox* comboRedeemPath;      // Select redemption path
    QCheckBox* checkUseKeyPath;      // Optimize for key path spending

    // Display Taproot-specific information
    void showTaprootBenefits();
    void updatePrivacyIndicator();
    void displayAvailableRedemptionPaths();
};
```

## 8. RPC Interface Extensions

**What This Section Covers**: RPC (Remote Procedure Call) commands are how advanced users and developers interact with DigiDollar through the command line. Think of these as "power user" commands for those who want direct control.

### 8.1 Taproot-Enhanced RPC Commands

**In Simple Terms**: These are new commands you can type to:
- Get a new DigiDollar address
- Create DigiDollars
- Check your positions
- See available redemption options
- Verify oracle prices

**Why RPC Matters**: Developers can build applications, exchanges can integrate DigiDollar, and power users can automate their operations.

```cpp
// Location: src/rpc/digidollar.cpp

// Get P2TR DigiDollar address
UniValue getnewdigidollaraddress(const JSONRPCRequest& request) {
    // Returns a new P2TR address for receiving DigiDollars
    // Internally manages Taproot keys and scripts
}

// Create Taproot mint transaction
UniValue mintdigidollartaproot(const JSONRPCRequest& request) {
    // Parameters: amount, lockperiod ("30days", "3months", "6months", "1year", "3years", "5years", "10years")
    // Returns: txid, taproot_address, spend_paths, collateral_ratio_used
    // Example: mintdigidollartaproot 100.00 "1year" -> locks at 175% ratio
}

// List available redemption paths
UniValue listredemptionpaths(const JSONRPCRequest& request) {
    // Parameters: position_txid
    // Returns: array of available MAST paths with conditions
}

// Get Taproot spend info
UniValue getdigidollarspendinfo(const JSONRPCRequest& request) {
    // Parameters: outpoint
    // Returns: internal_key, merkle_root, script_paths
}

// Verify oracle threshold
UniValue verifyoraclethreshold(const JSONRPCRequest& request) {
    // Parameters: price_data, signatures
    // Returns: valid, threshold_met, verified_count
}
```

### 8.2 Example Usage

**Real Examples You Can Run**: These show how to use the commands in practice. Each command returns useful information about your transaction or position.

```bash
# Create P2TR DigiDollar address
digibyte-cli getnewdigidollaraddress

# Mint with Taproot (returns detailed spend info)
# Example: $1000 DigiDollars with 1-year lock (175% collateral)
digibyte-cli mintdigidollartaproot 1000.00 "1year"
{
  "txid": "...",
  "address": "dgb1p...",  # P2TR address
  "collateral_ratio": 175,
  "collateral_amount": "175000 DGB",  # If DGB = $0.01
  "spend_paths": [
    "normal_redemption",
    "emergency_override",
    "future_upgrade"
  ]
}

# Mint with 10-year lock for 1:1 ratio
digibyte-cli mintdigidollartaproot 5000.00 "10years"
{
  "txid": "...",
  "address": "dgb1p...",
  "collateral_ratio": 100,  # Only need $5000 worth of DGB
  "spend_paths": [...]
}

# Check available redemption paths
digibyte-cli listredemptionpaths "txid"

# Redeem using specific path
digibyte-cli redeemdigidollar "txid" "normal_redemption"
```

## 9. Running a DigiDollar Oracle Node

**What This Section Covers**: This section explains how to run your DigiByte Core wallet as an Oracle price node, providing critical price data to the DigiDollar system. Think of it as volunteering to be one of the trusted price reporters for the network.

### 9.1 Oracle Node Overview

**In Simple Terms**: Oracle nodes are special DigiByte Core wallets that fetch DGB/USD prices from exchanges and broadcast them to the network. Just like DNS seed nodes help peers find each other, oracle nodes help the network know the current DGB price.

**How It Works**:
1. Your node fetches prices from multiple exchanges every minute
2. It signs the price with your oracle key
3. It broadcasts the signed price to all peers
4. Miners collect these prices and include them in blocks
5. The network requires 8 out of 15 oracles to agree on price

### 9.2 Hardcoded Oracle Implementation

**Similar to Seed Nodes**: Just like DigiByte has hardcoded DNS seeds (seed.digibyte.io, etc.), oracle nodes will be hardcoded into the client:

```cpp
// Location: src/chainparams.cpp
// Oracle nodes hardcoded like DNS seeds
class CMainParams : public CChainParams {
    // Existing DNS seeds
    vSeeds.emplace_back("seed.digibyte.io");
    vSeeds.emplace_back("seed.diginode.tools");

    // New: Hardcoded oracle nodes (30 committed operators)
    vOracleNodes.emplace_back("oracle1.digibyte.io", "xpub....");  // Node URL + pubkey
    vOracleNodes.emplace_back("oracle2.diginode.tools", "xpub....");
    vOracleNodes.emplace_back("oracle3.dgb.community", "xpub....");
    // ... up to 30 hardcoded oracle nodes
}
```

### 9.3 Running an Oracle Node

#### 9.3.1 Prerequisites

**What You Need**:
- Reliable 24/7 server (VPS or dedicated)
- DigiByte Core with oracle support enabled
- API keys from 5+ exchanges (Binance, KuCoin, Bittrex, etc.)
- Commitment to maintain uptime

#### 9.3.2 Configuration

```bash
# digibyte.conf settings for oracle nodes
oracle=1                    # Enable oracle mode
oracleexchanges=binance,kucoin,bittrex,okex,huobi
oracleapikey_binance=YOUR_API_KEY
oracleapikey_kucoin=YOUR_API_KEY
oraclebroadcastinterval=60  # Broadcast every 60 seconds
oraclemedianwindow=5        # Use 5 exchanges for median
```

#### 9.3.3 Oracle Node Commands

```bash
# Start oracle broadcasting
digibyte-cli startoracle

# Check oracle status
digibyte-cli getoraclestatus
{
  "active": true,
  "last_price": 0.01234,
  "last_broadcast": "2024-01-09 12:34:56",
  "exchanges_active": 5,
  "broadcasts_sent": 1440,
  "signature_count": 8
}

# List active oracle peers
digibyte-cli listoracles
[
  {
    "address": "oracle1.digibyte.io",
    "pubkey": "xpub...",
    "last_seen": "2024-01-09 12:34:00",
    "price": 0.01234,
    "reliability": 99.8
  },
  // ... other oracles
]
```

### 9.4 Oracle Selection Mechanism

**Network-Based Selection**: While 30 oracles are hardcoded, the network dynamically selects which to use:

```cpp
// Location: src/consensus/oracle.cpp
std::vector<COracleNode> SelectActiveOracles(int nHeight) {
    // Deterministic selection based on block height
    // Rotates through all 30 hardcoded oracles
    // Selects 15 for each epoch (100 blocks)

    uint256 epochSeed = GetEpochSeed(nHeight / 100);
    std::vector<COracleNode> shuffled = ShuffleOracles(vOracleNodes, epochSeed);

    // Return first 15 from shuffled list
    return std::vector<COracleNode>(shuffled.begin(), shuffled.begin() + 15);
}
```

### 9.5 Incentive Mechanisms

**How to Encourage Oracle Participation**:

#### 9.5.1 Direct Incentives

1. **Oracle Rewards Pool**:
   ```cpp
   // 0.1% of each DigiDollar mint goes to oracle pool
   CAmount oracleFee = ddAmount * 10 / 10000;  // 0.1%
   // Distributed weekly to reliable oracles
   ```

2. **Reliability-Based Rewards**:
   - Oracles with 99%+ uptime get full share
   - Oracles with 95-99% get 75% share
   - Below 95% get no rewards

#### 9.5.2 Indirect Incentives

1. **Reputation System**:
   - Public dashboard showing oracle performance
   - "Trusted Oracle" badge for consistent operators
   - Community recognition

2. **Staking Requirements** (Future):
   ```cpp
   // Oracles must lock 100,000 DGB as stake
   // Slashed for prolonged downtime or false data
   const CAmount ORACLE_STAKE = 100000 * COIN;
   ```

3. **Business Incentives**:
   - Exchanges running oracles get "DigiDollar Certified" status
   - Mining pools can run oracles for better block validation
   - Payment processors need accurate prices

### 9.6 Scaling to Hundreds of Oracles

**Future Expansion Plan**:

1. **Phase 1** (Launch): 30 hardcoded oracles
2. **Phase 2** (6 months): Expand to 100 via soft fork
3. **Phase 3** (1 year): Dynamic oracle registration with staking

```cpp
// Future: Dynamic oracle registration
class CDynamicOracleRegistry {
    // Oracles can register by:
    // 1. Staking 100,000 DGB
    // 2. Proving 30-day reliability period
    // 3. Getting voted in by existing oracles

    std::map<CPubKey, COracleRegistration> m_registrations;

    bool RegisterOracle(const CPubKey& pubkey, const CTransaction& stakeTx);
    bool VoteForOracle(const CPubKey& candidate, bool approve);
    std::vector<CPubKey> GetTopOracles(int count);  // By reliability score
};
```

### 9.7 Technical Implementation Details

#### 9.7.1 Price Broadcasting Protocol

```cpp
// New P2P message types
const char* ORACLEPRICE = "oracleprice";
const char* GETORACLES = "getoracles";

// Oracle price message structure
class COraclePriceMessage {
    int64_t nTime;
    uint32_t nPrice;        // micro-USD per DGB
    uint256 nBlockHash;     // Recent block for context
    std::vector<unsigned char> vchSig;  // Schnorr signature

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nTime);
        READWRITE(nPrice);
        READWRITE(nBlockHash);
        READWRITE(vchSig);
    }
};
```

#### 9.7.2 Exchange Price Fetching

```cpp
// Location: src/oracle/pricefeed.cpp
class CExchangePriceFeed {
    std::map<std::string, std::unique_ptr<IExchangeAPI>> m_exchanges;

    double GetMedianPrice() {
        std::vector<double> prices;

        // Fetch from each configured exchange
        for (const auto& [name, api] : m_exchanges) {
            try {
                double price = api->GetDGBUSDPrice();
                if (price > 0) prices.push_back(price);
            } catch (...) {
                LogPrintf("Oracle: Failed to fetch from %s\n", name);
            }
        }

        // Return median to filter outliers
        std::sort(prices.begin(), prices.end());
        return prices[prices.size() / 2];
    }
};
```

### 9.7 Option B: DNS Seeder Price Feeds

**Alternative Approach**: Instead of dedicated oracle nodes, leverage existing DNS seed infrastructure.

#### 9.7.1 How DNS Seeder Price Feeds Would Work

**The Concept**: DNS seeders already provide peer discovery. They could also provide price data:

```bash
# Current DNS query for peers
$ dig seed.digibyte.io
# Returns: IP addresses of DigiByte nodes

# New: DNS TXT record for price
$ dig TXT price.seed.digibyte.io
# Returns: "dgb-usd-price=1234" (micro-USD per DGB)
```

#### 9.7.2 Implementation for DNS Operators

**Simple Script for Seeders**:
```bash
#!/bin/bash
# update-price-record.sh - Run every 60 seconds

# Fetch prices from exchanges
BINANCE=$(curl -s https://api.binance.com/api/v3/ticker/price?symbol=DGBUSDT | jq -r .price)
KUCOIN=$(curl -s https://api.kucoin.com/api/v1/market/orderbook/level1?symbol=DGB-USDT | jq -r .data.price)
BITTREX=$(curl -s https://api.bittrex.com/v3/markets/DGB-USD/ticker | jq -r .lastTradeRate)

# Calculate median
MEDIAN=$(echo "$BINANCE $KUCOIN $BITTREX" | tr ' ' '\n' | sort -n | awk 'NR==2')

# Convert to micro-USD
MICRO_USD=$(echo "$MEDIAN * 1000000" | bc | cut -d. -f1)

# Update DNS TXT record
nsupdate <<EOF
server localhost
zone digibyte.io
update delete price.seed.digibyte.io TXT
update add price.seed.digibyte.io 60 TXT "dgb-usd-price=$MICRO_USD"
send
EOF
```

#### 9.7.3 Node Implementation

```cpp
// Location: src/oracle/dnsprice.cpp
class CDNSPriceOracle {
    uint32_t GetPriceFromDNS(const std::string& seed) {
        // Query TXT record
        std::string query = "price." + seed;
        std::vector<std::string> records = DNSLookupTXT(query);

        // Parse price from TXT record
        for (const auto& record : records) {
            if (record.find("dgb-usd-price=") == 0) {
                return ParsePrice(record.substr(14));
            }
        }
        return 0;
    }

    uint32_t GetMedianDNSPrice() {
        std::vector<uint32_t> prices;

        // Query all DNS seeds
        for (const auto& seed : Params().DNSSeeds()) {
            uint32_t price = GetPriceFromDNS(seed);
            if (price > 0) prices.push_back(price);
        }

        // Return median
        std::sort(prices.begin(), prices.end());
        return prices[prices.size() / 2];
    }
};
```

#### 9.7.4 Advantages of DNS Approach

1. **Simplicity**: No new P2P protocol needed
2. **Reuse Infrastructure**: DNS seeds already trusted and maintained
3. **Easy Updates**: Simple script, no blockchain software changes
4. **Censorship Resistant**: Multiple DNS seeds provide redundancy

#### 9.7.5 Disadvantages of DNS Approach

1. **No Signatures**: Can't cryptographically verify price source
2. **DNS Attacks**: Subject to DNS hijacking/poisoning
3. **Less Granular**: Only one price per seed (vs multiple oracles)
4. **Update Delays**: DNS caching might delay price updates

### 9.8 Recommendation: Hybrid Approach

**Best of Both Worlds**: Start with Option B (DNS) for simplicity, upgrade to Option A (dedicated oracles) for security:

1. **Phase 1** (Launch): Use DNS seeder prices
   - Quick to implement
   - Leverages existing infrastructure
   - Good enough for initial launch

2. **Phase 2** (6 months): Add dedicated oracle nodes
   - Cryptographic signatures
   - More price sources
   - Better security guarantees

3. **Phase 3** (1 year): Phase out DNS prices
   - Full oracle network established
   - Proven reliability
   - Enhanced features (staking, rewards)

## 10. DigiDollar Fungibility and Redemption

**What This Section Covers**: This explains how DigiDollar redemption works and confirms that all DigiDollars are fully fungible - you don't need the exact ones you minted.

### 10.1 Full Fungibility Confirmed

**In Simple Terms**: DigiDollars work like regular money - a dollar is a dollar, no matter where it came from. You can:
- Mint 100 DD today
- Spend them all
- Buy 100 DD on an exchange next year
- Use those to redeem your original collateral

### 10.2 How Redemption Works

```cpp
// The system tracks: Collateral Position -> Amount Minted
// NOT: Collateral Position -> Specific DD UTXOs

bool CreateRedemptionTransaction(...) {
    // Step 1: Check how many DD this collateral minted
    CAmount ddRequired = GetCollateralDDAmount(collateralOutpoint);
    // Example: This position minted 100 DD

    // Step 2: Gather ANY DigiDollars from wallet
    std::vector<COutput> availableDD = wallet.GetP2TRDigiDollarCoins();

    // Step 3: Select enough DD to burn (any will work!)
    CAmount ddToBurn = 0;
    for (const auto& coin : availableDD) {
        tx.vin.push_back(CTxIn(coin.outpoint));
        ddToBurn += GetDigiDollarAmount(coin.tx->vout[coin.i]);
        if (ddToBurn >= ddRequired) break;
    }

    // Step 4: Burn the DD and unlock collateral
    // The DD are destroyed, DGB is released
}
```

### 10.3 Why This Matters

**Benefits of Fungibility**:
1. **Privacy**: No tracking of DD lineage or history
2. **Liquidity**: Can freely trade on exchanges
3. **Usability**: Works like any other currency
4. **DeFi Ready**: Can be used in smart contracts, lending, etc.

**What This Enables**:
- **Exchange Trading**: Buy/sell DD without worrying about collateral
- **Payments**: Use DD for purchases, they're all the same
- **Lending**: Borrow DD and repay with any DD
- **Arbitrage**: Take advantage of price differences across markets

### 10.4 Technical Implementation

```cpp
// DigiDollar amounts are tracked in outputs, not tied to collateral
class CTxOut {
    CAmount nValue;        // 0 for DD outputs (amount in witness)
    CScript scriptPubKey;  // P2TR script identifying DD output

    // DD amount extracted from witness/annex, not linked to origin
    CAmount GetDDAmount() const {
        if (!IsDigiDollarOutput()) return 0;
        return ExtractDDAmountFromWitness();
    }
};

// Collateral only tracks total minted, not specific outputs
struct CollateralPosition {
    COutPoint outpoint;      // The locked DGB
    CAmount dgbAmount;       // Amount of DGB locked
    CAmount ddMinted;        // Amount of DD created (not which ones!)
    int64_t unlockHeight;    // When it can be redeemed
};
```

This design ensures DigiDollars function as a true fungible currency while maintaining the security of collateral-backed minting.

## 11. Security Considerations

### 10.1 Taproot-Specific Security Enhancements

#### 10.1.1 Enhanced Privacy
- All DigiDollar transactions appear identical on-chain
- Unused MAST branches remain hidden
- Oracle operations indistinguishable from transfers
- Improved fungibility for DigiDollar tokens

#### 10.1.2 Schnorr Signature Security
- Provably secure under standard assumptions
- Non-malleable signatures prevent transaction tampering
- Batch verification reduces validation time
- Key aggregation enables future multi-party features

#### 10.1.3 Script Path Protection
```cpp
// Ensure script paths are properly validated
bool ValidateTaprootScriptPath(const CTransaction& tx,
                               const CTxOut& prevout,
                               const std::vector<unsigned char>& witness) {
    // Verify Merkle proof
    // Check script execution
    // Validate signature
    // Ensure no path can bypass collateral requirements
}
```

### 10.2 Attack Mitigation

#### 10.2.1 Oracle Manipulation (Enhanced)
- **Attack**: Compromise oracle threshold
- **Mitigation**:
  - Schnorr threshold requires 8 of 15 oracles
  - Batch verification detects invalid signatures
  - Commit-reveal can be added via witness

#### 10.2.2 MAST Path Exploitation
- **Attack**: Find unintended script path
- **Mitigation**:
  - Careful script construction
  - Limited path options
  - OP_SUCCESSx disabled until activated

## 12. Implementation Phases

### Phase 1: Taproot Foundation
- Implement P2TR output creation for DigiDollar
- Basic Schnorr signature support
- Update wallet to handle Taproot addresses
- Modify validation for witness v1

### Phase 2: Oracle Integration

**In Simple Terms**: Implement the price oracle system (start with DNS approach).

- Phase 2a: DNS-based price feeds
  - Modify DNS seeders to provide price data
  - Add DNS TXT record parsing to nodes
  - Test with existing seed infrastructure

- Phase 2b: Dedicated oracle nodes (if needed)
  - Implement OP_CHECKSIGADD threshold validation
  - Convert oracle system to Schnorr signatures
  - Add batch verification
  - Deploy hardcoded oracle nodes

### Phase 3: MAST Implementation
- Create redemption path trees
- Implement script path validation
- Add emergency override paths
- Test all MAST branches

### Phase 4: Advanced Features
- Key path optimization
- PSBT support for hardware wallets
- Witness discount calculations
- Privacy analysis tools

### Phase 5: Wallet Enhancement
- Full GUI support for Taproot features
- Path selection interface
- Privacy indicators
- Advanced coin control

### Phase 6: Production Deployment
- Comprehensive Taproot-specific testing
- Security audit focusing on script paths
- Performance optimization
- Mainnet activation

## 13. Testing Strategy

### 13.1 Taproot-Specific Tests

```cpp
// Location: src/test/digidollar_taproot_tests.cpp
BOOST_AUTO_TEST_CASE(digidollar_p2tr_validation) {
    // Test P2TR output creation
    // Verify witness structure
    // Validate script paths
}

BOOST_AUTO_TEST_CASE(schnorr_oracle_threshold) {
    // Test OP_CHECKSIGADD with oracles
    // Verify batch validation
    // Test threshold edge cases
}

BOOST_AUTO_TEST_CASE(mast_redemption_paths) {
    // Test each redemption path
    // Verify unused paths remain hidden
    // Validate Merkle proofs
}
```

### 13.2 Integration Tests with Taproot

```python
# Location: test/functional/digidollar_taproot.py
class DigiDollarTaprootTest(DigiByteTestFramework):
    def test_p2tr_privacy(self):
        """Verify all DD transactions look identical"""

    def test_schnorr_oracle_efficiency(self):
        """Test batch verification performance"""

    def test_mast_path_selection(self):
        """Test different redemption scenarios"""
```

## 14. Implementation Blueprint

### 14.1 New Files to Create

#### Taproot-Specific Modules
```
src/digidollar/
├── taproot_scripts.h               // P2TR script construction
├── taproot_scripts.cpp             // Implementation
├── schnorr_oracle.h                // Schnorr oracle system
├── schnorr_oracle.cpp              // Oracle implementation
├── mast_builder.h                  // MAST tree construction
├── mast_builder.cpp                // MAST implementation
└── taproot_validation.h/cpp        // Taproot-specific validation
```

### 14.2 Modified Files

#### Script System Updates
```cpp
// src/script/interpreter.cpp
// Add OP_DIGIDOLLAR support in Tapscript context
case OP_DIGIDOLLAR:
    if (sigversion != SigVersion::TAPSCRIPT)
        return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
    // Implementation...

// src/script/standard.cpp
// Add P2TR DigiDollar output detection
bool IsP2TRDigiDollar(const CScript& script);
```

### 14.3 New Classes

#### Taproot Classes
```cpp
// src/digidollar/taproot_scripts.h
class CDigiDollarTaprootBuilder {
    TaprootBuilder m_builder;
    std::vector<CScript> m_spend_paths;

public:
    void AddRedemptionPath(const CScript& script, int weight = 1);
    void AddEmergencyPath(const std::vector<XOnlyPubKey>& oracles);
    CScript Finalize(const XOnlyPubKey& internal_key);
};

// src/digidollar/schnorr_oracle.h
class CSchnorrOracleValidator {
    bool ValidateThreshold(const std::vector<SchnorrSig>& sigs,
                          const std::vector<XOnlyPubKey>& pubkeys,
                          uint32_t threshold);
    bool BatchVerify(const std::vector<OracleMessage>& messages);
};
```

### 14.4 Build System Updates

```cmake
# src/Makefile.am
# Add Taproot-specific files
libdigibyte_server_a_SOURCES += \
  digidollar/taproot_scripts.cpp \
  digidollar/schnorr_oracle.cpp \
  digidollar/mast_builder.cpp \
  digidollar/taproot_validation.cpp
```

## Conclusion

**In Simple Terms**: DigiDollar represents a major advancement for DigiByte - a native stablecoin that's always worth $1 USD, built using the latest blockchain technology. By leveraging Taproot, we've created a system that's private, efficient, and secure.

This Taproot-enhanced specification transforms DigiDollar into a privacy-preserving, efficient, and flexible stablecoin system. By leveraging P2TR outputs, Schnorr signatures, and MAST, we achieve:

1. **Complete Transaction Privacy**: All DigiDollar operations appear identical
2. **Enhanced Efficiency**: 30-50% smaller transactions using key path spending
3. **Future Flexibility**: Multiple redemption paths and upgrade mechanisms
4. **Better Security**: Schnorr signatures and hidden script complexity
5. **Improved Scalability**: Batch verification and witness discounts

**What This Means for Users**:
- **Lower Fees**: Smaller transactions mean you pay less to use DigiDollar
- **Better Privacy**: Nobody can tell what type of DigiDollar transaction you're making
- **More Options**: Multiple ways to redeem your collateral when needed
- **Future-Proof**: The system can be upgraded without breaking existing functionality
- **Flexible Collateral**: Choose your lock period from 30 days (300%) to 10 years (100%)
- **Treasury Model**: Like U.S. Treasury bonds, longer commitments get better rates

The implementation maintains full UTXO compatibility while providing cutting-edge features that position DigiDollar as the most advanced stablecoin on any UTXO blockchain.

**Next Steps**: With this specification, developers can begin implementing DigiDollar on the DigiByte blockchain, bringing stable, decentralized digital dollars to the DigiByte ecosystem.
