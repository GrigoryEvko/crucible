// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-255 HS14 fixture 2/2 — chain-direction (strengthen-up) class.
//
// BarrierGuarded<Tier, T>::weaken<Lower>() moves DOWN the BarrierStrength
// chain only (requires Lower ⊑ Tier).  Strengthening UP — here
// BarrierGuarded<AcquireLoad>.weaken<SeqCst>() — is forbidden: it would
// CLAIM a stronger publication fence than the value was actually released
// under, fooling a consumer that requires SeqCst into trusting ordering
// that was never issued.  Weakening DOWN (claiming LESS than you provide)
// is always sound; strengthening UP is the unsound direction this fixture
// pins.
//
// Distinct mismatch class from neg_barrier_consumer_too_weak.cpp: this
// rejects at the PRODUCER chain-direction boundary (`.weaken<Stronger>()`
// requires-clause), whereas the sister fixture rejects at a CONSUMER
// admission boundary (a `satisfies<Floor>` gate).
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "weaken" naming the rejected weaken<> overload.

#include <crucible/safety/BarrierGuarded.h>

namespace sf = ::crucible::safety;
using Bs_t   = sf::BarrierStrength_v;

int main() {
    sf::BarrierGuarded<Bs_t::AcquireLoad, int> acquire_only{42};
    // AcquireLoad ⊉ SeqCst going UP — weaken MUST be rejected.
    auto strengthened = acquire_only.weaken<Bs_t::SeqCst>();
    return strengthened.peek();
}
