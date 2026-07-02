# DigiDollar Technical Specification v1.0
## Complete Implementation Guide for DigiByte v8.26

# Executive Summary

DigiDollar is a fully decentralized USD-pegged stablecoin native to the DigiByte blockchain, implementing a collateral-backed model with time-locked DGB reserves. This specification provides the complete technical blueprint for implementing DigiDollar on DigiByte v8.26, leveraging Taproot for enhanced privacy and efficiency while maintaining a robust 1:1 USD peg through decentralized oracles and multi-tier collateralization.

## Core Features
- **Native UTXO Implementation**: Built directly into DigiByte Core without sidechains
- **Treasury-Model Collateral**: 10 canonical lock periods (1 hour to 10 years) with ratios from 1000% to 200%
- **Four-Layer Protection**: Higher collateral + DCA + ERR + Reserve dynamics
- **Taproot Enhanced**: P2TR outputs for privacy, efficiency, and flexibility
- **Decentralized Oracles**: 30 hardcoded nodes with 8-of-15 threshold consensus
- **Full Fungibility**: Any DigiDollar can redeem any collateral position

# 1. System Architecture

## 1.1 Core Components

### 1.1.1 Consensus Layer Modifications
```cpp
// Location: src/consensus/digidollar.h
namespace DigiDollar {
    // Core consensus parameters
    struct ConsensusParams {
        // Collateral ratios (higher for shorter periods - treasury model)
        std::map<int64_t, int> collateralRatios = {
            {30 * 24 * 60 * 4, 500},           // 30 days: 500%
            {90 * 24 * 60 * 4, 400},           // 3 months: 400%
            {180 * 24 * 60 * 4, 350},          // 6 months: 350%
            {365 * 24 * 60 * 4, 300},          // 1 year: 300%
            {3 * 365 * 24 * 60 * 4, 250},      // 3 years: 250%
            {5 * 365 * 24 * 60 * 4, 225},      // 5 years: 225%
            {7 * 365 * 24 * 60 * 4, 212},      // 7 years: 212%
            {10 * 365 * 24 * 60 * 4, 200}      // 10 years: 200%
        };

        // Transaction limits
        CAmount minMintAmount = 100 * CENT;        // $100 minimum
        CAmount maxMintAmount = 100000 * CENT;     // $100k maximum per tx
        CAmount minOutputAmount = 100;             // $1 minimum output

        // Oracle configuration
        uint32_t oracleCount = 30;                 // Total hardcoded oracles
        uint32_t activeOracles = 15;               // Active per epoch
        uint32_t oracleThreshold = 8;              // 8-of-15 consensus
        uint32_t priceValidBlocks = 20;            // 5 minutes at 15s blocks

        // Protection mechanisms
        uint32_t volatilityThreshold = 20;         // 20% triggers DCA
        uint32_t emergencyThreshold = 100;         // 100% collateral triggers ERR

        // System health thresholds for DCA
        struct DCALevel {
            int systemCollateral;   // System-wide collateral %
            int multiplier;         // Collateral requirement multiplier
        };

        std::vector<DCALevel> dcaLevels = {
            {150, 100},  // >150%: Normal (100% of base requirement)
            {120, 125},  // 120-150%: +25% collateral required
            {110, 150},  // 110-120%: +50% collateral required
            {100, 200},  // <110%: +100% collateral required
        };
    };
}
```

### 1.1.2 Transaction Types
```cpp
// Location: src/primitives/transaction.h
enum DigiDollarTxType : uint8_t {
    DD_TX_NONE = 0,
    DD_TX_MINT = 1,      // Lock DGB, create DigiDollars
    DD_TX_TRANSFER = 2,  // Transfer DigiDollars between addresses
    DD_TX_REDEEM = 3,    // Burn DigiDollars, unlock DGB (NORMAL and ERR paths)
    DD_TX_MAX = 4        // Sentinel for validation
};
// NOTE: ERR is a redemption PATH, not a tx type. Both paths use DD_TX_REDEEM.

// DigiDollar transaction marker in nVersion
static const int32_t DD_TX_VERSION = 0x0D1D0770;  // "DigiDollar" marker
```

## 1.2 Data Structures

### 1.2.1 DigiDollar Output Structure
```cpp
// Location: src/digidollar/outputs.h
class CDigiDollarOutput {
public:
    CAmount nDDAmount;          // DigiDollar amount in cents (100 = $1)
    uint256 collateralId;       // Links to specific collateral UTXO
    int64_t nLockTime;          // Time-lock period in blocks

    // P2TR specific
    XOnlyPubKey internalKey;    // Taproot internal key
    uint256 taprootMerkleRoot;  // MAST root for redemption paths

    // Serialization
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nDDAmount);
        READWRITE(collateralId);
        READWRITE(nLockTime);
        READWRITE(internalKey);
        READWRITE(taprootMerkleRoot);
    }

    // Validation
    bool IsValid() const;
    CAmount GetUSDValue() const { return nDDAmount; }
};
```

### 1.2.2 Collateral Position Structure
```cpp
// Location: src/digidollar/collateral.h
class CCollateralPosition {
public:
    COutPoint outpoint;         // The locked DGB UTXO
    CAmount dgbLocked;          // Amount of DGB locked
    CAmount ddMinted;           // Amount of DD created
    int64_t unlockHeight;       // Block height when redeemable
    int collateralRatio;        // Initial collateral ratio used

    // Taproot redemption paths (only 2 paths - NO partial, NO oracle emergency override)
    enum RedemptionPath {
        PATH_NORMAL = 0,        // Standard timelock expiry (system health >= 100%)
        PATH_ERR = 1            // Emergency Redemption Ratio (health < 100%, burn more DD)
    };

    TaprootSpendData spendData;
    std::vector<RedemptionPath> availablePaths;

    // System health tracking
    CAmount GetCurrentCollateralRatio(CAmount currentPrice) const;
    bool IsHealthy(CAmount currentPrice) const;
    CAmount GetRequiredDDForRedemption(int systemCollateral) const;
};
```

## 1.3 Script System Extensions

### 1.3.1 New Opcodes
```cpp
// Location: src/script/script.h
enum opcodetype {
    // ... existing opcodes ...

    // DigiDollar specific opcodes (using OP_NOP slots for soft fork)
    OP_DIGIDOLLAR = 0xbb,          // Marks DD outputs (OP_NOP11)
    OP_DDVERIFY = 0xbc,            // Verify DD conditions (OP_NOP12)
    OP_CHECKPRICE = 0xbd,          // Check oracle price (OP_NOP13)
    OP_CHECKCOLLATERAL = 0xbe,     // Verify collateral ratio (OP_NOP14)

    // Taproot support
    OP_CHECKSIGADD = 0xba,         // BIP342 for oracle threshold
};
```

