// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 3 for FIXY-V-008 (test_row_hash_distinctness).
//
// Premise: V-008's `find_collision()` consteval sweep enforces pairwise
// distinctness across every wrapper × stance row_hash.  A subtle salt
// drift, a copy-paste between two `row_hash_contribution<>` specia-
// lizations, or a stale combine_ids ordering could silently produce
// two equal hashes and collapse two federation cache slots — the
// exact Agent 4 bug class FIXY-V-001/V-002 closed at the substrate
// surface.  V-008's matrix sweep is the structural sentinel that
// would catch a regression in either direction.
//
// This fixture witnesses the distinctness gate fires: a deliberately-
// duplicated entry at index [0] and [2] triggers `find_collision()` to
// return a non-default CollisionIndices.  The static_assert's regex-
// matchable text MUST contain "row_hash collision".
//
// Distinct mismatch class from companion fixtures:
//   * This fixture:                  DISTINCTNESS gate (find_collision)
//   * neg_row_hash_anchor_drift.cpp: ANCHOR gate (kFoldAnchor pin)
//   * neg_row_hash_sentinel.cpp:     RESERVED-VALUE gate (sentinel guard)

#include <array>
#include <cstddef>
#include <cstdint>

inline constexpr std::array<std::uint64_t, 3> kHashes = {
    0x1111'1111'1111'1111ULL,
    0x2222'2222'2222'2222ULL,
    0x1111'1111'1111'1111ULL,  // ← deliberate duplicate of entry [0]
};

// Mirrors V-008's CollisionIndices + find_collision algebra exactly.
struct CollisionIndices {
    std::size_t i = static_cast<std::size_t>(-1);
    std::size_t j = static_cast<std::size_t>(-1);
    [[nodiscard]] constexpr bool ok() const noexcept {
        return i == static_cast<std::size_t>(-1);
    }
};

[[nodiscard]] consteval CollisionIndices find_collision() noexcept {
    for (std::size_t i = 0; i < kHashes.size(); ++i) {
        for (std::size_t j = i + 1; j < kHashes.size(); ++j) {
            if (kHashes[i] == kHashes[j]) return {i, j};
        }
    }
    return {};
}

static_assert(find_collision().ok(),
    "row_hash collision in the canonical wrapper × stance matrix");

int main() { return 0; }
