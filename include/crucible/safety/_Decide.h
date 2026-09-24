// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <crucible/effects/_EffectRow.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace crucible::decide {

// True iff the mathematical product of a and b is representable in T. For
// signed T both extremes count as overflow.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool no_overflow_mul(T a, T b) noexcept {
    T r{};
    return !__builtin_mul_overflow(a, b, &r);
}

// True iff the mathematical sum of a and b is representable in T. For signed T
// both extremes count as overflow.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool no_overflow_sum(T a, T b) noexcept {
    T r{};
    return !__builtin_add_overflow(a, b, &r);
}

// True iff `a << b` is defined and the mathematical value a * 2^b is
// representable in T. Rejects a negative shift count, a count at or past the
// width of T, a negative value, and a result that does not fit.
//
// The fit test compares against `MAX >> b` rather than against `T{1} << b`,
// because the latter is itself undefined for signed T at b == W - 1.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool no_overflow_pow2_shift(T a, T b) noexcept {
    constexpr T W = static_cast<T>(sizeof(T) * 8);
    if constexpr (std::is_signed_v<T>) {
        if (b < T{0} || b >= W) {
            return false;
        }
        if (a < T{0}) {
            return false;
        }
        return a <= (std::numeric_limits<T>::max() >> b);
    } else {
        if (b >= W) {
            return false;
        }
        return a <= (std::numeric_limits<T>::max() >> b);
    }
}

// True iff every element lies in the closed interval [lo, hi]. An empty span is
// vacuously true. `lo > hi` is permitted and rejects every element.
template <std::integral T>
[[nodiscard, gnu::pure]]
constexpr bool all_in_range(std::span<const T> xs, T lo, T hi) noexcept {
    for (T const& x : xs) {
        if (x < lo || x > hi) {
            return false;
        }
    }
    return true;
}

// True iff every consecutive pair satisfies `xs[i - 1] < xs[i]`. Equal
// neighbours fail. Fewer than two elements is vacuously true.
template <std::integral T>
[[nodiscard, gnu::pure]]
constexpr bool strictly_increasing(std::span<const T> xs) noexcept {
    for (std::size_t i = 1; i < xs.size(); ++i) {
        if (!(xs[i - 1] < xs[i])) {
            return false;
        }
    }
    return true;
}

// True iff every consecutive pair satisfies `xs[i - 1] <= xs[i]`. Equal
// neighbours pass. Only a strict regression fails. Fewer than two elements is
// vacuously true.
template <std::integral T>
[[nodiscard, gnu::pure]]
constexpr bool weakly_increasing(std::span<const T> xs) noexcept {
    for (std::size_t i = 1; i < xs.size(); ++i) {
        if (xs[i - 1] > xs[i]) {
            return false;
        }
    }
    return true;
}

// True iff x has exactly one set bit and x <= bound. Zero and, for signed T,
// negatives are rejected. A bound below 1 admits nothing.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool is_power_of_two_le(T x, T bound) noexcept {
    if (x <= T{0}) {
        return false;
    }
    if (x > bound) {
        return false;
    }
    // The x > 0 guard above makes x - 1 well defined for signed and unsigned T
    // alike.
    return (x & (x - T{1})) == T{0};
}

// True iff the product of the factors equals total. An empty span requires
// total == 1, the empty product. Overflow of the running product rejects, even
// when the wrapped value would compare equal to total.
template <std::integral T>
[[nodiscard, gnu::pure]]
constexpr bool factorization_eq(std::span<const T> factors, T total) noexcept {
    T product{1};
    for (T const& f : factors) {
        if (__builtin_mul_overflow(product, f, &product)) {
            return false;
        }
    }
    return product == total;
}

// True iff gcd(|a|, |b|) == 1. Sign is irrelevant. The pinned edges are
// coprime(0, 0) false, coprime(0, 1) true, coprime(0, n > 1) false,
// coprime(1, n) true and coprime(n, n) true only at n == 1.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool coprime(T a, T b) noexcept {
    if (a == T{0} && b == T{0}) {
        return false;
    }
    using U = std::make_unsigned_t<T>;
    // Negating the unsigned cast yields the magnitude for every signed input,
    // including the minimum, where negating the signed value is undefined.
    U au = (a < T{0}) ? static_cast<U>(0) - static_cast<U>(a) : static_cast<U>(a);
    U bu = (b < T{0}) ? static_cast<U>(0) - static_cast<U>(b) : static_cast<U>(b);
    while (bu != U{0}) {
        U t = au % bu;
        au = bu;
        bu = t;
    }
    return au == U{1};
}

