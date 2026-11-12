// A Protocol grade that the reading of L003 does not name.
//
// The catalog of atoms is closed, so a fixture cannot add a spawn atom to
// the Protocol axis.  The fixture reads the reader of L003 directly, with
// a type that has the shape of a Protocol atom.  The closed relation names
// the strict pole and each Protocol atom, and it gives no answer for any
// other type.  So the reader stops the build with one message, and it does
// not read the type as a spawn that a join ties to the frame.

#include <fixy/Collision.h>

namespace fixture {
struct detached_thread final : ::fixy::atom::atom_of<::fixy::Axis::Protocol> {};
}  // namespace fixture

[[maybe_unused]] constexpr bool answer =
    ::fixy::collision::detail::can_spawn_outlive_the_frame_<fixture::detached_thread>::value;

int main() { return 0; }
