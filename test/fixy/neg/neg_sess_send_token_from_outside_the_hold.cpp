// Negative compile test.  This file must fail to compile.
//
// A permissioned session sends a Transferable whose token did not come from
// the slots of the handle.  The session opens with the token of Region, and
// the slots of the handle keep that token.  A second root token of Region
// comes from the same root site, with the brand of the slots, a hold of its own
// packs it into a Transferable, and the send takes that marker.  The step builds the marker
// of a send from the slots of the handle and takes only its value, so a
// marker that the caller built is refused.  Before the slots, the send read
// only the permission set, and this file compiled and sent the foreign
// token.
//
// Expected diagnostic: the deleted send of a marker, [Marker_From_The_Slots].

#include <fixy/Ctx.h>
#include <fixy/session/Mint.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_send_token_from_outside_the_hold_types {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct OneSlot {
    int sends = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
// The one root site of Region.  Each root of the site has one brand.
[[nodiscard]] inline auto mint_region_root() noexcept {
    return ::foundation::permissions::mint_permission_root<Region>();
}
using Message = s::Transferable<int, Region, ::foundation::brand::brand_of_t<decltype(mint_region_root())>>;
}  // namespace neg_sess_send_token_from_outside_the_hold_types

int main() {
    namespace s = ::fixy::session;
    namespace fp = ::foundation::permissions;
    using namespace neg_sess_send_token_from_outside_the_hold_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto sender = s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, OneSlot{}, mint_region_root());
    auto [foreign, rest] = s::mint_permission_hold(mint_region_root()).template pack<Region>(7);
    auto done = std::move(sender).send(std::move(foreign), [](OneSlot& slot, Message&) noexcept {
        ++slot.sends;
        return true;
    });
    static_cast<void>(std::move(rest).into_permissions());
    static_cast<void>(std::move(done).close());
    return 0;
}
