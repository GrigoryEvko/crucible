// Tier 4 refuses two atoms on Precision.  Precision is a Lattice axis in
// the table of fixy/Axis.h, so it carries one grade, and the pack does not
// say which of the two precisions the result has.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::precision_f32, ::fixy::atom::precision_f64> refused{};
    return 0;
}
