// Tier 4 refuses two atoms on SimdIsa.  SimdIsa is a Lattice axis in the
// table of fixy/Axis.h, so it carries one grade, and the pack does not say
// for which of the two instruction sets the body is built.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::simd::avx2, ::fixy::atom::simd::neon> refused{};
    return 0;
}
