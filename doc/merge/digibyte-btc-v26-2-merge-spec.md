# DigiByte v8.22.2 to Bitcoin Core v26.2 Merge Specification (Target: DigiByte v8.26)

## Executive Summary

This specification defines the process for upgrading DigiByte from v8.22.2 to v8.26 (aligned with Bitcoin Core v26.2) while preserving all DigiByte-specific functionality including multi-algorithm mining, Dandelion++, 15-second blocks, and other unique features. The approach uses pre-conversion to minimize conflicts while ensuring all Bitcoin improvements are captured.

## Why Bitcoin Core v26.2

Bitcoin Core v26.2 is the final release in the v26 series and includes all bug fixes and improvements:
- All fixes from v26.0 and v26.1
- Additional stability improvements
- The most tested and stable version of the v26 series
- AssumeUTXO functionality for fast initial sync
- V2 transport protocol (optional)
- Numerous performance and security improvements

## Critical DigiByte Features to Preserve

### Core Functionality
- **Multi-Algorithm Mining**: 5 algorithms (SHA256D, Scrypt, Groestl, Skein, Qubit) + Odocrypt
- **15-Second Block Time**: Fast block generation with custom difficulty adjustment
- **Dandelion++**: Enhanced privacy for transaction propagation
- **DigiShield/MultiShield**: Custom difficulty adjustment algorithms (V1-V4)
- **Custom Block Rewards**: 6-period reward schedule unique to DigiByte
- **21 Billion Supply**: 1000:1 ratio to Bitcoin
- **No RBF**: Replace-by-fee disabled by design

### Technical Specifics
```cpp
// Critical Constants
nPowTargetSpacing = 15;                    // 15 seconds
multiAlgoTargetSpacing = 150;              // 30 * 5
nDefaultPort = 12024;                      // Mainnet
nDefaultPort = 12025;                      // Testnet
pchMessageStart = {0xfa,0xc3,0xb6,0xda};  // Magic bytes
MAX_MONEY = 21000000000 * COIN;           // 21 billion DGB
```

## Phase 1: Bitcoin v26.2 Preparation

**CRITICAL IMPORTANCE**: The pre-conversion step (1.2) is absolutely essential to prevent merge conflicts. Without proper renaming, you will encounter approximately 30,000 unnecessary conflicts that make the merge nearly impossible. The conversion script MUST be run on the Bitcoin v26.2 codebase BEFORE attempting any merge with DigiByte.

### 1.1 Clone and Prepare Bitcoin v26.2

```bash
# Clone Bitcoin repository
git clone https://github.com/bitcoin/bitcoin.git bitcoin-v26.2-for-digibyte
cd bitcoin-v26.2-for-digibyte

# Checkout v26.2 tag
git checkout v26.2
git checkout -b digibyte-v26.2-naming-conversion

# Create backup
git branch backup-original-bitcoin-v26.2
```

### 1.2 Pre-conversion Script

**CRITICAL**: This script MUST be run on Bitcoin v26.2 BEFORE attempting any merge to avoid thousands of unnecessary conflicts.

Create `convert-bitcoin-to-digibyte.sh`:

