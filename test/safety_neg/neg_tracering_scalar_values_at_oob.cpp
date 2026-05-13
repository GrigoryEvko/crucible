// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1057 WRAP-TraceRing-5
// (Entry::scalar_values raw int64_t[5] → safety::FixedArray<int64_t, 5>).
//
// Premise: with the field migrated to FixedArray<int64_t, 5>, a call to
// `entry.scalar_values.at<5>()` must be rejected by FixedArray's
// `requires (I < N)` clause on the proof-token at<I>() overload — N=5
// for the inline-scalar slot count.  Pre-migration, the equivalent
// expression `entry.scalar_values[5]` was raw-array UB the compiler
// could not catch without bounds instrumentation; the migration turns
// it into a hard compile error.
//
// Distinct mismatch class from companion fixture
// neg_tracering_scalar_values_in_place_wrong_arity.cpp:
//   * This fixture: ACCESS-side gate (compile-time index ≥ N rejected by
//     at<I>()'s requires clause).
//   * Companion:    CONSTRUCTION-side gate (variadic ctor's
//     `sizeof...(Args) == N` rejection on a 6-arg brace-init).
//
// Two fixtures cover both directions of the new soundness gate per HS14.

#include <crucible/ir001/TraceRing.h>

int main() {
    crucible::TraceRing::Entry entry{};
    // (I < 5) requires-clause fires: I=5 is out of range for the
    // 5-slot inline scalar buffer.  No fallback overload — at(size_t)
    // does not exist on FixedArray (the run-time tier is operator[]
    // for UB-bounded access, or at(Refined<bounded_above<N-1>>) for
    // a proof-token).  Production code that legitimately reads slot i
    // routes through Entry::get_scalar_type(i) /
    // Entry::set_scalar_type(i, t) helpers (CONTRACT-128 cite
    // `decide::in_range<uint32_t>(i, 0u, 4u)` discharges the bound),
    // never a compile-time-known i ≥ 5.
    (void)entry.scalar_values.at<5>();
    return 0;
}
