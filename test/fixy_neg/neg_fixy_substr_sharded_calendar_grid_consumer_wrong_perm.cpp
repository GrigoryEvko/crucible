// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-11 negative fixture #4/8:
// `fixy::substr::sharded_calendar_grid::
//   mint_sharded_calendar_grid_consumer<Grid, S>(grid, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<typename Grid::template shard_consumer_tag<S>>&&`.
//
// Mirrors fixture #2 (producer_wrong_perm) on the consumer side:
// `PermissionedShardedCalendarGrid<Job, 2, 8, 16, Key,
//  1'000'000ULL, UserTag>` is a known
// ShardedCalendarGridSessionSurface (it specializes the trait
// per ShardedCalendarGridSession.h:43).  The first parameter
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

#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fscal = ::crucible::fixy::substr::sharded_calendar_grid;
namespace conc  = ::crucible::concurrent;

namespace neg_fixy_scal_consumer_wrong_perm {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept {
        return job.deadline_ns;
    }
};
using Grid = conc::PermissionedShardedCalendarGrid<
    Job, 2, 8, 16, Key, 1'000'000ULL, UserTag>;
}

int main() {
    neg_fixy_scal_consumer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fscal::mint_sharded_calendar_grid_consumer<
        neg_fixy_scal_consumer_wrong_perm::Grid, 0>(grid, not_a_perm);
    (void)bad;
    return 0;
}
