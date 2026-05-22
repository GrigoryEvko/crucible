// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-088 HS14 fixture #1 of 2 for DimensionAxis::FpMode addition.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// CARDINALITY-STALE half — asserts the PRE-V-088 cardinality
// `DIMENSION_AXIS_COUNT == 22`.  Post-V-088 the count is 23, so the
// static_assert fires and the build reddens.
//
// Why this matters: downstream consumers that hard-code the axis
// count (per-axis arrays sized by DIMENSION_AXIS_COUNT, switch-arm
// exhaustiveness checks, federation-cache slot table sizes) would
// silently lose their FpMode slot if cardinality drift goes
// undetected.  This fixture catches a regression that REMOVES FpMode
// (or any subsequent axis) and inadvertently restores the old count.
//
// Sibling fixture `neg_fp_mode_tier_misclassify.cpp` exercises the
// TIER-MISCLASSIFY half (FpMode must be Tier-S Semiring; folding it
// onto any other Tier would silently break Forge phase E.RecipeSelect's
// NumericalRecipe par/seq composition).
//
// Expected diagnostic: "static assertion failed|DIMENSION_AXIS_COUNT|
// FIXY-V-088|22".

#include <crucible/safety/DimensionTraits.h>

namespace neg_fp_mode_cardinality_stale {

// STRAINING POINT: post-V-088 the catalog grew from 22 axes to 23
// (FpMode was appended at ordinal 22).  This static_assert reads as
// "the catalog has NOT grown" — true PRE-V-088, false POST-V-088.
// If this file compiles, V-088's axis was REMOVED without notice
// (or its commit was reverted), and downstream consumers that hard-
// code the count silently lose their FpMode slot.
static_assert(::crucible::safety::DIMENSION_AXIS_COUNT == 22,
    "FIXY-V-088 CARDINALITY-STALE neg-compile: this assertion MUST "
    "fail post-V-088.  If it passes, DimensionAxis::FpMode was "
    "removed (regression).");

}  // namespace neg_fp_mode_cardinality_stale

int main() { return 0; }
