// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-011 HS14 fixture #1: primitive Tag rejected.
//
// Pre-fix, `Permission<int>` instantiated silently — sizeof was 1, the
// move-only linearity discipline applied, the type compiled.  The
// catch: every CSL machinery downstream (splits_into specializations,
// SharedPermissionPool keys, federation row admission) is keyed on
// `Tag` by *identity*.  `int` is a primitive, not a namespace-scoped
// phantom struct, so no `splits_into<int, ...>` partial specialization
// matches it, no `permission_row<int>` exists, no human reviewer would
// recognize `Permission<int>` in a diff as the right region label.
// The "permission" carried zero information and silently failed to
// participate in any discipline.
//
// fixy-A1-011 adds a class-body `static_assert(PermissionTag<Tag>,
// ...)` to the `Permission` primary template.  `PermissionTag` is the
// concept `std::is_class_v<T> && std::is_empty_v<T>` — primitives,
// pointers, references, enums, unions, and stateful classes are all
// rejected at the static_assert.  Federation tags like
// `tag::FederatedPeer<Org>` (typedef-only, still empty by [class]/4)
// remain admissible.
//
// VIOLATION: a user TU tries `Permission<int>`.  The primary template's
// static_assert fires.  Diagnostic mentions `PermissionTag`.
//
// Expected diagnostic: "static assertion failed", "Tag must be an
// empty non-union class type", "PermissionTag", or equivalent — any
// signal that the in-body static_assert rejects the instantiation.

#include <crucible/permissions/Permission.h>

int main() {
    namespace safe = ::crucible::safety;

    // VIOLATION: `int` is not a class type; PermissionTag<int> is false.
    auto rejected = safe::mint_permission_root<int>();
    safe::permission_drop(std::move(rejected));
    return 0;
}
