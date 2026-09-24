// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An upgrade under a context hands the exclusive back to a scope that
// runs under that context, so the context must admit the row of the tag.
// The region here does IO, and the foreground context admits no IO, so
// the pool's context check refuses the upgrade.
//
// Expected diagnostic: try_upgrade has no viable candidate, and the note
// names PoolCtx.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace {
struct NeedsIo {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
}  // namespace

int main() {
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>
        test_ctx{eff::testing::test()};
    perm::SharedPermissionPool pool{perm::mint_permission_root<NeedsIo>(test_ctx)};
    [[maybe_unused]] auto exclusive = pool.try_upgrade(eff::testing::foreground());
    return 0;
}
