// The permissioned mint walks the whole protocol from the set of the
// tokens that it consumes.  This protocol sends the region and then sends
// it again, so the second send moves a token that the set no longer
// holds.  The gate refuses the protocol before the first send runs.

#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace flow_open_fixture {
struct Region {
    using permission_row = eff::Row<>;
};
struct Wire {};
using Token = perm::Permission<Region>;
using SendsTwice = s::Send<Token, s::Send<Token, s::End>>;
}  // namespace flow_open_fixture

int main() {
    using namespace flow_open_fixture;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto [head, hold] = s::mint_permissioned_session<SendsTwice>(ctx, Wire{}, perm::mint_permission_root<Region>());
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    static_cast<void>(std::move(hold).into_permissions());
    return 0;
}
