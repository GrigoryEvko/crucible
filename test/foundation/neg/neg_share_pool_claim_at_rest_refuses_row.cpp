// Negative-compile test. This file must fail to compile.
//
// The claim of the exclusive of a pool at rest takes no context, because a
// destructor calls it.  No context admits the row of the tag there, so only a
// tag whose row is empty has the door.  The region here does IO, so the claim
// at rest does not compile.
//
// Expected diagnostic: claim_exclusive_at_rest has an unsatisfied
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
    [[maybe_unused]] auto lease = pool.claim_exclusive_at_rest();
    return 0;
}
