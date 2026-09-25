// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Calendar-grid session mint negative fixture 1 of 8:
// `safety::proto::calendar_grid_session::mint_calendar_grid_producer<
//      Grid, P>(grid, perm)` rejects when Grid is NOT a
// CalendarGridSessionSurface.
//
// `int` lacks the CalendarGridSessionSurface concept's required
// nested types (value_type, user_tag, consumer_tag,
// ProducerHandle<P>, ConsumerHandle) and the producer<P>()
// factory member.  The requires-clause fires at substitution
// time.
//
// Distinct from fixture #2 (wrong_perm): #1 exercises the
// CalendarGridSessionSurface concept gate on the first (Grid)
// parameter; #2 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// Expected diagnostic: "CalendarGridSessionSurface" /
// "constraints not satisfied" / "no matching function" /
// "mint_calendar_grid_producer".

#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/CalendarGridSession.h>

namespace fcal = ::crucible::safety::proto::calendar_grid_session;
namespace saf = ::crucible::safety;

struct producer_tag_placeholder {};

int main() {
    int not_a_grid = 0;
    auto perm = saf::mint_permission_root<producer_tag_placeholder>();

    auto bad = fcal::mint_calendar_grid_producer<int, 0>(not_a_grid, std::move(perm));
    (void)bad;
    return 0;
}
