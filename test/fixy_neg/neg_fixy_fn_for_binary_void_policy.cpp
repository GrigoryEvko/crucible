// fixy_neg: mint_fn_for<Stance, void, Type>(...) rejects via the
// StanceForBinary concept gate — Policy axis (fixy-A4-019).
//
// HS14 fixture 1/2.  Pre-A4-019 the binary mint_fn_for overload's
// requires-clause only enforced `TypeIsStanceCompatible<Type>` — the
// Policy slot was accepted unchecked.  `mint_fn_for<stance::
// SecretConsumer, void>(42)` would silently substitute Type=int and
// then instantiate `stance::SecretConsumer<int, void>`, failing INSIDE
// the stance with a noisy template-instantiation cascade BELOW the
// function signature.  This violates §XXI's "single concept gate at
// the function signature" rule.
//
// Tightened form: `StanceForBinary<Stance, Type, Policy>` now also
// checks `TypeIsStanceCompatible<Policy>`.  `void` Policy trips
// `std::is_void_v<void>` inside the new clause; the requires-clause
// rejects at the function signature.
//
// Distinct from neg_fixy_fn_for_binary_ref_policy.cpp: that fixture
// witnesses rejection via REFERENCE Policy; this fixture witnesses
// rejection via VOID Policy.  Both flow through the SAME new gate
// clause but cover orthogonal Policy-shape mismatches a production
// engineer could trip on (forgetting to write the policy tag at all
// vs. typo'ing a reference instead of a bare class type).
//
// Expected diagnostic: "StanceForBinary" — requires-clause failure at
// the function signature.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;

int main() {
    // void Policy via the binary overload's template arg list:
    // <Stance, Policy, Type>.  TypeIsStanceCompatible<void> = false
    // (std::is_void_v) → StanceForBinary fails → no viable overload.
    auto bad = fixy::mint_fn_for<fixy::stance::SecretConsumer, void>(42);
    (void)bad;
    return 0;
}
