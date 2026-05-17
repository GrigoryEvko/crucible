// fixy_neg: mint_fn_for<Stance, const int>(...) rejects via Stance's
// class-body IsAccepted gate.
//
// HS14 floor for fixy::mint_fn_for (fixy/Fn.h §A11).  Passing
// `const int` as the explicit `Type` template parameter instantiates
// `Stance<const int>`, whose class-body static_assert fires on the
// Type-axis well-formedness check (`std::is_const_v<const int>` makes
// the wrapper's copy-assignment silently broken).  Distinct from the
// array-rejection sibling (decay corruption); both must fire the same
// IsAccepted-named diagnostic chain.
//
// Expected diagnostic: "IsAccepted" — class-body assertion failure
// surfaced through Stance<const int> instantiation inside mint_fn_for.

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
