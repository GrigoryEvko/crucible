// The permissioned mint consumes each token.  A token passed as an lvalue
// would stay with the caller, and the set of the session would then name
// a region that a second holder also holds.  A Permission cannot be
// copied, so the call is refused at the copy of the token.

#include <fixy/session/Handle.h>

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
using SendsRegion = s::Send<perm::Permission<Region>, s::End>;
}  // namespace token_kept_fixture

int main() {
    using namespace token_kept_fixture;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto token = perm::mint_permission_root<Region>();
    auto [head, hold] = s::mint_permissioned_session<SendsRegion>(ctx, Wire{}, token);
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    static_cast<void>(std::move(hold).into_permissions());
    perm::permission_drop(std::move(token));
    return 0;
}
