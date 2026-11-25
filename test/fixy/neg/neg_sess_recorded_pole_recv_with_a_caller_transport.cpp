// Negative compile test.  This file must fail to compile.
//
// A recorded session over a pole of a channel steps through the read of the
// pole table of its channel, as the session that it wraps does.  The recorder
// refuses a transport of the caller on a pole, so a recorded pole moves the
// values of its channel in one way.  The endpoint runs over the frame handle
// of the consumer, so it opens in the body of the frame door.
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
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    s::SessionEventLog log;
    c::with_ends(
        channel, std::move(tokens.at(::foundation::core::index<0>())),
        std::move(tokens.at(::foundation::core::index<1>())), [&ctx, &log](auto producer, auto consumer) noexcept {
            (void)producer.try_push(7);
            std::move(producer).close();
            auto recorded = channels::mint_recording_endpoint(
                channels::mint_endpoint<Channel, channels::Direction::Consumer>(ctx, std::move(consumer)),
                s::mint_session_event_writer(eff::testing::test().alloc, log), s::RoleTagId{2}, s::RoleTagId{1});
            auto received =
                std::move(recorded).recv([](auto& handle) noexcept -> ::fixy::Option<int> { return handle.try_pop(); });
            (void)received;
        });
    return 0;
}
