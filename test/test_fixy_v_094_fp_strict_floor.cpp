// A compile definition is the only in-language witness that the strict
// floating-point floor reached this translation unit. If it is absent the
// floor is not engaged for downstream consumers, and the cross-vendor
// bit-exactness invariants lose their guarantee without any build failure.

#include <crucible/fixy/fp/_Canonicalize.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error \
    "CRUCIBLE_FP_STRICT_FLOOR is not defined. The strict-FP interface library is not transitively linked, or its compile definitions did not propagate. The cross-vendor invariants are at risk."
#endif

static_assert(CRUCIBLE_FP_STRICT_FLOOR == 1, "CRUCIBLE_FP_STRICT_FLOOR must be defined to 1. Any other "
                                             "value is a downgrade of the floor.");

// __FAST_MATH__ is set whenever -ffast-math, or any flag implying it, is
// active. Its absence is necessary but not sufficient proof that the
// strict pack is engaged.
#ifdef __FAST_MATH__
#error \
    "__FAST_MATH__ is defined. -ffast-math is active on this translation unit, and the strict floor's -fno-fast-math did not suppress it."
#endif

// __FINITE_MATH_ONLY__ is the narrower no-NaN-no-Inf claim. The
// canonicalization below relies on it being false.
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ != 0
#error \
    "__FINITE_MATH_ONLY__ is defined and non-zero. std::isnan branches are dead-code-eliminated, so canonicalize() produces wrong results."
#endif

void witness_canonicalize_under_floor() {
    // The payload is volatile to defeat constant folding. If
    // -ffinite-math-only were active despite the checks above, the
    // optimizer would replace the isnan test inside canonicalize with
    // false and return the raw bits instead of the canonical quiet NaN.
    volatile std::uint64_t nan_payload = 0x7FF1234567890ABCULL;
    const double nan_value = std::bit_cast<double>(nan_payload);

    const auto canonical = crucible::fixy::fp::canonicalize(nan_value);
    if (canonical != crucible::fixy::fp::kCanonicalQNaN64) {
        std::fprintf(stderr,
                     "fp-strict floor: canonicalize(NaN) returned "
                     "0x%016lx, expected 0x%016lx. The floor is not engaged "
                     "on this translation unit despite the compile "
                     "definition being set.\n",
                     canonical, crucible::fixy::fp::kCanonicalQNaN64);
        std::abort();
    }

    // The negative-zero collapse below only means anything while signed
    // zeros are preserved, that is, while -fno-signed-zeros is off.
    volatile double neg_zero_v = -0.0;
    const double neg_zero = neg_zero_v;
    const auto raw_neg_zero = std::bit_cast<std::uint64_t>(neg_zero);
    if (raw_neg_zero == 0) {
        std::fprintf(stderr, "fp-strict floor: the -0.0 bit pattern is zero on "
                             "this translation unit. -fno-signed-zeros is active "
                             "and the signed-zero canonicalization is broken.\n");
        std::abort();
    }
    if (crucible::fixy::fp::canonicalize(neg_zero) != 0) {
        std::fprintf(stderr, "fp-strict floor: canonicalize(-0.0) returned "
                             "non-zero. The signed-zero projection failed.\n");
        std::abort();
    }
}

}  // namespace

int main() {
    witness_canonicalize_under_floor();
    return 0;
}
