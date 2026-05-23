// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-255 HS14 fixture 1/2 — admission-gate (floor-not-met) class.
//
// A consumer that requires an AcqRel publication floor accepts any
// BarrierGuarded<Tier, T> whose fence is ⊒ AcqRel (AcqRel ⊑ Tier).  A
// BarrierGuarded<AcquireLoad, T> falls SHORT of that floor (AcquireLoad
// ⊏ AcqRel — an acquire-only publication lacks the release half), so the
// gate MUST reject it.  Without this rejection, a value published with
// only acquire ordering could flow into a consumer that relies on the
// release-store visibility an AcqRel floor guarantees — a memory-ordering
// hole the BarrierGuarded floor gate (V-255) is designed to catch at
// compile time.
//
// Distinct mismatch class from neg_barrier_strengthen_up.cpp: this
// rejects at the CONSUMER admission boundary (a `requires satisfies<
// Floor>` gate), whereas the sister fixture rejects at a PRODUCER
// chain-direction boundary (`.weaken<Stronger>()`).
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" naming the satisfies-gated require_acqrel_floor template.

#include <crucible/safety/BarrierGuarded.h>

namespace sf = ::crucible::safety;
using Bs_t   = sf::BarrierStrength_v;

// A consumer requiring at least an AcqRel publication floor.
template <typename W>
    requires (W::template satisfies<Bs_t::AcqRel>)
[[nodiscard]] constexpr int require_acqrel_floor(W const& w) noexcept {
    return w.peek();
}

int main() {
    sf::BarrierGuarded<Bs_t::AcquireLoad, int> acquire_only{42};
    // AcquireLoad ⊏ AcqRel — the floor is NOT met, admission MUST reject.
    return require_acqrel_floor(acquire_only);
}
