// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating shared_permission_tag_t<T> where T is
// not a SharedPermission specialization.  The alias is constrained
// on is_shared_permission_v<T>, so non-SharedPermission arguments
// are rejected at the requires clause.
//
// Sister fixture to neg_is_permission_tag_t_on_non_permission.cpp:
// the two extractors carry their `requires` constraints
// independently.  A refactor that drops the constraint from
// shared_permission_tag_t while leaving permission_tag_t correctly
// constrained would slip through the linear neg-compile but is
// caught here.
//
// Specifically uses Permission<Tag> (the LINEAR sibling) as the
// reject witness — guards against the Permission ↔ SharedPermission
// confusion in the OTHER direction from the linear neg-compile
// fixture.  Each direction has independent failure modes; both
// fixtures are load-bearing.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsPermission.h>

namespace { struct neg_test_tag {}; }

int main() {
    // Permission<Tag> is NOT a SharedPermission<Tag> — the linear
    // CSL token must not satisfy the fractional-permission gate.
    using P = crucible::safety::Permission<neg_test_tag>;
    using Tag = crucible::safety::extract::shared_permission_tag_t<P>;
    Tag const t{};
    (void)t;
    return 0;
}
