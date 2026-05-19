// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-09 fixture: `declassify<Policy>` requires
// `DeclassificationPolicy<Policy>` (the substrate-side concept from
// safety/Secret.h, which fixy-H-24 tightened to enforce derivation
// from `secret_policy::secret_policy_base`).
//
// Pre-M-09 the fixy-layer `declassify<>` template accepted ANY type;
// an `int` parameter compiled silently and bypassed the substrate's
// audit-trail discipline.  Post-M-09 the rejection fires HERE with
// the named DeclassificationPolicy concept in the diagnostic — the
// same chokepoint Secret::declassify<>()'s member-function gate
// uses, now mirrored at the fixy grant tag.
//
// Pairs structurally with the [SecretPolicy_NotInBase] diagnostic
// emitted by Secret::declassify itself when a non-policy class is
// supplied; both surfaces should reject the same misuse class.
//
// Expected diagnostic: constraints not satisfied /
// DeclassificationPolicy / secret_policy_base / no matching function.

#include <crucible/fixy/Grant.h>

namespace gr = crucible::fixy::grant;

int main() {
    // Should FAIL: `int` is not a class → DeclassificationPolicy<int>
    // evaluates to false (fails the `std::is_class_v` clause and the
    // `std::derived_from<int, secret_policy_base>` clause both) →
    // the requires-clause on `declassify<Policy>` rejects.
    [[maybe_unused]] auto bad = gr::declassify<int>{};
    return 0;
}
