#pragma once

#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/Types.h>
#include <fixy/Bands.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <utility>

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
[[nodiscard, gnu::const]] constexpr ::fixy::Tolerance tolerance_for_dtype(ScalarType dtype) noexcept {
    switch (dtype) {
        case ScalarType::Double:
            return ::fixy::Tolerance::ULP_FP64;
        case ScalarType::Float:
            return ::fixy::Tolerance::ULP_FP32;
        case ScalarType::Half:
            return ::fixy::Tolerance::ULP_FP16;
        case ScalarType::BFloat16:
            return ::fixy::Tolerance::ULP_FP16;
        case ScalarType::Float8_e4m3fn:
            return ::fixy::Tolerance::ULP_FP8;
        case ScalarType::Float8_e5m2:
            return ::fixy::Tolerance::ULP_FP8;
        case ScalarType::Char:
        case ScalarType::Byte:
            return ::fixy::Tolerance::ULP_INT8;
        default:
            return ::fixy::Tolerance::RELAXED;
    }
}

// The strongest tolerance class a recipe is admitted to claim.
//
// gnu::pure, not gnu::const: the fields are read through a reference. const
// promises the result depends on argument values alone with no memory access,
// and claiming it here lets the optimiser treat a preceding write to one of
// those fields as dead and read a value-initialised field instead.
[[nodiscard, gnu::pure]] constexpr ::fixy::Tolerance tolerance_of(NumericalRecipe const& r) noexcept {
    switch (r.determinism) {
        case ReductionDeterminism::BITEXACT_STRICT:
            return ::fixy::Tolerance::BITEXACT;
        case ReductionDeterminism::BITEXACT_TC:
            return tolerance_for_dtype(r.out_dtype);
        case ReductionDeterminism::ORDERED:
        case ReductionDeterminism::UNORDERED:
            // ORDERED bounds the error at 4 ULP, but every ULP_* class in the
            // lattice means 1 ULP at that precision. Mapping ORDERED onto the
            // dtype's ULP class would let a 4-ULP recipe satisfy a consumer
            // that asked for 1 ULP, so both land at the lattice bottom.
            return ::fixy::Tolerance::RELAXED;
        default:
            // Unreachable for the tiers above, and required by the build's
            // switch-default warning. A tier added later admits at the bottom
            // of the lattice until its tolerance class is mapped explicitly.
            return ::fixy::Tolerance::RELAXED;
    }
}

// One family per algorithm, and the families are siblings in the lattice
// rather than a chain: no algorithm is stronger than another. The lattice's
// wildcards exist for its own algebra and never describe a registry entry,
// which always names one concrete algorithm.
//
// gnu::pure, not gnu::const, for the same reason as tolerance_of.
[[nodiscard, gnu::pure]] constexpr ::fixy::RecipeFamily recipe_family_of(NumericalRecipe const& r) noexcept {
    switch (r.reduction_algo) {
        case ReductionAlgo::PAIRWISE:
            return ::fixy::RecipeFamily::Pairwise;
        case ReductionAlgo::LINEAR:
            return ::fixy::RecipeFamily::Linear;
        case ReductionAlgo::KAHAN:
            return ::fixy::RecipeFamily::Kahan;
        case ReductionAlgo::BLOCK_STABLE:
            return ::fixy::RecipeFamily::BlockStable;
        default:
            // Unreachable for the algorithms above, and required by the
            // build's switch-default warning. An algorithm added later lands
            // at the lattice bottom, which subsumes nothing, so any specific
            // family request is refused until the mapping is written.
            return ::fixy::RecipeFamily::None;
    }
}

// Written once at construction and read-only afterwards, so concurrent
// lookups race only against each other and need no synchronisation.
class CRUCIBLE_OWNER RecipeRegistry {
public:
    using PoolBorrow = ::fixy::BorrowedRef<RecipePool>;
    using pure_projection_row = ::foundation::effects::Row<>;

    static_assert(::fixy::IsBorrowedRef<PoolBorrow>);

    static constexpr std::size_t STARTER_COUNT = 8;