### 1.3.2 P2TR DigiDollar Scripts
```cpp
// Location: src/digidollar/scripts.cpp

// Create collateral locking script with MAST paths
CScript CreateCollateralP2TR(const DigiDollarMintParams& params) {
    TaprootBuilder builder;

    // Path 1: Normal redemption after timelock
    CScript normalPath;
    normalPath << params.lockHeight << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    normalPath << OP_DIGIDOLLAR << params.ddAmount << OP_EQUALVERIFY;
    normalPath << params.ownerKey << OP_CHECKSIG;
    builder.Add(normalPath, 64);  // Most likely path

    // Path 2: Emergency override (8-of-15 oracles)
    CScript emergencyPath;
    emergencyPath << OP_DIGIDOLLAR << params.ddAmount << OP_EQUALVERIFY;
    for (int i = 0; i < 15; i++) {
        emergencyPath << GetOracleKey(i) << OP_CHECKSIGADD;
    }
    emergencyPath << OP_8 << OP_EQUAL;
    builder.Add(emergencyPath, 4);  // Rare path

    // Path 3: Partial redemption
    CScript partialPath;
    partialPath << OP_DIGIDOLLAR << OP_DDVERIFY;
    partialPath << params.ownerKey << OP_CHECKSIGVERIFY;
    partialPath << OP_CHECKPRICE;  // Verify current price
    builder.Add(partialPath, 16);

    // Path 4: ERR redemption (when system < 100% collateralized)
    CScript errPath;
    errPath << OP_CHECKCOLLATERAL << OP_100 << OP_LESSTHAN << OP_VERIFY;
    errPath << OP_DIGIDOLLAR << OP_DDVERIFY;
    errPath << params.ownerKey << OP_CHECKSIG;
    builder.Add(errPath, 2);  // Very rare

    // Finalize with internal key
    builder.Finalize(params.internalKey);

    // Create P2TR output
    CScript scriptPubKey;
    scriptPubKey << OP_1 << builder.GetOutput();
    return scriptPubKey;
}

// Create DigiDollar token output
CScript CreateDigiDollarP2TR(const XOnlyPubKey& owner, CAmount ddAmount) {
    TaprootBuilder builder;

    // Simple transfer path (key path spending)
    CScript transferPath;
    transferPath << OP_DIGIDOLLAR << ddAmount << OP_EQUALVERIFY;
    transferPath << owner << OP_CHECKSIG;
    builder.Add(transferPath);

    builder.Finalize(owner);

    CScript scriptPubKey;
    scriptPubKey << OP_1 << builder.GetOutput();
    return scriptPubKey;
}
```

# 2. Oracle System Implementation

## 2.1 Oracle Infrastructure

### 2.1.1 Hardcoded Oracle Nodes
```cpp
// Location: src/chainparams.cpp
class CMainParams : public CChainParams {
    // ... existing params ...

    // DigiDollar oracle nodes (30 hardcoded, 15 active per epoch)
    std::vector<OracleNode> vOracleNodes = {
        {"oracle1.digibyte.io", "xpub661MyMwAqRbcFW31YEwpkMuc..."},
        {"oracle2.diginode.tools", "xpub661MyMwAqRbcGczjuLamPf..."},
        {"oracle3.dgb.community", "xpub661MyMwAqRbcFtXgS5sYJA..."},
        // ... 27 more oracle nodes
    };

    // Oracle epoch configuration
    consensus.nOracleEpochBlocks = 100;  // Rotate oracles every 100 blocks
    consensus.nOracleUpdateInterval = 4;  // Update price every minute (4 blocks)
};
```

### 2.1.2 Oracle Price Message
```cpp
// Location: src/primitives/oracle.h
class COraclePriceMessage {
public:
    // Price data
    uint32_t nTimestamp;
    uint32_t nPricePerDGB;      // Price in micro-USD (1,000,000 = $1.00)
    uint32_t nBlockHeight;

    // Signature data (Schnorr)
    std::vector<unsigned char> vchSchnorrSig;
    XOnlyPubKey oraclePubKey;

    // Anti-replay
    uint256 nonce;

    // Methods
    bool VerifySignature() const;
    uint256 GetMessageHash() const;

    ADD_SERIALIZE_METHODS;
};
```

### 2.1.3 Oracle Selection Algorithm
```cpp
// Location: src/consensus/oracle.cpp
std::vector<COracleNode> SelectActiveOracles(int nHeight) {
    // Deterministic selection based on block height
    uint256 epochSeed = GetBlockHash(nHeight / 100 * 100);

    // Shuffle all 30 oracles using epoch seed
    std::vector<COracleNode> shuffled = vOracleNodes;
    std::mt19937 rng(epochSeed.GetUint64(0));
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    // Return first 15 oracles for this epoch
    return std::vector<COracleNode>(shuffled.begin(), shuffled.begin() + 15);
}

// Aggregate oracle prices with threshold validation
CAmount GetConsensusPrice(const std::vector<COraclePriceMessage>& prices) {
    if (prices.size() < 8) {
        throw std::runtime_error("Insufficient oracle consensus");
    }

    // Verify all signatures
    for (const auto& price : prices) {
        if (!price.VerifySignature()) {
            throw std::runtime_error("Invalid oracle signature");
        }
    }

    // Calculate median price (resistant to outliers)
    std::vector<uint32_t> validPrices;
    for (const auto& p : prices) {
        validPrices.push_back(p.nPricePerDGB);
    }
    std::sort(validPrices.begin(), validPrices.end());

    return validPrices[validPrices.size() / 2];
}
```

## 2.2 Price Feed Integration

### 2.2.1 Block Price Inclusion
```cpp
// Location: src/validation.cpp
bool CheckBlockOracleData(const CBlock& block, const CChainParams& chainparams) {
    // Extract oracle data from coinbase
    const CTransaction& coinbase = block.vtx[0];

    // Look for OP_RETURN with oracle data
    COracleBundle oracleData;
    bool foundOracle = false;

    for (const auto& output : coinbase.vout) {
        if (output.scriptPubKey[0] == OP_RETURN) {
            if (ExtractOracleData(output.scriptPubKey, oracleData)) {
                foundOracle = true;
                break;
            }
        }
    }

    if (!foundOracle) {
        // Allow blocks without oracle data but restrict DD transactions
        return !BlockContainsDigiDollarTx(block);
    }

    // Verify oracle signatures (at least 8 of 15)
    auto activeOracles = SelectActiveOracles(block.nHeight);
    int validSigs = 0;

    for (const auto& sig : oracleData.signatures) {
        if (VerifyOracleSignature(sig, activeOracles)) {
            validSigs++;
        }
    }

    return validSigs >= 8;
}
```

### 2.2.2 Oracle Node Implementation
```cpp
// Location: src/oracle/node.cpp
class COracleNode {
private:
    std::vector<std::string> m_exchanges = {
        "binance", "kucoin", "bittrex", "okex", "huobi"
    };

public:
    CAmount FetchMedianPrice() {
        std::vector<double> prices;

        // Fetch from each exchange
        for (const auto& exchange : m_exchanges) {
            try {
                double price = FetchExchangePrice(exchange);
                if (price > 0) {
                    prices.push_back(price);
                }
            } catch (...) {
                LogPrintf("Failed to fetch from %s\n", exchange);
            }
        }

        // Calculate median
        if (prices.size() < 3) {
            throw std::runtime_error("Insufficient price sources");
        }

        std::sort(prices.begin(), prices.end());
        return prices[prices.size() / 2] * 1000000;  // Convert to micro-USD
    }

    COraclePriceMessage CreatePriceMessage() {
        COraclePriceMessage msg;
        msg.nTimestamp = GetTime();
        msg.nPricePerDGB = FetchMedianPrice();
        msg.nBlockHeight = chainActive.Height();
        msg.nonce = GetRandHash();

        // Sign with oracle key
        SignSchnorr(msg.GetMessageHash(), m_oracleKey, msg.vchSchnorrSig);

        return msg;
    }
};
```

# 3. Transaction Implementation

## 3.1 Mint Transaction

