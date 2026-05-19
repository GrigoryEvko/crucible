// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-12 negative fixture #4/8:
// `fixy::substr::sharded_grid::
//   mint_sharded_grid_consumer<Grid, J>(grid, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<grid_tag::Consumer<typename Grid::user_tag, J>>&&`.
//
// Mirrors fixture #2 (producer_wrong_perm) on the consumer side:
// `PermissionedShardedGrid<int, 2, 3, 8, UserTag>` is a known
// ShardedGridSessionSurface (it specializes the trait per
// ShardedGridSession.h:43).  The first parameter binds; the
// concept passes; the second parameter `int` cannot bind to
// `Permission<grid_tag::Consumer<UserTag, J>>&&`.
//
// Distinct from fixture #3 (consumer_non_surface): #3 exercises
// the ShardedGridSessionSurface concept gate on the first
// (Grid) parameter; #4 exercises the second (perm) parameter
// binding AFTER the concept gate succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_sharded_grid_consumer'" / "cannot convert" /
// "Permission" / "mint_sharded_grid_consumer".

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fsg  = ::crucible::fixy::substr::sharded_grid;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_sg_consumer_wrong_perm {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}

int main() {
    neg_fixy_sg_consumer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fsg::mint_sharded_grid_consumer<
        neg_fixy_sg_consumer_wrong_perm::Grid, 0>(grid, not_a_perm);
    (void)bad;
    return 0;
}
