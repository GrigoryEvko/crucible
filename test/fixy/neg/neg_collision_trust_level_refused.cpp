// A Trust grade with no level, read by the pass of the premises.
//
// The pass reads the Trust grade of a binding through trust_level_or_refuse_,
// which stops the build with a message that names the closed relation to
// extend.  trust_level_of_ gives no level for such a grade, and T001 would
// read it as a verified binding.  The catalog of atoms is closed, so a
// fixture cannot write a pack with such a grade.  The fixture reads the
// assertion directly, with a type of the shape of a Trust atom.

#include <fixy/Collision.h>

namespace fixture {
struct unlisted_trust final : ::fixy::atom::atom_of<::fixy::Axis::Trust> {};
}  // namespace fixture

[[maybe_unused]] constexpr auto level = ::fixy::collision::detail::trust_level_or_refuse_<fixture::unlisted_trust>();

int main() { return 0; }
