// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-12 negative fixture #5/8:
// `fixy::substr::sharded_grid::mint_producer_session<
//      Grid, I, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// Carries the non-deducible producer shard index `I`
// (std::size_t) — preserved through the using-decl re-export.
// Proves the IsExecCtx prerequisite is enforced on the sharded
// grid mint INDEPENDENTLY of the ShardedCalendarGrid family.
//
// Distinct from fixture #6 (producer_session_wrong_handle): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the ProducerHandle<I> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_producer_session".

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/fixy/Substr.h>

namespace fsg  = ::crucible::fixy::substr::sharded_grid;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_sg_producer_session_non_ctx {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_sg_producer_session_non_ctx::Grid::template ProducerHandle<0>*
        handle = nullptr;

    auto bad = fsg::mint_producer_session<
        neg_fixy_sg_producer_session_non_ctx::Grid, 0>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
