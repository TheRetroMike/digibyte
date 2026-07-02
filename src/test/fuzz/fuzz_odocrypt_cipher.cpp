// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/odocrypt.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

FUZZ_TARGET(fuzz_odocrypt_cipher)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    auto consume_block = [&](char (&out)[OdoCrypt::DIGEST_SIZE]) {
        const std::vector<uint8_t> bytes = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, OdoCrypt::DIGEST_SIZE));
        std::memset(out, 0, OdoCrypt::DIGEST_SIZE);
        if (!bytes.empty()) {
            std::memcpy(out, bytes.data(), bytes.size());
        }
    };

    LIMITED_WHILE(fdp.remaining_bytes() > 0, 128) {
        const uint32_t key = fdp.ConsumeIntegral<uint32_t>();

        char plain[OdoCrypt::DIGEST_SIZE];
        char cipher[OdoCrypt::DIGEST_SIZE]{};
        char decrypted[OdoCrypt::DIGEST_SIZE]{};
        consume_block(plain);

        OdoCrypt odo(key);
        odo.Encrypt(cipher, plain);
        odo.Decrypt(decrypted, cipher);

        // Core invariant: OdoCrypt is a permutation over 80-byte blocks.
        assert(std::memcmp(plain, decrypted, OdoCrypt::DIGEST_SIZE) == 0);

        // Regeneration invariant: same key + same plaintext => same ciphertext.
        OdoCrypt odo_regen(key);
        char cipher_regen[OdoCrypt::DIGEST_SIZE]{};
        odo_regen.Encrypt(cipher_regen, plain);
        assert(std::memcmp(cipher, cipher_regen, OdoCrypt::DIGEST_SIZE) == 0);

        // Key-space edge cases: all-zero key and all-one key.
        char edge_cipher_zero[OdoCrypt::DIGEST_SIZE]{};
        char edge_cipher_ones[OdoCrypt::DIGEST_SIZE]{};
        OdoCrypt(0U).Encrypt(edge_cipher_zero, plain);
        OdoCrypt(UINT32_MAX).Encrypt(edge_cipher_ones, plain);

        // All-zero and all-one plaintext edge cases.
        const char zero_plain[OdoCrypt::DIGEST_SIZE] = {};
        char ones_plain[OdoCrypt::DIGEST_SIZE];
        std::memset(ones_plain, 0xFF, OdoCrypt::DIGEST_SIZE);

        char zero_cipher[OdoCrypt::DIGEST_SIZE]{};
        char zero_roundtrip[OdoCrypt::DIGEST_SIZE]{};
        odo.Encrypt(zero_cipher, zero_plain);
        odo.Decrypt(zero_roundtrip, zero_cipher);
        assert(std::memcmp(zero_plain, zero_roundtrip, OdoCrypt::DIGEST_SIZE) == 0);

        char ones_cipher[OdoCrypt::DIGEST_SIZE]{};
        char ones_roundtrip[OdoCrypt::DIGEST_SIZE]{};
        odo.Encrypt(ones_cipher, ones_plain);
        odo.Decrypt(ones_roundtrip, ones_cipher);
        assert(std::memcmp(ones_plain, ones_roundtrip, OdoCrypt::DIGEST_SIZE) == 0);
    }
}
