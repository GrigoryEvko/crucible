// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-254 HS14 fixture 1/2 — admission-gate mismatch class.
//
// A consumer that admits the NonDeterministicTsc capability ceiling
// accepts any Hw<Tier, T> with Tier ⊑ NonDeterministicTsc.  An
// Hw<PrivilegedMsr, T> exceeds that ceiling (PrivilegedMsr ⊄
// NonDeterministicTsc — ring-0 MSR/port I/O is strictly above a TSC
// read), so the gate MUST reject it.  Without this rejection, a kernel
// issuing privileged ring-0 instructions could flow into a context that
// only authorized a non-deterministic TSC read — a privilege-escalation
// the Mimic legalization gate (V-251 §3) is designed to catch at
// compile time.
//
// Distinct mismatch class from neg_hw_widen_to_lower.cpp: this rejects
// at the CONSUMER admission boundary (a `requires satisfies<Ceiling>`
// gate), whereas the sister fixture rejects at a PRODUCER chain-direction
// boundary (`.widen<Lower>()`).
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" naming the satisfies-gated admit_at_tsc_ceiling template.

#include <crucible/safety/Hw.h>

namespace sf = ::crucible::safety;
using Hw_t   = sf::HwInstruction_v;

// A consumer admitting at most a NonDeterministicTsc ceiling.
template <typename W>
    requires (W::template satisfies<Hw_t::NonDeterministicTsc>)
[[nodiscard]] constexpr int admit_at_tsc_ceiling(W const& w) noexcept {
    return w.peek();
}

int main() {
    sf::Hw<Hw_t::PrivilegedMsr, int> msr_kernel{42};
    // PrivilegedMsr ⊄ NonDeterministicTsc — admission MUST be rejected.
    return admit_at_tsc_ceiling(msr_kernel);
}