### 3.1.1 Mint Transaction Structure
```cpp
// Location: src/digidollar/mint.cpp
class CMintTransaction {
public:
    // Calculate required collateral with DCA adjustment
    static CAmount CalculateRequiredCollateral(
        CAmount ddAmount,
        int64_t lockBlocks,
        CAmount currentPrice,
        int systemCollateral) {

        // Get base collateral ratio for lock period
        int baseRatio = GetCollateralRatioForLockTime(lockBlocks);

        // Apply DCA multiplier based on system health
        double dcaMultiplier = GetDCAMultiplier(systemCollateral);
        int adjustedRatio = baseRatio * dcaMultiplier;

        // Calculate DGB required
        CAmount usdValue = ddAmount;  // DD amount = USD value in cents
        CAmount dgbFor100Percent = (usdValue * COIN) / currentPrice;

        return (dgbFor100Percent * adjustedRatio) / 100;
    }

    static bool CreateMintTransaction(
        const CWallet& wallet,
        const MintParams& params,
        CMutableTransaction& tx) {

        // Set DD transaction version
        tx.nVersion = DD_TX_VERSION | (DD_TX_MINT << 16);

        // Get current price and system health
        CAmount currentPrice = GetConsensusPrice();
        int systemCollateral = GetSystemCollateralRatio();

        // Calculate required collateral
        CAmount requiredDGB = CalculateRequiredCollateral(
            params.ddAmount,
            params.lockDays * 24 * 60 * 4,
            currentPrice,
            systemCollateral
        );

        // Select inputs from wallet
        std::vector<COutput> vCoins;
        wallet.AvailableCoins(vCoins);

        CAmount totalIn = 0;
        for (const auto& coin : vCoins) {
            tx.vin.push_back(CTxIn(coin.outpoint));
            totalIn += coin.tx->vout[coin.i].nValue;
            if (totalIn >= requiredDGB + params.fee) break;
        }

        if (totalIn < requiredDGB + params.fee) {
            return error("Insufficient DGB for collateral");
        }

        // Create P2TR collateral output
        DigiDollarMintParams mintParams;
        mintParams.ddAmount = params.ddAmount;
        mintParams.lockHeight = chainActive.Height() + params.lockDays * 24 * 60 * 4;
        mintParams.ownerKey = wallet.GenerateNewKey();
        mintParams.internalKey = XOnlyPubKey(mintParams.ownerKey.GetPubKey());

        CScript collateralScript = CreateCollateralP2TR(mintParams);
        tx.vout.push_back(CTxOut(requiredDGB, collateralScript));

        // Create P2TR DigiDollar output
        CPubKey ddPubKey = wallet.GenerateNewKey();
        CScript ddScript = CreateDigiDollarP2TR(XOnlyPubKey(ddPubKey), params.ddAmount);
        tx.vout.push_back(CTxOut(0, ddScript));  // 0 DGB, value in witness

        // Add metadata output
        CScript metadata;
        metadata << OP_RETURN << OP_DIGIDOLLAR;
        metadata << SerializeHash(params.ddAmount, requiredDGB, params.lockDays);
        tx.vout.push_back(CTxOut(0, metadata));

        // Add change if any
        CAmount change = totalIn - requiredDGB - params.fee;
        if (change > DUST_THRESHOLD) {
            CTxDestination changeDest = wallet.GetNewAddress();
            tx.vout.push_back(CTxOut(change, GetScriptForDestination(changeDest)));
        }

        // Sign inputs
        return wallet.SignTransaction(tx);
    }
};
```

### 3.1.2 Mint Validation
```cpp
// Location: src/validation.cpp
bool ValidateMintTransaction(const CTransaction& tx, CValidationState& state) {
    // Check transaction version
    if ((tx.nVersion & 0xFFFF) != DD_TX_VERSION) {
        return state.Invalid("bad-dd-version");
    }

    if (((tx.nVersion >> 16) & 0xFF) != DD_TX_MINT) {
        return state.Invalid("not-mint-tx");
    }

    // Extract parameters from outputs
    CAmount collateralAmount = 0;
    CAmount ddAmount = 0;
    int64_t lockTime = 0;

    for (const auto& output : tx.vout) {
        if (output.scriptPubKey.IsPayToTaproot()) {
            if (IsCollateralOutput(output)) {
                collateralAmount = output.nValue;
                ExtractLockTime(output.scriptPubKey, lockTime);
            } else if (IsDigiDollarOutput(output)) {
                ExtractDDAmount(output.scriptPubKey, ddAmount);
            }
        }
    }

    // Verify collateral ratio
    CAmount currentPrice = GetLatestOraclePrice();
    int systemCollateral = GetSystemCollateralRatio();
    CAmount requiredCollateral = CMintTransaction::CalculateRequiredCollateral(
        ddAmount, lockTime, currentPrice, systemCollateral
    );

    if (collateralAmount < requiredCollateral) {
        return state.Invalid("insufficient-collateral");
    }

    // Check mint amount limits
    if (ddAmount < consensus.nDDMinMintAmount) {
        return state.Invalid("mint-below-minimum");
    }

    if (ddAmount > consensus.nDDMaxMintAmount) {
        return state.Invalid("mint-above-maximum");
    }

    // Verify volatility check
    if (IsVolatilityFreeze()) {
        return state.Invalid("volatility-freeze-active");
    }

    return true;
}
```

## 3.2 Transfer Transaction

### 3.2.1 Transfer Implementation
```cpp
// Location: src/digidollar/transfer.cpp
bool CreateTransferTransaction(
    const CWallet& wallet,
    const std::vector<CRecipient>& vecSend,
    CMutableTransaction& tx) {

    // Set transaction version
    tx.nVersion = DD_TX_VERSION | (DD_TX_TRANSFER << 16);

    // Find DigiDollar UTXOs
    std::vector<COutput> vDDCoins;
    wallet.GetDigiDollarCoins(vDDCoins);

    // Calculate total to send
    CAmount totalSend = 0;
    for (const auto& recipient : vecSend) {
        totalSend += recipient.nAmount;
    }

    // Select DD inputs
    CAmount totalIn = 0;
    for (const auto& coin : vDDCoins) {
        tx.vin.push_back(CTxIn(coin.outpoint));
        totalIn += GetDigiDollarAmount(coin.tx->vout[coin.i]);
        if (totalIn >= totalSend) break;
    }

    if (totalIn < totalSend) {
        return error("Insufficient DigiDollars");
    }

    // Create outputs for recipients
    for (const auto& recipient : vecSend) {
        XOnlyPubKey recipientKey = GetXOnlyPubKey(recipient.destination);
        CScript ddScript = CreateDigiDollarP2TR(recipientKey, recipient.nAmount);
        tx.vout.push_back(CTxOut(0, ddScript));
    }

    // Create change output if needed
    CAmount change = totalIn - totalSend;
    if (change > 0) {
        CPubKey changeKey = wallet.GenerateNewKey();
        CScript changeScript = CreateDigiDollarP2TR(XOnlyPubKey(changeKey), change);
        tx.vout.push_back(CTxOut(0, changeScript));
    }

    // Add DGB input for fees
    CAmount fee = CalculateFee(tx);
    std::vector<COutput> vDGBCoins;
    wallet.AvailableCoins(vDGBCoins);

    for (const auto& coin : vDGBCoins) {
        if (coin.tx->vout[coin.i].nValue >= fee) {
            tx.vin.push_back(CTxIn(coin.outpoint));

            // Add DGB change if needed
            CAmount dgbChange = coin.tx->vout[coin.i].nValue - fee;
            if (dgbChange > DUST_THRESHOLD) {
                CTxDestination dest = wallet.GetNewAddress();
                tx.vout.push_back(CTxOut(dgbChange, GetScriptForDestination(dest)));
            }
            break;
        }
    }

    // Sign using key path (most efficient)
    return wallet.SignP2TRKeyPath(tx);
}
```

