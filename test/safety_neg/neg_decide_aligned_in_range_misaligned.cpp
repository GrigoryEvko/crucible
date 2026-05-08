// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// aligned_in_range, mismatch class #1: MISALIGNED VALUE within
// the bounds (alignment-clause violator).
//
// Pins the alignment clause: the predicate must reject a value
// that lies within `[low, high]` but is NOT a multiple of
// `alignment`.  CRUCIBLE_PRE fires `__builtin_trap()` at consteval,
// which the front-end rejects as "non-constant condition".
//
// Witness `(value=100, low=0, high=200, alignment=8)`.  The value
// 100 lies within `[0, 200]` (bounds clauses hold), but 100 % 8
// = 4 ≠ 0 (alignment clause fails).  A buggy "bounds-only"
// implementation that drops the alignment check would COMPILE this
// fixture; the correct impl rejects.
//
// In production this bug manifests as: a memory plan offset
// assignment `pre (decide::aligned_in_range(offset, 0, capacity,
// SLOT_ALIGNMENT))` admits a misaligned offset within the pool
// bounds.  Subsequent typed access through that offset reads or
// writes a misaligned address, producing:
//
//   - x86: silent corruption on aligned-store intrinsics
//     (vmovaps, vmovdqa) generating a #GP fault that the runtime
//     translates to SIGSEGV — far from the actual root cause.
//   - ARM: crash with SIGBUS on aligned 16/32-byte loads.
//   - Any architecture: undefined behaviour for atomic ops
//     spanning a cache line that isn't aligned.
//
// The bug is sneaky because:
//
//   1. Alignment violations crash AT USE, not at the precondition.
//      Without the alignment clause in the predicate, the
//      foreground hot path passes a clean pointer to a kernel,
//      the kernel writes to it, and the misalignment manifests
//      as a SIGBUS half a million cycles later in a different
//      function.
//   2. A buggy impl `return low <= value && value <= high;`
//      (BOUNDS-ONLY) accepts this fixture wrongly because
//      0 ≤ 100 ≤ 200 holds.  This fixture catches it.
//   3. The companion fixture (out-of-range) does NOT catch this
//      bug: the BOUNDS-ONLY impl correctly rejects an
//      out-of-range value (e.g. value=300 against high=200).
//      Both fixtures together pin the orthogonal bug-class
//      buckets.
//   4. A buggy impl `return (value % alignment) == 0;`
//      (ALIGNMENT-ONLY) correctly REJECTS this fixture (100 % 8
//      = 4 ≠ 0).  It would NOT catch the bug class this fixture
//      targets — an ALIGNMENT-ONLY impl passes this and fails the
//      companion (which uses an ALIGNED but out-of-range value).
//      Caught by companion.
//
// Anti-pattern targeted: bounds-only / alignment-only / dropped-
// clause implementations of aligned_in_range.  Specific shapes:
//
//   bool aligned_in_range(uint64_t v, uint64_t l, uint64_t h,
//                         uint64_t a) {
//     return l <= v && v <= h;     // BOUNDS-ONLY
//   }
//     // Drops the alignment clause.  This fixture catches it:
//     // value=100 in [0, 200] passes, but should reject because
//     // 100 % 8 != 0.
//
//   bool aligned_in_range(uint64_t v, uint64_t l, uint64_t h,
//                         uint64_t a) {
//     return (v % a) == 0;         // ALIGNMENT-ONLY
//   }
//     // Drops the bounds check AND the zero-alignment guard.
//     // Companion fixture catches the bounds drop; this fixture
//     // does not target alignment-only directly (it shares the
//     // same rejection direction).  However, an alignment-only
//     // impl with `if (a == 0) return true;` (zero-alignment-
//     // accepts variant) would pass this fixture wrongly — caught
//     // only at the alignment-clause witness shape we use.
//
// Distinct from the companion fixture (out-of-range, aligned):
//   * misaligned (this fixture)       — value in range, value %
//     alignment != 0.  Catches BOUNDS-ONLY-style bugs.
//   * out-of-range (companion)        — value out of range, value
//     aligned.  Catches ALIGNMENT-ONLY-style bugs.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for aligned_in_range: a bounds-only impl passes #1 fails #2, an
// alignment-only impl passes #2 fails #1.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr bool gate(std::uint64_t value,
                                  std::uint64_t low,
                                  std::uint64_t high,
                                  std::uint64_t alignment) noexcept {
    CRUCIBLE_PRE(crucible::decide::aligned_in_range(
        value, low, high, alignment));
    return true;
}

// `(value=100, low=0, high=200, alignment=8)` — in range but
// misaligned (100 % 8 = 4).  aligned_in_range rejects;
// CRUCIBLE_PRE's __builtin_trap fires at consteval.  Catches
// any "bounds-only" buggy implementation that drops the
// alignment check.
constexpr auto witness = gate(100u, 0u, 200u, 8u);

}  // namespace

int main() { return 0; }
