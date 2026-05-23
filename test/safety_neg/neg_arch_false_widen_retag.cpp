// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-261 fixture #3 of 3 — distinct mismatch class:
// "false WIDENING retag (concrete trunk → Portable)".
//
// Violation: invoking `Tagged<int, source::X86Pinned>::retag<source::
// PortablePinned>()` — relabelling an x86-pinned value as Portable (⊤,
// "runs on every trunk").  That is a false widening: x86-pinned code
// `#UD`s on ARM, so it does NOT run everywhere.  Only the WEAKENING
// direction (Portable → concrete) is admitted by the V-261 catalog;
// the widening direction stays rejected by the V-022 fail-closed
// primary, so the `RetagAllowed` requires-clause fires.
//
// Distinct from neg_arch_cross_retag.cpp: that is a lateral cross-trunk
// relabel (X86 → Arm); this is an upward widening (X86 → Portable ⊤).
// Both are unsound, structurally different directions in the lattice.
//
// Expected diagnostic substring: a constraint-not-satisfied family
// diagnostic mentioning RetagAllowed.

#include <crucible/safety/source/Arch.h>
#include <crucible/safety/Tagged.h>

#include <utility>

namespace ss  = crucible::safety;
namespace src = crucible::safety::source;

int main() {
    // An x86-pinned value.
    ss::Tagged<int, src::X86Pinned> x86{0};

    // Widen x86 → Portable: claim it runs on every trunk.  False — it
    // #UDs on ARM.  RetagAllowed<X86Pinned, PortablePinned> is
    // unsatisfied (no admittance shipped; only Portable → concrete is).
    auto portable = std::move(x86).retag<src::PortablePinned>();
    (void)portable;
    return 0;
}
