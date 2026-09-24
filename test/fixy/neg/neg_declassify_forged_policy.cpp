// A declassification policy declared outside the closed set.
//
// The tag is final and derives secret_policy_base, which is the whole
// recipe.  A policy licenses a drop of the Security grade, so the set of
// policies is closed the way the atom catalog is: IsDeclassificationPolicy
// reads the namespace and the file of the declaration.  The tag below is
// declared in a user namespace, so declassify<Policy> does not form.

#include <fixy/Atom.h>

namespace user_code {
struct forged_release final : ::fixy::tags::secret_policy::secret_policy_base {};
}  // namespace user_code

int main() {
    [[maybe_unused]] ::fixy::atom::declassify<user_code::forged_release> refused{};
    return 0;
}
