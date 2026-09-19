#pragma once

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include <crucible/RecipePool.h>
#include <crucible/Types.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace crucible {

// Ordinals appear in persisted blobs, so a new error is appended and an
// existing one is never renumbered. A hash that fails to resolve means either
// that the blob was written by a build knowing a recipe this one does not, or
// that the blob is corrupt. The two are indistinguishable from here.
enum class RecipeError : uint8_t {
    NameNotFound = 1,
    HashNotFound = 2,
    ToleranceMismatch = 3,
};

// Each tier here is the strongest one that dtype's 1-ULP error budget can
// sustain. A coarser budget must never be mapped into a per-dtype 1-ULP class,
// because that is exactly what the admission gate downstream exists to reject.
[[nodiscard, gnu::const]] constexpr fixy::wrap::Tolerance tolerance_for_dtype(ScalarType dtype) noexcept {
    switch (dtype) {
        case ScalarType::Double:
            return fixy::wrap::Tolerance::ULP_FP64;
        case ScalarType::Float:
            return fixy::wrap::Tolerance::ULP_FP32;
        case ScalarType::Half:
            return fixy::wrap::Tolerance::ULP_FP16;
        case ScalarType::BFloat16:
            return fixy::wrap::Tolerance::ULP_FP16;
        case ScalarType::Float8_e4m3fn:
            return fixy::wrap::Tolerance::ULP_FP8;
        case ScalarType::Float8_e5m2:
            return fixy::wrap::Tolerance::ULP_FP8;
        case ScalarType::Char:
        case ScalarType::Byte:
            return fixy::wrap::Tolerance::ULP_INT8;
        default:
            return fixy::wrap::Tolerance::RELAXED;
    }
}

// The strongest tolerance class a recipe is admitted to claim.
//
// gnu::pure, not gnu::const: the fields are read through a reference. const
// promises the result depends on argument values alone with no memory access,
// and claiming it here lets the optimiser treat a preceding write to one of
// those fields as dead and read a value-initialised field instead.
[[nodiscard, gnu::pure]] constexpr fixy::wrap::Tolerance tolerance_of(NumericalRecipe const& r) noexcept {
    switch (r.determinism) {
        case ReductionDeterminism::BITEXACT_STRICT:
            return fixy::wrap::Tolerance::BITEXACT;
        case ReductionDeterminism::BITEXACT_TC:
            return tolerance_for_dtype(r.out_dtype);
        case ReductionDeterminism::ORDERED:
        case ReductionDeterminism::UNORDERED:
            // ORDERED bounds the error at 4 ULP, but every ULP_* class in the
            // lattice means 1 ULP at that precision. Mapping ORDERED onto the
            // dtype's ULP class would let a 4-ULP recipe satisfy a consumer
            // that asked for 1 ULP, so both land at the lattice bottom.
            return fixy::wrap::Tolerance::RELAXED;
        default:
            // Unreachable for the tiers above, and required by the build's
            // switch-default warning. A tier added later admits at the bottom
            // of the lattice until its tolerance class is mapped explicitly.
            return fixy::wrap::Tolerance::RELAXED;
    }
}

// One family per algorithm, and the families are siblings in the lattice
// rather than a chain: no algorithm is stronger than another. The lattice's
// wildcards exist for its own algebra and never describe a registry entry,
// which always names one concrete algorithm.
//
// gnu::pure, not gnu::const, for the same reason as tolerance_of.
[[nodiscard, gnu::pure]] constexpr fixy::wrap::RecipeFamily recipe_family_of(NumericalRecipe const& r) noexcept {
    switch (r.reduction_algo) {
        case ReductionAlgo::PAIRWISE:
            return fixy::wrap::RecipeFamily::Pairwise;
        case ReductionAlgo::LINEAR:
            return fixy::wrap::RecipeFamily::Linear;
        case ReductionAlgo::KAHAN:
            return fixy::wrap::RecipeFamily::Kahan;
        case ReductionAlgo::BLOCK_STABLE:
            return fixy::wrap::RecipeFamily::BlockStable;
        default:
            // Unreachable for the algorithms above, and required by the
            // build's switch-default warning. An algorithm added later lands
            // at the lattice bottom, which subsumes nothing, so any specific
            // family request is refused until the mapping is written.
            return fixy::wrap::RecipeFamily::None;
    }
}

// Written once at construction and read-only afterwards, so concurrent
// lookups race only against each other and need no synchronisation.
class CRUCIBLE_OWNER RecipeRegistry {
public:
    using PoolBorrow = fixy::wrap::BorrowedRef<RecipePool>;
    using pure_projection_row = effects::Row<>;

