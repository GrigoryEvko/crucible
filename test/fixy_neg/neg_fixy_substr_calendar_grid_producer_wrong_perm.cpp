// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-10 negative fixture #2/8:
// `fixy::substr::calendar_grid::mint_calendar_grid_producer<
//      Grid, P>(grid, perm)` rejects when the second (perm)
// parameter cannot bind to
// `Permission<calendar_tag::Producer<UserTag, P>>&&`.
//
// Distinct from fixture #1 (producer_non_surface): #1 exercises
// the CalendarGridSessionSurface concept gate on the first
// (Grid) parameter; #2 exercises the second (perm) parameter
// binding AFTER the concept gate succeeds.
//
// `PermissionedCalendarGrid<Job, 2, 8, 16, Key, 1'000'000ULL,
// UserTag>` is a known CalendarGridSessionSurface (mirrors the
// detail::calendar_grid_session_self_test sketch).  The first
// parameter binds; the concept passes; the second parameter
// `int` cannot bind to `Permission<Producer<UserTag, P>>&&`.
//
// Expected diagnostic: "no matching function for call to
// 'mint_calendar_grid_producer'" / "cannot convert" /
// "Permission" / "mint_calendar_grid_producer".

#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fcal = ::crucible::fixy::substr::calendar_grid;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_cal_producer_wrong_perm {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept {
        return job.deadline_ns;
    }
};
using Grid = conc::PermissionedCalendarGrid<
    Job, 2, 8, 16, Key, 1'000'000ULL, UserTag>;
}

int main() {
    neg_fixy_cal_producer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fcal::mint_calendar_grid_producer<
        neg_fixy_cal_producer_wrong_perm::Grid, 0>(grid, not_a_perm);
    (void)bad;
    return 0;
}
