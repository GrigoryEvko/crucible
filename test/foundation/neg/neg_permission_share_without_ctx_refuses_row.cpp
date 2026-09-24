// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A share with no context names no scope that may read the region.
// That is sound only for a pure tag.  The token here was minted under a
// context that does IO, and the static_assert in the body of the share
// is the only check that compares its row when no context is passed.
//
// Expected diagnostic: the static_assert in mint_permission_share that
// asks for an empty row when no context is passed.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

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
    auto token = perm::mint_permission_root<NeedsIo>(test_ctx);
    [[maybe_unused]] auto share = perm::mint_permission_share(std::move(token));
    return 0;
}
