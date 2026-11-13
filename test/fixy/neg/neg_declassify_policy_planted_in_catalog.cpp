// A declassification policy planted in the policy namespace from another
// file.
//
// The namespace is fixy::tags::secret_policy, so the namespace read
// admits it.  The file read compares this file with fixy/Tags.h, which
// declares the base, and refuses it.  A declassify atom over the planted
// tag does not form.

#include <fixy/Atom.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::tags::secret_policy {
struct PlantedRelease final : secret_policy_base {};
}  // namespace fixy::tags::secret_policy
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    [[maybe_unused]] ::fixy::atom::declassify<::fixy::tags::secret_policy::PlantedRelease> refused{};
    return 0;
}
