// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/odocrypt.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>

#include <cassert>
#include <cstdint>
#include <cstring>

FUZZ_TARGET(fuzz_odocrypt_keygen)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    LIMITED_WHILE(fdp.remaining_bytes() > 0, 256) {
        const uint32_t seed = fdp.ConsumeIntegral<uint32_t>();

        // Constructor exercises key-schedule generation (S-box/P-box/rotations).
        OdoCrypt generated(seed);
        OdoCrypt regenerated(seed);

        char plain[OdoCrypt::DIGEST_SIZE];
        if (fdp.ConsumeBool()) {
            std::memset(plain, 0x00, OdoCrypt::DIGEST_SIZE);
        } else if (fdp.ConsumeBool()) {
            std::memset(plain, 0xFF, OdoCrypt::DIGEST_SIZE);
        } else {
            const auto data = fdp.ConsumeBytes<uint8_t>(OdoCrypt::DIGEST_SIZE);
            std::memset(plain, 0x00, OdoCrypt::DIGEST_SIZE);
            if (!data.empty()) {
                std::memcpy(plain, data.data(), data.size());
            }
        }

        char cipher_a[OdoCrypt::DIGEST_SIZE]{};
        char cipher_b[OdoCrypt::DIGEST_SIZE]{};
        char roundtrip[OdoCrypt::DIGEST_SIZE]{};

        generated.Encrypt(cipher_a, plain);
        regenerated.Encrypt(cipher_b, plain);
        regenerated.Decrypt(roundtrip, cipher_a);

        // Regeneration determinism + roundtrip safety invariants.
        assert(std::memcmp(cipher_a, cipher_b, OdoCrypt::DIGEST_SIZE) == 0);
        assert(std::memcmp(plain, roundtrip, OdoCrypt::DIGEST_SIZE) == 0);

        // Explicitly hammer extreme seeds too.
        char edge_cipher[OdoCrypt::DIGEST_SIZE]{};
        OdoCrypt(0U).Encrypt(edge_cipher, plain);
        OdoCrypt(UINT32_MAX).Encrypt(edge_cipher, plain);
    }
}
