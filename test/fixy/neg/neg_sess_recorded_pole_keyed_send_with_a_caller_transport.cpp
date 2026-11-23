// Negative compile test.  This file must fail to compile.
//
// A unit records a session over the producer pole of a bridged channel of
// Labelled values, and it sends the label word of the keyed message with a
// write of a word of its own.  The pole table moves no label word, so the
// recorded handle refuses the write of the caller, as the handle that it
// wraps does.  The session runs over the frame handle of the producer, so it
// opens in the body of the frame door.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the send of the recorded
// handle.

#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Recording.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace recorded_pole_keyed_send_fixture {
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
}  // namespace recorded_pole_keyed_send_fixture

int main() {
    using namespace recorded_pole_keyed_send_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    s::SessionEventLog log;
    c::with_producer(
        channel, std::move(tokens.at(::foundation::core::index<0>())), [&ctx, &log](auto producer) noexcept {
            auto recorded = s::mint_recorded_session(
                channels::mint_substrate_session<Channel, channels::Direction::Producer>(ctx, std::move(producer)),
                s::mint_session_event_writer(log), s::RoleTagId{1}, s::RoleTagId{2});
            auto next = std::move(recorded).send([](auto&, std::size_t) noexcept -> bool { return true; });
            std::move(next).detach(s::detach_reason::TestInstrumentation{});
        });
    return 0;
}
