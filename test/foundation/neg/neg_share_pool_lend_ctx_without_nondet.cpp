// The outcome of a lend reads the schedule: a lend fails while another
// thread holds the exclusive.  So a pool lends only in a context whose row
// holds Nondet.  The compile context admits the row of the tag and holds no
// Nondet.
//
// Expected diagnostic: lend has no candidate, because PoolCtx refuses the
// context.

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
    [[maybe_unused]] auto share = pool.lend(compile);
    return 0;
}
