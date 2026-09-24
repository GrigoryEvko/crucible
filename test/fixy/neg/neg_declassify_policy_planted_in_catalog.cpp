// A declassification policy planted in the policy namespace from another
// file.
//
// The namespace is fixy::tags::secret_policy, so the namespace read
// admits it.  The file read compares this file with fixy/Tags.h, which
// declares the base, and refuses it.  A declassify atom over the planted
// tag does not form.

#include <fixy/Atom.h>

namespace fixy::tags::secret_policy {
struct PlantedRelease final : secret_policy_base {};
}  // namespace fixy::tags::secret_policy

int main() {
    [[maybe_unused]] ::fixy::atom::declassify<::fixy::tags::secret_policy::PlantedRelease> refused{};
    return 0;
}
