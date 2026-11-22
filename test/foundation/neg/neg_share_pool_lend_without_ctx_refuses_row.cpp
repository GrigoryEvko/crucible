// Negative-compile test. This file must fail to compile.
//
// The schedule decides whether a lend gives a share, so a lend takes a
// context that holds Nondet and admits the row of the tag.  A lend with no
// context does not compile, and the region here also does IO.
//
// Expected diagnostic: lend has no candidate with no argument.

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
    [[maybe_unused]] auto share = pool.lend();
    return 0;
}
