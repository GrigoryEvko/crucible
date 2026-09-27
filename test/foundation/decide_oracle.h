// SPDX-License-Identifier: Apache-2.0
//
// Slow reference implementations of the fuzzable predicate procedures of
// include/foundation/contracts/Decide.h.  test_decide_fuzz.cpp compares each
// production answer against the matching oracle here.  The header lives with
// the tests, because only test code includes it.
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
// This header does not include the production predicate library, so an oracle
// cannot reuse a production definition by accident.

#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace foundation::decide::oracle {

// The mapped type holds the sum of any two values of T without wrapping: 64
// bits covers every T of 32 bits or fewer, 128 bits covers a 64-bit T.  The
// width and the sign of T select the type, so `long` and `long long` answer
// alongside the fixed-width spellings.
//
// ISO C++ ships no 128-bit integer type, so the 64-bit rows rest on the
// compiler extension, which test code may use.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
template <std::integral T>
struct widen {
    static_assert(sizeof(T) <= 8, "widen<T>: no integer holds the sum of two values wider than 64 bits");
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
[[nodiscard]] constexpr bool no_overflow_sum_oracle(T a, T b) noexcept {
    using W = widen_t<T>;
    W const wa = static_cast<W>(a);
    W const wb = static_cast<W>(b);
    W const sum = wa + wb;
    W const lo = static_cast<W>(std::numeric_limits<T>::min());
    W const hi = static_cast<W>(std::numeric_limits<T>::max());
    return sum >= lo && sum <= hi;
}

// All pairs rather than adjacent pairs.  The quadratic form states the ordering
// property directly and leaves no room for an off-by-one in the walk.
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

// A half-open range [lo, hi) of the oracle's own, so that this header names
// no type of the production library.
template <std::integral T>
struct Range {
    T lo{};
    T hi{};
};

// A sweep instead of the all-pairs walk of the production form.  A malformed
// range refuses the whole set.  An empty range holds no integer, so it is
// dropped.  The remaining ranges are sorted by their low end, and each one
// must end at or before the start of the next one.  Complexity: O(n log n),
// with one allocation for the copy.
template <std::integral T>
[[nodiscard]] constexpr bool intervals_pairwise_disjoint_oracle(std::span<const Range<T>> ranges) {
    std::vector<Range<T>> occupied;
    for (Range<T> const& range : ranges) {
        if (range.hi < range.lo) return false;
        if (range.lo < range.hi) occupied.push_back(range);
    }
    std::sort(occupied.begin(), occupied.end(),
              [](Range<T> const& left, Range<T> const& right) { return left.lo < right.lo; });
    for (std::size_t index = 1; index < occupied.size(); ++index) {
        if (occupied[index].lo < occupied[index - 1].hi) return false;
    }
    return true;
}

}  // namespace foundation::decide::oracle