```bash
#!/bin/bash
# Comprehensive Bitcoin to DigiByte naming conversion script for v26.2
# IMPORTANT: This script ensures ALL Bitcoin references are converted to DigiByte
# to prevent merge conflicts. Only Bitcoin copyright notices are preserved.

set -e  # Exit on error

echo "Starting comprehensive Bitcoin v26.2 to DigiByte naming conversion..."
echo "This will rename ALL Bitcoin references to DigiByte throughout the codebase."
echo ""

# Step 1: File and Directory Renaming (must be done first)
echo "Step 1: Renaming all files and directories..."

# First rename directories (deepest first to avoid issues)
find . -type d -name "*bitcoin*" -o -type d -name "*Bitcoin*" | grep -v ".git" | sort -r | while read dir; do
    newdir=$(echo "$dir" | sed -e 's/bitcoin/digibyte/g' -e 's/Bitcoin/DigiByte/g')
    if [ "$dir" != "$newdir" ] && [ -e "$dir" ]; then
        echo "  Renaming directory: $dir -> $newdir"
        git mv "$dir" "$newdir" 2>/dev/null || mv "$dir" "$newdir"
    fi
done

find . -type d -name "*btc*" -o -type d -name "*BTC*" | grep -v ".git" | sort -r | while read dir; do
    newdir=$(echo "$dir" | sed -e 's/btc/dgb/g' -e 's/BTC/DGB/g')
    if [ "$dir" != "$newdir" ] && [ -e "$dir" ]; then
        echo "  Renaming directory: $dir -> $newdir"
        git mv "$dir" "$newdir" 2>/dev/null || mv "$dir" "$newdir"
    fi
done

# Then rename files
find . -name "*bitcoin*" -o -name "*Bitcoin*" | grep -v ".git" | while read file; do
    newfile=$(echo "$file" | sed -e 's/bitcoin/digibyte/g' -e 's/Bitcoin/DigiByte/g')
    if [ "$file" != "$newfile" ] && [ -e "$file" ]; then
        echo "  Renaming file: $file -> $newfile"
        git mv "$file" "$newfile" 2>/dev/null || mv "$file" "$newfile"
    fi
done

find . -name "*btc*" -o -name "*BTC*" | grep -v ".git" | while read file; do
    newfile=$(echo "$file" | sed -e 's/btc/dgb/g' -e 's/BTC/DGB/g')
    if [ "$file" != "$newfile" ] && [ -e "$file" ]; then
        echo "  Renaming file: $file -> $newfile"
        git mv "$file" "$newfile" 2>/dev/null || mv "$file" "$newfile"
    fi
done

# Step 2: Update ALL file contents
echo ""
echo "Step 2: Updating all file contents..."

# Process all text files (including build files, configs, docs, etc.)
find . -type f \( \
    -name "*.cpp" -o -name "*.h" -o -name "*.c" -o \
    -name "*.mk" -o -name "*.am" -o -name "*.ac" -o \
    -name "*.py" -o -name "*.sh" -o -name "*.bash" -o \
    -name "*.md" -o -name "*.txt" -o -name "*.rc" -o \
    -name "*.xml" -o -name "*.json" -o -name "*.yml" -o -name "*.yaml" -o \
    -name "*.conf" -o -name "*.ini" -o -name "*.cfg" -o \
    -name "*.pro" -o -name "*.pri" -o -name "*.qrc" -o \
    -name "*.ts" -o -name "*.ui" -o -name "*.forms" -o \
    -name "*.cmake" -o -name "CMakeLists.txt" -o \
    -name "*.in" -o -name "*.include" -o \
    -name "*.vcxproj" -o -name "*.vcxproj.filters" -o \
    -name "*.sln" -o -name "*.props" -o \
    -name "Makefile" -o -name "makefile" -o \
    -name "*.plist" -o -name "*.strings" -o \
    -name "*.desktop" -o -name "*.service" -o \
    -name "*.policy" -o -name "*.rules" -o \
    -name "*.nsi" -o -name "*.wxs" -o \
    -name "*.spec" -o -name "*.control" -o \
    -name ".gitignore" -o -name ".gitattributes" -o \
    -name "README*" -o -name "LICENSE*" -o -name "COPYING*" -o \
    -name "AUTHORS*" -o -name "INSTALL*" -o -name "NEWS*" -o \
    -name "CONTRIBUTING*" -o -name "*.1" -o -name "*.5" \
    \) | grep -v ".git/" | while read file; do
    
    echo "  Processing: $file"
    
    # Create temporary file for sed operations
    cp "$file" "$file.tmp"
    
    # Binary names (order matters - do specific before general)
    sed -i \
        -e 's/bitcoind/digibyted/g' \
        -e 's/bitcoin-cli/digibyte-cli/g' \
        -e 's/bitcoin-tx/digibyte-tx/g' \
        -e 's/bitcoin-wallet/digibyte-wallet/g' \
        -e 's/bitcoin-qt/digibyte-qt/g' \
        -e 's/bitcoin-util/digibyte-util/g' \
        -e 's/bitcoin-chainstate/digibyte-chainstate/g' \
        -e 's/bitcoin-node/digibyte-node/g' \
        "$file.tmp"
    
    # Library names
    sed -i \
        -e 's/libbitcoinconsensus/libdigibyteconsensus/g' \
        -e 's/libbitcoin_/libdigibyte_/g' \
        -e 's/libbitcoin/libdigibyte/g' \
        -e 's/LIBBITCOINCONSENSUS/LIBDIGIBYTECONSENSUS/g' \
        -e 's/LIBBITCOIN_/LIBDIGIBYTE_/g' \
        -e 's/LIBBITCOIN/LIBDIGIBYTE/g' \
        -e 's/bitcoinconsensus/digibyteconsensus/g' \
        -e 's/BITCOINCONSENSUS/DIGIBYTECONSENSUS/g' \
        "$file.tmp"
    
    # Header guards and macros
    sed -i \
        -e 's/BITCOIN_/DIGIBYTE_/g' \
        -e 's/_BITCOIN_H/_DIGIBYTE_H/g' \
        -e 's/ENABLE_BITCOIN/ENABLE_DIGIBYTE/g' \
        -e 's/HAVE_BITCOIN/HAVE_DIGIBYTE/g' \
        -e 's/USE_BITCOIN/USE_DIGIBYTE/g' \
        "$file.tmp"
    
    # Class and namespace names
    sed -i \
        -e 's/BitcoinGUI/DigiByteGUI/g' \
        -e 's/BitcoinUnits/DigiByteUnits/g' \
        -e 's/BitcoinApplication/DigiByteApplication/g' \
        -e 's/BitcoinCore/DigiByteCore/g' \
        -e 's/BitcoinConsensus/DigiByteConsensus/g' \
        -e 's/Bitcoin(/DigiByte(/g' \
        -e 's/::Bitcoin/::DigiByte/g' \
        -e 's/namespace bitcoin/namespace digibyte/g' \
        "$file.tmp"
    
    # Configuration and data files
    sed -i \
        -e 's/bitcoin\.conf/digibyte.conf/g' \
        -e 's/bitcoin\.pid/digibyte.pid/g' \
        -e 's/\.bitcoin/\.digibyte/g' \
        -e 's/BITCOIN_CONF_FILENAME/DIGIBYTE_CONF_FILENAME/g' \
        -e 's/BITCOIN_PID_FILENAME/DIGIBYTE_PID_FILENAME/g' \
        "$file.tmp"
    
    # Network and protocol
    sed -i \
        -e 's/bitcoin:/digibyte:/g' \
        -e 's/bitcoin\.org/digibyte.org/g' \
        -e 's/bitcoin\.it/digibyte.org/g' \
        -e 's/bitcointalk/digibytetalk/g' \
        -e 's/bitcoin-dev/digibyte-dev/g' \
        "$file.tmp"
    
    # Currency codes (word boundaries to avoid partial matches)
    sed -i \
        -e 's/\bBTC\b/DGB/g' \
        -e 's/\bbtc\b/dgb/g' \
        -e 's/\bXBT\b/DGB/g' \
        -e 's/\bxbt\b/dgb/g' \
        -e 's/\bmBTC\b/mDGB/g' \
        -e 's/\bmbtc\b/mdgb/g' \
        -e 's/\buBTC\b/uDGB/g' \
        -e 's/\bubtc\b/udgb/g' \
        -e 's/\bsBTC\b/sDGB/g' \
        -e 's/\bsbtc\b/sdgb/g' \
        "$file.tmp"
    
    # Package and project names
    sed -i \
        -e 's/org\.bitcoin/org.digibyte/g' \
        -e 's/bitcoin-core/digibyte-core/g' \
        -e 's/bitcoin_core/digibyte_core/g' \
        -e 's/bitcoin-project/digibyte-project/g' \
        -e 's/bitcoin_project/digibyte_project/g' \
        "$file.tmp"
    
    # General replacements (do these last to avoid double-replacements)
    sed -i \
        -e 's/Bitcoin Core/DigiByte Core/g' \
        -e 's/Bitcoin network/DigiByte network/g' \
        -e 's/Bitcoin protocol/DigiByte protocol/g' \
        -e 's/Bitcoin address/DigiByte address/g' \
        -e 's/Bitcoin transaction/DigiByte transaction/g' \
        -e 's/Bitcoin blockchain/DigiByte blockchain/g' \
        -e 's/Bitcoin wallet/DigiByte wallet/g' \
        -e 's/Bitcoin node/DigiByte node/g' \
        -e 's/Bitcoin mining/DigiByte mining/g' \
        -e 's/Bitcoin developers/DigiByte developers/g' \
        -e 's/The Bitcoin/The DigiByte/g' \
        -e 's/\bBitcoin\b/DigiByte/g' \
        -e 's/\bbitcoin\b/digibyte/g' \
        -e 's/BITCOIN/DIGIBYTE/g' \
        "$file.tmp"
    
    # Move temporary file back
    mv "$file.tmp" "$file"
done

# Step 3: Update Copyright (ADD DigiByte copyright, KEEP Bitcoin copyright)
echo ""
echo "Step 3: Adding DigiByte copyright while preserving Bitcoin copyright..."
find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.c" \) | grep -v ".git/" | while read file; do
    # Check if file has Bitcoin copyright but not DigiByte copyright
    if grep -q "Copyright.*The Bitcoin Core developers" "$file" && ! grep -q "Copyright.*The DigiByte Core developers" "$file"; then
        echo "  Adding DigiByte copyright to: $file"
        # Add DigiByte copyright after Bitcoin copyright
        sed -i '/Copyright.*The Bitcoin Core developers/a\// Copyright (c) 2014-2025 The DigiByte Core developers' "$file"
    fi
done

# Step 4: Restore Bitcoin references where they should be preserved
echo ""
echo "Step 4: Restoring Bitcoin references in copyright notices only..."
find . -type f | grep -v ".git/" | while read file; do
    # Restore "Bitcoin Core developers" in copyright lines only
    sed -i '/Copyright.*The DigiByte Core developers/! s/The DigiByte Core developers/The Bitcoin Core developers/g' "$file"
done

# Step 5: Handle special build and config files
echo ""
echo "Step 5: Updating build system files..."

# Update autoconf files
if [ -f "configure.ac" ]; then
    echo "  Updating configure.ac..."
    sed -i 's/AC_INIT(\[Bitcoin Core\]/AC_INIT([DigiByte Core]/g' configure.ac
    sed -i 's/PACKAGE_NAME="Bitcoin Core"/PACKAGE_NAME="DigiByte Core"/g' configure.ac
fi

# Update Qt project files
find . -name "*.pro" -o -name "*.pri" | while read file; do
    echo "  Updating Qt project file: $file"
    sed -i 's/TARGET = bitcoin/TARGET = digibyte/g' "$file"
done

# Update pkg-config files
find . -name "*.pc.in" | while read file; do
    echo "  Updating pkg-config file: $file"
    sed -i 's/Name: Bitcoin/Name: DigiByte/g' "$file"
done

# Step 6: Verify critical files were updated
echo ""
echo "Step 6: Verifying critical files..."

critical_files=(
    "configure.ac"
    "Makefile.am"
    "src/Makefile.am"
    "src/qt/Makefile.am"
    "src/init.cpp"
    "src/chainparams.cpp"
    "src/net.cpp"
    "src/rpc/server.cpp"
)

for file in "${critical_files[@]}"; do
    if [ -f "$file" ]; then
        if grep -q "bitcoin" "$file" || grep -q "Bitcoin" "$file" || grep -q "BTC" "$file"; then
            echo "  WARNING: $file still contains Bitcoin references!"
            grep -n -E "(bitcoin|Bitcoin|BTC)" "$file" | head -5
        else
            echo "  ✓ $file appears clean"
        fi
    fi
done

# Step 7: Final report
echo ""
echo "Step 7: Generating conversion report..."

# Count remaining Bitcoin references (excluding copyright lines)
echo ""
echo "Remaining Bitcoin references (excluding copyright):"
grep -r -E "(bitcoin|Bitcoin|BTC)" . --exclude-dir=.git | grep -v "Copyright.*Bitcoin Core developers" | wc -l

echo ""
echo "Conversion complete!"
echo ""
echo "IMPORTANT NOTES:"
echo "1. Bitcoin copyright notices have been preserved alongside DigiByte copyrights"
echo "2. Please review any warnings above for files that may need manual attention"
echo "3. Run 'git status' to see all changes"
echo "4. This script should prevent ~30,000 merge conflicts when merging with DigiByte"
echo ""
echo "Next steps:"
echo "1. Review the changes: git diff"
echo "2. Commit the changes: git add -A && git commit -m 'Pre-convert Bitcoin v26.2 to DigiByte naming'"
echo "3. Proceed with the merge into DigiByte repository"
```

