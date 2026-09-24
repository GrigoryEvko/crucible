// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A lend with no context names no scope that may read the region.  That
// is sound only for a pure tag.  The region here does IO, and the
// static_assert in the body of lend is the only check that compares its
// row when no context is passed.
//
// Expected diagnostic: the static_assert in lend that asks for an empty
// row when no context is passed.

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
    [[maybe_unused]] auto share = pool.lend();
    return 0;
}
