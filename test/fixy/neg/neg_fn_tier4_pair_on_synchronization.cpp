// Tier 4 refuses two atoms on Synchronization.  Synchronization is a
// Lattice axis in the table of fixy/Axis.h, so it carries one grade, and the
// pack does not say which of the two wait strategies the body uses.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::sync::park, ::fixy::atom::sync::spin_pause> refused{};
    return 0;
}
