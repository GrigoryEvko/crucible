// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Calendar-grid session mint negative fixture 4 of 8:
// `safety::proto::calendar_grid_session::mint_calendar_grid_consumer<
//      Grid>(grid, perm)` rejects when the second (perm)
// parameter cannot bind to
// `Permission<calendar_tag::Consumer<UserTag>>&&`.
//
// Mirrors fixture #2 (producer_wrong_perm) on the consumer side:
// proves the per-mint parameter shape holds INDEPENDENTLY of the
// producer-side instantiation.
//
// `PermissionedCalendarGrid<Job, 2, 8, 16, Key, 1'000'000ULL,
// UserTag>` is a known CalendarGridSessionSurface.  The first
// parameter binds; the concept passes; the second parameter
// `int` cannot bind to `Permission<Consumer<UserTag>>&&`.
//
// Expected diagnostic: "no matching function for call to
// 'mint_calendar_grid_consumer'" / "cannot convert" /
// "Permission" / "mint_calendar_grid_consumer".

#include <crucible/concurrent/_PermissionedCalendarGrid.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_CalendarGridSession.h>

namespace fcal = ::crucible::safety::proto::calendar_grid_session;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_cal_consumer_wrong_perm {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept { return job.deadline_ns; }
};
using Grid = conc::PermissionedCalendarGrid<Job, 2, 8, 16, Key, 1000000ULL, UserTag>;
}  // namespace neg_fixy_cal_consumer_wrong_perm

int main() {
    neg_fixy_cal_consumer_wrong_perm::Grid grid{};
    int not_a_perm = 0;

    auto bad = fcal::mint_calendar_grid_consumer(grid, not_a_perm);
    (void)bad;
    return 0;
}
