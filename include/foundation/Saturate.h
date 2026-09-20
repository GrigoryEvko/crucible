#pragma once

// Saturating integer arithmetic: the exact result clamped into the
// type's range.  The library ships these, and each helper here is one
// call to it.  The names are kept because the call sites read better
// unqualified, and because the library spells them saturating_add
// rather than the add_sat of the paper: libstdc++ 16 declares
// saturating_add, saturating_sub, saturating_mul, saturating_div and
// saturating_cast in <numeric>, and defines no add_sat at all.
//
// Only the three bare helpers live here.  The wrapped forms that return
// a Saturated<T> or a DetSafe<Pure, Saturated<T>> belong to the fixy
// layer, beside the wrappers they name.

#include <foundation/Platform.h>

#include <concepts>
#include <numeric>

namespace foundation::sat {

template <std::integral T>
CRUCIBLE_CONST constexpr T add_sat(T a, T b) noexcept {
    return std::saturating_add(a, b);
}

template <std::integral T>
CRUCIBLE_CONST constexpr T sub_sat(T a, T b) noexcept {
    return std::saturating_sub(a, b);
}

template <std::integral T>
CRUCIBLE_CONST constexpr T mul_sat(T a, T b) noexcept {
    return std::saturating_mul(a, b);
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

}  // namespace detail::saturate_self_test

}  // namespace foundation::sat
