#pragma once
// FIXY-V-093 — `fixy::fp::canonicalize` for Merkle-hash-safe FP
// canonicalization at IR001 nodes.
//
// PROBLEM (Agent 5 Bug 7).  An `f64` or `f32` scalar that flows into
// a merkle hash (RegionNode content_hash, LoopNode termination,
// SymbolTable range bounds, NumericalRecipe scale factors) must
// serialize to the SAME bit pattern across compilers, ISAs, and
// `-ffp-contract` settings.  Two divergence sources break that:
//
//   (a) NaN payload entropy.  IEEE 754 leaves the lower 51 bits of a
//       NaN's mantissa unspecified; libm and SIMD instructions on
//       NV / AM / Apple silicon all produce DIFFERENT NaN payloads
//       for the same arithmetic chain.  bit_cast<uint64_t>(NaN1) !=
//       bit_cast<uint64_t>(NaN2) ⟹ content_hash drift ⟹ KernelCache
//       miss ⟹ recompile ⟹ federation cache evict.  And quiet vs
//       signalling NaN bit (bit 51) toggles under `-fsignaling-nans`.
//
//   (b) ±0 entropy.  IEEE 754 distinguishes +0.0 (0x0000…0000) from
//       -0.0 (0x8000…0000) bitwise BUT compares them equal.  An FP
//       reduction that touches a -0.0 once and a +0.0 the next run
//       (subtract-of-equal underflow direction differs across FMA-vs-
//       multiply-then-add lowerings) emits different bits to merkle.
//
// SOLUTION.  Project every double through `canonicalize`:
//
//   - any NaN bit pattern  →  the single canonical qNaN
//                              0x7FF8_0000_0000_0000 (double)
//                              0x7FC0_0000           (float)
//   - -0.0                 →  +0.0 (sign bit cleared)
//   - all other values     →  pass-through
//
// Result: a Merkle-stable uint64_t (or uint32_t) bit projection that
// agrees across NV / AM / TPU / Trainium / CPU oracle on every
// `BITEXACT_TC` and `BITEXACT_STRICT` recipe.
//
// RECIPE GATE (the load-bearing static_assert wall).  Calling
// `canonicalize_for<R>(x)` with a recipe `R` that does NOT promise
// cross-impl bit-stability is a compile error.  Two gates:
//
//   - R.determinism must be BITEXACT_TC or BITEXACT_STRICT.
//     UNORDERED and ORDERED recipes give multi-ULP cross-vendor
//     drift; folding a double into merkle under those tiers is a
//     correctness lie — different vendors would produce different
//     content_hash for the SAME logical recipe.
//
//   - R.rounding must be RoundingMode::RN (round-to-nearest, ties to
//     even, IEEE 754 default).  RZ / RM / RP are well-defined but
//     are NOT what any default kernel pipeline uses; folding their
//     scalars into merkle would silently lock the cache key to a
//     non-portable rounding mode.
//
// The recipe-free `canonicalize(x)` overload skips the gates — it's
// the building block for the gated form and stays callable from
// anywhere that has already proven recipe admissibility through some
// other channel (e.g. the recipe-checked branch of a Forge phase).
//
// NON-GOALS (V-093).  We do NOT:
//   - re-normalize subnormals (the bit pattern is well-defined; the
//     arithmetic divergence is a recipe concern, not a hashing one);
//   - touch ±Inf (well-defined bit pattern, no sign-zero entropy);
//   - quantize to lower precision (FP8/FP4 hashing routes through a
//     different surface — see FORGE.md §19.1 block-scaled formats).
//
// Migration of MerkleDag.h / SymbolTable.h call sites is the
// follow-on (#535 + #1030); V-093 ships the primitive + two
// distinct-mismatch-class neg-compile fixtures (HS14).
//
// Wrapper-axis assignment: this surface is a primitive of the
// DetSafe lattice (every output is grade-equivalent to its input
// modulo the canonical-projection equivalence class).  It does NOT
// itself synthesize a wrapper — it returns the raw bit pattern so
// downstream merkle folds can XOR/wymix directly.

