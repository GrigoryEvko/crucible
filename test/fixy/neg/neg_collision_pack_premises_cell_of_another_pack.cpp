// The verdict of a binding reads the premises of its pack from their sealed
// cell.  Only the build of the cell makes one, and the build reads the pack
// that its key names.  This file gives the premises cell of borrow with
// coroutine, which L002 refuses, the cell of borrow alone.  The verdict reads
// the cell with the key of its own pack, so the read refuses the cell of
// another pack, and the forgery admits nothing.

#include <fixy/Reject.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives the premises cell of a pack the cell of another pack")
template <>
inline constexpr fixy::collision::detail::pack_premises_cell fixy::collision::detail::pack_premises_cell_v<
    fixy::collision::pack_key<fixy::atom::borrow, fixy::atom::coroutine>> =
    fixy::collision::detail::build_pack_premises_cell(^^fixy::collision::pack_key<fixy::atom::borrow>);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::ValidComposition<int, fixy::atom::borrow, fixy::atom::coroutine>);

int main() { return 0; }
