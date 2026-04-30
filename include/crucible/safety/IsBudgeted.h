#pragma once

// ── crucible::safety::extract::is_budgeted_v ────────────────────────
//
// FOUND-D30 (fifth of batch — FIRST product wrapper).  Wrapper-detection
// predicate for `Budgeted<T>`.
//
// ── Shape divergence from prior D30 wrappers ──────────────────────
//
// Budgeted is the FIRST product wrapper in the D-series detector
// family.  Its template signature is `template<typename T>` only —
// the two budget axes (BitsBudget, PeakBytes) are RUNTIME values
// stored inside the Graded substrate, NOT compile-time NTTPs.
//
// Consequences for the detector:
//
//   1. Single partial-spec on `Budgeted<U>` (no NTTP to capture).
//   2. value_type extraction is the ONLY constrained extractor —
//      there is no compile-time tag to extract.
//   3. Layout invariant is `>= sizeof(T) + 16` (16 bytes for the
//      runtime BitsBudget+PeakBytes pair), NOT `== sizeof(T)`.
//   4. Public-alias coverage tests check `BudgetedInt == Budgeted<int>`
//      etc. — Budgeted exposes type aliases, not template aliases.
//
// This shape is shared by EpochVersioned, NumaPlacement, RecipeSpec
// (the four product wrappers in the D30 batch).
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_budgeted_v<T>          Variable template; cv-ref-stripped.
//   IsBudgeted<T>             Concept form.
//   budgeted_value_t<T>       Wrapped element type; constrained.
//
// NO `budgeted_lattice_t` extractor: while the static `lattice_type`
// member is accessible, it carries no useful per-instance information
// (the runtime budget pair lives in the wrapper's impl_ field, queried
// via `b.bits()` / `b.peak_bytes()`).  Exposing it would invite
// dispatcher branches on a static type that tells the dispatcher
// nothing — admit the runtime accessors via the wrapper directly.

#include <crucible/safety/Budgeted.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_budgeted_impl : std::false_type {
    using value_type = void;
};

template <typename U>
struct is_budgeted_impl<::crucible::safety::Budgeted<U>>
    : std::true_type
{
    using value_type = U;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_budgeted_v =
    detail::is_budgeted_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsBudgeted = is_budgeted_v<T>;

template <typename T>
    requires is_budgeted_v<T>
using budgeted_value_t =
    typename detail::is_budgeted_impl<
        std::remove_cvref_t<T>>::value_type;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_budgeted_self_test {

using B_int      = ::crucible::safety::Budgeted<int>;
using B_double   = ::crucible::safety::Budgeted<double>;
using B_char     = ::crucible::safety::Budgeted<char>;
using B_uint64   = ::crucible::safety::Budgeted<std::uint64_t>;

static_assert(is_budgeted_v<B_int>);
static_assert(is_budgeted_v<B_double>);
static_assert(is_budgeted_v<B_char>);
static_assert(is_budgeted_v<B_uint64>);

static_assert(is_budgeted_v<B_int&>);
static_assert(is_budgeted_v<B_int const&>);

static_assert(!is_budgeted_v<int>);
static_assert(!is_budgeted_v<int*>);
static_assert(!is_budgeted_v<void>);

struct LookalikeBudgeted {
    int value;
    std::uint64_t bits_field;
    std::uint64_t peak_field;
};
static_assert(!is_budgeted_v<LookalikeBudgeted>);

static_assert(!is_budgeted_v<B_int*>);

static_assert(IsBudgeted<B_int>);
static_assert(!IsBudgeted<int>);

static_assert(std::is_same_v<budgeted_value_t<B_int>,    int>);
static_assert(std::is_same_v<budgeted_value_t<B_double>, double>);
static_assert(std::is_same_v<budgeted_value_t<B_uint64>, std::uint64_t>);

// Layout invariant — runtime grade adds 16 bytes (two uint64_t).
// On uint64_t the layout is exact (no padding needed).
static_assert(sizeof(B_int)    >= sizeof(int)    + 16);
static_assert(sizeof(B_double) >= sizeof(double) + 16);
static_assert(sizeof(B_uint64) == 24);

}  // namespace detail::is_budgeted_self_test

inline bool is_budgeted_smoke_test() noexcept {
    using namespace detail::is_budgeted_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_budgeted_v<B_int>;
        ok = ok && is_budgeted_v<B_double>;
        ok = ok && !is_budgeted_v<int>;
        ok = ok && IsBudgeted<B_int&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