    static_assert(fixy::wrap::IsBorrowedRef<PoolBorrow>);

    // The name points into read-only storage and the recipe into the pool's
    // arena, so neither is owned here and both outlive the registry.
    struct Entry {
        std::string_view name;
        const NumericalRecipe* recipe = nullptr;
    };

    using Entries = fixy::wrap::Tagged<std::span<const Entry>, fixy::tags::source::JsonRegistry>;

    static constexpr std::size_t STARTER_COUNT = 8;

    // The pool stays owned by the caller and must outlive the registry: after
    // construction the registry holds only non-owning pointers into it, and
    // never mutates or destroys it.
    [[gnu::cold]] explicit RecipeRegistry(PoolBorrow pool, effects::Alloc a) noexcept;

    RecipeRegistry(const RecipeRegistry&) =
        delete("RecipeRegistry holds interior recipe pointers into the caller's pool arena");
    RecipeRegistry& operator=(const RecipeRegistry&) =
        delete("RecipeRegistry holds interior recipe pointers into the caller's pool arena");
    RecipeRegistry(RecipeRegistry&&) = delete("interior pointers would dangle");
    RecipeRegistry& operator=(RecipeRegistry&&) = delete("interior pointers would dangle");

    // Comparison is exact and case-sensitive. The whole table fits in a couple
    // of cache lines, so a linear scan beats hashing the name.
    template <typename CallerRow = pure_projection_row>
        requires effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] std::expected<const NumericalRecipe*, RecipeError>
    by_name(std::string_view name) const noexcept;

    // Resolves a hash recovered from persisted state back to the recipe in
    // this process. Substituting a default on a miss is wrong: it silently
    // replays the run under different numerics. The miss has to abort the load
    // and say which recipe could not be resolved.
    template <typename CallerRow = pure_projection_row>
        requires effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] std::expected<const NumericalRecipe*, RecipeError> by_hash(RecipeHash hash) const noexcept;

    // Order matches the starter table's declaration order and is stable for
    // the lifetime of the registry.
    template <typename CallerRow = pure_projection_row>
        requires effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] Entries entries() const noexcept CRUCIBLE_LIFETIMEBOUND {
        return Entries{std::span<const Entry>{entries_.data(), STARTER_COUNT}};
    }

    [[nodiscard, gnu::const]] static constexpr std::size_t size() noexcept { return STARTER_COUNT; }

    // Lookups that also carry the tolerance tier in the return type, so a
    // consumer pinned to a tier cannot be handed a recipe that fails it.
    //
    // The lattice runs from RELAXED at the bottom to BITEXACT at the top, and
    // leq(a, b) reads "a is at or below b". The admission test is therefore
    // leq(requested, actual): a recipe making a stronger promise satisfies a
    // consumer asking for a weaker one, never the reverse. A request at the
    // bottom always succeeds. A failure to resolve the name or hash is
    // reported ahead of any tier mismatch.

    template <fixy::wrap::Tolerance T, typename CallerRow = pure_projection_row>
        requires effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]]
    std::expected<fixy::wrap::NumericalTier<T, const NumericalRecipe*>, RecipeError>
    by_name_pinned(std::string_view name) const noexcept {
        auto base = by_name<CallerRow>(name);
        if (!base) return std::unexpected(base.error());
        const NumericalRecipe* recipe = *base;
        if (!fixy::wrap::ToleranceLattice::leq(T, tolerance_of(*recipe))) {
            return std::unexpected(RecipeError::ToleranceMismatch);
        }
        return fixy::wrap::NumericalTier<T, const NumericalRecipe*>{recipe};
    }

    template <fixy::wrap::Tolerance T, typename CallerRow = pure_projection_row>
        requires effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]]
    std::expected<fixy::wrap::NumericalTier<T, const NumericalRecipe*>, RecipeError>
    by_hash_pinned(RecipeHash hash) const noexcept {
        auto base = by_hash<CallerRow>(hash);
        if (!base) return std::unexpected(base.error());
        const NumericalRecipe* recipe = *base;
        if (!fixy::wrap::ToleranceLattice::leq(T, tolerance_of(*recipe))) {
            return std::unexpected(RecipeError::ToleranceMismatch);
        }
        return fixy::wrap::NumericalTier<T, const NumericalRecipe*>{recipe};
    }

    // Lookups that carry both axes as runtime state instead of pinning one in
    // the type, so the caller decides admission. The second axis catches what
    // the tier alone cannot: two recipes can sit in the same tolerance band
    // and still use different reduction algorithms, and a consumer that needs
    // a specific one gets the wrong answer with no numerical warning.

    template <typename CallerRow = pure_projection_row>
        requires effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]]
    std::expected<fixy::wrap::RecipeSpec<const NumericalRecipe*>, RecipeError>
    by_name_spec(std::string_view name) const noexcept {
        auto base = by_name<CallerRow>(name);
        if (!base) return std::unexpected(base.error());
        const NumericalRecipe* recipe = *base;
        return fixy::wrap::RecipeSpec<const NumericalRecipe*>{recipe, tolerance_of(*recipe), recipe_family_of(*recipe)};
    }

    template <typename CallerRow = pure_projection_row>
        requires effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]]
    std::expected<fixy::wrap::RecipeSpec<const NumericalRecipe*>, RecipeError>
    by_hash_spec(RecipeHash hash) const noexcept {
        auto base = by_hash<CallerRow>(hash);
        if (!base) return std::unexpected(base.error());
        const NumericalRecipe* recipe = *base;
        return fixy::wrap::RecipeSpec<const NumericalRecipe*>{recipe, tolerance_of(*recipe), recipe_family_of(*recipe)};
    }

