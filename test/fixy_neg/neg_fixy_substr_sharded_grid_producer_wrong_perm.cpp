// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-12 negative fixture #2/8:
// `fixy::substr::sharded_grid::
//   mint_sharded_grid_producer<Grid, I>(grid, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<grid_tag::Producer<typename Grid::user_tag, I>>&&`.
//
// `PermissionedShardedGrid<int, 2, 3, 8, UserTag>` is a known
// ShardedGridSessionSurface (it specializes the trait per
// ShardedGridSession.h:43).  The first parameter binds; the
// concept passes; the second parameter `int` cannot bind to
// `Permission<grid_tag::Producer<UserTag, I>>&&`.
//
// Distinct from fixture #1 (producer_non_surface): #1 exercises
// the ShardedGridSessionSurface concept gate on the first
// (Grid) parameter; #2 exercises the second (perm) parameter
// binding AFTER the concept gate succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_sharded_grid_producer'" / "cannot convert" /
// "Permission" / "mint_sharded_grid_producer".

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fsg  = ::crucible::fixy::substr::sharded_grid;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_sg_producer_wrong_perm {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}

int main() {
    neg_fixy_sg_producer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fsg::mint_sharded_grid_producer<
        neg_fixy_sg_producer_wrong_perm::Grid, 0>(grid, not_a_perm);
    (void)bad;
    return 0;
}
