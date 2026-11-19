// Tier 4 refuses two atoms on Representation.  Representation is a Lattice
// axis in the table of fixy/Axis.h, so it carries one grade, and the pack
// does not say which of the two layouts the value has.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::repr<::fixy::pole::ReprKind::C>,
                                ::fixy::atom::repr<::fixy::pole::ReprKind::Packed>> refused{};
    return 0;
}
