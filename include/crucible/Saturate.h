#pragma once

// ── Saturation arithmetic polyfill ──────────────────────────────────
//
// P0543 (C++26) adds std::add_sat / sub_sat / mul_sat.  GCC 16.0.1
// rawhide does not yet ship these in libstdc++ (__cpp_lib_saturation_
// arithmetic is undefined).  This header provides drop-in replacements
// in crucible::sat::* using __builtin_*_overflow.
//
// When the standard library catches up, #ifdef __cpp_lib_saturation_
// arithmetic can forward to std::*; until then we implement it.
//
// Semantics match P0543:
//   add_sat(a, b):  min(max(a + b, T_MIN), T_MAX)
//   sub_sat(a, b):  min(max(a - b, T_MIN), T_MAX)
//   mul_sat(a, b):  min(max(a * b, T_MIN), T_MAX)
//
// Runtime cost: one __builtin_*_overflow (single CMP + CMOV on x86-64,
// ~1 cycle) plus a branchless clamp.  Constexpr-capable.

#include <crucible/Platform.h>
#include <version>

#include <concepts>
#include <limits>
#include <type_traits>

namespace crucible::sat {

// gnu::const: takes two values, no memory access, no side effects.
// Optimizer may CSE freely across statements (no aliasing concerns).
// CRUCIBLE_CONST bundles [[nodiscard]] — a saturated arith result that
// is thrown away is almost certainly a bug.

template <std::integral T>
CRUCIBLE_CONST constexpr T add_sat(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // Signed overflow direction: if a >= 0 → wrapped low → clamp MAX;
            // if a < 0 → wrapped high → clamp MIN.
            return (a < T{0}) ? std::numeric_limits<T>::min()
                              : std::numeric_limits<T>::max();
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
            // a - b wraps low iff a < 0 and result overshot MIN; wraps high
            // iff a >= 0 and result overshot MAX.
            return (a < T{0}) ? std::numeric_limits<T>::min()
                              : std::numeric_limits<T>::max();
        } else {
            // Unsigned sub can only wrap below zero → clamp MIN (= 0).
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
            // Sign of the mathematical result: negative iff exactly one of
            // a, b is negative.  XOR of sign bits suffices.
            const bool neg = (a < T{0}) != (b < T{0});
            return neg ? std::numeric_limits<T>::min()
                       : std::numeric_limits<T>::max();
        } else {
            return std::numeric_limits<T>::max();
        }
    }
    return r;
}

} // namespace crucible::sat
