// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DIGIBYTE_WALLET_WALLETDB_H
#define DIGIBYTE_WALLET_WALLETDB_H

#include <script/sign.h>
#include <wallet/db.h>
#include <wallet/walletutil.h>
#include <key.h>

#include <stdint.h>
#include <string>
#include <vector>

class CScript;
class uint160;
class uint256;
struct CBlockLocator;
struct DDTransaction;
struct WalletCollateralPosition;
struct WalletDDBalance;
struct CDigiDollarOutput;

namespace wallet {
class CKeyPool;
class CMasterKey;
class CWallet;
class CWalletTx;
struct WalletContext;

/**
 * Overview of wallet database classes:
 *
 * - WalletBatch is an abstract modifier object for the wallet database, and encapsulates a database
 *   batch update as well as methods to act on the database. It should be agnostic to the database implementation.
 *
 * The following classes are implementation specific:
 * - BerkeleyEnvironment is an environment in which the database exists.
 * - BerkeleyDatabase represents a wallet database.
 * - BerkeleyBatch is a low-level database batch update.
 */

static const bool DEFAULT_FLUSHWALLET = true;

/** Error statuses for the wallet database.
 * Values are in order of severity. When multiple errors occur, the most severe (highest value) will be returned.
 */
enum class DBErrors : int
{
    LOAD_OK = 0,
    NEED_RESCAN = 1,
    NEED_REWRITE = 2,
    EXTERNAL_SIGNER_SUPPORT_REQUIRED = 3,
    NONCRITICAL_ERROR = 4,
    TOO_NEW = 5,
    UNKNOWN_DESCRIPTOR = 6,
    LOAD_FAIL = 7,
    UNEXPECTED_LEGACY_ENTRY = 8,
    CORRUPT = 9,
};

namespace DBKeys {
extern const std::string ACENTRY;
extern const std::string ACTIVEEXTERNALSPK;
extern const std::string ACTIVEINTERNALSPK;
extern const std::string BESTBLOCK;
extern const std::string BESTBLOCK_NOMERKLE;
extern const std::string CRYPTED_KEY;
extern const std::string CSCRIPT;
extern const std::string DEFAULTKEY;
extern const std::string DESTDATA;
extern const std::string FLAGS;
extern const std::string HDCHAIN;
extern const std::string KEY;
extern const std::string KEYMETA;
extern const std::string LOCKED_UTXO;
extern const std::string MASTER_KEY;
extern const std::string MINVERSION;
extern const std::string NAME;
extern const std::string OLD_KEY;
extern const std::string ORDERPOSNEXT;
extern const std::string POOL;
extern const std::string PURPOSE;
extern const std::string SETTINGS;
extern const std::string TX;
extern const std::string VERSION;
extern const std::string WALLETDESCRIPTOR;
extern const std::string WALLETDESCRIPTORCKEY;
extern const std::string WALLETDESCRIPTORKEY;
extern const std::string WATCHMETA;
extern const std::string WATCHS;

// DigiDollar database keys
extern const std::string DD_POSITION;              // "ddposition" - DDTimeLocks (time-locked DGB backing DigiDollars)
extern const std::string DD_TRANSACTION;           // "ddtx"       - DD transaction history
extern const std::string DD_BALANCE;               // "ddbalance"  - DD balance per address
extern const std::string DD_OUTPUT;                // "ddutxo"     - DD UTXO tracking
extern const std::string DD_METADATA;              // "ddmeta"     - DD wallet metadata
extern const std::string DD_ADDRESS_KEY;           // "ddaddrkey"  - DD address keys for received tokens (plaintext)
extern const std::string DD_OWNER_KEY;             // "ddownerkey" - DD owner keys for minted tokens (plaintext)
extern const std::string DD_CRYPTED_ADDRESS_KEY;   // "ddcaddrkey" - encrypted DD address keys
extern const std::string DD_CRYPTED_OWNER_KEY;     // "ddcownerkey" - encrypted DD owner keys
extern const std::string ORACLE_KEY;               // "oraclekey"  - Oracle private keys by oracle_id
extern const std::string ORACLE_CRYPTED_KEY;       // "oracleckey" - encrypted Oracle private keys by oracle_id

// Keys in this set pertain only to the legacy wallet (LegacyScriptPubKeyMan) and are removed during migration from legacy to descriptors.
extern const std::unordered_set<std::string> LEGACY_TYPES;
} // namespace DBKeys

/* simple HD chain data model */
class CHDChain
{
public:
    uint32_t nExternalChainCounter;
    uint32_t nInternalChainCounter;
    CKeyID seed_id; //!< seed hash160
    int64_t m_next_external_index{0}; // Next index in the keypool to be used. Memory only.
    int64_t m_next_internal_index{0}; // Next index in the keypool to be used. Memory only.

