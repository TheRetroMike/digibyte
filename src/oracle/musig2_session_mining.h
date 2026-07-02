// Copyright (c) 2024-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_ORACLE_MUSIG2_SESSION_MINING_H
#define DIGIBYTE_ORACLE_MUSIG2_SESSION_MINING_H

/**
 * MuSig2 Session Mining — legacy global session map (Wave 10 Agent C note).
 *
 * Re-declares the legacy global signing-session map and its mutex so any
 * translation unit that needs to inspect them (notably the P2P ingestion
 * paths in `OracleBundleManager::ProcessRemoteMusigNonce` /
 * `ProcessRemoteMusigPartialSig` and tests under `src/test/`) can include
 * a single header. The globals are *defined* in
 * `oracle/musig2_orchestrator.cpp`.
 *
 * IMPORTANT — actual production miner path (RC30+):
 *   `OracleBundleManager::AddOracleBundleToBlock` does NOT consult this
 *   global session map for completed bundles. It queries
 *   `g_signing_orchestrator->GetCompletedSession(epoch, ...)` instead,
 *   which reads the orchestrator's private `m_signing_sessions` map.
 *
 *   The legacy global map (`g_oracle_signing_sessions`) and the
 *   companion `OracleBundleManager::CompleteMuSig2Session` method are
 *   retained only for the P2P ingestion shim and the
 *   `musig2_p2p_ingestion_tests` regression suite. They are NOT on the
 *   block-template hot path. Treat them as P2P-shim state, not as the
 *   miner's source of truth.
 *
 * Anyone editing this surface should update both this header comment
 * and the equivalent note in `REPO_MAP_DIGIDOLLAR.md` so the doc/code
 * pair stays consistent (DD-FA-DOC-004).
 */

#include <oracle/musig2_session.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <vector>

// Forward-declare global session map if not already declared in musig2_session.h
#ifndef DIGIBYTE_MUSIG2_SESSION_GLOBALS_DECLARED
#define DIGIBYTE_MUSIG2_SESSION_GLOBALS_DECLARED
extern std::map<int32_t, MuSig2SigningSession> g_oracle_signing_sessions;
extern Mutex g_oracle_signing_sessions_mutex;
#endif

#endif // DIGIBYTE_ORACLE_MUSIG2_SESSION_MINING_H
