#pragma once

// The compile-time gates of a capacity: a sum, a difference or a product of
// constants that stops the build when its type cannot hold the result.
//
// The arithmetic at run time is the Scalar family of
// foundation/core/Arithmetic.h, which fixy/Core.h puts into namespace fixy:
// the checked_*, wrapping_* and saturating_* sets.

#include <foundation/core/Arithmetic.h>

#include <cstddef>

namespace fixy {

// Capacities are often computed at compile time, and a bare product
// that overflows the result type propagates a corrupted size into
// buffer sizing and layout.  The variable templates below turn that
// into a build failure.
//
// The obvious spelling, a consteval lambda holding
// `if (!r) static_assert(false, ...)`, does not work.  A static_assert
// with a false condition in a function body fires on instantiation
// whatever the surrounding branch says.  Routing through a class
// template instead puts the test in a static member, and asserting
// on that member fires only on a real overflow.
//
// Every one of those assertions opens with a bracketed tag so an audit
// can find each compile-time arithmetic failure site by grep.
//
// When the checked result is empty, its assertion stops the build.  The
// value then reads value_or with the fallback 0, so the report holds that
// one error and not a second one from a later step of the same sum.

namespace detail {

template <::foundation::core::Integer T, T A, T B>
struct safe_add_impl {
    static constexpr bool fits = ::foundation::core::checked_add<T>(A, B).is_some();
    static_assert(fits, "[Checked_Capacity_Overflow] safe_add: A + B overflows the "
                        "destination integer type.  Pick a wider T or smaller operands.");
    static constexpr T value = ::foundation::core::checked_add<T>(A, B).value_or(T{0});
};

template <::foundation::core::Integer T, T A, T B>
struct safe_sub_impl {
    static constexpr bool fits = ::foundation::core::checked_sub<T>(A, B).is_some();
    static_assert(fits, "[Checked_Capacity_Overflow] safe_sub: A - B underflows the "
                        "destination integer type.  For unsigned T, A must be >= B.");
    static constexpr T value = ::foundation::core::checked_sub<T>(A, B).value_or(T{0});
};

template <::foundation::core::Integer T, T A, T B>
struct safe_mul_impl {
    static constexpr bool fits = ::foundation::core::checked_mul<T>(A, B).is_some();
    static_assert(fits, "[Checked_Capacity_Overflow] safe_mul: A * B overflows the "
                        "destination integer type.  Pick a wider T or smaller operands.");
    static constexpr T value = ::foundation::core::checked_mul<T>(A, B).value_or(T{0});
};

}  // namespace detail

template <::foundation::core::Integer T, T A, T B>
inline constexpr T safe_add = detail::safe_add_impl<T, A, B>::value;

template <::foundation::core::Integer T, T A, T B>
inline constexpr T safe_sub = detail::safe_sub_impl<T, A, B>::value;

template <::foundation::core::Integer T, T A, T B>
inline constexpr T safe_mul = detail::safe_mul_impl<T, A, B>::value;

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_capacity = safe_mul<std::size_t, A, B>;

// A budget is rarely a single product.  A sum of terms, each of them a
// product, has an intermediate that can wrap and silently under-size the
// whole.  The helpers below carry the check through every step.

namespace detail {

template <::foundation::core::Integer T, T... Xs>
struct safe_add_all_impl;

template <::foundation::core::Integer T>
struct safe_add_all_impl<T> {
    static constexpr T value = T{0};
};

template <::foundation::core::Integer T, T X>
struct safe_add_all_impl<T, X> {
    static constexpr T value = X;
};

template <::foundation::core::Integer T, T X, T Y, T... Rest>
struct safe_add_all_impl<T, X, Y, Rest...> {
    static constexpr bool fits = ::foundation::core::checked_add<T>(X, Y).is_some();
    static_assert(fits, "[Checked_Capacity_Overflow] safe_add_all: partial sum "
                        "overflows the destination integer type.  One of the terms "
                        "in the variadic sum exceeds the remaining budget; split the "
                        "sum into smaller chunks, pick a wider T, or reduce an "
                        "operand.");
    static constexpr T value =
        safe_add_all_impl<T, ::foundation::core::checked_add<T>(X, Y).value_or(T{0}), Rest...>::value;
};

}  // namespace detail

template <::foundation::core::Integer T, T... Xs>
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

}  // namespace fixy
