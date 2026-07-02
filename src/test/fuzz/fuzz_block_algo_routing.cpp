#include <chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <cassert>

namespace {
void initialize_block_algo_routing() { SelectParams(ChainType::REGTEST); }
}

FUZZ_TARGET(fuzz_block_algo_routing_phase2a, .init = initialize_block_algo_routing)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    CBlockHeader h;
    h.nVersion = fdp.ConsumeIntegral<int32_t>();
    h.nTime = fdp.ConsumeIntegral<uint32_t>();
    h.nNonce = fdp.ConsumeIntegral<uint32_t>();

    const uint256 algo_hash = GetPoWAlgoHash(h);
    assert(!algo_hash.IsNull() || h.GetAlgo() == ALGO_UNKNOWN);

    // Canonical algo/version mappings should route to deterministic non-null hashes.
    for (int algo : {ALGO_SHA256D, ALGO_SCRYPT, ALGO_SKEIN, ALGO_QUBIT, ALGO_ODO}) {
        h.nVersion = BLOCK_VERSION_DEFAULT | GetVersionForAlgo(algo);
        assert(!GetPoWAlgoHash(h).IsNull());
    }
}
