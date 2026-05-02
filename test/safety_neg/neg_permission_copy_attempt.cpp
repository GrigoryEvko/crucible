// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copy-constructing a Permission<Tag>.  Permission<Tag> is
// the foundational CSL frame-rule token (O'Hearn 2007) — a single
// instance proves exclusive ownership of the region named by Tag.
// Duplicating it would create two simultaneous owners and break the
// frame rule, allowing the kind of aliased mutation CSL was designed
// to forbid.
//
// The copy constructor is `= delete` with the named reason
// "linear — duplicating creates two simultaneous owners of the same
// region, breaking CSL's frame rule".  The diagnostic carries the
// substring "linear" which the harness pattern-matches.
//
// This pins the discipline at the second-most-foundational L0
// concurrency primitive (after Linear<T> itself; see
// neg_linear_copy_attempt.cpp for the resource-ownership analog).
// Without this guard, a future edit that accidentally relaxes the
// copy constraint (e.g. by adding a "convenience" copy for testing
// purposes) would silently permit aliased mutation in any handle
// that holds a Permission member, undermining every PermissionedFoo
// primitive that rests on it.
//
// Task #146 (A8-P2 Neg-compile coverage); see
// include/crucible/permissions/Permission.h.

#include <crucible/permissions/Permission.h>

struct MyTag {};

int main() {
    using crucible::safety::Permission;
    using crucible::safety::mint_permission_root;

    auto p1 = mint_permission_root<MyTag>();

    // Should FAIL: Permission<Tag>'s copy constructor is = delete
    // with reason "linear — duplicating creates two simultaneous
    // owners of the same region, breaking CSL's frame rule.  Use
    // std::move to transfer."
    Permission<MyTag> p2 = p1;
    (void)p2;

    return 0;
}
