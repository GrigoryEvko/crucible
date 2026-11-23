#pragma once

// Saturating arithmetic that keeps what the plain forms throw away.
//
// The plain forms that return a T are saturating_add, saturating_sub and
// saturating_mul of the Scalar family (foundation/core/Arithmetic.h).
// The checked forms that return a Saturated<T> are add_sat_checked,
// sub_sat_checked and mul_sat_checked of fixy/Saturated.h.  The six below
// return or step a fixy wrapper too, and the layer line keeps a fixy type
// out of foundation.  So the six live here.
//
//   *_sat_det   the checked result inside DetSafe<Pure, ...>: the value
//               is a function of its operands alone, and the pin says so
//   *_sat_into  the checked result of a Saturated counter and a step,
//               written back into the counter.  The flag of the counter
//               stays set after a clamp
//
// Each *_sat_det wraps the checked operation of Saturated.h in the band.
// Each *_sat_into is that checked operation followed by the write-back.
// A read of a counter with no write-back is the checked operation itself.
// The band is built at its door, mint_band in fixy/Bands.h, which names
// the Pure tier at each site.

#include <fixy/Bands.h>
#include <fixy/Saturated.h>
#include <foundation/Platform.h>
#include <foundation/core/Number.h>

namespace fixy::sat {

template <::foundation::core::Integer T>
using DetSatPure = DetSafe<DetSafeTier_v::Pure, Saturated<T>>;

template <::foundation::core::Integer T>
CRUCIBLE_CONST constexpr DetSatPure<T> add_sat_det(T a, T b) noexcept {
    return mint_band<DetSatPure<T>>(add_sat_checked(a, b));
}

template <::foundation::core::Integer T>
CRUCIBLE_CONST constexpr DetSatPure<T> sub_sat_det(T a, T b) noexcept {
    return mint_band<DetSatPure<T>>(sub_sat_checked(a, b));
}

template <::foundation::core::Integer T>
CRUCIBLE_CONST constexpr DetSatPure<T> mul_sat_det(T a, T b) noexcept {
    return mint_band<DetSatPure<T>>(mul_sat_checked(a, b));
}

// Each *_sat_into steps a counter: the checked result of the counter and the
// step goes back into the counter through then(), so the flag of a counter
// that clamped one time stays set after each later step.

template <::foundation::core::Integer T>
constexpr void add_sat_into(Saturated<T>& counter, T step) noexcept {
    counter = counter.then(add_sat_checked(counter.value(), step));
}

template <::foundation::core::Integer T>
constexpr void sub_sat_into(Saturated<T>& counter, T step) noexcept {
    counter = counter.then(sub_sat_checked(counter.value(), step));
}

template <::foundation::core::Integer T>
constexpr void mul_sat_into(Saturated<T>& counter, T step) noexcept {
    counter = counter.then(mul_sat_checked(counter.value(), step));
}

}  // namespace fixy::sat
