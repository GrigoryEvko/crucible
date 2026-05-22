// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-097 HS14 fixture #1 of 2 for DimensionAxis::SyscallSurface
// addition.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// CARDINALITY-STALE half — asserts the PRE-V-097 cardinality
// `DIMENSION_AXIS_COUNT == 23`.  Post-V-097 the count is 24, so the
// static_assert fires and the build reddens.
//
// Why this matters: downstream consumers that hard-code the axis
// count (per-axis arrays sized by DIMENSION_AXIS_COUNT, switch-arm
// exhaustiveness checks, federation-cache slot table sizes) would
// silently lose their SyscallSurface slot if cardinality drift goes
// undetected.  This fixture catches a regression that REMOVES
// SyscallSurface (or any subsequent axis) and inadvertently restores
// the old count.
//
// Sibling fixture `neg_syscall_family_tier_misclassify.cpp` exercises
// the TIER-MISCLASSIFY half (SyscallSurface must be Tier-S Semiring;
// folding it onto any other Tier would silently break the hot-path
// admission gate that par=join-composes a binding's claimed syscall
// surface with the call site's admitted surface).
//
// Expected diagnostic: "static assertion failed|DIMENSION_AXIS_COUNT|
// FIXY-V-097|23".

#include <crucible/safety/DimensionTraits.h>

namespace neg_syscall_family_cardinality_stale {

// STRAINING POINT: post-V-097 the catalog grew from 23 axes to 24
// (SyscallSurface was appended at ordinal 23).  This static_assert
// reads as "the catalog has NOT grown past 23" — true PRE-V-097,
// false POST-V-097.  If this file compiles, V-097's axis was REMOVED
// without notice (or its commit was reverted), and downstream
// consumers that hard-code the count silently lose their
// SyscallSurface slot.
static_assert(::crucible::safety::DIMENSION_AXIS_COUNT == 23,
    "FIXY-V-097 CARDINALITY-STALE neg-compile: this assertion MUST "
    "fail post-V-097.  If it passes, DimensionAxis::SyscallSurface "
    "was removed (regression).");

}  // namespace neg_syscall_family_cardinality_stale

int main() { return 0; }
