// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The payload order keeps the answer of a pair in a cell, and the cell
// holds the pair that it answers.  A unit cannot build a cell or copy one,
// but it can give a specialization of subsort_cell_at the cell that the
// walk builds for another pair.  This file gives the pair of Forged and
// Honest the cell of Honest and Honest, which holds.  The read of the order
// compares the pair of the cell with the pair that it asks for, and the
// call that it makes on a difference stops the build.
#include <fixy/session/Subtype.h>
#include <foundation/Quarantine.h>

struct Forged {};
struct Honest {};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives subsort_cell_at of a pair the cell of another pair")
template <>
inline constexpr fixy::session::transition::detail::subsort_cell
    fixy::session::transition::subsort_cell_at<^^fixy::session::payload_axioms, ^^Forged, ^^Honest> =
        fixy::session::transition::detail::walk_subsort(^^fixy::session::payload_axioms, ^^Honest, ^^Honest);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::session::is_payload_subsort_v<Forged, Honest>);

int main() { return 0; }
