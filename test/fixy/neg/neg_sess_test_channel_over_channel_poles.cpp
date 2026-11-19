// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A test mints a test channel over the two poles of an SPSC channel of int,
// with a protocol of its own that sends a long.  mint_test_channel names the
// protocol, and the table of the SPSC channel names another one.  So its
// gate refuses a Resource that reaches a pole of a channel, as the gate of
// mint_session does.  A pole enters a session only through the pole door,
// which takes the two protocols from the table of the channel.
//
// Expected diagnostic: no mint_test_channel admits the poles, because each
// Resource reaches a pole of a channel.

#include <fixy/Ctx.h>
#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace test_channel_pole_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace test_channel_pole_fixture

int main() {
    using namespace test_channel_pole_fixture;
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    const ::fixy::TestRunnerCtx ctx{eff::testing::test()};
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    auto [sender, receiver] = s::mint_test_channel<s::Loop<s::Send<long, s::Continue>>>(
        ctx, channel.producer(std::move(producer_perm)), channel.consumer(std::move(consumer_perm)));
    std::move(sender).detach(s::detach_reason::TestInstrumentation{});
    std::move(receiver).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
