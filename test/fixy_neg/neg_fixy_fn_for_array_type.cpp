// fixy_neg: mint_fn_for<Stance, int[4]>(...) rejects via Stance's
// class-body IsAccepted gate.
//
// HS14 floor for fixy::mint_fn_for (fixy/Fn.h §A11).  Passing an array
// type as the explicit `Type` template parameter instantiates
// `Stance<int[4]>`, whose class-body static_assert fires on the
// Type-axis well-formedness check.  Distinct from mint_fn's
// requires-clause path (D2 fixtures); mint_fn_for has no own requires
// clause and surfaces the diagnostic via stance instantiation.
//
// Expected diagnostic: "IsAccepted" — class-body assertion failure.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;

int main() {
    // Stance template + explicit Type=int[4].  The parameter value is
    // not needed; mint_fn_for fails inside Stance<int[4]> construction.
    int arr[4]{};
    auto bad = fixy::mint_fn_for<fixy::stance::PureLinear, int[4]>(arr);
    (void)bad;
    return 0;
}
