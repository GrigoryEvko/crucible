// fixy_neg: mint_fn_for<Stance, Policy, int[4]>(...) rejects via the
// StanceForBinary concept gate (fixy-H-01).
//
// HS14 floor for the binary mint_fn_for overload (fixy-H-01).  Passing
// an array type as the explicit `Type` template parameter trips
// `detail::TypeIsStanceCompatible<int[4]>` (std::is_array_v) inside
// `StanceForBinary`; the function template's requires-clause rejects
// BEFORE Stance<int[4], Policy> would instantiate.
//
// Sibling of neg_fixy_fn_for_binary_const_type (const-qualified Type);
// together they pin two distinct Type-shape rejection causes for the
// binary stance overload.  Distinct from the unary fixtures (array_type
// + const_type) which exercise the unary overload's StanceForUnary
// concept.
//
// Expected diagnostic: "StanceForBinary" — requires-clause failure at
// the function signature.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;

namespace test_policy {
struct EmitPolicy {};
}  // namespace test_policy

int main() {
    int arr[4]{};
    // Explicit Type=int[4] via the binary overload's template arg list.
    // TypeIsStanceCompatible<int[4]> = false → StanceForBinary fails.
    auto bad = fixy::mint_fn_for<fixy::stance::PublicEmit,
                                 test_policy::EmitPolicy, int[4]>(arr);
    (void)bad;
    return 0;
}
