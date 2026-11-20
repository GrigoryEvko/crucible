// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A recorded session over a pole of a channel steps through the read of the
// pole table of its channel, as the session that it wraps does.  The recorder
// refuses a transport of the caller on a pole, so a recorded pole moves the
// values of its channel in one way.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the receive of the
// recorder.

#include <fixy/channels/EndpointMint.h>
#include <fixy/session/EventLog.h>

#include <fixy/Core.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace recorded_pole_recv_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
namespace eff = ::foundation::effects;
namespace s = ::fixy::session;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace recorded_pole_recv_fixture

int main() {
    using namespace recorded_pole_recv_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    auto producer = channel.producer(std::move(producer_perm));
    (void)producer.try_push(7);
    s::SessionEventLog log;
    auto recorded =
        channels::mint_recording_endpoint(channels::mint_endpoint<Channel, channels::Direction::Consumer>(
                                              ctx, channel.consumer(std::move(consumer_perm))),
                                          s::mint_session_event_writer(log), s::RoleTagId{2}, s::RoleTagId{1});
    auto [value, next] =
        std::move(recorded).recv([](auto& handle) noexcept -> ::fixy::Option<int> { return handle.try_pop(); });
    (void)value;
    std::move(next).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
