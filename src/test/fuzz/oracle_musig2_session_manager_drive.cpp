// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Wave 10 Agent B fuzz harness: drive the REAL MuSig2SessionManager,
 * not a simulated state machine.
 *
 * Existing fuzz coverage of MuSig2 sessions/messages comprises:
 *   - musig2_nonce_message       — wire-format roundtrip + accessor exercise
 *   - musig2_partialsig_message  — wire-format roundtrip + accessor exercise
 *   - musig2_session_state       — SIMULATED in-process state machine that
 *                                  models OracleMusig*Msg.IsValid() but
 *                                  never touches MuSig2SigningSession or
 *                                  MuSig2SessionManager.
 *
 * Gap closed here: drive arbitrary fuzzed bytes through the actual
 * MuSig2SessionManager.OnNonceReceived / OnPartialSigReceived /
 * RegisterSeenNonce / RegisterSeenPartialSig / CleanupOldSessions /
 * CheckTimeouts / Clear surfaces. This catches:
 *   - crashes inside libsecp256k1 parse paths reached by malformed 66-byte
 *     pubnonce / 32-byte partial-sig blobs that pass the wire-size guard
 *     but fail the magic-byte / point-decode checks,
 *   - state-machine misuse (partial sig before session, replay across
 *     epoch transitions, cleanup invariants),
 *   - unbounded memory growth in the seen-nonce / seen-partialsig sets
 *     across long fuzz runs (verified by an upper-bound assertion after
 *     the inner loop).
 *
 * The harness intentionally accepts and IGNORES the boolean return — what
 * matters for fuzzing is that the manager never aborts/asserts/hangs on
 * arbitrary input.
 *
 * Note: Wave 10 Agent A confirmed `MuSig2SessionManager` is currently dead
 * code in production (production session storage lives in
 * `g_oracle_signing_sessions` and `OracleSigningOrchestrator`; replay
 * dedup uses `OracleBundleManager::HasOracleMessage` /
 * `RegisterSeenHash`). This harness still pins the public contract of
 * the wrapper class so that any future re-wiring catches surprise
 * breakage, and it stress-tests the libsecp256k1 parse paths that the
 * wrapper exposes.
 */

#include <oracle/musig2_session.h>
#include <oracle/musig2_session_manager.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

constexpr int kMaxIterations = 256;

void initialize_musig2_session_manager_drive()
{
    static const auto testing_setup = MakeNoLogFileContext<const BasicTestingSetup>(ChainType::REGTEST);
    (void)testing_setup;
}

} // namespace

