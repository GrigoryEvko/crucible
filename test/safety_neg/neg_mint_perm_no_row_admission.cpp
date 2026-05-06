// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-075: ctx-bound permission root mint requires the destination
// ExecCtx to admit permission_row<Tag>.
//
// Violation: DiskSpilledRegionTag carries Row<IO, Block>, while HotFgCtx
// carries Row<>.
// The mint_permission_root<Tag>(ctx) requires-clause must reject this
// before any effectful permission can enter foreground code.
//
// Expected diagnostic: CtxAdmitsPermission / constraints not satisfied.

#include <crucible/permissions/Permission.h>

int main() {
    namespace eff = ::crucible::effects;
    namespace perm = ::crucible::permissions;
    namespace saf = ::crucible::safety;

    auto p = saf::mint_permission_root<perm::tag::DiskSpilledRegionTag>(
        eff::HotFgCtx{});
    saf::permission_drop(std::move(p));
    return 0;
}
