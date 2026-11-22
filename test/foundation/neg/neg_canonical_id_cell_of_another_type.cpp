// Negative-compile test. This file must fail to compile.
//
// The row hash keeps the identity of a type in a cell, and the cell holds
// the type that it answers.  A unit cannot build a cell or copy one, but it
// can give a specialization of canonical_id_cell_of the cell that the build
// gives for another type.  This file gives Forged the cell of Honest, so
// that the two share one cache slot.  The read compares the type of the
// cell with the type that it asks for, and the call that it makes on a
// difference stops the build.
#include <foundation/Quarantine.h>
#include <foundation/diag/RowHash.h>

namespace forged_identity {
struct Forged {};
struct Honest {};
}  // namespace forged_identity

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives canonical_id_cell_of of a type the cell of another type")
template <>
inline constexpr foundation::diag::detail::canonical_id_cell
    foundation::diag::canonical_id_cell_of<forged_identity::Forged> = foundation::diag::detail::build_canonical_id_cell(
        ^^forged_identity::Honest, foundation::diag::detail::IdentityStatement::None);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(foundation::diag::read_canonical_id(foundation::diag::canonical_id_cell_of<forged_identity::Forged>,
                                                  ^^forged_identity::Forged)
              != 0);

int main() { return 0; }
