// The compile-time checks of fixy/fp/Canonicalize.h.

#include <fixy/fp/Canonicalize.h>

#include <foundation/core/Number.h>
#include <foundation/core/Scalar.h>

namespace fixy::fp::detail::canonicalize_self_test {

using ::foundation::core::RoundingMode;

// ── The pinned enumerator values (design note 2) ─────────────────────
//
// A crucible recipe names foundation::core::RoundingMode itself, and it
// converts its ReductionDeterminism into the enum of
// fixy/fp/Canonicalize.h.  These cells are what make that conversion a cast
// rather than a switch.  The rounding cells pin the four IEEE directions
// that the FP control door encodes.

static_assert(std::meta::enumerators_of(^^::foundation::core::RoundingMode).size() == 4,
              "foundation::core::RoundingMode drifted from the four IEEE rounding directions.");
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

// is_bitexact answers for every value of the enum, not just the one it
// admits.  BITEXACT_TC is refused, because FP16 and BF16 tensor-core
// fragments give different bits on different vendors.
static_assert(!is_bitexact(ReductionDeterminism::UNORDERED));
static_assert(!is_bitexact(ReductionDeterminism::ORDERED));
static_assert(!is_bitexact(ReductionDeterminism::BITEXACT_TC));
static_assert(is_bitexact(ReductionDeterminism::BITEXACT_STRICT));

// ── The projection ──────────────────────────────────────────────────

static_assert(canonicalize(::foundation::core::quiet_nan_of<double>) == kCanonicalQNaN64,
              "cell (a): quiet NaN must project to canonical qNaN");
static_assert(canonicalize(::foundation::core::quiet_nan_of<float>) == kCanonicalQNaN32,
              "cell (a): quiet NaN (float) must project to canonical qNaN");
static_assert(canonicalize(::foundation::core::bit_cast<double>(std::uint64_t{0x7FFABCDEF0123456ULL}))
                  == kCanonicalQNaN64,
              "cell (a): custom-payload NaN must project to canonical qNaN");
static_assert(canonicalize(::foundation::core::bit_cast<float>(std::uint32_t{0xFFC12345U})) == kCanonicalQNaN32,
              "cell (a): negative-sign NaN (float) must project to canonical qNaN");

static_assert(canonicalize(0.0) == 0, "cell (b): +0.0 must canonicalize to bit pattern 0");
static_assert(canonicalize(-0.0) == 0, "cell (b): -0.0 must canonicalize to bit pattern 0");
static_assert(canonicalize(0.0f) == 0, "cell (b): +0.0f must canonicalize to bit pattern 0");
static_assert(canonicalize(-0.0f) == 0, "cell (b): -0.0f must canonicalize to bit pattern 0");
static_assert(::foundation::core::bit_cast<std::uint64_t>(-0.0) != 0,
              "cell (b) sanity: -0.0 raw bit pattern is non-zero");

static_assert(canonicalize(1.0) == ::foundation::core::bit_cast<std::uint64_t>(1.0),
              "cell (c): finite values pass through");
static_assert(canonicalize(-3.14) == ::foundation::core::bit_cast<std::uint64_t>(-3.14),
              "cell (c): negative finite values pass through");
static_assert(canonicalize(::foundation::core::infinity_of<double>)
                  == ::foundation::core::bit_cast<std::uint64_t>(::foundation::core::infinity_of<double>),
              "cell (c): +Inf passes through");
static_assert(canonicalize(-::foundation::core::infinity_of<double>)
                  == ::foundation::core::bit_cast<std::uint64_t>(-::foundation::core::infinity_of<double>),
              "cell (c): -Inf passes through (sign preserved)");

// A subnormal has one well-defined pattern, so it passes through.
static_assert(canonicalize(::foundation::core::min_subnormal_of<double>)
                  == ::foundation::core::bit_cast<std::uint64_t>(::foundation::core::min_subnormal_of<double>),
              "cell (c): a subnormal passes through");

// ── The gated projection ────────────────────────────────────────────

inline constexpr CanonicalizeRecipeSpec kCanonicalSpec{
    RoundingMode::RN,
    ReductionDeterminism::BITEXACT_STRICT,
};
static_assert(canonicalize_for<kCanonicalSpec>(1.5) == ::foundation::core::bit_cast<std::uint64_t>(1.5),
              "cell (d): canonical spec accepts finite double");
static_assert(canonicalize_for<kCanonicalSpec>(-0.0) == 0, "cell (d): canonical spec still canonicalizes ±0");
static_assert(canonicalize_for<kCanonicalSpec>(2.5f) == ::foundation::core::bit_cast<std::uint32_t>(2.5f),
              "cell (d): canonical spec accepts finite float");

// The tensor-core tier fails the gate.  The fixture
// neg_canonicalize_for_tensor_core_spec proves that canonicalize_for
// refuses it.
inline constexpr CanonicalizeRecipeSpec kTensorCoreSpec{
    RoundingMode::RN,
    ReductionDeterminism::BITEXACT_TC,
};
static_assert(!is_bitexact(kTensorCoreSpec.determinism),
              "cell (d): BITEXACT_TC must NOT satisfy the gate. FP16 and BF16 tensor-core fragments give "
              "different bits on different vendors.");

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
