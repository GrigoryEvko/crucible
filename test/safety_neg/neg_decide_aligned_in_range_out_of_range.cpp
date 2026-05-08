// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// aligned_in_range, mismatch class #2: ALIGNED VALUE OUT OF RANGE
// (bounds-clause violator).
//
// Pins the bounds clauses: the predicate must reject a value that
// is correctly aligned but lies outside `[low, high]`.
// CRUCIBLE_PRE fires `__builtin_trap()` at consteval, which the
// front-end rejects as "non-constant condition".
//
// Witness `(value=512, low=0, high=256, alignment=64)`.  The value
// 512 is correctly aligned (512 % 64 == 0), and the alignment
// guard holds (alignment != 0), but 512 > high (=256), so the
// upper-bound clause fails.  A buggy "alignment-only"
// implementation that drops the bounds check would COMPILE this
// fixture; the correct impl rejects.
//
// In production this bug manifests as: a memory plan offset
// assignment `pre (decide::aligned_in_range(offset, 0, capacity,
// SLOT_ALIGNMENT))` admits an out-of-bounds offset that happens
// to be correctly aligned.  Subsequent typed access through that
// offset reads or writes past the end of the pool, producing:
//
//   - Heap-buffer overflow into the next allocation (malloc-
//     allocated pools): silent data corruption of the next
//     allocator object, or crash in the allocator's free-list
//     bookkeeping.
//   - Page-fault SIGSEGV if the offset crosses an unmapped page
//     boundary.
//   - In a content-addressed cache: arbitrary tensor content
//     written to the wrong cache slot, propagating undetectably
//     through subsequent KernelCache lookups.
//
// The bug is sneaky because:
//
//   1. Bounds violations on aligned offsets are HARDER to debug
//      than misalignment violations: the access succeeds at the
//      hardware level (no SIGBUS), and the corruption appears in
//      a different data structure (the one whose memory was
//      overrun into).  Detection often happens hours later.
//   2. A buggy impl `return (value % alignment) == 0;`
//      (ALIGNMENT-ONLY) accepts this fixture wrongly because
//      512 % 64 == 0.  This fixture catches it.
//   3. The companion fixture (misaligned, in-range) does NOT
//      catch this bug: an ALIGNMENT-ONLY impl correctly rejects
//      a misaligned value (e.g. value=100 against alignment=8).
//      Both fixtures together pin the orthogonal bug-class
//      buckets.
//   4. A buggy impl `return low <= value && value <= high;`
//      (BOUNDS-ONLY) correctly REJECTS this fixture (512 > 256).
//      It would NOT catch the bug class this fixture targets —
//      a BOUNDS-ONLY impl passes this and fails the companion
//      (which uses an in-range but misaligned value).  Caught
//      by companion.
//   5. The witness uses `value=512 = 8*high`, far above the upper
//      bound, to make the bounds violation unambiguous.  A
//      witness like `value=high+alignment` would also catch it,
//      but `8*high` makes the off-by-one-vs-bug-class
//      distinction crystal clear in review.
//
// Anti-pattern targeted: alignment-only / bounds-only / dropped-
// clause implementations of aligned_in_range.  Specific shapes:
//
//   bool aligned_in_range(uint64_t v, uint64_t l, uint64_t h,
//                         uint64_t a) {
//     return (v % a) == 0;          // ALIGNMENT-ONLY
//   }
//     // Drops both bounds clauses.  This fixture catches it:
//     // 512 is aligned to 64 but exceeds high=256.
//
//   bool aligned_in_range(uint64_t v, uint64_t l, uint64_t h,
//                         uint64_t a) {
//     return (v % a) == 0 && a != 0; // ALIGNMENT-AND-GUARD-ONLY
//   }
//     // Drops bounds clauses but keeps the zero-alignment guard.
//     // Same fixture catches it.
//
//   bool aligned_in_range(uint64_t v, uint64_t l, uint64_t h,
//                         uint64_t a) {
//     return l <= v && (v % a) == 0;  // ASYMMETRIC-BOUNDS
//   }
//     // Drops the upper-bound clause (`v <= h`).  This fixture
//     // catches it: 512 ≥ low=0 holds, alignment holds, but
//     // upper bound is dropped.
//
//   bool aligned_in_range(uint64_t v, uint64_t l, uint64_t h,
//                         uint64_t a) {
//     return v <= h && (v % a) == 0;  // ASYMMETRIC-BOUNDS-2
//   }
//     // Drops the lower-bound clause (`l <= v`).  This fixture
//     // does NOT catch it (512 > h=256, the upper bound clause
//     // here would fail correctly).  A separate witness with
//     // `value < low` would be needed; covered by runtime tests.
//
// Distinct from the companion fixture (misaligned, in-range):
//   * misaligned (companion)          — value in range, value %
//     alignment != 0.  Catches BOUNDS-ONLY-style bugs.
//   * out-of-range (this fixture)     — value out of range, value
//     aligned.  Catches ALIGNMENT-ONLY-style bugs and
//     ASYMMETRIC-BOUNDS-DROPPED-UPPER bugs.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// for aligned_in_range: a bounds-only impl passes the companion
// fails this; an alignment-only impl passes this fails the
// companion.
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

// `(value=512, low=0, high=256, alignment=64)` — correctly
// aligned (512 % 64 == 0) but out of range above (512 > 256).
// aligned_in_range rejects; CRUCIBLE_PRE's __builtin_trap fires
// at consteval.  Catches any "alignment-only" buggy
// implementation that drops the bounds check.
constexpr auto witness = gate(512u, 0u, 256u, 64u);

}  // namespace

int main() { return 0; }
