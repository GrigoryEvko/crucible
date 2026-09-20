#pragma once

// The standard names for these are declared but not shipped by the standard
// library in use, so they are implemented here with the same semantics: the
// exact result clamped into the type's range.

#include <crucible/Platform.h>
// The umbrella header that re-exports these wrappers pulls in a header that
// includes this one, so including the umbrella here cycles. Instead: include
// the narrow substrate headers and re-open the wrapper namespace below with
// the using declarations this file needs. Naming one entity from two using
// declarations in one namespace is not a redeclaration, so the umbrella's own
// declarations stay compatible.
#include <crucible/safety/_DetSafe.h>
#include <crucible/safety/_Saturated.h>
#include <version>

namespace crucible::fixy::wrap {
using ::crucible::safety::DetSafe;
using ::crucible::safety::DetSafeTier_v;
using ::crucible::safety::Saturated;
using ::crucible::safety::add_sat_checked;
using ::crucible::safety::sub_sat_checked;
using ::crucible::safety::mul_sat_checked;
}  // namespace crucible::fixy::wrap

#include <concepts>
#include <limits>
#include <type_traits>

namespace crucible::sat {

template <std::integral T>
using DetSatPure =
    ::crucible::fixy::wrap::DetSafe<::crucible::fixy::wrap::DetSafeTier_v::Pure, ::crucible::fixy::wrap::Saturated<T>>;

template <std::integral T>
CRUCIBLE_CONST constexpr T add_sat(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // Which end it ran off is decided by the sign of the left
            // operand: a negative one can only have gone below the minimum.
            return (a < T{0}) ? std::numeric_limits<T>::min() : std::numeric_limits<T>::max();
        } else {
            return std::numeric_limits<T>::max();
        }
    }
    return r;
}

template <std::integral T>
CRUCIBLE_CONST constexpr T sub_sat(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // As with addition, the sign of the left operand says which end
            // the result ran off.
            return (a < T{0}) ? std::numeric_limits<T>::min() : std::numeric_limits<T>::max();
        } else {
            // An unsigned difference can only run off the bottom.
            return std::numeric_limits<T>::min();
        }
    }
    return r;
}

template <std::integral T>
CRUCIBLE_CONST constexpr T mul_sat(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // The exact product is negative exactly when one operand is.
            const bool neg = (a < T{0}) != (b < T{0});
            return neg ? std::numeric_limits<T>::min() : std::numeric_limits<T>::max();
        } else {
            return std::numeric_limits<T>::max();
        }
    }
    return r;
}

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> add_sat_det(T a, T b) noexcept {
    return DetSatPure<T>{::crucible::fixy::wrap::add_sat_checked(a, b)};
}

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> sub_sat_det(T a, T b) noexcept {
    return DetSatPure<T>{::crucible::fixy::wrap::sub_sat_checked(a, b)};
}

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> mul_sat_det(T a, T b) noexcept {
    return DetSatPure<T>{::crucible::fixy::wrap::mul_sat_checked(a, b)};
}

template <std::integral T>
CRUCIBLE_PURE constexpr ::crucible::fixy::wrap::Saturated<T> add_sat_from(T const& counter, T value) noexcept {
    return ::crucible::fixy::wrap::add_sat_checked(counter, value);
}

template <std::integral T>
CRUCIBLE_PURE constexpr ::crucible::fixy::wrap::Saturated<T> sub_sat_from(T const& counter, T value) noexcept {
    return ::crucible::fixy::wrap::sub_sat_checked(counter, value);
}

template <std::integral T>
CRUCIBLE_PURE constexpr ::crucible::fixy::wrap::Saturated<T> mul_sat_from(T const& counter, T value) noexcept {
    return ::crucible::fixy::wrap::mul_sat_checked(counter, value);
}

template <std::integral T>
[[nodiscard]] constexpr ::crucible::fixy::wrap::Saturated<T> add_sat_into(T& dest, T value) noexcept {
    auto result = add_sat_from(dest, value);
    dest = result.value();
    return result;
}

template <std::integral T>
[[nodiscard]] constexpr ::crucible::fixy::wrap::Saturated<T> sub_sat_into(T& dest, T value) noexcept {
    auto result = sub_sat_from(dest, value);
    dest = result.value();
    return result;
}

template <std::integral T>
[[nodiscard]] constexpr ::crucible::fixy::wrap::Saturated<T> mul_sat_into(T& dest, T value) noexcept {
    auto result = mul_sat_from(dest, value);
    dest = result.value();
    return result;
}

}  // namespace crucible::sat
