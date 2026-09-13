#pragma once

#include <crucible/safety/NumericalTier.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::Tolerance;

namespace detail {

template <typename T>
struct is_numerical_tier_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_tier = false;
};

template <Tolerance T_at, typename U>
struct is_numerical_tier_impl<::crucible::safety::NumericalTier<T_at, U>> : std::true_type {
    using value_type = U;
    static constexpr Tolerance tier = T_at;
    static constexpr bool has_tier = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_numerical_tier_v = detail::is_numerical_tier_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsNumericalTier = is_numerical_tier_v<T>;

// The two extractors are constrained rather than left to the primary
// template, which would hand back void and an undefined tier for an unrelated
// argument instead of failing.

template <typename T>
    requires is_numerical_tier_v<T>
using numerical_tier_value_t = typename detail::is_numerical_tier_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_numerical_tier_v<T>
inline constexpr Tolerance numerical_tier_v = detail::is_numerical_tier_impl<std::remove_cvref_t<T>>::tier;

namespace detail::is_numerical_tier_self_test {

using NT_int_bitexact = ::crucible::safety::NumericalTier<Tolerance::BITEXACT, int>;
using NT_double_relaxed = ::crucible::safety::NumericalTier<Tolerance::RELAXED, double>;
using NT_int_fp32 = ::crucible::safety::NumericalTier<Tolerance::ULP_FP32, int>;

static_assert(is_numerical_tier_v<NT_int_bitexact>);
static_assert(is_numerical_tier_v<NT_double_relaxed>);
static_assert(is_numerical_tier_v<NT_int_fp32>);

static_assert(is_numerical_tier_v<NT_int_bitexact&>);
static_assert(is_numerical_tier_v<NT_int_bitexact&&>);
static_assert(is_numerical_tier_v<NT_int_bitexact const&>);
static_assert(is_numerical_tier_v<NT_int_bitexact const>);
static_assert(is_numerical_tier_v<NT_int_bitexact const&&>);

static_assert(!is_numerical_tier_v<int>);
static_assert(!is_numerical_tier_v<int*>);
static_assert(!is_numerical_tier_v<int&>);
static_assert(!is_numerical_tier_v<void>);

struct LookalikeNumericalTier {
    int value;
    Tolerance tier;
};
static_assert(!is_numerical_tier_v<LookalikeNumericalTier>);

static_assert(!is_numerical_tier_v<NT_int_bitexact*>);

static_assert(IsNumericalTier<NT_int_bitexact>);
static_assert(IsNumericalTier<NT_int_bitexact&&>);
static_assert(!IsNumericalTier<int>);

static_assert(std::is_same_v<numerical_tier_value_t<NT_int_bitexact>, int>);
static_assert(std::is_same_v<numerical_tier_value_t<NT_double_relaxed>, double>);

static_assert(std::is_same_v<numerical_tier_value_t<NT_int_bitexact const&>, int>);
static_assert(std::is_same_v<numerical_tier_value_t<NT_int_bitexact&&>, int>);

static_assert(numerical_tier_v<NT_int_bitexact> == Tolerance::BITEXACT);
static_assert(numerical_tier_v<NT_double_relaxed> == Tolerance::RELAXED);
static_assert(numerical_tier_v<NT_int_fp32> == Tolerance::ULP_FP32);
static_assert(numerical_tier_v<NT_int_bitexact const&> == Tolerance::BITEXACT);

static_assert(std::is_same_v<numerical_tier_value_t<NT_int_bitexact>, numerical_tier_value_t<NT_int_fp32>>);
static_assert(numerical_tier_v<NT_int_bitexact> != numerical_tier_v<NT_int_fp32>);

}  // namespace detail::is_numerical_tier_self_test

inline bool is_numerical_tier_smoke_test() noexcept {
    using namespace detail::is_numerical_tier_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_numerical_tier_v<NT_int_bitexact>;
        ok = ok && !is_numerical_tier_v<int>;
        ok = ok && IsNumericalTier<NT_int_bitexact&&>;
        ok = ok && (numerical_tier_v<NT_int_bitexact> == Tolerance::BITEXACT);
    }
    return ok;
}

}  // namespace crucible::safety::extract