private:
    std::array<Entry, STARTER_COUNT> entries_{};
};

// These strings are persisted by anything that pins a recipe by name, so
// renaming one breaks every stored reference to it. The spelling is
// storage-dtype, then accumulator qualifier, then determinism tier.

namespace recipe_names {
inline constexpr std::string_view kF32Strict = "f32_strict";
inline constexpr std::string_view kF32Ordered = "f32_ordered";
inline constexpr std::string_view kF16F32AccumTc = "f16_f32accum_tc";
inline constexpr std::string_view kF16F32AccumOrdered = "f16_f32accum_ordered";
inline constexpr std::string_view kBf16F32AccumTc = "bf16_f32accum_tc";
inline constexpr std::string_view kBf16F32AccumOrdered = "bf16_f32accum_ordered";
inline constexpr std::string_view kFp8E4m3F32AccumMxOrd = "fp8e4m3_f32accum_mx_ordered";
inline constexpr std::string_view kFp8E5m2F32AccumMxOrd = "fp8e5m2_f32accum_mx_ordered";
}  // namespace recipe_names

namespace detail_recipe_registry {

struct StarterSpec {
    std::string_view name;
    NumericalRecipe fields;
};

// A cross-section of the determinism tiers against the common storage dtypes.
inline constexpr std::array<StarterSpec, RecipeRegistry::STARTER_COUNT> kStarterRecipes = {{
    {recipe_names::kF32Strict, hashed(NumericalRecipe{
                                   .accum_dtype = ScalarType::Float,
                                   .out_dtype = ScalarType::Float,
                                   .reduction_algo = ReductionAlgo::PAIRWISE,
                                   .rounding = RoundingMode::RN,
                                   .scale_policy = ScalePolicy::NONE,
                                   .softmax = SoftmaxRecurrence::ONLINE_LSE,
                                   .determinism = ReductionDeterminism::BITEXACT_STRICT,
                                   .flags = {},
                                   .hash = {},
                               })},
    {recipe_names::kF32Ordered, hashed(NumericalRecipe{
                                    .accum_dtype = ScalarType::Float,
                                    .out_dtype = ScalarType::Float,
                                    .reduction_algo = ReductionAlgo::PAIRWISE,
                                    .rounding = RoundingMode::RN,
                                    .scale_policy = ScalePolicy::NONE,
                                    .softmax = SoftmaxRecurrence::ONLINE_LSE,
                                    .determinism = ReductionDeterminism::ORDERED,
                                    .flags = {},
                                    .hash = {},
                                })},
    {recipe_names::kF16F32AccumTc, hashed(NumericalRecipe{
                                       .accum_dtype = ScalarType::Float,
                                       .out_dtype = ScalarType::Half,
                                       .reduction_algo = ReductionAlgo::PAIRWISE,
                                       .rounding = RoundingMode::RN,
                                       .scale_policy = ScalePolicy::NONE,
                                       .softmax = SoftmaxRecurrence::ONLINE_LSE,
                                       .determinism = ReductionDeterminism::BITEXACT_TC,
                                       .flags = {},
                                       .hash = {},
                                   })},
    {recipe_names::kF16F32AccumOrdered, hashed(NumericalRecipe{
                                            .accum_dtype = ScalarType::Float,
                                            .out_dtype = ScalarType::Half,
                                            .reduction_algo = ReductionAlgo::PAIRWISE,
                                            .rounding = RoundingMode::RN,
                                            .scale_policy = ScalePolicy::NONE,
                                            .softmax = SoftmaxRecurrence::ONLINE_LSE,
                                            .determinism = ReductionDeterminism::ORDERED,
                                            .flags = {},
                                            .hash = {},
                                        })},
    {recipe_names::kBf16F32AccumTc, hashed(NumericalRecipe{
                                        .accum_dtype = ScalarType::Float,
                                        .out_dtype = ScalarType::BFloat16,
                                        .reduction_algo = ReductionAlgo::PAIRWISE,
                                        .rounding = RoundingMode::RN,
                                        .scale_policy = ScalePolicy::NONE,
                                        .softmax = SoftmaxRecurrence::ONLINE_LSE,
                                        .determinism = ReductionDeterminism::BITEXACT_TC,
                                        .flags = {},
                                        .hash = {},
                                    })},
    {recipe_names::kBf16F32AccumOrdered, hashed(NumericalRecipe{
                                             .accum_dtype = ScalarType::Float,
                                             .out_dtype = ScalarType::BFloat16,
                                             .reduction_algo = ReductionAlgo::PAIRWISE,
                                             .rounding = RoundingMode::RN,
                                             .scale_policy = ScalePolicy::NONE,
                                             .softmax = SoftmaxRecurrence::ONLINE_LSE,
                                             .determinism = ReductionDeterminism::ORDERED,
                                             .flags = {},
                                             .hash = {},
                                         })},
    // Softmax is naive rather than online because a block-scaled format does
    // not compose with a running log-sum-exp. Determinism cannot be better
    // than ordered either: block-scale divergence alone exceeds one ULP.
    {recipe_names::kFp8E4m3F32AccumMxOrd, hashed(NumericalRecipe{
                                              .accum_dtype = ScalarType::Float,
                                              .out_dtype = ScalarType::Float8_e4m3fn,
                                              .reduction_algo = ReductionAlgo::PAIRWISE,
                                              .rounding = RoundingMode::RN,
                                              .scale_policy = ScalePolicy::PER_BLOCK_MX,
                                              .softmax = SoftmaxRecurrence::NAIVE,
                                              .determinism = ReductionDeterminism::ORDERED,
                                              .flags = {},
                                              .hash = {},
                                          })},
    // The wider exponent pairs with the entry above for asymmetric training,
    // where gradients need the dynamic range and weights need the mantissa.
    {recipe_names::kFp8E5m2F32AccumMxOrd, hashed(NumericalRecipe{
                                              .accum_dtype = ScalarType::Float,
                                              .out_dtype = ScalarType::Float8_e5m2,
                                              .reduction_algo = ReductionAlgo::PAIRWISE,
                                              .rounding = RoundingMode::RN,
                                              .scale_policy = ScalePolicy::PER_BLOCK_MX,
                                              .softmax = SoftmaxRecurrence::NAIVE,
                                              .determinism = ReductionDeterminism::ORDERED,
                                              .flags = {},
                                              .hash = {},
                                          })},
}};

}  // namespace detail_recipe_registry

