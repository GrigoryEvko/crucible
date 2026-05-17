// fixy_neg: mint_fn_for<Stance, int[4]>(...) rejects via the
// StanceForUnary concept gate (fixy-H-01).
//
// HS14 floor for fixy::mint_fn_for (fixy/Fn.h §A11 + fixy-H-01).
// Passing an array type as the explicit `Type` template parameter
// trips `detail::TypeIsStanceCompatible<int[4]>` (std::is_array_v)
// inside the `StanceForUnary` concept; the function template's
// requires-clause rejects the call BEFORE Stance<int[4]> would
// instantiate.  Distinct from mint_fn's requires-clause path (D2
// fixtures); the concept gate now matches CLAUDE.md §XXI.
//
// Expected diagnostic: "StanceForUnary" — requires-clause failure at
// the function signature.

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
