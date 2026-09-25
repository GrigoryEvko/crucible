// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Sharded-grid session mint negative fixture 8 of 8:
// `safety::proto::sharded_grid_session::mint_consumer_session<
//      Grid, J, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to
// `typename Grid::template ConsumerHandle<J>&`.
//
// Mirrors fixture #6 (producer_session_wrong_handle) on the
// consumer side: proves the ConsumerHandle<J> reference binding
// fires INDEPENDENTLY of the producer-side instantiation.
// Carries the non-deducible consumer shard index `J`.
//
// Distinct from fixture #7 (consumer_session_non_ctx): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle<J> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_consumer_session'" / "cannot convert" / "ConsumerHandle"
// / "mint_consumer_session".

#include <crucible/concurrent/_PermissionedShardedGrid.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_ShardedGridSession.h>

namespace fsg = ::crucible::safety::proto::sharded_grid_session;
namespace conc = ::crucible::concurrent;
namespace eff = ::crucible::effects;

namespace neg_fixy_sg_consumer_session_wrong_handle {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}  // namespace neg_fixy_sg_consumer_session_wrong_handle

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fsg::mint_consumer_session<neg_fixy_sg_consumer_session_wrong_handle::Grid, 0>(ctx, not_a_handle);
    (void)bad;
    return 0;
}
