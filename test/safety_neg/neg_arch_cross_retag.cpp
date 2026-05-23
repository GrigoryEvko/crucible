// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-261 fixture #2 of 3 — distinct mismatch class:
// "cross-trunk RETAG on the production Tagged::retag<>() path".
//
// Violation: invoking `Tagged<int, source::X86Pinned>::retag<source::
// ArmPinned>()` — relabelling an x86-pinned value as ARM-pinned.  The
// value was produced by x86 instructions (mfence / SSE..AVX-512); it
// does NOT run on ARM.  V-261 ships NO retag_policy<X86Pinned, ArmPinned>
// specialization, so the V-022 fail-closed primary leaves it rejected
// and the `RetagAllowed` requires-clause on Tagged::retag<>() fires.
//
// Distinct from neg_arch_cross_compose.cpp: that exercises the
// COMPOSITION concept on bare types; this exercises the actual
// production retag<>() member (the runtime relabel path).
//
// Expected diagnostic substring: a constraint-not-satisfied family
// diagnostic mentioning RetagAllowed (same family as the V-024 / V-232
// retag gate fixtures).

#include <crucible/safety/source/Arch.h>
#include <crucible/safety/Tagged.h>

#include <utility>

namespace ss  = crucible::safety;
namespace src = crucible::safety::source;

int main() {
    // An x86-pinned value: produced by x86-specific instructions.
    ss::Tagged<int, src::X86Pinned> x86{0};

    // Relabel x86 → ARM: a lie about which trunk can decode the code.
    // The RetagAllowed<X86Pinned, ArmPinned> constraint is unsatisfied.
    auto arm = std::move(x86).retag<src::ArmPinned>();
    (void)arm;
    return 0;
}