    static const int VERSION_HD_BASE        = 1;
    static const int VERSION_HD_CHAIN_SPLIT = 2;
    static const int CURRENT_VERSION        = VERSION_HD_CHAIN_SPLIT;
    int nVersion;

    CHDChain() { SetNull(); }

    SERIALIZE_METHODS(CHDChain, obj)
    {
        READWRITE(obj.nVersion, obj.nExternalChainCounter, obj.seed_id);
        if (obj.nVersion >= VERSION_HD_CHAIN_SPLIT) {
            READWRITE(obj.nInternalChainCounter);
        }
    }

    void SetNull()
    {
        nVersion = CHDChain::CURRENT_VERSION;
        nExternalChainCounter = 0;
        nInternalChainCounter = 0;
        seed_id.SetNull();
    }

    bool operator==(const CHDChain& chain) const
    {
        return seed_id == chain.seed_id;
    }
};

class CKeyMetadata
{
public:
    static const int VERSION_BASIC=1;
    static const int VERSION_WITH_HDDATA=10;
    static const int VERSION_WITH_KEY_ORIGIN = 12;
    static const int CURRENT_VERSION=VERSION_WITH_KEY_ORIGIN;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown
    std::string hdKeypath; //optional HD/bip32 keypath. Still used to determine whether a key is a seed. Also kept for backwards compatibility
    CKeyID hd_seed_id; //id of the HD seed used to derive this key
    KeyOriginInfo key_origin; // Key origin info with path and fingerprint
    bool has_key_origin = false; //!< Whether the key_origin is useful

    CKeyMetadata()
    {
        SetNull();
    }
    explicit CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nCreateTime = nCreateTime_;
    }

    SERIALIZE_METHODS(CKeyMetadata, obj)
    {
        READWRITE(obj.nVersion, obj.nCreateTime);
        if (obj.nVersion >= VERSION_WITH_HDDATA) {
            READWRITE(obj.hdKeypath, obj.hd_seed_id);
        }
        if (obj.nVersion >= VERSION_WITH_KEY_ORIGIN)
        {
            READWRITE(obj.key_origin);
            READWRITE(obj.has_key_origin);
        }
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        hdKeypath.clear();
        hd_seed_id.SetNull();
        key_origin.clear();
        has_key_origin = false;
    }
};

/** Access to the wallet database.
 * Opens the database and provides read and write access to it. Each read and write is its own transaction.
 * Multiple operation transactions can be started using TxnBegin() and committed using TxnCommit()
 * Otherwise the transaction will be committed when the object goes out of scope.
 * Optionally (on by default) it will flush to disk on close.
 * Every 1000 writes will automatically trigger a flush to disk.
 */
class WalletBatch
{
private:
    template <typename K, typename T>
    bool WriteIC(const K& key, const T& value, bool fOverwrite = true)
    {
        if (!m_batch->Write(key, value, fOverwrite)) {
            return false;
        }
        m_database.IncrementUpdateCounter();
        if (m_database.nUpdateCounter % 1000 == 0) {
            m_batch->Flush();
        }
        return true;
    }

    template <typename K>
    bool EraseIC(const K& key)
    {
        if (!m_batch->Erase(key)) {
            return false;
        }
        m_database.IncrementUpdateCounter();
        if (m_database.nUpdateCounter % 1000 == 0) {
            m_batch->Flush();
        }
        return true;
    }

public:
    explicit WalletBatch(WalletDatabase &database, bool _fFlushOnClose = true) :
        m_batch(database.MakeBatch(_fFlushOnClose)),
        m_database(database)
    {
    }
    WalletBatch(const WalletBatch&) = delete;
    WalletBatch& operator=(const WalletBatch&) = delete;

