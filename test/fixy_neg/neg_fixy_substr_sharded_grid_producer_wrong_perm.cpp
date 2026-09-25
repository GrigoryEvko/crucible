// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Sharded-grid session mint negative fixture 2 of 8:
// `safety::proto::sharded_grid_session::
//   mint_sharded_grid_producer<Grid, I>(grid, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<grid_tag::Producer<typename Grid::user_tag, I>>&&`.
//
// `PermissionedShardedGrid<int, 2, 3, 8, UserTag>` is a known
// ShardedGridSessionSurface (ShardedGridSession.h specializes the
// trait for it).  The first parameter binds; the
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
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/ShardedGridSession.h>

namespace fsg = ::crucible::safety::proto::sharded_grid_session;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_sg_producer_wrong_perm {
struct UserTag {};
using Grid = conc::PermissionedShardedGrid<int, 2, 3, 8, UserTag>;
}  // namespace neg_fixy_sg_producer_wrong_perm

int main() {
    neg_fixy_sg_producer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fsg::mint_sharded_grid_producer<neg_fixy_sg_producer_wrong_perm::Grid, 0>(grid, not_a_perm);
    (void)bad;
    return 0;
}
