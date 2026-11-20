// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A unit records a session over the consumer pole of a bridged channel of
// Labelled values, and it reads the label word of the keyed message with a
// read of a word of its own.  The pole table moves no label word, so the
// recorded handle refuses the read of the caller, as the handle that it
// wraps does.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the recv of the recorded
// handle.

#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Recording.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace recorded_pole_keyed_recv_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
namespace eff = ::foundation::effects;
namespace s = ::fixy::session;
struct Tag {};
struct Ping {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Message = s::Labelled<Ping, int>;
using Channel = c::channel_t<Message, 8, decltype(channel_root())>;
}  // namespace recorded_pole_keyed_recv_fixture

int main() {
    using namespace recorded_pole_keyed_recv_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    (void)producer_perm;
    s::SessionEventLog log;
    auto recorded = s::mint_recorded_session(channels::mint_substrate_session<Channel, channels::Direction::Consumer>(
                                                 ctx, channel.consumer(std::move(consumer_perm))),
                                             s::mint_session_event_writer(log), s::RoleTagId{1}, s::RoleTagId{2});
    auto next = std::move(recorded).recv(
        [](auto&) noexcept -> ::fixy::Option<std::size_t> { return ::fixy::Option<std::size_t>{}; });
    std::move(next).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
