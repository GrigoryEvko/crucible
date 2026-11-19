// Tier 4 refuses two atoms on MemoryScope.  MemoryScope is a Lattice axis
// in the table of fixy/Axis.h, so it carries one grade, and the pack does
// not say how far a publication of the body reaches.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::scope::thread, ::fixy::atom::scope::gpu> refused{};
    return 0;
}
