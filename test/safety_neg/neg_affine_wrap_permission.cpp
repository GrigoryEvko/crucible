// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating Affine<Permission<Tag>> downgrades the
// Permission's CSL-frame-rule EXACTLY-ONCE obligation to Affine's
// at-most-once — making the consume OPTIONAL when the frame rule
// REQUIRES it.  The class-body static_assert on
// `!is_already_consume_disciplined_v<T>` MUST reject the call.
//
// HS14 substrate-side rejection gate per CLAUDE.md §XVI: Permission
// tokens carry a finer-grained linearity discipline than Affine; the
// rejection table at the Affine class body keeps the linearity-axis
// wrappers in lockstep with Linear's same-shaped rejection
// (neg_linear_of_permission_redundant.cpp is the Linear-side peer).
// The Linear-side fires with "redundant"; the Affine-side fires with
// "unsound" — distinct DIAGNOSTIC, distinct fixture, distinct
// mismatch class.
//
// Concrete bug-class this catches: a contributor adds a new
// permission-family token (e.g., FederatedPeerPermission) but only
// updates Linear's is_already_linear table, forgetting Affine's
// is_already_consume_disciplined table.  Affine<NewPermission> would
// then compile and silently allow the no-consume drop that breaks
// the frame rule.  This fixture pins the rejection at the canonical
// Permission specialization so a missing token at one end forces a
// compiler-visible action — either add the new token to the
// rejection table OR document why it's exempt.
//
// Pairs with neg_affine_construct_unbuildable.cpp for the 2-fixture
// HS14 floor — one fixture per distinct mismatch class:
//   1. mint-wrong-arg:   substrate-side constructibility gate.
//   2. wrap-permission:  Permission-family rejection table (this).
//
// Substring "unsound" pins the diagnostic — Affine.h's static_assert
// message contains "Affine<Permission<Tag>> / Affine<SharedPermission
// <Tag>> is unsound: ...".

#include <crucible/permissions/Permission.h>
#include <crucible/safety/Affine.h>

namespace {
struct MyAffineTestTag {};
}  // namespace

int main() {
    using crucible::safety::Affine;
    using crucible::safety::Permission;

    // Should FAIL: Affine<Permission<MyAffineTestTag>> trips the
    // is_already_consume_disciplined_v<Permission<MyAffineTestTag>>
    // static_assert at class-body instantiation.
    Affine<Permission<MyAffineTestTag>> bad{
        crucible::permissions::mint_permission_root<MyAffineTestTag>()};
    (void)bad;
    return 0;
}
