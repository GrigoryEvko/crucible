// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-267 HS14 fixture 1/2 — within-trunk admission (provider-too-narrow).
//
// A consumer requiring a device-wide (Gpu) publish scope accepts any
// ScopedFence<S, T> whose pinned scope SUBSUMES Gpu (Gpu ⊑ S in the
// MemoryScopeLattice partial order).  A ScopedFence<Cta, T> does NOT
// subsume Gpu — within the accel trunk Cta (block scope) sits BELOW Gpu
// (device scope), so a block-scope fence does not make writes visible to
// the whole device the consumer needs.  The gate MUST reject it; without
// the rejection a publication consumed at device scope would read writes a
// block-scope fence never made device-visible — a weak-memory data race.
//
// Distinct mismatch class from neg_scoped_fence_relax_up_or_cross_trunk.cpp:
// this rejects at the CONSUMER admission boundary (a `requires satisfies<R>`
// gate) along a COMPARABLE within-trunk pair, whereas the sister fixture
// rejects at a PRODUCER `relax<>` boundary on the INCOMPARABLE cross-trunk
// direction.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" naming the satisfies-gated require_gpu_fence template.

#include <crucible/safety/ScopedFence.h>

namespace sf = ::crucible::safety;
using Ms_t   = sf::MemoryScope_v;

// A consumer requiring at least a device-wide (Gpu) publish scope.
template <typename W>
    requires (W::template satisfies<Ms_t::Gpu>)
[[nodiscard]] constexpr int require_gpu_fence(W const& w) noexcept {
    return w.peek();
}

int main() {
    sf::ScopedFence<Ms_t::Cta, int> cta_fence{42};
    // Cta does NOT subsume Gpu — admission MUST be rejected.
    return require_gpu_fence(cta_fence);
}