## 3.3 Redemption Transaction

### 3.3.1 Redemption with ERR
```cpp
// Location: src/digidollar/redeem.cpp
class CRedemptionTransaction {
public:
    static CAmount CalculateRequiredDD(
        const CCollateralPosition& position,
        int systemCollateral) {

        CAmount baseRequired = position.ddMinted;

        // Apply ERR if system undercollateralized
        if (systemCollateral < 100) {
            // Required DD = Original DD × (100% / System Collateral %)
            baseRequired = (baseRequired * 100) / systemCollateral;
        }

        return baseRequired;
    }

    static bool CreateRedemptionTransaction(
        const CWallet& wallet,
        const COutPoint& collateralOutpoint,
        RedemptionPath path,
        CMutableTransaction& tx) {

        // Set transaction version
        tx.nVersion = DD_TX_VERSION | (DD_TX_REDEEM << 16);

        // Get collateral position
        CCollateralPosition position;
        if (!GetCollateralPosition(collateralOutpoint, position)) {
            return error("Collateral position not found");
        }

        // Check if timelock expired
        if (chainActive.Height() < position.unlockHeight &&
            path != PATH_EMERGENCY && path != PATH_ERR) {
            return error("Timelock not expired");
        }

        // Calculate required DD (may be higher due to ERR)
        int systemCollateral = GetSystemCollateralRatio();
        CAmount requiredDD = CalculateRequiredDD(position, systemCollateral);

        // Add collateral input
        tx.vin.push_back(CTxIn(collateralOutpoint));

        // Find and add DigiDollar inputs
        std::vector<COutput> vDDCoins;
        wallet.GetDigiDollarCoins(vDDCoins);

        CAmount ddToBurn = 0;
        for (const auto& coin : vDDCoins) {
            tx.vin.push_back(CTxIn(coin.outpoint));
            ddToBurn += GetDigiDollarAmount(coin.tx->vout[coin.i]);
            if (ddToBurn >= requiredDD) break;
        }

        if (ddToBurn < requiredDD) {
            return error("Insufficient DigiDollars for redemption (ERR may apply)");
        }

        // Calculate DGB to release
        CAmount currentPrice = GetConsensusPrice();
        CAmount dgbToRelease = position.dgbLocked;

        // For partial redemption, calculate proportional release
        if (path == PATH_PARTIAL && ddToBurn < position.ddMinted) {
            dgbToRelease = (position.dgbLocked * ddToBurn) / position.ddMinted;
        }

        // Create DGB output
        CTxDestination userDest = wallet.GetNewAddress();
        tx.vout.push_back(CTxOut(dgbToRelease, GetScriptForDestination(userDest)));

        // If partial, create new collateral output for remainder
        if (path == PATH_PARTIAL && ddToBurn < position.ddMinted) {
            CAmount remainingDGB = position.dgbLocked - dgbToRelease;
            CAmount remainingDD = position.ddMinted - ddToBurn;

            // Create new collateral output with updated values
            DigiDollarMintParams newParams;
            newParams.ddAmount = remainingDD;
            newParams.lockHeight = position.unlockHeight;
            newParams.ownerKey = wallet.GetKey(position.ownerKey);

            CScript newCollateral = CreateCollateralP2TR(newParams);
            tx.vout.push_back(CTxOut(remainingDGB, newCollateral));
        }

        // Return excess DD if any
        if (ddToBurn > requiredDD) {
            CAmount excessDD = ddToBurn - requiredDD;
            CPubKey changeKey = wallet.GenerateNewKey();
            CScript changeScript = CreateDigiDollarP2TR(XOnlyPubKey(changeKey), excessDD);
            tx.vout.push_back(CTxOut(0, changeScript));
        }

        // Sign using appropriate script path
        return wallet.SignP2TRScriptPath(tx, 0, SIGHASH_DEFAULT, path);
    }
};
```

# 4. Protection Mechanisms

## 4.1 Dynamic Collateral Adjustment (DCA)

```cpp
// Location: src/consensus/dca.cpp
double GetDCAMultiplier(int systemCollateral) {
    for (const auto& level : consensus.dcaLevels) {
        if (systemCollateral >= level.systemCollateral) {
            return level.multiplier / 100.0;
        }
    }
    return 2.0;  // Maximum 200% multiplier
}

bool ApplyDCA(CMutableTransaction& mintTx) {
    int systemCollateral = GetSystemCollateralRatio();

    if (systemCollateral < 150) {
        // System under stress - increase requirements
        LogPrintf("DCA Active: System collateral at %d%%, multiplier %.2f\n",
                  systemCollateral, GetDCAMultiplier(systemCollateral));
        return true;
    }

    return false;
}
```

## 4.2 Emergency Redemption Ratio (ERR)

```cpp
// Location: src/consensus/err.cpp
bool IsERRActive() {
    return GetSystemCollateralRatio() < 100;
}

CAmount GetERRAdjustedRequirement(CAmount originalDD) {
    if (!IsERRActive()) {
        return originalDD;
    }

    int systemCollateral = GetSystemCollateralRatio();

    // Formula: Required DD = Original DD × (100% / System Collateral %)
    // Example: System at 80% → Need 125% of original DD to redeem
    return (originalDD * 100) / systemCollateral;
}
```

## 4.3 Volatility Protection

```cpp
// Location: src/consensus/volatility.cpp
class CVolatilityMonitor {
private:
    static constexpr int LOOKBACK_BLOCKS = 240;  // 1 hour at 15s blocks

public:
    bool IsVolatilityFreeze() {
        std::vector<CAmount> recentPrices;

        // Collect prices from last hour
        for (int i = 0; i < LOOKBACK_BLOCKS; i++) {
            CBlockIndex* pindex = chainActive[chainActive.Height() - i];
            if (!pindex) break;

            CAmount price = GetBlockOraclePrice(pindex);
            if (price > 0) {
                recentPrices.push_back(price);
            }
        }

        if (recentPrices.size() < LOOKBACK_BLOCKS / 2) {
            return true;  // Not enough data - freeze as precaution
        }

        // Calculate volatility
        auto [minPrice, maxPrice] = std::minmax_element(
            recentPrices.begin(), recentPrices.end()
        );

        int volatility = (*maxPrice - *minPrice) * 100 / *minPrice;

        if (volatility > consensus.volatilityThreshold) {
            LogPrintf("Volatility freeze triggered: %d%% change\n", volatility);
            return true;
        }

        return false;
    }

    // Get current freeze status with details
    VolatilityStatus GetStatus() {
        VolatilityStatus status;
        status.isFrozen = IsVolatilityFreeze();
        status.currentVolatility = CalculateCurrentVolatility();
        status.threshold = consensus.volatilityThreshold;
        status.cooldownBlocks = status.isFrozen ? 100 : 0;
        return status;
    }
};
```

## 4.4 System Health Monitoring

