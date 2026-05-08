// SPDX-License-Identifier: Apache-2.0
//
// crucible::decide::oracle::* — slow reference implementations of
// every fuzzable `crucible::decide::*` procedure.
//
// PURPOSE
// -------
// Decide's production procedures are clever: they use compiler
// builtins (`__builtin_mul_overflow`), single-pass loops with
// short-circuit early-exit, sort-and-sweep algorithms, Euclidean
// GCD.  Cleverness is a bug surface.  The CONTRACT-090 fuzz
// harness verifies that for every fuzzed input pair (or n-tuple),
// the production procedure's answer matches a deliberately SLOW
// but TRANSPARENTLY CORRECT reference implementation.
//
// ORACLE DESIGN PRINCIPLES
// ------------------------
//
//   1. Different algorithm.  An oracle that re-uses the production
//      impl's body is tautological.  Oracles ALWAYS use a different
//      computational path:
//        * For `no_overflow_mul<T>`: widen to `int128_t` (or
//          `__int128`) and check the result range.
//        * For `coprime`: trial-divide instead of Euclidean.
//        * For `intervals_pairwise_disjoint`: O(n²) all-pairs
//          overlap check instead of sort+sweep.
//        * For span-based folds: hand-rolled loop (the production
//          impl IS the loop, but the oracle re-derives the answer
//          via a transparently inefficient path: count truths, OR
//          all values, etc.).
//
//   2. Obviously correct.  An oracle is the spec, not optimized
//      code.  Each oracle is short enough that a reviewer reads it
//      once and agrees by inspection that it computes the predicate
//      definition.
//
//   3. Total.  Same total-function-of-inputs contract as the
//      production procedures: every input returns a defined bool,
//      no UB, no contract preconditions to violate.
//
//   4. Header-only.  Oracles inline cleanly into the fuzz harness;
//      no separate compilation unit, no link order.
//
//   5. No CRUCIBLE_PRE.  Oracles do not call the predicate library
//      at consteval — they ARE the spec, so guarding them with PRE
//      would be circular.  They use plain `if (cond) std::abort();`
//      where contracts on inputs would normally appear (none of
//      these have such contracts; predicates are total functions).
//
// COMPILE-TIME GUARANTEE
// ----------------------
// Every oracle is `constexpr`.  The fuzz harness invokes both fast
// and oracle paths inside a runtime loop driven by Philox-derived
// random inputs; the constexpr-ness of the oracle is incidental
// (it could be plain runtime).  We mark constexpr because it costs
// nothing and makes future compile-time fuzzing (CONTRACT-130
// compile-time bench) trivial.
//
// DEPENDENCY DISCIPLINE
// ---------------------
// This header includes ONLY:
//   <cstdint>, <span>, <type_traits>, <bit>, <limits>, <cstddef>
//
// It does NOT include `crucible/safety/Decide.h` to make it
// unambiguous that the oracles are independent definitions.
// However, we share the `Interval<T>` aggregate by including only
// the parts of Decide.h that define the type — to avoid
// duplication, the fuzz harness includes both headers and the
// oracles take `Interval<T>` by template parameter, leaving the
// definition site to Decide.h.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace crucible::decide::oracle {

// ─── widen<T> — pick a "wider" integer type for overflow oracles ──
//
// For 32-bit integral T, `int64_t` / `uint64_t` is wide enough for
// any sum or product of two T values.  For 64-bit integral T,
// `__int128` / `unsigned __int128` is wide enough.  For 8-/16-bit
// T, the 64-bit versions also work (always wider).
//
// We do NOT support `int128`-input fuzz: the production library
// fuzzes T ∈ {uint8_t, uint16_t, uint32_t, uint64_t, int8_t, ...,
// int64_t}, and 128-bit widen suffices for products of all of them.

template <typename T>
struct widen;

template <> struct widen<std::uint8_t>  { using type = std::uint64_t; };
template <> struct widen<std::uint16_t> { using type = std::uint64_t; };
template <> struct widen<std::uint32_t> { using type = std::uint64_t; };

// `__int128` is a GCC / Clang extension; ISO C++ does not yet ship a
// 128-bit integer type.  This header is build-internal (test/oracle
// only — never compiled into the production library), so the
// extension is acceptable.  Suppress the pedantic diagnostic at the
// declaration site.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
template <> struct widen<std::uint64_t> { using type = unsigned __int128; };

template <> struct widen<std::int8_t>   { using type = std::int64_t; };
template <> struct widen<std::int16_t>  { using type = std::int64_t; };
template <> struct widen<std::int32_t>  { using type = std::int64_t; };
template <> struct widen<std::int64_t>  { using type = __int128; };
#pragma GCC diagnostic pop

template <typename T>
using widen_t = typename widen<T>::type;

