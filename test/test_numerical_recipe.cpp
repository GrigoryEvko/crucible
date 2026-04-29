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

#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <type_traits>
#include <unordered_set>

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

    // ── Hash idempotence ────────────────────────────────────────────
    //
    // compute_recipe_hash must NOT consult the `hash` field itself;
    // re-hashing an already-hashed recipe must produce the same value
    // as hashing a fresh recipe with identical semantic fields.  If
    // this property breaks, serialize/deserialize round-trips would
    // drift a recipe's identity on reload.
    {
        NumericalRecipe fresh{};
        fresh.accum_dtype = ScalarType::Float;
        fresh.out_dtype   = ScalarType::Half;
        fresh.determinism = ReductionDeterminism::BITEXACT_TC;

        const auto h_fresh = crucible::compute_recipe_hash(fresh);

        // Populate the hash field, re-hash — must equal.
        NumericalRecipe filled = fresh;
        filled.hash = h_fresh;
        const auto h_filled = crucible::compute_recipe_hash(filled);
        assert(h_fresh == h_filled && "hash must exclude the hash field");

        // Double-populated: still the same.
        filled.hash = h_filled;
        assert(crucible::compute_recipe_hash(filled) == h_fresh);

        // crucible::hashed() populates the field; hashed(hashed(x)) is idempotent.
        const auto once  = crucible::hashed(fresh);
        const auto twice = crucible::hashed(once);
        assert(once.hash == twice.hash);
        assert(once.hash == h_fresh);
    }

    // ── Hash stability goldens (Family-A per Types.h taxonomy) ──────
    //
    // Wire-safe recipe hashes participate in KernelContentHash
    // (FORGE.md §18.6) and L1 federation cache keys (§23.2).  Drift
    // here invalidates every persisted kernel binding.  Three pinned
    // values cover the four-tier × multi-dtype matrix corners.
    //
    // To update after an intentional hash-function change:
    //   1. Run this test, capture the printed actual values
    //   2. Update the constants below
    //   3. Audit every caller that stored a recipe hash anywhere
    //      (Cipher entries, kernel bindings, federation caches)
    //   4. Bump the relevant wire versions if persistence exists
    {
        constexpr NumericalRecipe r_f32_strict =
            crucible::hashed(NumericalRecipe{
                .accum_dtype    = ScalarType::Float,
                .out_dtype      = ScalarType::Float,
                .reduction_algo = ReductionAlgo::PAIRWISE,
                .rounding       = RoundingMode::RN,
                .scale_policy   = ScalePolicy::NONE,
                .softmax        = SoftmaxRecurrence::ONLINE_LSE,
                .determinism    = ReductionDeterminism::BITEXACT_STRICT,
                .flags          = 0,
                .hash           = {},
            });
        constexpr NumericalRecipe r_f16_tc =
            crucible::hashed(NumericalRecipe{
                .accum_dtype    = ScalarType::Float,
                .out_dtype      = ScalarType::Half,
                .reduction_algo = ReductionAlgo::PAIRWISE,
                .rounding       = RoundingMode::RN,
                .scale_policy   = ScalePolicy::NONE,
                .softmax        = SoftmaxRecurrence::ONLINE_LSE,
                .determinism    = ReductionDeterminism::BITEXACT_TC,
                .flags          = 0,
                .hash           = {},
            });
        constexpr NumericalRecipe r_fp8_mx =
            crucible::hashed(NumericalRecipe{
                .accum_dtype    = ScalarType::Float,
                .out_dtype      = ScalarType::Float8_e4m3fn,
                .reduction_algo = ReductionAlgo::PAIRWISE,
                .rounding       = RoundingMode::RN,
                .scale_policy   = ScalePolicy::PER_BLOCK_MX,
                .softmax        = SoftmaxRecurrence::NAIVE,
                .determinism    = ReductionDeterminism::ORDERED,
                .flags          = 0,
                .hash           = {},
            });

        // ── GOLDEN VALUES (update atomically with any hash-fn change) ──
        constexpr uint64_t EXPECTED_F32_STRICT = 0xce0eb0cd5c376b79ULL;
        constexpr uint64_t EXPECTED_F16_TC     = 0xc737d38ea930d024ULL;
        constexpr uint64_t EXPECTED_FP8_MX     = 0x5ba4c6b1bdefc89dULL;

        if (r_f32_strict.hash.raw() != EXPECTED_F32_STRICT
            || r_f16_tc.hash.raw()     != EXPECTED_F16_TC
            || r_fp8_mx.hash.raw()     != EXPECTED_FP8_MX)
        {
            std::fprintf(stderr,
                "RECIPE-HASH DRIFT DETECTED\n"
                "  got  f32_strict=0x%016" PRIx64 "  expected 0x%016" PRIx64 "\n"
                "  got  f16_tc    =0x%016" PRIx64 "  expected 0x%016" PRIx64 "\n"
                "  got  fp8_mx    =0x%016" PRIx64 "  expected 0x%016" PRIx64 "\n"
                "  update the EXPECTED_* constants in test_numerical_recipe.cpp\n"
                "  and audit every persisted consumer of RecipeHash before\n"
                "  committing.\n",
                r_f32_strict.hash.raw(), EXPECTED_F32_STRICT,
                r_f16_tc.hash.raw(),     EXPECTED_F16_TC,
                r_fp8_mx.hash.raw(),     EXPECTED_FP8_MX);
            assert(false && "recipe-hash golden mismatch");
        }
    }

    // ── Hash uniqueness across the recipe space ────────────────────
    //
    // For the recipes the registry will expose, distinct semantic
    // fields must produce distinct hashes.  fmix64 has ~2^-64 per-
    // pair collision probability; any collision in this small set is
    // a definite bug, not bad luck.
    //
    // We enumerate a grid of every combinatorially-valid
    // (dtype pair, determinism, scale) tuple and verify no two
    // produce the same hash.
    {
        std::unordered_set<uint64_t> seen;
        unsigned checked = 0;

        const ScalarType dtypes[] = {
            ScalarType::Float, ScalarType::Half, ScalarType::BFloat16,
            ScalarType::Float8_e4m3fn, ScalarType::Float8_e5m2,
        };
        const ReductionDeterminism dets[] = {
            ReductionDeterminism::UNORDERED,
            ReductionDeterminism::ORDERED,
            ReductionDeterminism::BITEXACT_TC,
            ReductionDeterminism::BITEXACT_STRICT,
        };
        const ScalePolicy scales[] = {
            ScalePolicy::NONE,
            ScalePolicy::PER_TENSOR_POST,
            ScalePolicy::PER_BLOCK_MX,
            ScalePolicy::PER_BLOCK_NVFP4,
        };

        for (auto accum : dtypes) {
            for (auto out : dtypes) {
                for (auto det : dets) {
                    for (auto sp : scales) {
                        NumericalRecipe r{};
                        r.accum_dtype  = accum;
                        r.out_dtype    = out;
                        r.determinism  = det;
                        r.scale_policy = sp;
                        const uint64_t h = crucible::compute_recipe_hash(r).raw();
                        auto [it, inserted] = seen.insert(h);
                        assert(inserted && "recipe-hash collision in the grid");
                        ++checked;
                    }
                }
            }
        }

        // 5 dtypes × 5 dtypes × 4 determinism × 4 scale = 400 recipes
        assert(checked == 400);
        assert(seen.size() == 400);
    }

    // ── Hashes independent of non-semantic bits ────────────────────
    //
    // Defensive check: constructing a NumericalRecipe with
    // non-zero `hash` but identical semantic fields must produce the
    // same compute_recipe_hash output.  Prevents the `hash` field
    // from accidentally being folded in by a future refactor.
    {
        NumericalRecipe a{};
        NumericalRecipe b{};
        b.hash = crucible::RecipeHash{0xDEADBEEFCAFEBABEULL};  // poison
        assert(crucible::compute_recipe_hash(a) == crucible::compute_recipe_hash(b));
    }

    std::printf("test_numerical_recipe: all tests passed\n");
    return 0;
}
