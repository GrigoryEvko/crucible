// The payload order keeps its answer for one pair in a cell, and only the
// walk of the order builds a cell.  This file tries to give the pair
// (Forged, int) the answer of the pair (ContentAddressed<int>, int): it
// specializes the cell of the first pair with a copy of the second.  The
// copy constructor of a cell is private, so the specialization does not
// compile.

#include <fixy/session/Subtype.h>
#include <foundation/Quarantine.h>

struct Forged {};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a cell of the base, and the compiler refuses it")
template <>
inline constexpr fixy::session::transition::detail::subsort_cell
    fixy::session::transition::subsort_cell_at<^^fixy::session::payload_axioms, ^^Forged, ^^int> =
        fixy::session::transition::subsort_cell_at<^^fixy::session::payload_axioms,
                                                   ^^fixy::session::ContentAddressed<int>, ^^int>;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
