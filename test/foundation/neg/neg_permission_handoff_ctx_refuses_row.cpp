// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// permission_handoff moves a token into a scope under that scope's
// context, so the context must admit the row of the tag.  The token here
// was minted under a context that does IO, and the foreground context
// admits no IO, so the handoff refuses the call at its own door, not
// inside the admission it forwards to.
//
// Expected diagnostic: permission_handoff has no viable candidate, and
// the note names CtxAdmitsPermission.

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
    [[maybe_unused]] auto handed = perm::permission_handoff(eff::testing::foreground(), std::move(token));
    return 0;
}
