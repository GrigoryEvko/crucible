// fixy_neg: mint_fn_for<Stance, EmitPolicy&, Type>(...) rejects via the
// StanceForBinary concept gate — Policy axis (fixy-A4-019).
//
// HS14 fixture 2/2.  Pre-A4-019 the binary mint_fn_for overload's
// requires-clause only enforced `TypeIsStanceCompatible<Type>` — the
// Policy slot was accepted unchecked.  Substituting an lvalue-reference
// Policy like `EmitPolicy&` would silently instantiate `stance::
// SecretConsumer<int, EmitPolicy&>`, breaking the phantom-tag identity
// invariant (a tag's class type IS its identity; a reference is a
// distinct type that defeats type-id matching).
//
// Tightened form: `StanceForBinary<Stance, Type, Policy>` now also
// checks `TypeIsStanceCompatible<Policy>`.  Reference Policy trips
// `std::is_reference_v<EmitPolicy&>` inside the new clause; the
// requires-clause rejects at the function signature.
//
// Distinct from neg_fixy_fn_for_binary_void_policy.cpp: that fixture
// witnesses rejection via VOID Policy (forgetting the tag entirely);
// this fixture witnesses rejection via REFERENCE Policy (typo'ing a
// reference instead of a bare class type — a production engineer's
// "I have a `const PolicyTag& my_policy;` variable lying around, why
// not just use its type?" mistake).
//
// Expected diagnostic: "StanceForBinary" — requires-clause failure at
// the function signature.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;

namespace test_policy {
struct EmitPolicy {};
}  // namespace test_policy

int main() {
    // Reference Policy via the binary overload's template arg list:
    // <Stance, Policy, Type>.  TypeIsStanceCompatible<EmitPolicy&> =
    // false (std::is_reference_v) → StanceForBinary fails → no viable
    // overload.
    auto bad = fixy::mint_fn_for<fixy::stance::SecretConsumer,
                                 test_policy::EmitPolicy&>(42);
    (void)bad;
    return 0;
}
