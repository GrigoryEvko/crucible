// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Sharded calendar-grid session mint negative fixture 4 of 8:
// `safety::proto::sharded_calendar_grid_session::
//   mint_sharded_calendar_grid_consumer<Grid, S>(grid, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<typename Grid::template shard_consumer_tag<S>>&&`.
//
// Mirrors fixture #2 (producer_wrong_perm) on the consumer side:
// `PermissionedShardedCalendarGrid<Job, 2, 8, 16, Key,
//  1'000'000ULL, UserTag>` is a known
// ShardedCalendarGridSessionSurface (ShardedCalendarGridSession.h
// specializes the trait for it).  The first parameter
// binds; the concept passes; the second parameter `int` cannot
// bind to `Permission<shard_consumer_tag<S>>&&`.
//
// Distinct from fixture #3 (consumer_non_surface): #3 exercises
// the ShardedCalendarGridSessionSurface concept gate on the
// first (Grid) parameter; #4 exercises the second (perm)
// parameter binding AFTER the concept gate succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_sharded_calendar_grid_consumer'" / "cannot convert" /
// "Permission" / "mint_sharded_calendar_grid_consumer".

#include <crucible/concurrent/_PermissionedShardedCalendarGrid.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_ShardedCalendarGridSession.h>

namespace fscal = ::crucible::safety::proto::sharded_calendar_grid_session;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_scal_consumer_wrong_perm {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept { return job.deadline_ns; }
};
using Grid = conc::PermissionedShardedCalendarGrid<Job, 2, 8, 16, Key, 1000000ULL, UserTag>;
}  // namespace neg_fixy_scal_consumer_wrong_perm

int main() {
    neg_fixy_scal_consumer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fscal::mint_sharded_calendar_grid_consumer<neg_fixy_scal_consumer_wrong_perm::Grid, 0>(grid, not_a_perm);
    (void)bad;
    return 0;
}
