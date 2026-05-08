// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// intervals_cover_unit, mismatch class #2: INTERVAL OVERSHOOTS
// UNIT BOUNDARY.
//
// Pins that the predicate correctly rejects a family of intervals
// where at least one subinterval extends past the unit's upper
// bound `total` — i.e., `iv.hi > total`.
//
// Witness `total = 100`, ivs = `{[0, 50), [50, 110)}`.  The two
// intervals are pairwise disjoint and the first fits in `[0, 100)`,
// but the second has hi=110 > 100.  Pass-1's per-interval bounds
// check (`iv.hi <= total`) rejects.
//
// In production this bug manifests as: a Cipher cold-tier blob
// layout where the LAST slot's `[offset, offset + nbytes)` byte
// range extends past `blob_bytes` — writes go past the allocated
// blob, corrupting unrelated data on disk OR truncating silently
// at the filesystem boundary.  Or: a 5D parallelism shard cover
// where the last shard's index range exceeds the global tensor
// dim — out-of-bounds reads.
//
// The bug is sneaky because:
//
//   1. Lengths SUM correctly if the writer derived them from
//      `nbytes_i` values — `50 + 60 = 110` doesn't match `total=100`,
//      but a lazy `pre (sum(widths) == total)` check would catch
//      it; a lazy `pre (forall i, widths[i] > 0)` would not.
//
//   2. Pairwise disjointness still holds — the second interval
//      is just shifted right of the first; no overlap.
//
//   3. Boundary-only checks (`max(hi_i) == total && min(lo_i) ==
//      0`) reject this case BUT accept the gap case from the
//      companion fixture.  Neither alone is sufficient.
//
// Anti-pattern targeted: the predicate that checks pairwise
// disjointness AND coverage-by-sum but FORGETS the per-interval
// bounds check.  Specific shape:
//
//   pre (intervals_pairwise_disjoint(slots) &&
//        sum_widths(slots) == total);
//
// On `{[0, 50), [50, 110)}` this passes both: disjoint AND sum =
// 50 + 60 = 110 != 100.  Hmm, sum check fails here.  But consider
// `{[0, 60), [60, 100), [100, 50)}` — that has an inverted
// interval that pass-1 rejects.  More subtle: `{[-10, 50), [50,
// 100)}` on signed T — sum = 60 + 50 = 110 != 100, but a
// predicate that doesn't check `lo >= 0` would treat it as valid
// pairwise.  intervals_cover_unit's pass-1 catches both `lo < 0`
// AND `hi > total`.
//
// Distinct from the companion fixture (gap):
//   * gap                       — fires on `{[0, 30), [50, 100)}`
//     with total=100.  Sum = 80 != 100; gap is the missing
//     coverage.  Catches the missing-completeness bug class.
//   * overshoot (this fixture)  — fires on `{[0, 50), [50, 110)}`
//     with total=100.  Pass-1 bounds check rejects on hi=110 >
//     100.  Catches the missing-bounds-check bug class.
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

// `{[0, 50), [50, 110)}` with total=100 — second interval's
// hi=110 > total=100 (OVERSHOOT).  Pass-1 bounds check rejects;
// CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr crucible::decide::Interval<uint64_t> bad[] = {
    {0, 50},
    {50, 110},
};

constexpr auto witness = gate(std::span{bad}, uint64_t{100});

}  // namespace

int main() { return 0; }
