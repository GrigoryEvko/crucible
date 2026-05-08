// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// intervals_cover_unit, mismatch class #1: GAP IN COVERAGE.
//
// Pins that the predicate correctly rejects a family of intervals
// that is pairwise disjoint AND each contained in `[0, total)`,
// BUT does not exhaustively cover `[0, total)` — there is at
// least one integer in the unit range that no subinterval covers.
//
// Witness `total = 100`, ivs = `{[0, 30), [50, 100)}`.  The two
// intervals are pairwise disjoint, both fit inside `[0, 100)`,
// and individually look fine.  But the integers 30..49 (range
// `[30, 50)`) are uncovered — a 20-byte gap.
//
// In production this bug manifests as: a Cipher cold-tier blob
// layout where slot byte ranges leave gaps.  The unused gap bytes
// either waste disk space (best case) or are read at recovery
// time as garbage and treated as valid slot data (worst case —
// silent corruption).  Or: a 5D parallelism shard cover where
// not every batch element is assigned to a worker — those
// elements are never trained on, silently degrading model
// quality without error.
//
// Anti-pattern targeted: the predicate that checks pairwise
// disjointness AND containment but FORGETS to verify exhaustive
// coverage.  Specific shapes:
//
//   pre (intervals_pairwise_disjoint(slots) &&
//        forall i: slots[i].lo >= 0 && slots[i].hi <= total);
//
// Both clauses pass on `{[0, 30), [50, 100)}`, but the partition
// is incomplete.  intervals_cover_unit's third check (sum of
// widths == total) catches it: 30 + 50 = 80 != 100.
//
// Distinct from the companion fixture (overshoot):
//   * gap (this fixture)         — fires on `{[0, 30), [50, 100)}`
//     with total=100.  All in-bounds; pairwise disjoint; sum = 80
//     != 100.  Catches the missing-completeness bug class.
//   * overshoot                   — fires on `{[0, 50), [50, 110)}`
//     with total=100.  Pairwise disjoint and lo≥0, but the second
//     interval's hi=110 > total=100.  Pass-1 bounds check rejects
//     before the sum is computed.  Catches the missing-bounds-
//     check bug class.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <span>

namespace {

[[nodiscard]] constexpr bool gate(
    std::span<const crucible::decide::Interval<uint64_t>> ivs,
    uint64_t total
) noexcept {
    CRUCIBLE_PRE(crucible::decide::intervals_cover_unit(ivs, total));
    return true;
}

// `{[0, 30), [50, 100)}` with total=100 — GAP at `[30, 50)`.
// Pairwise disjoint AND each contained in `[0, 100)`, but the
// union does not equal `[0, 100)`.  Sum of widths = 30 + 50 = 80
// != 100.  Predicate rejects; CRUCIBLE_PRE's __builtin_trap fires
// at consteval.
constexpr crucible::decide::Interval<uint64_t> bad[] = {
    {0, 30},
    {50, 100},
};

constexpr auto witness = gate(std::span{bad}, uint64_t{100});

}  // namespace

int main() { return 0; }
