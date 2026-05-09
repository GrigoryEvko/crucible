// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1057 WRAP-TraceRing-5
// (Entry::scalar_values raw int64_t[5] → safety::FixedArray<int64_t, 5>).
//
// Premise: with the field migrated to FixedArray<int64_t, 5>, the
// variadic constructor's `requires (sizeof...(Args) == N)` clause
// rejects partial- or over-fill via brace-init.  Pre-migration the
// equivalent `int64_t scalar_values[5] = {1,2,3,4,5,6}` was a -Werror
// warning at best and a silent truncation under non-strict toolchains;
// the migration turns it into a hard compile error.
//
// Distinct mismatch class from companion fixture
// neg_tracering_scalar_values_at_oob.cpp:
//   * Companion:    ACCESS-side gate (compile-time index ≥ N rejected
//                   by at<I>()'s requires clause).
//   * This fixture: CONSTRUCTION-side gate (the variadic ctor's
//                   `sizeof...(Args) == N` rejection on a 6-arg
//                   brace-init).
//
// Two fixtures cover both directions of the new soundness gate per HS14.

#include <crucible/TraceRing.h>

int main() {
    // Brace-init with 6 arguments hits FixedArray's variadic ctor:
    //   template <typename... Args>
    //   constexpr FixedArray(Args&&... args)
    //       requires (sizeof...(Args) == N) && ...;
    // sizeof...(Args) == 6, N == 5 → constraint not satisfied →
    // ill-formed.  No partial-fill escape hatch (FixedArray rejects
    // sizeof...(Args) < N as well — partial-fill would silently
    // value-init the trailing slots, breaking the per-slot type-tag
    // packing in scalar_types / op_flags[6:7]).
    crucible::safety::FixedArray<int64_t, 5> bad{
        int64_t{1}, int64_t{2}, int64_t{3},
        int64_t{4}, int64_t{5}, int64_t{6}
    };
    (void)bad;
    return 0;
}
