// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-261 fixture #1 of 3 — distinct mismatch class:
// "composing an x86-pinned value with an ARM-pinned value".
//
// Violation: asserting `ArchComposable<X86Pinned, ArmPinned>` — the
// cross-arch composition gate.  An x86+ARM binding can never run: the
// emitted binary would `#UD` on whichever ISA it lands.  `arch_compatible
// (X86, Arm)` is false (the two concrete trunks are mutually
// incomparable, like SimdIsaLattice cross-trunk leq), so the
// ArchComposable concept is NOT satisfied and the static_assert fires.
//
// This is the source-tag-level companion to V-260's V002 catalog rule
// (marks_vendor_cross_arch), which catches the same hazard at the
// Fn-binding grant-pack level.
//
// Sister fixtures:
//   neg_arch_cross_retag.cpp       — cross-trunk RETAG (production path)
//   neg_arch_false_widen_retag.cpp — concrete → Portable false widening
//
// Expected diagnostic substring: V-261.

#include <crucible/safety/source/Arch.h>

namespace ss  = crucible::safety;
namespace src = crucible::safety::source;

// The gate must reject x86 × ARM.  This static_assert is written to
// SUCCEED only if the (unsound) composition were admitted — so the
// build correctly FAILS here, printing the V-261 message.
static_assert(ss::ArchComposable<src::X86Pinned, src::ArmPinned>,
    "V-261: x86 × ARM composition must be rejected by the cross-arch "
    "gate — this fixture proves the rejection fires.");

int main() { return 0; }
