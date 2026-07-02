#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <cassert>

namespace {
void initialize_difficulty_clamping() { SelectParams(ChainType::REGTEST); }
}

FUZZ_TARGET(fuzz_difficulty_clamping_phase2a, .init = initialize_difficulty_clamping)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& params = Params().GetConsensus();
    const arith_uint256 pow_limit = UintToArith256(params.powLimit);

    CBlockIndex prev;
    prev.nHeight = fdp.ConsumeIntegralInRange<int>(1000, 3'000'000);
    prev.nTime = fdp.ConsumeIntegralInRange<uint32_t>(1'500'000'000, 2'000'000'000);
    prev.nBits = fdp.PickValueInArray<unsigned int>({0U, 1U, pow_limit.GetCompact(), 0x1d00ffffU, 0x207fffffU});
    prev.nVersion = BLOCK_VERSION_DEFAULT | BLOCK_VERSION_SHA256D;

    CBlockHeader next;
    next.nVersion = prev.nVersion;
    next.nTime = prev.nTime + fdp.ConsumeIntegralInRange<uint32_t>(1, 10 * 3600);

    const unsigned int bits = GetNextWorkRequired(&prev, &next, params, ALGO_SHA256D);
    bool neg{false}, ovf{false};
    const arith_uint256 target = arith_uint256().SetCompact(bits, &neg, &ovf);

    assert(!neg);
    assert(!ovf);
    assert(target > 0);
    assert(target <= pow_limit); // MAX_TARGET clamp

    assert(MoneyRange(MAX_MONEY));
    assert(!MoneyRange(MAX_MONEY + 1));
}
