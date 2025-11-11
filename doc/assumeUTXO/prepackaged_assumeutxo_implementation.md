# DigiByte v8.26 Prepackaged AssumeUTXO Implementation Plan

## Overview
This document outlines how to implement automatic loading of a prepackaged UTXO snapshot with DigiByte v8.26, allowing new users to get up and running quickly without manual snapshot loading.

## Goals
1. Package UTXO snapshot with wallet distribution
2. Automatically load snapshot on first run if no existing chainstate
3. Provide seamless user experience with no manual intervention
4. Maintain security by validating against hardcoded hashes

## Implementation Strategy

### 1. Snapshot Distribution Structure
```
digibyte-8.26/
├── bin/
│   ├── digibyted
│   ├── digibyte-cli
│   └── digibyte-qt
├── share/
│   └── utxo-snapshots/
│       └── mainnet-21500000.dat  # The prepackaged snapshot
└── README.md
```

### 2. Code Changes Required

#### A. Add Auto-Loading Logic in `src/node/chainstate.cpp`

After line 187 in `LoadChainstate()`, add automatic snapshot loading:

```cpp
// Load a chain created from a UTXO snapshot, if any exist.
bool has_snapshot = chainman.DetectSnapshotChainstate();

// NEW CODE: Auto-load prepackaged snapshot on first run ONLY
if (!has_snapshot && !options.reindex && !options.reindex_chainstate) {
    // Only attempt snapshot loading for completely fresh installations
    // Check multiple conditions to ensure this is truly a new wallet:
    // 1. No existing blocks directory
    // 2. No existing chainstate directory  
    // 3. No existing blocks in database
    
    const fs::path blocks_dir = GetDataDir() / "blocks";
    const fs::path chainstate_dir = GetDataDir() / "chainstate";
    
    bool is_fresh_install = !fs::exists(blocks_dir) || 
                           (fs::exists(blocks_dir) && fs::is_empty(blocks_dir));
    
    // Also check if chainstate exists
    if (is_fresh_install && !fs::exists(chainstate_dir)) {
        LogPrintf("[snapshot] Detected fresh installation (no blocks or chainstate), attempting to load prepackaged snapshot...\n");
        
        // Try to load prepackaged snapshot
        auto snapshot_loaded = LoadPrepackagedSnapshot(chainman, options);
        if (snapshot_loaded) {
            has_snapshot = true;
            LogPrintf("[snapshot] Successfully loaded prepackaged snapshot for new installation\n");
        }
    } else if (fs::exists(blocks_dir) || fs::exists(chainstate_dir)) {
        // Existing installation - check sync status
        CBlockIndex* tip = chainman.ActiveChain().Tip();
        if (tip) {
            LogPrintf("[snapshot] Existing installation detected at height %d, skipping snapshot load\n", tip->nHeight);
        }
    }
}
```

#### B. Implement `LoadPrepackagedSnapshot()` Function

Add to `src/node/chainstate.cpp`:

```cpp
#include <rpc/blockchain.h>  // For loadtxoutset logic

static bool LoadPrepackagedSnapshot(ChainstateManager& chainman, const ChainstateLoadOptions& options)
{
    // Define possible snapshot locations (in order of preference)
    std::vector<fs::path> snapshot_paths = {
        // 1. Check data directory first (user may have placed custom snapshot)
        GetDataDir() / "snapshots" / "mainnet-21500000.dat",
        
        // 2. Check installation directory (prepackaged location)
        GetExecutableDir() / ".." / "share" / "utxo-snapshots" / "mainnet-21500000.dat",
        
        // 3. Check common installation paths
        fs::path("/usr/share/digibyte/utxo-snapshots/mainnet-21500000.dat"),
        fs::path("/usr/local/share/digibyte/utxo-snapshots/mainnet-21500000.dat"),
        
        // 4. Windows: Check Program Files
        #ifdef WIN32
        fs::path(getenv("ProgramFiles")) / "DigiByte" / "utxo-snapshots" / "mainnet-21500000.dat",
        #endif
        
        // 5. macOS: Check Applications
        #ifdef __APPLE__
        fs::path("/Applications/DigiByte.app/Contents/Resources/utxo-snapshots/mainnet-21500000.dat"),
        #endif
    };
    
    fs::path snapshot_path;
    for (const auto& path : snapshot_paths) {
        if (fs::exists(path)) {
            snapshot_path = path;
            LogPrintf("[snapshot] Found snapshot at: %s\n", fs::PathToString(path));
            break;
        }
    }
    
    if (snapshot_path.empty()) {
        LogPrintf("[snapshot] No prepackaged snapshot found\n");
        return false;
    }
    
    // Verify this is mainnet
    if (Params().GetChainType() != ChainType::MAIN) {
        LogPrintf("[snapshot] Skipping snapshot loading for non-mainnet\n");
        return false;
    }
    
    // Load the snapshot using existing logic from loadtxoutset RPC
    try {
        AutoFile afile{fsbridge::fopen(snapshot_path, "rb")};
        if (afile.IsNull()) {
            LogPrintf("[snapshot] Failed to open snapshot file\n");
            return false;
        }
        
        // Read snapshot metadata
        SnapshotMetadata metadata;
        afile >> metadata;
        
        // Wait for headers chain to catch up to snapshot base
        const CBlockIndex* snapshot_start_block = nullptr;
        LogPrintf("[snapshot] Waiting for block headers up to %s\n", metadata.m_base_blockhash.ToString());
        
        while (true) {
            snapshot_start_block = WITH_LOCK(::cs_main, 
                return chainman.m_blockman.LookupBlockIndex(metadata.m_base_blockhash));
            
            if (snapshot_start_block) break;
            
            // In auto-load scenario, we might not have headers yet
            // Return false to continue normal sync
            static int wait_count = 0;
            if (++wait_count > 10) {
                LogPrintf("[snapshot] Headers not available for snapshot base, continuing normal sync\n");
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Activate the snapshot
        if (!chainman.ActivateSnapshot(afile, metadata, false)) {
            LogPrintf("[snapshot] Failed to activate snapshot\n");
            return false;
        }
        
        LogPrintf("[snapshot] Successfully loaded snapshot at height %d\n", 
                  snapshot_start_block->nHeight);
        return true;
        
    } catch (const std::exception& e) {
        LogPrintf("[snapshot] Error loading snapshot: %s\n", e.what());
        return false;
    }
}
```

