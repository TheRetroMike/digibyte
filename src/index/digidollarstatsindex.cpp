// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/digidollarstatsindex.h>

#include <chainparams.h>
#include <coins.h>
#include <common/args.h>
#include <consensus/digidollar.h>
#include <digidollar/digidollar.h>
#include <digidollar/validation.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <serialize.h>
#include <undo.h>
#include <validation.h>

#include <limits>

static constexpr uint8_t DB_BLOCK_HASH{'D'};
static constexpr uint8_t DB_BLOCK_HEIGHT{'H'};
static constexpr uint8_t DB_VAULT_OUTPOINT{'V'};

namespace {

/**
 * Database value structure for DigiDollar statistics.
 * Stores the complete state at a specific block height.
 */
struct DigiDollarDBVal {
    CAmount total_dd_supply;
    CAmount total_collateral;
    uint64_t vault_count;

    SERIALIZE_METHODS(DigiDollarDBVal, obj)
    {
        READWRITE(obj.total_dd_supply);
        READWRITE(obj.total_collateral);
        READWRITE(obj.vault_count);
    }
};

/**
 * Database key for height-based lookups.
 */
struct DBHeightKey {
    int height;

    explicit DBHeightKey(int height_in) : height(height_in) {}

    template <typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata8(s, DB_BLOCK_HEIGHT);
        ser_writedata32be(s, height);
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        const uint8_t prefix{ser_readdata8(s)};
        if (prefix != DB_BLOCK_HEIGHT) {
            throw std::ios_base::failure("Invalid format for digidollarstatsindex DB height key");
        }
        height = ser_readdata32be(s);
    }
};

/**
 * Database key for hash-based lookups.
 */
struct DBHashKey {
    uint256 block_hash;

    explicit DBHashKey(const uint256& hash_in) : block_hash(hash_in) {}

    SERIALIZE_METHODS(DBHashKey, obj)
    {
        uint8_t prefix{DB_BLOCK_HASH};
        READWRITE(prefix);
        if (prefix != DB_BLOCK_HASH) {
            throw std::ios_base::failure("Invalid format for digidollarstatsindex DB hash key");
        }

        READWRITE(obj.block_hash);
    }
};

/**
 * Vault information stored in database.
 * Maps minted vault outpoints to DD amount and collateral. Entries are
 * immutable mint metadata, not an active-vault set, so alternate-branch
 * redemptions after a reorg can still recover the original mint amount.
 */
struct VaultInfo {
    CAmount dd_amount;
    CAmount collateral;

    SERIALIZE_METHODS(VaultInfo, obj)
    {
        READWRITE(obj.dd_amount);
        READWRITE(obj.collateral);
    }
};

/**
 * Database key for vault outpoint lookups.
 */
struct DBVaultKey {
    COutPoint outpoint;

    explicit DBVaultKey(const COutPoint& op) : outpoint(op) {}

    SERIALIZE_METHODS(DBVaultKey, obj)
    {
        uint8_t prefix{DB_VAULT_OUTPOINT};
        READWRITE(prefix);
        if (prefix != DB_VAULT_OUTPOINT) {
            throw std::ios_base::failure("Invalid format for digidollarstatsindex DB vault key");
        }

        READWRITE(obj.outpoint);
    }
};

} // namespace

std::unique_ptr<DigiDollarStatsIndex> g_digidollar_stats_index;

DigiDollarStatsIndex::DigiDollarStatsIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex(std::move(chain), "digidollarstatsindex")
{
    fs::path path{gArgs.GetDataDirNet() / "indexes" / "digidollarstats"};
    fs::create_directories(path);

    m_db = std::make_unique<DigiDollarStatsIndex::DB>(path / "db", n_cache_size, f_memory, f_wipe);
}

