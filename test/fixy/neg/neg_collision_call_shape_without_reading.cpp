// A CallShape grade that the reading of D001 does not name.
//
// The catalog of atoms is closed, so a fixture cannot add an atom to the
// CallShape axis.  The fixture reads the reader of D001 directly, with a
// type that has the shape of a CallShape atom.  The closed relation names
// the strict pole and each CallShape atom, and it gives no answer for any
// other type.  So the reader stops the build with one message, and it does
// not read the type as a call that cannot throw.

#include <fixy/Collision.h>

namespace fixture {
struct virtual_dispatch final : ::fixy::atom::atom_of<::fixy::Axis::CallShape> {};
}  // namespace fixture

[[maybe_unused]] constexpr bool answer =
    ::fixy::collision::detail::can_indirect_call_throw_<fixture::virtual_dispatch>::value;

int main() { return 0; }