### 1.3 Execute Pre-conversion

```bash
chmod +x convert-bitcoin-to-digibyte.sh
./convert-bitcoin-to-digibyte.sh

# Verify the conversion was successful
echo "Checking for remaining Bitcoin references..."
grep -r "bitcoin\|Bitcoin\|BTC" . --exclude-dir=.git | grep -v "Copyright.*Bitcoin Core developers" | wc -l
# This should return a very low number (< 100)

# Commit the pre-converted code
git add -A
git commit -m "Pre-convert Bitcoin v26.2 to DigiByte naming conventions

This commit applies DigiByte naming conventions to Bitcoin v26.2.
No functional changes - naming only to simplify merge.
Both Bitcoin and DigiByte copyrights are preserved.

Based on Bitcoin Core v26.2 which includes:
- All v26.0 and v26.1 fixes
- AssumeUTXO functionality
- V2 transport protocol
- Performance improvements
- Security enhancements"
```

### 1.4 Common Pre-conversion Pitfalls to Avoid

**WARNING**: The following mistakes will result in thousands of merge conflicts:

1. **Skipping the pre-conversion step entirely** - This is the #1 cause of merge failures
2. **Running an incomplete conversion script** - Missing file types or patterns
3. **Not converting directory names** - Leads to path conflicts
4. **Missing build system files** - configure.ac, Makefile.am, etc.
5. **Forgetting about test files** - Test directories need conversion too
6. **Not handling all file extensions** - .json, .yml, .xml, .rc files matter
7. **Partial conversions** - Converting only some occurrences creates inconsistencies