#### C. Add User Notification in `src/qt/bitcoingui.cpp`

For GUI users, show progress when loading snapshot:

```cpp
// In BitcoinGUI::setClientModel()
if (clientModel->isLoadingSnapshot()) {
    progressBar->setFormat(tr("Loading UTXO snapshot... %p%"));
    showProgress(tr("Loading UTXO snapshot..."), 0);
}
```

#### D. Add Configuration Option

Add to `src/init.cpp` in `SetupServerArgs()`:

```cpp
argsman.AddArg("-loadsnapshot", 
    strprintf("Automatically load prepackaged UTXO snapshot on first run (default: %u)", 
    DEFAULT_LOAD_SNAPSHOT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
```

And define in `src/node/chainstate.h`:

```cpp
static constexpr bool DEFAULT_LOAD_SNAPSHOT{true};
```

#### E. Update Build System

Modify `Makefile.am` to include snapshot in distribution:

```makefile
# Add to dist_noinst_DATA or similar section
utxosnapshotdir = $(datadir)/utxo-snapshots
dist_utxosnapshot_DATA = share/utxo-snapshots/mainnet-21500000.dat
```

### 3. Security Considerations

1. **Snapshot Validation**: The snapshot is always validated against hardcoded hashes in chainparams.cpp
2. **Optional Loading**: Users can disable with `-loadsnapshot=0`
3. **Checksum Verification**: Add SHA256 checksum verification before loading:

```cpp
// In LoadPrepackagedSnapshot(), before loading:
const std::string expected_hash = "HASH_OF_SNAPSHOT_FILE";  // Hardcode this
if (GetFileHash(snapshot_path) != expected_hash) {
    LogPrintf("[snapshot] Snapshot file hash mismatch, skipping load\n");
    return false;
}
```

### 4. User Experience Enhancements

#### A. Progress Indication
- Show loading progress in GUI splash screen
- Log progress messages for CLI users

#### B. First-Run Detection
- Create marker file after successful snapshot load
- Skip snapshot loading if marker exists

#### C. Error Handling
- Gracefully fall back to normal sync if snapshot load fails
- Don't prevent node startup on snapshot errors

### 5. Behavior for Different Installation Scenarios

#### A. Brand New Installation (Fresh Data Directory)
- **Condition**: No `blocks/` or `chainstate/` directories exist
- **Behavior**: Automatically loads prepackaged snapshot
- **Result**: User syncs to block 21,500,000 instantly, then continues with normal sync
- **User Experience**: Wallet ready in minutes instead of hours/days

#### B. Existing Full Node (Fully Synced)
- **Condition**: Has `blocks/` and `chainstate/` directories with recent data
- **Behavior**: Skips snapshot loading completely
- **Result**: Continues normal operation with existing blockchain data
- **User Experience**: No change, uses existing blockchain

#### C. Partially Synced Node (Incomplete Sync)
- **Condition**: Has `blocks/` directory but sync incomplete (e.g., at block 10,000,000)
- **Behavior**: Skips snapshot loading, continues from current position
- **Result**: Resumes sync from where it left off
- **User Experience**: No disruption to ongoing sync
- **Note**: User could manually delete data and restart to use snapshot if desired

