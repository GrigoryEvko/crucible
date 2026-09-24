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

namespace user_code {
struct private_stream final {};
}  // namespace user_code

template <>
struct fixy::atom::stdio::write<user_code::private_stream> final : ::fixy::atom::atom_of<::fixy::Axis::Usage> {};

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::stdio::write<user_code::private_stream>> refused{};
    return 0;
}