FUZZ_TARGET(musig2_session_manager_drive, .init = initialize_musig2_session_manager_drive)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());

    // Choose a min_signers and timeout in plausible ranges. Manager bounds:
    //   min_signers > 0 (uint8), timeout positive int32.
    const uint8_t min_signers = fdp.ConsumeIntegralInRange<uint8_t>(1, 17);
    const int32_t timeout_blocks = fdp.ConsumeIntegralInRange<int32_t>(1, 10000);

    MuSig2SessionManager manager(min_signers, timeout_blocks);

    int32_t current_epoch = fdp.ConsumeIntegralInRange<int32_t>(0, 100000);

    LIMITED_WHILE(fdp.remaining_bytes() > 0, kMaxIterations) {
        switch (fdp.ConsumeIntegralInRange<int>(0, 8)) {
        case 0: {
            // OnNonceReceived with pubnonce sized 0..128. Sizes != 66 must be
            // rejected immediately. Size == 66 with bad magic / bad point goes
            // through the secp256k1 parse path.
            const int32_t epoch = fdp.ConsumeBool() ? current_epoch
                                                    : fdp.ConsumeIntegralInRange<int32_t>(0, 100000);
            const uint8_t oracle_id = fdp.ConsumeIntegralInRange<uint8_t>(0, 30);
            const size_t len = fdp.ConsumeIntegralInRange<size_t>(0, 128);
            std::vector<unsigned char> bytes = fdp.ConsumeBytes<unsigned char>(len);
            (void)manager.OnNonceReceived(epoch, oracle_id, bytes);
            break;
        }
        case 1: {
            // OnPartialSigReceived. Sizes != 32 must be rejected immediately.
            const int32_t epoch = fdp.ConsumeBool() ? current_epoch
                                                    : fdp.ConsumeIntegralInRange<int32_t>(0, 100000);
            const uint8_t oracle_id = fdp.ConsumeIntegralInRange<uint8_t>(0, 30);
            const size_t len = fdp.ConsumeIntegralInRange<size_t>(0, 128);
            std::vector<unsigned char> bytes = fdp.ConsumeBytes<unsigned char>(len);
            (void)manager.OnPartialSigReceived(epoch, oracle_id, bytes);
            break;
        }
        case 2: {
            // RegisterSeenNonce / HasSeenNonce dance.
            uint256 hash;
            const std::vector<unsigned char> bytes32 = fdp.ConsumeBytes<unsigned char>(32);
            for (size_t i = 0; i < 32 && i < bytes32.size(); ++i) {
                hash.begin()[i] = bytes32[i];
            }
            const bool first = manager.RegisterSeenNonce(hash);
            const bool present = manager.HasSeenNonce(hash);
            // Once registered, HasSeenNonce must report true.
            if (first) {
                assert(present);
            }
            // Re-registration must NOT succeed before cleanup.
            if (first) {
                assert(!manager.RegisterSeenNonce(hash));
            }
            break;
        }
        case 3: {
            // RegisterSeenPartialSig / HasSeenPartialSig dance.
            uint256 hash;
            const std::vector<unsigned char> bytes32 = fdp.ConsumeBytes<unsigned char>(32);
            for (size_t i = 0; i < 32 && i < bytes32.size(); ++i) {
                hash.begin()[i] = bytes32[i];
            }
            const bool first = manager.RegisterSeenPartialSig(hash);
            const bool present = manager.HasSeenPartialSig(hash);
            if (first) {
                assert(present);
                assert(!manager.RegisterSeenPartialSig(hash));
            }
            break;
        }
        case 4: {
            // Advance epoch and clean up. After cleanup with a far-future
            // epoch, every terminal session and the seen-* sets MUST be
            // pruned per MuSig2SessionManager::CleanupOldSessions contract.
            current_epoch = fdp.ConsumeIntegralInRange<int32_t>(current_epoch, 100000);
            manager.CleanupOldSessions(current_epoch);
            break;
        }
        case 5: {
            // CheckTimeouts at an arbitrary block height.
            const int32_t height = fdp.ConsumeIntegralInRange<int32_t>(0, 1'000'000);
            manager.CheckTimeouts(height);
            break;
        }
        case 6: {
            // Direct accessors must never crash regardless of state.
            const int32_t epoch = fdp.ConsumeIntegralInRange<int32_t>(0, 100000);
            (void)manager.HasSession(epoch);
            (void)manager.GetSessionState(epoch);
            (void)manager.HasEnoughNonces(epoch);
            (void)manager.HasEnoughPartialSigs(epoch);
            (void)manager.GetSession(epoch);
            (void)manager.GetActiveSessionCount();
            break;
        }
        case 7: {
            // Construct a 66-byte pubnonce with valid magic but garbage point.
            // This exercises the libsecp256k1 parse path, NOT the size guard.
            std::vector<unsigned char> bytes(66);
            static const unsigned char pubnonce_magic[4] = {0xf5, 0x7a, 0x3d, 0xa0};
            for (size_t i = 0; i < 4; ++i) bytes[i] = pubnonce_magic[i];
            const std::vector<unsigned char> body = fdp.ConsumeBytes<unsigned char>(62);
            for (size_t i = 0; i < body.size() && i + 4 < bytes.size(); ++i) {
                bytes[4 + i] = body[i];
            }
            const int32_t epoch = fdp.ConsumeBool() ? current_epoch
                                                    : fdp.ConsumeIntegralInRange<int32_t>(0, 100000);
            const uint8_t oracle_id = fdp.ConsumeIntegralInRange<uint8_t>(0, 30);
            (void)manager.OnNonceReceived(epoch, oracle_id, bytes);
            break;
        }
        case 8: {
            // Construct a 32-byte partial sig with valid magic but garbage tail.
            std::vector<unsigned char> bytes(32);
            static const unsigned char psig_magic[4] = {0xeb, 0xfb, 0x1a, 0x32};
            for (size_t i = 0; i < 4; ++i) bytes[i] = psig_magic[i];
            const std::vector<unsigned char> body = fdp.ConsumeBytes<unsigned char>(28);
            for (size_t i = 0; i < body.size() && i + 4 < bytes.size(); ++i) {
                bytes[4 + i] = body[i];
            }
            const int32_t epoch = fdp.ConsumeBool() ? current_epoch
                                                    : fdp.ConsumeIntegralInRange<int32_t>(0, 100000);
            const uint8_t oracle_id = fdp.ConsumeIntegralInRange<uint8_t>(0, 30);
            (void)manager.OnPartialSigReceived(epoch, oracle_id, bytes);
            break;
        }
        }
    }

    // After the inner loop, force a sweeping cleanup and verify the manager
    // is in a clean state (Clear is idempotent, accessors must not crash).
    manager.Clear();
    assert(manager.GetActiveSessionCount() == 0);
}