```cpp
// Location: src/digidollar/health.cpp
class CSystemHealthMonitor {
public:
    struct SystemHealth {
        CAmount totalDGBLocked;
        CAmount totalDDSupply;
        int overallCollateralRatio;
        std::map<int64_t, TierHealth> tierHealth;
        bool isDCAActive;
        bool isERRActive;
        bool isVolatilityFreeze;
    };

    SystemHealth GetSystemHealth() {
        SystemHealth health;

        // Aggregate all positions
        std::vector<CCollateralPosition> positions;
        GetAllCollateralPositions(positions);

        for (const auto& pos : positions) {
            health.totalDGBLocked += pos.dgbLocked;
            health.totalDDSupply += pos.ddMinted;

            // Group by tier
            int64_t tier = GetTierFromLockTime(pos.unlockHeight - chainActive.Height());
            health.tierHealth[tier].dgbLocked += pos.dgbLocked;
            health.tierHealth[tier].ddSupply += pos.ddMinted;
        }

        // Calculate overall ratio
        CAmount currentPrice = GetConsensusPrice();
        CAmount totalValue = (health.totalDGBLocked * currentPrice) / COIN;
        health.overallCollateralRatio = (totalValue * 100) / health.totalDDSupply;

        // Check protection status
        health.isDCAActive = health.overallCollateralRatio < 150;
        health.isERRActive = health.overallCollateralRatio < 100;
        health.isVolatilityFreeze = CVolatilityMonitor().IsVolatilityFreeze();

        return health;
    }

    // Calculate per-tier health
    struct TierHealth {
        CAmount dgbLocked;
        CAmount ddSupply;
        int collateralRatio;
        int positionCount;
    };
};
```

# 5. Wallet Integration

## 5.1 DigiDollar Address Format

### 5.1.1 Address Prefix Implementation
```cpp
// Location: src/base58.cpp
class CDigiDollarAddress : public CBase58 {
public:
    enum AddressType {
        DD_P2TR_MAINNET = 0x1F,  // Prefix "DD" for mainnet
        DD_P2TR_TESTNET = 0x7F,  // Prefix "TD" for testnet
        DD_P2TR_REGTEST = 0x6F   // Prefix "RD" for regtest
    };

    // Set version bytes to generate "DD" prefix
    bool SetDigiDollar(const CTxDestination& dest, CChainParams::Base58Type type) {
        if (!IsValidDestination(dest)) return false;

        // Only P2TR addresses can be DigiDollar addresses
        if (!std::holds_alternative<WitnessV1Taproot>(dest)) {
            return false;
        }

        // Set appropriate version based on network
        switch (type) {
            case CChainParams::DIGIDOLLAR_ADDRESS:
                SetData(DD_P2TR_MAINNET, dest);
                break;
            case CChainParams::DIGIDOLLAR_ADDRESS_TESTNET:
                SetData(DD_P2TR_TESTNET, dest);
                break;
            default:
                return false;
        }
        return true;
    }

    // Decode DD address back to script
    CTxDestination GetDigiDollarDestination() const {
        if (!IsValid()) return CNoDestination();

        const std::vector<unsigned char>& data = vchData;
        if (data.size() != 32) return CNoDestination();

        // Convert to P2TR destination
        uint256 hash;
        std::copy(data.begin(), data.end(), hash.begin());
        return WitnessV1Taproot(XOnlyPubKey(hash));
    }

    // Validate DD address format
    static bool IsValidDigiDollarAddress(const std::string& str) {
        return str.length() >= 2 &&
               ((str[0] == 'D' && str[1] == 'D') ||  // Mainnet
                (str[0] == 'T' && str[1] == 'D') ||  // Testnet
                (str[0] == 'R' && str[1] == 'D'));   // Regtest
    }
};

// Helper functions
std::string EncodeDigiDollarAddress(const CTxDestination& dest) {
    CDigiDollarAddress addr;
    if (!addr.SetDigiDollar(dest, Params().GetAddressType())) {
        return "";
    }
    return addr.ToString();
}

CTxDestination DecodeDigiDollarAddress(const std::string& str) {
    CDigiDollarAddress addr(str);
    return addr.GetDigiDollarDestination();
}
```

## 5.2 Core Wallet Extensions

```cpp
// Location: src/wallet/digidollarwallet.h
class CDigiDollarWallet : public CWallet {
private:
    // Track DD-specific data
    std::map<uint256, CDigiDollarOutput> m_ddOutputs;
    std::map<COutPoint, CCollateralPosition> m_positions;

public:
    // Balance calculation
    CAmount GetDigiDollarBalance() const;
    CAmount GetLockedCollateral() const;
    std::vector<CCollateralPosition> GetMyPositions() const;

    // Transaction creation
    bool MintDigiDollar(CAmount amount, int lockDays, CWalletTx& wtx);
    bool TransferDigiDollar(const std::vector<CRecipient>& recipients, CWalletTx& wtx);
    bool RedeemDigiDollar(const COutPoint& collateral, RedemptionPath path, CWalletTx& wtx);

    // Position management
    bool CanRedeem(const CCollateralPosition& position) const;
    CAmount GetRedemptionRequirement(const CCollateralPosition& position) const;

    // P2TR support
    bool SignP2TRKeyPath(CMutableTransaction& tx);
    bool SignP2TRScriptPath(CMutableTransaction& tx, int inputIndex, int sighashType, int pathIndex);
};
```

## 5.3 Qt GUI Implementation

### 5.3.1 DigiDollar Tab
```cpp
// Location: src/qt/digidollartab.h
class DigiDollarTab : public QWidget {
    Q_OBJECT

public:
    explicit DigiDollarTab(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~DigiDollarTab();

    void setWalletModel(WalletModel *model);
    void setBalance(const CAmount& ddBalance, const CAmount& dgbLocked);

private:
    Ui::DigiDollarTab *ui;
    WalletModel *walletModel;

    // Main sections
    QWidget *overviewSection;
    QWidget *sendSection;
    QWidget *mintSection;
    QWidget *redeemSection;
    QWidget *positionsSection;

    // Balance displays
    QLabel *labelDDBalance;
    QLabel *labelDGBLocked;
    QLabel *labelOraclePrice;
    QLabel *labelSystemHealth;

    // Send DigiDollar controls
    QValidatedLineEdit *ddSendAddress;
    BitcoinAmountField *ddSendAmount;
    QPushButton *sendButton;

    // Mint controls
    QComboBox *lockPeriodCombo;
    BitcoinAmountField *mintAmount;
    QLabel *requiredCollateral;
    QPushButton *mintButton;

    // Positions table
    QTableWidget *positionsTable;

private Q_SLOTS:
    void on_sendButton_clicked();
    void on_mintButton_clicked();
    void on_redeemButton_clicked();
    void updateDisplayUnit();
    void updateCollateralRequirement();
    void refreshPositions();
    void handleAddressValidation();

Q_SIGNALS:
    void sendDigiDollarRequested(const QString &address, const CAmount &amount);
    void mintDigiDollarRequested(const CAmount &amount, const QString &lockPeriod);
    void redeemDigiDollarRequested(const COutPoint &position);
};
```

