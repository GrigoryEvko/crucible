// Tier 4 refuses two atoms on Security.  Security is a Lattice axis in the
// table of fixy/Axis.h, so it carries one grade, and the pack does not say
// which of as_public and as_internal the binding means.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::as_public, ::fixy::atom::as_internal> refused{};
    return 0;
}
