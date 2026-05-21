// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 3 for FIXY-V-008 (test_row_hash_distinctness).
//
// Premise: the wire-format-break ceremony anchor (`kFoldAnchor`) is the
// single literal that captures the entire wrapper × stance row_hash
// matrix via order-sensitive `combine_ids` fold.  ANY drift — added
// salt, reordered entry, changed Inner contribution, modified mix —
// flips this literal.  The static_assert tied to it RED-FAILS the
// build, forcing reviewers to (a) recompute & paste the new anchor AND
// (b) document the wire-format break in the commit message naming
// which federation cache slots moved.
//
// This fixture witnesses the ceremony fires: it replays V-008's
// consteval algebra with a known 3-entry array against a deliberately-
// wrong anchor literal.  The build's diagnostic text MUST contain
// "ceremony anchor drift" — proof that the production gate's static
// machinery (the combine_ids fold + the static_assert) is operational.
//
// Distinct mismatch class from companion fixtures:
//   * This fixture:                  ANCHOR-side gate (kFoldAnchor pin)
//   * neg_row_hash_collision.cpp:    DISTINCTNESS gate (find_collision)
//   * neg_row_hash_sentinel.cpp:     RESERVED-VALUE gate (no_sentinel_collisions)
//
// If this fixture starts compiling cleanly, the production V-008 gate
// has been weakened — the federation cache key surface is unprotected.

#include <crucible/safety/diag/StableName.h>  // detail::combine_ids
#include <array>
#include <cstdint>

namespace cd = crucible::safety::diag;

// Mirror V-008's kFoldSeed exactly — same algebra, same seed.
inline constexpr std::uint64_t kFoldSeed = 0xC0FFEEBADF00DBA5ULL;

// 3-entry stand-in matrix — well below V-008's 34, but the gate's
// structural property (fold-collapse pinning) is orthogonal to entry
// count; 3 is sufficient to demonstrate.
inline constexpr std::array<std::uint64_t, 3> kHashes = {
    0xAAAA'AAAA'AAAA'AAAAULL,
    0xBBBB'BBBB'BBBB'BBBBULL,
    0xCCCC'CCCC'CCCC'CCCCULL,
};

[[nodiscard]] consteval std::uint64_t fold_anchor() noexcept {
    std::uint64_t acc = kFoldSeed;
    for (auto h : kHashes) acc = cd::detail::combine_ids(acc, h);
    return acc;
}

// Deliberately-wrong literal.  The true fold of {AAAA…, BBBB…, CCCC…}
// under kFoldSeed lands on an entirely different 64-bit value; pinning
// to 0xDEADBEEFDEADBEEF guarantees the static_assert below fails.
inline constexpr std::uint64_t kFoldAnchor = 0xDEAD'BEEF'DEAD'BEEFULL;

static_assert(fold_anchor() == kFoldAnchor,
    "ceremony anchor drift — wire-format break for federation cache");

int main() { return 0; }
