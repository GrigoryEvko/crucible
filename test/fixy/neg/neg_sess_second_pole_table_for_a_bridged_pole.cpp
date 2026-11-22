// Negative compile test.  This file must fail to compile.
//
// A unit plants a second pole table, in a probe region that the plugin
// rules admit.  The table knows the producer pole of a bridged channel and
// names a protocol of its own for it, which sends a long.  The consumer pole
// runs the protocol of the bridge, which receives an int, so the two ends of
// the channel would run protocols that are not duals.  The reader of the
// tables finds two tables that know the pole, and it stops the build.
//
// Expected diagnostic: two_pole_tables_know_one_pole, which has no constant
// definition.

#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/Quarantine.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <concepts>
#include <utility>

namespace second_table_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace second_table_fixture

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges a second pole table for one pole of a bridged channel")
namespace fixy::session::pole_tables::forged {
template <class Pole>
    requires std::same_as<Pole, second_table_fixture::Channel::ProducerHandle>
struct forged_protocols {
    using self_protocol = ::fixy::session::Loop<::fixy::session::Send<long, ::fixy::session::Continue>>;
    using peer_protocol = ::fixy::session::Loop<::fixy::session::Recv<long, ::fixy::session::Continue>>;
};
inline constexpr ::fixy::session::pole_table forged_table{.table = ^^forged_protocols};
inline constexpr ::fixy::session::transition::seal table_seal{.kind = ^^::fixy::session::pole_table, .count = 1};
}  // namespace fixy::session::pole_tables::forged
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    using namespace second_table_fixture;
    namespace channels = ::fixy::channels;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    auto producer = channels::mint_substrate_session<Channel, channels::Direction::Producer>(
        ctx, channel.producer(std::move(producer_perm)));
    std::move(producer).detach(::fixy::session::detach_reason::TestInstrumentation{});
    static_cast<void>(consumer_perm);
    return 0;
}
