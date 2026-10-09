// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The write borrow door lends a view of a region under the context of the
// caller, so the context must admit the row of the tag, as each mint of
// the tag asks.  The region here does IO, and the foreground context
// admits no IO.  The door refuses the call and names the row check.
//
// Expected diagnostic: no candidate of with_write_view, with a note on the
// row check of the tag.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <utility>

namespace {
struct IoRegion {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

using TestCtx = ::foundation::effects::detail::ctx_witnesses::TestWitnessCtx;
using FgCtx = ::foundation::effects::detail::ctx_witnesses::FgWitness;
}  // namespace

int main() {
    auto token =
        ::foundation::permissions::mint_permission_root<IoRegion>(TestCtx{::foundation::effects::testing::test()});
    [[maybe_unused]] auto back =
        ::foundation::permissions::with_write_view(::foundation::effects::testing::foreground(), std::move(token),
                                                   [](auto const& /*view*/, FgCtx const&) noexcept {});
    return 0;
}