inline RecipeRegistry::RecipeRegistry(PoolBorrow pool, effects::Alloc a) noexcept {
    for (std::size_t i = 0; i < STARTER_COUNT; ++i) {
        const auto& spec = detail_recipe_registry::kStarterRecipes[i];
        entries_[i].name = spec.name;
        entries_[i].recipe = pool->intern(a, spec.fields);
    }
}

template <typename CallerRow>
    requires effects::Subrow<CallerRow, RecipeRegistry::pure_projection_row>
inline std::expected<const NumericalRecipe*, RecipeError>
RecipeRegistry::by_name(std::string_view name) const noexcept {
    for (const auto& e : entries_) {
        if (e.name == name) {
            return e.recipe;
        }
    }
    return std::unexpected(RecipeError::NameNotFound);
}

template <typename CallerRow>
    requires effects::Subrow<CallerRow, RecipeRegistry::pure_projection_row>
inline std::expected<const NumericalRecipe*, RecipeError> RecipeRegistry::by_hash(RecipeHash hash) const noexcept {
    // Comparing the stored hash is the identity the caller means, because
    // interning is what wrote it and it is authoritative from then on.
    for (const auto& e : entries_) {
        if (e.recipe->hash == hash) {
            return e.recipe;
        }
    }
    return std::unexpected(RecipeError::HashNotFound);
}

}  // namespace crucible
