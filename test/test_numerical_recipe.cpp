// Stub test for NumericalRecipe / ReductionDeterminism — per FORGE.md §19.
//
// NumericalRecipe is not wired into any compile pass yet.  This test
// pins the contract: layout is 16 bytes, defaults are the expected
// "ORDERED FP32-accum pairwise RN ONLINE_LSE" combo, determinism-tier
// predicates agree with the four-tier escalation ladder in FORGE.md
// §19.1 and CRUCIBLE.md §10.5.
//
// When Phase E RecipeSelect lands (FORGE.md §10), this test grows
// teeth: registry round-trip, fleet-intersection pick, realize_recipe
// per-backend byte exactness.  Today it's a compile-and-contract
// smoke test that keeps the stub honest as the tier semantics evolve.

#include <crucible/NumericalRecipe.h>

#include <cassert>
#include <cstdio>
#include <type_traits>

int main() {
    using crucible::NumericalRecipe;
    using crucible::ReductionAlgo;
    using crucible::ReductionDeterminism;
    using crucible::RoundingMode;
    using crucible::ScalarType;
    using crucible::ScalePolicy;
    using crucible::SoftmaxRecurrence;

    // ── Layout contract ─────────────────────────────────────────────
    //
    // 16 bytes is load-bearing — the RecipePool Swiss table assumes
    // this and Phase J's plan_hash composition reserves exactly this
    // many bytes per referenced recipe.  Drift here = recompute
    // every cached kernel's hash.
    static_assert(sizeof(NumericalRecipe) == 16);
    static_assert(alignof(NumericalRecipe) == 16);
    static_assert(std::is_trivially_copyable_v<NumericalRecipe>);
    static_assert(std::is_standard_layout_v<NumericalRecipe>);

    // ── Default-construction ────────────────────────────────────────
    //
    // The default recipe is "FP32-accum pairwise ORDERED ONLINE_LSE"
    // — the production-default for mixed-vendor training on FP16 /
    // BF16 activations.  Per FORGE.md §19.4 (source #2: model default),
    // every CKernelId that doesn't override falls back here.
    {
        NumericalRecipe r{};
        assert(r.accum_dtype    == ScalarType::Float);
        assert(r.out_dtype      == ScalarType::Undefined);
        assert(r.reduction_algo == ReductionAlgo::PAIRWISE);
        assert(r.rounding       == RoundingMode::RN);
        assert(r.scale_policy   == ScalePolicy::NONE);
        assert(r.softmax        == SoftmaxRecurrence::ONLINE_LSE);
        assert(r.determinism    == ReductionDeterminism::ORDERED);
        assert(r.flags          == 0);
        assert(r.hash.raw()     == 0);  // unhashed until RecipePool interns
    }

    // ── Four-tier determinism predicates ────────────────────────────
    //
    // Lock the ladder semantics from FORGE.md §19.1:
    //   UNORDERED    — tensor cores OK; block-scaled OK; not bit-exact
    //   ORDERED      — same; bounded ULP
    //   BITEXACT_TC  — tensor cores OK (K≤8 constraint at recipe level);
    //                  block-scaled NOT OK; cross-vendor 0-1 ULP
    //   BITEXACT_STRICT — scalar FMA only; block-scaled NOT OK;
    //                     byte-identical cross-arch
    using D = ReductionDeterminism;

    assert(!crucible::is_bitexact(D::UNORDERED));
    assert(!crucible::is_bitexact(D::ORDERED));
    assert( crucible::is_bitexact(D::BITEXACT_TC));
    assert( crucible::is_bitexact(D::BITEXACT_STRICT));

    assert( crucible::permits_tensor_cores(D::UNORDERED));
    assert( crucible::permits_tensor_cores(D::ORDERED));
    assert( crucible::permits_tensor_cores(D::BITEXACT_TC));
    assert(!crucible::permits_tensor_cores(D::BITEXACT_STRICT));

    assert( crucible::allows_block_scaled_formats(D::UNORDERED));
    assert( crucible::allows_block_scaled_formats(D::ORDERED));
    assert(!crucible::allows_block_scaled_formats(D::BITEXACT_TC));
    assert(!crucible::allows_block_scaled_formats(D::BITEXACT_STRICT));

    // ── Mutating a field mutates bit layout (trivial, catches ABI drift) ─
    {
        NumericalRecipe r{};
        r.determinism = ReductionDeterminism::BITEXACT_STRICT;
        r.flags       = 0x05;  // flush_to_zero | allow_denormal
        // accum_dtype unchanged
        assert(r.accum_dtype == ScalarType::Float);
        assert(r.determinism == ReductionDeterminism::BITEXACT_STRICT);
        assert(r.flags       == 0x05);
    }

    std::printf("test_numerical_recipe: all tests passed\n");
    return 0;
}
