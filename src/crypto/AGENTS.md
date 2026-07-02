# CRYPTO/ KNOWLEDGE BASE

## OVERVIEW

Multi-algorithm hashing for DigiByte PoW. Six algorithms: SHA256D, Scrypt, Groestl, Skein, Qubit, Odocrypt.

## STRUCTURE

```
crypto/
├── sha256.cpp/h        # SHA256D (algo 0x0200)
├── scrypt.cpp/h        # Scrypt (algo 0x0000) - Litecoin derived
├── groestl.cpp         # Groestl (algo 0x0400)
├── hashgroestl.h       # Groestl wrapper
├── skein.cpp           # Skein (algo 0x0600)
├── hashskein.h         # Skein wrapper
├── qubit/              # Qubit (algo 0x0800)
├── hashqubit.h         # Qubit wrapper (Luffa+Cubehash+Shavite+Simd+Echo)
├── odo.cpp             # Odocrypt (algo 0x0E00) - FPGA-resistant
├── hashodo.h           # Odocrypt wrapper
├── ctaes/              # AES implementation
├── chacha20.cpp/h      # ChaCha20 stream cipher
├── chacha20poly1305.*  # AEAD for BIP324
├── ripemd160.cpp/h     # RIPEMD-160
├── hmac_sha*.cpp/h     # HMAC variants
└── siphash.cpp/h       # SipHash for hash tables
```

## ALGORITHM USAGE

| Algorithm | Version Bits | Block Header Hash | Notes |
|-----------|--------------|-------------------|-------|
| Scrypt | 0x0000 | Yes | Original, memory-hard |
| SHA256D | 0x0200 | Yes | Bitcoin-compatible |
| Groestl | 0x0400 | Yes | AES-based |
| Skein | 0x0600 | Yes | SHA-3 finalist |
| Qubit | 0x0800 | Yes | 5-hash chain |
| Odocrypt | 0x0E00 | Yes | FPGA-resistant, key changes |

## HASH WRAPPERS

```cpp
// In hashgroestl.h, hashskein.h, hashqubit.h, hashodo.h
template<typename T>
class CHashWriter {
    void Write(const char* data, size_t len);
    uint256 GetHash();
};

// Usage for block hashing
uint256 GetPoWHash(int algo) {
    switch(algo) {
        case ALGO_SCRYPT:  return HashScrypt(...);
        case ALGO_SHA256D: return Hash(...);
        case ALGO_GROESTL: return HashGroestl(...);
        case ALGO_SKEIN:   return HashSkein(...);
        case ALGO_QUBIT:   return HashQubit(...);
        case ALGO_ODO:     return HashOdo(...);
    }
}
```

## ODOCRYPT SPECIAL

```cpp
// Odocrypt changes its internal function every 10 days
// Key derived from block height: key = height / (10 * 24 * 60 * 4)
// FPGA-resistant: requires reconfiguration with each key change
```

## KEY FILES

| File | Purpose |
|------|---------|
| `sha256.cpp` | Optimized SHA256 with hardware acceleration |
| `groestl.cpp` | 132KB, complex AES-based |
| `keccak.cpp` | SHA-3 primitives used by Qubit |
| `odo.cpp` | Odocrypt with dynamic S-boxes |

## ANTI-PATTERNS

| NEVER | Why |
|-------|-----|
| Use wrong hash for algo | Block rejected |
| Ignore algo bits in version | Invalid PoW |
| Mock Scrypt without flag | Tests fail silently |
| Assume fixed Odocrypt | Key rotates with height |
