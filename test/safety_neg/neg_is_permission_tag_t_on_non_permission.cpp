// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating permission_tag_t<T> where T is not a
// Permission specialization.  The alias is constrained on
// is_permission_v<T>, so non-Permission arguments are rejected at
// the requires clause rather than producing `void` (which would be
// a silent failure).
//
// Concrete bug-class this catches: a refactor that drops the
// `requires is_permission_v<T>` constraint on permission_tag_t
// would let `permission_tag_t<int>` resolve to `void` from a
// primary-template default, propagating that void silently into
// downstream type machinery (e.g. `Permission<permission_tag_t<int>>`
// would mint a Permission<void>).  With the constraint, the alias
// rejects non-Permission at the call boundary.
//
// Specifically guards against the SharedPermission ↔ Permission
// confusion: a SharedPermission<X> satisfies a different predicate;
// it MUST NOT slip into permission_tag_t<>.  This fixture passes a
// SharedPermission to confirm the constraint discriminates correctly
// (the simpler `int` case also fails, but a wrapper-vs-wrapper miss
// is the real risk class).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsPermission.h>

namespace { struct neg_test_tag {}; }

int main() {
    // SharedPermission<Tag> is NOT a Permission<Tag> — different CSL
    // surfaces (fractional vs linear).  permission_tag_t must reject.
    using SP = crucible::safety::SharedPermission<neg_test_tag>;
    using Tag = crucible::safety::extract::permission_tag_t<SP>;
    Tag const t{};
    (void)t;
    return 0;
}
