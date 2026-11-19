// Tier 4 refuses two atoms on Space.  Space is a Lattice axis in the table
// of fixy/Axis.h, so it carries one grade, and the pack does not say which
// of the two bounds the body keeps.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::space_bounded<64>, ::fixy::atom::space_unbounded> refused{};
    return 0;
}