**Verification Checklist**:
- [ ] All directories renamed (bitcoin→digibyte, btc→dgb)
- [ ] All files renamed (bitcoin→digibyte, btc→dgb)
- [ ] Binary names converted (bitcoind→digibyted, etc.)
- [ ] Library names converted (libbitcoin→libdigibyte)
- [ ] Header guards converted (BITCOIN_→DIGIBYTE_)
- [ ] Currency codes converted (BTC→DGB)
- [ ] Configuration files converted (bitcoin.conf→digibyte.conf)
- [ ] Build system fully updated
- [ ] Less than 100 remaining Bitcoin references (excluding copyright)

## Phase 2: Merge Strategy

### 2.1 Prepare DigiByte Repository

```bash
cd /path/to/digibyte
git checkout develop
git pull origin develop
git checkout -b feature/bitcoin-v26.2-merge

# Add Bitcoin remote
git remote add bitcoin-v26-2-converted /path/to/bitcoin-v26.2-for-digibyte
git fetch bitcoin-v26-2-converted
```

### 2.2 Execute Strategic Merge

```bash
# Start merge with manual conflict resolution
git merge bitcoin-v26-2-converted/digibyte-v26.2-naming-conversion \
    --no-commit \
    --no-ff \
    -X patience

# This will create conflicts - this is expected and desired
# We want to manually review each conflict
```

## Phase 3: Conflict Resolution by Component

### 3.1 Mining System (CRITICAL - Preserve DigiByte)

**Files requiring special attention:**
```yaml
mining_system:
  - src/miner.cpp           # Multi-algo block creation
  - src/miner.h            
  - src/rpc/mining.cpp      # getmininginfo with per-algo stats
  - src/pow.cpp             # DigiShield difficulty algorithms
  - src/pow.h
  - src/validation.cpp      # Multi-algo validation rules
```

**Resolution approach:**
```cpp
// In src/miner.cpp - PRESERVE DigiByte's CreateNewBlock with algo parameter
std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, int algo)
{
    // Take Bitcoin's improvements to block assembly
    // BUT KEEP: Algorithm-specific block creation
    // KEEP: DigiByte's 15-second timing logic
}

// In src/pow.cpp - PRESERVE all DigiByte difficulty functions
unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, int algo, const Consensus::Params& params)
{
    // KEEP: All V1-V4 difficulty algorithms
    // KEEP: Multi-algo difficulty adjustment
    // ADD: Any new Bitcoin PoW validation improvements
}

// In src/rpc/blockchain.cpp - PRESERVE getblockreward RPC command
UniValue getblockreward(const JSONRPCRequest& request)
{
    // KEEP: This DigiByte-specific RPC command
    // Returns current block reward based on 6-period schedule
}

// In src/rpc/mining.cpp - PRESERVE enhanced RPC commands
// getmininginfo: Include per-algorithm statistics
// getdifficulty: Return object with all 5 algorithm difficulties
// getnetworkhashps: Accept algo parameter
// generatetoaddress: Accept algo parameter
```

