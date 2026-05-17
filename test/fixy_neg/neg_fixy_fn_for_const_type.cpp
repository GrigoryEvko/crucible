// fixy_neg: mint_fn_for<Stance, const int>(...) rejects via the
// StanceForUnary concept gate (fixy-H-01).
//
// HS14 floor for fixy::mint_fn_for (fixy/Fn.h §A11 + fixy-H-01).
// Passing `const int` as the explicit `Type` template parameter trips
// `detail::TypeIsStanceCompatible<const int>` (std::is_const_v) inside
// the `StanceForUnary` concept; the function template's requires-clause
// rejects the call BEFORE Stance<const int> would instantiate.  Distinct
// from the array-rejection sibling (decay corruption); both fire the
// same StanceForUnary-named diagnostic chain.
//
// Before fixy-H-01 this rejection surfaced one level deeper via fn<>'s
// class-body IsAccepted static_assert; the requires-clause now names
// the failure at the function signature, matching CLAUDE.md §XXI.
//
// Expected diagnostic: "StanceForUnary" — requires-clause failure at
// the function signature.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;

int main() {
    const int c = 42;
    // Explicit Type=const int.  mint_fn_for instantiates
    // PureLinear<const int>, whose IsAccepted static_assert rejects
    // the const-qualified Type axis.
    auto bad = fixy::mint_fn_for<fixy::stance::PureLinear, const int>(c);
    (void)bad;
    return 0;
}
