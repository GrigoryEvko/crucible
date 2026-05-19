// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-12 negative fixture #6/8:
// `fixy::substr::sharded_grid::mint_producer_session<
//      Grid, I, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to
// `typename Grid::template ProducerHandle<I>&`.
//
// Carries the non-deducible producer shard index `I`.  Proves
// the ProducerHandle<I> reference binding is preserved through
// the using-decl re-export INDEPENDENTLY of the consumer-side
// instantiation.
//
// Distinct from fixture #5 (producer_session_non_ctx): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the ProducerHandle<I> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_producer_session'" / "cannot convert" / "ProducerHandle"
// / "mint_producer_session".

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fsg  = ::crucible::fixy::substr::sharded_grid;
namespace conc = ::crucible::concurrent;
namespace eff  = ::crucible::effects;

namespace neg_fixy_sg_producer_session_wrong_handle {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fsg::mint_producer_session<
        neg_fixy_sg_producer_session_wrong_handle::Grid, 0>(
            ctx, not_a_handle);
    (void)bad;
    return 0;
}
