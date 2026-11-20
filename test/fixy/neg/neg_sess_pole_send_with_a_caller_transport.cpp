// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A session over a pole of a channel sends through the write that the pole
// table of its channel names, in the context of the step.  A transport of
// the caller would move the values of one channel in a second way, so the
// send of a pole takes no transport.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the send of the handle.

#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace pole_send_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
namespace eff = ::foundation::effects;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace pole_send_fixture

int main() {
    using namespace pole_send_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    (void)consumer_perm;
    auto head = channels::mint_substrate_session<Channel, channels::Direction::Producer>(
        ctx, channel.producer(std::move(producer_perm)));
    auto next =
        std::move(head).send(7, [](auto& handle, int& value) noexcept -> bool { return handle.try_push(value); });
    std::move(next).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
