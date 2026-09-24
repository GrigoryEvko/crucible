// The handle holds the region.  The second arm lends it and reaches End
// with the loan open.  The program selects only the first arm, which ends
// with the region owned, so no compiled step sees the open loan.  The
// gate of the permissioned mint visits every arm, so it refuses the
// protocol.

#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace unselected_arm_fixture {
struct Region {
    using permission_row = eff::Row<>;
};
struct Wire {};
using Proto = s::Select<s::End, s::Send<s::Borrowed<int, Region>, s::End>>;
}  // namespace unselected_arm_fixture

int main() {
    using namespace unselected_arm_fixture;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto [head, hold] = s::mint_permissioned_session<Proto>(ctx, Wire{}, perm::mint_permission_root<Region>());
    auto at_end = std::move(head).select<0>([](Wire&, std::size_t) noexcept { return true; });
    static_cast<void>(std::move(at_end).close());
    static_cast<void>(std::move(hold).into_permissions());
    return 0;
}
