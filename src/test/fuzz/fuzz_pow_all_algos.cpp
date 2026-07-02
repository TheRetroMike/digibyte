#include <chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstring>

namespace {
void initialize_pow_all_algos() { SelectParams(ChainType::REGTEST); }
}

FUZZ_TARGET(fuzz_pow_all_algos_phase2a, .init = initialize_pow_all_algos)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& params = Params().GetConsensus();

    for (int algo : {ALGO_SHA256D, ALGO_SCRYPT, ALGO_SKEIN, ALGO_QUBIT, ALGO_ODO}) {
        CBlockHeader h;
        h.nVersion = BLOCK_VERSION_DEFAULT | GetVersionForAlgo(algo);
        h.nTime = fdp.ConsumeIntegral<uint32_t>();
        h.nNonce = fdp.ConsumeIntegral<uint32_t>();
        h.nBits = fdp.ConsumeIntegral<unsigned int>();

        const uint256 hash = GetPoWAlgoHash(h);
        const bool ok = CheckProofOfWork(hash, h.nBits, params);
        (void)ok;

        // Pow target 0xffffffff compact must be rejected or bounded safely.
        (void)CheckProofOfWork(hash, 0xffffffffU, params);
    }
}
