// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-11 negative fixture #1/8:
// `fixy::substr::sharded_calendar_grid::
//   mint_sharded_calendar_grid_producer<Grid, S>(grid, perm)`
// rejects when Grid is NOT a ShardedCalendarGridSessionSurface.
//
// `int` lacks the ShardedCalendarGridSessionSurface concept's
// required nested types (value_type, user_tag, shard_producer_tag<S>,
// shard_consumer_tag<S>, ProducerHandle<S>, ConsumerHandle<S>)
// and the producer<S>()/consumer<S>() factory members.  The
// is_sharded_calendar_grid_session_surface trait is_specialization
// for PermissionedShardedCalendarGrid<...> only — anything else
// defaults to std::false_type.
//
// Distinct from fixture #2 (wrong_perm): #1 exercises the
// ShardedCalendarGridSessionSurface concept gate on the first
// (Grid) parameter; #2 exercises the second (perm) parameter
// binding AFTER the concept gate succeeds.
//
// Expected diagnostic: "ShardedCalendarGridSessionSurface" /
// "constraints not satisfied" / "no matching function" /
// "mint_sharded_calendar_grid_producer".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fscal = ::crucible::fixy::substr::sharded_calendar_grid;
namespace saf   = ::crucible::safety;

struct shard_producer_tag_placeholder {};

int main() {
    int not_a_grid = 0;
    auto perm = saf::mint_permission_root<shard_producer_tag_placeholder>();

    auto bad = fscal::mint_sharded_calendar_grid_producer<int, 0>(
        not_a_grid, std::move(perm));
    (void)bad;
    return 0;
}
