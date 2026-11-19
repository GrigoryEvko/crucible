// Tier 4 refuses two atoms on Regime.  Regime is a Lattice axis in the
// table of fixy/Axis.h, so it carries one grade, and the pack does not say
// in which of the two parts of the latency budget the body runs.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::regime::cold> refused{};
    return 0;
}
