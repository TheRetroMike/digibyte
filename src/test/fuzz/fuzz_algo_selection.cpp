#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <cassert>

namespace {
void initialize_algo_selection() { SelectParams(ChainType::REGTEST); }
}

FUZZ_TARGET(fuzz_algo_selection_phase2a, .init = initialize_algo_selection)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    CBlockHeader h;
    h.nVersion = fdp.ConsumeIntegral<int32_t>();

    const int algo = h.GetAlgo();
    if (algo != ALGO_UNKNOWN) {
        assert((h.nVersion & BLOCK_VERSION_ALGO) == GetVersionForAlgo(algo));
    }

    // Invalid/multi-bit mixes should never crash GetPoWAlgoHash routing.
    (void)GetPoWAlgoHash(h);
}
