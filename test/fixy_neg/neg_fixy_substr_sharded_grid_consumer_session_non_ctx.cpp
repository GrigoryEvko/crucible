// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-12 negative fixture #7/8:
// `fixy::substr::sharded_grid::mint_consumer_session<
//      Grid, J, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// Mirrors fixture #5 (producer_session_non_ctx) on the consumer
// side: proves the IsExecCtx prerequisite is preserved through
// the using-decl INDEPENDENTLY of the producer-side
// instantiation.  Carries the non-deducible consumer shard
// index `J` (std::size_t).
//
// Distinct from fixture #8 (consumer_session_wrong_handle): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle<J> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_consumer_session".

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/fixy/Substr.h>

namespace fsg  = ::crucible::fixy::substr::sharded_grid;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_sg_consumer_session_non_ctx {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_sg_consumer_session_non_ctx::Grid::template ConsumerHandle<0>*
        handle = nullptr;

    auto bad = fsg::mint_consumer_session<
        neg_fixy_sg_consumer_session_non_ctx::Grid, 0>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
