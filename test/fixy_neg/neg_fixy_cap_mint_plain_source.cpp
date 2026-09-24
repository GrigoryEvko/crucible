// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Cap fixture #3: `mint_cap` via fixy:: alias rejects a
// plain primitive as Source.
//
// Violation: a bare `int` is NOT a cap_type (no
// `is_cap_type<int>` specialization → false_type primary).
// `CanMintCap<Effect::Alloc, int>` = `is_cap_type_v<int> &&
// row_contains_v<...>` short-circuits to false on the first clause.
// Routing through `fixy::cap::mint_cap` must reject identically.
//
// Distinct from fixtures #1-#2:
//   #1 mint_from_fg — Fg passes is_cap_type but its permitted row is
//                     Row<>; CanMintCap fails on the row-containment
//                     clause.  Rejection class: second-clause failure.
//   #2 mint_from_ctx_wrong_effect — HotFgCtx is a valid ExecCtx but its
//                     cap_type is Fg with Row<>; CtxCanMint fails on
//                     row-containment.  Rejection class: ctx-bound
//                     second-clause failure.
//   #3 mint_plain_source — int is NOT a cap_type at all; CanMintCap
//                     fails on the FIRST clause (is_cap_type_v<int> is
//                     false).  Rejection class: first-clause failure.
//
// This fixture pins the type-system gate that distinguishes "a known
// cap_type with the wrong permitted_row" (fixture #1) from "a random
// type masquerading as a cap source" (this fixture).  Without it, a
// caller could pass any type to mint_cap and rely on the row-check
// clause to fire — but the row-check clause expects an
// `is_cap_type` specialization to provide cap_permitted_row<S>::type,
// and a malformed Source would dereference an undefined primary
// template.  The first clause is the load-bearing soundness gate.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at CanMintCap's is_cap_type clause.

#include <crucible/fixy/Cap.h>

namespace eff = crucible::effects;
namespace cap = crucible::fixy::cap;

int main() {
    int plain_value = 42;
    // plain int is not a cap_type — CanMintCap rejects on the first
    // clause (is_cap_type_v<int> == false).
    auto bad = cap::mint_cap<eff::Effect::Alloc>(plain_value);
    (void)bad;
    return 0;
}
