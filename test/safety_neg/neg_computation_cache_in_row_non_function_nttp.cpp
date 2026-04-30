// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F11-AUDIT fixture — cross-constraint coverage for the
// row-aware cache API.  The `_in_row` template carries TWO
// constraints joined by `&&`:
//
//     requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
//
// FOUND-F11 ships ONE neg fixture
// (neg_computation_cache_in_row_non_row_param.cpp) exercising
// the IsEffectRow<Row> half — non-Row template parameter rejected.
// The IsCacheableFunction<FnPtr> half on the `_in_row` path is
// untested by F11.  Without this fixture, a future refactor that
// accidentally weakens the IsCacheableFunction conjunct (e.g.
// drops the `&& IsCacheableFunction<FnPtr>` half during a merge,
// keeps only the IsEffectRow half) would not be caught — bare
// integral-NTTP misuse like `lookup_in_row<42, Row<>, int>()`
// would silently compile and produce a nonsensical cache key
// hashed off an integer.
//
// `42` is not a function pointer — IsCacheableFunction<42> is
// false (the concept's first conjunct, `is_pointer_v<int>`,
// rejects).  The lookup template's requires clause must reject
// this BEFORE the IsEffectRow check on Row<>, but the AND is
// short-circuit-style at the concept level — both must hold —
// so this fixture exercises the IsCacheableFunction conjunct
// as the rejection cause.
//
// Sister-fixture matrix:
//   * neg_computation_cache_integral_nttp.cpp        — `int` NTTP, row-blind path
//   * neg_computation_cache_data_ptr_nttp.cpp        — `int*` NTTP, row-blind path
//   * neg_computation_cache_member_fn_nttp.cpp       — member-fn NTTP, row-blind path
//   * neg_computation_cache_member_data_ptr_nttp.cpp — member-data NTTP, row-blind path
//   * neg_computation_cache_in_row_non_row_param.cpp — non-Row R, row-aware path
//   * neg_computation_cache_in_row_non_function_nttp.cpp ← THIS
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsCacheableFunction<42>.

#include <crucible/cipher/ComputationCache.h>
#include <crucible/effects/EffectRow.h>

int main() {
    // `42` is the FnPtr template arg — bound to `auto FnPtr`.
    // IsCacheableFunction<42> is false (42 is not a function
    // pointer; `decltype(42)` is `int`, and `is_pointer_v<int>`
    // is false — the concept's first conjunct rejects).  The
    // lookup_in_row template's requires clause must reject this
    // BEFORE the IsEffectRow<Row> check on Row<>.
    (void)crucible::cipher::lookup_computation_cache_in_row<
        42, ::crucible::effects::Row<>, int>();
    return 0;
}