### 3.2 Network Processing (MERGE CAREFULLY - Contains Dandelion)

**Files:**
```yaml
network_processing:
  - src/net_processing.cpp   # Contains Dandelion++ hooks - MERGE CAREFULLY
  - src/net.cpp             # May have Dandelion connections
  - src/net.h
```

**Resolution approach:**
```cpp
// In net_processing.cpp
// PRESERVE all Dandelion++ transaction routing logic
// MERGE Bitcoin's networking improvements around it
void PeerManagerImpl::ProcessMessage(...)
{
    // Bitcoin improvements to message processing
    // BUT KEEP all Dandelion++ specific routing:
    if (strCommand == NetMsgType::DANDELIONTX) {
        // PRESERVE this entire block
    }
    
    // For TX processing, preserve Dandelion logic:
    if (msg_type == NetMsgType::TX) {
        // MERGE Bitcoin improvements
        // BUT KEEP Dandelion stem/fluff decisions
    }
}

// Look for Dandelion-specific network messages:
// - DANDELIONTX
// - Stem pool management
// - Fluff timers
```

### 3.3 Consensus Rules (MERGE CAREFULLY)

**Files:**
```yaml
consensus:
  - src/consensus/params.h    # DigiByte parameters + Bitcoin additions
  - src/chainparams.cpp       # Network-specific values
  - src/validation.cpp        # Block validation with multi-algo
```

**Key preservations:**
```cpp
// In chainparams.cpp - PRESERVE all DigiByte values
consensus.nPowTargetSpacing = 60 / 4; // 15 seconds - DO NOT CHANGE
consensus.DifficultyAdjustmentInterval = 1; // Every block - DO NOT CHANGE
consensus.nMaxMoney = 21000000000 * COIN; // 21 billion - DO NOT CHANGE

// ADD Bitcoin v26.2 features:
consensus.SegwitHeight = 1; // Already active in DigiByte
// BUT ADAPT for DigiByte:
// - assumeutxo parameters need DigiByte-specific values
// - Any new deployment parameters
```

### 3.4 Dandelion++ Integration (PRESERVE with care)

**Files:**
```yaml
dandelion:
  - src/dandelion.cpp       # Core Dandelion logic - PRESERVE
  - src/dandelion.h         # Dandelion definitions - PRESERVE
  - src/stempool.h          # Stem transaction pool - PRESERVE
  - src/net_processing.cpp  # Has Dandelion hooks - MERGE CAREFULLY
```

**Resolution:**
```cpp
// PRESERVE all core Dandelion files completely
// In files that integrate with Dandelion, merge carefully:
// Keep all Dandelion-specific message types, routing logic, and stem/fluff decisions

// Key Dandelion integration points to preserve:
// - Transaction routing decisions (stem vs fluff)
// - Stem pool management
// - Dandelion timers
// - Privacy guarantees
```

### 3.5 Cryptographic Algorithms (PRESERVE + ADD)

**Files to FULLY PRESERVE:**
```yaml
crypto_preserve:
  - src/crypto/hashgroestl.h
  - src/crypto/hashqubit.h  
  - src/crypto/hashskein.h
  - src/crypto/hashodo.h
  - src/crypto/odocrypt.cpp
  - src/crypto/odocrypt.h
  - src/crypto/sph_*.h      # All sphlib headers
  - src/crypto/KeccakP-800-SnP.h
  - src/crypto/scrypt.h
  - src/crypto/scrypt.cpp
```

**Integration:**
```cpp
// In src/primitives/block.cpp
uint256 CBlockHeader::GetPoWAlgoHash(const Consensus::Params& params) const
{
    // PRESERVE all DigiByte algorithm implementations
    switch (GetAlgo()) {
        case ALGO_SHA256D: // Take Bitcoin's optimizations
        case ALGO_SCRYPT:  // Keep DigiByte's implementation
        case ALGO_GROESTL: // Keep DigiByte's implementation
        case ALGO_SKEIN:   // Keep DigiByte's implementation
        case ALGO_QUBIT:   // Keep DigiByte's implementation
        case ALGO_ODO:     // Keep DigiByte's Odocrypt
    }
}
```

### 3.6 New Bitcoin v26.2 Features to Integrate

**AssumeUTXO (ADAPT for DigiByte):**
```cpp
// In src/validation.cpp
// INTEGRATE assumeutxo but with DigiByte parameters
// Create DigiByte-specific snapshots at these heights:
// - Height 10,000,000 (historical)
// - Height 15,000,000 (historical)
// - Height 20,000,000 (recent)
// - Height 21,000,000 (very recent)

// DigiByte-specific assumeutxo considerations:
// - Much larger block count (21M vs Bitcoin's ~800K)
// - Different UTXO set characteristics
// - Multi-algo validation requirements
```

**V2 Transport (INTEGRATE with modifications):**
```cpp
// In src/net.cpp
// ENABLE v2 transport but ensure compatibility with DigiByte network
// Default to OFF initially for network stability
// Test thoroughly with DigiByte's network topology
```

