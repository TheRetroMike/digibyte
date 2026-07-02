// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/KeccakP-800-SnP.h>
#include <crypto/hashodo.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

FUZZ_TARGET(fuzz_odocrypt_keccak)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    unsigned char state[KeccakP800_stateSizeInBytes];
    KeccakP800_Initialize(state);

    // Seed initial state with fuzz bytes.
    const auto init_bytes = fdp.ConsumeBytes<uint8_t>(KeccakP800_stateSizeInBytes);
    if (!init_bytes.empty()) {
        KeccakP800_OverwriteBytes(state, init_bytes.data(), 0, init_bytes.size());
    }

    LIMITED_WHILE(fdp.remaining_bytes() > 0, 128) {
        const unsigned int offset = fdp.ConsumeIntegralInRange<unsigned int>(0, KeccakP800_stateSizeInBytes - 1);
        const unsigned int length = fdp.ConsumeIntegralInRange<unsigned int>(0, KeccakP800_stateSizeInBytes - offset);
        const auto bytes = fdp.ConsumeBytes<uint8_t>(length);

        if (fdp.ConsumeBool() && !bytes.empty()) {
            KeccakP800_AddBytes(state, bytes.data(), offset, bytes.size());
        } else if (!bytes.empty()) {
            KeccakP800_OverwriteBytes(state, bytes.data(), offset, bytes.size());
        }

        if (fdp.ConsumeBool()) {
            KeccakP800_Permute_12rounds(state);
        }
        if (fdp.ConsumeBool()) {
            KeccakP800_Permute_22rounds(state);
        }
    }

    // Deterministic permutation invariant.
    unsigned char state_copy_a[KeccakP800_stateSizeInBytes];
    unsigned char state_copy_b[KeccakP800_stateSizeInBytes];
    std::memcpy(state_copy_a, state, sizeof(state));
    std::memcpy(state_copy_b, state, sizeof(state));
    KeccakP800_Permute_12rounds(state_copy_a);
    KeccakP800_Permute_12rounds(state_copy_b);
    assert(std::memcmp(state_copy_a, state_copy_b, KeccakP800_stateSizeInBytes) == 0);

    // Exercise Odocrypt finisher path (HashOdo uses Keccak-P[800] 12 rounds).
    const uint32_t key = fdp.ConsumeIntegral<uint32_t>();
    std::vector<uint8_t> msg = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, OdoCrypt::DIGEST_SIZE));
    if (msg.empty()) {
        msg.push_back(0);
    }
    const auto h1 = HashOdo(msg.begin(), msg.end(), key);
    const auto h2 = HashOdo(msg.begin(), msg.end(), key);
    assert(h1 == h2);
}
