// Tier 4 refuses two atoms on BarrierStrength.  BarrierStrength is a
// Lattice axis in the table of fixy/Axis.h, so it carries one grade, and the
// pack does not say which of the two fences the region provides.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::barrier::acquire_load, ::fixy::atom::barrier::seq_cst> refused{};
    return 0;
}
