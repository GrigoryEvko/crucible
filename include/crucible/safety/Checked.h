#pragma once

// ── crucible::safety::Checked ───────────────────────────────────────
//
// Overflow-mode arithmetic primitives.  Explicit choice at the call
// site for every operation that could overflow, divide by zero, or
// shift out of range.
//
//   Axiom coverage: TypeSafe, DetSafe.
//   Runtime cost:   overflow detection is a single flag check on
//                   x86-64 via __builtin_*_overflow.  Division and
//                   shift validation is a single comparison.
//
// Four modes per op:
//   checked_*     — returns std::optional<T>; nullopt on overflow/invalid
//   wrapping_*    — two's-complement wrap on overflow
//   trapping_*    — std::abort() on overflow
//   saturating_*  — clamp to T's min/max on overflow
//
// Supported ops: add, sub, mul, div, mod, neg, shl, shr, abs.

#include <crucible/Platform.h>
#include <crucible/Saturate.h>

#include <concepts>
#include <cstdlib>
#include <limits>
#include <optional>
#include <type_traits>

namespace crucible::safety {

// ── Checked (nullopt on overflow) ───────────────────────────────────

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_add(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] return std::nullopt;
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_sub(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] return std::nullopt;
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_mul(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] return std::nullopt;
    return r;
}

// Division: two overflow paths — divide by zero, and (for signed) MIN/-1.
template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_div(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]] return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]]
            return std::nullopt;
    }
    return static_cast<T>(a / b);
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_mod(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]] return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]]
            return T{0};  // mathematically defined, despite INT_MIN/-1 overflow
    }
    return static_cast<T>(a % b);
}

// Negation: INT_MIN negated overflows for signed.
template <std::signed_integral T>
[[nodiscard]] constexpr std::optional<T> checked_neg(T a) noexcept {
    if (a == std::numeric_limits<T>::min()) [[unlikely]] return std::nullopt;
    return static_cast<T>(-a);
}

template <std::signed_integral T>
[[nodiscard]] constexpr std::optional<T> checked_abs(T a) noexcept {
    if (a == std::numeric_limits<T>::min()) [[unlikely]] return std::nullopt;
    return static_cast<T>(a < T{0} ? -a : a);
}

// Shift: invalid if shift count >= bit width or negative.
template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_shl(T a, int shift) noexcept {
    if (shift < 0 || shift >= static_cast<int>(sizeof(T) * 8)) [[unlikely]]
        return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        // Left-shift of negative values is UB; reject.
        if (a < T{0}) [[unlikely]] return std::nullopt;
    }
    return static_cast<T>(a << shift);
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_shr(T a, int shift) noexcept {
    if (shift < 0 || shift >= static_cast<int>(sizeof(T) * 8)) [[unlikely]]
        return std::nullopt;
    return static_cast<T>(a >> shift);
}

// ── Wrapping (two's-complement wrap) ────────────────────────────────

template <std::integral T>
[[nodiscard]] constexpr T wrapping_add(T a, T b) noexcept {
    T r{};
    (void)__builtin_add_overflow(a, b, &r);
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T wrapping_sub(T a, T b) noexcept {
    T r{};
    (void)__builtin_sub_overflow(a, b, &r);
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T wrapping_mul(T a, T b) noexcept {
    T r{};
    (void)__builtin_mul_overflow(a, b, &r);
    return r;
}

// ── Trapping (abort on overflow) ────────────────────────────────────

template <std::integral T>
[[nodiscard]] constexpr T trapping_add(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_sub(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_mul(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_div(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]] std::abort();
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]] std::abort();
    }
    return static_cast<T>(a / b);
}

// ── Saturating (clamp to T's [min, max]) ────────────────────────────

template <std::integral T>
[[nodiscard]] constexpr T saturating_add(T a, T b) noexcept {
    return ::crucible::sat::add_sat(a, b);
}

template <std::integral T>
[[nodiscard]] constexpr T saturating_sub(T a, T b) noexcept {
    return ::crucible::sat::sub_sat(a, b);
}

template <std::integral T>
[[nodiscard]] constexpr T saturating_mul(T a, T b) noexcept {
    return ::crucible::sat::mul_sat(a, b);
}

} // namespace crucible::safety
