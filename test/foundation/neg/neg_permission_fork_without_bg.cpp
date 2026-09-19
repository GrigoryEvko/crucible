// The spawning arm of the fork starts one thread per child, which is
// background work.  The partition is declared, but the foreground
// context's row has no Bg effect, so the fit concept is not satisfied.
// The inline arm would accept this context.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {};
struct Left {};
struct Right {};

using FgCtx = ::foundation::effects::detail::exec_ctx_self_test::FgWitness;
}  // namespace

namespace foundation::permissions {
template <>
struct splits_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct splits_into_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = ::foundation::permissions::mint_permission_fork<Left, Right>(
        FgCtx{}, std::move(whole), [](::foundation::permissions::Permission<Left>, FgCtx const&) noexcept {},
        [](::foundation::permissions::Permission<Right>, FgCtx const&) noexcept {});
    return 0;
}
