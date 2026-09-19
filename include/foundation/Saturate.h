#pragma once

// Saturating integer arithmetic: the exact result clamped into the
// type's range.  The standard names these but the standard library in
// use does not ship them, so they are implemented here with the same
// semantics.
//
// Only the three bare helpers live here.  The wrapped forms that return
// a Saturated<T> or a DetSafe<Pure, Saturated<T>> belong to the fixy
// layer, beside the wrappers they name.

#include <foundation/Platform.h>

#include <concepts>
#include <limits>
#include <type_traits>

namespace foundation::sat {

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

namespace detail::saturate_self_test {

static_assert(add_sat<unsigned char>(250, 10) == 255);
static_assert(add_sat<signed char>(120, 10) == 127);
static_assert(add_sat<signed char>(-120, -10) == -128);
static_assert(add_sat<int>(1, 2) == 3);
static_assert(sub_sat<unsigned char>(5, 10) == 0);
static_assert(sub_sat<signed char>(-120, 10) == -128);
static_assert(sub_sat<signed char>(120, -10) == 127);
static_assert(mul_sat<unsigned char>(16, 16) == 255);
static_assert(mul_sat<signed char>(-16, 16) == -128);
static_assert(mul_sat<signed char>(-16, -16) == 127);
static_assert(mul_sat<long long>(3, 4) == 12);

// The overflow branch is reached with non-constant operands here, so
// the builtin's runtime form and the clamp are both exercised.
inline void runtime_smoke_test() {
    unsigned char lo = 250;  // deliberately not constexpr
    unsigned char step = 10;
    [[maybe_unused]] unsigned char clamped_up = add_sat(lo, step);
    [[maybe_unused]] unsigned char clamped_down = sub_sat(step, lo);
    [[maybe_unused]] unsigned char clamped_mul = mul_sat(lo, step);
    int x = 1000000;
    [[maybe_unused]] int sum = add_sat(x, x);
    [[maybe_unused]] int prod = mul_sat(x, x);
}

}  // namespace detail::saturate_self_test

}  // namespace foundation::sat
