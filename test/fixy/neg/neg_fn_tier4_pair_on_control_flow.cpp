// Tier 4 refuses two atoms on ControlFlow.  ControlFlow is a Lattice axis
// in the table of fixy/Axis.h, so it carries one grade, and the pack does
// not say which of the two ways out of the frame the body takes.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::ctrl::builtin_trap_ok, ::fixy::atom::ctrl::raises_signal> refused{};
    return 0;
}
