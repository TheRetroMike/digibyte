#include <consensus/amount.h>
#include <chainparams.h>
#include <primitives/block.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>

extern int GetAlgoWorkFactor(int nHeight, int algo);

namespace {
void initialize_algo_work_factor() { SelectParams(ChainType::REGTEST); }
}

FUZZ_TARGET(fuzz_algo_work_factor_phase2a, .init = initialize_algo_work_factor)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const int height = fdp.ConsumeIntegral<int>();
    const int algo = fdp.ConsumeIntegralInRange<int>(-4, 16);

    const int wf = GetAlgoWorkFactor(height, algo);
    assert(wf >= 0);
    assert(wf <= 1000000); // geometric mean multiplier should remain bounded

    // MAX_MONEY overflow boundary sanity in same harness.
    assert(MoneyRange(MAX_MONEY));
    assert(!MoneyRange(MAX_MONEY + 1));
}
