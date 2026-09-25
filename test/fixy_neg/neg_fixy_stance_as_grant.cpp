// fixy_neg: passing a stance alias as a grant tag rejects.
//
// HS14 floor for FIXY-AUDIT-G4 — companion to
// neg_fixy_relaxation_plus_strict_same_axis.  Stance aliases are
// typedefs for `fn<Type, ...full Grants pack...>`; a stance is
// therefore a class type, NOT a grant tag (no `grant_base` base, not
// `final` per the grant discipline).  Authors occasionally try
//
//     crucible_fixy::mint_fn<int, stance::PureLinear<int>, ...>(42)
//
// thinking they can "extend a stance" by adding grants on top.  The
// stance alias must be REJECTED by IsGrantTag at the
// `AllGrantsWellFormed` gate — it doesn't satisfy `IsGrantTag_v`
// because `fn<...>` doesn't inherit `grant_base`.
//
// Expected diagnostic: "AllGrantsWellFormed" or "IsGrantTag" — the
// well-formedness gate fires before engagement/uniqueness checks.

#include <crucible/fixy/_Fn.h>

namespace crucible_fixy = crucible::fixy;

int main() {
    // Pass a stance alias as if it were a grant tag — must reject.
    // (stance::PureLinear<int> is crucible_fixy::fn<int, ...> — a class type,
    // not a grant.)
    auto bad = crucible_fixy::mint_fn<int, crucible_fixy::stance::PureLinear<int>>(42);
    (void)bad;
    return 0;
}
