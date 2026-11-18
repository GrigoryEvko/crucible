#pragma once

// The projection that makes a floating-point value safe to fold into a
// content hash.  IEEE 754 leaves a NaN's payload unspecified and gives
// the two zeros different bit patterns, so the same arithmetic chain can
// emit different bytes on different silicon or on two runs of the same
// binary.  Every NaN projects to one quiet pattern and both zeros
// project to zero.
//
// Design notes:
//
//  1. The gate reads two axes.  The rounding mode is
//     ::foundation::core::RoundingMode, the type of the field rounding of a
//     recipe of crucible/NumericalRecipe.h as well.  The determinism tier is
//     the enum of this header, because crucible/NumericalRecipe.h is a
//     chain header above this layer, and fixy cannot include it.
//     CanonicalizeRecipeSpec below carries the two axes and nothing else,
//     and the crucible side builds the spec at its own boundary.
//
//  2. The enumerator values of the determinism tier are pinned.  Crucible
//     converts the field of a recipe into this spec, and the cheapest
//     correct conversion is a cast, which is only correct while the
//     ordinals agree.  The cells of
//     test/layer/checks/fixy/fp/Canonicalize.cpp pin every value and the
//     cardinality of each enum, so a reordering here or there is a build
//     error rather than a silently different spec.
//
//  3. There is no converting constructor from a recipe, because the
//     recipe type is not visible here.  The spec's two-argument
//     constructor is what the crucible side calls.

#include <foundation/core/FpMode.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy::fp {

// The rounding mode a kernel realizes is ::foundation::core::RoundingMode,
// the vocabulary of the base in foundation/core/FpMode.h.  A recipe of
// crucible names the same type, per design note 1.

// How much reordering a reduction may do.  Same enumerators and same
// values as the crucible ReductionDeterminism enum, per design note 2.
enum class ReductionDeterminism : std::uint8_t {
    UNORDERED = 0,
    ORDERED = 1,
    BITEXACT_TC = 2,
    BITEXACT_STRICT = 3,
};

// Only BITEXACT_STRICT promises the same bytes on each vendor and each run.
// BITEXACT_TC keeps tensor cores, and FP16 and BF16 fragments give
// different bits on different vendors and generations.  The tier holds
// only for exact fragments, and the spec does not record the fragment
// kind.  Thus this header does not count BITEXACT_TC as bit-exact.
[[nodiscard, gnu::const]]
constexpr bool is_bitexact(ReductionDeterminism d) noexcept {
    return d == ReductionDeterminism::BITEXACT_STRICT;
}

// An IEEE 754 quiet NaN with an empty payload and a clear sign bit.  The
// mantissa MSB set marks the NaN quiet, the clear sign bit picks one of
// the two signs a NaN may carry, and a zero payload erases the rest.
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

// A class-type template parameter needs every non-static data member of
// every subobject to be public, which is why this carries the two axes as
// plain fields rather than referring to a recipe: the recipe's flags
// member keeps its underlying byte private and is not a structural type.
struct CanonicalizeRecipeSpec {
    ::foundation::core::RoundingMode rounding = ::foundation::core::RoundingMode::RN;
    ReductionDeterminism determinism = ReductionDeterminism::ORDERED;

    constexpr CanonicalizeRecipeSpec() noexcept = default;
    constexpr CanonicalizeRecipeSpec(::foundation::core::RoundingMode r, ReductionDeterminism d) noexcept
        : rounding{r}, determinism{d} {}

    // A structural template parameter needs the defaulted comparison.
    constexpr auto operator<=>(const CanonicalizeRecipeSpec&) const = default;
};

template <CanonicalizeRecipeSpec Spec>
[[nodiscard, gnu::const]]
constexpr std::uint64_t canonicalize_for(double x) noexcept {
    static_assert(is_bitexact(Spec.determinism), "fixy::fp::canonicalize_for<Spec> requires "
                                                 "ReductionDeterminism::BITEXACT_STRICT. Under a different "
                                                 "tier, a different vendor or run can give different bits, "
                                                 "and the content hash of the value is not portable.");
    static_assert(Spec.rounding == ::foundation::core::RoundingMode::RN,
                  "fixy::fp::canonicalize_for<Spec> requires "
                  "RoundingMode::RN (round-to-nearest, ties to even) — "
                  "merkle-folding under RZ/RM/RP would silently lock "
                  "content_hash to a rounding mode no default kernel "
                  "realizes.");
    return canonicalize(x);
}

template <CanonicalizeRecipeSpec Spec>
[[nodiscard, gnu::const]]
constexpr std::uint32_t canonicalize_for(float x) noexcept {
    static_assert(is_bitexact(Spec.determinism), "fixy::fp::canonicalize_for<Spec> requires "
                                                 "ReductionDeterminism::BITEXACT_STRICT for the float "
                                                 "overload. Under a different tier, a different vendor or "
                                                 "run can give different bits, and the content hash of the "
                                                 "value is not portable.");
    static_assert(Spec.rounding == ::foundation::core::RoundingMode::RN,
                  "fixy::fp::canonicalize_for<Spec> requires "
                  "RoundingMode::RN for the float overload (same "
                  "rationale as double).");
    return canonicalize(x);
}

}  // namespace fixy::fp
