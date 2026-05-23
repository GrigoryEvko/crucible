// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-267 HS14 fixture 2/2 — partial-order direction (cross-trunk relax).
//
// ScopedFence<S, T>::relax<Narrower>() moves DOWN the MemoryScopeLattice
// partial order only (requires Narrower ⊑ S).  Relaxing an accel Cta fence
// to the ARM Inner shareability domain is forbidden: Cta and Inner are
// INCOMPARABLE (leq(Inner, Cta) == false — a GPU block-scope fence has no
// ordering relation to an ARM inner-shareable domain), so the relax
// requires-clause MUST reject it.  This is the signature rejection of a
// NON-DISTRIBUTIVE partial order — a case that cannot arise in the V-254
// Hw / V-255 BarrierGuarded total-order chains, where every pair is
// comparable.  Without the rejection a publication legalized for GPU block
// scope could be relabelled as ARM-domain-visible and consumed under a
// fence that never established that coherence relation.
//
// Distinct mismatch class from neg_scoped_fence_provider_too_narrow.cpp:
// this rejects at the PRODUCER `relax<>` boundary on the INCOMPARABLE
// cross-trunk direction, whereas the sister fixture rejects at a CONSUMER
// `satisfies<R>` admission gate along a comparable within-trunk pair.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "relax" naming the rejected relax<> overload.

#include <crucible/safety/ScopedFence.h>

namespace sf = ::crucible::safety;
using Ms_t   = sf::MemoryScope_v;

int main() {
    sf::ScopedFence<Ms_t::Cta, int> cta_fence{42};
    // Cta (accel) and Inner (ARM) are incomparable — relax MUST be rejected.
    auto cross_trunk = cta_fence.relax<Ms_t::Inner>();
    return cross_trunk.peek();
}