### 5.3.2 DigiDollar Tab Implementation
```cpp
// Location: src/qt/digidollartab.cpp
DigiDollarTab::DigiDollarTab(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DigiDollarTab),
    walletModel(nullptr)
{
    ui->setupUi(this);

    // Setup overview section
    setupOverviewSection();

    // Setup send section with DD address validation
    setupSendSection();

    // Setup mint section with collateral calculator
    setupMintSection();

    // Setup positions table
    setupPositionsTable();

    // Connect signals
    connect(ui->sendButton, &QPushButton::clicked, this, &DigiDollarTab::on_sendButton_clicked);
    connect(ui->mintButton, &QPushButton::clicked, this, &DigiDollarTab::on_mintButton_clicked);

    // Setup address validator for DD prefix
    ui->ddSendAddress->setValidator(new DigiDollarAddressValidator(this));
}

void DigiDollarTab::setupSendSection() {
    // Create send form
    QGroupBox *sendGroup = new QGroupBox(tr("Send DigiDollar"));
    QFormLayout *sendLayout = new QFormLayout();

    // Address input with DD prefix validation
    ddSendAddress = new QValidatedLineEdit();
    ddSendAddress->setPlaceholderText("Enter DigiDollar address (DD...)")

    // Amount input
    ddSendAmount = new BitcoinAmountField();
    ddSendAmount->setUnit(BitcoinUnits::DD);  // DigiDollar units

    // Add to layout
    sendLayout->addRow(tr("&To:"), ddSendAddress);
    sendLayout->addRow(tr("&Amount:"), ddSendAmount);

    // Send button
    sendButton = new QPushButton(tr("Send DigiDollar"));
    sendButton->setIcon(platformStyle->SingleColorIcon(":/icons/send"));
    sendLayout->addRow(sendButton);

    sendGroup->setLayout(sendLayout);
}

void DigiDollarTab::on_sendButton_clicked() {
    if (!walletModel) return;

    // Validate DD address
    QString address = ui->ddSendAddress->text();
    if (!CDigiDollarAddress::IsValidDigiDollarAddress(address.toStdString())) {
        QMessageBox::critical(this, tr("Invalid Address"),
            tr("Please enter a valid DigiDollar address starting with 'DD'"));
        return;
    }

    // Get amount
    CAmount amount = ui->ddSendAmount->value();
    if (amount <= 0) {
        QMessageBox::critical(this, tr("Invalid Amount"),
            tr("Please enter a valid amount"));
        return;
    }

    // Create send confirmation dialog
    SendConfirmationDialog dlg(tr("Confirm DigiDollar Send"),
        tr("Are you sure you want to send %1 DigiDollar to %2?")
        .arg(BitcoinUnits::formatWithUnit(BitcoinUnits::DD, amount))
        .arg(address), this);

    if (dlg.exec() == QDialog::Accepted) {
        // Execute send
        Q_EMIT sendDigiDollarRequested(address, amount);
    }
}

void DigiDollarTab::setupMintSection() {
    QGroupBox *mintGroup = new QGroupBox(tr("Mint DigiDollar"));
    QFormLayout *mintLayout = new QFormLayout();

    // Lock period dropdown
    lockPeriodCombo = new QComboBox();
    lockPeriodCombo->addItem(tr("30 days (500% collateral)"), 30);
    lockPeriodCombo->addItem(tr("3 months (400% collateral)"), 90);
    lockPeriodCombo->addItem(tr("6 months (350% collateral)"), 180);
    lockPeriodCombo->addItem(tr("1 year (300% collateral)"), 365);
    lockPeriodCombo->addItem(tr("3 years (250% collateral)"), 1095);
    lockPeriodCombo->addItem(tr("5 years (225% collateral)"), 1825);
    lockPeriodCombo->addItem(tr("7 years (212% collateral)"), 2555);
    lockPeriodCombo->addItem(tr("10 years (200% collateral)"), 3650);

    connect(lockPeriodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DigiDollarTab::updateCollateralRequirement);

    // Mint amount
    mintAmount = new BitcoinAmountField();
    mintAmount->setUnit(BitcoinUnits::DD);
    mintAmount->setMinimumAmount(100 * CENT);  // $100 minimum

    // Required collateral display
    requiredCollateral = new QLabel();
    requiredCollateral->setStyleSheet("QLabel { font-weight: bold; }");

    mintLayout->addRow(tr("Lock Period:"), lockPeriodCombo);
    mintLayout->addRow(tr("Amount to Mint:"), mintAmount);
    mintLayout->addRow(tr("Required DGB:"), requiredCollateral);

    mintButton = new QPushButton(tr("Mint DigiDollar"));
    mintLayout->addRow(mintButton);

    mintGroup->setLayout(mintLayout);
}
```

### 5.3.3 Address Validator
```cpp
// Location: src/qt/digidollaraddressvalidator.h
class DigiDollarAddressValidator : public QValidator {
    Q_OBJECT

public:
    explicit DigiDollarAddressValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
    void fixup(QString &input) const override;

private:
    bool isValidPrefix(const QString &input) const;
};

// Implementation
QValidator::State DigiDollarAddressValidator::validate(QString &input, int &pos) const {
    // Empty is intermediate
    if (input.isEmpty()) {
        return QValidator::Intermediate;
    }

    // Check prefix
    if (!input.startsWith("DD") && !input.startsWith("TD") && !input.startsWith("RD")) {
        // Allow typing D first
        if (input.length() == 1 && (input[0] == 'D' || input[0] == 'T' || input[0] == 'R')) {
            return QValidator::Intermediate;
        }
        return QValidator::Invalid;
    }

    // Full validation
    if (CDigiDollarAddress::IsValidDigiDollarAddress(input.toStdString())) {
        return QValidator::Acceptable;
    }

    // Partial address is intermediate
    if (input.length() < 34) {  // Typical address length
        return QValidator::Intermediate;
    }

    return QValidator::Invalid;
}
```

### 5.3.4 Integration with BitcoinGUI
```cpp
// Location: src/qt/bitcoingui.cpp (modifications)
void BitcoinGUI::createTabs() {
    // ... existing tabs ...

    // Add DigiDollar tab
    digiDollarTab = new DigiDollarTab(platformStyle);
    digiDollarTab->setWalletModel(walletModel);

    // Add to tab widget
    centralWidget->addTab(digiDollarTab, tr("DigiDollar"));
    centralWidget->setTabIcon(centralWidget->indexOf(digiDollarTab),
                             platformStyle->SingleColorIcon(":/icons/digidollar"));

    // Connect signals
    connect(digiDollarTab, &DigiDollarTab::sendDigiDollarRequested,
            this, &BitcoinGUI::handleSendDigiDollar);
    connect(digiDollarTab, &DigiDollarTab::mintDigiDollarRequested,
            this, &BitcoinGUI::handleMintDigiDollar);
}

void BitcoinGUI::handleSendDigiDollar(const QString &address, const CAmount &amount) {
    if (!walletModel) return;

    // Create DD transaction
    WalletModelTransaction transaction;

    // Convert DD address to destination
    CTxDestination dest = DecodeDigiDollarAddress(address.toStdString());
    if (!IsValidDestination(dest)) {
        QMessageBox::critical(this, tr("Error"), tr("Invalid DigiDollar address"));
        return;
    }

    // Create transaction
    CRecipient recipient{dest, amount, false};
    transaction.setRecipients({recipient});

    // Send through wallet model
    WalletModel::SendCoinsReturn result = walletModel->sendDigiDollar(transaction);

    // Handle result
    processSendCoinsReturn(result);
}
```

## 5.4 RPC Interface