    // A recipe in the arena of the pool.  The pool outlives the registry.
    using RecipeRef = ::fixy::Ref<NumericalRecipe const>;

    // The name points into read-only storage and the recipe into the pool's
    // arena, so neither is owned here and both outlive the registry.
    struct Entry {
        ::fixy::TextView name{};
        RecipeRef recipe;
    };

    using Entries = ::fixy::Tagged<::fixy::View<Entry const, STARTER_COUNT>, ::fixy::tags::source::JsonRegistry>;

    // The pool stays owned by the caller and must outlive the registry: after
    // construction the registry holds only non-owning pointers into it, and
    // never mutates or destroys it.
    //
    // The pool comes through ::fixy::mint_borrowed_ref, and the constructor
    // refuses a borrow that names no brand.  Each mint site is a new brand,
    // so this constructor only forwards to the one body below.
    template <typename Brand>
        requires ::foundation::brand::IsFreshBrand<Brand>
    [[gnu::cold]] explicit RecipeRegistry(::fixy::BorrowedRef<RecipePool, Brand> pool,
                                          ::foundation::effects::Alloc a) noexcept
        : RecipeRegistry{erased_door_{}, PoolBorrow{pool}, a} {}

    RecipeRegistry(const RecipeRegistry&) =
        delete("RecipeRegistry holds interior recipe pointers into the caller's pool arena");
    RecipeRegistry& operator=(const RecipeRegistry&) =
        delete("RecipeRegistry holds interior recipe pointers into the caller's pool arena");
    RecipeRegistry(RecipeRegistry&&) = delete("interior pointers would dangle");
    RecipeRegistry& operator=(RecipeRegistry&&) = delete("interior pointers would dangle");

    // Comparison is exact and case-sensitive. The whole table fits in a couple
    // of cache lines, so a linear scan beats hashing the name.
    template <typename CallerRow = pure_projection_row>
        requires ::foundation::effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] ::fixy::Result<RecipeRef, RecipeError> by_name(::fixy::TextView name) const noexcept;

    // Resolves a hash recovered from persisted state back to the recipe in
    // this process. Substituting a default on a miss is wrong: it silently
    // replays the run under different numerics. The miss has to abort the load
    // and say which recipe could not be resolved.
    template <typename CallerRow = pure_projection_row>
        requires ::foundation::effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] ::fixy::Result<RecipeRef, RecipeError> by_hash(RecipeHash hash) const noexcept;

    // Order matches the starter table's declaration order and is stable for
    // the lifetime of the registry.
    //
    // The view points into a member of the registry.  A temporary registry
    // releases that member at the end of the statement, and the deleted
    // `const&&` twin refuses the read.
    template <typename CallerRow = pure_projection_row>
        requires ::foundation::effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] Entries entries() const& noexcept {
        return ::fixy::mint_tagged<::fixy::tags::source::JsonRegistry>(entries_.view());
    }

    template <typename CallerRow = pure_projection_row>
        requires ::foundation::effects::Subrow<CallerRow, pure_projection_row>
    auto entries() const&& = delete("a temporary registry releases its entries at the end of the statement. Use a "
                                    "registry that has a name");

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

    template <::fixy::Tolerance T, typename CallerRow = pure_projection_row>
        requires ::foundation::effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] ::fixy::Result<::fixy::NumericalTier<T, RecipeRef>, RecipeError>
    by_name_pinned(::fixy::TextView name) const noexcept {
        return found_or_<::fixy::Result<::fixy::NumericalTier<T, RecipeRef>, RecipeError>>(
            find_name_(name), RecipeError::NameNotFound, [](RecipeRef recipe) noexcept { return pin_<T>(recipe); });
    }

    template <::fixy::Tolerance T, typename CallerRow = pure_projection_row>
        requires ::foundation::effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] ::fixy::Result<::fixy::NumericalTier<T, RecipeRef>, RecipeError>
    by_hash_pinned(RecipeHash hash) const noexcept {
        return found_or_<::fixy::Result<::fixy::NumericalTier<T, RecipeRef>, RecipeError>>(
            find_hash_(hash), RecipeError::HashNotFound, [](RecipeRef recipe) noexcept { return pin_<T>(recipe); });
    }

    // Lookups that carry both axes as runtime state instead of pinning one in
    // the type, so the caller decides admission. The second axis catches what
    // the tier alone cannot: two recipes can sit in the same tolerance band
    // and still use different reduction algorithms, and a consumer that needs
    // a specific one gets the wrong answer with no numerical warning.

    template <typename CallerRow = pure_projection_row>
        requires ::foundation::effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] ::fixy::Result<::fixy::RecipeSpec<RecipeRef>, RecipeError>
    by_name_spec(::fixy::TextView name) const noexcept {
        return found_or_<::fixy::Result<::fixy::RecipeSpec<RecipeRef>, RecipeError>>(
            find_name_(name), RecipeError::NameNotFound, [](RecipeRef recipe) noexcept { return spec_(recipe); });
    }

    template <typename CallerRow = pure_projection_row>
        requires ::foundation::effects::Subrow<CallerRow, pure_projection_row>
    [[nodiscard, gnu::pure]] ::fixy::Result<::fixy::RecipeSpec<RecipeRef>, RecipeError>
    by_hash_spec(RecipeHash hash) const noexcept {
        return found_or_<::fixy::Result<::fixy::RecipeSpec<RecipeRef>, RecipeError>>(
            find_hash_(hash), RecipeError::HashNotFound, [](RecipeRef recipe) noexcept { return spec_(recipe); });
    }

