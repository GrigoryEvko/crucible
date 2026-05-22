// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-093 HS14 fixture 2/2.  Recipe-rounding-mode-mismatch
// rejection.
//
// `fixy::fp::canonicalize_for<R>(double)` static_asserts on
// `R.rounding == RoundingMode::RN` — round-to-nearest-ties-to-even
// is the IEEE 754 default and the only mode the default kernel
// pipeline in Mimic realizes.  A recipe declared with `RoundingMode::RZ`
// (truncate-toward-zero) is well-formed and useful for specialized
// quantization kernels, but merkle-folding under RZ would silently
// lock content_hash to a rounding mode no production kernel uses.
//
// Mismatch class for HS14 audit: rounding-mode rejection (distinct
// from the non-bitexact-recipe fixture's `is_bitexact` gate — both
// static_asserts protect the SAME merkle-stability invariant but at
// orthogonal recipe axes: determinism tier covers reduction-algorithm
// cross-vendor agreement; rounding mode covers default-kernel
// realization).
//
// Note: the bad recipe here satisfies the BITEXACT determinism gate
// (BITEXACT_STRICT) so ONLY the rounding-mode static_assert fires —
// orthogonality demonstration, not accidental coverage by the other
// fixture.
//
// Architectural intent: RoundingMode::RN is the IEEE default for a
// reason — it's the only mode every supported backend pipes through
// without per-vendor pinning overrides.  RZ/RM/RP are valid IEEE
// modes but realizing them on Hopper SASS vs AMDGPU vs PJRT requires
// per-vendor mode-switch instructions, and the cost of pinning those
// across the kernel cache exceeds the benefit at merkle granularity.
//
// Concrete bug-class this catches: a contributor configures a recipe
// for an int8-quantization kernel that needs `RoundingMode::RZ` for
// the saturating cast — the recipe is correct for that kernel but
// folds non-portably into merkle.  Caught at the canonicalize_for
// instantiation site rather than days later when the recipe gets
// reused for a non-quantization site and merkle drifts.
//
// Expected diagnostic: "RoundingMode::RN" OR "round-to-nearest" OR
// "static assertion failed".

#include <crucible/fixy/fp/Canonicalize.h>
#include <crucible/NumericalRecipe.h>

namespace fp = crucible::fixy::fp;

// `BITEXACT_STRICT` passes the determinism gate (covered by the
// other fixture); `RoundingMode::RZ` is the load-bearing rejection
// cause here — proves the rounding-mode gate fires ORTHOGONALLY to
// the determinism-tier gate.
constexpr fp::CanonicalizeRecipeSpec kRzStrictSpec{
    crucible::RoundingMode::RZ,                              // NOT RN
    crucible::ReductionDeterminism::BITEXACT_STRICT,         // OK
};

int main() {
    // The static_assert on `Spec.rounding == RoundingMode::RN` in
    // canonicalize_for<Spec> fires at template-instantiation time.
    auto bad = fp::canonicalize_for<kRzStrictSpec>(1.0);
    (void)bad;
    return 0;
}
