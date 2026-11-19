// Tier 4 refuses two atoms on StackUse.  StackUse is a Lattice axis in the
// table of fixy/Axis.h, so it carries one grade, and the pack does not say
// which of the two frame budgets the body keeps.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::stack::alloc<64>, ::fixy::atom::stack::alloc<4096>> refused{};
    return 0;
}
