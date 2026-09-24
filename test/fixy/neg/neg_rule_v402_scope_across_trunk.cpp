// V402: a memory scope across the binding's architecture trunk.
//
// The two vendor trunks of each lattice meet only at the shared bottom
// and top.  A scope pinned on one trunk does not compose with an ISA
// pinned on another: the host shareability scopes (Inner, Outer) are the
// ARM DMB ISH and OSH families, and an x86-pinned body has no such
// domain to publish into.
//
// The old catalog carried this rule as a marker trait with a false_type
// primary, so it never fired structurally; fixy/Collision.h gives it
// detection from the two lattices' own trunk encodings, and the
// coherence table it reads is stated there.  This pack is the pair the
// table refuses most plainly: an AVX2 pin with an inner-shareable scope.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::simd::avx2, ::fixy::atom::scope::inner> refused{};
    return 0;
}
