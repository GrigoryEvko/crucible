// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-084 fixture #2 - a ShardedCalendarGrid producer session is
// Send-only for its shard.  ProducerHandle<S> must not expose recv
// through the generic substrate bridge.

#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>

#include <cstdint>
#include <tuple>
#include <utility>

namespace cc = crucible::concurrent;
namespace scgs = crucible::safety::proto::sharded_calendar_grid_session;

namespace {
struct Tag {};
struct Key {
    static std::uint64_t key(int value) noexcept {
        return static_cast<std::uint64_t>(value);
    }
};
using Grid = cc::PermissionedShardedCalendarGrid<int, 2, 8, 16, Key, 1ULL, Tag>;
}

int main() {
    Grid grid;
    auto whole = crucible::safety::mint_permission_root<
        cc::sharded_calendar_tag::Whole<Tag>>();
    auto perms = crucible::safety::mint_grid_permissions<
        cc::sharded_calendar_tag::Whole<Tag>, 2, 2>(std::move(whole));
    auto producer = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto psh = cc::mint_substrate_session<Grid, cc::ShardId<0>,
                                          cc::Direction::Producer>(
        ::crucible::effects::HotFgCtx{}, producer);
    [[maybe_unused]] auto bad = std::move(psh).recv(scgs::blocking_pop);
    return 0;
}
