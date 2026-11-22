// A body of the read door of a SharedRead gives the View of the elements
// back, so the View would outlive the call, and with it the share of the
// guard.  The result of a read door holds no borrow, so the refused twin of
// the door binds the body and gives its reason.

#include <fixy/SharedRegion.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace eff = ::foundation::effects;

namespace {
struct Cache {
    using permission_row = eff::Row<>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Nondet>>;
}  // namespace

int main() {
    BgCtx const ctx{eff::testing::bg()};
    ::fixy::SharedRegion shared{
        ::fixy::mint_owned_region<int>(nullptr, 0uz, ::foundation::permissions::mint_permission_root<Cache>())};
    auto guard = shared.lend(ctx).expect("an idle region lends a share");
    auto read = ::fixy::mint_shared_read(ctx, guard, shared);
    ::foundation::core::View<int const> const kept =
        read.with_view([](::foundation::core::View<int const> run) noexcept { return run; });
    return static_cast<int>(kept.size());
}
