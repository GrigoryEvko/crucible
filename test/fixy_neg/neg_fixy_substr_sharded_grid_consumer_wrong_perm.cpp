// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Sharded-grid session mint negative fixture 4 of 8:
// `safety::proto::sharded_grid_session::
//   mint_sharded_grid_consumer<Grid, J>(grid, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<grid_tag::Consumer<typename Grid::user_tag, J>>&&`.
//
// Mirrors fixture #2 (producer_wrong_perm) on the consumer side:
// `PermissionedShardedGrid<int, 2, 3, 8, UserTag>` is a known
// ShardedGridSessionSurface (ShardedGridSession.h specializes the
// trait for it).  The first parameter binds; the
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

#include <crucible/concurrent/_PermissionedShardedGrid.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_ShardedGridSession.h>

namespace fsg = ::crucible::safety::proto::sharded_grid_session;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_sg_consumer_wrong_perm {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}  // namespace neg_fixy_sg_consumer_wrong_perm

int main() {
    neg_fixy_sg_consumer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fsg::mint_sharded_grid_consumer<neg_fixy_sg_consumer_wrong_perm::Grid, 0>(grid, not_a_perm);
    (void)bad;
    return 0;
}
