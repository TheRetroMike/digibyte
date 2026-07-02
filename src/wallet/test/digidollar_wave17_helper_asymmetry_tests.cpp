// Copyright (c) 2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// =============================================================================
// Wave 17 Agent A - Wallet helper asymmetry (defense-in-depth)
// =============================================================================
//
// DD-FA-FUNC-026 (Wave 17 Agent A, defense-in-depth)
//
// Wave 7 left a deferred carry-forward: the wallet-side helper
// `DigiDollarWalletUtils::GetLockDaysForTier(tier)` returns 1 for tier 0
// (src/wallet/digidollarwallet.cpp:7290), while the RPC-side helper at
// src/rpc/digidollar.cpp:64 returns 0 for tier 0. The consensus
// canonical lock for tier 0 is exactly 240 blocks, produced by
// `LockDaysToBlocks(0) = 240` (src/consensus/digidollar.cpp:56-63).
//
//   - RPC helper: tier 0 -> days=0   -> LockDaysToBlocks(0) = 240   (correct)
//   - Wallet helper: tier 0 -> days=1 -> LockDaysToBlocks(1) = 5760 (wrong)
//
// The wallet-side helper is currently NOT used in any active tx-building
// path -- the only call site at digidollarwallet.cpp:4259 sits inside a
// `/* GREEN phase implementation would be: ... */` comment block, and a
// repo-wide grep finds no other consumer of `DigiDollarWalletUtils::
// GetLockDaysForTier` or `DigiDollarWallet::GetLockDaysForTier`. The
// helper IS however publicly exposed via the
// `wallet/digidollarwallet.h` namespace `DigiDollarWalletUtils`, so any
// future integrator picking it up by name (especially during the
// commented "GREEN phase" wiring) would silently produce a tier-0 mint
// with `lockHeight = currentHeight + 5760`, which consensus then
// rejects with `bad-mint-lock-tier-duration` (per CLAUDE.md and the
// commits e1dd69f99b / 11728a6980 that pinned the canonical-tier rule).
//
// This pin asserts both helpers agree, and that
// `LockDaysToBlocks(helper(tier))` produces the canonical block count
// for every tier including the 1-hour testing tier 0. A regression
// that flips either side back to the pre-fix asymmetry will fail this
// test before it ships, even if the broken helper is never wired in
// at the same time.
//
// Threat model: if any future commit wires the wallet helper into a
// tx-building path (e.g. by uncommenting the GREEN-phase block in
// MintDigiDollar), tier-0 mints would be rejected by consensus and
// the wallet would silently broadcast unmineable transactions. The
// pin keeps the helper aligned with the canonical durations the
// validator enforces.

#include <boost/test/unit_test.hpp>

#include <consensus/digidollar.h>
#include <test/util/setup_common.h>
#include <wallet/digidollarwallet.h>

