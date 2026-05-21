// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating `FixedArray<T, 0>`.  The class template
// carries `requires (N > 0)` (FixedArray.h:145) — a class-template-
// level concept-gate that rejects zero-extent instantiation at
// substitution time.
//
// Discipline rationale (FixedArray.h:144-146):
//   FixedArray<T, N>'s entire utility surface depends on N > 0:
//   - `front()` / `back()` are documented as "always valid since
//     N > 0 enforced" (header comment line 256-260) — both
//     dereference data_[0] / data_[N-1] without a runtime check.
//     For N == 0 these would dereference out-of-bounds, instant UB.
//   - The Refined index_type is `bounded_above<N-1>`.  When N == 0,
//     N - 1 underflows to SIZE_MAX (unsigned arithmetic), producing
//     a nonsensical "x ≤ SIZE_MAX" predicate that accepts every
//     value but indexes nothing.
//   - The NSDMI `T data_[N]{};` would be a zero-length C array,
//     which is non-standard in C++ and a known GCC extension trap.
//
//   The requires-clause moves all of these latent UB risks to a
//   compile error at the instantiation site, structurally
//   eliminating the entire failure class.
//
// HS14 — distinct-class fixture pair for FixedArray<T, N>:
//   * Class U-zero-extent (THIS file): the class-template-level
//     `requires (N > 0)` concept rejects FixedArray<T, 0> at
//     substitution.  Pins the structural "non-empty array" invariant
//     at template-substitution time.
//   * Class U-compile-time-oob (sibling neg_fixedarray_compile_time_
//     out_of_range): the method-template-level `requires (I < N)`
//     concept rejects `arr.at<I>()` when I >= N.  Pins the
//     compile-time-known-index bounds discipline at method-resolution
//     time.
//
//   Both Class U (concept-gate) but on DISTINCT scopes — class-
//   level non-empty constraint vs method-level index-bounds
//   constraint.  Two structurally separate enforcement points.
//
// FIXY-U-154 — closes the FixedArray slice of #146 A8-P2 (wrapper
// had ZERO neg-compile fixtures before this ship).

#include <crucible/safety/FixedArray.h>

#include <cstdint>

namespace {

// Anchor: positive-extent instantiation compiles cleanly.
using Anchor4 = ::crucible::safety::FixedArray<std::uint64_t, 4>;
[[maybe_unused]] constexpr Anchor4 anchor_positive_extent{};

// VIOLATION: FixedArray<T, 0> fails the class-template `requires
// (N > 0)` concept.  GCC emits "constraints not satisfied" /
// "the expression 'N > 0' evaluated to 'false'" naming the
// requires-clause at FixedArray.h:145.
using Offending = ::crucible::safety::FixedArray<std::uint64_t, 0>;
[[maybe_unused]] static Offending offending_zero_extent{};

}  // namespace

int main() { return 0; }
