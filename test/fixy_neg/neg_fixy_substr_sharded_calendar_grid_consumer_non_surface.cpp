// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Sharded calendar-grid session mint negative fixture 3 of 8:
// `safety::proto::sharded_calendar_grid_session::
//   mint_sharded_calendar_grid_consumer<Grid, S>(grid, perm)`
// rejects when Grid is NOT a ShardedCalendarGridSessionSurface.
//
// Mirrors fixture #1 (producer_non_surface) on the consumer side:
// `int` lacks the required nested types and factory members; the
// is_sharded_calendar_grid_session_surface trait defaults to
// std::false_type for non-PermissionedShardedCalendarGrid types.
//
// Distinct from fixture #4 (consumer_wrong_perm): #3 exercises
// the ShardedCalendarGridSessionSurface concept gate on the
// first (Grid) parameter; #4 exercises the second (perm)
// parameter binding AFTER the concept gate succeeds.
//
// Expected diagnostic: "ShardedCalendarGridSessionSurface" /
// "constraints not satisfied" / "no matching function" /
// "mint_sharded_calendar_grid_consumer".

#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>

namespace fscal = ::crucible::safety::proto::sharded_calendar_grid_session;
namespace saf = ::crucible::safety;

struct shard_consumer_tag_placeholder {};

int main() {
    int not_a_grid = 0;
    auto perm = saf::mint_permission_root<shard_consumer_tag_placeholder>();

    auto bad = fscal::mint_sharded_calendar_grid_consumer<int, 0>(not_a_grid, std::move(perm));
    (void)bad;
    return 0;
}