namespace wallet {

BOOST_FIXTURE_TEST_SUITE(digidollar_wave17_helper_asymmetry_tests, BasicTestingSetup)

// Canonical block counts per tier, sourced from the per-tier comment in
// src/consensus/digidollar.cpp and the Qt mint widget table at
// src/qt/digidollarmintwidget.cpp:960-972.
struct CanonicalTier {
    uint32_t tier;
    int64_t expected_blocks;
};

static const CanonicalTier kCanonicalTiers[] = {
    {0,        240},   // 1 hour
    {1,     172800},   // 30 days
    {2,     518400},   // 90 days
    {3,    1036800},   // 180 days
    {4,    2102400},   // 1 year
    {5,    4204800},   // 2 years
    {6,    6307200},   // 3 years
    {7,   10512000},   // 5 years
    {8,   14716800},   // 7 years
    {9,   21024000},   // 10 years
};

// =============================================================================
// W17A-01: wallet helper -> LockDaysToBlocks must hit canonical block counts
// =============================================================================
//
// Pins the only thing that matters for safety: feeding the wallet
// helper's day output through the consensus `LockDaysToBlocks`
// converter must yield exactly the canonical block count for every
// tier, including the 1-hour testing tier 0. Pre-fix, the wallet
// helper returns 1 for tier 0, which `LockDaysToBlocks` interprets
// as 1 day = 5760 blocks; consensus then rejects the mint with
// `bad-mint-lock-tier-duration`.

BOOST_AUTO_TEST_CASE(w17a_01_wallet_helper_blocks_match_canonical_tiers)
{
    for (const auto& expected : kCanonicalTiers) {
        const int days = DigiDollarWalletUtils::GetLockDaysForTier(expected.tier);
        const int64_t blocks = DigiDollar::LockDaysToBlocks(days);
        BOOST_CHECK_MESSAGE(
            blocks == expected.expected_blocks,
            "DD-FA-FUNC-026: tier " << expected.tier <<
            " wallet helper produced " << blocks <<
            " blocks via LockDaysToBlocks(" << days <<
            "); canonical consensus value is " << expected.expected_blocks <<
            ". This asymmetry would mismap the lock height in any future "
            "tx-builder caller and would be rejected with "
            "bad-mint-lock-tier-duration.");
    }
}

// =============================================================================
// W17A-02: wallet helper and RPC helper must produce identical block counts
// =============================================================================
//
// Independent sanity check: after the helpers are routed through
// `LockDaysToBlocks`, both must yield the same result. They are allowed
// to disagree on the day count (the RPC helper uses days=0 as the
// canonical "1-hour testing" sentinel; the wallet helper has historically
// used days=1 for the same tier), but feeding either through
// `LockDaysToBlocks` must produce the canonical tier block count, not
// two different values that diverge on tier 0.
//
// The local copy of the RPC helper here mirrors src/rpc/digidollar.cpp:64
// so the test is self-contained and does not need to link against the
// node-side rpc tu.

namespace {
int RpcLockDaysForTier(uint32_t tier)
{
    switch (tier) {
        case 0: return 0;     // 1 hour (240 blocks) handled by LockDaysToBlocks
        case 1: return 30;
        case 2: return 90;
        case 3: return 180;
        case 4: return 365;
        case 5: return 730;
        case 6: return 1095;
        case 7: return 1825;
        case 8: return 2555;
        case 9: return 3650;
        default: return 0;
    }
}
} // namespace

BOOST_AUTO_TEST_CASE(w17a_02_wallet_helper_matches_rpc_helper_in_blocks)
{
    for (const auto& expected : kCanonicalTiers) {
        const int wallet_days = DigiDollarWalletUtils::GetLockDaysForTier(expected.tier);
        const int rpc_days = RpcLockDaysForTier(expected.tier);
        const int64_t wallet_blocks = DigiDollar::LockDaysToBlocks(wallet_days);
        const int64_t rpc_blocks = DigiDollar::LockDaysToBlocks(rpc_days);
        BOOST_CHECK_MESSAGE(
            wallet_blocks == rpc_blocks,
            "DD-FA-FUNC-026: tier " << expected.tier <<
            " wallet helper -> " << wallet_blocks <<
            " blocks, RPC helper -> " << rpc_blocks <<
            " blocks. Helpers disagree; tx-builder callers would compute "
            "the wrong lock height depending on which side wired them in.");
    }
}

class ExposedDigiDollarWallet : public DigiDollarWallet
{
public:
    using DigiDollarWallet::ValidateMintParams;
};

BOOST_AUTO_TEST_CASE(w17a_03_wallet_mint_param_validation_accepts_tier_zero)
{
    ExposedDigiDollarWallet wallet;
    const CAmount amount = Params().GetDigiDollarParams().minMintAmount;

    BOOST_CHECK_MESSAGE(
        wallet.ValidateMintParams(amount, 0),
        "DD-FA-FUNC-026: wallet mint parameter validation must accept tier 0; "
        "consensus and the UI define tier 0 as the 1-hour testing lock.");
    BOOST_CHECK(wallet.ValidateMintParams(amount, 1));
    BOOST_CHECK(wallet.ValidateMintParams(amount, 9));
    BOOST_CHECK(!wallet.ValidateMintParams(amount, 10));
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace wallet
