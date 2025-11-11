// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2014-2019 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <validation.h>

// Include whatever header actually declares LogPrintf in your codebase.
// In DigiByte, it might be "logging.h" or "util/system.h". Adjust as needed:
#include <logging.h> // or #include <util/system.h> or #include "util.h"

/**
 * CBlockIndex default constructor
 */
CBlockIndex::CBlockIndex()
{
    for (unsigned i = 0; i < NUM_ALGOS_IMPL; i++) {
        lastAlgoBlocks[i] = nullptr;
    }
}

/**
 * CBlockIndex constructor that copies from a block header.
 * We can safely call LogPrintf here because we are in a .cpp file that includes logging.
 */
CBlockIndex::CBlockIndex(const CBlockHeader& block)
    : nVersion(block.nVersion),
      hashMerkleRoot(block.hashMerkleRoot),
      nTime(block.nTime),
      nBits(block.nBits),
      nNonce(block.nNonce)
{
    // Initialize lastAlgoBlocks to null.
    for (unsigned i = 0; i < NUM_ALGOS_IMPL; i++) {
        lastAlgoBlocks[i] = nullptr;
    }

    // Determine raw algo index from version bits:
    int rawAlgo = block.GetAlgo(); // This returns ALGO_UNKNOWN if it doesn't match recognized bits
    if (rawAlgo >= 0 && rawAlgo < NUM_ALGOS_IMPL) {
        lastAlgoBlocks[rawAlgo] = this;
    } else {
        // We can log this occurrence:
        LogPrintf("CBlockIndex ctor: ALGO_UNKNOWN in block version=0x%08x\n", block.nVersion);
    }
}

void CChain::SetTip(CBlockIndex *pindex) {
    if (pindex == nullptr) {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == nullptr) {
        return nullptr;
    }
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

CBlockIndex* CChain::FindEarliestAtLeast(int64_t nTime, int height) const
{
    std::pair<int64_t, int> blockparams = std::make_pair(nTime, height);
    auto lower = std::lower_bound(
        vChain.begin(), vChain.end(), blockparams,
        [](CBlockIndex* pBlock, const std::pair<int64_t, int>& bp) {
            return pBlock->GetBlockTimeMax() < bp.first || pBlock->nHeight < bp.second;
        }
    );
    return (lower == vChain.end() ? nullptr : *lower);
}

/**
 * Return recognized mining algo for this block, forcibly mapping blocks
 * below height 145,000 to ALGO_SCRYPT. If none recognized, logs a warning.
 */
int CBlockIndex::GetAlgo() const
{
    // If we’re on mainnet, for historical reasons we force blocks below 145k to scrypt:
    if (!Params().NetworkIDString().compare("main")) {
        if (nHeight < 145000) {
            return ALGO_SCRYPT;
        }
    }

    // Otherwise, parse from version bits (same as before):
    switch (nVersion & BLOCK_VERSION_ALGO) {
        case BLOCK_VERSION_SCRYPT:   return ALGO_SCRYPT;
        case BLOCK_VERSION_SHA256D:  return ALGO_SHA256D;
        case BLOCK_VERSION_GROESTL:  return ALGO_GROESTL;
        case BLOCK_VERSION_SKEIN:    return ALGO_SKEIN;
        case BLOCK_VERSION_QUBIT:    return ALGO_QUBIT;
        case BLOCK_VERSION_ODO:      return ALGO_ODO;
    }

    // If still not recognized:
    LogPrintf("Warning: block at height=%d has unrecognized nVersion=0x%08x\n", nHeight, nVersion);
    return ALGO_UNKNOWN;
}


/** Turn the lowest '1' bit in the binary representation of a number into '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    return (height & 1)
         ? InvertLowestOne(InvertLowestOne(height - 1)) + 1
         : InvertLowestOne(height);
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    const CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != nullptr &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

int GetAlgoWorkFactor(int nHeight, int algo) 
{
    if (nHeight < Params().GetConsensus().multiAlgoDiffChangeTarget) {
        return 1;
    }

    switch (algo)
    {
        case ALGO_SHA256D:
            return 1;
        case ALGO_SCRYPT:
            return 1024 * 4; // etc...
        case ALGO_GROESTL:
            return 64 * 8;
        case ALGO_SKEIN:
            return 4 * 6;
        case ALGO_QUBIT:
            return 128 * 8;
        default:
            return 1;
    }
}

arith_uint256 GetBlockProofBase(const CBlockIndex& block)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;

    // 2**256 / (bnTarget+1)
    return (~bnTarget / (bnTarget + 1)) + 1;
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    CBlockHeader header = block.GetBlockHeader();
    int nHeight = block.nHeight;
    const Consensus::Params& params = Params().GetConsensus();

    if (nHeight < params.workComputationChangeTarget) {
        arith_uint256 bnBlockWork = GetBlockProofBase(block);
        uint32_t nAlgoWork = GetAlgoWorkFactor(nHeight, header.GetAlgo());
        return bnBlockWork * nAlgoWork;
    } else {
        // Compute the geometric mean across all active algos
        arith_uint256 bnAvgTarget(1);

        for (int i = 0; i < NUM_ALGOS_IMPL; i++) {
            if (!IsAlgoActive(block.pprev, params, i))
                continue;
            unsigned int nBits = GetNextWorkRequired(block.pprev, &header, params, i);
            arith_uint256 bnTarget;
            bool fNegative;
            bool fOverflow;
            bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
            if (fNegative || fOverflow || bnTarget == 0)
                return 0;
            // Instead of multiplying them all together and then taking the
            // nth root at the end, take the roots individually then multiply so
            // that all intermediate values fit in 256-bit integers.
            bnAvgTarget *= bnTarget.ApproxNthRoot(NUM_ALGOS);
        }
        arith_uint256 bnRes = (~bnAvgTarget / (bnAvgTarget + 1)) + 1;
        // scale
        bnRes <<= 7;
        return bnRes;
    }
}

arith_uint256 GetBlockProof(const CBlockIndex& block, int algo)
{
    CBlockHeader header = block.GetBlockHeader();
    int nHeight = block.nHeight;
    const Consensus::Params& params = Params().GetConsensus();

    if (nHeight < params.workComputationChangeTarget) {
        arith_uint256 bnBlockWork = GetBlockProofBase(block);
        uint32_t nAlgoWork = GetAlgoWorkFactor(nHeight, header.GetAlgo());
        return bnBlockWork * nAlgoWork;
    } else {
        if (!IsAlgoActive(block.pprev, params, algo))
            return 0;
        unsigned int nBits = GetNextWorkRequired(block.pprev, &header, params, algo);
        arith_uint256 bnTarget;
        bool fNegative;
        bool fOverflow;
        bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
        if (fNegative || fOverflow || bnTarget == 0)
            return 0;
        return (~bnTarget / (bnTarget + 1)) + 1;
    }
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from,
                                    const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}

const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb)
{
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}
