// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for fixy-A1-004 (#1546 / fixy-L-01 #1517):
// Linear<Permission<Tag>> stacking rejection.
//
// Premise: Permission<Tag> is ALREADY a move-only linearity token
// (sizeof = 1, deleted copy, [[nodiscard]], EBO-collapsible per
// CLAUDE.md §XVI).  Wrapping Permission inside Linear<> stacks two
// disciplines without adding any new bug class — both already enforce
// exactly-once via deleted-copy + use-after-move diagnostic — AND
// defeats the §XXI grep discipline by routing Permission token
// synthesis through `mint_linear<Permission<Tag>>` instead of the
// canonical `mint_permission_root<Tag>()` factory.
//
// Linear.h class-body static_assert fires the rejection via the
// is_already_linear<T> trait (true for Permission<Tag> /
// SharedPermission<Tag> specializations).
//
// Distinct mismatch class from companion fixture
// neg_linear_of_shared_permission_redundant.cpp:
//   * This fixture: Linear<Permission<Tag>>          (exclusive-token branch)
//   * Companion:    Linear<SharedPermission<Tag>>    (fractional-token branch)
// Two independent trait specializations; both must fire.
//
// Substring "redundant" pins the diagnostic — Linear.h's static_assert
// message leads with "Linear<Permission<Tag>> / Linear<SharedPermission
// <Tag>> is redundant: ...".

#include <crucible/safety/Linear.h>
#include <crucible/permissions/Permission.h>

namespace {
struct MyTag {};
}  // namespace

int main() {
    using crucible::safety::Linear;
    using crucible::safety::Permission;

    // Should FAIL: Linear<Permission<MyTag>> trips the
    // is_already_linear_v<Permission<MyTag>> static_assert.
    Linear<Permission<MyTag>> wrapped{
        crucible::permissions::mint_permission_root<MyTag>()};
    (void)wrapped;
    return 0;
}
