// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-217 HS14 fixture #1 of 2 for fixy::cap::CtxAdmitsCap:
// IsExecCtx structural rejection — passing a type that is not an
// ExecCtx<...> instantiation as the Ctx template parameter must
// reject via the IsExecCtx clause of CtxAdmitsCap.
//
// Violation: `CtxAdmitsCap<Ctx, Cap>` is defined as
//
//     IsExecCtx<Ctx> && row_contains_v<row_type_of_t<Ctx>, Cap>
//
// The IsExecCtx<Ctx> clause is hard-checked structurally — Ctx
// must be an ExecCtx<...> template instantiation, and a plain
// non-ExecCtx type cannot satisfy the concept.  The short-circuit
// evaluation order guarantees that row_type_of_t<Ctx> is NEVER
// reached when IsExecCtx<Ctx> = false, so the concept evaluates
// to false WITHOUT hard-erroring on the row_type_of_t alias
// substitution — clean rejection diagnostic.
//
// Distinct from fixture #2 (row-membership rejection):
//   * Fixture #1 — STRUCTURAL rejection at the IsExecCtx clause.
//     Ctx is not an ExecCtx<...> at all.  No row to query.
//   * Fixture #2 — SEMANTIC rejection at the row_contains_v
//     clause.  Ctx IS a valid ExecCtx<...>, but its row_type
//     does not claim the requested Effect.
// Two distinct rejection axes ⇒ HS14 floor satisfied.
//
// Expected diagnostic: static assertion failed mentioning
// CtxAdmitsCap / IsExecCtx (the concept is unsatisfied because
// `int` is not an ExecCtx<...>).

#include <crucible/fixy/Cap.h>

int main() {
    // `int` is not an ExecCtx<...> template instantiation.  The
    // IsExecCtx<int> clause rejects; the concept evaluates to
    // false; the static_assert fires with a grep-discoverable
    // diagnostic.  If the concept ever drifted to admit non-
    // ExecCtx types (e.g. via a duck-typed `requires { typename
    // T::row_type; }` weakening), this fixture would silently
    // compile and a band-3 site could pass a stray int through
    // a cap-admission gate.
    static_assert(::crucible::fixy::cap::CtxAdmitsCap<
                      int, ::crucible::effects::Effect::Alloc>,
        "FIXY-V-217 fixture #1: int is not an ExecCtx — "
        "CtxAdmitsCap must reject via IsExecCtx structural check.");
    return 0;
}
