// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-075: permission handoff into a new ExecCtx requires that ctx to
// admit the tag's permission row.
//
// Violation: DiskSpilledRegionTag carries Row<IO, Block>.  TestRunnerCtx
// can mint it, but BgCompileCtx carries only Row<Bg, Alloc, IO>; it does
// not admit the full permission row because Block is absent.
// permission_handoff must reject the transfer.
//
// Expected diagnostic: CtxAdmitsPermission / constraints not satisfied.

#include <crucible/permissions/Permission.h>

#include <utility>

int main() {
    namespace eff = ::crucible::effects;
    namespace perm = ::crucible::permissions;
    namespace saf = ::crucible::safety;

    auto p = saf::mint_permission_root<perm::tag::DiskSpilledRegionTag>(
        eff::TestRunnerCtx{});
    auto handed = saf::permission_handoff(eff::BgCompileCtx{}, std::move(p));
    saf::permission_drop(std::move(handed));
    return 0;
}
