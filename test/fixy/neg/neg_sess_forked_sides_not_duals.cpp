// Negative compile test.  This file must fail to compile.
//
// A unit calls the fork member of the session door directly, with two sides
// that both send first.  No side relation of the unit admits the pair, and
// the two sides are not duals, so each side would wait for a message that
// the other never sends.  The door asks the relation of the two sides in the
// one member that opens each end, and the gate of the fork asks it too.
//
// Expected diagnostic: no fork_channel admits the two sides, because
// ChannelSidesAgree does not hold.

#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

struct Whole {
    using permission_row = eff::Row<>;
};
struct Left {
    using permission_row = eff::Row<>;
};
struct Right {
    using permission_row = eff::Row<>;
};

struct Msg {};
struct Wire {};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using Proto = s::Send<Msg, s::End>;
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    const BgCtx ctx{eff::testing::bg()};
    const auto send_body = [](auto head, auto const& /*view*/, BgCtx const&) noexcept {
        return std::move(head).send(Msg{}, [](Wire&, Msg&) noexcept { return true; });
    };
    auto back = s::SessionMintDoor::fork_channel<Proto, Proto, Left, Right, s::DefaultAbandonmentPolicy>(
        ctx, perm::mint_permission_root<Whole>(), Wire{}, Wire{}, send_body, send_body);
    ::foundation::linear::drop(std::move(back));
    return 0;
}
