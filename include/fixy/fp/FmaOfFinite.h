#pragma once

// fixy::fp::detail::fma_of_finite(a, b, c): a * b + c with one round to
// nearest even, as fp::fma of foundation/core/Float.h, with no canonical step
// for a NaN result.  The contract: the caller sends no NaN, or it
// canonicalizes the result itself.  For each input that is not a NaN the
// bits are the bits of fp::fma.  A NaN result has the NaN of the form of the
// target, which changes with the build and the call site.
//
// exp, log and erf of fixy/fp test a NaN at entry and return quiet_nan_of<T>
// before the first fma, and the polynomials of Polynomial.h state finite
// domains, so they call this door and pay no canonical step.  Each other
// caller calls fp::fma.  The door reads the target, so it is in the inline
// namespace CRUCIBLE_SIMD_TARGET, as fp::fma is.

#include <foundation/Platform.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error \
    "fixy/fp/FmaOfFinite.h needs the floor crucible_fp_strict (-ffp-contract=off). Without it the compiler can fuse a multiply and an add, and the bits change with the level."
#endif

namespace fixy::fp::detail {

inline namespace CRUCIBLE_SIMD_TARGET {

template <::foundation::core::Float T>
[[nodiscard, gnu::always_inline]] constexpr T fma_of_finite(T left, T right, T addend) noexcept {
    return ::foundation::core::detail::fma_of_target_(left, right, addend);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace fixy::fp::detail
