# DigiByte v8.26 Bitcoin Core v26.2 Merge Prompt

## ⚠️ CRITICAL WARNING ⚠️
**The pre-conversion step is MANDATORY**. Skipping or incompletely executing the Bitcoin→DigiByte naming conversion will result in ~30,000 merge conflicts, making the merge practically impossible. The comprehensive conversion script in the specification MUST be run on Bitcoin v26.2 BEFORE any merge attempt.

## Overview
You are tasked with merging Bitcoin Core v26.2 into DigiByte v8.22.2 to create DigiByte v8.26. This merge must preserve all DigiByte-specific features while incorporating Bitcoin's improvements and bug fixes.

## Key Requirements

### Version Target
- **Current**: DigiByte v8.22.2
- **Target**: DigiByte v8.26 (aligned with Bitcoin Core v26.2)
- **Protocol Version**: Maintain 70018 unless required for new features

### Critical Features to Preserve

1. **Multi-Algorithm Mining**
   - 5 algorithms: SHA256D, Scrypt, Groestl, Skein, Qubit
   - Odocrypt (activates at height 9,112,320 with 10-day shape-change)
   - Algorithm-specific difficulty adjustment
   - 15-second block time (75 seconds per algorithm)

2. **DigiByte-Specific RPC Commands**
   - `getblockreward` - Returns current DGB block reward
   - Enhanced `getmininginfo` with per-algorithm stats
   - `getdifficulty` returns object with all 5 algorithm difficulties
   - Mining commands accept `algo` parameter

3. **Unique Consensus Rules**
   - 21 billion max supply
   - 6-period reward schedule
   - DigiShield difficulty algorithms (V1-V4)
   - No Replace-by-Fee (RBF)
   - Taproot activation: Jan 10, 2025 to Jan 10, 2027

4. **Privacy Features**
   - Dandelion++ implementation (complete preservation)
   - Special message type: `DANDELIONTX`
   - Stem pool and routing logic

5. **Network Constants**
   - Mainnet port: 12024
   - Testnet port: 12025
   - Magic bytes: 0xfa,0xc3,0xb6,0xda
   - Address prefixes: D=30, S=63
   - Bech32 HRP: "dgb"

## Merge Process

### Phase 1: Pre-conversion (ABSOLUTELY CRITICAL)
1. Clone Bitcoin v26.2 into `bitcoin-v26.2-for-digibyte/`
2. Run the COMPREHENSIVE `convert-bitcoin-to-digibyte.sh` script that:
   - Renames ALL directories (bitcoin→digibyte, btc→dgb)
   - Renames ALL files with bitcoin/btc in the name
   - Updates ALL file contents including:
     * Binary names (bitcoind→digibyted)
     * Library names (libbitcoin→libdigibyte)
     * Header guards (BITCOIN_→DIGIBYTE_)
     * Currency codes (BTC→DGB)
     * Configuration files (bitcoin.conf→digibyte.conf)
     * Build system files (configure.ac, Makefile.am, etc.)
     * All code references throughout the entire codebase
3. Verify conversion: Less than 100 Bitcoin references should remain (excluding copyright)
4. Commit pre-converted code

### Phase 2: Strategic Merge
1. Create feature branch following GitFlow: `feature/bitcoin-v26.2-merge`
2. Add Bitcoin remote and fetch
3. Execute merge with `--no-commit --no-ff -X patience`

### Phase 3: Conflict Resolution Priority
1. **Mining System** - FULLY PRESERVE DigiByte implementation
2. **RPC Commands** - PRESERVE all DigiByte-specific commands
3. **Network Processing** - CAREFUL merge (contains Dandelion++)
4. **Consensus Rules** - PRESERVE DigiByte parameters
5. **Crypto Algorithms** - PRESERVE all custom hash functions

### Phase 4: Integration
- Adapt AssumeUTXO for DigiByte's 21M+ blocks
- Enable V2 transport as optional (not default)
- Apply all Bitcoin v26.0, v26.1, and v26.2 fixes

## Testing Requirements
- All 5 algorithms produce valid blocks
- 15-second block time maintained
- Dandelion++ privacy functioning
- Custom RPC commands working
- Network compatibility verified
- AssumeUTXO with DigiByte snapshots

## Success Criteria
- Zero consensus changes
- All DigiByte features preserved
- All Bitcoin v26.2 improvements integrated
- Clean compilation
- All tests passing
- Network stability maintained

## Reference Documents
- Full specification: `digibyte-btc-v26-2-merge-spec.md`
- Pre-conversion script included in spec
- CLAUDE.md for AI-assisted merge guidance

## Important Notes
- **PRE-CONVERSION IS CRITICAL**: Without proper Bitcoin→DigiByte renaming, expect 30,000+ conflicts
- NEVER change core DigiByte parameters (block time, supply, etc.)
- ALWAYS preserve both Bitcoin and DigiByte copyrights (only Bitcoin copyright in headers should remain)
- Test thoroughly on testnet before mainnet deployment
- Document all decisions and changes made during merge
- Use the comprehensive conversion script from the specification - partial conversions will fail