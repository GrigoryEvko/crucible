// Negative compile test.  This file must fail to compile.
//
// A unit gives the producer end of an SPSC channel a protocol of its own, in
// a probe region that the rule specialization-home admits.  The two ends of
// the channel would then run protocols that are not duals: the producer sends
// a long, and the consumer receives an int.  The protocol of each end is an
// alias over the protocol of the producer end, and no unit can specialize an
// alias or name a protocol of one end alone.
//
// Expected diagnostic: default_proto_for is no template of the bridge.

#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/Quarantine.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace substrate_one_end_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace substrate_one_end_fixture

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges the protocol of one end of a channel")
namespace fixy::channels {
template <>
struct default_proto_for<substrate_one_end_fixture::Channel, Direction::Producer> {
    using type = ::fixy::session::Loop<::fixy::session::Send<long, ::fixy::session::Continue>>;
};
}  // namespace fixy::channels
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    using namespace substrate_one_end_fixture;
    namespace channels = ::fixy::channels;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    c::with_ends(channel, std::move(tokens.at(::foundation::core::index<0>())),
                 std::move(tokens.at(::foundation::core::index<1>())), [&ctx](auto producer, auto consumer) noexcept {
                     auto sender = channels::mint_substrate_session<Channel, channels::Direction::Producer>(
                         ctx, std::move(producer));
                     auto receiver = channels::mint_substrate_session<Channel, channels::Direction::Consumer>(
                         ctx, std::move(consumer));
                     std::move(sender).detach(::fixy::session::detach_reason::TestInstrumentation{});
                     std::move(receiver).detach(::fixy::session::detach_reason::TestInstrumentation{});
                 });
    return 0;
}
