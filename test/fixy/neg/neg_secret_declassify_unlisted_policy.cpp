// A policy that derives from the marker base but has no edge in
// fixy::tags::secret_policy::admitted_policies is declared and not
// admitted.  declassify<Policy>() refuses it and names
// AdmittedDeclassification.

#include <fixy/Secret.h>

#include <utility>

namespace {
struct Unlisted final : ::fixy::tags::secret_policy::secret_policy_base {};
}  // namespace

int main() {
    fixy::Secret<int> key = fixy::mint_secret<int>(7);
    return std::move(key).declassify<Unlisted>();
}
