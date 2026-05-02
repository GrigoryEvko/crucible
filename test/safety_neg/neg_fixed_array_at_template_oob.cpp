// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 3 for safety::FixedArray<T, N> (#1081 audit-r2).
//
// Premise: FixedArray<T, N>::at<I>() with I >= N MUST be a compile
// error.  The template's `requires (I < N)` clause makes out-of-range
// compile-time-known indices ill-formed, NOT UB at runtime.
//
// This fixture witnesses the (I < N) gate at TRANSLATION-UNIT SCOPE
// under the project's full -Werror warning matrix — distinct from the
// in-self-test `concept can_compile_at` check which only exercises the
// gate inside a single consteval evaluation context.  Production
// callsites of at<I>() compile under exactly this TU-scope context, so
// the fixture proves the rejection fires under the same conditions.
//
// Without this rejection, `FixedArray<int, 8>::at<99>()` would silently
// dereference data_[99], reading 99*sizeof(int) = 396 bytes past the
// end of the 8-int array — pure stack-OOB UB the compiler can't catch
// without bounds instrumentation.  The (I < N) requires-clause forces
// the call to use one of the runtime-checked tiers instead:
//   (a) operator[](size_type) — UB on OOB, libstdc++ debug catches
//   (b) at(Refined<bounded_above<N-1>, size_t>) — proof-token
//
// Expected diagnostic: "no matching function for call to" /
// "constraints not satisfied" / "associated constraints are not
// satisfied" pointing at the at<99>() call site.  The (I < N)
// constraint fires at template instantiation; no fallback overload
// exists for at<99>().

#include <crucible/safety/FixedArray.h>

namespace saf = crucible::safety;

int main() {
    saf::FixedArray<int, 8> arr{};
    // Bridge fires: I=99 violates (I < N) where N=8.  The at<99>()
    // template is rejected by the requires-clause.  No fallback
    // overload — at(size_t) does NOT exist (that path uses
    // operator[] or at(Refined)).
    (void)arr.at<99>();
    return 0;
}
