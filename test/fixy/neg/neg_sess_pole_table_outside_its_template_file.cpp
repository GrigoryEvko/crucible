// Negative compile test.  This file must fail to compile.
//
// A unit plants a pole table in a probe region that the plugin rules admit.
// The table names the template of the bridge, which stands in
// fixy/channels/SubstrateSessionBridge.h, so the registration stands outside
// the file of its template.  A table that a unit can register for a template
// of another header would give the poles of that header a table in some
// units and none in others.  The reader of the tables stops the build.
//
// Expected diagnostic: a_pole_table_stands_outside_the_file_of_its_template,
// which has no constant definition.

#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/Quarantine.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace borrowed_table_fixture {
namespace c = ::fixy::concurrent;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace borrowed_table_fixture

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture registers a pole table outside the file of its template")
namespace fixy::session::pole_tables::borrowed {
inline constexpr ::fixy::session::pole_table borrowed_table{.table = ^^::fixy::channels::bridged_pole_protocols};
inline constexpr ::fixy::session::transition::seal table_seal{.kind = ^^::fixy::session::pole_table, .count = 1};
}  // namespace fixy::session::pole_tables::borrowed
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    using namespace borrowed_table_fixture;
    namespace channels = ::fixy::channels;
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    c::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())), [&ctx](auto producer) noexcept {
        auto head = channels::mint_substrate_session<Channel, channels::Direction::Producer>(ctx, std::move(producer));
        std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    });
    return 0;
}
