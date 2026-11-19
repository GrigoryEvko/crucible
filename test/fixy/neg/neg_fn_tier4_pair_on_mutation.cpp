// Tier 4 refuses two atoms on Mutation.  Mutation is a Lattice axis in the
// table of fixy/Axis.h, so it carries one grade, and the pack does not say
// which of the two rights to mutate the binding claims.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::mut_mutable, ::fixy::atom::mut_append> refused{};
    return 0;
}
