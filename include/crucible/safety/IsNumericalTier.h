#pragma once

// ── crucible::safety::extract::is_numerical_tier_v ──────────────────
//
// Wrapper-detection predicate for `NumericalTier<T_at, T>`.  First
// member of the FOUND-D21..D30 series — the wrapper-detection
// extractors for the FOUND-G product wrappers (the §4.2.1 catalog
// from 28_04_2026_effects.md).  Mechanical extension of D03's
// pattern (is_owned_region_v).
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_numerical_tier_v<T>      Variable template: true iff T (after
//                                cv-ref stripping) is a specialization
//                                of NumericalTier<T_at, U>.
//   IsNumericalTier<T>           Concept form for `requires`-clauses.
//   numerical_tier_value_t<T>    Alias to the wrapped element type U.
//                                Constrained on is_numerical_tier_v;
//                                ill-formed otherwise.
//   numerical_tier_v<T>          Compile-time variable: the
//                                Tolerance tier pinned at the type
//                                level.  Constrained on
//                                is_numerical_tier_v.
//
// ── Why this lives in safety::extract ──────────────────────────────
//
// The dispatcher's reading-surface predicates all live in
// `crucible::safety::extract` (D03-D20).  Adding wrapper-detection
// for the G-series wrappers extends that surface uniformly:
// dispatcher-side code asks `is_numerical_tier_v<param_type_t<FnPtr,
// 0>>` to recognize a tier-pinned tensor parameter; the analog
// extractors yield the pinned tier and element type for tier-aware
// dispatch and diagnostics.
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Canonical primary-false-template + partial-specialization-true
// pattern, mirroring is_owned_region_v in IsOwnedRegion.h.
// `std::remove_cvref_t<T>` strips reference categories before the
// trait check so call sites can write the predicate against a
// parameter type directly without manual decay.
//
// NumericalTier's first template parameter is a `Tolerance` enum
// value (NTTP), not a type — the partial specialization captures
// the NTTP via `<Tolerance T_at, typename U>` and exposes both
// pieces via the trait.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval predicate.
//   TypeSafe — partial specialization is the only true case;
//              everything else is false.  No silent conversions.
//   DetSafe — same T → same value, deterministically; no hidden
//              state.

#include <crucible/safety/NumericalTier.h>

#include <type_traits>

namespace crucible::safety::extract {

// Re-export Tolerance into safety::extract so dispatcher call sites
// don't need to spell `algebra::lattices::Tolerance::BITEXACT`.
using ::crucible::safety::Tolerance;

// ═════════════════════════════════════════════════════════════════════
// ── detail: primary + partial specialization ──────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <typename T>
struct is_numerical_tier_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_tier = false;
};

template <Tolerance T_at, typename U>
struct is_numerical_tier_impl<::crucible::safety::NumericalTier<T_at, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr Tolerance tier = T_at;
    static constexpr bool has_tier = true;
};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_numerical_tier_v =
    detail::is_numerical_tier_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsNumericalTier = is_numerical_tier_v<T>;

// numerical_tier_value_t / numerical_tier_v are constrained on
// is_numerical_tier_v to produce a clean compile error rather than
// `void` / undefined-tier for non-NumericalTier arguments.

template <typename T>
    requires is_numerical_tier_v<T>
using numerical_tier_value_t =
    typename detail::is_numerical_tier_impl<
        std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_numerical_tier_v<T>
inline constexpr Tolerance numerical_tier_v =
    detail::is_numerical_tier_impl<std::remove_cvref_t<T>>::tier;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Adversarial cases verified at compile time.  Every claim is
// duplicated by the sentinel TU under the project's full warning
// matrix.

namespace detail::is_numerical_tier_self_test {

using NT_int_bitexact =
    ::crucible::safety::NumericalTier<Tolerance::BITEXACT, int>;
using NT_double_relaxed =
    ::crucible::safety::NumericalTier<Tolerance::RELAXED, double>;
using NT_int_fp32 =
    ::crucible::safety::NumericalTier<Tolerance::ULP_FP32, int>;

// ── Positive cases ────────────────────────────────────────────────

static_assert(is_numerical_tier_v<NT_int_bitexact>);
static_assert(is_numerical_tier_v<NT_double_relaxed>);
static_assert(is_numerical_tier_v<NT_int_fp32>);

// Cv-ref stripping — every reference category resolves identically.
static_assert(is_numerical_tier_v<NT_int_bitexact&>);
static_assert(is_numerical_tier_v<NT_int_bitexact&&>);
static_assert(is_numerical_tier_v<NT_int_bitexact const&>);
static_assert(is_numerical_tier_v<NT_int_bitexact const>);
static_assert(is_numerical_tier_v<NT_int_bitexact const&&>);

// ── Negative cases ────────────────────────────────────────────────

static_assert(!is_numerical_tier_v<int>);
static_assert(!is_numerical_tier_v<int*>);
static_assert(!is_numerical_tier_v<int&>);
static_assert(!is_numerical_tier_v<void>);

// A struct that has the same shape but is not NumericalTier is rejected.
struct LookalikeNumericalTier { int value; Tolerance tier; };
static_assert(!is_numerical_tier_v<LookalikeNumericalTier>);

// Pointer-to-NumericalTier is NOT a NumericalTier.
static_assert(!is_numerical_tier_v<NT_int_bitexact*>);

// ── Concept form ─────────────────────────────────────────────────

static_assert(IsNumericalTier<NT_int_bitexact>);
static_assert(IsNumericalTier<NT_int_bitexact&&>);
static_assert(!IsNumericalTier<int>);

// ── Element type / tier extraction ────────────────────────────────

static_assert(std::is_same_v<numerical_tier_value_t<NT_int_bitexact>, int>);
static_assert(std::is_same_v<
    numerical_tier_value_t<NT_double_relaxed>, double>);

// Cv-ref stripping — value_type unwraps consistently.
static_assert(std::is_same_v<
    numerical_tier_value_t<NT_int_bitexact const&>, int>);
static_assert(std::is_same_v<
    numerical_tier_value_t<NT_int_bitexact&&>, int>);

// Tier extraction — pinned NTTP recovered.
static_assert(numerical_tier_v<NT_int_bitexact> == Tolerance::BITEXACT);
static_assert(numerical_tier_v<NT_double_relaxed> == Tolerance::RELAXED);
static_assert(numerical_tier_v<NT_int_fp32> == Tolerance::ULP_FP32);
static_assert(numerical_tier_v<NT_int_bitexact const&>
              == Tolerance::BITEXACT);

// Distinct (T_at, U) → distinct trait specializations; element types
// agree only when they actually do.
static_assert(std::is_same_v<
    numerical_tier_value_t<NT_int_bitexact>,
    numerical_tier_value_t<NT_int_fp32>>);
static_assert(numerical_tier_v<NT_int_bitexact>
              != numerical_tier_v<NT_int_fp32>);

}  // namespace detail::is_numerical_tier_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool is_numerical_tier_smoke_test() noexcept {
    using namespace detail::is_numerical_tier_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_numerical_tier_v<NT_int_bitexact>;
        ok = ok && !is_numerical_tier_v<int>;
        ok = ok && IsNumericalTier<NT_int_bitexact&&>;
        ok = ok && (numerical_tier_v<NT_int_bitexact>
                    == Tolerance::BITEXACT);
    }
    return ok;
}

}  // namespace crucible::safety::extract