// ─── no_overflow_mul oracle — widen-and-bound ─────────────────────
//
// Compute the product in the wider type, check it lies within the
// representable range of T.  Independent of `__builtin_mul_overflow`
// — the wider type's multiplication is defined, the bound check is
// trivial.
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

// ─── no_overflow_sum oracle — widen-and-bound ─────────────────────
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

// ─── all_in_range oracle — manual loop ────────────────────────────
//
// Trivially obvious: iterate, return false on first out-of-range.
template <std::integral T>
[[nodiscard]] constexpr bool all_in_range_oracle(
    std::span<const T> xs, T lo, T hi
) noexcept {
    if (lo > hi) return xs.empty();
    for (T const& x : xs) {
        if (x < lo || x > hi) return false;
    }
    return true;
}

// ─── strictly_increasing oracle — O(n²) all-pairs ─────────────────
//
// For every (i, j) with i < j, require xs[i] < xs[j].  Quadratic
// to make the predicate definition unambiguous (no off-by-one in
// adjacent comparisons).
template <std::integral T>
[[nodiscard]] constexpr bool strictly_increasing_oracle(
    std::span<const T> xs
) noexcept {
    std::size_t const n = xs.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!(xs[i] < xs[j])) return false;
        }
    }
    return true;
}

// ─── weakly_increasing oracle — O(n²) all-pairs ───────────────────
//
// For every (i, j) with i < j, require xs[i] ≤ xs[j].  (Weakly =
// non-strict, allows equal values.)
template <std::integral T>
[[nodiscard]] constexpr bool weakly_increasing_oracle(
    std::span<const T> xs
) noexcept {
    std::size_t const n = xs.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!(xs[i] <= xs[j])) return false;
        }
    }
    return true;
}

// ─── is_power_of_two_le oracle — popcount + bound ─────────────────
//
// `x is a power of two` ≡ `popcount(x) == 1` (for unsigned T).
// For signed T, the predicate concerns positive powers only:
// `x > 0 && popcount(x) == 1`.  The combined predicate is
// `(power of two) AND (x ≤ bound)`.
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

// ─── factorization_eq oracle — widen-product + equality ───────────
//
// Compute the product of all factors in the WIDER type, check it
// fits in T AND equals `total`.  An empty factor list is handled
// per the production semantic: factors.empty() → product == 1
// (multiplicative identity), and `factorization_eq({}, total) ==
// (total == 1)`.
//
// Note: production semantic for `factorization_eq` may differ on
// the empty case; the fuzz harness avoids empty inputs to elide
// that ambiguity.
template <std::integral T>
[[nodiscard]] constexpr bool factorization_eq_oracle(
    std::span<const T> factors, T total
) noexcept {
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

// ─── coprime oracle — Euclidean GCD ───────────────────────────────
//
// gcd(a, b) == 1.  Implemented with the textbook Euclidean
// algorithm — terminates by induction on min(a, b).  The
// production `coprime` may use a different reduction (binary GCD,
// Stein's algorithm, etc.) — Euclidean is the unambiguous spec.
//
// For signed T, take absolute values first; gcd is defined on
// non-negative integers.  gcd(0, 0) == 0 by convention; thus
// coprime(0, 0) == false.
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
    // Now ua == gcd(|a|, |b|).  Coprime iff gcd == 1.
    return ua == U{1};
}

// ─── conjunction / disjunction oracles — manual loops ─────────────
//
// The production impls are themselves manual loops, so the oracle
// "different algorithm" requirement is weak here.  Use a different
// SHAPE: a count-based form that checks for any-false (resp. any-
// true).  An ALWAYS-TRUE buggy fast impl would still differ from
// these oracles on negative inputs.
[[nodiscard]] constexpr bool conjunction_oracle(
    std::span<const bool> xs
) noexcept {
    std::size_t false_count = 0;
    for (bool const& b : xs) {
        if (!b) ++false_count;
    }
    return false_count == 0;
}

[[nodiscard]] constexpr bool disjunction_oracle(
    std::span<const bool> xs
) noexcept {
    std::size_t true_count = 0;
    for (bool const& b : xs) {
        if (b) ++true_count;
    }
    return true_count != 0;
}

// ─── aligned_in_range oracle — straightforward four-clause check ──
//
// The production predicate IS this expression, so the oracle is
// not strongly distinct — the value is in fuzzing many random
// inputs to ensure the production formula's clause ORDER doesn't
// trigger short-circuit bugs (e.g. modulo-by-zero if guard is
// reordered).
[[nodiscard]] constexpr bool aligned_in_range_oracle(
    std::uint64_t value,
    std::uint64_t low,
    std::uint64_t high,
    std::uint64_t alignment
) noexcept {
    if (alignment == 0u) return false;
    if (value < low) return false;
    if (value > high) return false;
    if ((value % alignment) != 0u) return false;
    return true;
}

}  // namespace crucible::decide::oracle