#include <crucible/NumericalRecipe.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace crucible::fixy::fp {

// ─── Canonical bit patterns ─────────────────────────────────────────
//
// IEEE 754 qNaN with empty payload, positive sign:
//   double : 0 11111111111 1000000000000000000000000000000000000000000000000000
//   float  : 0 11111111    10000000000000000000000
//
// Choosing zero-payload + positive sign means:
//   - bit 51 (mantissa MSB) = 1 marks "quiet" (sNaN→qNaN promotion);
//   - bit 63 (sign) = 0 picks one of the two sign-of-NaN bits cross-
//     impl divergence sites encode;
//   - lower 51 bits = 0 erases all payload entropy.

inline constexpr std::uint64_t kCanonicalQNaN64 = 0x7FF8000000000000ULL;
inline constexpr std::uint32_t kCanonicalQNaN32 = 0x7FC00000U;

// ─── canonicalize (recipe-free overloads) ───────────────────────────
//
// `gnu::const`: pure function of one argument, no memory reads, no
// side effects.  The optimizer is free to CSE adjacent calls and to
// constant-fold at compile time (std::isnan + std::bit_cast are both
// constexpr in C++23).

[[nodiscard, gnu::const]]
constexpr std::uint64_t canonicalize(double x) noexcept {
    // NaN canonicalization: ALL NaN bit patterns project to the same
    // canonical qNaN.  std::isnan(x) is constexpr and the canonical
    // implementation reduces to "x != x" which the optimizer lowers
    // to a single FP-compare instruction.
    if (std::isnan(x)) {
        return kCanonicalQNaN64;
    }
    // ±0 canonicalization: +0.0 (0x0000…0000) and -0.0
    // (0x8000…0000) differ only in the sign bit.  Detect by masking
    // off bit 63 and checking the low 63 bits are zero.  Avoids the
    // `-Werror=float-equal` ban (the project disallows `x == 0.0` on
    // safety/footgun grounds — same rule, exact-zero is the one case
    // it would have been correct, and bit-mask detection is more
    // explicit anyway).
    const auto bits = std::bit_cast<std::uint64_t>(x);
    constexpr std::uint64_t kMagnitudeMask = 0x7FFFFFFFFFFFFFFFULL;
    if ((bits & kMagnitudeMask) == 0) {
        return std::uint64_t{0};
    }
    // Everything else (normals, subnormals, ±Inf): well-defined bit
    // pattern, pass through.
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

// ─── CanonicalizeRecipeSpec — structural NTTP projection ────────────
//
// `NumericalRecipe` itself is NOT a structural literal type per
// [temp.param]/7: its `flags` member is a `Bits<RecipeFlags>` whose
// underlying `bits_` byte is private (the public surface goes
// through `.raw()` / `.from_raw()`).  C++20 class-type NTTPs require
// every non-static data member of every nested subobject to be
// public, so `template <NumericalRecipe R>` is ill-formed.
//
// Solution: a structural projection capturing JUST the two recipe
// axes canonicalize cares about (determinism tier + rounding mode).
// Every other recipe field is irrelevant to FP-merkle admissibility,
// so the projection loses nothing and gains NTTP eligibility for
// free.  The implicit ctor from `const NumericalRecipe&` keeps call
// sites ergonomic — `canonicalize_for<{kRecipe}>(x)` projects in
// one brace.

struct CanonicalizeRecipeSpec {
    ::crucible::RoundingMode         rounding    = ::crucible::RoundingMode::RN;
    ::crucible::ReductionDeterminism determinism = ::crucible::ReductionDeterminism::ORDERED;

    constexpr CanonicalizeRecipeSpec() noexcept = default;
    constexpr CanonicalizeRecipeSpec(
        ::crucible::RoundingMode r,
        ::crucible::ReductionDeterminism d) noexcept
        : rounding{r}, determinism{d} {}
    // Project from a full NumericalRecipe (non-explicit so call sites
    // can write `canonicalize_for<{recipe}>(x)`).
    constexpr CanonicalizeRecipeSpec(  // NOLINT(google-explicit-constructor)
        const ::crucible::NumericalRecipe& r) noexcept
        : rounding{r.rounding}, determinism{r.determinism} {}

    // Defaulted spaceship — structural-equality NTTPs need it.
    constexpr auto operator<=>(const CanonicalizeRecipeSpec&) const = default;
};

// ─── canonicalize_for<Spec> (recipe-gated overloads) ────────────────
//
// Two gates, two distinct mismatch classes (the HS14 fixtures
// exercise BOTH):
//
//   - is_bitexact(Spec.determinism): recipe-tier admissibility.
//     UNORDERED / ORDERED recipes give multi-ULP cross-vendor drift;
//     folding their scalars into merkle is a correctness lie.
//
//   - Spec.rounding == RoundingMode::RN: rounding-mode pinning.
//     RZ / RM / RP are valid IEEE modes but their default-kernel
//     coverage in Mimic is RN only; merkle-folding under RZ would
//     silently lock content_hash to a rounding mode no production
//     kernel realizes.

template <CanonicalizeRecipeSpec Spec>
[[nodiscard, gnu::const]]
constexpr std::uint64_t canonicalize_for(double x) noexcept {
    static_assert(::crucible::is_bitexact(Spec.determinism),
                  "fixy::fp::canonicalize_for<Spec> requires "
                  "ReductionDeterminism::BITEXACT_TC or BITEXACT_STRICT — "
                  "merkle-hashing a double under UNORDERED/ORDERED tiers "
                  "would lock content_hash to a non-portable bit pattern "
                  "(cross-vendor drift exceeds 1 ULP). See "
                  "NumericalRecipe.h is_bitexact() + FORGE.md §19.1.");
    static_assert(Spec.rounding == ::crucible::RoundingMode::RN,
                  "fixy::fp::canonicalize_for<Spec> requires "
                  "RoundingMode::RN (round-to-nearest, ties to even) — "
                  "merkle-folding under RZ/RM/RP would silently lock "
                  "content_hash to a rounding mode no default kernel "
                  "realizes. See NumericalRecipe.h:55 + Mimic backend "
                  "rounding pinning.");
    return canonicalize(x);
}

template <CanonicalizeRecipeSpec Spec>
[[nodiscard, gnu::const]]
constexpr std::uint32_t canonicalize_for(float x) noexcept {
    static_assert(::crucible::is_bitexact(Spec.determinism),
                  "fixy::fp::canonicalize_for<Spec> requires "
                  "ReductionDeterminism::BITEXACT_TC or BITEXACT_STRICT "
                  "for the float overload (same rationale as double).");
    static_assert(Spec.rounding == ::crucible::RoundingMode::RN,
                  "fixy::fp::canonicalize_for<Spec> requires "
                  "RoundingMode::RN for the float overload (same "
                  "rationale as double).");
    return canonicalize(x);
}

// ─── Self-test (consteval-only, layered by property) ────────────────
//
// Embedded asserts witness the four bit-pattern projections at
// compile time so any future regression (a refactor that flips a
// sign-handling branch, mis-orders the NaN check, etc.) reds CI
// before runtime.  Layered as four cells:
//
//   (a) NaN→qNaN projection — every NaN flavor → kCanonicalQNaN.
//   (b) ±0→+0 projection — both signed-zero bit patterns → 0.
//   (c) Pass-through invariance for finite non-zero values.
//   (d) Recipe-gated overload accepts canonical BITEXACT_STRICT/RN.
//
// Float and double symmetry is asserted at each cell.

namespace detail::fp_canonicalize_self_test {

// Cell (a): NaN canonicalization. ALL NaN bit patterns project to
// the single canonical qNaN.
static_assert(canonicalize(std::numeric_limits<double>::quiet_NaN())
              == kCanonicalQNaN64,
              "V-093 cell (a): quiet NaN must project to canonical qNaN");
static_assert(canonicalize(std::numeric_limits<float>::quiet_NaN())
              == kCanonicalQNaN32,
              "V-093 cell (a): quiet NaN (float) must project to canonical qNaN");
// Custom NaN bit pattern (different payload) also projects:
static_assert(canonicalize(std::bit_cast<double>(
                  std::uint64_t{0x7FFABCDEF0123456ULL}))
              == kCanonicalQNaN64,
              "V-093 cell (a): custom-payload NaN must project to canonical qNaN");
static_assert(canonicalize(std::bit_cast<float>(
                  std::uint32_t{0xFFC12345U}))
              == kCanonicalQNaN32,
              "V-093 cell (a): negative-sign NaN (float) must project to canonical qNaN");

// Cell (b): ±0 canonicalization. Both signed zeros → +0.
static_assert(canonicalize(0.0) == 0,
              "V-093 cell (b): +0.0 must canonicalize to bit pattern 0");
static_assert(canonicalize(-0.0) == 0,
              "V-093 cell (b): -0.0 must canonicalize to bit pattern 0");
static_assert(canonicalize(0.0f) == 0,
              "V-093 cell (b): +0.0f must canonicalize to bit pattern 0");
static_assert(canonicalize(-0.0f) == 0,
              "V-093 cell (b): -0.0f must canonicalize to bit pattern 0");
// Sanity: -0.0 bit-cast WITHOUT canonicalize is NOT zero (proves the
// projection is doing real work, not a tautology).
static_assert(std::bit_cast<std::uint64_t>(-0.0) != 0,
              "V-093 cell (b) sanity: -0.0 raw bit pattern is non-zero");

// Cell (c): finite non-zero values pass through unchanged.
static_assert(canonicalize(1.0) == std::bit_cast<std::uint64_t>(1.0),
              "V-093 cell (c): finite values pass through");
static_assert(canonicalize(-3.14) == std::bit_cast<std::uint64_t>(-3.14),
              "V-093 cell (c): negative finite values pass through");
static_assert(canonicalize(std::numeric_limits<double>::infinity())
              == std::bit_cast<std::uint64_t>(
                  std::numeric_limits<double>::infinity()),
              "V-093 cell (c): +Inf passes through");
static_assert(canonicalize(-std::numeric_limits<double>::infinity())
              == std::bit_cast<std::uint64_t>(
                  -std::numeric_limits<double>::infinity()),
              "V-093 cell (c): -Inf passes through (sign preserved)");

// Cell (d): recipe-gated overload accepts a canonical
// BITEXACT_STRICT/RN spec and rejects nothing.  The negative side
// (spec NOT satisfying the gate is a compile error) is covered by
// the two fixy_neg/ fixtures.
inline constexpr CanonicalizeRecipeSpec kCanonicalSpec{
    ::crucible::RoundingMode::RN,
    ::crucible::ReductionDeterminism::BITEXACT_STRICT,
};
static_assert(canonicalize_for<kCanonicalSpec>(1.5) ==
              std::bit_cast<std::uint64_t>(1.5),
              "V-093 cell (d): canonical spec accepts finite double");
static_assert(canonicalize_for<kCanonicalSpec>(-0.0) == 0,
              "V-093 cell (d): canonical spec still canonicalizes ±0");
static_assert(canonicalize_for<kCanonicalSpec>(2.5f) ==
              std::bit_cast<std::uint32_t>(2.5f),
              "V-093 cell (d): canonical spec accepts finite float");
// Implicit-conversion path from a full NumericalRecipe — proves the
// projection ctor binds the recipe's two relevant fields into the
// structural NTTP-eligible Spec.
inline constexpr ::crucible::NumericalRecipe kCanonicalRecipe{
    .reduction_algo = ::crucible::ReductionAlgo::PAIRWISE,
    .rounding       = ::crucible::RoundingMode::RN,
    .determinism    = ::crucible::ReductionDeterminism::BITEXACT_TC,
    .hash           = ::crucible::RecipeHash{},
};
static_assert(canonicalize_for<CanonicalizeRecipeSpec{kCanonicalRecipe}>(3.5) ==
              std::bit_cast<std::uint64_t>(3.5),
              "V-093 cell (d): projection ctor from NumericalRecipe");

}  // namespace detail::fp_canonicalize_self_test

}  // namespace crucible::fixy::fp
