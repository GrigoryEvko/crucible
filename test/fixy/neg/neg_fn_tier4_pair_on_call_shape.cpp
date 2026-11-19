// Tier 4 refuses two atoms on CallShape.  CallShape is a Lattice axis in
// the table of fixy/Axis.h, so it carries one grade, and the pack does not
// say whether the recursion has a depth bound.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::dispatch::recurses<32>, ::fixy::atom::dispatch::recurses_unbounded>
        refused{};
    return 0;
}
