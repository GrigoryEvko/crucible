// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-088 HS14 fixture #2 of 2 for DimensionAxis::FpMode addition.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// TIER-MISCLASSIFY half — asserts that `tier_of_axis(FpMode)` returns
// `TierKind::Foundational`.  The actual classification is
// `TierKind::Semiring` (par=join, strictest-wins) per V-088, so the
// static_assert fires and the build reddens.
//
// Why this matters: Tier classification drives the par/seq composition
// law applied at Forge phase E.RecipeSelect and at every FpMode-pinned
// call site.  Misclassifying FpMode as Foundational (bidirectional
// elaboration) instead of Semiring (par=+, seq=*) would silently
// admit incompatible (FpMode, NumericalRecipe) pairs — two ops with
// FpRounding::RoundToZero composing with FpRounding::RoundToNearestEven
// would NOT be rejected at the par site, breaking BITEXACT recipes.
// The V-091 cross-axis CollisionCatalog rules (F101-F105) also fan
// out from Tier-S semantics; a Foundational reclassify silently
// disables those rules.
//
// Sibling fixture `neg_fp_mode_cardinality_stale.cpp` exercises the
// CARDINALITY-STALE half (the catalog cardinality must grow when a
// new axis is appended).
//
// Expected diagnostic: "static assertion failed|FpMode|TierKind|
// Foundational|Semiring|FIXY-V-088".

#include <crucible/safety/DimensionTraits.h>

namespace neg_fp_mode_tier_misclassify {

// STRAINING POINT: V-088 classifies FpMode on Tier-S (Semiring) per
// the `tier_of_axis` switch arm.  This static_assert reads as "FpMode
// is Tier-F (Foundational)" — false under V-088.  If this file
// compiles, FpMode was silently reclassified onto Foundational
// (regression) and Forge phase E.RecipeSelect's par/seq composition
// law no longer applies to FpMode-tagged kernel selection.
static_assert(::crucible::safety::tier_of_axis(
                  ::crucible::safety::DimensionAxis::FpMode)
              == ::crucible::safety::TierKind::Foundational,
    "FIXY-V-088 TIER-MISCLASSIFY neg-compile: this assertion MUST "
    "fail post-V-088.  FpMode is Tier-S (Semiring), NOT Tier-F "
    "(Foundational).  If it passes, FpMode was silently demoted off "
    "Semiring and Forge phase E.RecipeSelect's par/seq composition "
    "no longer applies.");

}  // namespace neg_fp_mode_tier_misclassify

int main() { return 0; }
