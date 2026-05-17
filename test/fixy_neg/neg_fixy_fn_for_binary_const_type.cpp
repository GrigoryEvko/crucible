// fixy_neg: mint_fn_for<Stance, Policy, const int>(...) rejects via the
// StanceForBinary concept gate (fixy-H-01).
//
// HS14 floor for the binary mint_fn_for overload covering 2-parameter
// stances (stance::SecretConsumer, stance::PublicEmit).  Passing
// `const int` as the explicit `Type` template parameter trips
// `detail::TypeIsStanceCompatible<const int>` (std::is_const_v) inside
// `StanceForBinary`; the function template's requires-clause rejects
// BEFORE Stance<const int, Policy> would instantiate.
//
// Before fixy-H-01 the binary overload did not exist — the 2-parameter
// stances could not be minted at all.  Now they can be minted cleanly
// AND mint-time Type-shape rejection surfaces at the function signature
// per CLAUDE.md §XXI.
//
// Expected diagnostic: "StanceForBinary" — requires-clause failure at
// the function signature.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;

namespace test_policy {
struct EmitPolicy {};
}  // namespace test_policy

int main() {
    const int c = 42;
    // Explicit Type=const int via the binary overload's template arg
    // list: <Stance, Policy, Type>.  TypeIsStanceCompatible<const int>
    // = false → StanceForBinary fails → no viable overload.
    auto bad = fixy::mint_fn_for<fixy::stance::SecretConsumer,
                                 test_policy::EmitPolicy, const int>(c);
    (void)bad;
    return 0;
}
