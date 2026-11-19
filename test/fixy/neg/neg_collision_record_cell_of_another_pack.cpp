// Tier 4 reads the record of a pack from its sealed cell.  Only the build of
// the cell makes one, and the build walks the pack that its key names.  This
// file gives the record cell of copy with affine, which tier 4 refuses, the
// cell of copy alone.  Tier 4 reads the cell with the key of its own pack, so
// the read refuses the cell of another pack, and the forgery admits nothing.

#include <fixy/Reject.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives the record cell of a pack the cell of another pack")
template <>
inline constexpr fixy::collision::detail::pack_record_cell
    fixy::collision::detail::pack_record_cell_v<fixy::collision::pack_key<fixy::atom::copy, fixy::atom::affine>> =
        fixy::collision::detail::build_pack_record_cell(^^fixy::collision::pack_key<fixy::atom::copy>);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::UniqueAtomPerAxis<fixy::atom::copy, fixy::atom::affine>);

int main() { return 0; }
