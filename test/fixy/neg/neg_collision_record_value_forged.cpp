// The cache of the record of a pack holds a sealed cell.  The constructors of
// the cell are private, and the build of the cell is their one friend, so a
// specialization of the cache cannot hold a record that no walk gave.  This
// file tries to give the pack copy with affine, which tier 4 refuses, an
// empty record.

#include <fixy/Reject.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges the record of a pack that tier 4 refuses")
template <>
inline constexpr fixy::collision::detail::pack_record_cell
    fixy::collision::detail::pack_record_cell_v<fixy::collision::pack_key<fixy::atom::copy, fixy::atom::affine>> = {};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
