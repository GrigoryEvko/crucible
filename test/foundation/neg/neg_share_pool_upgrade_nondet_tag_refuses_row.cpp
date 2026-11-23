// Negative-compile test. This file must fail to compile.
//
// The upgrade with the bare tag of Nondet is only for a tag whose row is
// empty, because the tag of Nondet cannot admit the row of a region.  The
// region here does IO, so the upgrade takes a context that admits IO and
// holds Nondet, and the form with the bare tag does not compile.
//
// Expected diagnostic: try_upgrade with the tag of Nondet has an unsatisfied
// constraint, permission_row_empty.

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
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>
        test_ctx{eff::testing::test()};
    perm::SharedPermissionPool pool{perm::mint_permission_root<NeedsIo>(test_ctx)};
    [[maybe_unused]] auto lease = pool.try_upgrade(eff::testing::test().nondet);
    return 0;
}
