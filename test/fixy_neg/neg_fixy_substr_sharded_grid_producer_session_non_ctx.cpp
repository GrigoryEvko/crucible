// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Sharded-grid session mint negative fixture 5 of 8:
// `safety::proto::sharded_grid_session::mint_producer_session<
//      Grid, I, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// Carries the non-deducible producer shard index `I`
// (std::size_t).  Proves the IsExecCtx prerequisite is enforced
// on the sharded grid mint INDEPENDENTLY of the
// ShardedCalendarGrid family.
//
// Distinct from fixture #6 (producer_session_wrong_handle): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the ProducerHandle<I> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_producer_session".

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/sessions/ShardedGridSession.h>

namespace fsg = ::crucible::safety::proto::sharded_grid_session;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_sg_producer_session_non_ctx {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}  // namespace neg_fixy_sg_producer_session_non_ctx

int main() {
    int not_a_ctx = 0;
    neg_fixy_sg_producer_session_non_ctx::Grid::template ProducerHandle<0>* handle = nullptr;

    auto bad = fsg::mint_producer_session<neg_fixy_sg_producer_session_non_ctx::Grid, 0>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