// A half-open range [lo, hi): lo inclusive, hi exclusive. The empty range
// [k, k) is well formed. An inverted range lo > hi is malformed.
template <std::integral T>
struct Interval {
    T lo{};
    T hi{};
};

template <std::integral T>
[[nodiscard]]
constexpr bool operator==(Interval<T> const& a, Interval<T> const& b) noexcept {
    return a.lo == b.lo && a.hi == b.hi;
}

// True iff every interval is well formed and no two intervals overlap. Gaps
// between intervals are permitted. An empty span is vacuously true. An empty
// interval is disjoint from every other interval.
template <std::integral T, std::size_t N = std::dynamic_extent>
[[nodiscard, gnu::pure]]
constexpr bool intervals_pairwise_disjoint(std::span<const Interval<T>, N> ivs) noexcept {
    for (Interval<T> const& iv : ivs) {
        if (iv.lo > iv.hi) {
            return false;
        }
    }
    for (std::size_t i = 0; i < ivs.size(); ++i) {
        for (std::size_t j = i + 1; j < ivs.size(); ++j) {
            // Skip any pair holding an empty interval. The left-of test alone
            // reports an empty interval strictly inside another as an overlap:
            // for [5, 5) inside [0, 10) neither 5 <= 0 nor 10 <= 5 holds, yet
            // [5, 5) contains no integer and so intersects nothing.
            if (ivs[i].lo == ivs[i].hi || ivs[j].lo == ivs[j].hi) {
                continue;
            }
            const bool a_left_of_b = ivs[i].hi <= ivs[j].lo;
            const bool b_left_of_a = ivs[j].hi <= ivs[i].lo;
            if (!a_left_of_b && !b_left_of_a) {
                return false;
            }
        }
    }
    return true;
}

// True iff the intervals tile [0, total) exactly: each one well formed,
// non-empty and contained, all pairs disjoint, and the widths sum to total.
// Gaps and overlaps both fail. An empty interval fails, because in a partition
// it signals a caller error rather than a harmless no-op. An empty span holds
// only for total == 0. A negative total admits nothing.
template <std::integral T, std::size_t N = std::dynamic_extent>
[[nodiscard, gnu::pure]]
constexpr bool intervals_cover_unit(std::span<const Interval<T>, N> ivs, T total) noexcept {
    if (total < T{0}) {
        return false;
    }
    if (total == T{0}) {
        return ivs.empty();
    }
    for (Interval<T> const& iv : ivs) {
        if (iv.lo > iv.hi) {
            return false;
        }
        if (iv.lo == iv.hi) {
            return false;
        }
        if (iv.lo < T{0}) {
            return false;
        }
        if (iv.hi > total) {
            return false;
        }
    }
    for (std::size_t i = 0; i < ivs.size(); ++i) {
        for (std::size_t j = i + 1; j < ivs.size(); ++j) {
            const bool a_left_of_b = ivs[i].hi <= ivs[j].lo;
            const bool b_left_of_a = ivs[j].hi <= ivs[i].lo;
            if (!a_left_of_b && !b_left_of_a) {
                return false;
            }
        }
    }
    // Containment plus disjointness already bound the sum by total. The
    // overflow-detecting add is defence in depth.
    T width_sum{0};
    for (Interval<T> const& iv : ivs) {
        const T width = static_cast<T>(iv.hi - iv.lo);
        if (__builtin_add_overflow(width_sum, width, &width_sum)) {
            return false;
        }
    }
    return width_sum == total;
}

// True iff a candidate of tier `candidate` may stand in for a slot demanding
// tier `required`. Every chain-tier enum in the project orders its enumerators
// so that a stronger guarantee takes a higher ordinal, which makes the
// admission test one integer comparison. A new chain-tier enum that breaks that
// ordering breaks this predicate for that enum.
template <typename TierTag>
    requires std::is_enum_v<TierTag>
[[nodiscard, gnu::const]]
constexpr bool tier_replaces(TierTag candidate, TierTag required) noexcept {
    using U = std::underlying_type_t<TierTag>;
    return static_cast<U>(candidate) >= static_cast<U>(required);
}

// True iff every effect atom of row Payload also appears in row Ctx, so a
// value declared over Payload lifts into a slot declared over Ctx. The relation
// is set membership, not structural equality: atom order does not matter.
template <typename Payload, typename Ctx>
[[nodiscard, gnu::const]]
constexpr bool row_subset() noexcept {
    return effects::is_subrow_v<Payload, Ctx>;
}

