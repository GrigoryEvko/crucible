// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-11 negative fixture #2/8:
// `fixy::substr::sharded_calendar_grid::
//   mint_sharded_calendar_grid_producer<Grid, S>(grid, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<typename Grid::template shard_producer_tag<S>>&&`.
//
// Distinct from fixture #1 (producer_non_surface): #1 exercises
// the ShardedCalendarGridSessionSurface concept gate on the
// first (Grid) parameter; #2 exercises the second (perm)
// parameter binding AFTER the concept gate succeeds.
//
// `PermissionedShardedCalendarGrid<Job, 2, 8, 16, Key,
//  1'000'000ULL, UserTag>` is a known
// ShardedCalendarGridSessionSurface (it specializes the trait
// per ShardedCalendarGridSession.h:43).  The first parameter
// binds; the concept passes; the second parameter `int` cannot
// bind to `Permission<shard_producer_tag<S>>&&`.
//
// Expected diagnostic: "no matching function for call to
// 'mint_sharded_calendar_grid_producer'" / "cannot convert" /
// "Permission" / "mint_sharded_calendar_grid_producer".

#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fscal = ::crucible::fixy::substr::sharded_calendar_grid;
namespace conc  = ::crucible::concurrent;

namespace neg_fixy_scal_producer_wrong_perm {
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
    neg_fixy_scal_producer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fscal::mint_sharded_calendar_grid_producer<
        neg_fixy_scal_producer_wrong_perm::Grid, 0>(grid, not_a_perm);
    (void)bad;
    return 0;
}
