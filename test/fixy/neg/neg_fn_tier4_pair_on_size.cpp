// Tier 4 refuses two atoms on Size.  Size is a Lattice axis in the table of
// fixy/Axis.h, so it carries one grade, and the pack does not say which of
// the two size claims the binding makes.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::sized_at<1>, ::fixy::atom::productive> refused{};
    return 0;
}
