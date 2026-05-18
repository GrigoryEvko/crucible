// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-082 fixture #2 — an indexed ShardedGrid producer session is
// Send-only.  The generic substrate bridge must not expose recv on a
// ProducerHandle<I>.

#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/sessions/ShardedGridSession.h>

#include <tuple>
#include <utility>

namespace cc = crucible::concurrent;
namespace sgs = crucible::safety::proto::sharded_grid_session;

namespace {
struct Tag {};
using Grid = cc::PermissionedShardedGrid<int, 2, 2, 32, Tag>;
}

int main() {
    Grid grid;
    auto whole = crucible::safety::mint_permission_root<cc::grid_tag::Whole<Tag>>();
    auto perms = crucible::safety::mint_grid_permissions<cc::grid_tag::Whole<Tag>, 2, 2>(
        std::move(whole));
    auto producer = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto psh = cc::mint_substrate_session<Grid, cc::ShardId<0>,
                                          cc::Direction::Producer>(
        ::crucible::effects::HotFgCtx{}, producer);
    [[maybe_unused]] auto bad = std::move(psh).recv(sgs::blocking_pop);
    return 0;
}
