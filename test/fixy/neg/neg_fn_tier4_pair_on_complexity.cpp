// Tier 4 refuses two atoms on Complexity.  Complexity is a Lattice axis in
// the table of fixy/Axis.h, so it carries one grade, and the pack does not
// say which of the two costs the body has.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::cost_constant, ::fixy::atom::cost_unbounded> refused{};
    return 0;
}
