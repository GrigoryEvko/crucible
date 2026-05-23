// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-254 HS14 fixture 2/2 — chain-direction mismatch class.
//
// Hw<Tier, T>::widen<Higher>() moves UP the HwInstruction capability
// chain only (requires Tier ⊑ Higher).  Widening DOWN — here
// Hw<Vectorizable>.widen<Scalar>() — is forbidden: it would relabel a
// SIMD kernel as scalar-only, claiming a NARROWER instruction class than
// the kernel actually issues.  That defeats the Mimic instruction-
// legalization gate, which trusts the Hw pin to decide whether a SIMD
// ISA-availability proof (V-256 SimdWidthPinned) is required.
//
// Distinct mismatch class from neg_hw_consumer_too_weak.cpp: this rejects
// at the PRODUCER chain-direction boundary (`.widen<Lower>()` requires-
// clause), whereas the sister fixture rejects at a CONSUMER admission
// boundary (a `satisfies<Ceiling>` gate).
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "widen" naming the rejected widen<> overload.

#include <crucible/safety/Hw.h>

namespace sf = ::crucible::safety;
using Hw_t   = sf::HwInstruction_v;

int main() {
    sf::Hw<Hw_t::Vectorizable, int> simd_kernel{42};
    // Vectorizable ⊄ Scalar going DOWN — widen MUST be rejected.
    auto narrowed = simd_kernel.widen<Hw_t::Scalar>();
    return narrowed.peek();
}
