// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-075: SharedPermissionPool::lend(ctx) requires ctx to admit the
// pool tag's permission row.
//
// Violation: HugePageTag carries Row<IO>, while HotFgCtx carries Row<>.
// A reader borrow of that pool must not be minted into foreground code.
//
// Expected diagnostic: CtxAdmitsPermission / constraints not satisfied.

#include <crucible/permissions/Permission.h>

#include <utility>

int main() {
    namespace eff = ::crucible::effects;
    namespace perm = ::crucible::permissions;
    namespace saf = ::crucible::safety;

    auto p = saf::mint_permission_root<perm::tag::HugePageTag>(
        eff::BgCompileCtx{});
    saf::SharedPermissionPool<perm::tag::HugePageTag> pool{std::move(p)};
    auto guard = pool.lend(eff::HotFgCtx{});
    return guard.has_value() ? 0 : 1;
}