```cpp
// Location: src/rpc/digidollar.cpp

// Complete RPC command list for DigiDollar
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "digidollar",       "mintdigidollar",         &mintdigidollar,         {"amount", "lockperiod"} },
    { "digidollar",       "senddigidollar",         &senddigidollar,         {"address", "amount", "comment"} },
    { "digidollar",       "redeemdigidollar",       &redeemdigidollar,       {"position", "path"} },
    { "digidollar",       "getdigidollarbalance",   &getdigidollarbalance,   {} },
    { "digidollar",       "listdigidollarpositions",&listdigidollarpositions,{} },
    { "digidollar",       "getdigidollarstatus",    &getdigidollarstatus,    {} },
    { "digidollar",       "estimatecollateral",     &estimatecollateral,     {"amount", "lockperiod"} },
    { "digidollar",       "getdigidollaraddress",   &getdigidollaraddress,   {"label"} },
    { "digidollar",       "validateddaddress",      &validateddaddress,      {"address"} },
    { "digidollar",       "listdigidollartxs",      &listdigidollartxs,      {"count", "skip"} },
    { "digidollar",       "getoracleprice",         &getoracleprice,         {} },
    { "digidollar",       "getprotectionstatus",    &getprotectionstatus,    {} },
};

// RPC: senddigidollar - Send DigiDollars to a DD address
UniValue senddigidollar(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3) {
        throw std::runtime_error(
            "senddigidollar \"address\" amount ( \"comment\" )\n"
            "\nSend DigiDollars to a DD address.\n"
            "\nArguments:\n"
            "1. address     (string, required) The DigiDollar address (DD prefix)\n"
            "2. amount      (numeric, required) Amount in DigiDollars to send\n"
            "3. comment     (string, optional) A comment for the transaction\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",\n"
            "  \"fee\": xxx\n"
            "}\n"
        );
    }

    // Validate DD address
    std::string address = request.params[0].get_str();
    if (!CDigiDollarAddress::IsValidDigiDollarAddress(address)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DigiDollar address. Must start with 'DD'");
    }

    CTxDestination dest = DecodeDigiDollarAddress(address);
    CAmount amount = AmountFromValue(request.params[1]);

    if (amount <= 0) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    }

    // Create and send transaction
    std::vector<CRecipient> recipients;
    recipients.push_back({dest, amount, false});

    CWalletTx wtx;
    if (!pwallet->CreateDigiDollarTransaction(recipients, wtx)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to create transaction");
    }

    if (!pwallet->CommitTransaction(wtx)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to commit transaction");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtx.GetHash().GetHex());
    result.pushKV("fee", ValueFromAmount(wtx.GetFee()));
    return result;
}

// RPC: getdigidollaraddress - Generate a new DD address
UniValue getdigidollaraddress(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "getdigidollaraddress ( \"label\" )\n"
            "\nGenerate a new DigiDollar address for receiving DD.\n"
            "\nArguments:\n"
            "1. label       (string, optional) Label for the address\n"
            "\nResult:\n"
            "\"address\"    (string) The new DigiDollar address\n"
        );
    }

    // Generate new P2TR key
    CPubKey pubkey = pwallet->GenerateNewKey();
    CTxDestination dest = WitnessV1Taproot(XOnlyPubKey(pubkey));

    // Encode as DD address
    std::string address = EncodeDigiDollarAddress(dest);

    // Add to address book if label provided
    if (!request.params[0].isNull()) {
        std::string label = request.params[0].get_str();
        pwallet->SetAddressBook(dest, label, "digidollar");
    }

    return address;
}

// RPC: validateddaddress - Validate a DD address
UniValue validateddaddress(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "validateddaddress \"address\"\n"
            "\nValidate a DigiDollar address.\n"
            "\nArguments:\n"
            "1. address     (string, required) The DD address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\": true|false,\n"
            "  \"address\": \"address\",\n"
            "  \"scriptPubKey\": \"hex\",\n"
            "  \"ismine\": true|false,\n"
            "  \"iswatchonly\": true|false\n"
            "}\n"
        );
    }

    std::string address = request.params[0].get_str();
    bool isValid = CDigiDollarAddress::IsValidDigiDollarAddress(address);

    UniValue result(UniValue::VOBJ);
    result.pushKV("isvalid", isValid);

    if (isValid) {
        CTxDestination dest = DecodeDigiDollarAddress(address);
        result.pushKV("address", address);
        result.pushKV("scriptPubKey", HexStr(GetScriptForDestination(dest)));

        if (pwallet) {
            result.pushKV("ismine", pwallet->IsMine(dest));
            result.pushKV("iswatchonly", pwallet->IsWatchOnly(dest));
        }
    }

    return result;
}

// RPC: mintdigidollar
UniValue mintdigidollar(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 2) {
        throw std::runtime_error(
            "mintdigidollar amount lockperiod\n"
            "\nMint DigiDollars by locking DGB collateral.\n"
            "\nArguments:\n"
            "1. amount          (numeric) Amount of DigiDollars to mint\n"
            "2. lockperiod      (string) Lock period: 30days, 3months, 6months, 1year, 3years, 5years, 7years, 10years\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",\n"
            "  \"collateral_locked\": xxx,\n"
            "  \"collateral_ratio\": xxx,\n"
            "  \"unlock_height\": xxx\n"
            "}\n"
        );
    }

    CAmount amount = AmountFromValue(request.params[0]);
    std::string period = request.params[1].get_str();

    int lockDays = ParseLockPeriod(period);

    CWalletTx wtx;
    if (!pwallet->MintDigiDollar(amount, lockDays, wtx)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to mint DigiDollar");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtx.GetHash().GetHex());
    result.pushKV("collateral_locked", ValueFromAmount(GetCollateralFromTx(wtx)));
    result.pushKV("collateral_ratio", GetCollateralRatio(wtx));
    result.pushKV("unlock_height", GetUnlockHeight(wtx));

    return result;
}

// RPC: getdigidollarstatus
UniValue getdigidollarstatus(const JSONRPCRequest& request) {
    CSystemHealthMonitor monitor;
    auto health = monitor.GetSystemHealth();

    UniValue result(UniValue::VOBJ);
    result.pushKV("total_dgb_locked", ValueFromAmount(health.totalDGBLocked));
    result.pushKV("total_dd_supply", ValueFromAmount(health.totalDDSupply));
    result.pushKV("system_collateral_ratio", health.overallCollateralRatio);
    result.pushKV("dca_active", health.isDCAActive);
    result.pushKV("err_active", health.isERRActive);
    result.pushKV("volatility_freeze", health.isVolatilityFreeze);

    // Add tier breakdown
    UniValue tiers(UniValue::VOBJ);
    for (const auto& [lockTime, tierHealth] : health.tierHealth) {
        UniValue tier(UniValue::VOBJ);
        tier.pushKV("dgb_locked", ValueFromAmount(tierHealth.dgbLocked));
        tier.pushKV("dd_supply", ValueFromAmount(tierHealth.ddSupply));
        tier.pushKV("ratio", tierHealth.collateralRatio);
        tiers.pushKV(std::to_string(lockTime), tier);
    }
    result.pushKV("tiers", tiers);

    return result;
}

// RPC: listcollateralpositions
UniValue listcollateralpositions(const JSONRPCRequest& request) {
    std::vector<CCollateralPosition> positions;
    pwallet->GetMyPositions(positions);

    UniValue result(UniValue::VARR);
    for (const auto& pos : positions) {
        UniValue position(UniValue::VOBJ);
        position.pushKV("outpoint", pos.outpoint.ToString());
        position.pushKV("dgb_locked", ValueFromAmount(pos.dgbLocked));
        position.pushKV("dd_minted", ValueFromAmount(pos.ddMinted));
        position.pushKV("unlock_height", pos.unlockHeight);
        position.pushKV("can_redeem", chainActive.Height() >= pos.unlockHeight);
        position.pushKV("current_ratio", pos.GetCurrentCollateralRatio(GetConsensusPrice()));

        // ERR info
        if (IsERRActive()) {
            position.pushKV("err_required", ValueFromAmount(
                GetERRAdjustedRequirement(pos.ddMinted)
            ));
        }

        result.push_back(position);
    }

    return result;
}
```

# 6. Testing Strategy

## 6.1 Unit Tests

