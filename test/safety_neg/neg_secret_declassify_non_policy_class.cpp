// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-H-24 fixture #1: `Secret<T>::declassify<Policy>()` rejects a
// Policy that is a class type but does NOT derive from
// `secret_policy::secret_policy_base`.
//
// Pre-H-24 the `DeclassificationPolicy` concept admitted ANY class
// type, so an ad-hoc struct anywhere in user code could be passed and
// the grep-only audit trail (`grep "declassify<secret_policy::"`)
// would silently miss it.  H-24 tightens the concept to require
// derivation from the marker base, making the audit trail structurally
// enforced rather than convention-only.
//
// Violation: `EvilPolicy` is a perfectly valid class type but lives
// outside `secret_policy::` and does not inherit `secret_policy_base`,
// so `declassify<EvilPolicy>()` must reject.
//
// Expected diagnostic: "SecretPolicy_NotInBase" (named static_assert
// inside declassify) OR the GCC requires-clause failure citing
// "DeclassificationPolicy" / "constraints not satisfied".

#include <crucible/safety/Secret.h>

#include <cstdint>

using crucible::safety::Secret;

// A class that LOOKS plausible but does not inherit from
// secret_policy_base — exactly the audit-trail-bypass the H-24 gate
// closes.
struct EvilPolicy {};

int main() {
    Secret<std::uint64_t> s{0xDEADBEEFCAFEBABEULL};

    // BAD: EvilPolicy is a class but not derived from
    // secret_policy_base — the concept must reject.
    auto bad = std::move(s).declassify<EvilPolicy>();
    (void)bad;
    return 0;
}
