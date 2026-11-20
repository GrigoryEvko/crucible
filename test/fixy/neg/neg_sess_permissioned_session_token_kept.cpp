// The permissioned mint consumes each token.  A token passed as an lvalue
// would stay with the caller, and the set of the session would then name
// a region that a second holder also holds.  A Permission cannot be
// copied, so the call is refused at the copy of the token.

#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace token_kept_fixture {
struct Region {
    using permission_row = eff::Row<>;
};
struct Wire {};
// The one root site of Region, so the protocol names the brand of its root.
[[nodiscard]] inline auto mint_region_root() noexcept { return perm::mint_permission_root<Region>(); }
using SendsRegion = s::Send<decltype(mint_region_root()), s::End>;
}  // namespace token_kept_fixture

int main() {
    using namespace token_kept_fixture;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto token = mint_region_root();
    auto head = s::mint_permissioned_session<SendsRegion>(ctx, Wire{}, token);
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    ::foundation::linear::drop(std::move(token));
    return 0;
}
