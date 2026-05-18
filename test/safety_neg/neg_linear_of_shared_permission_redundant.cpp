// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for fixy-A1-004 (#1546 / fixy-L-01 #1517):
// Linear<SharedPermission<Tag>> stacking rejection.
//
// Premise: SharedPermission<Tag> is the FRACTIONAL-permission token
// in the CSL family (fractional 1/N read share via
// SharedPermissionPool).  Like Permission<Tag>, it is a stateless
// move-only token whose authority is encoded by the type rather than
// by per-token state — the share count lives in the Pinned Pool,
// and the SharedPermission token is just a proof-of-issue.
//
// Wrapping SharedPermission<Tag> in Linear<> stacks the linearity
// discipline twice (the pool already keeps the refcount; the token
// is already deleted-copy + move-only) AND breaks the §XXI grep
// chain — `mint_permission_share<Tag>(...)` is the authorization
// point; `mint_linear<SharedPermission<Tag>>(...)` would emit fresh
// authority OUTSIDE the share factory family.
//
// Distinct mismatch class from companion fixture
// neg_linear_of_permission_redundant.cpp:
//   * Companion: Linear<Permission<Tag>>          (exclusive-token branch)
//   * This file: Linear<SharedPermission<Tag>>    (fractional-token branch)
// Two independent is_already_linear<T> specializations; both must
// fire to witness the trait covers BOTH permission family carriers.
//
// Substring "redundant" pins the diagnostic — same Linear.h
// static_assert message as the companion.

#include <crucible/safety/Linear.h>
#include <crucible/permissions/Permission.h>

namespace {
struct MyShareTag {};
}  // namespace

int main() {
    using crucible::safety::Linear;
    using crucible::safety::SharedPermission;
    using crucible::permissions::mint_permission_root;
    using crucible::permissions::mint_permission_share;

    // Should FAIL: Linear<SharedPermission<MyShareTag>> trips the
    // is_already_linear_v<SharedPermission<MyShareTag>> static_assert.
    auto excl = mint_permission_root<MyShareTag>();
    Linear<SharedPermission<MyShareTag>> wrapped{
        mint_permission_share(std::move(excl))};
    (void)wrapped;
    return 0;
}
