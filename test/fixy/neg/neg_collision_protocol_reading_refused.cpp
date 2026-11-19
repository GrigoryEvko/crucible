// A Protocol grade with no reading, read by the pass of the premises.
//
// The pass reads the Protocol grade of a binding through
// protocol_reading_or_refuse_, which stops the build with a message that
// names the closed relation to extend.  The catalog of atoms is closed, so a
// fixture cannot write a pack with such a grade.  The fixture reads the
// assertion directly, with a type of the shape of a Protocol atom.

#include <fixy/Collision.h>

namespace fixture {
struct detached_thread final : ::fixy::atom::atom_of<::fixy::Axis::Protocol> {};
}  // namespace fixture

[[maybe_unused]] constexpr auto reading =
    ::fixy::collision::detail::protocol_reading_or_refuse_<fixture::detached_thread>();

int main() { return 0; }
