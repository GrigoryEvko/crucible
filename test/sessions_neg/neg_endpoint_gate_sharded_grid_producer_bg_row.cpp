// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_producer_session over a sharded grid whose payload carries the
// background row refuses the foreground context.  The send side of the
// protocol does not fit a context that admits no background row.

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/effects/_Computation.h>
#include <crucible/sessions/ShardedGridSession.h>

namespace eff = ::crucible::effects;
namespace ses = ::crucible::safety::proto::sharded_grid_session;

namespace {
struct Tag {};
using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using Grid = ::crucible::concurrent::PermissionedShardedGrid<BgInt, 2, 2, 16, Tag>;
}  // namespace

inline void mint_under_foreground(Grid::ProducerHandle<0>& handle) {
    auto session = ses::mint_producer_session<Grid, 0>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
