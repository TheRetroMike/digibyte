// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <util/chaintype.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <deque>

namespace {
void initialize_multishield_eras() { SelectParams(ChainType::REGTEST); }
static constexpr int ALGOS[NUM_ALGOS] = {ALGO_SHA256D, ALGO_SCRYPT, ALGO_SKEIN, ALGO_QUBIT, ALGO_ODO};
}

FUZZ_TARGET(fuzz_multishield_eras_phase2a, .init = initialize_multishield_eras)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto& params = Params().GetConsensus();
    const arith_uint256 pow_limit = UintToArith256(params.powLimit);

    CBlockIndex prev;
    prev.nHeight = fdp.ConsumeIntegralInRange<int>(1, 2'000'000);
    prev.nTime = fdp.ConsumeIntegralInRange<uint32_t>(1'400'000'000, 2'000'000'000);
    prev.nBits = pow_limit.GetCompact();
    prev.nVersion = BLOCK_VERSION_DEFAULT | BLOCK_VERSION_SHA256D;

    CBlockHeader next;
    next.nTime = prev.nTime + fdp.ConsumeIntegralInRange<uint32_t>(1, 600);

    const int algo = ALGOS[fdp.ConsumeIntegralInRange<int>(0, NUM_ALGOS - 1)];
    next.nVersion = BLOCK_VERSION_DEFAULT | GetVersionForAlgo(algo);

    const int h_v1 = static_cast<int>(params.multiAlgoDiffChangeTarget) - 1;
    const int h_v2 = static_cast<int>(params.alwaysUpdateDiffChangeTarget);
    const int h_v3 = static_cast<int>(params.workComputationChangeTarget);
    const int h_v4 = static_cast<int>(params.workComputationChangeTarget) + 1;

    std::array<unsigned int, 4> era_bits{};
    for (size_t i = 0; i < era_bits.size(); ++i) {
        const int h = (i == 0 ? h_v1 : i == 1 ? h_v2 : i == 2 ? h_v3 : h_v4) +
                      fdp.ConsumeIntegralInRange<int>(0, 32);
        prev.nHeight = h;
        era_bits[i] = GetNextWorkRequired(&prev, &next, params, algo);
    }

    for (unsigned int bits : era_bits) {
        bool neg{false}, ovf{false};
        const arith_uint256 t = arith_uint256().SetCompact(bits, &neg, &ovf);
        assert(!neg);
        assert(!ovf);
        assert(t > 0);
        assert(t <= pow_limit);
    }
}
