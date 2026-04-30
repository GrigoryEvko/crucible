#pragma once

// ── crucible::safety::extract::is_recipe_spec_v ─────────────────────
//
// FOUND-D30 (eighth and FINAL of batch — fourth product wrapper).
// Wrapper-detection predicate for `RecipeSpec<T>`.
//
// Mechanical extension of the IsBudgeted/IsEpochVersioned/
// IsNumaPlacement product-wrapper template.  RecipeSpec carries a
// (Tolerance, RecipeFamily) runtime grade pair where both axes are
// 1-byte enums — total grade overhead = 2 bytes (no alignment pad).
// This is the SMALLEST product wrapper in the D30 batch (compared to
// 16-byte Budgeted/EpochVersioned and 40-byte NumaPlacement).
//
// Production semantics: Forge Phase E pins each compiled kernel's
// numerical recipe at lower-time so consumers can route through the
// matching reduction path (Linear / Pairwise / Kahan / BlockStable).
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_recipe_spec_v<T>          Variable template; cv-ref-stripped.
//   IsRecipeSpec<T>              Concept form.
//   recipe_spec_value_t<T>       Wrapped element type; constrained.

#include <crucible/safety/RecipeSpec.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_recipe_spec_impl : std::false_type {
    using value_type = void;
};

template <typename U>
struct is_recipe_spec_impl<::crucible::safety::RecipeSpec<U>>
    : std::true_type
{
    using value_type = U;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_recipe_spec_v =
    detail::is_recipe_spec_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsRecipeSpec = is_recipe_spec_v<T>;

template <typename T>
    requires is_recipe_spec_v<T>
using recipe_spec_value_t =
    typename detail::is_recipe_spec_impl<
        std::remove_cvref_t<T>>::value_type;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_recipe_spec_self_test {

using RS_int      = ::crucible::safety::RecipeSpec<int>;
using RS_double   = ::crucible::safety::RecipeSpec<double>;
using RS_char     = ::crucible::safety::RecipeSpec<char>;
using RS_uint64   = ::crucible::safety::RecipeSpec<std::uint64_t>;

static_assert(is_recipe_spec_v<RS_int>);
static_assert(is_recipe_spec_v<RS_double>);
static_assert(is_recipe_spec_v<RS_char>);
static_assert(is_recipe_spec_v<RS_uint64>);

static_assert(is_recipe_spec_v<RS_int&>);
static_assert(is_recipe_spec_v<RS_int const&>);

static_assert(!is_recipe_spec_v<int>);
static_assert(!is_recipe_spec_v<int*>);
static_assert(!is_recipe_spec_v<void>);

struct LookalikeRecipeSpec {
    int value;
    ::crucible::safety::Tolerance tol_field;
    ::crucible::safety::RecipeFamily fam_field;
};
static_assert(!is_recipe_spec_v<LookalikeRecipeSpec>);

static_assert(!is_recipe_spec_v<RS_int*>);

static_assert(IsRecipeSpec<RS_int>);
static_assert(!IsRecipeSpec<int>);

static_assert(std::is_same_v<recipe_spec_value_t<RS_int>,    int>);
static_assert(std::is_same_v<recipe_spec_value_t<RS_double>, double>);
static_assert(std::is_same_v<recipe_spec_value_t<RS_uint64>, std::uint64_t>);

// Layout invariant — SMALLEST product wrapper grade in the D30 batch.
// 2-byte runtime grade pair (1 byte Tolerance + 1 byte RecipeFamily).
static_assert(sizeof(RS_int)    >= sizeof(int)    + 2);
static_assert(sizeof(RS_double) >= sizeof(double) + 2);

}  // namespace detail::is_recipe_spec_self_test

inline bool is_recipe_spec_smoke_test() noexcept {
    using namespace detail::is_recipe_spec_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_recipe_spec_v<RS_int>;
        ok = ok && is_recipe_spec_v<RS_double>;
        ok = ok && !is_recipe_spec_v<int>;
        ok = ok && IsRecipeSpec<RS_int&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
