#pragma once

// ── crucible::safety::Refined<Pred, T> ──────────────────────────────
//
// Refinement type: a T paired with a compile-time-named predicate.
//
//   Axiom coverage: InitSafe, NullSafe, TypeSafe (code_guide §II).
//   Runtime cost:   zero on hot path (contract semantic=ignore),
//                   one branch at boundaries (semantic=enforce).
//                   sizeof(Refined<P, T>) == sizeof(T).
//
// - Construction contract-checks the predicate.
// - No implicit conversion to T — use .value() explicitly.  This is
//   load-bearing: a Refined<positive, int> must NOT silently pass to a
//   function taking plain int.
// - Trusted construction (Refined::Trusted{}) for internal paths that
//   have already proven the invariant.
//
// Name every load-bearing predicate.  Don't define anonymous refinements
// at call sites — aliases are what participate in grep and review.

#include <crucible/Platform.h>

#include <compare>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── Common predicates ──────────────────────────────────────────────
//
// User code can define more; each is a stateless lambda or a
// constexpr-callable function object.

inline constexpr auto positive = [](auto x) constexpr noexcept {
    return x > decltype(x){0};
};

inline constexpr auto non_negative = [](auto x) constexpr noexcept {
    return x >= decltype(x){0};
};

inline constexpr auto non_null = [](auto* p) constexpr noexcept {
    return p != nullptr;
};

template <std::size_t Alignment>
inline constexpr auto aligned = [](auto* p) constexpr noexcept {
    return (reinterpret_cast<std::uintptr_t>(p) & (Alignment - 1)) == 0;
};

template <auto Lo, auto Hi>
inline constexpr auto in_range = [](auto x) constexpr noexcept {
    return x >= decltype(x)(Lo) && x <= decltype(x)(Hi);
};

template <auto Max>
inline constexpr auto bounded_above = [](auto x) constexpr noexcept {
    return x <= decltype(x)(Max);
};

inline constexpr auto power_of_two = [](auto x) constexpr noexcept {
    using U = decltype(x);
    return x != U{0} && (x & (x - U{1})) == U{0};
};

// Length-ge predicate for spans / strings / any .size()-having container.
template <std::size_t N>
inline constexpr auto length_ge = [](const auto& c) constexpr noexcept {
    return c.size() >= N;
};

// Non-empty predicate for containers.
inline constexpr auto non_empty = [](const auto& c) constexpr noexcept {
    return !c.empty();
};

// ── The wrapper ────────────────────────────────────────────────────

template <auto Pred, typename T>
class [[nodiscard]] Refined {
    T value_;

public:
    using value_type     = T;
    using predicate_type = decltype(Pred);

    // Tag for skipping the predicate check.  Use only when the caller
    // has already proven the invariant (internal paths, re-wrapping
    // already-validated boundary data).
    struct Trusted {};

    // Checked construction — contract fires if the predicate fails.
    constexpr explicit Refined(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
#if CRUCIBLE_HAS_CONTRACTS
        pre(Pred(v))
#endif
        : value_{std::move(v)} {}

    // Trusted construction — no check, caller-asserted invariant.
    constexpr Refined(T v, Trusted) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(v)} {}

    // Refinement applies to the value; once constructed the invariant
    // holds.  Copy/move just preserve the value.
    Refined(const Refined&)            = default;
    Refined(Refined&&)                 = default;
    Refined& operator=(const Refined&) = default;
    Refined& operator=(Refined&&)      = default;

    // Explicit accessor — no implicit conversion.
    [[nodiscard]] constexpr const T& value() const noexcept { return value_; }

    // Explicit raw extraction for re-wrapping paths.
    [[nodiscard]] constexpr T into() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(value_);
    }

    // Equality / ordering on the underlying value.  Refined values of
    // the same Pred and T compare by their inner T.
    friend constexpr bool operator==(const Refined& a, const Refined& b)
        noexcept(noexcept(a.value_ == b.value_))
    {
        return a.value_ == b.value_;
    }

    friend constexpr auto operator<=>(const Refined& a, const Refined& b)
        noexcept(noexcept(a.value_ <=> b.value_))
        requires std::three_way_comparable<T>
    {
        return a.value_ <=> b.value_;
    }
};

} // namespace crucible::safety
