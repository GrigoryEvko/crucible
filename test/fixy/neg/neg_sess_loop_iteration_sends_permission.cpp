// One iteration of this loop sends the region that the handle held at the
// loop entry, and receives nothing back.  The next iteration would start
// with a different permission set.  The gate of the permissioned mint
// walks the whole protocol from the set of the tokens, so it refuses the
// protocol before the step to the Continue is compiled.

#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace loop_iteration_fixture {
struct Region {
    using permission_row = eff::Row<>;
};
struct Wire {};
using Token = perm::Permission<Region>;
using Forever = s::Loop<s::Send<Token, s::Continue>>;
}  // namespace loop_iteration_fixture

int main() {
    using namespace loop_iteration_fixture;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto [head, hold] = s::mint_permissioned_session<Forever>(ctx, Wire{}, perm::mint_permission_root<Region>());
    std::move(head).detach(s::detach_reason::InfiniteLoopProtocol{});
    static_cast<void>(std::move(hold).into_permissions());
    return 0;
}
