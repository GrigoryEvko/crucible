#pragma once

// The projection that makes a floating-point value safe to fold into a
// content hash.  IEEE 754 leaves a NaN's payload unspecified and gives
// the two zeros different bit patterns, so the same arithmetic chain can
// emit different bytes on different silicon or on two runs of the same
// binary.  Every NaN projects to one quiet pattern and both zeros
// project to zero.
//
// Old spelling: include/crucible/fixy/fp/Canonicalize.h.
//
// Deviations, each deliberate:
//
//  1. The two axes the gate reads are fixy's own enums.  The old header
//     included crucible/NumericalRecipe.h, a chain header above this
//     layer, purely to name RoundingMode and ReductionDeterminism and to
//     offer a converting constructor from a recipe.  fixy cannot include
//     it.  CanonicalizeRecipeSpec below carries the two axes and nothing
//     else, and the crucible side builds the spec at its own boundary.
//
//  2. The enumerator values are pinned.  Crucible converts a recipe's two
//     fields into this spec, and the cheapest correct conversion is a
//     cast, which is only correct while the ordinals agree.  The cells at
//     the foot of this header pin every value and the cardinality of each
//     enum, so a reordering here or there is a build error rather than a
//     silently different spec.  The values are the ones the crucible
//     enums arrived with.
//
//  3. There is no converting constructor from a recipe, because the type
//     it converted from is not visible here.  The spec's two-argument
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
// as the crucible RoundingMode enum, per deviation 2.
enum class RoundingMode : std::uint8_t {
    RN = 0,  // to nearest, ties to even
    RZ = 1,  // toward zero
    RM = 2,  // toward minus infinity
    RP = 3,  // toward plus infinity
};

// How much reordering a reduction may do.  Same enumerators and same
// values as the crucible ReductionDeterminism enum, per deviation 2.
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

namespace fixy::fp::detail::canonicalize_self_test {

// ── The pinned enumerator values (deviation 2) ───────────────────────
//
// Crucible converts its RoundingMode and ReductionDeterminism
// enums into the two above.  These cells
// are what make that conversion a cast rather than a switch.

static_assert(std::meta::enumerators_of(^^RoundingMode).size() == 4,
              "fixy::fp::RoundingMode drifted from four enumerators. The crucible enum it converts from "
              "has four; adding one here without adding it there makes the conversion lossy.");
static_assert(std::to_underlying(RoundingMode::RN) == 0);
static_assert(std::to_underlying(RoundingMode::RZ) == 1);
static_assert(std::to_underlying(RoundingMode::RM) == 2);
static_assert(std::to_underlying(RoundingMode::RP) == 3);

static_assert(std::meta::enumerators_of(^^ReductionDeterminism).size() == 4,
              "fixy::fp::ReductionDeterminism drifted from four enumerators. The crucible enum it "
              "converts from has four; adding one here without adding it there makes the conversion "
              "lossy.");
static_assert(std::to_underlying(ReductionDeterminism::UNORDERED) == 0);
static_assert(std::to_underlying(ReductionDeterminism::ORDERED) == 1);
static_assert(std::to_underlying(ReductionDeterminism::BITEXACT_TC) == 2);
static_assert(std::to_underlying(ReductionDeterminism::BITEXACT_STRICT) == 3);

// is_bitexact answers for every value of the enum, not just the two it
// admits.
static_assert(!is_bitexact(ReductionDeterminism::UNORDERED));
static_assert(!is_bitexact(ReductionDeterminism::ORDERED));
static_assert(is_bitexact(ReductionDeterminism::BITEXACT_TC));
static_assert(is_bitexact(ReductionDeterminism::BITEXACT_STRICT));

// ── The projection ──────────────────────────────────────────────────

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

// A subnormal has one well-defined pattern, so it passes through.  The
// old header claimed this in a comment and never pinned it.
static_assert(canonicalize(std::numeric_limits<double>::denorm_min())
                  == std::bit_cast<std::uint64_t>(std::numeric_limits<double>::denorm_min()),
              "cell (c): a subnormal passes through");

// ── The gated projection ────────────────────────────────────────────

inline constexpr CanonicalizeRecipeSpec kCanonicalSpec{
    RoundingMode::RN,
    ReductionDeterminism::BITEXACT_STRICT,
};
static_assert(canonicalize_for<kCanonicalSpec>(1.5) == std::bit_cast<std::uint64_t>(1.5),
              "cell (d): canonical spec accepts finite double");
static_assert(canonicalize_for<kCanonicalSpec>(-0.0) == 0, "cell (d): canonical spec still canonicalizes ±0");
static_assert(canonicalize_for<kCanonicalSpec>(2.5f) == std::bit_cast<std::uint32_t>(2.5f),
              "cell (d): canonical spec accepts finite float");

// The other bit-exact tier is admitted too, so the gate reads the
// property and not one value.
inline constexpr CanonicalizeRecipeSpec kTensorCoreSpec{
    RoundingMode::RN,
    ReductionDeterminism::BITEXACT_TC,
};
static_assert(canonicalize_for<kTensorCoreSpec>(3.5) == std::bit_cast<std::uint64_t>(3.5),
              "cell (d): BITEXACT_TC is admitted as well as BITEXACT_STRICT");

// Two specs with the same fields are the same template argument, which is
// what makes the spec usable as a cache-key component.
static_assert(kCanonicalSpec == CanonicalizeRecipeSpec{RoundingMode::RN, ReductionDeterminism::BITEXACT_STRICT});
static_assert(kCanonicalSpec != kTensorCoreSpec);

// The default spec is refused, which is the point of the gate: a caller
// has to say which tier it is folding under.
static_assert(!is_bitexact(CanonicalizeRecipeSpec{}.determinism),
              "the default spec must NOT satisfy the gate. A default that passed would let a caller fold "
              "a double into a content hash without naming a determinism tier.");

}  // namespace fixy::fp::detail::canonicalize_self_test