private:
    // Only the public constructor names this tag, so no caller reaches the
    // erased borrow below.
    struct erased_door_ {};

    [[gnu::cold]] RecipeRegistry(erased_door_, PoolBorrow pool, ::foundation::effects::Alloc a) noexcept;

    // The entry of the starter recipe at position I: its name, and the
    // recipe that the pool interns from its fields.
    template <std::size_t I>
    [[nodiscard]] static Entry intern_starter_(PoolBorrow pool, ::foundation::effects::Alloc a) noexcept;

    using Position = ::fixy::Index<STARTER_COUNT>;

    // The position of the entry with the name, or no value.  O(STARTER_COUNT).
    [[nodiscard]] ::fixy::Option<Position> find_name_(::fixy::TextView name) const noexcept {
        for (auto const [position, entry] : entries_.enumerate()) {
            if (entry->name == name) return ::fixy::some(position);
        }
        return ::fixy::none;
    }

    // The position of the entry whose recipe has the hash, or no value.
    // O(STARTER_COUNT).
    [[nodiscard]] ::fixy::Option<Position> find_hash_(RecipeHash hash) const noexcept {
        for (auto const [position, entry] : entries_.enumerate()) {
            if (entry->recipe->hash == hash) return ::fixy::some(position);
        }
        return ::fixy::none;
    }

    // make(recipe) for the recipe at the found position, or the error miss.
    template <class Found, class Make>
    [[nodiscard]] Found found_or_(::fixy::Option<Position> found, RecipeError miss, Make make) const noexcept {
        return found.match([&](Position position) noexcept -> Found { return make(entries_.at(position).recipe); },
                           [miss]() noexcept -> Found { return ::fixy::err(miss); });
    }

    // The recipe at the tier T, or ToleranceMismatch when the recipe
    // promises less than T.
    template <::fixy::Tolerance T>
    [[nodiscard]] static ::fixy::Result<::fixy::NumericalTier<T, RecipeRef>, RecipeError>
    pin_(RecipeRef recipe) noexcept {
        if (!::fixy::ToleranceLattice::leq(T, tolerance_of(*recipe))) {
            return ::fixy::err(RecipeError::ToleranceMismatch);
        }
        return ::fixy::mint_band<::fixy::NumericalTier<T, RecipeRef>>(recipe);
    }

    // The recipe with its tier and its family.
    [[nodiscard]] static ::fixy::RecipeSpec<RecipeRef> spec_(RecipeRef recipe) noexcept {
        return ::fixy::mint_recipe_spec(recipe, tolerance_of(*recipe), recipe_family_of(*recipe));
    }

    ::foundation::core::FixedArray<Entry, STARTER_COUNT> entries_;
};

