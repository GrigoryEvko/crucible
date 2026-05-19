// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-10 negative fixture #3/8:
// `fixy::substr::calendar_grid::mint_calendar_grid_consumer<
//      Grid>(grid, perm)` rejects when Grid is NOT a
// CalendarGridSessionSurface.
//
// Mirrors fixture #1 (producer_non_surface) on the consumer
// side: proves the CalendarGridSessionSurface concept gate is
// preserved through the using-decl in Substr.h INDEPENDENTLY of
// the producer-side instantiation.
//
// Distinct from fixture #4 (consumer_wrong_perm): #3 exercises
// the concept gate on the first (Grid) parameter; #4 exercises
// the second (perm) parameter binding AFTER the concept gate
// succeeds.
//
// Expected diagnostic: "CalendarGridSessionSurface" /
// "constraints not satisfied" / "no matching function" /
// "mint_calendar_grid_consumer".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fcal = ::crucible::fixy::substr::calendar_grid;
namespace saf  = ::crucible::safety;

struct consumer_tag_placeholder {};

int main() {
    int not_a_grid = 0;
    auto perm = saf::mint_permission_root<consumer_tag_placeholder>();

    auto bad = fcal::mint_calendar_grid_consumer(not_a_grid, std::move(perm));
    (void)bad;
    return 0;
}
