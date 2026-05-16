// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture #1: ctx-bound permission_root via fixy:: alias.
//
// Violation: DiskSpilledRegionTag carries Row<IO, Block> per the
// FOUND-I05 effectful-tag manifest, while HotFgCtx carries Row<>.
// Routing through `fixy::perm::mint_permission_root` must reject
// identically to the substrate `safety::mint_permission_root` —
// proves the fixy:: alias preserves the substrate's
// CtxAdmitsPermission gate.
//
// Expected diagnostic: CtxAdmitsPermission / constraints not satisfied.

#include <crucible/fixy/Perm.h>

int main() {
    namespace eff   = ::crucible::effects;
    namespace permt = ::crucible::permissions;
    namespace fperm = ::crucible::fixy::perm;
    namespace saf   = ::crucible::safety;

    auto p = fperm::mint_permission_root<permt::tag::DiskSpilledRegionTag>(
        eff::HotFgCtx{});
    saf::permission_drop(std::move(p));
    return 0;
}
