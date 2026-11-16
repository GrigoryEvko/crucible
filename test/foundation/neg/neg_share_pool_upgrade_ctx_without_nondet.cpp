// The outcome of an upgrade reads the schedule: an upgrade fails while
// another thread holds a share.  So a pool gives the exclusive only in a
// context whose row holds Nondet.  The compile context admits the row of the
// tag and holds no Nondet.
//
// Expected diagnostic: try_upgrade has no candidate, because PoolCtx refuses
// the context.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/SharedPermission.h>

namespace {
struct NeedsIo {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
}  // namespace

int main() {
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    eff::detail::ctx_witnesses::BgCompileCtx const compile{eff::testing::bg()};
    perm::SharedPermissionPool pool{perm::mint_permission_root<NeedsIo>(compile)};
    [[maybe_unused]] auto exclusive = pool.try_upgrade(compile);
    return 0;
}
