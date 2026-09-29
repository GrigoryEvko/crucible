#pragma once

// Arithmetic that names its overflow behaviour at the call site.

#include <foundation/Platform.h>
#include <foundation/Saturate.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <type_traits>

namespace fixy {

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_add(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]]
        return std::nullopt;
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_sub(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]]
        return std::nullopt;
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_mul(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]]
        return std::nullopt;
    return r;
}

// Dividing the most negative value by minus one has no representable
// quotient, which is the second failure alongside a zero divisor.
template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_div(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]]
        return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]]
            return std::nullopt;
    }
    return static_cast<T>(a / b);
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_mod(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]]
        return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]]
            // The remainder is zero here even though the quotient of the
            // same operands is not representable, so this pair succeeds
            // where the division above fails.
            return T{0};
    }
    return static_cast<T>(a % b);
}

// The most negative value has no positive counterpart.
template <std::signed_integral T>
[[nodiscard]] constexpr std::optional<T> checked_neg(T a) noexcept {
    if (a == std::numeric_limits<T>::min()) [[unlikely]]
        return std::nullopt;
    return static_cast<T>(-a);
}

template <std::signed_integral T>
[[nodiscard]] constexpr std::optional<T> checked_abs(T a) noexcept {
    if (a == std::numeric_limits<T>::min()) [[unlikely]]
        return std::nullopt;
    return static_cast<T>(a < T{0} ? -a : a);
}

// The exact value a * 2^shift, if T can hold it.  A shift count outside
// [0, width) gives nullopt, and so does a shift that moves a bit out of
// the width or changes the sign.  A negative value is not refused: since
// C++20 its left shift is defined, and -1 shifted by 3 is exactly -8.
template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_shl(T a, int shift) noexcept {
    using Bits = std::make_unsigned_t<T>;
    if (shift < 0 || shift >= std::numeric_limits<Bits>::digits) [[unlikely]]
        return std::nullopt;
    const T shifted = static_cast<T>(static_cast<Bits>(a) << shift);
    // The right shift of a signed value copies the sign bit, so it gives
    // back a only if the left shift lost no bit and kept the sign.
    if ((shifted >> shift) != a) [[unlikely]]
        return std::nullopt;
    return shifted;
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_shr(T a, int shift) noexcept {
    if (shift < 0 || shift >= static_cast<int>(sizeof(T) * 8)) [[unlikely]]
        return std::nullopt;
    return static_cast<T>(a >> shift);
}

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

template <std::integral T>
[[nodiscard]] constexpr T trapping_add(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]]
        std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_sub(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]]
        std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_mul(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]]
        std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_div(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]]
        std::abort();
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]]
            std::abort();
    }
    return static_cast<T>(a / b);
}

template <std::integral T>
[[nodiscard]] constexpr T saturating_add(T a, T b) noexcept {
    return ::foundation::sat::add_sat(a, b);
}

template <std::integral T>
[[nodiscard]] constexpr T saturating_sub(T a, T b) noexcept {
    return ::foundation::sat::sub_sat(a, b);
}

template <std::integral T>
[[nodiscard]] constexpr T saturating_mul(T a, T b) noexcept {
    return ::foundation::sat::mul_sat(a, b);
}

// Capacities are often computed at compile time, and a bare product
// that overflows the result type propagates a corrupted size into
// buffer sizing and layout.  The variable templates below turn that
// into a build failure.
//
// The obvious spelling, a consteval lambda holding
// `if (!r) static_assert(false, ...)`, does not work.  A static_assert
// with a false condition in a function body fires on instantiation
// whatever the surrounding branch says.  Routing through a class
// template instead puts the optional in a static member, and asserting
// on that member fires only on a real overflow.
//
// Every one of those assertions opens with a bracketed tag so an audit
// can find each compile-time arithmetic failure site by grep.
//
// When the optional is empty, its assertion stops the build.  The value
// then reads value_or and not operator*, so the report holds that one
// error and not a second one from inside <optional>.

namespace detail {

template <std::integral T, T A, T B>
struct safe_add_impl {
    static constexpr auto _opt = checked_add<T>(A, B);
    static_assert(_opt.has_value(), "[Checked_Capacity_Overflow] safe_add: A + B overflows the "
                                    "destination integer type.  Pick a wider T or smaller operands.");
    static constexpr T value = _opt.value_or(T{0});
};

template <std::integral T, T A, T B>
struct safe_sub_impl {
    static constexpr auto _opt = checked_sub<T>(A, B);
    static_assert(_opt.has_value(), "[Checked_Capacity_Overflow] safe_sub: A - B underflows the "
                                    "destination integer type.  For unsigned T, A must be >= B.");
    static constexpr T value = _opt.value_or(T{0});
};

template <std::integral T, T A, T B>
struct safe_mul_impl {
    static constexpr auto _opt = checked_mul<T>(A, B);
    static_assert(_opt.has_value(), "[Checked_Capacity_Overflow] safe_mul: A * B overflows the "
                                    "destination integer type.  Pick a wider T or smaller operands.");
    static constexpr T value = _opt.value_or(T{0});
};

}  // namespace detail

