// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-097 HS14 fixture #2 of 2 for DimensionAxis::SyscallSurface
// addition.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// TIER-MISCLASSIFY half — asserts that `tier_of_axis(SyscallSurface)`
// returns `TierKind::Foundational`.  The actual classification is
// `TierKind::Semiring` (par=join, strictest-wins) per V-097, so the
// static_assert fires and the build reddens.
//
// Why this matters: Tier classification drives the par/seq composition
// law applied at the hot-path admission gate (Hardening.h hot-region
// check) and at every Mimic per-vendor backend that maps SyscallSurface
// onto its dispatch choice.  Misclassifying SyscallSurface as
// Foundational (bidirectional elaboration) instead of Semiring
// (par=join — composed syscall surface is the LUB of constituents)
// would silently admit incompatible (claimed-surface, admitted-policy)
// pairs — a binding declaring NoSyscall composing with one declaring
// ProcessControl would NOT lift its envelope at the par site,
// breaking Mimic's syscall-surface admission for the composite.  The
// V-100 effect-row bridge that lifts SyscallSurface into Met(X) rows
// also fans out from Tier-S semantics; a Foundational reclassify
// silently disables that lift.
//
// Sibling fixture `neg_syscall_family_cardinality_stale.cpp` exercises
// the CARDINALITY-STALE half (the catalog cardinality must grow when
// a new axis is appended).
//
// Expected diagnostic: "static assertion failed|SyscallSurface|
// TierKind|Foundational|Semiring|FIXY-V-097".

#include <crucible/safety/DimensionTraits.h>

namespace neg_syscall_family_tier_misclassify {

// STRAINING POINT: V-097 classifies SyscallSurface on Tier-S (Semiring)
// per the `tier_of_axis` switch arm.  This static_assert reads as
// "SyscallSurface is Tier-F (Foundational)" — false under V-097.  If
// this file compiles, SyscallSurface was silently reclassified onto
// Foundational (regression) and the hot-path admission gate's par=join
// composition law no longer applies to SyscallSurface-tagged binding
// selection.
static_assert(::crucible::safety::tier_of_axis(
                  ::crucible::safety::DimensionAxis::SyscallSurface)
              == ::crucible::safety::TierKind::Foundational,
    "FIXY-V-097 TIER-MISCLASSIFY neg-compile: this assertion MUST "
    "fail post-V-097.  SyscallSurface is Tier-S (Semiring), NOT "
    "Tier-F (Foundational).  If it passes, SyscallSurface was "
    "silently demoted off Semiring and Mimic per-vendor backend's "
    "par/seq composition no longer applies.");

}  // namespace neg_syscall_family_tier_misclassify

int main() { return 0; }
