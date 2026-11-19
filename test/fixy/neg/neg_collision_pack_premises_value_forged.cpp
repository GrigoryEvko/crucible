// The cache of the premises of a pack holds a sealed cell.  The constructors
// of the cell are private, and the build of the cell is their one friend, so
// a specialization of the cache cannot hold premises that no pass gave.  This
// file tries to give the pack borrow with coroutine, which L002 refuses, the
// premises of no fact.

#include <fixy/Reject.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges the premises of a pack that L002 refuses")
template <>
inline constexpr fixy::collision::detail::pack_premises_cell fixy::collision::detail::pack_premises_cell_v<
    fixy::collision::pack_key<fixy::atom::borrow, fixy::atom::coroutine>> = {};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
