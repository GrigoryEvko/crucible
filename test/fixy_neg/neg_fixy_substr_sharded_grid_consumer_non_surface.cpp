// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-12 negative fixture #3/8:
// `fixy::substr::sharded_grid::
//   mint_sharded_grid_consumer<Grid, J>(grid, perm)`
// rejects when Grid is NOT a ShardedGridSessionSurface.
//
// Mirrors fixture #1 (producer_non_surface) on the consumer
// side: proves the ShardedGridSessionSurface concept gate fires
// on the consumer mint INDEPENDENTLY of the producer-side
// instantiation.  `int` defaults to std::false_type for the
// is_sharded_grid_session_surface trait.
//
// Distinct from fixture #4 (consumer_wrong_perm): #3 exercises
// the ShardedGridSessionSurface concept gate on the first (Grid)
// parameter; #4 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// Expected diagnostic: "ShardedGridSessionSurface" /
// "constraints not satisfied" / "no matching function" /
// "mint_sharded_grid_consumer".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fsg = ::crucible::fixy::substr::sharded_grid;
namespace saf = ::crucible::safety;

struct consumer_tag_placeholder {};

int main() {
    int not_a_grid = 0;
    auto perm = saf::mint_permission_root<consumer_tag_placeholder>();

    auto bad = fsg::mint_sharded_grid_consumer<int, 0>(
        not_a_grid, std::move(perm));
    (void)bad;
    return 0;
}
