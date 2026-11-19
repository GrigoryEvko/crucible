// A CallShape grade with no reading, read by the pass of the premises.
//
// The pass reads the CallShape grade of a binding through
// call_shape_reading_or_refuse_, which stops the build with a message that
// names the closed relation to extend.  The catalog of atoms is closed, so a
// fixture cannot write a pack with such a grade.  The fixture reads the
// assertion directly, with a type of the shape of a CallShape atom.

#include <fixy/Collision.h>

namespace fixture {
struct virtual_dispatch final : ::fixy::atom::atom_of<::fixy::Axis::CallShape> {};
}  // namespace fixture

[[maybe_unused]] constexpr auto reading =
    ::fixy::collision::detail::call_shape_reading_or_refuse_<fixture::virtual_dispatch>();

int main() { return 0; }
