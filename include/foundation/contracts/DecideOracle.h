// SPDX-License-Identifier: Apache-2.0
//
// Slow reference implementations of the fuzzable predicate procedures.  A fuzz
// harness compares each production answer against the matching oracle here.
//
// Rules for adding an oracle:
//
//   1. Reach the answer by a different computational path than the production
//      procedure.  An oracle that mirrors the production body proves nothing.
//   2. Prefer the transparent form over the fast one.  An oracle is the spec.
//      A reviewer must agree with it by inspection.
//   3. Be total.  Every input returns a defined bool, with no undefined
//      behaviour and no precondition to violate.
//   4. Do not guard an oracle with a predicate from the production library.
//      The oracle is the definition of that predicate, so the guard would be
//      circular.
//
// This header deliberately does not include the production predicate library,
// so an oracle cannot silently reuse a production definition.
//
// Old spelling: include/crucible/safety/DecideOracle.h.

#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace foundation::decide::oracle {

// The mapped type holds the sum or the product of any two values of T without
// wrapping: 64 bits covers every T of 32 bits or fewer, 128 bits covers a
// 64-bit T.  The width and the sign of T select the type, so `long` and
// `long long` answer alongside the fixed-width spellings.
//
// ISO C++ ships no 128-bit integer type, so the 64-bit rows rest on the
// compiler extension.  This header is test-only and never enters the production
// library, which is what makes the extension acceptable here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
template <std::integral T>
struct widen {
    static_assert(sizeof(T) <= 8, "widen<T>: no integer holds the product of two values wider than 64 bits");
    using type =
        std::conditional_t<sizeof(T) <= 4, std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>,
                           std::conditional_t<std::is_signed_v<T>, __int128, unsigned __int128>>;
};
#pragma GCC diagnostic pop

template <typename T>
using widen_t = typename widen<T>::type;

static_assert(std::is_same_v<widen_t<std::uint8_t>, std::uint64_t>);
static_assert(std::is_same_v<widen_t<std::uint32_t>, std::uint64_t>);
static_assert(std::is_same_v<widen_t<std::int8_t>, std::int64_t>);
static_assert(std::is_same_v<widen_t<std::int32_t>, std::int64_t>);
static_assert(sizeof(widen_t<std::uint64_t>) == 16 && std::is_unsigned_v<widen_t<std::uint64_t>>);
static_assert(sizeof(widen_t<std::int64_t>) == 16 && std::is_signed_v<widen_t<std::int64_t>>);

template <std::integral T>
[[nodiscard]] constexpr bool no_overflow_mul_oracle(T a, T b) noexcept {
    using W = widen_t<T>;
    W const wa = static_cast<W>(a);
    W const wb = static_cast<W>(b);
    W const product = wa * wb;
    W const lo = static_cast<W>(std::numeric_limits<T>::min());
    W const hi = static_cast<W>(std::numeric_limits<T>::max());
    return product >= lo && product <= hi;
}

template <std::integral T>
[[nodiscard]] constexpr bool no_overflow_sum_oracle(T a, T b) noexcept {
    using W = widen_t<T>;
    W const wa = static_cast<W>(a);
    W const wb = static_cast<W>(b);
    W const sum = wa + wb;
    W const lo = static_cast<W>(std::numeric_limits<T>::min());
    W const hi = static_cast<W>(std::numeric_limits<T>::max());
    return sum >= lo && sum <= hi;
}

template <std::integral T>
[[nodiscard]] constexpr bool all_in_range_oracle(std::span<const T> xs, T lo, T hi) noexcept {
    if (lo > hi) return xs.empty();
    for (T const& x : xs) {
        if (x < lo || x > hi) return false;
    }
    return true;
}

// All pairs rather than adjacent pairs.  The quadratic form states the ordering
// property directly and leaves no room for an off-by-one in the walk.
template <std::integral T>
[[nodiscard]] constexpr bool strictly_increasing_oracle(std::span<const T> xs) noexcept {
    std::size_t const n = xs.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!(xs[i] < xs[j])) return false;
        }
    }
    return true;
}

template <std::integral T>
[[nodiscard]] constexpr bool weakly_increasing_oracle(std::span<const T> xs) noexcept {
    std::size_t const n = xs.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!(xs[i] <= xs[j])) return false;
        }
    }
    return true;
}

template <std::integral T>
[[nodiscard]] constexpr bool is_power_of_two_le_oracle(T x, T bound) noexcept {
    if constexpr (std::is_signed_v<T>) {
        if (x <= 0) return false;
    } else {
        if (x == 0) return false;
    }
    using U = std::make_unsigned_t<T>;
    auto const ux = static_cast<U>(x);
    if (std::popcount(ux) != 1) return false;
    return x <= bound;
}

template <std::integral T>
[[nodiscard]] constexpr bool factorization_eq_oracle(std::span<const T> factors, T total) noexcept {
    using W = widen_t<T>;
    W product = 1;
    W const lo = static_cast<W>(std::numeric_limits<T>::min());
    W const hi = static_cast<W>(std::numeric_limits<T>::max());
    for (T const& f : factors) {
        product *= static_cast<W>(f);
        if (product < lo || product > hi) return false;
    }
    return product == static_cast<W>(total);
}

// The gcd of a pair of zeros is zero by convention, so a pair of zeros is not
// coprime.
template <std::integral T>
[[nodiscard]] constexpr bool coprime_oracle(T a, T b) noexcept {
    using U = std::make_unsigned_t<T>;
    U ua = a < 0 ? static_cast<U>(-static_cast<U>(a)) : static_cast<U>(a);
    U ub = b < 0 ? static_cast<U>(-static_cast<U>(b)) : static_cast<U>(b);
    while (ub != 0) {
        U const r = ua % ub;
        ua = ub;
        ub = r;
    }
    return ua == U{1};
}

// The production forms are loops too, so these two count every element instead
// of exiting early.  That is the only shape difference available, and it still
// separates a production form that answers a constant from a correct one.
[[nodiscard]] constexpr bool conjunction_oracle(std::span<const bool> xs) noexcept {
    std::size_t false_count = 0;
    for (bool const& b : xs) {
        if (!b) ++false_count;
    }
    return false_count == 0;
}

[[nodiscard]] constexpr bool disjunction_oracle(std::span<const bool> xs) noexcept {
    std::size_t true_count = 0;
    for (bool const& b : xs) {
        if (b) ++true_count;
    }
    return true_count != 0;
}

// This oracle repeats the production expression, so it proves nothing about
// the formula.  What it does test is clause order: the zero-alignment guard
// runs first here, and a production form that reorders it divides by zero.
[[nodiscard]] constexpr bool aligned_in_range_oracle(std::uint64_t value, std::uint64_t low, std::uint64_t high,
                                                     std::uint64_t alignment) noexcept {
    if (alignment == 0u) return false;
    if (value < low) return false;
    if (value > high) return false;
    if ((value % alignment) != 0u) return false;
    return true;
}

}  // namespace foundation::decide::oracle
