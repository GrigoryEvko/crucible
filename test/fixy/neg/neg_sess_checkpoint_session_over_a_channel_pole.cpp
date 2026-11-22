// Negative compile test.  This file must fail to compile.
//
// A unit mints a checkpoint session over the producer pole of an SPSC
// channel.  The mint names the protocol of the peer, but it mints one end,
// and the consumer pole of the channel is minted apart, with a protocol that
// this mint cannot see.  So its gate refuses a Resource that reaches a pole
// of a channel, as the gate of mint_session does.
//
// Expected diagnostic: no mint_checkpoint_session admits the pole, because
// the Resource reaches a pole of a channel.

#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/Checkpoint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace checkpoint_pole_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
using Decider = ::fixy::session::Select<::fixy::session::Commit<::fixy::session::Send<long, ::fixy::session::End>>,
                                        ::fixy::session::Abort>;
}  // namespace checkpoint_pole_fixture

int main() {
    using namespace checkpoint_pole_fixture;
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    auto handle =
        s::mint_checkpoint_session<Decider, s::dual_of_t<Decider>>(ctx, channel.producer(std::move(producer_perm)));
    static_cast<void>(handle);
    static_cast<void>(consumer_perm);
    return 0;
}