template <std::integral T, T A, T B>
inline constexpr T safe_add = detail::safe_add_impl<T, A, B>::value;

template <std::integral T, T A, T B>
inline constexpr T safe_sub = detail::safe_sub_impl<T, A, B>::value;

template <std::integral T, T A, T B>
inline constexpr T safe_mul = detail::safe_mul_impl<T, A, B>::value;

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_capacity = safe_mul<std::size_t, A, B>;

// A budget is rarely a single product.  A sum of terms, each of them a
// product, has an intermediate that can wrap and silently under-size the
// whole.  The helpers below carry the check through every step.

namespace detail {

template <std::integral T, T... Xs>
struct safe_add_all_impl;

template <std::integral T>
struct safe_add_all_impl<T> {
    static constexpr T value = T{0};
};

template <std::integral T, T X>
struct safe_add_all_impl<T, X> {
    static constexpr T value = X;
};

template <std::integral T, T X, T Y, T... Rest>
struct safe_add_all_impl<T, X, Y, Rest...> {
    static constexpr auto _opt = checked_add<T>(X, Y);
    static_assert(_opt.has_value(), "[Checked_Capacity_Overflow] safe_add_all: partial sum "
                                    "overflows the destination integer type.  One of the terms "
                                    "in the variadic sum exceeds the remaining budget; split the "
                                    "sum into smaller chunks, pick a wider T, or reduce an "
                                    "operand.");
    static constexpr T value = safe_add_all_impl<T, _opt.value_or(T{0}), Rest...>::value;
};

}  // namespace detail

template <std::integral T, T... Xs>
inline constexpr T safe_add_all = detail::safe_add_all_impl<T, Xs...>::value;

template <typename T, std::size_t N>
inline constexpr std::size_t safe_array_bytes = safe_mul<std::size_t, sizeof(T), N>;

// This is a plain sum of the element sizes and knows nothing about
// alignment.  A struct holding the same types can be larger, because the
// compiler inserts padding this total does not account for.
template <typename... Ts>
inline constexpr std::size_t safe_struct_bytes = safe_add_all<std::size_t, sizeof(Ts)...>;

template <std::size_t Budget, std::size_t Used>
inline constexpr bool bytes_fit_v = (Used <= Budget);

template <std::size_t Budget, std::size_t Used>
consteval void ensure_bytes_fit() noexcept {
    static_assert(bytes_fit_v<Budget, Used>, "[Byte_Budget_Exceeded] ensure_bytes_fit<Budget, Used>(): "
                                             "the computed byte usage exceeds the declared budget.  "
                                             "Inspect `safe_struct_bytes<...>` / `safe_array_bytes<T, N>` / "
                                             "`safe_add_all<size_t, ...>` for the individual contributors, "
                                             "OR widen the budget if the carrier can accommodate it.  "
                                             "Common causes: (a) added a new field to a cache-line-tight "
                                             "struct, (b) bumped N for an array that was sized to fit a "
                                             "single page, (c) composed a buffer layout whose sum crosses "
                                             "a hardware-alignment boundary (cache line, page, sector).");
}

// Only the accepting cases can be witnessed here.  A rejection is a
// failed static_assert, which would break this translation unit, so
// those cases live in the negative-compile harness instead.

static_assert(safe_add<std::uint32_t, 10u, 20u> == 30u);
static_assert(safe_sub<std::uint32_t, 30u, 20u> == 10u);
static_assert(safe_mul<std::uint32_t, 6u, 7u> == 42u);
static_assert(safe_capacity<8u, 16u> == 128u);
static_assert(safe_capacity<std::size_t{1} << 16, std::size_t{1} << 16> == (std::size_t{1} << 32));

static_assert(safe_mul<std::size_t, std::size_t{0}, std::size_t{1} << 60> == 0u);
static_assert(safe_add<std::size_t, std::size_t{0}, std::size_t{0}> == 0u);

static_assert(safe_add_all<std::size_t> == 0u);
static_assert(safe_add_all<std::size_t, 42u> == 42u);
static_assert(safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u> == 15u);
static_assert(safe_add_all<std::uint32_t, 10u, 20u, 30u> == 60u);

static_assert(safe_array_bytes<std::uint64_t, 8u> == 64u);
static_assert(safe_array_bytes<std::byte, 4096u> == 4096u);
static_assert(safe_array_bytes<std::uint32_t, 0u> == 0u);

static_assert(safe_struct_bytes<> == 0u);
static_assert(safe_struct_bytes<std::uint64_t> == 8u);
static_assert(safe_struct_bytes<std::uint64_t, std::uint32_t> == 12u);
static_assert(safe_struct_bytes<std::uint64_t, std::uint64_t, std::uint32_t> == 20u);

static_assert(bytes_fit_v<64u, 20u>);
static_assert(bytes_fit_v<64u, 64u>);
static_assert(!bytes_fit_v<64u, 65u>);

[[maybe_unused]] constexpr auto _check_fits = []() {
    ensure_bytes_fit<64, safe_struct_bytes<std::uint64_t, std::uint64_t>>();
    return 0;
}();

}  // namespace fixy
