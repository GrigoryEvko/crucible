// fixy_neg: §XXI Universal Mint Pattern rejects CTAD on fixy::fn —
//           the deduction guide routes `fixy::fn{value}` to the
//           sentinel-typed instantiation that fires tier-0.
//
// fixy-A4-025 negative fixture #1 (HS14 ≥2 floor: CTAD route).  Prior
// to A4-025, `fixy::fn{42}` failed at the DEDUCTION step with an
// opaque "no viable deduction guide" diagnostic — no mention of
// mint_fn, no hint that direct construction was intentional policy.
// CLAUDE.md §XXI Universal Mint Pattern requires every value-carrying
// fixy::fn to be born via a `mint_*` factory so `grep "mint_"` finds
// every binding in the codebase; CTAD bypassing the mint factories
// would silently dilute that grep target.
//
// Post-A4-025, the deduction guide `fn(T) -> fn<sentinel>` routes
// CTAD to a sentinel-typed instantiation, which the class body's
// dedicated tier-0 static_assert detects and reports with a §XXI
// diagnostic naming `mint_fn` / `mint_fn_for<Stance>` as the correct
// entry points.  The sentinel CLASS NAME (`fn_ctad_blocked_use_mint_
// fn_or_mint_fn_for`) ALSO appears verbatim in the compiler's
// "required from" trail, so even before reading the static_assert
// message the user sees an actionable hint.
//
// Reject sequence: CTAD deduction step succeeds (matches the guide) →
// class instantiation `fn<sentinel>` begins → tier-0 static_assert
// fires (Type matches the sentinel) → other tiers silenced via
// short-circuit chain (`!fixy_a4_025_tier0_not_ctad_sentinel || ...`).
//
// Expected diagnostic: `fn_ctad_blocked_use_mint_fn` (sentinel CLASS
// name in error chain) OR `tier 0` (static_assert message tier prefix).

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;

int main() {
    // CTAD route: brace-init.  No explicit template arguments — the
    // compiler picks up the `fn(T) -> fn<sentinel>` deduction guide,
    // instantiates `fn<sentinel>`, and the tier-0 static_assert fires.
    auto bad = fixy::fn{42};
    (void)bad;
    return 0;
}
