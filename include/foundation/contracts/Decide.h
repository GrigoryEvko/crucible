// SPDX-License-Identifier: Apache-2.0
//
// The catalog of the named predicates that preconditions cite.  Each
// predicate is a common pattern of a precondition.  A predicate stays in the
// catalog whether or not a precondition cites it.

#pragma once

#include <concepts>
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
