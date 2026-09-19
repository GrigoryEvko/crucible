// The fork hands each body one child token, and the children must be
// a declared partition of the parent.  No splits_into_pack<Whole, Left,
// Right> is declared here, so the fork's fit concept is not satisfied.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <utility>

namespace {
struct Whole {};
struct Left {};
struct Right {};

using BgCtx = ::foundation::effects::detail::exec_ctx_self_test::BgWitness;
}  // namespace

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = ::foundation::permissions::mint_permission_fork<Left, Right>(
        BgCtx{}, std::move(whole), [](::foundation::permissions::Permission<Left>, BgCtx const&) noexcept {},
        [](::foundation::permissions::Permission<Right>, BgCtx const&) noexcept {});
    return 0;
}
