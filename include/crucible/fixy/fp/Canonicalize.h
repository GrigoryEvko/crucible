#pragma once

#include <crucible/NumericalRecipe.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace crucible::fixy::fp {

// An IEEE 754 quiet NaN with an empty payload and a clear sign bit. The
// mantissa MSB set marks the NaN quiet, the clear sign bit picks one of the
// two signs a NaN may carry, and a zero payload erases the rest.
inline constexpr std::uint64_t kCanonicalQNaN64 = 0x7FF8000000000000ULL;
inline constexpr std::uint32_t kCanonicalQNaN32 = 0x7FC00000U;

[[nodiscard, gnu::const]]
constexpr std::uint64_t canonicalize(double x) noexcept {
    // IEEE 754 leaves a NaN's payload unspecified, so the same arithmetic
    // chain yields different NaN bit patterns on different silicon. Every
    // NaN projects to one pattern.
    if (std::isnan(x)) {
        return kCanonicalQNaN64;
    }
    // IEEE 754 gives +0.0 and -0.0 different bit patterns but compares them
    // equal, so a reduction that lands on -0.0 one run and +0.0 the next
    // emits different bytes. Masking the sign bit detects both without an
    // equality compare on a float.
    const auto bits = std::bit_cast<std::uint64_t>(x);
    constexpr std::uint64_t kMagnitudeMask = 0x7FFFFFFFFFFFFFFFULL;
    if ((bits & kMagnitudeMask) == 0) {
        return std::uint64_t{0};
    }
    // Normals, subnormals and infinities each already have one well-defined
    // bit pattern, so they pass through.
    return bits;
}

[[nodiscard, gnu::const]]
constexpr std::uint32_t canonicalize(float x) noexcept {
    if (std::isnan(x)) {
        return kCanonicalQNaN32;
    }
    const auto bits = std::bit_cast<std::uint32_t>(x);
    constexpr std::uint32_t kMagnitudeMask = 0x7FFFFFFFU;
    if ((bits & kMagnitudeMask) == 0) {
        return std::uint32_t{0};
    }
    return bits;
}

// NumericalRecipe is not a structural type: a class-type template parameter
// requires every non-static data member of every subobject to be public, and
// the recipe's flags member keeps its underlying byte private. This
// projection carries only the two axes the gate reads, which makes it
// eligible as a template parameter.
struct CanonicalizeRecipeSpec {
    ::crucible::RoundingMode rounding = ::crucible::RoundingMode::RN;
    ::crucible::ReductionDeterminism determinism = ::crucible::ReductionDeterminism::ORDERED;

    constexpr CanonicalizeRecipeSpec() noexcept = default;
    constexpr CanonicalizeRecipeSpec(::crucible::RoundingMode r, ::crucible::ReductionDeterminism d) noexcept
        : rounding{r}, determinism{d} {}
    // Non-explicit on purpose so a call site can brace-project a recipe.
    constexpr CanonicalizeRecipeSpec(  // NOLINT(google-explicit-constructor)
        const ::crucible::NumericalRecipe& r) noexcept
        : rounding{r.rounding}, determinism{r.determinism} {}

    // A structural template parameter needs the defaulted comparison.
    constexpr auto operator<=>(const CanonicalizeRecipeSpec&) const = default;
};

template <CanonicalizeRecipeSpec Spec>
[[nodiscard, gnu::const]]
constexpr std::uint64_t canonicalize_for(double x) noexcept {
    static_assert(::crucible::is_bitexact(Spec.determinism), "fixy::fp::canonicalize_for<Spec> requires "
                                                             "ReductionDeterminism::BITEXACT_TC or BITEXACT_STRICT — "
                                                             "merkle-hashing a double under UNORDERED/ORDERED tiers "
                                                             "would lock content_hash to a non-portable bit pattern "
                                                             "(cross-vendor drift exceeds 1 ULP).");
    static_assert(Spec.rounding == ::crucible::RoundingMode::RN, "fixy::fp::canonicalize_for<Spec> requires "
                                                                 "RoundingMode::RN (round-to-nearest, ties to even) — "
                                                                 "merkle-folding under RZ/RM/RP would silently lock "
                                                                 "content_hash to a rounding mode no default kernel "
                                                                 "realizes.");
    return canonicalize(x);
}

