// SPDX-License-Identifier: Apache-2.0
//
// The named predicates that a production precondition cites.  A predicate
// earns its place here through its cites: utils/scripts/audit-decide-cite-ratio.py
// fails the build when a predicate has fewer than two production cites a
// fixed time after it entered this header.  A predicate that no
// precondition needs does not belong here.

#pragma once

#include <foundation/effects/Row.h>

#include <concepts>
#include <cstddef>
#include <meta>
#include <span>
#include <type_traits>

namespace foundation::decide {

// True iff the mathematical sum of a and b is representable in T. For signed T
// both extremes count as overflow.
template <std::integral T>
[[nodiscard, gnu::const]]
constexpr bool no_overflow_sum(T a, T b) noexcept {
    T r{};
    return !__builtin_add_overflow(a, b, &r);
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
// interval is disjoint from every other interval.  Complexity: quadratic in
// the number of intervals.
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
// The answer is a function at namespace scope that is not a template, so
// no translation unit can specialize it for a pair of rows.
[[nodiscard]] consteval bool row_subset(std::meta::info payload, std::meta::info ctx) {
    return ::foundation::effects::is_subrow(payload, ctx);
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

}  // namespace foundation::decide