    bool WriteName(const std::string& strAddress, const std::string& strName);
    bool EraseName(const std::string& strAddress);

    bool WritePurpose(const std::string& strAddress, const std::string& purpose);
    bool ErasePurpose(const std::string& strAddress);

    bool WriteTx(const CWalletTx& wtx);
    bool EraseTx(uint256 hash);

    bool WriteKeyMetadata(const CKeyMetadata& meta, const CPubKey& pubkey, const bool overwrite);
    bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata &keyMeta);
    bool WriteCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata &keyMeta);
    bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey);

    bool WriteCScript(const uint160& hash, const CScript& redeemScript);

    bool WriteWatchOnly(const CScript &script, const CKeyMetadata &keymeta);
    bool EraseWatchOnly(const CScript &script);

    bool WriteBestBlock(const CBlockLocator& locator);
    bool ReadBestBlock(CBlockLocator& locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool ReadPool(int64_t nPool, CKeyPool& keypool);
    bool WritePool(int64_t nPool, const CKeyPool& keypool);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    bool WriteDescriptorKey(const uint256& desc_id, const CPubKey& pubkey, const CPrivKey& privkey);
    bool WriteCryptedDescriptorKey(const uint256& desc_id, const CPubKey& pubkey, const std::vector<unsigned char>& secret);
    bool WriteDescriptor(const uint256& desc_id, const WalletDescriptor& descriptor);
    bool WriteDescriptorDerivedCache(const CExtPubKey& xpub, const uint256& desc_id, uint32_t key_exp_index, uint32_t der_index);
    bool WriteDescriptorParentCache(const CExtPubKey& xpub, const uint256& desc_id, uint32_t key_exp_index);
    bool WriteDescriptorLastHardenedCache(const CExtPubKey& xpub, const uint256& desc_id, uint32_t key_exp_index);
    bool WriteDescriptorCacheItems(const uint256& desc_id, const DescriptorCache& cache);

    bool WriteLockedUTXO(const COutPoint& output);
    bool EraseLockedUTXO(const COutPoint& output);

    // DigiDollar UTXO persistence methods (Fix #5)
    bool WriteDDUTXO(const COutPoint& outpoint, const CAmount& dd_amount);
    bool ReadDDUTXO(const COutPoint& outpoint, CAmount& dd_amount);
    bool EraseDDUTXO(const COutPoint& outpoint);

    // DigiDollar persistence write methods
    bool WriteDDTimeLock(const WalletCollateralPosition& position);
    bool WriteDDTransaction(const DDTransaction& ddtx);
    bool WriteDDBalance(const std::string& address, const WalletDDBalance& balance);
    bool WriteDDOutput(const uint256& output_id, const CDigiDollarOutput& output);
    bool WriteDDMetadata(const std::string& key, const std::string& value);

    // DigiDollar persistence read methods
    bool ReadDDTimeLock(const uint256& dd_timelock_id, WalletCollateralPosition& position);
    bool ReadDDTransaction(const uint256& txid, DDTransaction& ddtx);
    bool ReadDDBalance(const std::string& address, WalletDDBalance& balance);
    bool ReadDDOutput(const uint256& output_id, CDigiDollarOutput& output);
    bool ReadDDMetadata(const std::string& key, std::string& value);

    // DigiDollar persistence erase methods
    bool EraseDDTimeLock(const uint256& dd_timelock_id);
    bool EraseDDTransaction(const uint256& txid);
    bool EraseDDBalance(const std::string& address);
    bool EraseDDOutput(const uint256& output_id);

    // DigiDollar address key persistence (for received DD tokens)
    bool WriteDDAddressKey(const std::array<unsigned char, 32>& output_key, const CKey& key);
    bool ReadDDAddressKey(const std::array<unsigned char, 32>& output_key, CKey& key);
    bool EraseDDAddressKey(const std::array<unsigned char, 32>& output_key);

    // DigiDollar owner key persistence (for minted DD token vault redemption)
    bool WriteDDOwnerKey(const uint256& dd_timelock_id, const CKey& key);
    bool ReadDDOwnerKey(const uint256& dd_timelock_id, CKey& key);
    bool EraseDDOwnerKey(const uint256& dd_timelock_id);

    // Encrypted DigiDollar address key persistence (T4-03a: wallet encryption support)
    bool WriteCryptedDDAddressKey(const std::array<unsigned char, 32>& output_key,
                                  const CPubKey& pubkey,
                                  const std::vector<unsigned char>& vchCryptedSecret);
    bool ReadCryptedDDAddressKey(const std::array<unsigned char, 32>& output_key,
                                  CPubKey& pubkey,
                                  std::vector<unsigned char>& vchCryptedSecret);
    bool EraseCryptedDDAddressKey(const std::array<unsigned char, 32>& output_key);

    // Encrypted DigiDollar owner key persistence (T4-03a: wallet encryption support)
    bool WriteCryptedDDOwnerKey(const uint256& dd_timelock_id,
                                const CPubKey& pubkey,
                                const std::vector<unsigned char>& vchCryptedSecret);
    bool ReadCryptedDDOwnerKey(const uint256& dd_timelock_id,
                                CPubKey& pubkey,
                                std::vector<unsigned char>& vchCryptedSecret);
    bool EraseCryptedDDOwnerKey(const uint256& dd_timelock_id);

    // Oracle key persistence
    bool WriteOracleKey(uint32_t oracle_id, const CKey& key);
    bool HasOracleKey(uint32_t oracle_id);
    bool ReadOracleKey(uint32_t oracle_id, CKey& key);
    bool EraseOracleKey(uint32_t oracle_id);
    bool WriteCryptedOracleKey(uint32_t oracle_id,
                               const CPubKey& pubkey,
                               const std::vector<unsigned char>& vchCryptedSecret);
    bool HasCryptedOracleKey(uint32_t oracle_id);
    bool ReadCryptedOracleKey(uint32_t oracle_id,
                              CPubKey& pubkey,
                              std::vector<unsigned char>& vchCryptedSecret);
    bool EraseCryptedOracleKey(uint32_t oracle_id);

    bool WriteAddressPreviouslySpent(const CTxDestination& dest, bool previously_spent);
    bool WriteAddressReceiveRequest(const CTxDestination& dest, const std::string& id, const std::string& receive_request);
    bool EraseAddressReceiveRequest(const CTxDestination& dest, const std::string& id);
    bool EraseAddressData(const CTxDestination& dest);

    bool WriteActiveScriptPubKeyMan(uint8_t type, const uint256& id, bool internal);
    bool EraseActiveScriptPubKeyMan(uint8_t type, bool internal);

    DBErrors LoadWallet(CWallet* pwallet);
    DBErrors FindWalletTxHashes(std::vector<uint256>& tx_hashes);
    DBErrors ZapSelectTx(std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut);

    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain& chain);

    //! Delete records of the given types
    bool EraseRecords(const std::unordered_set<std::string>& types);

    bool WriteWalletFlags(const uint64_t flags);
    //! Begin a new transaction
    bool TxnBegin();
    //! Commit current transaction
    bool TxnCommit();
    //! Abort current transaction
    bool TxnAbort();

    //! Get database cursor for iteration
    std::unique_ptr<DatabaseCursor> GetNewCursor() { return m_batch->GetNewCursor(); }

private:
    std::unique_ptr<DatabaseBatch> m_batch;
    WalletDatabase& m_database;
};

//! Compacts BDB state so that wallet.dat is self-contained (if there are changes)
void MaybeCompactWalletDB(WalletContext& context);

bool LoadKey(CWallet* pwallet, DataStream& ssKey, DataStream& ssValue, std::string& strErr);
bool LoadCryptedKey(CWallet* pwallet, DataStream& ssKey, DataStream& ssValue, std::string& strErr);
bool LoadEncryptionKey(CWallet* pwallet, DataStream& ssKey, DataStream& ssValue, std::string& strErr);
bool LoadHDChain(CWallet* pwallet, DataStream& ssValue, std::string& strErr);
} // namespace wallet

#endif // DIGIBYTE_WALLET_WALLETDB_H
