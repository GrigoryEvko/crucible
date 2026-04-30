// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-H04-AUDIT-1: encapsulation witness for the H04 façade
// migration.  The H04 retarget moved Computation's storage from a
// public `T inner_` field to a private
// `[[no_unique_address]] graded_type impl_` field, with a
// cross-specialization friendship (template <typename, typename>
// friend class Computation;) granting only OTHER Computation
// specializations access — the standard monadic-carrier idiom for
// then()'s body.
//
// This fixture probes the encapsulation boundary: an external
// translation unit (non-Computation, non-friend) reaching for
// `c.impl_` MUST fail to compile.  If the friendship were ever
// accidentally widened (or impl_ moved back to public), this test
// would silently start compiling — a regression we must trip into
// CI before review.
//
// Locks the post-H04 access policy:
//   - impl_ is a private data member.
//   - friend access is bounded to Computation<R2, U> for any R2/U.
//   - external code reaches the substrate ONLY through the public
//     graded() accessor, never through impl_.
//
// Expected diagnostic (GCC 16): "is private within this context" or
// "is private member" or "non-public member".

#include <crucible/effects/Computation.h>

namespace eff = crucible::effects;

int main() {
    auto c = eff::Computation<eff::Row<>, int>::mk(42);

    // External-scope access to the private impl_ field.  Compile error.
    auto& view = c.impl_;
    (void)view;

    return 0;
}
