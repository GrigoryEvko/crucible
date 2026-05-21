// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `FixedArray<T, N>::at<I>()` where I >= N.  The
// method template carries `requires (I < N)` (FixedArray.h:246) — a
// method-template-level concept-gate that rejects out-of-range
// compile-time indices at overload resolution.
//
// Discipline rationale (FixedArray.h:239-249):
//   FixedArray ships three tiers of bounds-checked access:
//     1. `operator[](size_t)` — no check, std::span/-D_GLIBCXX_DEBUG
//        catches at runtime; documented UB-bounded.
//     2. `at(index_type)` — Refined<bounded_above<N-1>, size_t>
//        proof token; the bounds check runs ONCE at index_type
//        construction, subsequent at(idx) calls trust the proof.
//     3. `at<I>()` — compile-time-known index, requires-clause
//        proves I < N at the call site.  Zero runtime cost, no
//        proof token needed — pure compile-time enforcement.
//
//   THIS fixture pins tier (3): the `requires (I < N)` concept
//   rejects `arr.at<5>()` on FixedArray<T, 4> at overload
//   resolution time.  The discipline scales: any caller writing
//   a literal/constexpr index gets compile-time-checked bounds
//   without any runtime cost or proof-token boilerplate.
//
// HS14 — distinct-class fixture pair for FixedArray<T, N>:
//   * Class U-zero-extent (sibling neg_fixedarray_zero_extent):
//     the class-template-level `requires (N > 0)` rejects
//     FixedArray<T, 0> at template-substitution time.
//   * Class U-compile-time-oob (THIS file): the method-template-
//     level `requires (I < N)` rejects at<I>() for out-of-range
//     I at overload-resolution time.
//
//   Both Class U (concept-gate) but on DISTINCT scopes — class-
//   level non-empty constraint vs method-level index-bounds
//   constraint.
//
// FIXY-U-154 — second of the FixedArray pair (closes its slice of
// #146 A8-P2).

#include <crucible/safety/FixedArray.h>

#include <cstdint>

namespace {

using A4 = ::crucible::safety::FixedArray<std::uint64_t, 4>;

// Anchor: in-range compile-time index compiles cleanly.
[[maybe_unused]] constexpr auto anchor_in_range_at() {
    A4 arr{};
    return arr.at<3>();   // I=3 < N=4 — passes the requires-clause.
}

// VIOLATION: at<5>() on FixedArray<uint64_t, 4> fails the
// `requires (I < N)` concept — I=5, N=4.  GCC emits
// "constraints not satisfied" / "the expression 'I < N' evaluated
// to 'false'" naming the requires-clause at FixedArray.h:246.
[[maybe_unused]] static auto offending_compile_time_oob() {
    A4 arr{};
    return arr.at<5>();
    // ERROR: no matching function for call to 'at<5>()' — concept
    //        not satisfied: '5 < 4'.
}

}  // namespace

int main() { return 0; }