template <CanonicalizeRecipeSpec Spec>
[[nodiscard, gnu::const]]
constexpr std::uint32_t canonicalize_for(float x) noexcept {
    static_assert(::crucible::is_bitexact(Spec.determinism), "fixy::fp::canonicalize_for<Spec> requires "
                                                             "ReductionDeterminism::BITEXACT_TC or BITEXACT_STRICT "
                                                             "for the float overload (same rationale as double).");
    static_assert(Spec.rounding == ::crucible::RoundingMode::RN, "fixy::fp::canonicalize_for<Spec> requires "
                                                                 "RoundingMode::RN for the float overload (same "
                                                                 "rationale as double).");
    return canonicalize(x);
}

namespace detail::fp_canonicalize_self_test {

static_assert(canonicalize(std::numeric_limits<double>::quiet_NaN()) == kCanonicalQNaN64,
              "cell (a): quiet NaN must project to canonical qNaN");
static_assert(canonicalize(std::numeric_limits<float>::quiet_NaN()) == kCanonicalQNaN32,
              "cell (a): quiet NaN (float) must project to canonical qNaN");
static_assert(canonicalize(std::bit_cast<double>(std::uint64_t{0x7FFABCDEF0123456ULL})) == kCanonicalQNaN64,
              "cell (a): custom-payload NaN must project to canonical qNaN");
static_assert(canonicalize(std::bit_cast<float>(std::uint32_t{0xFFC12345U})) == kCanonicalQNaN32,
              "cell (a): negative-sign NaN (float) must project to canonical qNaN");

static_assert(canonicalize(0.0) == 0, "cell (b): +0.0 must canonicalize to bit pattern 0");
static_assert(canonicalize(-0.0) == 0, "cell (b): -0.0 must canonicalize to bit pattern 0");
static_assert(canonicalize(0.0f) == 0, "cell (b): +0.0f must canonicalize to bit pattern 0");
static_assert(canonicalize(-0.0f) == 0, "cell (b): -0.0f must canonicalize to bit pattern 0");
static_assert(std::bit_cast<std::uint64_t>(-0.0) != 0, "cell (b) sanity: -0.0 raw bit pattern is non-zero");

static_assert(canonicalize(1.0) == std::bit_cast<std::uint64_t>(1.0), "cell (c): finite values pass through");
static_assert(canonicalize(-3.14) == std::bit_cast<std::uint64_t>(-3.14),
              "cell (c): negative finite values pass through");
static_assert(canonicalize(std::numeric_limits<double>::infinity())
                  == std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity()),
              "cell (c): +Inf passes through");
static_assert(canonicalize(-std::numeric_limits<double>::infinity())
                  == std::bit_cast<std::uint64_t>(-std::numeric_limits<double>::infinity()),
              "cell (c): -Inf passes through (sign preserved)");

inline constexpr CanonicalizeRecipeSpec kCanonicalSpec{
    ::crucible::RoundingMode::RN,
    ::crucible::ReductionDeterminism::BITEXACT_STRICT,
};
static_assert(canonicalize_for<kCanonicalSpec>(1.5) == std::bit_cast<std::uint64_t>(1.5),
              "cell (d): canonical spec accepts finite double");
static_assert(canonicalize_for<kCanonicalSpec>(-0.0) == 0, "cell (d): canonical spec still canonicalizes ±0");
static_assert(canonicalize_for<kCanonicalSpec>(2.5f) == std::bit_cast<std::uint32_t>(2.5f),
              "cell (d): canonical spec accepts finite float");
inline constexpr ::crucible::NumericalRecipe kCanonicalRecipe{
    .reduction_algo = ::crucible::ReductionAlgo::PAIRWISE,
    .rounding = ::crucible::RoundingMode::RN,
    .determinism = ::crucible::ReductionDeterminism::BITEXACT_TC,
    .hash = ::crucible::RecipeHash{},
};
static_assert(canonicalize_for<CanonicalizeRecipeSpec{kCanonicalRecipe}>(3.5) == std::bit_cast<std::uint64_t>(3.5),
              "cell (d): projection ctor from NumericalRecipe");

}  // namespace detail::fp_canonicalize_self_test

}  // namespace crucible::fixy::fp