**Bitcoin v26.2 Specific Features:**
- All wallet migration fixes from v26.1
- P2P stability improvements
- Build system fixes
- GUI improvements
- Additional bug fixes from v26.2

## Phase 4: Build System Updates

### 4.1 Handle Build Changes

Bitcoin v26.2 still uses Autotools:

```bash
# Update configure.ac for DigiByte
sed -i 's/AC_INIT.*$/AC_INIT([DigiByte Core], [8.26.0], [https://github.com/digibyte-core/digibyte/issues], [digibyte], [https://digibyte.org/])/g' configure.ac

# Ensure all DigiByte crypto libs are included
# In src/Makefile.am, ensure:
# - All sph_* files are in crypto_libdigibyte_crypto_base_a_SOURCES
# - odocrypt files are included
# - Multi-algo test files are included
# - Dandelion++ files are included
```

### 4.2 Dependency Management

```bash
# Check for any new dependencies in v26.2
# Update depends/ directory if needed
# Ensure GUIX build compatibility
# Test cross-compilation for all platforms
```

## Phase 5: Testing Framework

### 5.1 Functional Test Preservation

Create/update DigiByte-specific tests:

```python
# test/functional/digibyte_multialgo.py
#!/usr/bin/env python3
"""Test multi-algorithm mining functionality"""

def test_all_algorithms():
    """Test that all 5 algorithms produce valid blocks"""
    for algo in ['sha256d', 'scrypt', 'groestl', 'skein', 'qubit']:
        block = node.generatetoaddress(1, address, algo)
        assert_equal(len(block), 1)
        
def test_algo_cycling():
    """Test that algorithms cycle properly"""
    # Test that different algos get fair share
    # Test difficulty adjustment per algo

# test/functional/digibyte_dandelion.py
#!/usr/bin/env python3
"""Test Dandelion++ privacy features"""

def test_dandelion_routing():
    """Test Dandelion++ transaction routing"""
    # Test stem phase
    # Test fluff phase
    # Test privacy guarantees
    # Test stem timeout

# test/functional/digibyte_difficulty.py
#!/usr/bin/env python3
"""Test DigiShield difficulty adjustment"""

def test_difficulty_adjustment():
    """Test DigiShield V3/V4 difficulty adjustment"""
    # Test per-algorithm difficulty
    # Test rapid adjustment (every block)
    # Test attack resistance
    # Test 15-second block timing

# test/functional/digibyte_assumeutxo.py
#!/usr/bin/env python3
"""Test AssumeUTXO with DigiByte parameters"""

def test_assumeutxo_load():
    """Test loading DigiByte UTXO snapshots"""
    # Test loading snapshot at height 20,000,000
    # Verify multi-algo blocks validate correctly
    # Test background validation
```

### 5.2 Performance Benchmarks

```bash
# Create performance baseline before merge
./src/bench/bench_digibyte > baseline_v8.22.txt

# After merge, compare
./src/bench/bench_digibyte > merged_v26.2.txt
diff baseline_v8.22.txt merged_v26.2.txt

# Specific DigiByte benchmarks to run:
# - Multi-algo block validation
# - Dandelion++ routing overhead
# - 15-second block propagation
# - UTXO set operations with 21M blocks
```

## Phase 6: Validation Checklist

### Critical Functionality Tests

- [ ] **Multi-Algorithm Mining**
  - [ ] SHA256D produces valid blocks
  - [ ] Scrypt produces valid blocks  
  - [ ] Groestl produces valid blocks
  - [ ] Skein produces valid blocks
  - [ ] Qubit produces valid blocks
  - [ ] Odocrypt activates at height 9,112,320
  - [ ] Odocrypt shape-change interval (10 days) functioning
  - [ ] Algorithm cycling works correctly
  - [ ] Per-algo difficulty adjustment works

- [ ] **Timing and Consensus**
  - [ ] 15-second average block time maintained
  - [ ] DigiShield difficulty adjustment functioning
  - [ ] No consensus rule violations
  - [ ] All checkpoints validate
  - [ ] Block rewards follow 6-period schedule

- [ ] **Dandelion++**
  - [ ] Stem pool functioning
  - [ ] Transaction routing working
  - [ ] Privacy features intact
  - [ ] Stem/fluff decision making correct
  - [ ] No transaction leaks

- [ ] **Network**
  - [ ] Connects on port 12024 (mainnet)
  - [ ] Connects on port 12025 (testnet)
  - [ ] Magic bytes correct (0xfa,0xc3,0xb6,0xda)
  - [ ] Peer discovery working
  - [ ] V2 transport optional (not default)
  - [ ] Network messages compatible

- [ ] **RPC Compatibility**
  - [ ] getmininginfo shows per-algo stats
  - [ ] getdifficulty returns object with all 5 algo difficulties
  - [ ] getblocktemplate works with multi-algo
  - [ ] getblockreward returns current DGB block reward
  - [ ] getnetworkhashps accepts algo parameter
  - [ ] generatetoaddress accepts algo parameter
  - [ ] All DigiByte-specific RPCs functioning

- [ ] **Bitcoin v26.2 Features**
  - [ ] AssumeUTXO works with DigiByte snapshots
  - [ ] Wallet migration fixes applied
  - [ ] P2P improvements integrated
  - [ ] GUI enhancements working
  - [ ] All v26.2 bug fixes applied

