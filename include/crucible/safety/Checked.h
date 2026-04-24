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
#include <cstddef>
#include <cstdint>
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

// ═════════════════════════════════════════════════════════════════════
// ── Compile-time capacity arithmetic (#408 SAFEINT-C19, §19) ────────
// ═════════════════════════════════════════════════════════════════════
//
// Session-queue capacities are often computed at compile time:
//
//     using MyRing = MpmcRing<Job, NumProducers * BurstPerProducer>;
//
// A bare `*` is silently wrong if the product overflows the result
// type — the corrupted capacity propagates into ring-buffer sizing,
// arena layout, and downstream invariants.  The variable templates
// below promote the existing runtime `checked_add` / `checked_sub` /
// `checked_mul` primitives into compile-time-failing arithmetic so
// the bug becomes a build error with a framework-controlled
// diagnostic.
//
// ─── Routing: helper struct, not consteval-lambda ──────────────────
//
// The natural-looking
//   inline constexpr auto safe_capacity = []() consteval {
//       auto r = checked_mul<std::size_t>(A, B);
//       if (!r) static_assert(false, "...");   // ← always fires!
//       return *r;
//   }();
// has a subtle bug: `static_assert(false, msg)` inside a function
// body fires unconditionally on instantiation regardless of any
// surrounding `if`.  The robust idiom routes through a class-template
// helper whose body has a `static constexpr auto _opt = ...` member:
// the `static_assert(_opt.has_value(), ...)` then evaluates the
// constexpr optional and fires ONLY on actual overflow.
//
// ─── Use ────────────────────────────────────────────────────────────
//
//     // Type-parameterised:
//     using MyChannel = MpmcRing<Job,
//         safe_mul<std::size_t, NumProducers, BurstPerProducer>>;
//
//     // size_t convenience (canonical session-queue capacity case):
//     using MyChannel = MpmcRing<Job,
//         safe_capacity<NumProducers, BurstPerProducer>>;
//
// ─── Diagnostics ────────────────────────────────────────────────────
//
// Every overflow site fires a static_assert beginning with
// `[Checked_Capacity_Overflow]` so audit greps can find every
// compile-time arithmetic-failure site mechanically.

namespace detail {

template <std::integral T, T A, T B>
struct safe_add_impl {
    static constexpr auto _opt = checked_add<T>(A, B);
    static_assert(_opt.has_value(),
        "[Checked_Capacity_Overflow] safe_add: A + B overflows the "
        "destination integer type.  Pick a wider T or smaller operands.");
    static constexpr T value = *_opt;
};

template <std::integral T, T A, T B>
struct safe_sub_impl {
    static constexpr auto _opt = checked_sub<T>(A, B);
    static_assert(_opt.has_value(),
        "[Checked_Capacity_Overflow] safe_sub: A - B underflows the "
        "destination integer type.  For unsigned T, A must be >= B.");
    static constexpr T value = *_opt;
};

template <std::integral T, T A, T B>
struct safe_mul_impl {
    static constexpr auto _opt = checked_mul<T>(A, B);
    static_assert(_opt.has_value(),
        "[Checked_Capacity_Overflow] safe_mul: A * B overflows the "
        "destination integer type.  Pick a wider T or smaller operands.");
    static constexpr T value = *_opt;
};

}  // namespace detail

// Type-parameterised compile-time arithmetic — fails to compile on
// overflow with the [Checked_Capacity_Overflow] diagnostic prefix.

template <std::integral T, T A, T B>
inline constexpr T safe_add = detail::safe_add_impl<T, A, B>::value;

template <std::integral T, T A, T B>
inline constexpr T safe_sub = detail::safe_sub_impl<T, A, B>::value;

template <std::integral T, T A, T B>
inline constexpr T safe_mul = detail::safe_mul_impl<T, A, B>::value;

// ─── std::size_t convenience aliases ────────────────────────────────
//
// The vast majority of capacity-arithmetic sites use std::size_t as
// the destination type (queue capacities, arena byte budgets, ring
// sizes).  These aliases let call sites omit the type parameter
// entirely, keeping the channel-declaration boilerplate to a minimum.

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_capacity = safe_mul<std::size_t, A, B>;

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_byte_budget = safe_mul<std::size_t, A, B>;

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_size_sum = safe_add<std::size_t, A, B>;

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_size_diff = safe_sub<std::size_t, A, B>;

// ── Self-tests ─────────────────────────────────────────────────────
//
// Compile-time witnesses that the helpers fire on the expected
// inputs.  Negative cases (overflow rejection) live in the test/
// safety_neg/ harness because `static_assert(false, ...)` is exactly
// what we want to TRIGGER, and that has to live in a TU that's not
// part of the regular build.

static_assert(safe_add<std::uint32_t, 10u, 20u>                         == 30u);
static_assert(safe_sub<std::uint32_t, 30u, 20u>                         == 10u);
static_assert(safe_mul<std::uint32_t, 6u, 7u>                           == 42u);
static_assert(safe_capacity<8u, 16u>                                    == 128u);
static_assert(safe_capacity<std::size_t{1} << 16, std::size_t{1} << 16> == (std::size_t{1} << 32));
static_assert(safe_byte_budget<256u, 64u>                               == 256u * 64u);
static_assert(safe_size_sum<10u, 20u>                                   == 30u);
static_assert(safe_size_diff<30u, 10u>                                  == 20u);

// Edge: zero is fine in both directions.
static_assert(safe_mul<std::size_t, std::size_t{0}, std::size_t{1} << 60> == 0u);
static_assert(safe_add<std::size_t, std::size_t{0}, std::size_t{0}>       == 0u);

} // namespace crucible::safety
