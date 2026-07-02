#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <cassert>
#include <deque>

namespace {
void initialize_local_target_adjustment() { SelectParams(ChainType::REGTEST); }
}

FUZZ_TARGET(fuzz_local_target_adjustment_phase2a, .init = initialize_local_target_adjustment)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& params = Params().GetConsensus();
    const arith_uint256 pow_limit = UintToArith256(params.powLimit);

    std::deque<CBlockIndex> chain(2);
    chain[0].nHeight = static_cast<int>(params.workComputationChangeTarget) + 300;
    chain[0].nTime = 1'700'000'000;
    chain[0].nBits = pow_limit.GetCompact();
    chain[0].nVersion = BLOCK_VERSION_DEFAULT | BLOCK_VERSION_SHA256D;
    chain[1].pprev = &chain[0];
    chain[1].nHeight = chain[0].nHeight + 1;
    chain[1].nTime = chain[0].nTime + fdp.ConsumeIntegralInRange<uint32_t>(1, 3600);
    chain[1].nBits = pow_limit.GetCompact();
    chain[1].nVersion = BLOCK_VERSION_DEFAULT | BLOCK_VERSION_ODO;

    CBlockHeader next;
    next.nVersion = BLOCK_VERSION_DEFAULT | BLOCK_VERSION_ODO;
    next.nTime = chain[1].nTime + fdp.ConsumeIntegralInRange<uint32_t>(1, 7200);

    const unsigned int bits = GetNextWorkRequired(&chain[1], &next, params, ALGO_ODO);
    bool neg{false}, ovf{false};
    const arith_uint256 t = arith_uint256().SetCompact(bits, &neg, &ovf);
    assert(!neg);
    assert(!ovf);
    assert(t > 0 && t <= pow_limit);
}