// True iff both the seed and the mix output are non-zero. The caller passes the
// already-computed output, so the predicate never re-runs the mix. Checking
// both ends witnesses at the use site that the mixer maps a non-zero seed to a
// non-zero hash, which the zero-means-empty slot conventions depend on.
[[nodiscard, gnu::const]]
constexpr bool fmix_preserves_non_zero(std::uint64_t seed, std::uint64_t mix_output) noexcept {
    return seed != 0 && mix_output != 0;
}

// Folds over independent clauses. Each empty fold takes the identity of its
// operator: conjunction of nothing is true, disjunction of nothing is false.
[[nodiscard, gnu::pure]]
constexpr bool conjunction(std::span<const bool> xs) noexcept {
    for (bool b : xs) {
        if (!b) return false;
    }
    return true;
}

[[nodiscard, gnu::pure]]
constexpr bool disjunction(std::span<const bool> xs) noexcept {
    for (bool b : xs) {
        if (b) return true;
    }
    return false;
}

// Material implication. False only for a true antecedent with a false
// consequent. A false antecedent holds vacuously.
//
// Both arguments are evaluated before the call. For a guarded dereference of
// the shape `p != nullptr` implies `p->field == x`, use the short-circuiting
// `||` operator instead, because the consequent here dereferences a null p.
[[nodiscard, gnu::const]]
constexpr bool implies(bool antecedent, bool consequent) noexcept {
    return !antecedent || consequent;
}

// True iff value lies in the closed interval [low, high] and is a multiple of
// alignment. An alignment of zero is rejected rather than treated as
// unconstrained. A half-open caller passes `high - 1` so the adjustment stays
// visible at the call site.
[[nodiscard, gnu::const]]
constexpr bool aligned_in_range(std::uint64_t value, std::uint64_t low, std::uint64_t high,
                                std::uint64_t alignment) noexcept {
    return alignment != 0u && low <= value && value <= high && (value % alignment) == 0u;
}

// True iff x lies in the closed interval [lo, hi]. Both endpoints are included.
// `lo > hi` is permitted and admits nothing. A half-open caller passes `hi - 1`
// so the adjustment stays visible at the call site.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool in_range(T x, T lo, T hi) noexcept {
    return lo <= x && x <= hi;
}

// True iff x is strictly greater than zero. Zero fails, and for signed T every
// negative fails.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool positive(T x) noexcept {
    return x > T{0};
}

// True iff x is greater than or equal to zero. Zero passes, and for signed T
// every negative fails. For unsigned T the predicate is a tautology.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool non_negative(T x) noexcept {
    return T{0} <= x;
}

// True iff a (count, ptr) pair forms a well-defined span: either the count is
// zero, in which case the pointer is never read and may be null, or the pointer
// is non-null and the count elements behind it are addressable.
template <std::integral C>
[[nodiscard, gnu::const]]
constexpr bool valid_span(C count, const void* ptr) noexcept {
    return count == C{0} || ptr != nullptr;
}

// True iff x differs from the structural zero `T{}` of its own type. For an
// aggregate that is the all-fields-default instance, so every field takes part
// in the comparison.
template <typename T>
    requires requires(T const& a, T const& b) {
        { a != b } -> std::convertible_to<bool>;
    }
[[nodiscard, gnu::pure]]
constexpr bool is_non_zero(T const& x) noexcept(noexcept(T{} != x)) {
    // The body is semantically `x != T{}`. GCC 16.1.1 misreports an aggregate
    // `T{}` rvalue as having uninitialized members when the consteval evaluator
    // re-runs the predicate inside an [[assume]] hint. A named local forces
    // value-initialization through the defaulted constructor and side-steps
    // that, while preserving the noexcept propagation above.
    T const zero{};
    return zero != x;
}

// True iff the hash is not at its reserved end-of-region sentinel value.
//
// Each strong-hash type reserves two distinct values for unrelated bookkeeping:
// the default-constructed zero marks an empty cache slot, and the sentinel
// marks the end of a region. Neither reserved value is rejected by the check
// for the other, so an admissibility gate over an untrusted hash cites this
// predicate and the non-zero predicate as a pair.
template <typename H>
    requires requires(H const& h) {
        { h.is_sentinel() } -> std::convertible_to<bool>;
    }
[[nodiscard, gnu::pure]]
constexpr bool not_sentinel_hash(H const& h) noexcept(noexcept(h.is_sentinel())) {
    return !h.is_sentinel();
}

}  // namespace crucible::decide