bool DigiDollarStatsIndex::CustomInit(const std::optional<interfaces::BlockKey>& block)
{
    if (block) {
        // Load existing state from database
        std::pair<uint256, DigiDollarDBVal> read_out;
        if (!m_db->Read(DBHeightKey(block->height), read_out)) {
            return error("%s: Cannot read current %s state; index may be corrupted",
                         __func__, GetName());
        }

        // Verify block hash matches
        if (read_out.first != block->hash) {
            LogPrintf("WARNING: %s height index has unexpected block %s; expected %s\n",
                      GetName(), read_out.first.ToString(), block->hash.ToString());

            // Try hash-based lookup
            if (!m_db->Read(DBHashKey(block->hash), read_out.second)) {
                return error("%s: Cannot read current %s state; index may be corrupted",
                             __func__, GetName());
            }
        }

        // Restore state from database
        m_total_dd_supply = read_out.second.total_dd_supply;
        m_total_collateral = read_out.second.total_collateral;
        m_vault_count = read_out.second.vault_count;

        LogPrint(BCLog::DIGIDOLLAR, "DigiDollarStatsIndex: Initialized from height %d - DD Supply: %d, Collateral: %d, Vaults: %d\n",
                 block->height, m_total_dd_supply, m_total_collateral, m_vault_count);
    } else {
        // Starting from genesis - initialize to zero
        m_total_dd_supply = 0;
        m_total_collateral = 0;
        m_vault_count = 0;

        LogPrint(BCLog::DIGIDOLLAR, "DigiDollarStatsIndex: Initialized from genesis\n");
    }

    return true;
}

