// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// THREE-TIER-GAP rejection (mid-range).  Companion to:
//   - neg_recipe_registry_relaxed_at_bitexact_fence (6-tier-gap, max)
//   - neg_recipe_registry_fp64_at_bitexact_fence (1-tier-gap, adjacent)
//
// Together the three fixtures span the lattice rejection surface:
// adjacent / mid-range / max gaps.  Proves the requires-clause
// correctly handles every cross-tier separation, not just the boundary
// cases.
//
// Violation: passing a `NumericalTier<ULP_FP16, T>` value to a function
// whose `requires` clause demands `NumericalTier::satisfies<BITEXACT>`.
// ULP_FP16 is THREE tiers below BITEXACT in the lattice
// (RELAXED ⊑ ULP_INT8 ⊑ ULP_FP8 ⊑ ULP_FP16 ⊑ ULP_FP32 ⊑ ULP_FP64
// ⊑ BITEXACT).  satisfies<BITEXACT> = leq(BITEXACT, ULP_FP16) = false.
//
// THE CANONICAL STARTER-RECIPE BUG-CLASS this catches.  Two of the
// eight starter recipes (f16_f32accum_tc, bf16_f32accum_tc) are
// BITEXACT_TC + FP16/BF16 output, which `tolerance_of()` maps to
// ULP_FP16.  These are the most commonly-used recipes for cross-
// vendor mixed-precision training (~5-8% perf tax vs UNORDERED, 0-1
// ULP cross-vendor at FP16 precision — the pragmatic sweet spot).
//
// A refactor that pins the deterministic-replay consumer (CPU
// oracle, regression CI) to BITEXACT but the upstream Forge phase
// silently selects f16_f32accum_tc instead of f32_strict because
// of a config typo or a missing intersection check at the partition
// solver — the recipe's tolerance_of() returns ULP_FP16, and the
// requires-clause REJECTS at compile time, naming the failed
// predicate.  Without this fence, BITEXACT-required call sites
// would silently accept ≤1-ULP-at-FP16 drift, breaking the
// 0-ULP-cross-vendor invariant on every replay step.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/NumericalTier.h>

#include <utility>

using namespace crucible::safety;

template <typename W>
    requires (W::template satisfies<Tolerance::BITEXACT>)
static int bitexact_fence_consumer(W wrapped) noexcept {
    (void) std::move(wrapped).consume();
    return 0;
}

int main() {
    // Pinned at ULP_FP16 — origin is a BITEXACT_TC FP16/BF16-storage
    // recipe (the f16_f32accum_tc / bf16_f32accum_tc starter recipes).
    // 0-1 ULP cross-vendor at FP16 precision; the pragmatic sweet spot
    // for tensor-core training.  Cannot reach a strict BITEXACT
    // (0 ULP byte-identical) consumer.
    NumericalTier<Tolerance::ULP_FP16, int> fp16_value{42};

    // Should FAIL: bitexact_fence_consumer requires satisfies<BITEXACT>;
    // ULP_FP16 is THREE tiers below BITEXACT.  Captures the
    // canonical "BITEXACT_TC + FP16-storage starter recipe silently
    // flowed into a BITEXACT_STRICT-required consumer" regression
    // class — the production failure mode for FOUND-G04.
    int result = bitexact_fence_consumer(std::move(fp16_value));
    return result;
}