## Phase 7: Progressive Rollout

### 7.1 Testing Stages

1. **Local Testing** (1 week)
   ```bash
   # Single node tests
   ./test/functional/test_runner.py
   ./test/functional/digibyte_*.py
   
   # Unit tests
   ./src/test/test_digibyte
   ```

2. **Testnet Release** (2 weeks)
   ```bash
   # Deploy to DigiByte testnet
   ./src/digibyted -testnet
   
   # Monitor:
   # - Block production (all algos)
   # - Network stability
   # - Dandelion++ functionality
   # - AssumeUTXO performance
   ```

3. **Beta Release** (2 weeks)
   - Limited mainnet nodes (10-20)
   - Monitor for issues
   - Gather performance metrics
   - Community feedback

4. **Full Release**
   - Gradual rollout (25% -> 50% -> 100%)
   - Monitor network health
   - Be ready for quick fixes

### 7.2 Rollback Plan

```bash
# Tag before release
git tag v8.22.2-pre-v26.2-merge

# Create release branch (following GitFlow)
git checkout -b release/8.26.0

# If critical issues arise:
git revert <merge-commit>
# OR
git checkout v8.22.2-pre-v26.2-merge
```

## Phase 8: AssumeUTXO Implementation for DigiByte

### 8.1 Snapshot Creation

```bash
# Create snapshots at strategic heights
./src/digibyte-cli dumptxoutset snapshot_10M.dat 10000000
./src/digibyte-cli dumptxoutset snapshot_15M.dat 15000000
./src/digibyte-cli dumptxoutset snapshot_20M.dat 20000000
./src/digibyte-cli dumptxoutset snapshot_21M.dat 21000000

# Generate metadata
./contrib/devtools/utxo_snapshot.sh snapshot_20M.dat
```

### 8.2 Distribution Strategy

1. **Official Snapshots**
   - Host on digibyte.org
   - Provide checksums
   - Sign with DigiByte Core developer keys

2. **Torrent Distribution**
   - Create torrents for large snapshots
   - Community seeding

3. **Integration**
   ```cpp
   // In chainparams.cpp
   // Add DigiByte snapshot metadata
   m_assumeutxo_data = MapAssumeutxo{
       {10000000, {/* snapshot hash */, /* size */}},
       {15000000, {/* snapshot hash */, /* size */}},
       {20000000, {/* snapshot hash */, /* size */}},
       {21000000, {/* snapshot hash */, /* size */}},
   };
   ```

## Work Progress Tracking Template

```markdown
## DigiByte v26.2 Merge Progress - [DATE]

### Completed Today:
- [ ] Files merged: 
- [ ] Conflicts resolved:
- [ ] Tests passing:
- [ ] Issues found:

### Current Focus:
- Working on: [component/file]
- Conflict type: [naming/logic/structure]
- Resolution approach: [preserve/merge/adapt]

### Blockers:
- 

### Tomorrow's Plan:
- 

### Overall Progress: __% complete
- Pre-conversion: ✓
- Initial merge: ✓
- Consensus code: __%
- Mining system: __%
- Network layer: __%
- Dandelion++: __%
- Wallet: __%
- GUI: __%
- Tests: __%
- AssumeUTXO: __%

### Key Decisions Made:
- 

### Questions for Team:
- 

### v26.2 Specific Items:
- [ ] All v26.0 features integrated
- [ ] All v26.1 fixes applied
- [ ] All v26.2 fixes applied
- [ ] AssumeUTXO adapted for DigiByte
- [ ] V2 transport tested
```

## Success Metrics

### 1. Functional Success
- All DigiByte features working correctly
- All Bitcoin v26.2 improvements integrated
- No consensus changes
- No network disruption
- All bug fixes from v26.0, v26.1, and v26.2 applied

### 2. Performance Success
- Sync time improved >90% with assumeutxo
- Memory usage reasonable (<8GB for full node)
- CPU usage not increased >10%
- Network bandwidth efficient
- 15-second blocks maintained

### 3. Code Quality
- Clean compilation (no warnings)
- All tests passing
- No memory leaks (valgrind clean)
- Code review approved
- Documentation updated

### 4. Network Health
- No chain splits
- All algorithms producing blocks
- Dandelion++ privacy maintained
- Peer count stable
- No increased orphan rate

## Critical Reminders

### 1. NEVER CHANGE:
- Block time (15 seconds)
- Difficulty adjustment interval (every block)
- Max supply (21 billion)
- Algorithm count (5 + Odocrypt)
- Network ports (12024/12025)
- Magic bytes (0xfa,0xc3,0xb6,0xda)
- Address prefixes (D=30, S=63)
- No RBF policy

### 2. ALWAYS PRESERVE:
- All multi-algo mining code
- All Dandelion++ code
- All DigiByte-specific RPCs (including getblockreward)
- Custom difficulty algorithms (V1-V4)
- 6-period reward schedule
- Both Bitcoin AND DigiByte copyrights
- Genesis block ("USA Today: 10/Jan/2014")
- Taproot deployment dates (Jan 10, 2025 to Jan 10, 2027)

### 3. CAREFULLY MERGE:
- Validation logic (has multi-algo)
- Network processing (has Dandelion hooks)
- Mining code (completely different)
- Consensus parameters
- RPC commands (many custom)

