#pragma once

// ── crucible::safety::extract::is_consistency_v ─────────────────────
//
// FOUND-D22 — wrapper-detection predicate for `Consistency<Level, T>`.
// Mechanical extension of D21 (is_numerical_tier_v) — partial-
// specialization captures the NTTP enum value alongside the wrapped
// type.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_consistency_v<T>      Variable template: true iff T (after
//                             cv-ref stripping) is a specialization
//                             of Consistency<Level, U>.
//   IsConsistency<T>          Concept form for `requires`-clauses.
//   consistency_value_t<T>    Alias to the wrapped element type U.
//                             Constrained on is_consistency_v;
//                             ill-formed otherwise.
//   consistency_level_v<T>    Compile-time variable: the
//                             Consistency_v level pinned at the type
//                             level.  Constrained on
//                             is_consistency_v.
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Identical to D21: partial-spec on `<Consistency_v Level, typename
// U>` captures the NTTP + wrapped type and exposes both via the
// trait.  The Consistency_v enum has 5 values (EVENTUAL,
// READ_YOUR_WRITES, CAUSAL_PREFIX, BOUNDED_STALENESS, STRONG).
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval predicate.
//   TypeSafe — partial specialization is the only true case.
//   DetSafe — same T → same value.

#include <crucible/safety/Consistency.h>

#include <type_traits>

namespace crucible::safety::extract {

// Re-export Consistency_v so dispatcher call sites don't need to
// spell `algebra::lattices::Consistency::STRONG`.
using ::crucible::safety::Consistency_v;

// ═════════════════════════════════════════════════════════════════════
// ── detail: primary + partial specialization ──────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <typename T>
struct is_consistency_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_level = false;
};

template <Consistency_v Level, typename U>
struct is_consistency_impl<::crucible::safety::Consistency<Level, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr Consistency_v level = Level;
    static constexpr bool has_level = true;
};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_consistency_v =
    detail::is_consistency_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsConsistency = is_consistency_v<T>;

template <typename T>
    requires is_consistency_v<T>
using consistency_value_t =
    typename detail::is_consistency_impl<
        std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_consistency_v<T>
inline constexpr Consistency_v consistency_level_v =
    detail::is_consistency_impl<std::remove_cvref_t<T>>::level;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::is_consistency_self_test {

using C_int_strong =
    ::crucible::safety::Consistency<Consistency_v::STRONG, int>;
using C_double_eventual =
    ::crucible::safety::Consistency<Consistency_v::EVENTUAL, double>;
using C_int_causal =
    ::crucible::safety::Consistency<Consistency_v::CAUSAL_PREFIX, int>;

// ── Positive cases ────────────────────────────────────────────────

static_assert(is_consistency_v<C_int_strong>);
static_assert(is_consistency_v<C_double_eventual>);
static_assert(is_consistency_v<C_int_causal>);

// Cv-ref stripping.
static_assert(is_consistency_v<C_int_strong&>);
static_assert(is_consistency_v<C_int_strong&&>);
static_assert(is_consistency_v<C_int_strong const&>);
static_assert(is_consistency_v<C_int_strong const>);
static_assert(is_consistency_v<C_int_strong const&&>);

// ── Negative cases ────────────────────────────────────────────────

static_assert(!is_consistency_v<int>);
static_assert(!is_consistency_v<int*>);
static_assert(!is_consistency_v<int&>);
static_assert(!is_consistency_v<void>);

struct LookalikeConsistency { int value; Consistency_v level; };
static_assert(!is_consistency_v<LookalikeConsistency>);

static_assert(!is_consistency_v<C_int_strong*>);

// ── Concept form ─────────────────────────────────────────────────

static_assert(IsConsistency<C_int_strong>);
static_assert(IsConsistency<C_int_strong&&>);
static_assert(!IsConsistency<int>);

// ── Element type / level extraction ──────────────────────────────

static_assert(std::is_same_v<consistency_value_t<C_int_strong>, int>);
static_assert(std::is_same_v<
    consistency_value_t<C_double_eventual>, double>);

static_assert(std::is_same_v<
    consistency_value_t<C_int_strong const&>, int>);
static_assert(std::is_same_v<
    consistency_value_t<C_int_strong&&>, int>);

static_assert(consistency_level_v<C_int_strong> == Consistency_v::STRONG);
static_assert(consistency_level_v<C_double_eventual>
              == Consistency_v::EVENTUAL);
static_assert(consistency_level_v<C_int_causal>
              == Consistency_v::CAUSAL_PREFIX);
static_assert(consistency_level_v<C_int_strong const&>
              == Consistency_v::STRONG);

// Distinct (Level, U) → distinct trait specializations.
static_assert(std::is_same_v<
    consistency_value_t<C_int_strong>,
    consistency_value_t<C_int_causal>>);
static_assert(consistency_level_v<C_int_strong>
              != consistency_level_v<C_int_causal>);

}  // namespace detail::is_consistency_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool is_consistency_smoke_test() noexcept {
    using namespace detail::is_consistency_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_consistency_v<C_int_strong>;
        ok = ok && !is_consistency_v<int>;
        ok = ok && IsConsistency<C_int_strong&&>;
        ok = ok && (consistency_level_v<C_int_strong>
                    == Consistency_v::STRONG);
    }
    return ok;
}

}  // namespace crucible::safety::extract