#### D. Corrupted/Problem Installation
- **Condition**: Has data directories but corrupted/invalid state
- **Behavior**: User should use `-reindex` which will skip snapshot
- **Alternative**: User can delete data directory to trigger fresh install with snapshot

#### E. Special Considerations for Existing Users

For existing users who want to benefit from the snapshot:
1. They would need to manually move/delete their existing data directory
2. Or we could add a special flag like `-forceloadSnapshot` that loads snapshot even with existing data
3. This would be an advanced option with appropriate warnings

```cpp
// Optional enhancement for existing users
if (gArgs.GetBoolArg("-forceloadsnapshot", false)) {
    if (fs::exists(blocks_dir) || fs::exists(chainstate_dir)) {
        return InitError(_("Cannot force load snapshot with existing blockchain data. "
                          "Please backup and remove the data directory first."));
    }
}
```

### 6. Distribution Package Creation

#### Build Script Addition (`contrib/build-snapshot-release.sh`):
```bash
#!/bin/bash
# Build script for creating release with snapshot

# 1. Build DigiByte normally
make -j$(nproc)

# 2. Generate snapshot (requires synced node)
./src/digibyte-cli dumptxoutset share/utxo-snapshots/mainnet-21500000.dat \
    00000000000000007cbe22612937832c2e6341ec867e881979e2246df44fa727

# 3. Create distribution package
make dist

# 4. Add snapshot to package
tar -rf digibyte-8.26.tar.gz share/utxo-snapshots/mainnet-21500000.dat
```

### 6. Testing Plan

1. **Fresh Install Test**:
   - Install DigiByte v8.26 with snapshot
   - Verify automatic loading on first run
   - Confirm quick sync to current tip

2. **Existing Node Test**:
   - Upgrade existing node
   - Verify snapshot is NOT loaded (has existing chainstate)

3. **Disable Flag Test**:
   - Run with `-loadsnapshot=0`
   - Verify normal sync behavior

4. **Corrupted Snapshot Test**:
   - Modify snapshot file
   - Verify graceful fallback to normal sync

### 7. Documentation Updates

#### Update `doc/assumeutxo.md`:
```markdown
## Automatic Snapshot Loading

DigiByte v8.26 includes a prepackaged UTXO snapshot at block 21,500,000. 
On first run, this snapshot will be automatically loaded to enable rapid sync.

To disable automatic loading:
- CLI: `digibyted -loadsnapshot=0`
- Config: Add `loadsnapshot=0` to digibyte.conf

The snapshot is validated against hardcoded hashes for security.
```

## Implementation Timeline

1. **Phase 1**: Implement core auto-loading logic (2-3 days)
2. **Phase 2**: Add GUI integration and progress indication (1-2 days)
3. **Phase 3**: Testing and validation (3-4 days)
4. **Phase 4**: Package creation and distribution prep (1-2 days)

## Important Technical Notes

### AssumeUTXO Benefits
1. **Instant Usability**: New users can send/receive transactions immediately
2. **Reduced Sync Time**: Skip ~21.5 million blocks of initial validation
3. **Lower Bandwidth**: Download ~3GB snapshot vs ~50GB+ full blockchain
4. **Background Validation**: Full validation continues in background

### AssumeUTXO Limitations  
1. **Trust Model**: Users must trust the snapshot until background validation completes
2. **No Historical Data**: Cannot query blocks before snapshot height without full sync
3. **Cannot Serve Old Blocks**: Node cannot help other nodes sync blocks before snapshot

### How It Works With Existing Installations
- **Existing nodes ARE NOT affected** - they continue using their full blockchain
- **Only new installations** get the snapshot automatically
- **Snapshot is ONLY loaded if**:
  - No `blocks/` directory exists, OR
  - `blocks/` directory is empty, AND
  - No `chainstate/` directory exists
- **This ensures**:
  - Upgrading users keep their existing blockchain
  - Only truly fresh installs use the snapshot
  - No risk of data loss or corruption for existing users

## File Size Considerations

- Estimated snapshot size: 2-3 GB
- Consider offering two downloads:
  - **Full package with snapshot** (~3.5 GB total)
    - Includes binaries + snapshot
    - Best for new users
  - **Light package without snapshot** (~50 MB)
    - Binaries only
    - Best for existing users upgrading

## Migration Path for Existing Users

If existing users want to use the snapshot to save disk space:
1. **Backup existing wallet.dat** and any important data
2. **Stop DigiByte node**
3. **Delete data directory** (except wallet.dat)
4. **Start fresh** with snapshot-enabled version
5. **Restore wallet.dat** after snapshot loads

## Conclusion

This implementation provides a seamless first-run experience for new users while maintaining security through hash validation and preserving existing users' blockchain data. New users get a fully synchronized node in minutes instead of days, significantly improving DigiByte adoption without affecting existing installations.