### 4. TEST THOROUGHLY:
- AssumeUTXO with 21M blocks
- Multi-algo mining fairness
- Dandelion++ privacy
- Network compatibility
- Upgrade path from v8.22
- Cross-platform builds

## Important Notes

1. **Copyright Headers**: Every file should contain BOTH copyrights:
   ```cpp
   // Copyright (c) 2009-2024 The Bitcoin Core developers
   // Copyright (c) 2014-2025 The DigiByte Core developers
   ```

2. **net_processing.cpp**: This file contains Dandelion++ integration points and should be merged very carefully. Look for:
   - `NetMsgType::DANDELIONTX` handling
   - Stem pool management code
   - Transaction routing decisions
   - Dandelion-specific timers and flags

3. **Version Numbers**: After merge, update to v8.26.0 to indicate alignment with Bitcoin Core v26.2

4. **Protocol Version**: Maintain DigiByte's protocol version (70018) and update only when necessary for new network features

5. **Release Notes**: Document all changes, emphasizing:
   - AssumeUTXO fast sync capability
   - Bitcoin Core v26.2 improvements
   - Maintained DigiByte features
   - Performance improvements

6. **GUIX Builds**: Ensure GUIX reproducible builds work with all DigiByte modifications

## Estimated Timeline

- **Phase 1** (Pre-conversion): 1-2 days
- **Phase 2** (Initial merge): 1 day
- **Phase 3** (Conflict resolution): 2-3 weeks
- **Phase 4** (Build system): 2-3 days
- **Phase 5** (Testing): 1-2 weeks
- **Phase 6** (Validation): 1 week
- **Phase 7** (Rollout): 4-6 weeks
- **Phase 8** (AssumeUTXO): 1 week

**Total: 10-14 weeks** for production-ready release

## Appendix A: DigiByte-Specific Constants

```cpp
// Multi-Algorithm Mining
const int NUM_ALGOS = 5;
enum {
    ALGO_SHA256D = 0,
    ALGO_SCRYPT = 1,
    ALGO_GROESTL = 2,
    ALGO_SKEIN = 3,
    ALGO_QUBIT = 4,
    ALGO_ODO = 7  // Odocrypt
};

// Network Parameters
static const int MAINNET_DEFAULT_PORT = 12024;
static const int TESTNET_DEFAULT_PORT = 12025;
static const unsigned char MAINNET_MESSAGE_START[4] = {0xfa, 0xc3, 0xb6, 0xda};

// Timing
static const int POW_TARGET_SPACING = 15; // 15 seconds
static const int MULTI_ALGO_TARGET_SPACING = 150; // 30 * 5

// Key Heights
static const int MULTI_ALGO_DIFF_CHANGE_TARGET = 145000;
static const int ALWAYS_UPDATE_DIFF_CHANGE_TARGET = 400000;
static const int WORK_COMPUTATION_CHANGE_TARGET = 1430000;
static const int ODO_HEIGHT = 9112320;

// Reward Schedule (6 periods)
// Period 1: Block 0-7,999 = 72,000 DGB
// Period 2: Block 8,000-1,051,199 = 2,498 DGB
// Period 3: Block 1,051,200-2,102,399 = 1,249 DGB
// Period 4: Block 2,102,400-3,153,599 = 625 DGB
// Period 5: Block 3,153,600-4,204,799 = 313 DGB
// Period 6: Block 4,204,800+ = rewards decrease monthly
```

## Appendix B: Key DigiByte Files

```yaml
digibyte_specific_files:
  crypto:
    - src/crypto/hashgroestl.h
    - src/crypto/hashqubit.h
    - src/crypto/hashskein.h
    - src/crypto/hashodo.h
    - src/crypto/odocrypt.cpp
    - src/crypto/odocrypt.h
    - src/crypto/sph_*.h
    - src/crypto/KeccakP-800-SnP.h
    - src/crypto/scrypt.h
    - src/crypto/scrypt.cpp
    
  dandelion:
    - src/dandelion.cpp
    - src/dandelion.h
    - src/stempool.h
    
  mining:
    - Multi-algo modifications in miner.*
    - DigiShield in pow.*
    - Custom difficulty in validation.cpp
    
  consensus:
    - Custom parameters in chainparams.cpp
    - Multi-algo support in primitives/block.*
    - Custom validation rules
```

## Appendix C: Common Merge Conflicts

### 1. Block Header
```cpp
// Bitcoin version
class CBlockHeader {
    // Standard fields only
};

// DigiByte version (PRESERVE)
class CBlockHeader {
    // Standard fields
    int GetAlgo() const;
    void SetAlgo(int algo);
    uint256 GetPoWAlgoHash(const Consensus::Params&) const;
};
```

### 2. Mining RPC
```cpp
// DigiByte additions to preserve:
UniValue getmininginfo(const JSONRPCRequest& request)
{
    // Include per-algorithm statistics
    // Show current algorithm
    // Display multi-algo difficulties
}
```

### 3. Network Messages
```cpp
// Additional DigiByte messages:
const char* DANDELIONTX = "dandeliontx";
// Preserve all Dandelion-specific handling
```

This specification ensures Bitcoin v26.2's improvements and all bug fixes are captured while maintaining DigiByte's unique identity and functionality. The use of v26.2 as the final v26 series release provides maximum stability and all accumulated fixes.
