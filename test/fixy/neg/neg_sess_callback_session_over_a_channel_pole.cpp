// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A unit runs a callback session over the producer pole of an SPSC channel
// of int, with a protocol of its own that sends a long.  with_session names
// one protocol, and it cannot see the protocol of the other end of the
// channel.  So its gate refuses a Resource that reaches a pole of a channel,
// as the gate of mint_session does.  A pole enters a session only through
// the pole door, which takes the two protocols from the table of the channel.
//
// Expected diagnostic: no with_session admits the pole, because the Resource
// reaches a pole of a channel.

#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace callback_pole_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;

struct SendNothing {
    template <typename Head>
    auto operator()(Head head) const noexcept {
        return head;
    }
};
}  // namespace callback_pole_fixture

int main() {
    using namespace callback_pole_fixture;
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    auto back = s::with_session<s::End>(ctx, channel.producer(std::move(producer_perm)), SendNothing{});
    static_cast<void>(back);
    static_cast<void>(consumer_perm);
    return 0;
}
