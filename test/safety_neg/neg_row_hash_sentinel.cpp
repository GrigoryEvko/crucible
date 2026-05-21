// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 3 for FIXY-V-008 (test_row_hash_distinctness).
//
// Premise: federation `KernelCacheKey` reserves two row_hash values
//   * 0                      — the bare-T payload-blind default
//   * UINT64_MAX (i.e. -1)   — the EMPTY-slot sentinel
// Neither value may be produced by ANY wrapper or stance, or that
// instantiation's cache slot would be indistinguishable from an empty
// / unaddressed cache state.  V-008's `no_sentinel_collisions()`
// consteval guard enforces this; missing it would silently mis-route
// lookups for the colliding wrapper.
//
// This fixture witnesses the reserved-value gate fires: kHashes
// contains 0 at index [1].  The static_assert's regex-matchable text
// MUST contain "EMPTY-slot sentinel".
//
// Distinct mismatch class from companion fixtures:
//   * This fixture:                  RESERVED-VALUE gate (sentinel guard)
//   * neg_row_hash_anchor_drift.cpp: ANCHOR gate (kFoldAnchor pin)
//   * neg_row_hash_collision.cpp:    DISTINCTNESS gate (find_collision)

#include <array>
#include <cstdint>

inline constexpr std::array<std::uint64_t, 3> kHashes = {
    0x1111'1111'1111'1111ULL,
    0ULL,                       // ← deliberately the EMPTY-slot sentinel
    0x3333'3333'3333'3333ULL,
};

[[nodiscard]] consteval bool no_sentinel_collisions() noexcept {
    for (auto h : kHashes) {
        if (h == 0) return false;
        if (h == static_cast<std::uint64_t>(-1)) return false;
    }
    return true;
}

static_assert(no_sentinel_collisions(),
    "EMPTY-slot sentinel — federation cache slot indistinguishable");

int main() { return 0; }
