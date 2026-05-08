// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// intervals_pairwise_disjoint, mismatch class #2: DEGENERATE
// INVERTED INTERVAL.
//
// Pins that the predicate correctly rejects a malformed interval
// where `lo > hi`.  Witness `[50, 30)` — no integer x satisfies
// `50 <= x < 30`.  Pass-1 well-formedness in the predicate fires
// BEFORE pass-2 pairwise compare runs at all; the predicate
// returns false; CRUCIBLE_PRE's __builtin_trap fires at consteval.
//
// In production this bug manifests as: a `[offset, offset +
// nbytes)` byte-range constructed where `offset + nbytes`
// underflowed, OR where `offset` and `nbytes` got swapped at the
// call site, OR where a Pythonic negative-stride convention
// leaked into a memory-plan slot's offset arithmetic.  Without
// this fixture, the bug surfaces downstream as: a slot occupies
// "[50, 30)" — the kernel reading from `offset_bytes` reads from
// 50, the kernel writing to `offset_bytes + nbytes` writes to 30,
// and the read sees stale (or never-written) bytes.  Silent
// wrong answer.
//
// Anti-pattern targeted: skipping the validity check on each
// interval before doing the pairwise compare.  Specific shapes:
//
//   // Predicate forgets the lo <= hi check, only checks pairwise
//   // disjointness.  An inverted interval would compare as
//   // "to the left of everything" and silently accept.
//   for (auto i, j of pairs) {
//       if (overlap(slots[i], slots[j])) return false;
//   }
//   return true;        // ← inverted slot {[50, 30)} passes
//
//   // OR: caller assumes the call site always builds well-formed
//   // intervals.  But `nbytes` returned by a faulty SIMD-vs-
//   // scalar fallback path may be `static_cast<uint64_t>(-32)`
//   // (massive positive), and `offset + nbytes` then wraps under
//   // unsigned arithmetic, producing `hi < lo` if `offset` was
//   // small.  Cite this predicate at the boundary AND cite
//   // `no_overflow_sum(offset, nbytes)` to gate the derivation
//   // BEFORE building the Interval<T>.
//
// Distinct from the companion fixture (overlap):
//   * overlap                       — fires on `{[0, 100), [50,
//     150)}`.  Both well-formed individually; pass-1 succeeds;
//     pass-2 pairwise rejects.  Catches the boundary-arithmetic
//     bug class (forgot to bump `next_offset` past `nbytes`).
//   * inverted (this fixture)       — fires on a SINGLE inverted
//     interval `[50, 30)`.  Pass-1 well-formedness rejects
//     before pass-2 runs at all.  Catches the missing-validity-
//     check bug class (forgot to verify `lo <= hi` before placing
//     the slot, or trusted an upstream that produced a malformed
//     range).
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

// `[50, 30)` — degenerate inverted interval.  No integer x
// satisfies `50 <= x < 30`.  The predicate's pass-1 well-
// formedness check rejects.  CRUCIBLE_PRE's __builtin_trap
// fires at consteval.
constexpr crucible::decide::Interval<uint64_t> bad[] = {
    {50, 30},
};

constexpr auto witness = gate(std::span{bad});

}  // namespace

int main() { return 0; }