```cpp
// Location: src/test/digidollar_tests.cpp
BOOST_AUTO_TEST_SUITE(digidollar_tests)

BOOST_AUTO_TEST_CASE(collateral_calculation) {
    // Test base collateral ratios
    BOOST_CHECK_EQUAL(GetCollateralRatioForLockTime(30 * 24 * 60 * 4), 500);
    BOOST_CHECK_EQUAL(GetCollateralRatioForLockTime(10 * 365 * 24 * 60 * 4), 200);

    // Test DCA adjustments
    BOOST_CHECK_EQUAL(GetDCAMultiplier(160), 1.0);   // No adjustment
    BOOST_CHECK_EQUAL(GetDCAMultiplier(130), 1.25);  // +25%
    BOOST_CHECK_EQUAL(GetDCAMultiplier(105), 2.0);   // +100%

    // Test ERR calculations
    CAmount originalDD = 100 * CENT;
    BOOST_CHECK_EQUAL(GetERRAdjustedRequirement(originalDD), originalDD);  // Normal

    // Simulate system at 80% collateral
    SetMockSystemCollateral(80);
    BOOST_CHECK_EQUAL(GetERRAdjustedRequirement(originalDD), 125 * CENT);  // Need 125%
}

BOOST_AUTO_TEST_CASE(p2tr_script_creation) {
    // Test collateral P2TR creation
    DigiDollarMintParams params;
    params.ddAmount = 100 * CENT;
    params.lockHeight = 100000;
    params.ownerKey = GenerateRandomKey();

    CScript collateralScript = CreateCollateralP2TR(params);
    BOOST_CHECK(collateralScript.IsPayToTaproot());

    // Verify script has correct size (33 bytes for P2TR)
    BOOST_CHECK_EQUAL(collateralScript.size(), 34);  // OP_1 + 32 bytes
}

BOOST_AUTO_TEST_CASE(mint_transaction_validation) {
    // Create mock mint transaction
    CMutableTransaction tx;
    tx.nVersion = DD_TX_VERSION | (DD_TX_MINT << 16);

    // Add collateral output (1000 DGB for 100 DD at 10x ratio)
    tx.vout.push_back(CTxOut(1000 * COIN, CreateMockCollateralScript()));

    // Add DD output
    tx.vout.push_back(CTxOut(0, CreateMockDDScript(100 * CENT)));

    // Validate
    CValidationState state;
    BOOST_CHECK(ValidateMintTransaction(tx, state));

    // Test insufficient collateral
    tx.vout[0].nValue = 100 * COIN;  // Too low
    BOOST_CHECK(!ValidateMintTransaction(tx, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "insufficient-collateral");
}

BOOST_AUTO_TEST_SUITE_END()
```

## 6.2 Functional Tests

```python
# Location: test/functional/digidollar_mint.py
#!/usr/bin/env python3
"""Test DigiDollar minting functionality."""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class DigiDollarMintTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [["-digidollar=1"], ["-digidollar=1"]]

    def run_test(self):
        self.log.info("Testing DigiDollar minting...")

        # Mine blocks to get DGB
        self.nodes[0].generate(101)
        self.sync_blocks()

        # Test minimum mint amount
        assert_raises_rpc_error(-8, "below-minimum",
            self.nodes[0].mintdigidollar, 50, "30days")  # $50 < $100 minimum

        # Test successful mint
        result = self.nodes[0].mintdigidollar(100, "1year")
        assert "txid" in result

        # Check collateral ratio (should be 300% for 1 year)
        assert_equal(result["collateral_ratio"], 300)

        # Test DCA activation
        # Simulate system stress...

        # Test ERR activation
        # Simulate undercollateralization...

if __name__ == '__main__':
    DigiDollarMintTest().main()
```

# 7. Implementation Phases

## Phase 1: Foundation (Weeks 1-4)
- Core data structures and opcodes
- Basic P2TR script creation
- Mint transaction structure
- Unit test framework

## Phase 2: Oracle System (Weeks 5-8)
- Hardcoded oracle configuration
- Price message protocol
- Consensus integration
- Oracle node implementation

## Phase 3: Transaction Types (Weeks 9-12)
- Complete mint validation
- Transfer implementation
- Redemption with all paths
- ERR mechanism

## Phase 4: Protection Systems (Weeks 13-16)
- DCA implementation
- Volatility monitoring
- System health tracking
- Emergency procedures

## Phase 5: Wallet Integration (Weeks 17-20)
- GUI components
- RPC commands
- Balance tracking
- Position management

## Phase 6: Testing & Hardening (Weeks 21-24)
- Comprehensive test suite
- Regtest validation
- Testnet deployment
- Security audit

## Phase 7: Mainnet Preparation (Weeks 25-28)
- Final adjustments
- Documentation
- Deployment planning
- Activation strategy

# 8. Security Considerations

## 8.1 Attack Vectors and Mitigations

1. **Oracle Manipulation**: 8-of-15 threshold, median pricing, reputation system
2. **Collateral Runs**: Time locks, ERR mechanism, high initial ratios
3. **Volatility Attacks**: Automatic freezing, DCA adjustments
4. **Sybil Attacks**: Hardcoded oracles, deterministic selection
5. **Front-Running**: P2TR privacy, batch processing

## 8.2 Emergency Procedures

```cpp
// Location: src/digidollar/emergency.cpp
class CEmergencyManager {
    bool TriggerEmergencyShutdown() {
        // Only if system < 50% collateralized
        if (GetSystemCollateralRatio() >= 50) {
            return false;
        }

        // Freeze all minting
        SetEmergencyFlag(EMERGENCY_NO_MINT);

        // Allow only redemptions
        SetEmergencyFlag(EMERGENCY_REDEEM_ONLY);

        // Alert all nodes
        AlertNotify("DigiDollar Emergency: System critically undercollateralized");

        return true;
    }
};
```

# 9. Performance Optimizations

## 9.1 Caching Strategy
- Oracle price caching (5-minute validity)
- Position index for fast lookups
- Balance caching with dirty flags
- Taproot spend data caching

## 9.2 Database Schema
```sql
-- DigiDollar specific tables
CREATE TABLE collateral_positions (
    outpoint BLOB PRIMARY KEY,
    dgb_locked INTEGER,
    dd_minted INTEGER,
    unlock_height INTEGER,
    owner_pubkey BLOB,
    taproot_data BLOB
);

CREATE INDEX idx_unlock_height ON collateral_positions(unlock_height);
CREATE INDEX idx_owner ON collateral_positions(owner_pubkey);

CREATE TABLE dd_outputs (
    outpoint BLOB PRIMARY KEY,
    amount INTEGER,
    owner_pubkey BLOB,
    spent INTEGER DEFAULT 0
);

CREATE INDEX idx_dd_owner ON dd_outputs(owner_pubkey);
```

# 10. Conclusion

This technical specification provides a complete blueprint for implementing DigiDollar on DigiByte v8.26. The system leverages:

1. **Advanced Collateralization**: Treasury-model with 10 canonical tiers from 1000% to 200%
2. **Four-Layer Protection**: Higher ratios + DCA + ERR + market dynamics
3. **Taproot Technology**: P2TR for privacy, efficiency, and flexibility
4. **Decentralized Oracles**: 30 hardcoded nodes with threshold consensus
5. **Full Integration**: Native wallet support, RPC interface, comprehensive testing

The implementation is designed to be:
- **Secure**: Multiple protection layers against all attack vectors
- **Scalable**: Efficient caching and indexing strategies
- **User-Friendly**: Intuitive wallet integration
- **Developer-Friendly**: Comprehensive RPC interface
- **Future-Proof**: Taproot enables upgrades without hard forks

With this specification, developers can proceed with implementing a robust, decentralized stablecoin that will significantly enhance the DigiByte ecosystem.