bool DigiDollarStatsIndex::CustomAppend(const interfaces::BlockInfo& block)
{
    assert(block.data);
    const CBlock& cblock = *block.data;

    // Ignore genesis block
    if (block.height == 0) {
        // Store genesis state (all zeros)
        std::pair<uint256, DigiDollarDBVal> value;
        value.first = block.hash;
        value.second.total_dd_supply = 0;
        value.second.total_collateral = 0;
        value.second.vault_count = 0;
        return m_db->Write(DBHeightKey(block.height), value);
    }

    // Get block undo data to track spent outputs
    CBlockUndo block_undo;
    const CBlockIndex* pindex = WITH_LOCK(cs_main, return m_chainstate->m_blockman.LookupBlockIndex(block.hash));
    if (!m_chainstate->m_blockman.UndoReadFromDisk(block_undo, *pindex)) {
        return error("%s: Failed to read undo data for block %s", __func__, block.hash.ToString());
    }

    DigiDollar::TxLookupFn tx_lookup = [this, pindex](const uint256& txid, uint32_t coin_height, CTransactionRef& tx_out) -> bool {
        const CBlockIndex* block_index = pindex->GetAncestor(coin_height);
        if (!block_index) return false;
        CBlock source_block;
        if (!m_chainstate->m_blockman.ReadBlockFromDisk(source_block, *block_index)) return false;
        for (const CTransactionRef& candidate_tx : source_block.vtx) {
            if (candidate_tx->GetHash() == txid) {
                tx_out = candidate_tx;
                return true;
            }
        }
        return false;
    };

    // Process each transaction in the block
    for (size_t tx_idx = 0; tx_idx < cblock.vtx.size(); ++tx_idx) {
        const CTransactionRef& tx = cblock.vtx[tx_idx];

        // Skip coinbase transactions
        if (tx->IsCoinBase()) {
            continue;
        }

        // Check transaction type
        DigiDollar::DigiDollarTxType txType = DigiDollar::GetDigiDollarTxType(*tx);

        // Process DD MINT transactions
        if (txType == DigiDollar::DD_TX_MINT) {
            CAmount ddAmount = 0;
            CAmount collateralAmount = 0;
            if (!DigiDollar::ExtractMintAccountingAmounts(*tx, ddAmount, collateralAmount)) {
                LogPrint(BCLog::DIGIDOLLAR, "DigiDollarStatsIndex: Failed to extract accounting amounts from DD_TX_MINT txid=%s\n",
                         tx->GetHash().ToString());
                continue;
            }

            uint32_t vault_index = std::numeric_limits<uint32_t>::max();
            for (uint32_t i = 0; i < tx->vout.size(); ++i) {
                const CTxOut& output = tx->vout[i];
                if (output.nValue == collateralAmount &&
                    output.scriptPubKey.size() == 34 &&
                    output.scriptPubKey[0] == OP_1) {
                    vault_index = i;
                    break;
                }
            }
            if (vault_index == std::numeric_limits<uint32_t>::max()) {
                LogPrint(BCLog::DIGIDOLLAR, "DigiDollarStatsIndex: Failed to locate vault output in DD_TX_MINT txid=%s\n",
                         tx->GetHash().ToString());
                continue;
            }

            // Update running totals
            m_total_dd_supply += ddAmount;
            m_total_collateral += collateralAmount;
            m_vault_count++;

            // Store vault info for later redemption tracking
            COutPoint vault_outpoint(tx->GetHash(), vault_index);
            VaultInfo vault_info;
            vault_info.dd_amount = ddAmount;
            vault_info.collateral = collateralAmount;
            if (!m_db->Write(DBVaultKey(vault_outpoint), vault_info)) {
                return error("%s: Failed to write vault info for %s:%u", __func__, tx->GetHash().ToString(), vault_index);
            }

            LogPrint(BCLog::DIGIDOLLAR, "DigiDollarStatsIndex: Block %d - DD MINT: +%d DD, +%d DGB collateral (total: %d DD, %d DGB, %d vaults)\n",
                     block.height, ddAmount, collateralAmount, m_total_dd_supply, m_total_collateral, m_vault_count);
        }

        // Process inputs to detect vault redemptions
        // When a DD vault output from a mint tx is spent, subtract it from totals.
        const CTxUndo& tx_undo = block_undo.vtxundo[tx_idx - 1]; // -1 because coinbase has no undo
        bool redemption_accounting_loaded = false;
        CAmount redemption_dd_burned = 0;
        CAmount redemption_collateral = 0;

        for (size_t input_idx = 0; input_idx < tx->vin.size(); ++input_idx) {
            const CTxIn& txin = tx->vin[input_idx];
            const Coin& coin = tx_undo.vprevout[input_idx];

            // Look up vault info from database. This binds redemption accounting
            // to the actual collateral outpoint stored at mint time instead of
            // assuming the vault was vout[0].
            VaultInfo vault_info;
            if (coin.out.scriptPubKey.size() == 34 &&
                coin.out.scriptPubKey[0] == OP_1 &&
                coin.out.nValue > 0 &&
                m_db->Read(DBVaultKey(txin.prevout), vault_info)) {
                // This is a DD vault being redeemed
                CAmount dd_to_subtract = vault_info.dd_amount;
                CAmount collateral_to_subtract = vault_info.collateral;
                if (txType == DigiDollar::DD_TX_REDEEM) {
                    if (!redemption_accounting_loaded) {
                        if (!DigiDollar::ExtractRedemptionAccountingAmounts(*tx, tx_undo.vprevout, tx_lookup,
                                                                            redemption_dd_burned, redemption_collateral)) {
                            return error("%s: Failed to extract redemption accounting for tx %s",
                                         __func__, tx->GetHash().ToString());
                        }
                        redemption_accounting_loaded = true;
                    }
                    dd_to_subtract = redemption_dd_burned;
                    collateral_to_subtract = redemption_collateral;
                }

                if (m_total_dd_supply < dd_to_subtract ||
                    m_total_collateral < collateral_to_subtract ||
                    m_vault_count == 0) {
                    return error("%s: DigiDollar stats underflow while processing vault spend %s:%u",
                                 __func__, txin.prevout.hash.ToString(), txin.prevout.n);
                }

                m_total_dd_supply -= dd_to_subtract;
                m_total_collateral -= collateral_to_subtract;
                m_vault_count--;

                // Keep immutable mint metadata for reorg safety.
                // A disconnected redeem may be mined again on a
                // different branch; deleting this entry would make
                // the alternate redeem invisible to the stats index.

                LogPrint(BCLog::DIGIDOLLAR, "DigiDollarStatsIndex: Block %d - DD REDEMPTION: -%d DD, -%d DGB collateral (total: %d DD, %d DGB, %d vaults)\n",
                         block.height, dd_to_subtract, collateral_to_subtract, m_total_dd_supply, m_total_collateral, m_vault_count);
            }
        }
    }

    // Store updated state to database
    std::pair<uint256, DigiDollarDBVal> value;
    value.first = block.hash;
    value.second.total_dd_supply = m_total_dd_supply;
    value.second.total_collateral = m_total_collateral;
    value.second.vault_count = m_vault_count;

    return m_db->Write(DBHeightKey(block.height), value);
}

