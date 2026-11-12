// An explicit specialization of a shipped atom template, written in a
// user file.
//
// stdio::write<Stream> engages Axis::Stdio.  The specialization below
// keeps the name and moves the axis to Usage, so a pack that names it
// would read as a Usage grade.  The template and its namespace are the
// catalog's.  source_location_of gives the file of the specialization,
// which is not the file that seals the family, so the catalog clause of
// IsAtom refuses it.

#include <fixy/Fn.h>
#include <fixy/atoms/Stdio.h>
#include <foundation/Quarantine.h>

namespace user_code {
struct private_stream final {};
}  // namespace user_code

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges stdio::write with the axis Usage for a private stream")
template <>
struct fixy::atom::stdio::write<user_code::private_stream> final : ::fixy::atom::atom_of<::fixy::Axis::Usage> {};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::stdio::write<user_code::private_stream>> refused{};
    return 0;
}
