// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Calendar-grid session mint negative fixture 3 of 8:
// `safety::proto::calendar_grid_session::mint_calendar_grid_consumer<
//      Grid>(grid, perm)` rejects when Grid is NOT a
// CalendarGridSessionSurface.
//
// Mirrors fixture 1 (producer_non_surface) on the consumer
// side: proves that the CalendarGridSessionSurface concept gate
// fires INDEPENDENTLY of the producer-side instantiation.
//
// Distinct from fixture #4 (consumer_wrong_perm): #3 exercises
// the concept gate on the first (Grid) parameter; #4 exercises
// the second (perm) parameter binding AFTER the concept gate
// succeeds.
//
// Expected diagnostic: "CalendarGridSessionSurface" /
// "constraints not satisfied" / "no matching function" /
// "mint_calendar_grid_consumer".

#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_CalendarGridSession.h>

namespace fcal = ::crucible::safety::proto::calendar_grid_session;
namespace saf = ::crucible::safety;

struct consumer_tag_placeholder {};

int main() {
    int not_a_grid = 0;
    auto perm = saf::mint_permission_root<consumer_tag_placeholder>();

    auto bad = fcal::mint_calendar_grid_consumer(not_a_grid, std::move(perm));
    (void)bad;
    return 0;
}
