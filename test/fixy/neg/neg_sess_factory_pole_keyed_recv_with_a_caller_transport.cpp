// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A session over the consumer pole of a bridged channel of Labelled values
// reads the label word of the keyed message with a read of a word of its
// own.  The pole table of the bridge moves the values of the channel and no
// label word, so a keyed step over a pole has no word to read through the
// table, and the step refuses a read of the caller.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the recv of the handle
// factory.

#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/session/Projection.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace factory_pole_keyed_recv_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
namespace eff = ::foundation::effects;
struct Tag {};
struct Ping {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Message = ::fixy::session::Labelled<Ping, int>;
using Channel = c::channel_t<Message, 8, decltype(channel_root())>;
}  // namespace factory_pole_keyed_recv_fixture

int main() {
    using namespace factory_pole_keyed_recv_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    (void)producer_perm;
    auto head = channels::mint_substrate_session<Channel, channels::Direction::Consumer>(
        ctx, channel.consumer(std::move(consumer_perm)));
    auto next = ::fixy::session::HandleFactory::recv(
        std::move(head), [](auto&) noexcept -> ::fixy::Option<std::size_t> { return ::fixy::Option<std::size_t>{}; });
    std::move(next).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
