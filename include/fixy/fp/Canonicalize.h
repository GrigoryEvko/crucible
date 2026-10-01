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
//  1. The two axes the gate reads are fixy's own enums.
//     crucible/NumericalRecipe.h names RoundingMode and
//     ReductionDeterminism, but it is a chain header above this layer,
//     and fixy cannot include it.  CanonicalizeRecipeSpec below carries
//     the two axes and nothing else, and the crucible side builds the
//     spec at its own boundary.
//
//  2. The enumerator values are pinned.  Crucible converts a recipe's two
//     fields into this spec, and the cheapest correct conversion is a
//     cast, which is only correct while the ordinals agree.  The cells of
//     test/layer/checks/fixy/fp/Canonicalize.cpp pin every value and the
//     cardinality of each enum, so a reordering here or there is a build
//     error rather than a silently different spec.  The values are the
//     values of the crucible enums.
//
//  3. There is no converting constructor from a recipe, because the
//     recipe type is not visible here.  The spec's two-argument
//     constructor is what the crucible side calls.

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy::fp {

// The rounding mode a kernel realizes.  Same enumerators and same values
// as the crucible RoundingMode enum, per design note 2.
enum class RoundingMode : std::uint8_t {
    RN = 0,  // to nearest, ties to even
    RZ = 1,  // toward zero
    RM = 2,  // toward minus infinity
    RP = 3,  // toward plus infinity
};

// How much reordering a reduction may do.  Same enumerators and same
// values as the crucible ReductionDeterminism enum, per design note 2.
enum class ReductionDeterminism : std::uint8_t {
    UNORDERED = 0,
    ORDERED = 1,
    BITEXACT_TC = 2,
    BITEXACT_STRICT = 3,
};

// The two bit-exact tiers are the ones that promise the same bytes twice.
// BITEXACT_TC constrains the fragment length and the outer reduction
// order rather than banning tensor cores; BITEXACT_STRICT bans them.
// Both are bit-exact, which is the only property this header reads.
[[nodiscard, gnu::const]]
constexpr bool is_bitexact(ReductionDeterminism d) noexcept {
    return d == ReductionDeterminism::BITEXACT_TC || d == ReductionDeterminism::BITEXACT_STRICT;
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
    RoundingMode rounding = RoundingMode::RN;
    ReductionDeterminism determinism = ReductionDeterminism::ORDERED;

    constexpr CanonicalizeRecipeSpec() noexcept = default;
    constexpr CanonicalizeRecipeSpec(RoundingMode r, ReductionDeterminism d) noexcept : rounding{r}, determinism{d} {}

    // A structural template parameter needs the defaulted comparison.
    constexpr auto operator<=>(const CanonicalizeRecipeSpec&) const = default;
};

template <CanonicalizeRecipeSpec Spec>
[[nodiscard, gnu::const]]
constexpr std::uint64_t canonicalize_for(double x) noexcept {
    static_assert(is_bitexact(Spec.determinism), "fixy::fp::canonicalize_for<Spec> requires "
                                                 "ReductionDeterminism::BITEXACT_TC or BITEXACT_STRICT — "
                                                 "merkle-hashing a double under UNORDERED/ORDERED tiers "
                                                 "would lock content_hash to a non-portable bit pattern "
                                                 "(cross-vendor drift exceeds 1 ULP).");
    static_assert(Spec.rounding == RoundingMode::RN, "fixy::fp::canonicalize_for<Spec> requires "
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
                                                 "ReductionDeterminism::BITEXACT_TC or BITEXACT_STRICT "
                                                 "for the float overload (same rationale as double).");
    static_assert(Spec.rounding == RoundingMode::RN, "fixy::fp::canonicalize_for<Spec> requires "
                                                     "RoundingMode::RN for the float overload (same "
                                                     "rationale as double).");
    return canonicalize(x);
}

}  // namespace fixy::fp
