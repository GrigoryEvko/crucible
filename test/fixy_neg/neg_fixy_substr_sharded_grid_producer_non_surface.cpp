// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-12 negative fixture #1/8:
// `fixy::substr::sharded_grid::
//   mint_sharded_grid_producer<Grid, I>(grid, perm)`
// rejects when Grid is NOT a ShardedGridSessionSurface.
//
// `int` lacks the ShardedGridSessionSurface concept's required
// nested types (value_type, user_tag, ProducerHandle<I>,
// ConsumerHandle<J>) and the producer<I>()/consumer<J>() factory
// members.  The is_sharded_grid_session_surface trait
// specializes for PermissionedShardedGrid<...> only — anything
// else defaults to std::false_type.
//
// Distinct from fixture #2 (producer_wrong_perm): #1 exercises
// the ShardedGridSessionSurface concept gate on the first (Grid)
// parameter; #2 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// Expected diagnostic: "ShardedGridSessionSurface" /
// "constraints not satisfied" / "no matching function" /
// "mint_sharded_grid_producer".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fsg = ::crucible::fixy::substr::sharded_grid;
namespace saf = ::crucible::safety;

struct producer_tag_placeholder {};

int main() {
    int not_a_grid = 0;
    auto perm = saf::mint_permission_root<producer_tag_placeholder>();

    auto bad = fsg::mint_sharded_grid_producer<int, 0>(
        not_a_grid, std::move(perm));
    (void)bad;
    return 0;
}
