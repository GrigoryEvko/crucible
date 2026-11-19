// Tier 4 refuses two atoms on HwInstruction.  HwInstruction is a Lattice
// axis in the table of fixy/Axis.h, so it carries one grade, and the pack
// does not say which of the two instruction tiers the body reaches.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::hw::scalar, ::fixy::atom::hw::vectorizable> refused{};
    return 0;
}
