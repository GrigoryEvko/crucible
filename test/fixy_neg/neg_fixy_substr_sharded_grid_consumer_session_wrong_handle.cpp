// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-12 negative fixture #8/8:
// `fixy::substr::sharded_grid::mint_consumer_session<
//      Grid, J, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to
// `typename Grid::template ConsumerHandle<J>&`.
//
// Mirrors fixture #6 (producer_session_wrong_handle) on the
// consumer side: proves the ConsumerHandle<J> reference binding
// is preserved through the using-decl INDEPENDENTLY of the
// producer-side instantiation.  Carries the non-deducible
// consumer shard index `J`.
//
// Distinct from fixture #7 (consumer_session_non_ctx): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle<J> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_consumer_session'" / "cannot convert" / "ConsumerHandle"
// / "mint_consumer_session".

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fsg  = ::crucible::fixy::substr::sharded_grid;
namespace conc = ::crucible::concurrent;
namespace eff  = ::crucible::effects;

namespace neg_fixy_sg_consumer_session_wrong_handle {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fsg::mint_consumer_session<
        neg_fixy_sg_consumer_session_wrong_handle::Grid, 0>(
            ctx, not_a_handle);
    (void)bad;
    return 0;
}
