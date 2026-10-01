#pragma once

// Saturating arithmetic that keeps what the plain helpers throw away.
//
// The three helpers that return a plain T live in foundation/Saturate.h.
// The nine below return a fixy wrapper, Saturated<T> or the DetSafe band
// over it, and the layer line keeps a fixy type out of foundation.  So
// the nine live here.  This header re-exports the three plain helpers,
// so fixy::sat holds all twelve under one name.
//
//   *_sat_det   the checked result inside DetSafe<Pure, ...>: the value
//               is a function of its operands alone, and the pin says so
//   *_sat_from  the checked result of a counter and a step, the counter
//               untouched
//   *_sat_into  the same, written back into the counter
//
// Each *_sat_det wraps the checked operation of Saturated.h in the band.
// Each *_sat_from is that checked operation read through a reference.
// Each *_sat_into is *_sat_from followed by the write-back.  The band is
// built at its door, mint_band in fixy/Bands.h, which names the Pure
// tier at each site.

#include <fixy/Bands.h>
#include <fixy/Saturated.h>
#include <foundation/Platform.h>
#include <foundation/Saturate.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace fixy::sat {

using ::foundation::sat::add_sat;
using ::foundation::sat::mul_sat;
using ::foundation::sat::sub_sat;

template <std::integral T>
using DetSatPure = DetSafe<DetSafeTier_v::Pure, Saturated<T>>;

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> add_sat_det(T a, T b) noexcept {
    return mint_band<DetSatPure<T>>(add_sat_checked(a, b));
}

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> sub_sat_det(T a, T b) noexcept {
    return mint_band<DetSatPure<T>>(sub_sat_checked(a, b));
}

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> mul_sat_det(T a, T b) noexcept {
    return mint_band<DetSatPure<T>>(mul_sat_checked(a, b));
}

template <std::integral T>
CRUCIBLE_PURE constexpr Saturated<T> add_sat_from(T const& counter, T value) noexcept {
    return add_sat_checked(counter, value);
}

template <std::integral T>
CRUCIBLE_PURE constexpr Saturated<T> sub_sat_from(T const& counter, T value) noexcept {
    return sub_sat_checked(counter, value);
}

template <std::integral T>
CRUCIBLE_PURE constexpr Saturated<T> mul_sat_from(T const& counter, T value) noexcept {
    return mul_sat_checked(counter, value);
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> add_sat_into(T& dest, T value) noexcept {
    auto result = add_sat_from(dest, value);
    dest = result.value();
    return result;
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> sub_sat_into(T& dest, T value) noexcept {
    auto result = sub_sat_from(dest, value);
    dest = result.value();
    return result;
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> mul_sat_into(T& dest, T value) noexcept {
    auto result = mul_sat_from(dest, value);
    dest = result.value();
    return result;
}

}  // namespace fixy::sat
