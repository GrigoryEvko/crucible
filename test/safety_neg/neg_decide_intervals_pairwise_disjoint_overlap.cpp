// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// intervals_pairwise_disjoint, mismatch class #1: ADJACENT-PAIR
// OVERLAP.
//
// Pins that the predicate correctly rejects two intervals whose
// half-open ranges share at least one integer.  Witness `[0, 100)`
// and `[50, 150)` overlap on `[50, 100)` — the canonical sweep-
// line offset-assignment bug where production code forgets to
// bump `next_offset` past the previous slot's nbytes before
// placing the next slot.
//
// In production this bug manifests as: a MemoryPlan with two
// TensorSlots whose `[offset_bytes, offset_bytes + nbytes)`
// byte ranges overlap.  Reads from one slot will trample writes
// from the other (or vice versa).  Two failure modes downstream:
//
//   1. Silent wrong results — the reader tensor sees corrupted
//      data because the writer overwrote bytes the reader still
//      needed.  The training step's loss is wrong; the model
//      slowly diverges.  Hard to debug because the corruption
//      is intermittent (depends on op scheduling order).
//
//   2. Crash — if the corruption hits a non-data field (size,
//      stride, dtype tag), the next op may compute on a TensorMeta
//      with garbage extents and segfault deep in a kernel.
//
// Anti-patterns targeted (all REJECTED on review per HS14):
//
//   * `pre (forall i, slots[i].hi <= slots[i+1].lo)` — adjacent-
//     only check.  `[0, 100)` and `[50, 150)` would pass this if
//     the spans are NOT pre-sorted (the predicate doesn't check
//     for a sort), AND for any sorted pair the check is correct
//     ONLY for adjacent pairs — distant overlaps slip through.
//     The adjacent-only form admits `{[0, 200), [50, 100), [200,
//     300)}`: the i+1 check on (0,1) is `200 <= 50` (false), and
//     code that keys on this fails open.
//
//   * `pre (forall i, slots[i].hi <= pool_bytes)` — capacity
//     check, not pairwise disjointness.  Two 60-byte slots in
//     a 100-byte pool both fit individually but their `[0, 60)`
//     and `[40, 100)` byte ranges overlap.
//
//   * `pre (sum(nbytes) <= pool_bytes)` — capacity check via
//     summation.  Same orthogonality bug: the SUM tells you
//     nothing about overlap.
//
// Distinct from the companion fixture (inverted):
//   * overlap (this fixture)         — fires on `[0, 100)` and
//     `[50, 150)`.  Both well-formed individually; the pairwise
//     compare rejects.  Catches the boundary-arithmetic bug class.
//   * inverted                       — fires on a SINGLE inverted
//     interval `[50, 30)`.  Pass-1 well-formedness rejects
//     before pass-2 pairwise compare runs at all.  Catches the
//     missing-validity-check bug class.
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
    std::span<const crucible::decide::Interval<uint64_t>> ivs
) noexcept {
    CRUCIBLE_PRE(crucible::decide::intervals_pairwise_disjoint(ivs));
    return true;
}

// Two intervals that overlap on `[50, 100)`.  Adjacent-only
// (`hi[0] <= lo[1]`) forms accept this when spans are NOT pre-
// sorted in the consumer; the full pairwise predicate rejects.
constexpr crucible::decide::Interval<uint64_t> bad[] = {
    {0, 100},
    {50, 150},
};

constexpr auto witness = gate(std::span{bad});

}  // namespace

int main() { return 0; }
