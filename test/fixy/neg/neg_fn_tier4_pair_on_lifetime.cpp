// Tier 4 refuses two atoms on Lifetime.  Lifetime is a Lattice axis in the
// table of fixy/Axis.h, so it carries one grade, and the pack does not say
// in which of the two regions the value lives.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::in_region<0>, ::fixy::atom::in_region<1>> refused{};
    return 0;
}
