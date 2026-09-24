// An atom declared in a namespace of the user's own.
//
// The type has every part of the recipe: it is final, it derives
// atom_of, and it names an axis.  IsAtom reads the namespace of the
// declaration with parent_of, and the namespace is not fixy::atom or a
// family of it, so tier 2 refuses the pack.  Without that read the type
// would reach every collision rule as a shipped Usage grade.

#include <fixy/Fn.h>

namespace user_code {
struct forged_usage final : ::fixy::atom::atom_of<::fixy::Axis::Usage> {};
}  // namespace user_code

int main() {
    [[maybe_unused]] ::fixy::fn<int, user_code::forged_usage> refused{};
    return 0;
}
