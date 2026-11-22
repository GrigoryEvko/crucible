// Negative compile test.  This file must fail to compile.
//
// The receive of the handle factory is the step that the receive of a
// handle calls.  A session over a pole of a channel receives through the
// read that the pole table of its channel names, so the factory refuses a
// transport of the caller on a pole, as the receive of the handle does.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the receive of the
// handle factory.

#include <fixy/channels/SubstrateSessionBridge.h>

#include <fixy/Core.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace factory_pole_recv_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
namespace eff = ::foundation::effects;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace factory_pole_recv_fixture

int main() {
    using namespace factory_pole_recv_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    auto producer = channel.producer(std::move(producer_perm));
    (void)producer.try_push(7);
    auto head = channels::mint_substrate_session<Channel, channels::Direction::Consumer>(
        ctx, channel.consumer(std::move(consumer_perm)));
    auto [value, next] = ::fixy::session::HandleFactory::recv(
        std::move(head), [](auto& handle) noexcept -> ::fixy::Option<int> { return handle.try_pop(); });
    (void)value;
    std::move(next).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