bool DigiDollarStatsIndex::CustomRewind(const interfaces::BlockKey& current_tip, const interfaces::BlockKey& new_tip)
{
    CDBBatch batch(*m_db);
    std::unique_ptr<CDBIterator> db_it(m_db->NewIterator());

    // During a reorg, copy hash digests for disconnected blocks
    // from height index to hash index
    DBHeightKey key{new_tip.height};
    db_it->Seek(key);

    for (int height = new_tip.height; height <= current_tip.height; ++height) {
        if (!db_it->GetKey(key) || key.height != height) {
            return error("%s: unexpected key in %s: expected (%c, %d)",
                         __func__, GetName(), DB_BLOCK_HEIGHT, height);
        }

        std::pair<uint256, DigiDollarDBVal> value;
        if (!db_it->GetValue(value)) {
            return error("%s: unable to read value in %s at key (%c, %d)",
                         __func__, GetName(), DB_BLOCK_HEIGHT, height);
        }

        batch.Write(DBHashKey(value.first), std::move(value.second));
        db_it->Next();
    }

    if (!m_db->WriteBatch(batch)) return false;

    // Load state from new_tip
    std::pair<uint256, DigiDollarDBVal> new_tip_state;
    if (!m_db->Read(DBHeightKey(new_tip.height), new_tip_state)) {
        return error("%s: Cannot read state at new tip height %d", __func__, new_tip.height);
    }

    if (new_tip_state.first != new_tip.hash) {
        // Try hash-based lookup
        if (!m_db->Read(DBHashKey(new_tip.hash), new_tip_state.second)) {
            return error("%s: Cannot read state at new tip %s", __func__, new_tip.hash.ToString());
        }
    }

    // Restore running state from new_tip
    m_total_dd_supply = new_tip_state.second.total_dd_supply;
    m_total_collateral = new_tip_state.second.total_collateral;
    m_vault_count = new_tip_state.second.vault_count;

    LogPrint(BCLog::DIGIDOLLAR, "DigiDollarStatsIndex: Rewound to height %d - DD Supply: %d, Collateral: %d, Vaults: %d\n",
             new_tip.height, m_total_dd_supply, m_total_collateral, m_vault_count);

    // Vault metadata is intentionally retained after redemption. Running totals
    // determine active supply/collateral; the side table stores immutable mint
    // amounts needed to account for alternate-branch redemptions after reorgs.

    return true;
}

bool DigiDollarStatsIndex::CustomCommit(CDBBatch& batch)
{
    // No additional state to commit beyond what's in CustomAppend
    return true;
}

static bool LookUpOne(const CDBWrapper& db, const interfaces::BlockKey& block, DigiDollarDBVal& result)
{
    // First check height index
    std::pair<uint256, DigiDollarDBVal> read_out;
    if (!db.Read(DBHeightKey(block.height), read_out)) {
        return false;
    }

    if (read_out.first == block.hash) {
        result = std::move(read_out.second);
        return true;
    }

    // Fall back to hash index for reorged blocks
    return db.Read(DBHashKey(block.hash), result);
}

std::optional<DigiDollarStats> DigiDollarStatsIndex::LookUpStats(const CBlockIndex& block_index) const
{
    DigiDollarDBVal entry;
    if (!LookUpOne(*m_db, {block_index.GetBlockHash(), block_index.nHeight}, entry)) {
        return std::nullopt;
    }

    DigiDollarStats stats;
    stats.total_dd_supply = entry.total_dd_supply;
    stats.total_collateral = entry.total_collateral;
    stats.vault_count = entry.vault_count;
    stats.height = block_index.nHeight;
    stats.block_hash = block_index.GetBlockHash();

    return stats;
}
