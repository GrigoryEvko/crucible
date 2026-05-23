// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-256 HS14 fixture 2/2 — partial-order direction (cross-trunk relax).
//
// SimdWidthPinned<W, T>::relax<Weaker>() moves DOWN the SimdIsaLattice
// partial order only (requires Weaker ⊑ W).  Relaxing an x86 Avx2 value
// to the ARM Neon trunk is forbidden: Avx2 and Neon are INCOMPARABLE
// (leq(Neon, Avx2) == false — x86 code never runs on ARM and vice versa),
// so the relax requires-clause MUST reject it.  This is the signature
// rejection of a NON-DISTRIBUTIVE partial order — a case that cannot
// arise in the V-254 Hw / V-255 BarrierGuarded total-order chains, where
// every pair is comparable.  Without the rejection a kernel legalized for
// x86 SIMD could be relabelled as ARM-runnable and dispatched to silicon
// that cannot decode its instructions.
//
// Distinct mismatch class from neg_simd_provider_too_weak.cpp: this
// rejects at the PRODUCER `relax<>` boundary on the INCOMPARABLE
// cross-trunk direction, whereas the sister fixture rejects at a CONSUMER
// `satisfies<R>` admission gate along a comparable within-trunk pair.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "relax" naming the rejected relax<> overload.

#include <crucible/safety/SimdWidthPinned.h>

namespace sf = ::crucible::safety;
using Si_t   = sf::SimdIsa_v;

int main() {
    sf::SimdWidthPinned<Si_t::Avx2, int> avx2_kernel{42};
    // Avx2 (x86) and Neon (ARM) are incomparable — relax MUST be rejected.
    auto cross_trunk = avx2_kernel.relax<Si_t::Neon>();
    return cross_trunk.peek();
}