// These strings are persisted by anything that pins a recipe by name, so
// renaming one breaks every stored reference to it. The spelling is
// storage-dtype, then accumulator qualifier, then determinism tier.  The
// suffix tc names the tensor-core path, whose tier is UNORDERED.

namespace recipe_names {
inline constexpr ::fixy::TextView kF32Strict = "f32_strict";
inline constexpr ::fixy::TextView kF32Ordered = "f32_ordered";
inline constexpr ::fixy::TextView kF16F32AccumTc = "f16_f32accum_tc";
inline constexpr ::fixy::TextView kF16F32AccumOrdered = "f16_f32accum_ordered";
inline constexpr ::fixy::TextView kBf16F32AccumTc = "bf16_f32accum_tc";
inline constexpr ::fixy::TextView kBf16F32AccumOrdered = "bf16_f32accum_ordered";
inline constexpr ::fixy::TextView kFp8E4m3F32AccumMxOrd = "fp8e4m3_f32accum_mx_ordered";
inline constexpr ::fixy::TextView kFp8E5m2F32AccumMxOrd = "fp8e5m2_f32accum_mx_ordered";
}  // namespace recipe_names

namespace detail_recipe_registry {

struct StarterSpec {
    ::fixy::TextView name{};
    NumericalRecipe fields{};
};

// A cross-section of the determinism tiers against the common storage dtypes.
//
// The two tc recipes run FP16 or BF16 fragments on tensor cores.  Each
// vendor and each generation fuses a different number of products, and
// truncates and rounds the fused sum in a different way.  So two chips give
// different bits for the same inputs, with no bound in units in the last
// place, and the tier of the two recipes is UNORDERED.
inline constexpr ::foundation::core::FixedArray<StarterSpec, RecipeRegistry::STARTER_COUNT> kStarterRecipes{
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
                                       .determinism = ReductionDeterminism::UNORDERED,
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
                                        .determinism = ReductionDeterminism::UNORDERED,
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
};

}  // namespace detail_recipe_registry

template <std::size_t I>
inline RecipeRegistry::Entry RecipeRegistry::intern_starter_(PoolBorrow pool, ::foundation::effects::Alloc a) noexcept {
    detail_recipe_registry::StarterSpec const& spec = detail_recipe_registry::kStarterRecipes.at(::fixy::index<I>());
    return Entry{.name = spec.name, .recipe = ::fixy::to(*pool->intern(a, spec.fields))};
}

// A braced list runs its elements in order, so the pool interns the starter
// recipes in the order of the table.
inline RecipeRegistry::RecipeRegistry(erased_door_, PoolBorrow pool, ::foundation::effects::Alloc a) noexcept
    : entries_{intern_starter_<0>(pool, a), intern_starter_<1>(pool, a), intern_starter_<2>(pool, a),
               intern_starter_<3>(pool, a), intern_starter_<4>(pool, a), intern_starter_<5>(pool, a),
               intern_starter_<6>(pool, a), intern_starter_<7>(pool, a)} {}

template <typename CallerRow>
    requires ::foundation::effects::Subrow<CallerRow, RecipeRegistry::pure_projection_row>
inline ::fixy::Result<RecipeRegistry::RecipeRef, RecipeError>
RecipeRegistry::by_name(::fixy::TextView name) const noexcept {
    return found_or_<::fixy::Result<RecipeRef, RecipeError>>(find_name_(name), RecipeError::NameNotFound,
                                                             [](RecipeRef recipe) noexcept { return recipe; });
}

template <typename CallerRow>
    requires ::foundation::effects::Subrow<CallerRow, RecipeRegistry::pure_projection_row>
inline ::fixy::Result<RecipeRegistry::RecipeRef, RecipeError> RecipeRegistry::by_hash(RecipeHash hash) const noexcept {
    // Comparing the stored hash is the identity the caller means, because
    // interning is what wrote it and it is authoritative from then on.
    return found_or_<::fixy::Result<RecipeRef, RecipeError>>(find_hash_(hash), RecipeError::HashNotFound,
                                                             [](RecipeRef recipe) noexcept { return recipe; });
}

}  // namespace crucible
