// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The write borrow door takes its token by rvalue reference, keeps the
// token in its own frame while the body runs, and hands the token back.
// A named token stays with the caller, so a body could move it or end it
// through a capture while the view says that the body holds the region.
// The deleted twin of the door refuses the named token.
//
// Expected diagnostic: a call of the deleted twin, with its reason.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

using FgCtx = ::foundation::effects::detail::ctx_witnesses::FgWitness;
}  // namespace

int main() {
    auto token = ::foundation::permissions::mint_permission_root<Region>();
    [[maybe_unused]] auto back = ::foundation::permissions::with_write_view(
        ::foundation::effects::testing::foreground(), token, [](auto const& /*view*/, FgCtx const&) noexcept {});
    return 0;
}
