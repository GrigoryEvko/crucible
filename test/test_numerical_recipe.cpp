// A recipe is a description of how a reduction is allowed to be
// computed.  This file pins its layout, its defaults, the meaning of
// each determinism tier, and the stability and uniqueness of its hash.

#include <crucible/NumericalRecipe.h>

#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <type_traits>
#include <unordered_set>

int main() {
    using crucible::NumericalRecipe;
    using crucible::RecipeFlags;
    using crucible::ReductionAlgo;
    using crucible::ReductionDeterminism;
    using crucible::RoundingMode;
    using crucible::ScalarType;
    using crucible::ScalePolicy;
    using crucible::SoftmaxRecurrence;

    // The size is depended on elsewhere: the table that interns
    // recipes assumes it, and a plan hash reserves exactly this many
    // bytes per recipe it names.  A change here invalidates every
    // cached kernel hash.
    static_assert(sizeof(NumericalRecipe) == 16);
    static_assert(alignof(NumericalRecipe) == 16);
    static_assert(std::is_trivially_copyable_v<NumericalRecipe>);
    static_assert(std::is_standard_layout_v<NumericalRecipe>);

    // Any kernel that names no recipe of its own gets this one, so
    // these are the values the whole system falls back to.
    {
        NumericalRecipe r{};
        assert(r.accum_dtype == ScalarType::Float);
        assert(r.out_dtype == ScalarType::Undefined);
        assert(r.reduction_algo == ReductionAlgo::PAIRWISE);
        assert(r.rounding == RoundingMode::RN);
        assert(r.scale_policy == ScalePolicy::NONE);
        assert(r.softmax == SoftmaxRecurrence::ONLINE_LSE);
        assert(r.determinism == ReductionDeterminism::ORDERED);
        assert(r.flags.none());
        assert(r.hash.raw() == 0);  // filled in at interning, not construction
    }

    // The four tiers form a ladder.  The two weakest admit tensor
    // cores and block-scaled formats and promise no bit-exactness, one
    // of them additionally bounding the error.  The third keeps tensor
    // cores but drops block-scaled formats and promises agreement
    // within one unit in the last place across vendors.  The strictest
    // drops tensor cores as well and promises identical bytes on every
    // architecture.
    using D = ReductionDeterminism;

    assert(!crucible::is_bitexact(D::UNORDERED));
    assert(!crucible::is_bitexact(D::ORDERED));
    assert(crucible::is_bitexact(D::BITEXACT_TC));
    assert(crucible::is_bitexact(D::BITEXACT_STRICT));

    assert(crucible::permits_tensor_cores(D::UNORDERED));
    assert(crucible::permits_tensor_cores(D::ORDERED));
    assert(crucible::permits_tensor_cores(D::BITEXACT_TC));
    assert(!crucible::permits_tensor_cores(D::BITEXACT_STRICT));

    assert(crucible::allows_block_scaled_formats(D::UNORDERED));
    assert(crucible::allows_block_scaled_formats(D::ORDERED));
    assert(!crucible::allows_block_scaled_formats(D::BITEXACT_TC));
    assert(!crucible::allows_block_scaled_formats(D::BITEXACT_STRICT));

    {
        NumericalRecipe r{};
        r.determinism = ReductionDeterminism::BITEXACT_STRICT;
        // The two flags occupy bits zero and two, which is what the
        // raw value below pins.  Setting one field must leave the
        // others alone.
        r.flags.set(RecipeFlags::FLUSH_TO_ZERO);
        r.flags.set(RecipeFlags::ALLOW_DENORMAL);
        assert(r.accum_dtype == ScalarType::Float);
        assert(r.determinism == ReductionDeterminism::BITEXACT_STRICT);
        assert(r.flags.raw() == 0x05);
    }

    // The hash must not be computed over the hash field itself.  Were
    // it, a recipe would change identity every time it was written out
    // and read back.
    {
        NumericalRecipe fresh{};
        fresh.accum_dtype = ScalarType::Float;
        fresh.out_dtype = ScalarType::Half;
        fresh.determinism = ReductionDeterminism::BITEXACT_TC;

        const auto h_fresh = crucible::compute_recipe_hash(fresh);

        NumericalRecipe filled = fresh;
        filled.hash = h_fresh;
        const auto h_filled = crucible::compute_recipe_hash(filled);
        assert(h_fresh == h_filled && "hash must exclude the hash field");

        filled.hash = h_filled;
        assert(crucible::compute_recipe_hash(filled) == h_fresh);

        // Hashing a recipe that is already hashed changes nothing.
        const auto once = crucible::hashed(fresh);
        const auto twice = crucible::hashed(once);
        assert(once.hash == twice.hash);
        assert(once.hash == h_fresh);
    }

    // A recipe hash goes into the content hash of every kernel that
    // names the recipe, and into the cache keys those kernels are
    // stored under.  A change here invalidates every persisted
    // binding, so the values below are pinned rather than computed.
    // The three cover opposite corners of the tier and type space.
    //
    // Changing the hash function means updating these, auditing every
    // place a recipe hash was stored, and raising the version of any
    // format that holds one.
    {
        constexpr NumericalRecipe r_f32_strict = crucible::hashed(NumericalRecipe{
            .accum_dtype = ScalarType::Float,
            .out_dtype = ScalarType::Float,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding = RoundingMode::RN,
            .scale_policy = ScalePolicy::NONE,
            .softmax = SoftmaxRecurrence::ONLINE_LSE,
            .determinism = ReductionDeterminism::BITEXACT_STRICT,
            .flags = {},
            .hash = {},
        });
        constexpr NumericalRecipe r_f16_tc = crucible::hashed(NumericalRecipe{
            .accum_dtype = ScalarType::Float,
            .out_dtype = ScalarType::Half,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding = RoundingMode::RN,
            .scale_policy = ScalePolicy::NONE,
            .softmax = SoftmaxRecurrence::ONLINE_LSE,
            .determinism = ReductionDeterminism::BITEXACT_TC,
            .flags = {},
            .hash = {},
        });
        constexpr NumericalRecipe r_fp8_mx = crucible::hashed(NumericalRecipe{
            .accum_dtype = ScalarType::Float,
            .out_dtype = ScalarType::Float8_e4m3fn,
            .reduction_algo = ReductionAlgo::PAIRWISE,
            .rounding = RoundingMode::RN,
            .scale_policy = ScalePolicy::PER_BLOCK_MX,
            .softmax = SoftmaxRecurrence::NAIVE,
            .determinism = ReductionDeterminism::ORDERED,
            .flags = {},
            .hash = {},
        });

        constexpr uint64_t EXPECTED_F32_STRICT = 0xce0eb0cd5c376b79ULL;
        constexpr uint64_t EXPECTED_F16_TC = 0xc737d38ea930d024ULL;
        constexpr uint64_t EXPECTED_FP8_MX = 0x5ba4c6b1bdefc89dULL;

        if (r_f32_strict.hash.raw() != EXPECTED_F32_STRICT || r_f16_tc.hash.raw() != EXPECTED_F16_TC
            || r_fp8_mx.hash.raw() != EXPECTED_FP8_MX) {
            std::fprintf(stderr,
                         "RECIPE-HASH DRIFT DETECTED\n"
                         "  got  f32_strict=0x%016" PRIx64 "  expected 0x%016" PRIx64 "\n"
                         "  got  f16_tc    =0x%016" PRIx64 "  expected 0x%016" PRIx64 "\n"
                         "  got  fp8_mx    =0x%016" PRIx64 "  expected 0x%016" PRIx64 "\n"
                         "  update the EXPECTED_* constants in test_numerical_recipe.cpp\n"
                         "  and audit every persisted consumer of RecipeHash before\n"
                         "  committing.\n",
                         r_f32_strict.hash.raw(), EXPECTED_F32_STRICT, r_f16_tc.hash.raw(), EXPECTED_F16_TC,
                         r_fp8_mx.hash.raw(), EXPECTED_FP8_MX);
            assert(false && "recipe-hash golden mismatch");
        }
    }

    // Recipes that differ in any semantic field must hash apart.  A
    // chance collision within a set this small is so unlikely that one
    // here means the hash is wrong, not that the draw was unlucky.
    {
        std::unordered_set<uint64_t> seen;
        unsigned checked = 0;

        const ScalarType dtypes[] = {
            ScalarType::Float,         ScalarType::Half,        ScalarType::BFloat16,
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
                        r.accum_dtype = accum;
                        r.out_dtype = out;
                        r.determinism = det;
                        r.scale_policy = sp;
                        const uint64_t h = crucible::compute_recipe_hash(r).raw();
                        auto [it, inserted] = seen.insert(h);
                        assert(inserted && "recipe-hash collision in the grid");
                        ++checked;
                    }
                }
            }
        }

        // Five accumulate types by five output types by four
        // determinism tiers by four scale policies.
        assert(checked == 400);
        assert(seen.size() == 400);
    }

    // The same claim as the idempotence case above, approached from
    // the other side: a recipe carrying an arbitrary hash value hashes
    // the same as one carrying none.
    {
        NumericalRecipe a{};
        NumericalRecipe b{};
        b.hash = crucible::RecipeHash{0xDEADBEEFCAFEBABEULL};  // poison
        assert(crucible::compute_recipe_hash(a) == crucible::compute_recipe_hash(b));
    }

    std::printf("test_numerical_recipe: all tests passed\n");
    return 0;
}
