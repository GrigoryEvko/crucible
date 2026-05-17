// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Cap fixture #4: `mint_from_ctx` via fixy:: alias rejects
// a non-ExecCtx type.
//
// Violation: a bare struct without `cap_type` / `row_type` typedefs
// is NOT an ExecCtx (fails IsExecCtx).  `CtxCanMint<NonCtx, Alloc>` =
// `IsExecCtx<NonCtx> && row_contains_v<...>` short-circuits to false
// on the first clause.  Routing through `fixy::cap::mint_from_ctx`
// must reject identically.
//
// Distinct from fixtures #1-#3:
//   #1 mint_from_fg                    — non-ctx-bound: Fg as Source.
//   #2 mint_from_ctx_wrong_effect      — ctx-bound: HotFgCtx with
//                                        Row<>; CtxCanMint fails on
//                                        row-containment clause.
//   #3 mint_plain_source               — non-ctx-bound: int as Source;
//                                        fails on is_cap_type clause.
//   #4 mint_from_non_ctx               — ctx-bound: NonCtx with no
//                                        typedefs; fails on IsExecCtx
//                                        clause (this fixture).
//
// This fixture pins the structural gate that distinguishes "a valid
// ExecCtx that happens to lack a specific row entry" (fixture #2)
// from "a random type masquerading as an ExecCtx" (this fixture).
// Without it, a caller could pass any type to mint_from_ctx and rely
// on the row-check clause to fire — but the row clause would
// dereference an undefined `Ctx::cap_type` typedef.  IsExecCtx is the
// load-bearing structural gate that ensures the row clause has a
// well-formed source to query.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at CtxCanMint's IsExecCtx clause.

#include <crucible/fixy/Cap.h>

namespace eff = crucible::effects;
namespace cap = crucible::fixy::cap;

// A struct that is NOT an ExecCtx — no cap_type typedef, no row_type
// typedef, no is_exec_ctx specialization.
struct CapNegFixture4_NonCtx {
    int payload = 0;
};

int main() {
    CapNegFixture4_NonCtx non_ctx{};
    // NonCtx is not an ExecCtx — CtxCanMint rejects on the first
    // clause (IsExecCtx<NonCtx> == false).
    auto bad = cap::mint_from_ctx<eff::Effect::Alloc>(non_ctx);
    (void)bad;
    return 0;
}
